#include "mainwindow.h"
#include "theme.h"
#include "appearancedialog.h"
#include "fileicons.h"
#include "filepicker.h"
#include "searchresults.h"
#include "integrationdialog.h"
#include "windowplacement.h"
#include <QtConcurrentRun>
#include <QApplication>
#include <QActionGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGraphicsDropShadowEffect>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QProgressBar>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QStackedWidget>
#include <QSplitter>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

namespace {
QToolButton* tool(QWidget* parent, const QString& icon, const QString& tip, const QSize& size = QSize(24,24)) {
    auto* button = new QToolButton(parent);
    button->setProperty("glyph",icon);
    button->setIcon(Theme::icon(icon,Theme::colors().muted));button->setIconSize({16,16}); button->setToolTip(tip); button->setAccessibleName(tip);
    button->setFixedSize(size); button->setCursor(Qt::PointingHandCursor); button->setFocusPolicy(Qt::NoFocus);
    return button;
}
QLabel* label(const QString& text, const char* name = "muted") {
    auto* result = new QLabel(text); result->setObjectName(name); return result;
}
QLabel* glyphLabel(const QString& glyph, const QString& role="muted", int size=18) {
    auto* result=label({});result->setProperty("glyph",glyph);result->setProperty("glyphRole",role);result->setProperty("glyphSize",size);return result;
}
QString normalized(const QString& value) {
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(value).absoluteFilePath()));
}
}

FilePage::FilePage(const QString& filename, QWidget* parent) : QWidget(parent), path(filename) {
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
    loadBar_=new QWidget;loadBar_->setObjectName("loadBar");
    auto* bar=new QHBoxLayout(loadBar_);bar->setContentsMargins(14,6,12,6);bar->setSpacing(12);
    loadLabel_=label({});loadLabel_->setMinimumWidth(155);bar->addWidget(loadLabel_);
    loadProgress_=new QProgressBar;loadProgress_->setObjectName("loadProgress");loadProgress_->setRange(0,100);loadProgress_->setTextVisible(false);loadProgress_->setFixedHeight(5);bar->addWidget(loadProgress_,1);
    cancel_=new QToolButton;cancel_->setObjectName("cancelLoad");cancel_->setText(QStringLiteral("取消"));cancel_->setAccessibleName(QStringLiteral("取消文件加载"));bar->addWidget(cancel_);
    layout->addWidget(loadBar_);loadBar_->hide();
    stack_ = new QStackedWidget(this); layout->addWidget(stack_);
    messagePage_=new QWidget;auto* body=new QVBoxLayout(messagePage_);body->addStretch();
    message_ = label(QStringLiteral("切换到此标签时载入文件")); message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true); message_->setMargin(40); message_->setTextFormat(Qt::PlainText);
    body->addWidget(message_);retry_=new QPushButton(QStringLiteral("重新加载"));retry_->setObjectName("retryLoad");body->addWidget(retry_,0,Qt::AlignHCenter);retry_->hide();body->addStretch();
    stack_->addWidget(messagePage_);
    progressTimer_.setInterval(40);connect(&progressTimer_,&QTimer::timeout,this,&FilePage::updateLoadProgress);
    connect(cancel_,&QToolButton::clicked,this,&FilePage::cancelLoad);connect(retry_,&QPushButton::clicked,this,&FilePage::reload);
}

FilePage::~FilePage() { if(control_)control_->canceled.store(true);if(followControl_)followControl_->canceled.store(true); }

void FilePage::updateLoadProgress() {
    if(!loading || !control_)return;
    const auto stage=control_->stage.load();
    const auto caption=stage==LoadStage::Reading?QStringLiteral("读取文件"):stage==LoadStage::Decoding?QStringLiteral("解析文本"):QStringLiteral("准备显示");
    const int percent=loadProgress();loadProgress_->setValue(percent);loadLabel_->setText(QStringLiteral("%1 · %2%").arg(caption).arg(percent));
    loadProgress_->setAccessibleName(loadLabel_->text());
    if(!ready)message_->setText(caption+QStringLiteral("…"));
}

void FilePage::cancelLoad() {
    if(!loading || committing_)return;
    if(control_)control_->canceled.store(true);control_.reset();++loadGeneration_;
    loading=false;canceled_=true;progressTimer_.stop();loadBar_->hide();
    if(ready){encoding_=loadedEncoding_;pendingPosition_.reset();stack_->setCurrentWidget(editor);}
    else {message_->setText(QStringLiteral("已取消加载\n\n点击“重新加载”继续打开这个文件。"));retry_->show();stack_->setCurrentWidget(messagePage_);}
    emit loadingChanged();
}

void FilePage::ensureEditor() {
    if (editor) return;
    editor = new CodeEditor; editor->setAppearance(appearance_); editor->setWordWrap(wrap_); stack_->addWidget(editor);
    connect(editor,&CodeEditor::verticalPositionChanged,this,&FilePage::trackFollowPosition);
    connect(editor,&CodeEditor::bookmarksChanged,this,[this]{bookmarks_=editor->bookmarks();emit bookmarksChanged();});
    emit editorCreated();
}
void FilePage::setAppearance(const EditorAppearance& appearance) { appearance_=appearance; if(editor)editor->setAppearance(appearance); }
void FilePage::setWordWrap(bool on) { wrap_=on; if(editor)editor->setWordWrap(on); }
void FilePage::setEncoding(const QString& encoding) {
    const auto next=normalizedTextEncoding(encoding);if(next==encoding_)return;
    if(loading)cancelLoad();encoding_=next;if(attempted)reload();
}
void FilePage::setReadingPosition(const ReadingPosition& position) { pendingPosition_=position; }
QVector<qint64> FilePage::bookmarks() const {return ready && editor?editor->bookmarks():bookmarks_;}
void FilePage::setBookmarks(const QVector<qint64>& lines){const auto previous=bookmarks();bookmarks_=lines;if(ready && editor){const QSignalBlocker blocker(editor);editor->setBookmarks(lines);bookmarks_=editor->bookmarks();}if(previous!=bookmarks_)emit bookmarksChanged();}
ReadingPosition FilePage::readingPosition() const {
    if(pendingPosition_)return *pendingPosition_;
    return ready && editor ? editor->readingPosition() : ReadingPosition{};
}
void FilePage::applyPendingPosition() {
    if(!ready || !editor || !pendingPosition_ || !isVisible())return;
    editor->restoreReadingPosition(*pendingPosition_); pendingPosition_.reset();
}
void FilePage::activate() {
    if(following_){pollFollow();return;}
    if(!attempted)reload();
    else if(ready)QTimer::singleShot(0,this,&FilePage::applyPendingPosition);
}

void FilePage::reload() {
    if(following_)setFollowing(false);
    if (loading) return;
    if(ready && !pendingPosition_)pendingPosition_=editor->readingPosition();
    ensureEditor(); loading = true; attempted = true;canceled_=false;committing_=false;
    const auto generation=++loadGeneration_;control_=std::make_shared<LoadControl>();const auto control=control_;
    auto* future = new QFutureWatcher<PreparedFile>(this);
    message_->setText(QStringLiteral("正在读取文件…"));
    retry_->hide();cancel_->setEnabled(true);updateLoadProgress();progressTimer_.start();
    if (!ready) stack_->setCurrentWidget(messagePage_);
    QTimer::singleShot(120,this,[this,generation]{if(loading && loadGeneration_==generation)loadBar_->show();});
    const auto encoding=encoding_;
    connect(future,&QFutureWatcher<PreparedFile>::finished,this,[this,future,encoding,generation] {
        future->deleteLater();
        if(generation!=loadGeneration_)return;
        auto prepared=future->result();
        if(encoding!=encoding_){loading=false;control_.reset();reload();return;}
        if(prepared.content.canceled){cancelLoad();return;}
        if(prepared.content.error.isEmpty() && !prepared.document)prepared.content.error=QStringLiteral("文档准备失败，请重试。");
        content=prepared.content;
        if (content.error.isEmpty()) {
            committing_=true;cancel_->setEnabled(false);editor->setPreparedContent(prepared,path);
            stack_->setCurrentWidget(editor); ready = true; stale = false;loadedEncoding_=encoding;
            {const auto previous=bookmarks_;editor->setBookmarks(bookmarks_);bookmarks_=editor->bookmarks();if(previous!=bookmarks_)emit bookmarksChanged();}
            // Wait for the visible editor's layout before translating document lines to wrapped lines.
            QTimer::singleShot(0,this,[this,encoding,generation]{
                if(generation!=loadGeneration_)return;
                committing_=false;
                if(encoding!=encoding_){loading=false;control_.reset();reload();return;}
                applyPendingPosition();loading=false;control_.reset();progressTimer_.stop();loadBar_->hide();emit loadingChanged();emit loaded();
            });
        } else {
            message_->setText(QStringLiteral("无法打开这个文件\n\n%1\n\n可点击底部编码切换，或通过“文件 → 重新载入”重试。").arg(content.error));
            stack_->setCurrentWidget(messagePage_);retry_->show();ready = false;
            loading=false;control_.reset();progressTimer_.stop();loadBar_->hide();emit loadingChanged();emit loaded();
        }
    });
    future->setFuture(QtConcurrent::run([filename=path,encoding,control] {return prepareFile(filename,encoding,control);}));
    emit loadingChanged();
}

FileTabs::FileTabs(QWidget* parent) : QTabBar(parent) {
    setObjectName("fileTabs"); setExpanding(false); setMovable(true); setDrawBase(false);
    setElideMode(Qt::ElideMiddle); setUsesScrollButtons(true); setMouseTracking(true);
    setFixedHeight(34); setFocusPolicy(Qt::NoFocus);
    setSelectionBehaviorOnRemove(QTabBar::SelectPreviousTab);
}
QSize FileTabs::tabSizeHint(int index) const { return {qBound(112,fontMetrics().horizontalAdvance(tabText(index))+62,220),34}; }
bool FileTabs::isPinned(int index) const {
    auto* page=qobject_cast<FilePage*>(tabData(index).value<QObject*>());return page && page->pinned;
}
void FileTabs::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    for (int i=0; i<count(); ++i) {
        auto rect=tabRect(i); if (!rect.intersects(this->rect())) continue;
        bool active=i==currentIndex();
        painter.fillRect(rect,active ? Theme::colors().background : i==hovered_ ? Theme::colors().hover : Theme::colors().chrome);
        if (active) painter.fillRect(rect.x(),rect.y(),rect.width(),1,Theme::colors().accent);
        painter.setPen(Theme::colors().border); painter.drawLine(rect.topRight(),rect.bottomRight());
        tabIcon(i).paint(&painter,QRect(rect.left()+11,rect.top()+10,14,14));
        painter.setPen(active ? Theme::colors().text : Theme::colors().muted);
        auto textRect=rect.adjusted(32,0,-28,0);
        painter.drawText(textRect,Qt::AlignVCenter|Qt::AlignLeft,fontMetrics().elidedText(tabText(i),Qt::ElideMiddle,textRect.width()));
        if(isPinned(i))Theme::icon("pin",Theme::colors().muted).paint(&painter,QRect(rect.right()-23,rect.top()+9,16,16));
        else if (active || i==hovered_) Theme::icon("close",Theme::colors().muted).paint(&painter,QRect(rect.right()-23,rect.top()+9,16,16));
    }
}
void FileTabs::mousePressEvent(QMouseEvent* event) {
    auto index=tabAt(event->position().toPoint());
    if (event->button()==Qt::LeftButton && index>=0 && !isPinned(index) && event->position().x()>=tabRect(index).right()-30) { closePressed_=index; event->accept(); return; }
    closePressed_=-1; QTabBar::mousePressEvent(event);
}
void FileTabs::mouseReleaseEvent(QMouseEvent* event) {
    auto index=tabAt(event->position().toPoint());
    if (index>=0 && !isPinned(index) && (event->button()==Qt::MiddleButton || (event->button()==Qt::LeftButton && closePressed_==index && event->position().x()>=tabRect(index).right()-30))) {
        closePressed_=-1; emit tabCloseRequested(index); return;
    }
    closePressed_=-1; QTabBar::mouseReleaseEvent(event);
}
void FileTabs::mouseMoveEvent(QMouseEvent* event) { hovered_=tabAt(event->position().toPoint()); update(); QTabBar::mouseMoveEvent(event); }
void FileTabs::leaveEvent(QEvent* event) { hovered_=-1; update(); QTabBar::leaveEvent(event); }

MainWindow::MainWindow(bool testMode, const QString& sessionFile) : settings_("sixunited","Zex"),
    sessionStore_(!sessionFile.isEmpty() ? sessionFile : testMode ? QString() : SessionStore::defaultPath()), testMode_(testMode) {
    setWindowTitle("Zex"); setWindowIcon(QIcon(":/zex.ico"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint);
    setMinimumSize(740,460); resize(1180,780); setAcceptDrops(true);
    wrap_=testMode ? false : settings_.value("wordWrap",false).toBool();
    appearance_=testMode ? EditorAppearance::defaults() : EditorAppearance::fromSettings(settings_);
    FileIcons::setTheme(testMode ? "material" : settings_.value("iconTheme","material").toString());
    if (!testMode) recent_=settings_.value("recentFiles").toStringList().mid(0,50);
    auto* shell = new QWidget; shell->setObjectName("shell"); setCentralWidget(shell);
    auto* root = new QVBoxLayout(shell); root->setContentsMargins(0,0,0,0); root->setSpacing(0);
    buildTitleBar(root);
    auto* strip = new QWidget; strip->setObjectName("tabstrip");
    auto* stripLayout=new QHBoxLayout(strip); stripLayout->setContentsMargins(0,0,6,0); stripLayout->setSpacing(3);
    auto* welcomeTab=label(QStringLiteral("欢迎"),"welcomeTab");welcomeTab->setFixedSize(112,34);stripLayout->addWidget(welcomeTab);
    tabs_=new FileTabs; stripLayout->addWidget(tabs_,1);
    allTabsButton_=tool(strip,"down",QStringLiteral("全部标签"));allTabsButton_->setObjectName("allTabsButton");stripLayout->addWidget(allTabsButton_);
    connect(allTabsButton_,&QToolButton::clicked,this,[this]{showFilePicker(true);});
    auto* open=tool(strip,"plus",QStringLiteral("打开文件 (Ctrl+O)")); stripLayout->addWidget(open);
    auto* find=tool(strip,"search",QStringLiteral("搜索 (Ctrl+F)"));find->setObjectName("findButton");stripLayout->addWidget(find);
    splitButton_=tool(strip,"split",QStringLiteral("左右分屏 (Ctrl+\\)"));splitButton_->setObjectName("splitButton");splitButton_->setCheckable(true);stripLayout->addWidget(splitButton_);connect(splitButton_,&QToolButton::toggled,this,[this](bool on){setSplitEnabled(on);});
    root->addWidget(strip);
    connect(open,&QToolButton::clicked,this,&MainWindow::openFiles);
    connect(find,&QToolButton::clicked,this,&MainWindow::showSearch);
    connect(tabs_,&QTabBar::currentChanged,this,&MainWindow::selectTab);
    connect(tabs_,&QTabBar::tabCloseRequested,this,&MainWindow::closeTab);
    connect(tabs_,&QTabBar::tabMoved,this,[this]{normalizePinnedTabs();scheduleSessionSave();});
    tabs_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabs_,&QWidget::customContextMenuRequested,this,[this](QPoint point) {
        int index=tabs_->tabAt(point); if(index<0) return;
        QMenu menu(this);
        auto* pin=menu.addAction(isTabPinned(index)?QStringLiteral("取消固定标签"):QStringLiteral("固定标签"));menu.addSeparator();
        auto* close=menu.addAction(QStringLiteral("关闭标签"));
        close->setEnabled(!isTabPinned(index));
        auto* others=menu.addAction(QStringLiteral("关闭其他标签"));
        auto* right=menu.addAction(QStringLiteral("关闭右侧标签"));
        auto* all=menu.addAction(QStringLiteral("关闭全部标签"));
        menu.addSeparator(); auto* copy=menu.addAction(QStringLiteral("复制完整路径"));
        auto* chosen=menu.exec(tabs_->mapToGlobal(point));
        if(chosen==pin)setTabPinned(index,!isTabPinned(index));
        else if(chosen==close) closeTab(index);
        else if(chosen==others)closeOtherTabs(index);
        else if(chosen==right)closeTabsToRight(index);
        else if(chosen==all) closeAllTabs();
        else if(chosen==copy) { if(auto* p=pageAt(index)) QApplication::clipboard()->setText(QDir::toNativeSeparators(p->path)); }
    });
    auto* breadcrumb=new QWidget; breadcrumb->setObjectName("breadcrumb"); breadcrumb->setFixedHeight(26);
    auto* pathLayout=new QHBoxLayout(breadcrumb); pathLayout->setContentsMargins(12,0,8,0); pathLayout->setSpacing(6);
    fileIcon_=label({}); fileIcon_->setFixedSize(14,14);
    pathLayout->addWidget(fileIcon_); pathLabel_=label(QStringLiteral("文件阅读器"),"path");
    pathLabel_->setMinimumWidth(0); pathLabel_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred); pathLabel_->setTextFormat(Qt::PlainText);
    pathLayout->addWidget(pathLabel_,1);
    reloadButton_=new QToolButton; reloadButton_->setText(QStringLiteral("文件已变化 · 重新载入")); reloadButton_->hide();
    pathLayout->addWidget(reloadButton_); connect(reloadButton_,&QToolButton::clicked,this,[this]{if(auto* p=currentPage())p->reload();});
    followButton_=new QToolButton;followButton_->setObjectName("followFileButton");followButton_->setText(QStringLiteral("跟随文件"));followButton_->setCheckable(true);followButton_->setToolTip(QStringLiteral("自动读取日志新增内容；向上滚动暂停，回到底部继续"));followButton_->setAccessibleName(QStringLiteral("跟随文件"));pathLayout->addWidget(followButton_);
    connect(followButton_,&QToolButton::toggled,this,[this](bool on){if(auto* page=currentPage())page->setFollowing(on);});
    wrapButton_=tool(breadcrumb,"wrap",QStringLiteral("自动换行 (Alt+Z)")); wrapButton_->setCheckable(true); wrapButton_->setChecked(wrap_);
    pathLayout->addWidget(wrapButton_); connect(wrapButton_,&QToolButton::toggled,this,&MainWindow::setWrap);
    root->addWidget(breadcrumb);

    searchbar_=new QWidget; searchbar_->setObjectName("searchbar"); searchbar_->setFixedHeight(36);
    searchLayout_=new QBoxLayout(QBoxLayout::LeftToRight,searchbar_);searchLayout_->setContentsMargins(6,5,5,5);searchLayout_->setSpacing(3);
    auto* field=new QWidget;auto* fieldLayout=new QHBoxLayout(field);fieldLayout->setContentsMargins(0,0,0,0);fieldLayout->setSpacing(2);searchLayout_->addWidget(field,1);
    auto* actions=new QWidget;auto* searchLayout=new QHBoxLayout(actions);searchLayout->setContentsMargins(0,0,0,0);searchLayout->setSpacing(2);searchLayout->addStretch();searchLayout_->addWidget(actions);
    auto* searchShadow=new QGraphicsDropShadowEffect(searchbar_);searchShadow->setBlurRadius(14);searchShadow->setOffset(0,3);searchShadow->setColor(QColor(0,0,0,75));searchbar_->setGraphicsEffect(searchShadow);
    search_=new QLineEdit; search_->setObjectName("searchInput"); search_->setAccessibleName(QStringLiteral("搜索关键词"));search_->setMaxLength(4096);
    search_->setPlaceholderText(QStringLiteral("搜索"));search_->setMinimumWidth(120); search_->installEventFilter(this);
    fieldLayout->addWidget(search_,1);
    case_=tool(searchbar_,"case",QStringLiteral("区分大小写"));case_->setCheckable(true);
    whole_=tool(searchbar_,"whole",QStringLiteral("全字匹配"));whole_->setCheckable(true);
    fieldLayout->addWidget(case_); fieldLayout->addWidget(whole_);
    regexButton_=tool(searchbar_,"regex",QStringLiteral("正则表达式"));regexButton_->setObjectName("regexSearch");regexButton_->setCheckable(true);fieldLayout->addWidget(regexButton_);
    selectionButton_=tool(searchbar_,"selection",QStringLiteral("仅搜索当前选区，范围保持不变"));selectionButton_->setObjectName("selectionSearch");selectionButton_->setCheckable(true);searchLayout->addWidget(selectionButton_);
    resultsButton_=tool(searchbar_,"list",QStringLiteral("匹配结果列表"));resultsButton_->setObjectName("searchResultsButton");resultsButton_->setCheckable(true);searchLayout->addWidget(resultsButton_);
    cancelSearch_=tool(searchbar_,"stop",QStringLiteral("取消搜索"));cancelSearch_->setObjectName("cancelSearch");searchLayout->addWidget(cancelSearch_);
    count_=label(QStringLiteral("输入关键词"));count_->setFixedWidth(64);count_->setStyleSheet("font-size:11px;"); count_->setAlignment(Qt::AlignCenter); searchLayout->addWidget(count_);
    auto* previous=tool(searchbar_,"up",QStringLiteral("上一个 (Shift+Enter)")); auto* next=tool(searchbar_,"down",QStringLiteral("下一个 (Enter)"));
    auto* closeFind=tool(searchbar_,"close",QStringLiteral("关闭搜索 (Esc)")); searchLayout->addWidget(previous); searchLayout->addWidget(next); searchLayout->addWidget(closeFind);
    searchbar_->hide();
    searchDebounce_.setSingleShot(true); searchDebounce_.setInterval(110);
    connect(&searchDebounce_,&QTimer::timeout,this,[this]{runSearch();});
    connect(search_,&QLineEdit::textChanged,this,[this]{searchDebounce_.start();});
    connect(case_,&QToolButton::toggled,this,[this]{runSearch();});connect(whole_,&QToolButton::toggled,this,[this]{runSearch();});connect(regexButton_,&QToolButton::toggled,this,[this]{runSearch();});
    connect(selectionButton_,&QToolButton::toggled,this,&MainWindow::setSelectionSearch);connect(resultsButton_,&QToolButton::toggled,this,&MainWindow::showSearchResults);
    connect(cancelSearch_,&QToolButton::clicked,this,[this]{searchDebounce_.stop();if(auto* p=currentPage();p && p->editor)p->editor->cancelSearch();});
    connect(search_,&QLineEdit::returnPressed,this,[this]{if(searchDebounce_.isActive()){runSearch();return;} if(auto* p=currentPage();p&&p->ready){if(p->editor->searchStopped())runSearch();else p->editor->findNext();}});
    connect(previous,&QToolButton::clicked,this,[this]{if(auto* p=currentPage();p&&p->ready)p->editor->findNext(true);});
    connect(next,&QToolButton::clicked,this,[this]{if(auto* p=currentPage();p&&p->ready)p->editor->findNext();});
    connect(closeFind,&QToolButton::clicked,this,&MainWindow::hideSearch);

    contentSplit_=new QSplitter(Qt::Vertical);contentSplit_->setHandleWidth(3);contentSplit_->setChildrenCollapsible(false);initializePanes();
    editorHost_=new QWidget;editorHost_->setObjectName("editorHost");editorHost_->installEventFilter(this);auto* editorLayout=new QVBoxLayout(editorHost_);editorLayout->setContentsMargins(0,0,0,0);editorLayout->setSpacing(0);editorLayout->addWidget(paneSplit_);
    searchbar_->setParent(editorHost_);contentSplit_->addWidget(editorHost_);root->addWidget(contentSplit_,1);
    welcome_=buildWelcome();pages_->addWidget(welcome_);
    auto* status=new QWidget; status->setObjectName("statusbar"); status->setFixedHeight(23);
    auto* statusLayout=new QHBoxLayout(status); statusLayout->setContentsMargins(8,0,6,0); statusLayout->setSpacing(3);
    auto* lock=glyphLabel("lock","muted",14);statusLayout->addWidget(lock);
    statusLayout->addWidget(label(QStringLiteral("只读"),"status")); fileInfo_=label("Zex 0.8.0","status"); statusLayout->addWidget(fileInfo_);
    encodingButton_=new QToolButton;encodingButton_->setObjectName("encodingButton");encodingButton_->setPopupMode(QToolButton::InstantPopup);encodingButton_->setMenu(encodingMenu_);encodingButton_->setAccessibleName(QStringLiteral("文件编码"));statusLayout->addWidget(encodingButton_);
    sessionNotice_=label(QStringLiteral("会话未保存"),"sessionNotice");sessionNotice_->hide();statusLayout->addWidget(sessionNotice_);
    statusLayout->addStretch(); position_=label({},"status"); language_=label({},"status"); statusLayout->addWidget(position_); statusLayout->addWidget(language_);
    root->addWidget(status);
    sessionSaveTimer_.setSingleShot(true);sessionSaveTimer_.setInterval(1000);
    connect(&sessionSaveTimer_,&QTimer::timeout,this,&MainWindow::saveSession);

    auto shortcut=[this](const QKeySequence& key, auto action){auto* s=new QShortcut(key,this);connect(s,&QShortcut::activated,this,action);};
    shortcut(QKeySequence("Ctrl+P"),[this]{showFilePicker();});
    shortcut(QKeySequence("Ctrl+\\"),[this]{setSplitEnabled(!splitEnabled());});
    shortcut(QKeySequence("Ctrl+F2"),[this]{if(auto* p=currentPage();p && p->ready)p->editor->toggleBookmark();});
    shortcut(QKeySequence("F2"),[this]{if(auto* p=currentPage();p && p->ready)p->editor->nextBookmark();});
    shortcut(QKeySequence("Shift+F2"),[this]{if(auto* p=currentPage();p && p->ready)p->editor->nextBookmark(true);});
    shortcut(QKeySequence::Open,[this]{openFiles();});
    shortcut(QKeySequence::Find,[this]{showSearch();});
    shortcut(QKeySequence("Ctrl+W"),[this]{closeTab(tabs_->currentIndex());});
    shortcut(QKeySequence("Ctrl+Shift+T"),[this]{reopenClosed();});
    shortcut(QKeySequence("Ctrl+Tab"),[this]{if(tabs_->count())tabs_->setCurrentIndex((tabs_->currentIndex()+1)%tabs_->count());});
    shortcut(QKeySequence("Ctrl+Shift+Tab"),[this]{if(tabs_->count())tabs_->setCurrentIndex((tabs_->currentIndex()+tabs_->count()-1)%tabs_->count());});
    shortcut(QKeySequence("Ctrl+G"),[this]{jumpToLine();});
    shortcut(QKeySequence("Alt+Z"),[this]{setWrap(!wrap_);});
    shortcut(QKeySequence("Ctrl+="),[this]{changeFont(1);});
    shortcut(QKeySequence("Ctrl++"),[this]{changeFont(1);});
    shortcut(QKeySequence("Ctrl+-"),[this]{changeFont(-1);});
    shortcut(QKeySequence("Ctrl+0"),[this]{appearance_.points=EditorAppearance::defaults().points;changeFont(0);});
    shortcut(QKeySequence("Ctrl+,"),[this]{showAppearanceSettings();});
    shortcut(QKeySequence("F3"),[this]{if(auto* p=currentPage();p&&p->ready)p->editor->findNext();});
    shortcut(QKeySequence("Shift+F3"),[this]{if(auto* p=currentPage();p&&p->ready)p->editor->findNext(true);});
    shortcut(QKeySequence("F5"),[this]{if(auto* p=currentPage())p->reload();});
    shortcut(QKeySequence("Escape"),[this]{hideSearch();});
    for(int i=1;i<=9;++i) shortcut(QKeySequence(QString("Alt+%1").arg(i)),[this,i]{if(tabs_->count())tabs_->setCurrentIndex(qMin(i-1,tabs_->count()-1));});
    connect(&fileWatcher_,&QFileSystemWatcher::fileChanged,this,[this](const QString& path){
        for(auto* p:findChildren<FilePage*>())if(p->path==path && !p->following()){p->stale=true;if(p==currentPage())refreshStatus();}
    });
    if(!testMode && settings_.contains("geometry")) {
        restoreGeometry(settings_.value("geometry").toByteArray());
    } else if(auto* screen=QGuiApplication::primaryScreen()) move(screen->availableGeometry().center()-rect().center());
    QList<QRect> areas;for(auto* screen:QGuiApplication::screens())areas.append(screen->availableGeometry());
    setGeometry(WindowPlacement::fit(geometry(),areas));
    if(!testMode && settings_.value("maximized",false).toBool()) setWindowState(Qt::WindowMaximized);
    refreshThemeIcons();applyNativeChrome();
    refreshStatus();
    WindowPlacement::keepVisible(this);
}

void MainWindow::buildTitleBar(QVBoxLayout* root) {
    titlebar_=new QWidget; titlebar_->setObjectName("titlebar"); titlebar_->setFixedHeight(34); titlebar_->installEventFilter(this);
    auto* row=new QHBoxLayout(titlebar_); row->setContentsMargins(12,0,0,0); row->setSpacing(2);
    auto* logo=glyphLabel("zex","muted",16);row->addWidget(logo);row->addSpacing(9);
    auto menuButton=[&](const QString& text){auto* b=new QToolButton; b->setText(text);b->setFixedHeight(26);b->setPopupMode(QToolButton::InstantPopup);b->setFocusPolicy(Qt::NoFocus);row->addWidget(b);return b;};
    auto* file=menuButton(QStringLiteral("文件"));auto* view=menuButton(QStringLiteral("查看"));auto* help=menuButton(QStringLiteral("帮助"));
    row->addStretch();
    auto* quick=new QPushButton;quick->setObjectName("quickOpenTitle");quick->setFixedHeight(25);quick->setMaximumWidth(420);quick->setMinimumWidth(180);quick->setFocusPolicy(Qt::NoFocus);quick->setCursor(Qt::PointingHandCursor);quick->setToolTip(QStringLiteral("快速切换文件 (Ctrl+P)"));quick->setAccessibleName(quick->toolTip());
    auto* command=new QHBoxLayout(quick);command->setContentsMargins(9,0,9,0);command->setSpacing(8);auto* search=glyphLabel("search","muted",14);search->setAttribute(Qt::WA_TransparentForMouseEvents);command->addWidget(search);
    title_=label(QStringLiteral("搜索文件"));title_->setMinimumWidth(0);title_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);title_->setTextFormat(Qt::PlainText);title_->setAttribute(Qt::WA_TransparentForMouseEvents);command->addWidget(title_,1);
    auto* key=label("Ctrl P");key->setStyleSheet("font-size:10px;");key->setAttribute(Qt::WA_TransparentForMouseEvents);command->addWidget(key);row->addWidget(quick,3);
    connect(quick,&QPushButton::clicked,this,[this]{showFilePicker();});row->addStretch();row->addSpacing(20);
    auto* minimize=tool(titlebar_,"minimize",QStringLiteral("最小化"),{43,34});minimize->setObjectName("windowButton");
    maximizeButton_=tool(titlebar_,"maximize",QStringLiteral("最大化 / 还原"),{43,34});maximizeButton_->setObjectName("windowButton");
    auto* close=tool(titlebar_,"close",QStringLiteral("关闭窗口"),{43,34});close->setObjectName("windowClose");
    row->addWidget(minimize);row->addWidget(maximizeButton_);row->addWidget(close);
    connect(minimize,&QToolButton::clicked,this,&QWidget::showMinimized);connect(maximizeButton_,&QToolButton::clicked,this,&MainWindow::toggleMaximize);connect(close,&QToolButton::clicked,this,&QWidget::close);
    buildMenus(file,view,help);root->addWidget(titlebar_);
}

void MainWindow::buildMenus(QToolButton* file, QToolButton* view, QToolButton* help) {
    auto* files=new QMenu(this);file->setMenu(files);
    files->addAction(QStringLiteral("打开文件…\tCtrl+O"),this,&MainWindow::openFiles);
    files->addAction(QStringLiteral("快速切换文件…\tCtrl+P"),this,[this]{showFilePicker();});
    recentMenu_=files->addMenu(QStringLiteral("最近打开"));connect(recentMenu_,&QMenu::aboutToShow,this,&MainWindow::updateRecentMenu);
    files->addSeparator();files->addAction(QStringLiteral("重新载入\tF5"),this,[this]{if(auto* p=currentPage())p->reload();});
    encodingMenu_=files->addMenu(QStringLiteral("以编码重新打开"));encodingMenu_->setObjectName("encodingMenu");
    auto* encodings=new QActionGroup(this);encodings->setExclusive(true);
    for(const auto& encoding:textEncodingIds()) {
        auto* action=encodingMenu_->addAction(textEncodingName(encoding));action->setCheckable(true);action->setData(encoding);encodings->addAction(action);
        connect(action,&QAction::triggered,this,[this,encoding]{setCurrentEncoding(encoding);});
    }
    connect(encodingMenu_,&QMenu::aboutToShow,this,[this]{for(auto* action:encodingMenu_->actions()){action->setEnabled(currentPage()!=nullptr);action->setChecked(currentPage() && action->data().toString()==currentPage()->encoding());}});
    files->addAction(QStringLiteral("复制完整路径"),this,[this]{if(auto* p=currentPage())QApplication::clipboard()->setText(QDir::toNativeSeparators(p->path));});
    files->addSeparator();files->addAction(QStringLiteral("关闭标签\tCtrl+W"),this,[this]{closeTab(tabs_->currentIndex());});
    files->addAction(QStringLiteral("恢复关闭的标签\tCtrl+Shift+T"),this,&MainWindow::reopenClosed);
    files->addSeparator();files->addAction(QStringLiteral("退出"),this,&QWidget::close);
    auto* views=new QMenu(this);view->setMenu(views);
    splitAction_=views->addAction(QStringLiteral("左右分屏\tCtrl+\\"));splitAction_->setCheckable(true);connect(splitAction_,&QAction::toggled,this,[this](bool on){setSplitEnabled(on);});
    views->addAction(QStringLiteral("搜索…\tCtrl+F"),this,&MainWindow::showSearch);
    views->addAction(QStringLiteral("跳转到行…\tCtrl+G"),this,&MainWindow::jumpToLine);views->addSeparator();
    auto* bookmarks=views->addMenu(QStringLiteral("书签"));
    bookmarks->addAction(QStringLiteral("添加 / 取消书签\tCtrl+F2"),this,[this]{if(auto* p=currentPage();p && p->ready)p->editor->toggleBookmark();});
    bookmarks->addAction(QStringLiteral("下一个书签\tF2"),this,[this]{if(auto* p=currentPage();p && p->ready)p->editor->nextBookmark();});
    bookmarks->addAction(QStringLiteral("上一个书签\tShift+F2"),this,[this]{if(auto* p=currentPage();p && p->ready)p->editor->nextBookmark(true);});
    bookmarks->addSeparator();bookmarks->addAction(QStringLiteral("清除当前文件书签"),this,[this]{if(auto* p=currentPage())p->setBookmarks({});});
    wrapAction_=views->addAction(QStringLiteral("自动换行\tAlt+Z"));wrapAction_->setCheckable(true);wrapAction_->setChecked(wrap_);connect(wrapAction_,&QAction::toggled,this,&MainWindow::setWrap);
    followAction_=views->addAction(QStringLiteral("跟随文件"));followAction_->setCheckable(true);connect(followAction_,&QAction::toggled,this,[this](bool on){if(auto* page=currentPage())page->setFollowing(on);});
    views->addAction(QStringLiteral("放大字体\tCtrl++"),this,[this]{changeFont(1);});
    views->addAction(QStringLiteral("缩小字体\tCtrl+-"),this,[this]{changeFont(-1);});
    views->addAction(QStringLiteral("重置字号\tCtrl+0"),this,[this]{appearance_.points=EditorAppearance::defaults().points;changeFont(0);});
    views->addSeparator();
    views->addAction(QStringLiteral("外观设置…\tCtrl+,"),this,&MainWindow::showAppearanceSettings);
    views->addAction(QStringLiteral("Windows 集成…"),this,[this]{IntegrationDialog dialog(ShellIntegration(QCoreApplication::applicationFilePath()),this);dialog.exec();});
    auto* helps=new QMenu(this);help->setMenu(helps);
    helps->addAction(QStringLiteral("快捷键"),this,[this]{QMessageBox::information(this,QStringLiteral("Zex · 快捷键"),QStringLiteral("Ctrl+O    打开一个或多个文件\nCtrl+P    快速切换文件\nCtrl+\\    左右分屏\nCtrl+F2    添加 / 取消行书签\nF2 / Shift+F2    下一个 / 上一个书签\nCtrl+F    搜索当前文件\nEnter / Shift+Enter    下一个 / 上一个结果\nF3 / Shift+F3    下一个 / 上一个结果\nEsc    关闭搜索\nCtrl+G    跳转到行\nCtrl+W / 鼠标中键    关闭标签\nCtrl+Shift+T    恢复标签\nCtrl+Tab / Ctrl+Shift+Tab    切换标签\nAlt+1 … Alt+9    切换到指定标签\nAlt+Z    自动换行\nCtrl++ / Ctrl+- / Ctrl+0    调整字号\nCtrl+,    外观设置\nF5    重新载入文件\n\n支持拖入文件、拖动标签排序；右键标签可固定、关闭右侧。固定标签需先取消固定才能关闭。"));});
    helps->addAction(QStringLiteral("关于 Zex"),this,[this]{QMessageBox::about(this,QStringLiteral("关于 Zex"),QStringLiteral("<h2>Zex <small>0.8.0</small></h2><p>专注于文件与代码的桌面阅读器。</p><p>C++ · Qt 6 · Scintilla · Lexilla</p><p>Copyright (c) 2026 sixunited.<br>Author: sclliang</p><p>本版本支持只读查看，不会修改打开的文件。<br>开源组件许可位于 bin/licenses。</p>"));});
}

FilePage* MainWindow::pageAt(int index) const {
    if(index<0 || index>=tabs_->count())return nullptr;
    return qobject_cast<FilePage*>(tabs_->tabData(index).value<QObject*>());
}
FilePage* MainWindow::primaryPage() const {return pages_?qobject_cast<FilePage*>(pages_->currentWidget()):nullptr;}
FilePage* MainWindow::currentPage() const {return rightActive_ && secondaryPage_?secondaryPage_.data():primaryPage();}
int MainWindow::tabCount() const {return tabs_->count();}
bool MainWindow::allLoaded() const {for(int i=0;i<tabs_->count();++i)if(auto* p=pageAt(i);p&&p->loading)return false;return !secondaryPage_ || !secondaryPage_->loading;}

FilePage* MainWindow::addPage(const QString& path, const ReadingPosition& position,const QString& encoding,bool pinned) {
    auto* page=new FilePage(path,pages_);page->setAppearance(appearance_);page->setWordWrap(wrap_);page->setReadingPosition(position);pages_->addWidget(page);
    page->setEncoding(encoding);
    page->pinned=pinned;
    // Adding the first tab emits currentChanged before tabData is assigned; activate only after setup.
    const QSignalBlocker blocker(tabs_);
    int index=tabs_->addTab(Theme::fileIcon(path),QFileInfo(path).fileName());
    tabs_->setTabData(index,QVariant::fromValue(static_cast<QObject*>(page)));tabs_->setTabToolTip(index,QDir::toNativeSeparators(path));
    connectPage(page);return page;
}
void MainWindow::connectPage(FilePage* page) {
    connect(page,&FilePage::loaded,this,[this,page]{
        if(page->ready) {
            if(!fileWatcher_.files().contains(page->path))fileWatcher_.addPath(page->path);
            recent_.removeAll(page->path);recent_.prepend(page->path);recent_=recent_.mid(0,50);
        }
        if(page==currentPage()){refreshStatus();if(searchbar_->isVisible()){runSearch(false);updateSearchLabel();}}
        scheduleSessionSave();
        emit fileReady(page->path);
    });
    connect(page,&FilePage::editorCreated,this,[this,page]{
        connect(page->editor,&CodeEditor::positionChanged,this,[this,page]{if(page==currentPage())refreshStatus();});
        connect(page->editor,&CodeEditor::readingPositionChanged,this,&MainWindow::scheduleSessionSave);
        connect(page->editor,&CodeEditor::searchChanged,this,[this,page]{if(page==currentPage())updateSearchLabel();});
        connect(page->editor,&CodeEditor::fileDropped,this,[this,page](const QString& p){activatePane(page==secondaryPage_,false);openPath(p);});
    });
    connect(page,&FilePage::loadingChanged,this,[this,page]{if(page==currentPage())refreshStatus();scheduleSessionSave();});
    connect(page,&FilePage::followingChanged,this,[this,page]{if(page==currentPage())refreshStatus();scheduleSessionSave();});
    connect(page,&FilePage::bookmarksChanged,this,[this,page]{
        if(syncingBookmarks_)return;QScopedValueRollback guard(syncingBookmarks_,true);
        for(auto* other:findChildren<FilePage*>())if(other!=page && other->path.compare(page->path,Qt::CaseInsensitive)==0)other->setBookmarks(page->bookmarks());
        scheduleSessionSave();
    });
}

FilePage* MainWindow::openPath(const QString& value) {
    const auto path=normalized(value);
    for(int i=0;i<tabs_->count();++i)if(auto* p=pageAt(i);p&&p->path.compare(path,Qt::CaseInsensitive)==0){tabs_->setCurrentIndex(i);selectTab(i);return currentPage();}
    addPage(path);const int index=tabs_->count()-1;tabs_->setCurrentIndex(index);selectTab(index);return currentPage();
}

void MainWindow::openPaths(const QStringList& paths) {
    int last=-1;
    for(const auto& value:paths) {
        const auto path=normalized(value);int found=-1;
        for(int i=0;i<tabs_->count();++i)if(pageAt(i)->path.compare(path,Qt::CaseInsensitive)==0){found=i;break;}
        if(found<0){addPage(path);found=tabs_->count()-1;}
        last=found;
    }
    if(last>=0){{QSignalBlocker blocker(tabs_);tabs_->setCurrentIndex(last);}selectTab(last);}
}
void MainWindow::receiveFiles(const QStringList& paths) {
    openPaths(paths);
    if(isMinimized())setWindowState(windowState() & ~Qt::WindowMinimized);
    show();raise();activateWindow();SetForegroundWindow(reinterpret_cast<HWND>(winId()));
    if(auto* modal=QApplication::activeModalWidget()){modal->raise();modal->activateWindow();}
}
void MainWindow::setCurrentEncoding(const QString& encoding) {
    if(auto* page=currentPage()){page->setEncoding(encoding);refreshStatus();scheduleSessionSave();}
}

void MainWindow::restoreLastSession(const QStringList& startupFiles) {
    if(sessionInitialized_)return;
    restoringSession_=true;
    const auto session=sessionStore_.read();
    QString active=session.activePath;
    for(const auto& tab:session.tabs)addPage(tab.path,tab.position,tab.encoding,tab.pinned)->setBookmarks(tab.bookmarks);
    normalizePinnedTabs();
    for(const auto& filename:startupFiles) {
        const auto path=normalized(filename);bool exists=false;
        for(int i=0;i<tabs_->count();++i)if(pageAt(i)->path.compare(path,Qt::CaseInsensitive)==0){exists=true;break;}
        if(!exists)addPage(path);
        active=path;
    }
    auto left=session.split.enabled?session.split.leftPath:active;
    auto right=session.split.rightPath;
    if(!startupFiles.isEmpty() && session.split.enabled){if(session.split.activeRight)right=active;else left=active;}
    int activeIndex=tabs_->count()?0:-1;
    for(int i=0;i<tabs_->count();++i)if(pageAt(i)->path.compare(left,Qt::CaseInsensitive)==0){activeIndex=i;break;}
    {QSignalBlocker blocker(tabs_);tabs_->setCurrentIndex(activeIndex);}
    if(activeIndex>=0)selectTab(activeIndex);
    if(session.split.enabled) {
        splitPercent_=session.split.leftPercent;
        rightPositions_.insert(session.split.rightPath.toCaseFolded(),session.split.rightPosition);
        rightEncodings_.insert(session.split.rightPath.toCaseFolded(),session.split.rightEncoding);
        setSplitEnabled(true,right);activatePane(session.split.activeRight);
    }
    sessionInitialized_=true;restoringSession_=false;
}

SessionSnapshot MainWindow::sessionSnapshot() const {
    SessionSnapshot session;
    for(int i=0;i<tabs_->count();++i)if(auto* page=pageAt(i))session.tabs.append({page->path,page->readingPosition(),page->encoding(),page->pinned,page->bookmarks()});
    if(auto* page=currentPage())session.activePath=page->path;
    if(secondaryPage_ && primaryPage())session.split={true,rightActive_,primaryPage()->path,secondaryPage_->path,secondaryPage_->readingPosition(),secondaryPage_->encoding(),splitPercent_};
    return session;
}
void MainWindow::scheduleSessionSave() {
    // Throttle continuous scrolling; never serialize or write for every paint/keystroke.
    if(sessionInitialized_ && !restoringSession_ && sessionStore_.enabled() && !sessionSaveTimer_.isActive())sessionSaveTimer_.start();
}
bool MainWindow::saveSession() {
    sessionSaveTimer_.stop();
    if(!sessionInitialized_)return true;
    QString error;const bool saved=sessionStore_.write(sessionSnapshot(),&error);
    sessionNotice_->setVisible(!saved);sessionNotice_->setToolTip(error);
    return saved;
}

bool MainWindow::isTabPinned(int index) const {auto* page=pageAt(index);return page && page->pinned;}
void MainWindow::normalizePinnedTabs() {
    const QSignalBlocker blocker(tabs_);auto* active=pageAt(tabs_->currentIndex());QVector<FilePage*> order;
    for(bool pinned:{true,false})for(int i=0;i<tabs_->count();++i)if(pageAt(i)->pinned==pinned)order.append(pageAt(i));
    for(int i=0;i<order.size();++i)for(int j=i;j<tabs_->count();++j)if(pageAt(j)==order[i]){if(j!=i)tabs_->moveTab(j,i);break;}
    for(int i=0;i<tabs_->count();++i)if(pageAt(i)==active){tabs_->setCurrentIndex(i);break;}
    tabs_->update();
}
void MainWindow::setTabPinned(int index,bool pinned) {
    if(auto* page=pageAt(index)){page->pinned=pinned;normalizePinnedTabs();scheduleSessionSave();}
}
void MainWindow::closeTab(int index) { if(!isTabPinned(index))removeTab(index,true); }
void MainWindow::closeAllTabs() {
    for(int i=tabs_->count()-1;i>=0;--i)if(!isTabPinned(i))removeTab(i,false);
    if(tabs_->count())selectTab(tabs_->currentIndex());
}
void MainWindow::closeOtherTabs(int index) {
    auto* keep=pageAt(index);if(!keep)return;
    for(int i=tabs_->count()-1;i>=0;--i)if(pageAt(i)!=keep && !isTabPinned(i))removeTab(i,false);
    for(int i=0;i<tabs_->count();++i)if(pageAt(i)==keep){tabs_->setCurrentIndex(i);selectTab(i);break;}
}
void MainWindow::closeTabsToRight(int index) {
    if(!pageAt(index))return;
    for(int i=tabs_->count()-1;i>index;--i)if(!isTabPinned(i))removeTab(i,false);
    selectTab(tabs_->currentIndex());
}
void MainWindow::removeTab(int index, bool activateNext) {
    auto* page=pageAt(index);if(!page)return;
    const QSignalBlocker blocker(tabs_);
    if(secondaryPage_ && secondaryPage_->path.compare(page->path,Qt::CaseInsensitive)==0)setSplitEnabled(false);
    closed_.append(page->path);if(closed_.size()>20)closed_.removeFirst();
    if(fileWatcher_.files().contains(page->path))fileWatcher_.removePath(page->path);
    rightPositions_.remove(page->path.toCaseFolded());rightEncodings_.remove(page->path.toCaseFolded());
    tabs_->removeTab(index);pages_->removeWidget(page);delete page;
    if(!tabs_->count()){setSplitEnabled(false);pages_->setCurrentWidget(welcome_);hideSearch();}
    else if(activateNext)selectTab(tabs_->currentIndex());
    refreshStatus();
    scheduleSessionSave();
}

void MainWindow::selectTab(int index) {
    auto* page=pageAt(index);if(!page)return;
    if(rightActive_ && secondaryPage_){setSecondaryPath(page->path);page=secondaryPage_;}
    else {pages_->setCurrentWidget(page);page->activate();}
    if(!primaryPage() && tabs_->count())pages_->setCurrentWidget(pageAt(0));
    if(auto* left=primaryPage())left->activate();
    refreshStatus();
    if(searchbar_->isVisible()){runSearch(false);search_->setFocus();}else if(page->editor)page->editor->setFocus();
    scheduleSessionSave();
}

void MainWindow::openFiles() {
    const auto files=QFileDialog::getOpenFileNames(this,QStringLiteral("打开文件"),settings_.value("lastDirectory").toString(),QStringLiteral("所有文件 (*);;代码与文本 (*.txt *.log *.cpp *.h *.py *.js *.ts *.json *.md *.xml *.yaml *.yml *.rs *.go *.ps1 *.ini)"));
    openPaths(files);
    if(!files.isEmpty()&&!testMode_)settings_.setValue("lastDirectory",QFileInfo(files.first()).absolutePath());
}

void MainWindow::showFilePicker(bool tabsOnly) {
    QVector<FileChoice> opened;
    for(int i=0;i<tabs_->count();++i)if(auto* page=pageAt(i))opened.append({page->path,true,page->pinned});
    FilePickerDialog dialog(mergeFileChoices(opened,tabsOnly?QStringList{}:recent_),tabsOnly,this);
    QRect frame(QPoint(),dialog.size());
    if(tabsOnly)frame.moveTopRight(allTabsButton_->mapToGlobal(QPoint(allTabsButton_->width(),allTabsButton_->height()+4)));
    else frame.moveTopLeft(mapToGlobal(QPoint((width()-dialog.width())/2,qMin(110,height()/5))));
    dialog.setGeometry(WindowPlacement::fit(frame,{screen()->availableGeometry()}));
    if(dialog.exec()==QDialog::Accepted)openPath(dialog.selectedPath());
    else if(auto* page=currentPage();page && page->editor)page->editor->setFocus();
}

void MainWindow::refreshStatus() {
    refreshPaneHeaders();
    const bool hasFile=currentPage()!=nullptr;
    findChild<QWidget*>("breadcrumb")->setVisible(hasFile);findChild<QLabel*>("welcomeTab")->setVisible(!hasFile);findChild<QToolButton*>("findButton")->setEnabled(hasFile);
    allTabsButton_->setEnabled(tabs_->count()>0);
    {const QSignalBlocker button(followButton_),action(followAction_);auto* page=currentPage();
        followButton_->setVisible(page!=nullptr);followButton_->setEnabled(page && !page->loading);followAction_->setEnabled(page && !page->loading);
        followButton_->setChecked(page && page->following());followAction_->setChecked(page && page->following());}
    if(auto* page=currentPage()) {
        encodingButton_->show();encodingButton_->setText((page->loading?textEncodingName(page->encoding())+QStringLiteral("…"):page->ready?page->content.encoding:textEncodingName(page->encoding()))+QStringLiteral(" ▾"));
        encodingButton_->setToolTip(QStringLiteral("%1 · 点击选择编码并重新读取文件").arg(textEncodingName(page->encoding())));
        const QFileInfo info(page->path);auto name=info.fileName();title_->setText(name);title_->setToolTip(QDir::toNativeSeparators(page->path));setWindowTitle(name+" — Zex");
        fileIcon_->setPixmap(Theme::fileIcon(page->path).pixmap(QSize(14,14),devicePixelRatioF()));
        pathLabel_->setText(info.dir().dirName()+QStringLiteral("  ›  ")+name);pathLabel_->setToolTip(QDir::toNativeSeparators(page->path));
        reloadButton_->setText(page->stale?QStringLiteral("文件已变化 · 重新载入"):QStringLiteral("重试打开"));
        reloadButton_->setVisible(page->stale || (page->attempted && !page->ready && !page->loading));
        if(page->ready){fileInfo_->setText(QStringLiteral("%1   ·   %2").arg(page->content.lineEnding,QLocale().formattedDataSize(page->content.originalSize)));position_->setText(page->editor->positionSummary());language_->setText(page->editor->language());}
        else {fileInfo_->setText(page->loading?QStringLiteral("正在读取…"):page->wasCanceled()?QStringLiteral("已取消加载"):QStringLiteral("无法读取"));position_->clear();language_->clear();}
    } else {
        encodingButton_->hide();
        fileIcon_->setPixmap(Theme::icon("file",Theme::colors().subtle).pixmap(QSize(14,14),devicePixelRatioF()));
        title_->setText(QStringLiteral("搜索文件"));setWindowTitle("Zex");pathLabel_->setText(QStringLiteral("文件阅读器"));pathLabel_->setToolTip({});reloadButton_->hide();fileInfo_->setText("Zex 0.8.0");position_->clear();language_->clear();
    }
}

void MainWindow::showSearch() {
    auto* page=currentPage();if(!page||!page->ready)return;
    searchbar_->show();positionSearchBar();searchbar_->raise();const auto selected=page->editor->selectedText();
    if(!selected.isEmpty()&&!selected.contains('\n')&&!selected.contains('\r'))search_->setText(selected);
    runSearch();search_->setFocus();search_->selectAll();
}
void MainWindow::hideSearch() {
    searchDebounce_.stop();searchbar_->hide();
    if(resultsPanel_)resultsPanel_->hide();{QSignalBlocker blocker(resultsButton_);resultsButton_->setChecked(false);}
    for(int i=0;i<tabs_->count();++i)if(auto* p=pageAt(i);p&&p->editor)p->editor->clearSearch();
    if(secondaryPage_ && secondaryPage_->editor)secondaryPage_->editor->clearSearch();
    if(auto* p=currentPage();p&&p->editor)p->editor->setFocus();
}
void MainWindow::runSearch(bool navigate) {
    searchDebounce_.stop();if(!searchbar_->isVisible())return;
    if(auto* page=currentPage();page&&page->ready) {
        search_->setPlaceholderText(splitEnabled()?(rightActive_?QStringLiteral("在右侧搜索"):QStringLiteral("在左侧搜索")):QStringLiteral("搜索"));
        if(selectionEditor_!=page->editor){selectionScope_.reset();selectionEditor_=nullptr;QSignalBlocker blocker(selectionButton_);selectionButton_->setChecked(false);}
        page->editor->setSearch(search_->text(),case_->isChecked(),whole_->isChecked(),regexButton_->isChecked(),selectionScope_,navigate);
        if(resultsModel_)resultsModel_->setEditor(page->editor);
    }
    updateSearchLabel();
}
void MainWindow::updateSearchLabel() {
    auto* page=currentPage();auto* editor=page && page->ready?page->editor:nullptr;
    const auto summary=editor?editor->matchSummary():QStringLiteral("等待文件载入");
    count_->setText(count_->fontMetrics().elidedText(summary,Qt::ElideRight,count_->width()));count_->setToolTip(summary);count_->setAccessibleName(summary);
    cancelSearch_->setEnabled(editor && editor->searchBusy());cancelSearch_->setVisible(editor && editor->searchBusy());search_->setToolTip(editor?editor->searchError():QString());
    if(resultsModel_)resultsModel_->setEditor(editor);
}
void MainWindow::setSelectionSearch(bool on) {
    selectionScope_.reset();selectionEditor_=nullptr;
    if(on)if(auto* page=currentPage();page && page->ready) {
        const auto begin=page->editor->send(SCI_GETSELECTIONSTART),end=page->editor->send(SCI_GETSELECTIONEND);
        if(end>begin){selectionScope_=SearchMatch{begin,end};selectionEditor_=page->editor;}
    }
    if(on && !selectionScope_){QSignalBlocker blocker(selectionButton_);selectionButton_->setChecked(false);count_->setText(QStringLiteral("请先选择文本"));return;}
    runSearch();
}
void MainWindow::showSearchResults(bool on) {
    if(on && !resultsPanel_) {
        resultsPanel_=new QWidget;resultsPanel_->setObjectName("searchResultsPanel");resultsPanel_->setMinimumHeight(90);
        auto* layout=new QVBoxLayout(resultsPanel_);layout->setContentsMargins(10,4,10,4);layout->setSpacing(2);
        auto* row=new QHBoxLayout;auto* heading=label(QStringLiteral("搜索结果"),"sectionHeading");heading->setToolTip(QStringLiteral("点击结果跳转，最多列出 10,000 项"));row->addWidget(heading);row->addStretch();auto* close=tool(resultsPanel_,"close",QStringLiteral("收起结果列表"));row->addWidget(close);connect(close,&QToolButton::clicked,this,[this]{resultsButton_->setChecked(false);});layout->addLayout(row);
        resultsList_=new QListView;resultsList_->setObjectName("searchResultsList");resultsList_->setUniformItemSizes(true);resultsList_->setEditTriggers(QAbstractItemView::NoEditTriggers);resultsList_->setAccessibleName(QStringLiteral("搜索匹配结果"));
        QFont resultFont(appearance_.family);resultFont.setPixelSize(12);resultsList_->setFont(resultFont);
        resultsModel_=new SearchResultsModel(resultsList_);resultsList_->setModel(resultsModel_);layout->addWidget(resultsList_,1);
        auto choose=[this](const QModelIndex& index){resultsModel_->activate(index.row());};connect(resultsList_,&QListView::clicked,this,choose);connect(resultsList_,&QListView::activated,this,choose);
        contentSplit_->addWidget(resultsPanel_);contentSplit_->setSizes({qMax(250,height()-300),180});
    }
    if(resultsPanel_)resultsPanel_->setVisible(on);updateSearchLabel();
}
void MainWindow::setWrap(bool on) {
    wrap_=on;{QSignalBlocker a(wrapButton_),b(wrapAction_);wrapButton_->setChecked(on);wrapAction_->setChecked(on);}
    for(int i=0;i<tabs_->count();++i)if(auto* p=pageAt(i))p->setWordWrap(on);
    if(secondaryPage_)secondaryPage_->setWordWrap(on);
}
void MainWindow::changeFont(int delta) {
    appearance_.points=qBound(8,appearance_.points+delta,32);
    for(int i=0;i<tabs_->count();++i)if(auto* p=pageAt(i))p->setAppearance(appearance_);
    if(secondaryPage_)secondaryPage_->setAppearance(appearance_);
    if(!testMode_)appearance_.save(settings_);
}
void MainWindow::showAppearanceSettings() {
    AppearanceDialog dialog(appearance_,FileIcons::theme(),this);
    connect(&dialog,&AppearanceDialog::applyRequested,this,[this,&dialog]{applyAppearance(dialog.editorAppearance(),dialog.iconTheme(),dialog.colorTheme());});
    dialog.exec();
}
void MainWindow::applyAppearance(const EditorAppearance& appearance, const QString& iconTheme, const QString& colorTheme) {
    const auto next=appearance.sanitized();const bool fontChanged=!(appearance_==next);appearance_=next;
    const auto theme=Theme::normalizedTheme(colorTheme.isEmpty()?Theme::id():colorTheme);
    if(theme!=Theme::id())Theme::apply(*qApp,theme);
    FileIcons::setTheme(iconTheme);
    for(int i=0;i<tabs_->count();++i)if(auto* p=pageAt(i)) {
        if(fontChanged)p->setAppearance(appearance_);
        if(p->editor)p->editor->setColorTheme(theme);
        tabs_->setTabIcon(i,Theme::fileIcon(p->path));
    }
    for(auto* button:welcome_->findChildren<QToolButton*>()) {
        const auto path=button->property("filePath").toString();
        if(!path.isEmpty())button->setIcon(Theme::fileIcon(path));
    }
    if(secondaryPage_){if(fontChanged)secondaryPage_->setAppearance(appearance_);if(secondaryPage_->editor)secondaryPage_->editor->setColorTheme(theme);}
    refreshThemeIcons();applyNativeChrome();updateRecentMenu();refreshStatus();tabs_->update();
    if(!testMode_){appearance_.save(settings_);settings_.setValue("iconTheme",FileIcons::theme());Theme::saveTheme(settings_,theme);settings_.sync();}
}
void MainWindow::refreshThemeIcons() {
    const auto& c=Theme::colors();
    auto color=[&](const QString& role){return role=="accentText"?c.accentText:role=="accent"?c.accent:role=="subtle"?c.subtle:role=="text"?c.text:c.muted;};
    if(maximizeButton_)maximizeButton_->setProperty("glyph",isMaximized()?"restore":"maximize");
    for(auto* button:findChildren<QAbstractButton*>()) {
        const auto glyph=button->property("glyph").toString();
        if(!glyph.isEmpty())button->setIcon(Theme::icon(glyph,color(button->property("glyphRole").toString())));
    }
    for(auto* label:findChildren<QLabel*>()) {
        const auto glyph=label->property("glyph").toString();if(glyph.isEmpty())continue;
        const int size=label->property("glyphSize").toInt();
        label->setPixmap(Theme::icon(glyph,color(label->property("glyphRole").toString()),size).pixmap(QSize(size,size),devicePixelRatioF()));
    }
}
void MainWindow::jumpToLine() {
    auto* p=currentPage();if(!p||!p->ready)return;
    bool ok=false;int line=QInputDialog::getInt(this,QStringLiteral("跳转到行"),QStringLiteral("行号"),p->editor->send(SCI_LINEFROMPOSITION,p->editor->send(SCI_GETCURRENTPOS))+1,1,p->editor->send(SCI_GETLINECOUNT),1,&ok);
    if(ok){p->editor->goToLine(line);p->editor->setFocus();}
}
void MainWindow::reopenClosed() {if(!closed_.isEmpty())openPath(closed_.takeLast());}
void MainWindow::updateRecentMenu() {
    recentMenu_->clear();if(recent_.isEmpty()){recentMenu_->addAction(QStringLiteral("暂无最近文件"))->setEnabled(false);return;}
    for(const auto& path:recent_.mid(0,12)){auto* a=recentMenu_->addAction(Theme::fileIcon(path),QFileInfo(path).fileName());a->setToolTip(path);connect(a,&QAction::triggered,this,[this,path]{openPath(path);});}
}
void MainWindow::toggleMaximize() {isMaximized()?showNormal():showMaximized();}
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {if(event->mimeData()->hasUrls())event->acceptProposedAction();}
void MainWindow::dropEvent(QDropEvent* event) {for(const auto& url:event->mimeData()->urls())if(url.isLocalFile())openPath(url.toLocalFile());event->acceptProposedAction();}
void MainWindow::closeEvent(QCloseEvent* event) {
    if(!saveSession())QMessageBox::warning(this,QStringLiteral("Zex · 会话未保存"),QStringLiteral("本次标签和阅读位置未能保存：\n%1").arg(sessionNotice_->toolTip()));
    if(!testMode_){settings_.setValue("geometry",saveGeometry());settings_.setValue("maximized",isMaximized());appearance_.save(settings_);settings_.setValue("iconTheme",FileIcons::theme());settings_.setValue("wordWrap",wrap_);settings_.setValue("recentFiles",recent_);}
    emit closing();
    QMainWindow::closeEvent(event);
}
void MainWindow::changeEvent(QEvent* event) {
    if(event->type()==QEvent::WindowStateChange) {
        if(maximizeButton_)maximizeButton_->setIcon(Theme::icon(isMaximized()?"restore":"maximize"));
        if(testAttribute(Qt::WA_WState_Created))applyNativeChrome();
    }
    QMainWindow::changeEvent(event);
    // Qt reapplies its native frame after palette changes; our frameless window
    // must restore the explicit border policy once that update has completed.
    if((event->type()==QEvent::PaletteChange || event->type()==QEvent::StyleChange || event->type()==QEvent::ThemeChange)
        && testAttribute(Qt::WA_WState_Created))QTimer::singleShot(0,this,&MainWindow::applyNativeChrome);
}
void MainWindow::applyNativeChrome() {
    const auto hwnd=reinterpret_cast<HWND>(winId());
    const BOOL dark=Theme::colors().dark; DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));
    // Do not extend DWM's light frame into our custom client area (also on Windows 10).
    const MARGINS margins{0,0,0,0}; DwmExtendFrameIntoClientArea(hwnd,&margins);
    // Windows 11: explicitly suppress the system outline. Unsupported on Windows 10.
    const DWORD border=0xFFFFFFFE; DwmSetWindowAttribute(hwnd,34,&border,sizeof(border));
    const DWORD corner=isMaximized()?1:2; DwmSetWindowAttribute(hwnd,33,&corner,sizeof(corner));
}
bool MainWindow::eventFilter(QObject* obj,QEvent* event) {
    if(obj==editorHost_ && event->type()==QEvent::Resize && searchbar_->isVisible())positionSearchBar();
    if(obj==titlebar_) {
        if(event->type()==QEvent::MouseButtonDblClick){if(static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton){toggleMaximize();return true;}}
        if(event->type()==QEvent::MouseButtonPress){if(static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton && windowHandle()){windowHandle()->startSystemMove();return true;}}
    }
    if(obj==search_ && event->type()==QEvent::KeyPress){auto* key=static_cast<QKeyEvent*>(event);if((key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter)&&key->modifiers().testFlag(Qt::ShiftModifier)){if(auto* p=currentPage();p&&p->ready)p->editor->findNext(true);return true;}}
    return QMainWindow::eventFilter(obj,event);
}

void MainWindow::positionSearchBar() {
    if(!editorHost_)return;
    auto* pane=rightActive_ && rightPane_?rightPane_:leftPane_;
    const QRect area(pane->mapTo(editorHost_,QPoint()),pane->size());
    const int width=qMin(520,qMax(236,area.width()-24));const bool compact=width<460;
    searchLayout_->setDirection(compact?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight);
    const int height=compact?61:36;searchbar_->setFixedHeight(height);
    searchbar_->setGeometry(qMax(0,area.right()-width-11),splitEnabled()?32:6,width,height);
    searchbar_->raise();
}
bool MainWindow::nativeEvent(const QByteArray& type,void* message,qintptr* result) {
    auto* msg=static_cast<MSG*>(message);
    if(msg->message==WM_THEMECHANGED || msg->message==WM_DWMCOMPOSITIONCHANGED || msg->message==WM_ACTIVATE)
        QTimer::singleShot(0,this,&MainWindow::applyNativeChrome);
    if(msg->message==WM_NCHITTEST && !isMaximized()) {
        RECT r;GetWindowRect(msg->hwnd,&r);int x=GET_X_LPARAM(msg->lParam),y=GET_Y_LPARAM(msg->lParam);int b=qRound(6*devicePixelRatioF());
        bool left=x<r.left+b,right=x>=r.right-b,top=y<r.top+b,bottom=y>=r.bottom-b;
        if(top&&left)*result=HTTOPLEFT;else if(top&&right)*result=HTTOPRIGHT;else if(bottom&&left)*result=HTBOTTOMLEFT;else if(bottom&&right)*result=HTBOTTOMRIGHT;else if(left)*result=HTLEFT;else if(right)*result=HTRIGHT;else if(top)*result=HTTOP;else if(bottom)*result=HTBOTTOM;else return QMainWindow::nativeEvent(type,message,result);
        return true;
    }
    if(msg->message==WM_GETMINMAXINFO) {
        auto* limits=reinterpret_cast<MINMAXINFO*>(msg->lParam);MONITORINFO monitor{sizeof(MONITORINFO)};
        if(GetMonitorInfoW(MonitorFromWindow(msg->hwnd,MONITOR_DEFAULTTONEAREST),&monitor)) {
            limits->ptMaxPosition={monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top};
            limits->ptMaxSize={monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top};
            limits->ptMinTrackSize={qRound(minimumWidth()*devicePixelRatioF()),qRound(minimumHeight()*devicePixelRatioF())};
            *result=0;return true;
        }
    }
    return QMainWindow::nativeEvent(type,message,result);
}
