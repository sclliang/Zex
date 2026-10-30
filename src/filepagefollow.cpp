#include "mainwindow.h"
#include <QtConcurrentRun>
#include <QFutureWatcher>
#include <QLabel>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

void FilePage::setupFollowUi(QVBoxLayout* layout) {
    followBar_=new QWidget;followBar_->setObjectName("followBar");auto* row=new QHBoxLayout(followBar_);row->setContentsMargins(14,4,10,4);
    followLabel_=new QLabel;followLabel_->setObjectName("followStatus");followLabel_->setTextFormat(Qt::PlainText);followLabel_->setWordWrap(true);row->addWidget(followLabel_,1);
    resumeFollow_=new QToolButton;resumeFollow_->setObjectName("resumeFollow");resumeFollow_->setText(QStringLiteral("回到底部"));row->addWidget(resumeFollow_);
    stopFollow_=new QToolButton;stopFollow_->setText(QStringLiteral("停止跟随"));stopFollow_->setObjectName("stopFollow");row->addWidget(stopFollow_);
    connect(resumeFollow_,&QToolButton::clicked,this,&FilePage::resumeFollowing);connect(stopFollow_,&QToolButton::clicked,this,[this]{setFollowing(false);});
    layout->insertWidget(1,followBar_);followBar_->hide();followTimer_.setInterval(500);connect(&followTimer_,&QTimer::timeout,this,&FilePage::pollFollow);
}
void FilePage::refreshFollowUi() {
    if(!followBar_)return;
    followBar_->setVisible(following_ || !followNotice_.isEmpty());resumeFollow_->setVisible(following_ && followPaused_);
    stopFollow_->setVisible(following_);
    followLabel_->setText(!followNotice_.isEmpty()?followNotice_:followPaused_?QStringLiteral("跟随已暂停 · 保持当前阅读位置"):QStringLiteral("实时跟随 · 自动显示新增内容"));
    emit followingChanged();
}
void FilePage::setFollowing(bool on) {
    if(on==following_) {if(!on){followNotice_.clear();refreshFollowUi();}return;}
    if(on && loading)return;
    if(on && !followBar_)setupFollowUi(qobject_cast<QVBoxLayout*>(layout()));
    if(followControl_)followControl_->canceled.store(true);
    ++followGeneration_;followControl_.reset();logReader_.reset();followBusy_=false;following_=on;followPaused_=false;followInitializing_=on;followNotice_.clear();
    followTimer_.stop();
    if(on) {ensureEditor();logReader_=std::make_shared<LogReader>(encoding_);followNotice_=QStringLiteral("正在连接文件…");followTimer_.start();pollFollow();}
    refreshFollowUi();
}
void FilePage::trackFollowPosition() {
    if(!following_ || !ready || applyingFollow_ || followInitializing_ || loading)return;
    const bool paused=!editor->atDocumentEnd();if(paused==followPaused_)return;
    followPaused_=paused;followNotice_.clear();refreshFollowUi();
    if(!paused)pollFollow();
}
void FilePage::resumeFollowing() {
    if(!following_)return;
    followPaused_=false;followNotice_.clear();applyingFollow_=true;if(ready)editor->scrollToEnd();applyingFollow_=false;
    refreshFollowUi();pollFollow();
}
void FilePage::pollFollow() {
    if(!following_ || followBusy_ || followPaused_ || loading || !isVisible())return;
    followBusy_=true;followControl_=std::make_shared<LoadControl>();
    const auto control=followControl_;const auto reader=logReader_;const auto generation=followGeneration_;
    auto* future=new QFutureWatcher<LogUpdate>(this);
    connect(future,&QFutureWatcher<LogUpdate>::finished,this,[this,future,generation] {
        future->deleteLater();if(generation!=followGeneration_)return;
        auto result=std::make_shared<LogUpdate>(future->result());
        if(result->failed){setFollowing(false);followNotice_=result->notice;refreshFollowUi();return;}
        if(result->waiting){followBusy_=false;followNotice_=result->notice;refreshFollowUi();return;}
        // A rotated/replaced file must not replace the text the user is reading while paused.
        if(result->reset && followPaused_){followBusy_=false;followInitializing_=false;logReader_=std::make_shared<LogReader>(encoding_);followNotice_=QStringLiteral("文件已更新，回到底部后载入新内容");refreshFollowUi();return;}
        applyLogUpdate(result,generation);
    });
    future->setFuture(QtConcurrent::run([reader,control,filename=path]{return reader->poll(filename,control);}));
}
void FilePage::applyLogUpdate(const std::shared_ptr<LogUpdate>& update,quint64 generation,qsizetype offset) {
    if(generation!=followGeneration_)return;
    applyingFollow_=true;
    if(update->reset) {
        if(!update->replacement.document){applyingFollow_=false;setFollowing(false);followNotice_=QStringLiteral("无法准备日志文档，请重新开启跟随。");refreshFollowUi();return;}
        pendingPosition_.reset();editor->setPreparedContent(update->replacement,path);stack_->setCurrentWidget(editor);
        ready=true;attempted=true;stale=false;canceled_=false;loadedEncoding_=encoding_;
        {const auto previous=bookmarks_;editor->setBookmarks(bookmarks_);bookmarks_=editor->bookmarks();if(previous!=bookmarks_)emit bookmarksChanged();}
    } else if(offset<update->appended.size()) {
        auto end=qMin<qsizetype>(offset+64*1024,update->appended.size());
        // Keep every visible intermediate document valid UTF-8.
        while(end<update->appended.size() && (static_cast<unsigned char>(update->appended[end])&0xc0)==0x80)--end;
        if(!editor->appendLogText(update->appended.mid(offset,end-offset),path,update->content.large)) {
            applyingFollow_=false;setFollowing(false);followNotice_=QStringLiteral("已停止跟随：内存不足。原文件未被修改。");refreshFollowUi();return;
        }
        offset=end;
    }
    if(ready && !followPaused_ && isVisible())editor->scrollToEnd();
    applyingFollow_=false;
    if(!update->reset && offset<update->appended.size()) {QTimer::singleShot(1,this,[this,update,generation,offset]{applyLogUpdate(update,generation,offset);});return;}
    content=update->content;stale=false;followNotice_.clear();
    QTimer::singleShot(0,this,[this,update,generation] {
        if(generation!=followGeneration_)return;
        applyingFollow_=true;if(ready && !followPaused_ && isVisible())editor->scrollToEnd();applyingFollow_=false;
        followInitializing_=false;followBusy_=false;refreshFollowUi();
        if(update->reset)emit loaded();
        if(update->more)QTimer::singleShot(10,this,&FilePage::pollFollow);
    });
}
