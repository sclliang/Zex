#include "mainwindow.h"
#include "theme.h"
#include <QApplication>
#include <QAction>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QToolButton* paneButton(QWidget* parent,const QString& glyph,const QString& tip) {
    auto* button=new QToolButton(parent);button->setProperty("glyph",glyph);
    button->setIcon(Theme::icon(glyph,Theme::colors().muted));button->setIconSize({16,16});button->setToolTip(tip);button->setAccessibleName(tip);
    button->setFixedSize(24,24);button->setFocusPolicy(Qt::NoFocus);return button;
}
QWidget* header(QWidget* parent,QLabel*& title) {
    auto* widget=new QWidget(parent);widget->setObjectName("paneHeader");widget->setFixedHeight(28);
    auto* row=new QHBoxLayout(widget);row->setContentsMargins(12,0,6,0);row->setSpacing(3);
    title=new QLabel;title->setTextFormat(Qt::PlainText);title->setMinimumWidth(0);title->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);row->addWidget(title,1);
    return widget;
}
void activeHeader(QWidget* widget,bool active) {
    if(widget && widget->property("active")!=active){widget->setProperty("active",active);widget->style()->unpolish(widget);widget->style()->polish(widget);widget->update();}
}
}

void MainWindow::initializePanes() {
    paneSplit_=new QSplitter(Qt::Horizontal);paneSplit_->setObjectName("editorPanes");paneSplit_->setChildrenCollapsible(false);paneSplit_->setHandleWidth(3);
    leftPane_=new QWidget;leftPane_->setMinimumWidth(260);
    auto* layout=new QVBoxLayout(leftPane_);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
    leftHeader_=header(leftPane_,leftTitle_);layout->addWidget(leftHeader_);leftHeader_->hide();
    auto* choose=paneButton(leftHeader_,"files",QStringLiteral("在左侧切换文件"));choose->setObjectName("chooseLeftFile");leftHeader_->layout()->addWidget(choose);
    connect(choose,&QToolButton::clicked,this,[this]{activatePane(false,false);showFilePicker();});
    pages_=new QStackedWidget;layout->addWidget(pages_,1);paneSplit_->addWidget(leftPane_);
    connect(paneSplit_,&QSplitter::splitterMoved,this,[this]{
        const auto sizes=paneSplit_->sizes();if(secondaryPage_ && sizes.size()==2 && sizes[0]+sizes[1]>0)splitPercent_=qBound(20,qRound(100.0*sizes[0]/(sizes[0]+sizes[1])),80);
        positionSearchBar();scheduleSessionSave();
    });
    connect(qApp,&QApplication::focusChanged,this,[this](QWidget*,QWidget* next){
        if(!next || !secondaryPage_)return;
        if(secondaryPage_->isAncestorOf(next))activatePane(true,false);
        else if(auto* left=primaryPage();left && left->isAncestorOf(next))activatePane(false,false);
    });
}

void MainWindow::setSecondaryPath(const QString& path) {
    FilePage* source=nullptr;
    for(int i=0;i<tabs_->count();++i)if(pageAt(i)->path.compare(path,Qt::CaseInsensitive)==0){source=pageAt(i);break;}
    if(!source || !rightPane_)return;
    if(secondaryPage_ && secondaryPage_->path==source->path){secondaryPage_->activate();return;}
    if(secondaryPage_) {
        rightPositions_.insert(secondaryPage_->path.toCaseFolded(),secondaryPage_->readingPosition());
        rightEncodings_.insert(secondaryPage_->path.toCaseFolded(),secondaryPage_->encoding());
        auto* previous=secondaryPage_.data();secondaryPage_.clear();delete previous;
    }
    auto* page=new FilePage(source->path,rightPane_);secondaryPage_=page;
    page->setAppearance(appearance_);page->setWordWrap(wrap_);
    const auto key=source->path.toCaseFolded();
    page->setReadingPosition(rightPositions_.value(key,source->readingPosition()));page->setEncoding(rightEncodings_.value(key,source->encoding()));page->setBookmarks(source->bookmarks());
    connectPage(page);rightPane_->layout()->addWidget(page);page->show();page->activate();
}

void MainWindow::setSplitEnabled(bool on,const QString& rightPath) {
    if(on && !primaryPage()){refreshPaneHeaders();return;}
    if(on) {
        if(!rightPane_) {
            rightPane_=new QWidget;rightPane_->setMinimumWidth(260);
            auto* layout=new QVBoxLayout(rightPane_);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
            rightHeader_=header(rightPane_,rightTitle_);layout->addWidget(rightHeader_);
            auto* choose=paneButton(rightHeader_,"files",QStringLiteral("在右侧切换文件"));choose->setObjectName("chooseRightFile");rightHeader_->layout()->addWidget(choose);
            connect(choose,&QToolButton::clicked,this,[this]{activatePane(true,false);showFilePicker();});
            auto* close=paneButton(rightHeader_,"close",QStringLiteral("关闭右侧分屏"));close->setObjectName("closeRightPane");rightHeader_->layout()->addWidget(close);
            connect(close,&QToolButton::clicked,this,[this]{setSplitEnabled(false);});
            paneSplit_->addWidget(rightPane_);
        }
        rightPane_->show();leftHeader_->show();
        const int available=qMax(400,paneSplit_->width()-paneSplit_->handleWidth());
        paneSplit_->setSizes({available*splitPercent_/100,available*(100-splitPercent_)/100});
        setSecondaryPath(rightPath.isEmpty()?primaryPage()->path:rightPath);
        activatePane(true);
    } else {
        if(secondaryPage_) {
            rightPositions_.insert(secondaryPage_->path.toCaseFolded(),secondaryPage_->readingPosition());
            rightEncodings_.insert(secondaryPage_->path.toCaseFolded(),secondaryPage_->encoding());
            auto* previous=secondaryPage_.data();secondaryPage_.clear();delete previous;
        }
        if(rightPane_)rightPane_->hide();leftHeader_->hide();
        activatePane(false);
    }
    refreshPaneHeaders();positionSearchBar();scheduleSessionSave();
}

void MainWindow::activatePane(bool right,bool focusEditor) {
    const bool next=right && secondaryPage_;
    const bool changed=rightActive_!=next;rightActive_=next;
    auto* page=currentPage();
    if(page) {
        const QSignalBlocker blocker(tabs_);
        for(int i=0;i<tabs_->count();++i)if(pageAt(i)->path==page->path){tabs_->setCurrentIndex(i);break;}
    }
    refreshStatus();
    positionSearchBar();
    if(changed && searchbar_->isVisible())runSearch(false);
    if(focusEditor && page && page->editor)page->editor->setFocus();
    if(changed)scheduleSessionSave();
}

void MainWindow::refreshPaneHeaders() {
    const bool enabled=splitEnabled();
    if(splitButton_){const QSignalBlocker blocker(splitButton_);splitButton_->setChecked(enabled);splitButton_->setEnabled(primaryPage()!=nullptr);}
    if(splitAction_){const QSignalBlocker blocker(splitAction_);splitAction_->setChecked(enabled);splitAction_->setEnabled(primaryPage()!=nullptr);}
    if(auto* left=primaryPage();leftTitle_ && left){leftTitle_->setText(QFileInfo(left->path).fileName());leftTitle_->setToolTip(QStringLiteral("左侧 · ")+left->path);}
    if(secondaryPage_ && rightTitle_){rightTitle_->setText(QFileInfo(secondaryPage_->path).fileName());rightTitle_->setToolTip(QStringLiteral("右侧 · ")+secondaryPage_->path);}
    activeHeader(leftHeader_,!rightActive_);activeHeader(rightHeader_,rightActive_);
}
