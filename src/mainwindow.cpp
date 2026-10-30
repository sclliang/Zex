#include "mainwindow.h"
#include "theme.h"
#include "appearancedialog.h"
#include "fileicons.h"
#include "filepicker.h"
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
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
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
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

namespace {
QToolButton* tool(QWidget* parent, const QString& icon, const QString& tip, const QSize& size = QSize(28,28)) {
    auto* button = new QToolButton(parent);
    button->setProperty("glyph",icon);
    button->setIcon(Theme::icon(icon)); button->setToolTip(tip); button->setAccessibleName(tip);
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
    emit editorCreated();
}
void FilePage::setAppearance(const EditorAppearance& appearance) { appearance_=appearance; if(editor)editor->setAppearance(appearance); }
void FilePage::setWordWrap(bool on) { wrap_=on; if(editor)editor->setWordWrap(on); }
void FilePage::setEncoding(const QString& encoding) {
    const auto next=normalizedTextEncoding(encoding);if(next==encoding_)return;
    if(loading)cancelLoad();encoding_=next;if(attempted)reload();
}
void FilePage::setReadingPosition(const ReadingPosition& position) { pendingPosition_=position; }
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
    setFixedHeight(39); setFocusPolicy(Qt::NoFocus);
    setSelectionBehaviorOnRemove(QTabBar::SelectPreviousTab);
}
QSize FileTabs::tabSizeHint(int index) const { return {qBound(140,fontMetrics().horizontalAdvance(tabText(index))+78,230),39}; }
bool FileTabs::isPinned(int index) const {
    auto* page=qobject_cast<FilePage*>(tabData(index).value<QObject*>());return page && page->pinned;
}
void FileTabs::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    for (int i=0; i<count(); ++i) {
        auto rect=tabRect(i); if (!rect.intersects(this->rect())) continue;
        bool active=i==currentIndex();
        painter.fillRect(rect,active ? Theme::colors().background : i==hovered_ ? Theme::colors().hover : Theme::colors().chrome);
        if (active) painter.fillRect(rect.x()+1,rect.y(),rect.width()-1,2,Theme::colors().accent);
        painter.setPen(Theme::colors().border); painter.drawLine(rect.topRight()+QPoint(0,8),rect.bottomRight()-QPoint(0,8));
        tabIcon(i).paint(&painter,QRect(rect.left()+13,rect.top()+11,17,17));
        painter.setPen(active ? Theme::colors().text : Theme::colors().muted);
        auto textRect=rect.adjusted(39,0,-31,0);
        painter.drawText(textRect,Qt::AlignVCenter|Qt::AlignLeft,fontMetrics().elidedText(tabText(i),Qt::ElideMiddle,textRect.width()));
        if(isPinned(i))Theme::icon("pin",Theme::colors().accent,16).paint(&painter,QRect(rect.right()-25,rect.top()+12,16,16));
        else if (active || i==hovered_) Theme::icon("close",Theme::colors().muted,16).paint(&painter,QRect(rect.right()-25,rect.top()+12,16,16));
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
    auto* stripLayout=new QHBoxLayout(strip); stripLayout->setContentsMargins(0,0,7,0); stripLayout->setSpacing(2);
    tabs_=new FileTabs; stripLayout->addWidget(tabs_,1);
    allTabsButton_=tool(strip,"down",QStringLiteral("全部标签"));allTabsButton_->setObjectName("allTabsButton");stripLayout->addWidget(allTabsButton_);
    connect(allTabsButton_,&QToolButton::clicked,this,[this]{showFilePicker(true);});
    auto* open=tool(strip,"plus",QStringLiteral("打开文件 (Ctrl+O)")); stripLayout->addWidget(open);
    auto* find=tool(strip,"search",QStringLiteral("搜索 (Ctrl+F)")); stripLayout->addWidget(find);
    auto* switchFile=tool(strip,"files",QStringLiteral("快速切换文件 (Ctrl+P)"));stripLayout->addWidget(switchFile);
    connect(switchFile,&QToolButton::clicked,this,[this]{showFilePicker();});
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
    auto* breadcrumb=new QWidget; breadcrumb->setObjectName("breadcrumb"); breadcrumb->setFixedHeight(34);
    auto* pathLayout=new QHBoxLayout(breadcrumb); pathLayout->setContentsMargins(14,0,10,0); pathLayout->setSpacing(7);
    fileIcon_=label({}); fileIcon_->setFixedSize(18,18);
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

    searchbar_=new QWidget; searchbar_->setObjectName("searchbar"); searchbar_->setFixedHeight(49);
    auto* searchLayout=new QHBoxLayout(searchbar_); searchLayout->setContentsMargins(14,7,10,7); searchLayout->setSpacing(7);
    auto* searchGlyph=glyphLabel("search");searchLayout->addWidget(searchGlyph);
    search_=new QLineEdit; search_->setObjectName("searchInput"); search_->setAccessibleName(QStringLiteral("搜索关键词"));
    search_->setPlaceholderText(QStringLiteral("在当前文件中搜索")); search_->setClearButtonEnabled(true); search_->installEventFilter(this);
    searchLayout->addWidget(search_,1);
    case_=new QToolButton; case_->setText("Aa"); case_->setCheckable(true); case_->setFixedSize(30,28); case_->setToolTip(QStringLiteral("区分大小写")); case_->setAccessibleName(QStringLiteral("区分大小写"));
    whole_=new QToolButton; whole_->setText("ab"); whole_->setCheckable(true); whole_->setFixedSize(30,28); whole_->setToolTip(QStringLiteral("全字匹配")); whole_->setAccessibleName(QStringLiteral("全字匹配"));
    searchLayout->addWidget(case_); searchLayout->addWidget(whole_);
    count_=label(QStringLiteral("输入关键词")); count_->setMinimumWidth(105); count_->setAlignment(Qt::AlignCenter); searchLayout->addWidget(count_);
    auto* previous=tool(searchbar_,"up",QStringLiteral("上一个 (Shift+Enter)")); auto* next=tool(searchbar_,"down",QStringLiteral("下一个 (Enter)"));
    auto* closeFind=tool(searchbar_,"close",QStringLiteral("关闭搜索 (Esc)")); searchLayout->addWidget(previous); searchLayout->addWidget(next); searchLayout->addWidget(closeFind);
    root->addWidget(searchbar_); searchbar_->hide();
    searchDebounce_.setSingleShot(true); searchDebounce_.setInterval(110);
    connect(&searchDebounce_,&QTimer::timeout,this,&MainWindow::runSearch);
    connect(search_,&QLineEdit::textChanged,this,[this]{searchDebounce_.start();});
    connect(case_,&QToolButton::toggled,this,&MainWindow::runSearch); connect(whole_,&QToolButton::toggled,this,&MainWindow::runSearch);
    connect(search_,&QLineEdit::returnPressed,this,[this]{if(searchDebounce_.isActive()){runSearch();return;} if(auto* p=currentPage();p&&p->ready)p->editor->findNext();});
    connect(previous,&QToolButton::clicked,this,[this]{if(auto* p=currentPage();p&&p->ready)p->editor->findNext(true);});
    connect(next,&QToolButton::clicked,this,[this]{if(auto* p=currentPage();p&&p->ready)p->editor->findNext();});
    connect(closeFind,&QToolButton::clicked,this,&MainWindow::hideSearch);

    pages_=new QStackedWidget; welcome_=buildWelcome(); pages_->addWidget(welcome_); root->addWidget(pages_,1);
    auto* status=new QWidget; status->setObjectName("statusbar"); status->setFixedHeight(28);
    auto* statusLayout=new QHBoxLayout(status); statusLayout->setContentsMargins(10,0,9,0); statusLayout->setSpacing(4);
    auto* lock=glyphLabel("lock","muted",14);statusLayout->addWidget(lock);
    statusLayout->addWidget(label(QStringLiteral("只读"),"status")); fileInfo_=label("Zex 0.6.0","status"); statusLayout->addWidget(fileInfo_);
    encodingButton_=new QToolButton;encodingButton_->setObjectName("encodingButton");encodingButton_->setPopupMode(QToolButton::InstantPopup);encodingButton_->setMenu(encodingMenu_);encodingButton_->setAccessibleName(QStringLiteral("文件编码"));statusLayout->addWidget(encodingButton_);
    sessionNotice_=label(QStringLiteral("会话未保存"),"sessionNotice");sessionNotice_->hide();statusLayout->addWidget(sessionNotice_);
    statusLayout->addStretch(); position_=label({},"status"); language_=label({},"status"); statusLayout->addWidget(position_); statusLayout->addWidget(language_);
    root->addWidget(status);
    sessionSaveTimer_.setSingleShot(true);sessionSaveTimer_.setInterval(1000);
    connect(&sessionSaveTimer_,&QTimer::timeout,this,&MainWindow::saveSession);

    auto shortcut=[this](const QKeySequence& key, auto action){auto* s=new QShortcut(key,this);connect(s,&QShortcut::activated,this,action);};
    shortcut(QKeySequence("Ctrl+P"),[this]{showFilePicker();});
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
    shortcut(QKeySequence("Ctrl+0"),[this]{appearance_.points=12;changeFont(0);});
    shortcut(QKeySequence("Ctrl+,"),[this]{showAppearanceSettings();});
    shortcut(QKeySequence("F3"),[this]{if(auto* p=currentPage();p&&p->ready)p->editor->findNext();});
    shortcut(QKeySequence("Shift+F3"),[this]{if(auto* p=currentPage();p&&p->ready)p->editor->findNext(true);});
    shortcut(QKeySequence("F5"),[this]{if(auto* p=currentPage())p->reload();});
    shortcut(QKeySequence("Escape"),[this]{hideSearch();});
    for(int i=1;i<=9;++i) shortcut(QKeySequence(QString("Alt+%1").arg(i)),[this,i]{if(tabs_->count())tabs_->setCurrentIndex(qMin(i-1,tabs_->count()-1));});
    connect(&fileWatcher_,&QFileSystemWatcher::fileChanged,this,[this](const QString& path){
        for(int i=0;i<tabs_->count();++i) if(auto* p=pageAt(i);p && p->path==path && !p->following()){p->stale=true;if(p==currentPage())refreshStatus();}
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
    titlebar_=new QWidget; titlebar_->setObjectName("titlebar"); titlebar_->setFixedHeight(37); titlebar_->installEventFilter(this);
    auto* row=new QHBoxLayout(titlebar_); row->setContentsMargins(11,0,0,0); row->setSpacing(3);
    auto* logo=glyphLabel("zex","text");row->addWidget(logo);row->addSpacing(9);
    auto menuButton=[&](const QString& text){auto* b=new QToolButton; b->setText(text);b->setFixedHeight(27);b->setPopupMode(QToolButton::InstantPopup);b->setFocusPolicy(Qt::NoFocus);row->addWidget(b);return b;};
    auto* file=menuButton(QStringLiteral("文件"));auto* view=menuButton(QStringLiteral("查看"));auto* help=menuButton(QStringLiteral("帮助"));
    row->addStretch(); title_=label("Zex");title_->setTextFormat(Qt::PlainText);title_->setAttribute(Qt::WA_TransparentForMouseEvents);row->addWidget(title_);row->addStretch();
    row->addSpacing(85);
    auto* minimize=tool(titlebar_,"minimize",QStringLiteral("最小化"),{43,37});minimize->setObjectName("windowButton");
    maximizeButton_=tool(titlebar_,"maximize",QStringLiteral("最大化 / 还原"),{43,37});maximizeButton_->setObjectName("windowButton");
    auto* close=tool(titlebar_,"close",QStringLiteral("关闭窗口"),{43,37});close->setObjectName("windowClose");
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
    views->addAction(QStringLiteral("搜索…\tCtrl+F"),this,&MainWindow::showSearch);
    views->addAction(QStringLiteral("跳转到行…\tCtrl+G"),this,&MainWindow::jumpToLine);views->addSeparator();
    wrapAction_=views->addAction(QStringLiteral("自动换行\tAlt+Z"));wrapAction_->setCheckable(true);wrapAction_->setChecked(wrap_);connect(wrapAction_,&QAction::toggled,this,&MainWindow::setWrap);
    followAction_=views->addAction(QStringLiteral("跟随文件"));followAction_->setCheckable(true);connect(followAction_,&QAction::toggled,this,[this](bool on){if(auto* page=currentPage())page->setFollowing(on);});
    views->addAction(QStringLiteral("放大字体\tCtrl++"),this,[this]{changeFont(1);});
    views->addAction(QStringLiteral("缩小字体\tCtrl+-"),this,[this]{changeFont(-1);});
    views->addAction(QStringLiteral("重置字号\tCtrl+0"),this,[this]{appearance_.points=12;changeFont(0);});
    views->addSeparator();
    views->addAction(QStringLiteral("外观设置…\tCtrl+,"),this,&MainWindow::showAppearanceSettings);
    views->addAction(QStringLiteral("Windows 集成…"),this,[this]{IntegrationDialog dialog(ShellIntegration(QCoreApplication::applicationFilePath()),this);dialog.exec();});
    auto* helps=new QMenu(this);help->setMenu(helps);
    helps->addAction(QStringLiteral("快捷键"),this,[this]{QMessageBox::information(this,QStringLiteral("Zex · 快捷键"),QStringLiteral("Ctrl+O    打开一个或多个文件\nCtrl+P    快速切换文件\nCtrl+F    搜索当前文件\nEnter / Shift+Enter    下一个 / 上一个结果\nF3 / Shift+F3    下一个 / 上一个结果\nEsc    关闭搜索\nCtrl+G    跳转到行\nCtrl+W / 鼠标中键    关闭标签\nCtrl+Shift+T    恢复标签\nCtrl+Tab / Ctrl+Shift+Tab    切换标签\nAlt+1 … Alt+9    切换到指定标签\nAlt+Z    自动换行\nCtrl++ / Ctrl+- / Ctrl+0    调整字号\nCtrl+,    外观设置\nF5    重新载入文件\n\n支持拖入文件、拖动标签排序；右键标签可固定、关闭右侧。固定标签需先取消固定才能关闭。"));});
    helps->addAction(QStringLiteral("关于 Zex"),this,[this]{QMessageBox::about(this,QStringLiteral("关于 Zex"),QStringLiteral("<h2>Zex <small>0.6.0</small></h2><p>专注于文件与代码的桌面阅读器。</p><p>C++ · Qt 6 · Scintilla · Lexilla</p><p>Copyright (c) 2026 sixunited.<br>Author: sclliang</p><p>本版本支持只读查看，不会修改打开的文件。<br>开源组件许可位于 bin/licenses。</p>"));});
}

QWidget* MainWindow::buildWelcome() {
    auto* page=new QWidget;auto* outer=new QVBoxLayout(page);outer->setContentsMargins(30,20,30,20);outer->addStretch(3);
    auto* panel=new QWidget;panel->setMaximumWidth(440);auto* body=new QVBoxLayout(panel);body->setContentsMargins(20,0,20,0);body->setSpacing(0);
    auto* mark=glyphLabel("zex","subtle",54);body->addWidget(mark);body->addSpacing(19);
    body->addWidget(label("Zex","brand"));body->addSpacing(7);body->addWidget(label(QStringLiteral("专注于文件与代码。"),"subtitle"));body->addSpacing(30);
    auto* open=new QPushButton(QStringLiteral("打开文件"));open->setObjectName("openPrimary");open->setProperty("glyph","folder");open->setProperty("glyphRole","accentText");open->setFixedHeight(40);open->setCursor(Qt::PointingHandCursor);body->addWidget(open);
    connect(open,&QPushButton::clicked,this,&MainWindow::openFiles);body->addSpacing(13);
    auto* hint=label(QStringLiteral("也可以把文件拖到窗口中"),"subtitle");hint->setAlignment(Qt::AlignCenter);body->addWidget(hint);body->addSpacing(32);
    auto shortcut=[&](const QString& text,const QString& key){auto* row=new QHBoxLayout;row->addWidget(label(text));row->addStretch();auto* k=label(key,"shortcutKey");row->addWidget(k);body->addLayout(row);body->addSpacing(12);};
    shortcut(QStringLiteral("打开文件"),"Ctrl O");shortcut(QStringLiteral("搜索文本"),"Ctrl F");shortcut(QStringLiteral("跳转到行"),"Ctrl G");
    if(!recent_.isEmpty()) {
        body->addSpacing(17);body->addWidget(label(QStringLiteral("最近打开")));body->addSpacing(7);
        for(const auto& path:recent_.mid(0,3)) {
            auto* b=new QToolButton; b->setProperty("filePath",path);b->setText(QFileInfo(path).fileName());b->setToolTip(path);b->setIcon(Theme::fileIcon(path));b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);b->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);b->setStyleSheet("text-align:left;padding:6px;");body->addWidget(b);connect(b,&QToolButton::clicked,this,[this,path]{openPath(path);});
        }
    }
    outer->addWidget(panel,0,Qt::AlignHCenter);outer->addStretch(4);
    auto* bottom=label(QStringLiteral("本地文件  /  即开即读"),"subtitle");bottom->setAlignment(Qt::AlignCenter);outer->addWidget(bottom);outer->addSpacing(12);
    auto* scroll=new QScrollArea;scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);scroll->setWidget(page);return scroll;
}

FilePage* MainWindow::pageAt(int index) const {
    if(index<0 || index>=tabs_->count())return nullptr;
    return qobject_cast<FilePage*>(tabs_->tabData(index).value<QObject*>());
}
FilePage* MainWindow::currentPage() const {return pageAt(tabs_->currentIndex());}
int MainWindow::tabCount() const {return tabs_->count();}
bool MainWindow::allLoaded() const {for(int i=0;i<tabs_->count();++i)if(auto* p=pageAt(i);p&&p->loading)return false;return true;}

FilePage* MainWindow::addPage(const QString& path, const ReadingPosition& position,const QString& encoding,bool pinned) {
    auto* page=new FilePage(path,pages_);page->setAppearance(appearance_);page->setWordWrap(wrap_);page->setReadingPosition(position);pages_->addWidget(page);
    page->setEncoding(encoding);
    page->pinned=pinned;
    // Adding the first tab emits currentChanged before tabData is assigned; activate only after setup.
    const QSignalBlocker blocker(tabs_);
    int index=tabs_->addTab(Theme::fileIcon(path),QFileInfo(path).fileName());
    tabs_->setTabData(index,QVariant::fromValue(static_cast<QObject*>(page)));tabs_->setTabToolTip(index,QDir::toNativeSeparators(path));
    connect(page,&FilePage::loaded,this,[this,page]{
        if(page->ready) {
            if(!fileWatcher_.files().contains(page->path))fileWatcher_.addPath(page->path);
            recent_.removeAll(page->path);recent_.prepend(page->path);recent_=recent_.mid(0,50);
        }
        if(page==currentPage()){refreshStatus();if(searchbar_->isVisible()){const auto position=page->readingPosition();runSearch();if(page->ready)page->editor->restoreReadingPosition(position);updateSearchLabel();}}
        scheduleSessionSave();
        emit fileReady(page->path);
    });
    connect(page,&FilePage::editorCreated,this,[this,page]{
        connect(page->editor,&CodeEditor::positionChanged,this,[this,page]{if(page==currentPage())refreshStatus();});
        connect(page->editor,&CodeEditor::readingPositionChanged,this,&MainWindow::scheduleSessionSave);
        connect(page->editor,&CodeEditor::searchChanged,this,[this,page]{if(page==currentPage())updateSearchLabel();});
        connect(page->editor,&CodeEditor::fileDropped,this,[this](const QString& p){openPath(p);});
    });
    connect(page,&FilePage::loadingChanged,this,[this,page]{if(page==currentPage())refreshStatus();scheduleSessionSave();});
    connect(page,&FilePage::followingChanged,this,[this,page]{if(page==currentPage())refreshStatus();scheduleSessionSave();});
    return page;
}

FilePage* MainWindow::openPath(const QString& value) {
    const auto path=normalized(value);
    for(int i=0;i<tabs_->count();++i)if(auto* p=pageAt(i);p&&p->path.compare(path,Qt::CaseInsensitive)==0){tabs_->setCurrentIndex(i);selectTab(i);return p;}
    auto* page=addPage(path);const int index=tabs_->count()-1;tabs_->setCurrentIndex(index);selectTab(index);return page;
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
    for(const auto& tab:session.tabs)addPage(tab.path,tab.position,tab.encoding,tab.pinned);
    normalizePinnedTabs();
    for(const auto& filename:startupFiles) {
        const auto path=normalized(filename);bool exists=false;
        for(int i=0;i<tabs_->count();++i)if(pageAt(i)->path.compare(path,Qt::CaseInsensitive)==0){exists=true;break;}
        if(!exists)addPage(path);
        active=path;
    }
    int activeIndex=tabs_->count()?0:-1;
    for(int i=0;i<tabs_->count();++i)if(pageAt(i)->path.compare(active,Qt::CaseInsensitive)==0){activeIndex=i;break;}
    sessionInitialized_=true;restoringSession_=false;
    {QSignalBlocker blocker(tabs_);tabs_->setCurrentIndex(activeIndex);}
    if(activeIndex>=0)selectTab(activeIndex);
}

SessionSnapshot MainWindow::sessionSnapshot() const {
    SessionSnapshot session;
    for(int i=0;i<tabs_->count();++i)if(auto* page=pageAt(i))session.tabs.append({page->path,page->readingPosition(),page->encoding(),page->pinned});
    if(auto* page=currentPage())session.activePath=page->path;
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
    const QSignalBlocker blocker(tabs_);auto* active=currentPage();QVector<FilePage*> order;
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
    closed_.append(page->path);if(closed_.size()>20)closed_.removeFirst();
    if(fileWatcher_.files().contains(page->path))fileWatcher_.removePath(page->path);
    tabs_->removeTab(index);pages_->removeWidget(page);delete page;
    if(!tabs_->count()){pages_->setCurrentWidget(welcome_);hideSearch();}
    else if(activateNext)selectTab(tabs_->currentIndex());
    refreshStatus();
    scheduleSessionSave();
}

void MainWindow::selectTab(int index) {
    auto* page=pageAt(index);if(!page)return;
    pages_->setCurrentWidget(page);page->activate();refreshStatus();
    if(searchbar_->isVisible()){runSearch();search_->setFocus();}else if(page->editor)page->editor->setFocus();
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
    {const QSignalBlocker button(followButton_),action(followAction_);auto* page=currentPage();
        followButton_->setVisible(page!=nullptr);followButton_->setEnabled(page && !page->loading);followAction_->setEnabled(page && !page->loading);
        followButton_->setChecked(page && page->following());followAction_->setChecked(page && page->following());}
    if(auto* page=currentPage()) {
        encodingButton_->show();encodingButton_->setText((page->loading?textEncodingName(page->encoding())+QStringLiteral("…"):page->ready?page->content.encoding:textEncodingName(page->encoding()))+QStringLiteral(" ▾"));
        encodingButton_->setToolTip(QStringLiteral("%1 · 点击选择编码并重新读取文件").arg(textEncodingName(page->encoding())));
        auto name=QFileInfo(page->path).fileName();title_->setText(name+"  —  Zex");setWindowTitle(name+" — Zex");
        fileIcon_->setPixmap(Theme::fileIcon(page->path).pixmap(QSize(18,18),devicePixelRatioF()));
        pathLabel_->setText(QDir::toNativeSeparators(page->path));pathLabel_->setToolTip(pathLabel_->text());
        reloadButton_->setText(page->stale?QStringLiteral("文件已变化 · 重新载入"):QStringLiteral("重试打开"));
        reloadButton_->setVisible(page->stale || (page->attempted && !page->ready && !page->loading));
        if(page->ready){fileInfo_->setText(QStringLiteral("%1   ·   %2").arg(page->content.lineEnding,QLocale().formattedDataSize(page->content.originalSize)));position_->setText(page->editor->positionSummary());language_->setText(page->editor->language());}
        else {fileInfo_->setText(page->loading?QStringLiteral("正在读取…"):page->wasCanceled()?QStringLiteral("已取消加载"):QStringLiteral("无法读取"));position_->clear();language_->clear();}
    } else {
        encodingButton_->hide();
        fileIcon_->setPixmap(Theme::icon("file",Theme::colors().subtle).pixmap(QSize(18,18),devicePixelRatioF()));
        title_->setText("Zex");setWindowTitle("Zex");pathLabel_->setText(QStringLiteral("文件阅读器"));pathLabel_->setToolTip({});reloadButton_->hide();fileInfo_->setText("Zex 0.6.0");position_->clear();language_->clear();
    }
}

void MainWindow::showSearch() {
    auto* page=currentPage();if(!page||!page->ready)return;
    searchbar_->show();const auto selected=page->editor->selectedText();
    if(!selected.isEmpty()&&!selected.contains('\n')&&!selected.contains('\r'))search_->setText(selected);
    runSearch();search_->setFocus();search_->selectAll();
}
void MainWindow::hideSearch() {
    searchDebounce_.stop();searchbar_->hide();
    for(int i=0;i<tabs_->count();++i)if(auto* p=pageAt(i);p&&p->editor)p->editor->clearSearch();
    if(auto* p=currentPage();p&&p->editor)p->editor->setFocus();
}
void MainWindow::runSearch() {
    searchDebounce_.stop();if(!searchbar_->isVisible())return;
    if(auto* page=currentPage();page&&page->ready)page->editor->setSearch(search_->text(),case_->isChecked(),whole_->isChecked());
    updateSearchLabel();
}
void MainWindow::updateSearchLabel() {if(auto* p=currentPage();p&&p->ready)count_->setText(p->editor->matchSummary());else count_->setText(QStringLiteral("等待文件载入"));}
void MainWindow::setWrap(bool on) {
    wrap_=on;{QSignalBlocker a(wrapButton_),b(wrapAction_);wrapButton_->setChecked(on);wrapAction_->setChecked(on);}
    for(int i=0;i<tabs_->count();++i)if(auto* p=pageAt(i))p->setWordWrap(on);
}
void MainWindow::changeFont(int delta) {
    appearance_.points=qBound(8,appearance_.points+delta,32);
    for(int i=0;i<tabs_->count();++i)if(auto* p=pageAt(i))p->setAppearance(appearance_);
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
    refreshThemeIcons();applyNativeChrome();updateRecentMenu();refreshStatus();tabs_->update();
    if(!testMode_){appearance_.save(settings_);settings_.setValue("iconTheme",FileIcons::theme());Theme::saveTheme(settings_,theme);settings_.sync();}
}
void MainWindow::refreshThemeIcons() {
    const auto& c=Theme::colors();
    auto color=[&](const QString& role){return role=="accentText"?c.accentText:role=="subtle"?c.subtle:role=="text"?c.text:c.muted;};
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
    if(obj==titlebar_) {
        if(event->type()==QEvent::MouseButtonDblClick){if(static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton){toggleMaximize();return true;}}
        if(event->type()==QEvent::MouseButtonPress){if(static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton && windowHandle()){windowHandle()->startSystemMove();return true;}}
    }
    if(obj==search_ && event->type()==QEvent::KeyPress){auto* key=static_cast<QKeyEvent*>(event);if((key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter)&&key->modifiers().testFlag(Qt::ShiftModifier)){if(auto* p=currentPage();p&&p->ready)p->editor->findNext(true);return true;}}
    return QMainWindow::eventFilter(obj,event);
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
