#include "mainwindow.h"
#include "theme.h"
#include "appearancedialog.h"
#include "fileicons.h"
#include "filereceiver.h"
#include "integrationdialog.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QToolButton>
#include <QThreadPool>
#include <QSettings>
#include <QPointer>
#include <QMessageBox>
#include <QScreen>
#include <QWindow>
#include <windows.h>

extern "C" __declspec(dllexport) int runZex(int argc,char** argv,unsigned long long launchTick,const wchar_t* receiverName) {
    QElapsedTimer startup;startup.start();
    // The native launcher passes UTF-8. Qt's Windows narrow-argv path uses the
    // system code page, so decode our file arguments before constructing Qt.
    QStringList args;
    for (int i=0;i<argc;++i) args.append(QString::fromUtf8(argv[i]));
    wchar_t filename[32768];GetModuleFileNameW(nullptr,filename,32768);
    const auto root=QFileInfo(QString::fromWCharArray(filename)).absolutePath();
    const auto plugins=QDir(root).filePath("bin/plugins");
    QCoreApplication::setLibraryPaths({plugins});
    qputenv("QT_PLUGIN_PATH",plugins.toUtf8());
    QApplication app(argc,argv);
    app.setApplicationName("Zex");app.setOrganizationName("sixunited");app.setApplicationVersion("0.5.2.0");
    QThreadPool::globalInstance()->setMaxThreadCount(qMax(2,qMin(4,QThread::idealThreadCount())));
    QString reportPath,capturePath,searchText,sessionFile,verifyTheme,appearanceCapture;
    QPointer<AppearanceDialog> appearanceDialog;
    int verifyLine=0,verifyWait=500,verifyCancelPercent=-1;
    QString loadingCapture;
    QString verifyReady,verifyEncoding,integrationCapture;
    QPointer<IntegrationDialog> integrationDialog;
    QStringList files;
    bool testMode=false,verbatim=false;
    for(int i=1;i<args.size();++i) {
        if(verbatim){files.append(args[i]);continue;}
        if(args[i]=="--"){verbatim=true;continue;}
        if(args[i]=="--verify-instance"&&i+1<args.size()){++i;testMode=true;}
        else if(args[i]=="--verify-ready"&&i+1<args.size()){verifyReady=args[++i];testMode=true;}
        else if(args[i]=="--verify-wait-ms"&&i+1<args.size()){verifyWait=qBound(500,args[++i].toInt(),20000);testMode=true;}
        else if(args[i]=="--verify-encoding"&&i+1<args.size()){verifyEncoding=args[++i];testMode=true;}
        else if(args[i]=="--verify-integration-capture"&&i+1<args.size()){integrationCapture=args[++i];testMode=true;}
        else if(args[i]=="--verify-package"&&i+1<args.size()){reportPath=args[++i];testMode=true;}
        else if(args[i]=="--capture"&&i+1<args.size()){capturePath=args[++i];testMode=true;}
        else if(args[i]=="--find"&&i+1<args.size())searchText=args[++i];
        else if(args[i]=="--verify-session"&&i+1<args.size()){sessionFile=args[++i];testMode=true;}
        else if(args[i]=="--verify-line"&&i+1<args.size())verifyLine=qMax(0,args[++i].toInt());
        else if(args[i]=="--verify-theme"&&i+1<args.size()){verifyTheme=args[++i];testMode=true;}
        else if(args[i]=="--capture-appearance"&&i+1<args.size()){appearanceCapture=args[++i];testMode=true;}
        else if(args[i]=="--verify-cancel-percent"&&i+1<args.size()){verifyCancelPercent=qBound(0,args[++i].toInt(),99);testMode=true;}
        else if(args[i]=="--capture-loading"&&i+1<args.size()){loadingCapture=args[++i];testMode=true;}
        else files.append(args[i]);
    }
    QSettings preferences("sixunited","Zex");
    Theme::apply(app,!verifyTheme.isEmpty()?verifyTheme:testMode?"graphite":Theme::storedTheme(preferences));
    QTimer uiPulse;QElapsedTimer pulseClock;int maxUiGap=0,maxReadyUiGap=0,maxLoadingUiGap=0;qint64 shellReadyMs=0;bool measuringReady=false;
    bool loadingCaptured=false,cancelRequested=false;int canceledAt=-1;qint64 cancelMs=-1;
    if(testMode){pulseClock.start();uiPulse.setInterval(16);QObject::connect(&uiPulse,&QTimer::timeout,&app,[&]{const auto gap=int(pulseClock.restart());maxUiGap=qMax(maxUiGap,gap);if(measuringReady)maxReadyUiGap=qMax(maxReadyUiGap,gap);else if(shellReadyMs)maxLoadingUiGap=qMax(maxLoadingUiGap,gap);});uiPulse.start();}
    MainWindow window(testMode,sessionFile);
    FileReceiver receiver;
    QObject::connect(&receiver,&FileReceiver::filesRequested,&window,&MainWindow::receiveFiles);
    QObject::connect(&window,&MainWindow::closing,&receiver,&FileReceiver::close);
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&window,[&window]{window.saveSession();});
    window.show();
    // Paint the shell first; create lightweight tabs and load only the final active document.
    QTimer::singleShot(0,&window,[&]{
        window.restoreLastSession(files);
        if(!verifyEncoding.isEmpty())window.setCurrentEncoding(verifyEncoding);
        if(receiverName && !receiver.listen(receiverName)) {
            QMessageBox::critical(&window,QStringLiteral("Zex"),QStringLiteral("无法启动文件接收服务，请关闭窗口后重新打开。"));app.exit(4);return;
        }
        if(!verifyReady.isEmpty()){QFile ready(verifyReady);if(ready.open(QIODevice::WriteOnly))ready.write(QByteArray::number(GetCurrentProcessId()));}
    });
    if(!reportPath.isEmpty() || !capturePath.isEmpty() || !appearanceCapture.isEmpty() || !integrationCapture.isEmpty()) {
        auto* poll=new QTimer(&window);
        poll->setInterval(30);
        auto completed=std::make_shared<bool>(false);
        QObject::connect(poll,&QTimer::timeout,&window,[&,poll,completed,reportPath,capturePath,searchText]{
            if(!shellReadyMs && window.windowHandle()->isExposed())shellReadyMs=GetTickCount64()-launchTick;
            if(auto* page=window.currentPage();page && page->loading) {
                if(!loadingCapture.isEmpty() && !loadingCaptured && page->findChild<QWidget*>("loadBar")->isVisible())loadingCaptured=window.grab().save(loadingCapture);
                if(!cancelRequested && verifyCancelPercent>=0 && page->loadProgress()>=verifyCancelPercent) {
                    cancelRequested=true;canceledAt=page->loadProgress();QElapsedTimer elapsed;elapsed.start();
                    page->findChild<QToolButton*>("cancelLoad")->click();cancelMs=elapsed.elapsed();
                }
            }
            if(*completed || !window.allLoaded() || !window.windowHandle()->isExposed())return;
            *completed=true;poll->stop();
            const auto loadedMs=static_cast<qint64>(GetTickCount64()-launchTick);
            measuringReady=true;pulseClock.restart();
            if(verifyLine>0)if(auto* page=window.currentPage();page&&page->ready){
                page->editor->goToLine(verifyLine);
                page->editor->send(SCI_SETFIRSTVISIBLELINE,page->editor->send(SCI_VISIBLEFROMDOCLINE,verifyLine-1));
            }
            if(!searchText.isEmpty()){window.showSearch();window.findChild<QLineEdit*>("searchInput")->setText(searchText);}
            if(!appearanceCapture.isEmpty()) {
                appearanceDialog=new AppearanceDialog(window.currentPage()?window.currentPage()->editor->appearance():EditorAppearance::defaults(),FileIcons::theme(),&window);
                appearanceDialog->show();
            }
            if(!integrationCapture.isEmpty()) {
                integrationDialog=new IntegrationDialog(ShellIntegration(QCoreApplication::applicationFilePath(),"Software\\sixunited\\ZexDiagnostics"),&window);integrationDialog->show();
            }
            auto* finish=new QTimer(&window);finish->setInterval(30);
            const auto notBefore=GetTickCount64()+verifyWait;
            QObject::connect(finish,&QTimer::timeout,&window,[&,reportPath,capturePath,loadedMs,finish,notBefore]{
                if(GetTickCount64()<notBefore)return;
                if(auto* page=window.currentPage();page && page->editor && page->editor->searchBusy())return;
                finish->stop();
                QJsonObject report{{"version","0.5.2.0"},{"qt",qVersion()},{"tabs",window.tabCount()},{"startup_to_ready_ms",loadedMs},{"plugin_paths",QJsonArray::fromStringList(QCoreApplication::libraryPaths())}};
                const auto available=window.screen()->availableGeometry();
                auto rectJson=[](const QRect& r){return QJsonArray{r.x(),r.y(),r.width(),r.height()};};
                report.insert("startup_to_shell_ms",shellReadyMs);report.insert("max_ui_gap_ms",maxUiGap);report.insert("max_ready_ui_gap_ms",maxReadyUiGap);
                report.insert("max_loading_ui_gap_ms",maxLoadingUiGap);report.insert("loading_screenshot_saved",loadingCaptured);
                report.insert("cancel_requested",cancelRequested);report.insert("canceled_at_percent",canceledAt);report.insert("cancel_ms",cancelMs);
                report.insert("device_pixel_ratio",window.devicePixelRatioF());report.insert("screen_count",QGuiApplication::screens().size());
                report.insert("available_geometry",rectJson(available));report.insert("window_geometry",rectJson(window.frameGeometry()));
                report.insert("window_fits",available.contains(window.frameGeometry()));
                int loadedTabs=0;for(auto* page:window.findChildren<FilePage*>())if(page->ready)++loadedTabs;
                report.insert("loaded_tabs",loadedTabs);
                report.insert("pid",qint64(GetCurrentProcessId()));report.insert("single_instance",receiverName!=nullptr);
                report.insert("theme",Theme::id());report.insert("theme_dark",Theme::colors().dark);
                report.insert("session",QJsonDocument::fromJson(window.sessionSnapshot().serialize()).object());
                if(!sessionFile.isEmpty())report.insert("session_saved",window.saveSession());
                if(auto* page=window.currentPage()) {
                    report.insert("file",page->path);report.insert("ready",page->ready);report.insert("error",page->content.error);
                    report.insert("encoding",page->content.encoding);report.insert("language",page->editor->language());report.insert("search_matches",page->editor->matchCount());
                    report.insert("search_complete",!page->editor->searchBusy());
                    report.insert("load_canceled",page->wasCanceled());
                } else report.insert("ready",true);
                if(!capturePath.isEmpty())report.insert("screenshot_saved",window.grab().save(capturePath));
                if(appearanceDialog)report.insert("appearance_saved",appearanceDialog->grab().save(appearanceCapture));
                if(integrationDialog)report.insert("integration_saved",integrationDialog->grab().save(integrationCapture));
                if(appearanceDialog)report.insert("appearance_fits",appearanceDialog->screen()->availableGeometry().contains(appearanceDialog->frameGeometry()));
                if(integrationDialog)report.insert("integration_fits",integrationDialog->screen()->availableGeometry().contains(integrationDialog->frameGeometry()));
                if(!reportPath.isEmpty()){QFile output(reportPath);if(output.open(QIODevice::WriteOnly))output.write(QJsonDocument(report).toJson());}
                app.exit(report.value("ready").toBool()?0:1);
            });
            finish->start();
        });
        poll->start();
        QTimer::singleShot(30000,&window,[&]{app.exit(2);});
    }
    return app.exec();
}
