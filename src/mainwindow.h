#pragma once
#include "editor.h"
#include "logreader.h"
#include <QFileSystemWatcher>
#include <QMainWindow>
#include <QSettings>
#include <QTabBar>
#include <optional>

class QLabel;
class QLineEdit;
class QToolButton;
class QStackedWidget;
class QMenu;
class QVBoxLayout;
class QProgressBar;
class QPushButton;

class FilePage : public QWidget {
    Q_OBJECT
public:
    explicit FilePage(const QString& path, QWidget* parent = nullptr);
    ~FilePage() override;
    void reload();
    void cancelLoad();
    int loadProgress() const { return control_?control_->percent.load():ready?100:0; }
    bool wasCanceled() const { return canceled_; }
    void setFollowing(bool on);
    void resumeFollowing();
    bool following() const { return following_; }
    bool followPaused() const { return followPaused_; }
    bool followBusy() const { return followBusy_; }
    QString followNotice() const { return followNotice_; }
    void activate();
    void setAppearance(const EditorAppearance& appearance);
    void setWordWrap(bool on);
    void setEncoding(const QString& encoding);
    QString encoding() const { return encoding_; }
    void setReadingPosition(const ReadingPosition& position);
    ReadingPosition readingPosition() const;
    QString path;
    FileContent content;
    CodeEditor* editor = nullptr;
    bool ready = false;
    bool loading = false;
    bool stale = false;
    bool attempted = false;
    bool pinned = false;
signals:
    void loaded();
    void editorCreated();
    void loadingChanged();
    void followingChanged();
private:
    void setupFollowUi(QVBoxLayout* layout);
    void pollFollow();
    void applyLogUpdate(const std::shared_ptr<LogUpdate>& update,quint64 generation,qsizetype offset = 0);
    void refreshFollowUi();
    void trackFollowPosition();
    QTimer followTimer_;
    QWidget* followBar_ = nullptr;
    QLabel* followLabel_ = nullptr;
    QToolButton* resumeFollow_ = nullptr;
    QToolButton* stopFollow_ = nullptr;
    bool following_ = false,followPaused_ = false,followBusy_ = false,applyingFollow_ = false,followInitializing_ = false;
    QString followNotice_;
    std::shared_ptr<LogReader> logReader_;
    LoadControlPtr followControl_;
    quint64 followGeneration_ = 0;
    void ensureEditor();
    void applyPendingPosition();
    void updateLoadProgress();
    QStackedWidget* stack_;
    QLabel* message_;
    QWidget* messagePage_;
    QWidget* loadBar_;
    QLabel* loadLabel_;
    QProgressBar* loadProgress_;
    QPushButton* retry_;
    QToolButton* cancel_;
    QTimer progressTimer_;
    LoadControlPtr control_;
    quint64 loadGeneration_ = 0;
    bool canceled_ = false;
    bool committing_ = false;
    EditorAppearance appearance_;
    bool wrap_ = false;
    QString encoding_ = "auto";
    QString loadedEncoding_ = "auto";
    std::optional<ReadingPosition> pendingPosition_;
};

class FileTabs : public QTabBar {
    Q_OBJECT
public:
    explicit FileTabs(QWidget* parent = nullptr);
    bool isPinned(int index) const;
protected:
    QSize tabSizeHint(int index) const override;
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
private:
    int hovered_ = -1;
    int closePressed_ = -1;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(bool testMode = false, const QString& sessionFile = {});
    FilePage* openPath(const QString& path);
    void openPaths(const QStringList& paths);
    void receiveFiles(const QStringList& paths);
    void setCurrentEncoding(const QString& encoding);
    FilePage* currentPage() const;
    int tabCount() const;
    void closeTab(int index);
    void closeAllTabs();
    void closeOtherTabs(int index);
    void closeTabsToRight(int index);
    void setTabPinned(int index, bool pinned);
    bool isTabPinned(int index) const;
    void showSearch();
    void hideSearch();
    void openFiles();
    void showFilePicker(bool tabsOnly = false);
    bool allLoaded() const;
    void showAppearanceSettings();
    void applyAppearance(const EditorAppearance& appearance, const QString& iconTheme, const QString& colorTheme = {});
    void restoreLastSession(const QStringList& startupFiles = {});
    SessionSnapshot sessionSnapshot() const;
    bool saveSession();
signals:
    void fileReady(const QString& path);
    void closing();
protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
    void closeEvent(QCloseEvent*) override;
    void changeEvent(QEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
    bool nativeEvent(const QByteArray&, void*, qintptr*) override;
private:
    FilePage* pageAt(int index) const;
    FilePage* addPage(const QString& path, const ReadingPosition& position = {},const QString& encoding = "auto", bool pinned = false);
    void normalizePinnedTabs();
    void removeTab(int index, bool activateNext);
    void scheduleSessionSave();
    void buildTitleBar(QVBoxLayout* root);
    void buildMenus(QToolButton* file, QToolButton* view, QToolButton* help);
    QWidget* buildWelcome();
    void selectTab(int index);
    void refreshStatus();
    void runSearch();
    void updateSearchLabel();
    void setWrap(bool on);
    void changeFont(int delta);
    void jumpToLine();
    void reopenClosed();
    void updateRecentMenu();
    void toggleMaximize();
    void applyNativeChrome();
    void refreshThemeIcons();
    QSettings settings_;
    SessionStore sessionStore_;
    bool testMode_;
    bool sessionInitialized_ = false;
    bool restoringSession_ = false;
    bool wrap_ = false;
    EditorAppearance appearance_ = EditorAppearance::defaults();
    QStringList recent_;
    QStringList closed_;
    FileTabs* tabs_;
    QStackedWidget* pages_;
    QWidget* welcome_;
    QWidget* titlebar_;
    QWidget* searchbar_;
    QLabel* title_;
    QLabel* pathLabel_;
    QLabel* fileIcon_;
    QLabel* count_;
    QLabel* position_;
    QLabel* fileInfo_;
    QLabel* language_;
    QLabel* sessionNotice_;
    QLineEdit* search_;
    QToolButton* case_;
    QToolButton* whole_;
    QToolButton* wrapButton_;
    QToolButton* reloadButton_;
    QToolButton* allTabsButton_;
    QToolButton* followButton_;
    QToolButton* maximizeButton_ = nullptr;
    QToolButton* encodingButton_;
    QMenu* encodingMenu_;
    QMenu* recentMenu_;
    QAction* wrapAction_;
    QAction* followAction_;
    QFileSystemWatcher fileWatcher_;
    QTimer searchDebounce_;
    QTimer sessionSaveTimer_;
};
