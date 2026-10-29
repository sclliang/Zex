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
#include <QThreadPool>
#include <QSettings>
#include <QPointer>
#include <QMessageBox>
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
    app.setApplicationName("Zex");app.setOrganizationName("sixunited");app.setApplicationVersion("0.5.0.0");
    QThreadPool::globalInstance()->setMaxThreadCount(qMax(2,qMin(4,QThread::idealThreadCount())));
    QString reportPath,capturePath,searchText,sessionFile,verifyTheme,appearanceCapture;
    QPointer<AppearanceDialog> appearanceDialog;
    int verifyLine=0,verifyWait=500;
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
        else files.append(args[i]);
    }
    QSettings preferences("sixunited","Zex");
    Theme::apply(app,!verifyTheme.isEmpty()?verifyTheme:testMode?"graphite":Theme::storedTheme(preferences));
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
            if(*completed || !window.allLoaded() || !window.windowHandle()->isExposed())return;
            *completed=true;poll->stop();
            const auto loadedMs=static_cast<qint64>(GetTickCount64()-launchTick);
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
            QTimer::singleShot(verifyWait,&window,[&,reportPath,capturePath,loadedMs]{
                QJsonObject report{{"version","0.5.0.0"},{"qt",qVersion()},{"tabs",window.tabCount()},{"startup_to_ready_ms",loadedMs},{"plugin_paths",QJsonArray::fromStringList(QCoreApplication::libraryPaths())}};
                int loadedTabs=0;for(auto* page:window.findChildren<FilePage*>())if(page->ready)++loadedTabs;
                report.insert("loaded_tabs",loadedTabs);
                report.insert("pid",qint64(GetCurrentProcessId()));report.insert("single_instance",receiverName!=nullptr);
                report.insert("theme",Theme::id());report.insert("theme_dark",Theme::colors().dark);
                report.insert("session",QJsonDocument::fromJson(window.sessionSnapshot().serialize()).object());
                if(!sessionFile.isEmpty())report.insert("session_saved",window.saveSession());
                if(auto* page=window.currentPage()) {
                    report.insert("file",page->path);report.insert("ready",page->ready);report.insert("error",page->content.error);
                    report.insert("encoding",page->content.encoding);report.insert("language",page->editor->language());report.insert("search_matches",page->editor->matchCount());
                } else report.insert("ready",true);
                if(!capturePath.isEmpty())report.insert("screenshot_saved",window.grab().save(capturePath));
                if(appearanceDialog)report.insert("appearance_saved",appearanceDialog->grab().save(appearanceCapture));
                if(integrationDialog)report.insert("integration_saved",integrationDialog->grab().save(integrationCapture));
                if(!reportPath.isEmpty()){QFile output(reportPath);if(output.open(QIODevice::WriteOnly))output.write(QJsonDocument(report).toJson());}
                app.exit(report.value("ready").toBool()?0:1);
            });
        });
        poll->start();
        QTimer::singleShot(30000,&window,[&]{app.exit(2);});
    }
    return app.exec();
}
