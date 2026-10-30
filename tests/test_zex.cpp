#include "fileloader.h"
#include "mainwindow.h"
#include "theme.h"
#include "appearancedialog.h"
#include "fileicons.h"
#include "filepicker.h"
#include "filereceiver.h"
#include "integrationdialog.h"
#include "windowplacement.h"
#include "SciLexer.h"
#include <QApplication>
#include <QFile>
#include <QComboBox>
#include <QCheckBox>
#include <QFontComboBox>
#include <QLineEdit>
#include <QListView>
#include <QLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSpinBox>
#include <QScreen>
#include <QSaveFile>
#include <QProgressBar>
#include <QToolButton>
#include <QtConcurrentRun>
#include <QTemporaryDir>
#include <QTextCodec>
#include <QtTest>
#include <QUuid>
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

struct RegistrySandbox {
    QString prefix="Software\\sixunited\\ZexTests\\"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSettings settings{QString("HKEY_CURRENT_USER\\")+prefix,QSettings::NativeFormat};
    ~RegistrySandbox(){settings.sync();RegDeleteTreeW(HKEY_CURRENT_USER,reinterpret_cast<LPCWSTR>(prefix.utf16()));}
    QString value(const QString& name){settings.sync();return settings.value(name).toString();}
};

class ZexTests : public QObject {
    Q_OBJECT
private slots:
    void bookmarksNavigationAndReload() {
        CodeEditor editor;editor.resize(850,500);editor.show();QVERIFY(QTest::qWaitForWindowExposed(&editor));
        editor.setContent(decodeFile(QByteArray("line\n").repeated(150)),"notes.txt");
        editor.restoreReadingPosition({40,0,0,45,2,45,1});const auto position=editor.readingPosition();
        editor.setBookmarks({149,0,50,50,-1,999});QCOMPARE(editor.bookmarks(),QVector<qint64>({0,50,149}));
        editor.toggleBookmark(70);QVERIFY(editor.readingPosition()==position);QVERIFY(editor.nextBookmark());QCOMPARE(editor.readingPosition().caretLine,qint64(50));
        QVERIFY(editor.nextBookmark(true));QCOMPARE(editor.readingPosition().caretLine,qint64(0));QVERIFY(editor.nextBookmark(true));QCOMPARE(editor.readingPosition().caretLine,qint64(149));
        editor.toggleBookmark();QCOMPARE(editor.bookmarks(),QVector<qint64>({0,50,70}));
        editor.setColorTheme("paper");auto appearance=editor.appearance();appearance.points=15;editor.setAppearance(appearance);QCOMPARE(editor.bookmarks(),QVector<qint64>({0,50,70}));
        editor.setBookmarks({});QVERIFY(!editor.nextBookmark());
        QTemporaryDir directory;const auto path=directory.filePath("bookmark.txt");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("a\nb\nc");file.close();
        FilePage page(path);page.setBookmarks({0,2,999});QVERIFY(!page.editor);page.show();page.activate();QTRY_VERIFY(page.ready && !page.loading);QCOMPARE(page.bookmarks(),QVector<qint64>({0,2}));
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write("short");file.close();page.reload();QTRY_VERIFY(page.ready && !page.loading);QCOMPARE(page.bookmarks(),QVector<qint64>({0}));
    }
    void splitSessionIndependentPositionsAndLazyLoad() {
        QTemporaryDir directory;SessionSnapshot seed;
        for(int i=0;i<100;++i){auto path=directory.filePath(QString("file%1.txt").arg(i));QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));f.write(QByteArray("reading line 123\n").repeated(300));seed.tabs.append({path,{20,0,0,25,3,25,3},"auto",false,{3,50}});}
        seed.activePath=seed.tabs[7].path;seed.split={true,true,seed.tabs[2].path,seed.tabs[7].path,{140,0,0,147,3,147,3},"utf-8",60};
        const auto sessionPath=directory.filePath("session.json");SessionStore store(sessionPath);QVERIFY(store.write(seed));
        MainWindow window(true,sessionPath);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));window.restoreLastSession();QTRY_VERIFY(window.allLoaded());
        QVERIFY(window.splitEnabled());QCOMPARE(window.tabCount(),100);QCOMPARE(window.findChildren<CodeEditor*>().size(),2);QCOMPARE(window.currentPage(),window.secondaryPage());
        QCOMPARE(window.primaryPage()->path,seed.tabs[2].path);QTRY_VERIFY(window.primaryPage()->readingPosition()==seed.tabs[2].position);QTRY_VERIFY(window.secondaryPage()->readingPosition()==seed.split.rightPosition);
        QCOMPARE(window.secondaryPage()->encoding(),QString("utf-8"));QCOMPARE(window.secondaryPage()->bookmarks(),seed.tabs[7].bookmarks);
        window.activatePane(false);window.primaryPage()->editor->restoreReadingPosition({55,0,0,60,2,60,2});
        auto saved=window.sessionSnapshot();QVERIFY(!saved.split.activeRight);QCOMPARE(saved.split.leftPercent,60);QVERIFY(window.saveSession());window.close();
        MainWindow reopened(true,sessionPath);reopened.show();QVERIFY(QTest::qWaitForWindowExposed(&reopened));reopened.restoreLastSession();QTRY_VERIFY(reopened.allLoaded());
        QCOMPARE(reopened.findChildren<CodeEditor*>().size(),2);QTRY_VERIFY(reopened.sessionSnapshot().serialize()==saved.serialize());
        reopened.close();
        MainWindow cli(true,sessionPath);cli.show();cli.restoreLastSession({seed.tabs[9].path});QTRY_VERIFY(cli.allLoaded());
        QCOMPARE(cli.primaryPage()->path,seed.tabs[9].path);QCOMPARE(cli.secondaryPage()->path,seed.tabs[7].path);QCOMPARE(cli.findChildren<CodeEditor*>().size(),2);cli.close();
        auto json=QJsonDocument::fromJson(seed.serialize()).object();auto split=json["split"].toObject();split["rightPath"]=directory.filePath("missing-tab.txt");json["split"]=split;
        QVERIFY(!SessionSnapshot::deserialize(QJsonDocument(json).toJson()).split.enabled);
    }
    void splitSameFileBookmarksSearchAndClosing() {
        QTemporaryDir directory;const auto path=directory.filePath("same.txt"),other=directory.filePath("other.txt");
        for(const auto& name:{path,other}){QFile f(name);QVERIFY(f.open(QIODevice::WriteOnly));f.write(QByteArray("alpha 123 beta\n").repeated(300));}
        MainWindow window(true);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* left=window.openPath(path);QTRY_VERIFY(window.allLoaded());
        left->editor->restoreReadingPosition({20,0,0,25,2,25,2});window.setSplitEnabled(true);QTRY_VERIFY(window.allLoaded());auto* right=window.secondaryPage();
        QVERIFY(left!=right);QVERIFY(left->editor!=right->editor);right->editor->restoreReadingPosition({170,0,0,175,4,175,4});QCOMPARE(left->readingPosition().topLine,qint64(20));
        right->editor->toggleBookmark(175);QCOMPARE(left->bookmarks(),QVector<qint64>({175}));left->editor->toggleBookmark(20);QCOMPARE(right->bookmarks(),QVector<qint64>({20,175}));
        right->editor->setSearch("123",true,false,true);QTRY_VERIFY(!right->editor->searchBusy());QCOMPARE(right->editor->matchCount(),300);QCOMPARE(left->editor->matchCount(),0);
        const auto leftPosition=left->readingPosition();const auto rightPosition=right->readingPosition();
        window.activatePane(false);QCOMPARE(window.currentPage(),left);window.activatePane(true);QCOMPARE(window.currentPage(),right);
        auto* changed=window.openPath(other);QTRY_VERIFY(window.allLoaded());QCOMPARE(window.primaryPage(),left);QCOMPARE(changed,window.secondaryPage());QCOMPARE(changed->path,other);
        window.openPath(path);QTRY_VERIFY(window.allLoaded());QTRY_VERIFY(window.secondaryPage()->readingPosition()==rightPosition);QVERIFY(left->readingPosition()==leftPosition);
        window.setTabPinned(0,true);window.closeTab(0);QVERIFY(window.splitEnabled());QCOMPARE(window.tabCount(),2);
        window.setTabPinned(0,false);window.closeTab(0);QVERIFY(!window.splitEnabled());QTRY_VERIFY(window.allLoaded());QCOMPARE(window.currentPage()->path,other);QVERIFY(window.currentPage()->ready);
        window.setSplitEnabled(true);QTRY_VERIFY(window.allLoaded());window.closeAllTabs();QCOMPARE(window.tabCount(),0);QVERIFY(!window.splitEnabled());QVERIFY(!window.currentPage());window.close();
    }
    void searchControlsAndResultsFollowActivePane() {
        QTemporaryDir directory;const auto path=directory.filePath("query.txt");QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));f.write("alpha 123\nalpha 456\nlast 789\n");f.close();
        MainWindow window(true);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));window.openPath(path);QTRY_VERIFY(window.allLoaded());window.setSplitEnabled(true);QTRY_VERIFY(window.allLoaded());window.showSearch();
        auto* input=window.findChild<QLineEdit*>("searchInput");auto* regex=window.findChild<QToolButton*>("regexSearch");auto* selection=window.findChild<QToolButton*>("selectionSearch");auto* results=window.findChild<QToolButton*>("searchResultsButton");QVERIFY(input && regex && selection && results);
        auto* right=window.secondaryPage()->editor;right->send(SCI_SETSEL,0,20);QTest::mouseClick(selection,Qt::LeftButton);input->setText("\\d+");QTest::mouseClick(regex,Qt::LeftButton);QTRY_VERIFY(!right->searchBusy());QCOMPARE(right->matchCount(),2);
        QTest::mouseClick(results,Qt::LeftButton);auto* list=window.findChild<QListView*>("searchResultsList");QVERIFY(list);QCOMPARE(list->model()->rowCount(),2);
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,list->visualRect(list->model()->index(1,0)).center());QCOMPARE(right->selectedText(),QString("456"));
        window.activatePane(false);auto* left=window.primaryPage()->editor;QTRY_VERIFY(!left->searchBusy());QVERIFY(!selection->isChecked());QCOMPARE(left->matchCount(),3);QCOMPARE(list->model()->rowCount(),3);
        input->setText("(");QTest::keyClick(input,Qt::Key_Return);QTRY_VERIFY(!left->searchBusy());QVERIFY(!left->searchError().isEmpty());QVERIFY(!input->toolTip().isEmpty());
        window.hideSearch();QCOMPARE(left->matchCount(),0);QCOMPARE(right->matchCount(),0);QVERIFY(!list->isVisible());window.close();
    }
    void regexSearchUnicodeLimitsAndCancellation() {
        auto control=std::make_shared<RegexControl>();RegexRequest request;
        request.text=QStringLiteral("first\r\n中文 123\r\n中文 456\r\nlast").toUtf8();request.pattern=QStringLiteral("^中文 \\d+$").toUtf8();request.caseSensitive=true;
        auto result=searchRegex(request,control);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.matches.size(),2);
        QCOMPARE(request.text.mid(result.matches[0].first,result.matches[0].second-result.matches[0].first),QStringLiteral("中文 123").toUtf8());
        request.pattern="(?=中)";result=searchRegex(request,control);QCOMPARE(result.matches.size(),2);QCOMPARE(result.matches[0].first,result.matches[0].second);
        request.pattern="(";result=searchRegex(request,control);QVERIFY(!result.error.isEmpty());
        request.pattern="(a+)+$";request.text=QByteArray(100000,'a')+"!";QElapsedTimer elapsed;elapsed.start();result=searchRegex(request,control);QVERIFY(!result.error.isEmpty());QVERIFY(elapsed.elapsed()<4000);
        request.pattern="z";request.text=QByteArray(64*1024*1024,'a');auto future=QtConcurrent::run([request,control]{return searchRegex(request,control);});QTest::qWait(10);control->canceled.store(true);QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(),2000);QVERIFY(future.result().canceled);
        control=std::make_shared<RegexControl>();request.text="\xff";result=searchRegex(request,control);QVERIFY(!result.error.isEmpty());
    }
    void editorRegexSelectionResultsAndRaces() {
        CodeEditor editor;editor.resize(850,500);editor.show();QVERIFY(QTest::qWaitForWindowExposed(&editor));
        const auto bytes=QStringLiteral("中文 one 123\n中文 two 456\nlast 789\n").toUtf8();editor.setContent(decodeFile(bytes),"sample.txt");
        editor.setSearch("\\d+",true,false,true);QTRY_VERIFY(!editor.searchBusy());QVERIFY2(editor.searchError().isEmpty(),qPrintable(editor.searchError()));QCOMPARE(editor.matchCount(),3);QCOMPARE(editor.selectedText(),QString("123"));
        QVERIFY(editor.resultText(0).contains(QStringLiteral("中文 one 123")));QVERIFY(editor.selectResult(1));QCOMPARE(editor.selectedText(),QString("456"));
        QVERIFY(editor.findNext());QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.selectedText(),QString("789"));QVERIFY(editor.findNext());QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.selectedText(),QString("123"));
        QVERIFY(editor.findNext(true));QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.selectedText(),QString("789"));
        const qint64 start=bytes.indexOf("two"),end=bytes.indexOf("last");
        editor.setSearch("\\d+",true,false,true,SearchMatch{start,end});QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.matchCount(),1);QCOMPARE(editor.selectedText(),QString("456"));
        editor.setSearch("中文",true,false,false,SearchMatch{bytes.indexOf('\n')+1,bytes.size()});QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.matchCount(),1);QVERIFY(editor.findNext());QCOMPARE(editor.send(SCI_GETSELECTIONSTART),sptr_t(bytes.indexOf('\n')+1));
        editor.setSearch(".",true,false,true);editor.cancelSearch();QVERIFY(editor.searchStopped());QVERIFY(!editor.searchBusy());editor.setSearch("789",true,false);QTRY_VERIFY(!editor.searchBusy());QTest::qWait(50);QCOMPARE(editor.matchCount(),1);QCOMPARE(editor.selectedText(),QString("789"));
        editor.setSearch("\\d+",true,false,true);editor.appendLogText("new 999\n","sample.txt",false);QTest::qWait(50);QVERIFY(editor.searchStopped());QVERIFY(editor.searchError().contains(QStringLiteral("内容已更新")));
    }
    void regexNavigationBeyondResultCap() {
        CodeEditor editor;editor.setContent(decodeFile(QByteArray("hit\n").repeated(12000)),"hits.log");editor.setSearch("h.t",true,false,true);QTRY_VERIFY_WITH_TIMEOUT(!editor.searchBusy(),5000);QCOMPARE(editor.matchCount(),10000);
        editor.send(SCI_SETSEL,11000*4,11000*4+3);QVERIFY(editor.findNext());QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.send(SCI_GETSELECTIONSTART),sptr_t(11001*4));
        QVERIFY(editor.findNext(true));QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.send(SCI_GETSELECTIONSTART),sptr_t(11000*4));
    }
    void regexCaretAndEmptyMatchNavigation() {
        CodeEditor editor;editor.setContent(decodeFile("123 abc 456"),"test.txt");
        editor.setSearch("\\d+",true,false,true,{},false);QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.send(SCI_GETCURRENTPOS),sptr_t(0));
        QVERIFY(editor.findNext());QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.selectedText(),QString("123"));
        QVERIFY(editor.findNext());QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.selectedText(),QString("456"));
        QVERIFY(editor.findNext());QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.selectedText(),QString("123"));
        editor.send(SCI_SETSEL,0,7);QVERIFY(editor.findNext());QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.selectedText(),QString("456"));
        editor.send(SCI_SETEMPTYSELECTION,0);editor.setSearch("^|$",true,false,true);QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.matchCount(),2);
        QVERIFY(editor.findNext());QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.send(SCI_GETCURRENTPOS),sptr_t(11));
        QVERIFY(editor.findNext());QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.send(SCI_GETCURRENTPOS),sptr_t(0));
        QVERIFY(editor.findNext(true));QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.send(SCI_GETCURRENTPOS),sptr_t(11));
    }
    void logReaderEncodingBoundaries_data() {
        QTest::addColumn<QString>("encoding");
        for(const auto& id:textEncodingIds())if(id!="auto")QTest::newRow(qPrintable(id))<<id;
    }
    void logReaderEncodingBoundaries() {
        QFETCH(QString,encoding);QTemporaryDir directory;const auto path=directory.filePath("stream.log");
        auto* codec=QTextCodec::codecForName(encoding.toLatin1());QVERIFY(codec);
        const QString initial=QStringLiteral("header\r\n");const QString addition=encoding=="gbk"?QStringLiteral("中文\r\n尾部"):QStringLiteral("中文🙂\r\n尾部");
        QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);
        const auto first=codec->fromUnicode(initial.constData(),initial.size(),&state);
        const auto extra=codec->fromUnicode(addition.constData(),addition.size(),&state);
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(first);file.close();
        LogReader reader(encoding);auto control=std::make_shared<LoadControl>();auto update=reader.poll(path,control);
        QVERIFY2(!update.failed,qPrintable(update.notice));QVERIFY(update.reset);QVERIFY(update.replacement.document);
        CodeEditor editor;editor.setPreparedContent(update.replacement,path);
        for(char byte:extra) {
            QVERIFY(file.open(QIODevice::Append));file.write(&byte,1);file.close();update=reader.poll(path,control);
            QVERIFY2(!update.failed,qPrintable(update.notice));QVERIFY(!update.reset);QVERIFY(editor.appendLogText(update.appended,path,false));
        }
        const auto expected=(initial+addition).toUtf8();
        QCOMPARE(decodeFile(first+extra,encoding).utf8,expected);
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(editor.send(SCI_GETCHARACTERPOINTER)),editor.send(SCI_GETLENGTH)),expected);
        QCOMPARE(update.content.lineEnding,QString("CRLF"));QCOMPARE(editor.send(SCI_GETREADONLY),sptr_t(1));
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),first+extra);
    }
    void logReaderRotationTruncationAndPartialInitial() {
        QTemporaryDir directory;const auto path=directory.filePath("日志.log");
        auto write=[&](const QByteArray& bytes){QFile f(path);if(!f.open(QIODevice::WriteOnly))return false;return f.write(bytes)==bytes.size();};
        const auto chinese=QStringLiteral("中").toUtf8();QVERIFY(write("first\n"+chinese.left(1)));
        LogReader reader("auto");auto control=std::make_shared<LoadControl>();auto update=reader.poll(path,control);QVERIFY(update.reset && update.replacement.document);QCOMPARE(update.content.encoding,QString("UTF-8"));
        CodeEditor editor;editor.setPreparedContent(update.replacement,path);QCOMPARE(editor.send(SCI_GETLENGTH),sptr_t(6));
        QFile file(path);QVERIFY(file.open(QIODevice::Append));file.write(chinese.mid(1));file.close();update=reader.poll(path,control);QVERIFY(!update.reset);QCOMPARE(update.appended,chinese);
        QVERIFY(write("x"));update=reader.poll(path,control);QVERIFY(update.reset && update.replacement.document);
        const auto bytes=QByteArray(12000,'a');QVERIFY(write(bytes));update=reader.poll(path,control);QVERIFY(update.reset);
        QSaveFile rotated(path);QVERIFY(rotated.open(QIODevice::WriteOnly));auto replaced=bytes;replaced[6000]='b';rotated.write(replaced);QVERIFY(rotated.commit());
        update=reader.poll(path,control);QVERIFY(update.reset);editor.setPreparedContent(update.replacement,path);QCOMPARE(editor.send(SCI_GETCHARAT,6000),sptr_t('b'));
        QVERIFY(QFile::remove(path));update=reader.poll(path,control);QVERIFY(update.waiting);QVERIFY(!update.failed);
        QVERIFY(write("returned\n"));update=reader.poll(path,control);QVERIFY(update.reset);
        QVERIFY(write({}));update=reader.poll(path,control);QVERIFY(update.reset && update.replacement.document);editor.setPreparedContent(update.replacement,path);QCOMPARE(editor.send(SCI_GETLENGTH),sptr_t(0));
        const auto resumed=QStringLiteral("中文🙂\n");auto* utf16=QTextCodec::codecForName("UTF-16LE");QVERIFY(write(utf16->fromUnicode(resumed)));
        update=reader.poll(path,control);QVERIFY(update.reset);QCOMPARE(update.content.encoding,QString("UTF-16 LE"));editor.setPreparedContent(update.replacement,path);
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(editor.send(SCI_GETCHARACTERPOINTER)),editor.send(SCI_GETLENGTH)),resumed.toUtf8());
        QVERIFY(file.open(QIODevice::ReadWrite));QVERIFY(file.resize(257ll*1024*1024));file.close();update=reader.poll(path,control);QVERIFY(update.failed);QVERIFY(update.notice.contains("256 MiB"));
        control->canceled.store(true);update=reader.poll(path,control);QVERIFY(!update.replacement.document);QVERIFY(update.appended.isEmpty());
    }
    void appendPreservesSelectionAndUpdatesSearchBoundary() {
        CodeEditor editor;editor.resize(900,500);editor.show();QVERIFY(QTest::qWaitForWindowExposed(&editor));
        editor.setContent(decodeFile(QByteArray("line\n").repeated(300)+"ERR"),"trace.log");
        editor.setSearch("ERR",true,true);QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.matchCount(),1);
        editor.restoreReadingPosition({40,0,0,45,2,45,0});QTest::qWait(40);const auto position=editor.readingPosition();
        QVERIFY(editor.appendLogText("OR\nERR\n","trace.log",false));QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.matchCount(),1);
        QCOMPARE((SessionSnapshot{{{"C:/trace.log",editor.readingPosition()}}}.serialize()),(SessionSnapshot{{{"C:/trace.log",position}}}.serialize()));QCOMPARE(editor.selectedText(),QString("li"));
        QVERIFY(editor.appendLogText("ERR","trace.log",false));QTRY_COMPARE(editor.matchCount(),2);
        QCOMPARE((SessionSnapshot{{{"C:/trace.log",editor.readingPosition()}}}.serialize()),(SessionSnapshot{{{"C:/trace.log",position}}}.serialize()));editor.scrollToEnd();QTRY_VERIFY(editor.atDocumentEnd());QCOMPARE(editor.selectedText(),QString("li"));
    }
    void followPauseResumeRotationAndClose() {
        QTemporaryDir directory;const auto path=directory.filePath("follow.log");QFile file(path);
        const auto initial=QByteArray("log line\n").repeated(400);QVERIFY(file.open(QIODevice::WriteOnly));file.write(initial);file.close();
        MainWindow window(true);window.show();auto* page=window.openPath(path);QTRY_VERIFY(page->ready && !page->loading);
        page->setFollowing(true);QTRY_VERIFY(!page->followBusy());QVERIFY(page->following());QTRY_VERIFY(page->editor->atDocumentEnd());
        QVERIFY(file.open(QIODevice::Append));file.write("new event\n");file.close();
        QTRY_COMPARE(page->editor->send(SCI_GETLENGTH),sptr_t(initial.size()+10));QVERIFY(!page->followPaused());
        page->editor->restoreReadingPosition({40,0,0,44,3,44,0});QTRY_VERIFY(page->followPaused());const auto position=page->readingPosition();
        QVERIFY(file.open(QIODevice::Append));file.write("paused event\n");file.close();QTest::qWait(650);
        QVERIFY(page->readingPosition()==position);QCOMPARE(page->editor->send(SCI_GETLENGTH),sptr_t(initial.size()+10));
        page->resumeFollowing();QTRY_COMPARE(page->editor->send(SCI_GETLENGTH),sptr_t(initial.size()+23));QTRY_VERIFY(page->editor->atDocumentEnd());
        page->editor->restoreReadingPosition({30,0,0,34,3,34,0});QTRY_VERIFY(page->followPaused());
        QSaveFile replacement(path);QVERIFY(replacement.open(QIODevice::WriteOnly));replacement.write("rotated\n");QVERIFY(replacement.commit());QTest::qWait(600);
        QCOMPARE(page->editor->send(SCI_GETLENGTH),sptr_t(initial.size()+23));page->resumeFollowing();QTRY_COMPARE(page->editor->send(SCI_GETLENGTH),sptr_t(8));
        QVERIFY(QFile::remove(path));QTRY_VERIFY(!page->followNotice().isEmpty());QVERIFY(page->following());QCOMPARE(page->editor->send(SCI_GETLENGTH),sptr_t(8));
        QVERIFY(file.open(QIODevice::WriteOnly));file.write("back again\n");file.close();QTRY_COMPARE(page->editor->send(SCI_GETLENGTH),sptr_t(11));
        page->setFollowing(false);QVERIFY(file.open(QIODevice::Append));file.write("stopped\n");file.close();QTest::qWait(600);QCOMPARE(page->editor->send(SCI_GETLENGTH),sptr_t(11));
        page->setFollowing(true);window.closeTab(0);QCOMPARE(window.tabCount(),0);QTest::qWait(150);window.close();
        MainWindow reopened(true);reopened.show();auto* ordinary=reopened.openPath(path);QTRY_VERIFY(ordinary->ready && !ordinary->loading);QVERIFY(!ordinary->following());reopened.close();
    }
    void filePickerRankingAndKeyboard() {
        const auto choices=mergeFileChoices({{"C:/alpha/mainwindow.cpp",true,true},{"C:/beta/mainwindow.cpp",true,false},{QStringLiteral("C:/中文/日志.log"),true,false}},
            {"c:\\ALPHA\\mainwindow.cpp","C:/recent/notes.txt","C:/other/mwc.txt"});
        QCOMPARE(choices.size(),5);QVERIFY(choices[0].opened);QVERIFY(choices[0].pinned);
        QCOMPARE(rankFileChoices(choices,"beta mwc"),QVector<int>{1});
        QCOMPARE(rankFileChoices(choices,QStringLiteral("中文 日志")),QVector<int>{2});
        QCOMPARE(rankFileChoices(choices,"mwc").first(),4);QVERIFY(rankFileChoices(choices,"absent").isEmpty());
        FilePickerDialog dialog(choices,false);dialog.show();
        auto* input=dialog.findChild<QLineEdit*>("filePickerInput");auto* list=dialog.findChild<QListView*>("filePickerList");
        QCOMPARE(list->model()->rowCount(),5);QTest::keyClick(input,Qt::Key_Down);QCOMPARE(dialog.selectedPath(),choices[1].path);
        input->setText("notes");QCOMPARE(list->model()->rowCount(),1);QCOMPARE(dialog.selectedPath(),choices[3].path);
        input->setText("no match");QTest::keyClick(input,Qt::Key_Return);QVERIFY(dialog.isVisible());
        input->setText("beta mwc");QTest::keyClick(input,Qt::Key_Return);QCOMPARE(dialog.result(),int(QDialog::Accepted));
        FilePickerDialog empty({},false);empty.show();QTest::keyClick(empty.findChild<QLineEdit*>(),Qt::Key_Escape);QVERIFY(!empty.isVisible());
    }
    void followBurstBackgroundAndGeneration() {
        QTemporaryDir directory;const auto path=directory.filePath("burst.log"),other=directory.filePath("other.txt");
        const auto initial=QByteArray("line\n").repeated(10000),extra=QStringLiteral("新增🙂 日志\r\n").repeated(90000).toUtf8();
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(initial);file.close();
        QFile second(other);QVERIFY(second.open(QIODevice::WriteOnly));second.write("another file\n");second.close();
        MainWindow window(true);window.show();auto* page=window.openPath(path);QTRY_VERIFY(page->ready && !page->loading);
        page->setFollowing(true);page->setFollowing(false);page->setFollowing(true);QTRY_VERIFY(!page->followBusy());
        QVERIFY(file.open(QIODevice::Append));file.write(extra);file.close();QTRY_COMPARE_WITH_TIMEOUT(page->editor->send(SCI_GETLENGTH),sptr_t(initial.size()+extra.size()),10000);
        QTRY_VERIFY(!page->followBusy());QVERIFY(!page->followPaused());
        const auto document=page->editor->send(SCI_GETDOCPOINTER);
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(page->editor->send(SCI_GETCHARACTERPOINTER)),page->editor->send(SCI_GETLENGTH)),initial+extra);
        window.openPath(other);QTRY_VERIFY(window.allLoaded());QTest::qWait(100);
        QVERIFY(file.open(QIODevice::Append));file.write("hidden append\n");file.close();QTest::qWait(650);QCOMPARE(page->editor->send(SCI_GETLENGTH),sptr_t(initial.size()+extra.size()));
        window.openPath(path);QTRY_COMPARE(page->editor->send(SCI_GETLENGTH),sptr_t(initial.size()+extra.size()+14));QCOMPARE(page->editor->send(SCI_GETDOCPOINTER),document);
        page->reload();QVERIFY(!page->following());QTRY_VERIFY(!page->loading);QVERIFY(page->ready);
        page->setFollowing(true);QTRY_VERIFY(!page->followBusy());page->setEncoding("utf-8");QVERIFY(!page->following());QTRY_VERIFY(!page->loading);window.close();
    }
    void pickerKeepsDormantTabsLazy() {
        QTemporaryDir directory;SessionSnapshot seed;
        for(int i=0;i<80;++i)seed.tabs.append({directory.filePath(QString("file-%1.log").arg(i))});
        for(int i:{0,79}){QFile file(seed.tabs[i].path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("hello\n");}
        seed.activePath=seed.tabs[0].path;const auto path=directory.filePath("session.json");QVERIFY(SessionStore(path).write(seed));
        MainWindow window(true,path);window.show();window.restoreLastSession();QTRY_VERIFY(window.allLoaded());
        QTimer::singleShot(40,&window,[&]{auto* dialog=window.findChild<FilePickerDialog*>();QVERIFY(dialog);QCOMPARE(window.findChildren<CodeEditor*>().size(),1);dialog->reject();});
        window.showFilePicker();QCOMPARE(window.findChildren<CodeEditor*>().size(),1);
        QTimer::singleShot(40,&window,[&]{auto* dialog=window.findChild<FilePickerDialog*>();QVERIFY(dialog);auto* input=dialog->findChild<QLineEdit*>();input->setText("file-79");QTest::keyClick(input,Qt::Key_Return);});
        window.showFilePicker(true);QTRY_VERIFY(window.allLoaded());QCOMPARE(window.currentPage()->path,seed.tabs[79].path);
        QCOMPARE(window.tabCount(),80);QCOMPARE(window.findChildren<CodeEditor*>().size(),2);
        window.openPath(seed.tabs[0].path);window.closeTab(79);QCOMPARE(window.tabCount(),79);
        QTimer::singleShot(40,&window,[&]{auto* dialog=window.findChild<FilePickerDialog*>();QVERIFY(dialog);auto* input=dialog->findChild<QLineEdit*>();input->setText("file-79");QTest::keyClick(input,Qt::Key_Return);});
        window.currentPage()->editor->setFocus();QTest::keyClick(window.currentPage()->editor,Qt::Key_P,Qt::ControlModifier);
        QTRY_VERIFY(window.allLoaded());QCOMPARE(window.tabCount(),80);QCOMPARE(window.currentPage()->path,seed.tabs[79].path);
        window.close();
    }
    void pinnedTabsSurviveReorderCloseAndRestart() {
        QTemporaryDir directory;SessionSnapshot seed;
        for(int i=0;i<6;++i)seed.tabs.append({directory.filePath(QString("tab-%1.txt").arg(i))});
        QFile file(seed.tabs[4].path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("active file\n");file.close();
        seed.activePath=seed.tabs[4].path;const auto path=directory.filePath("session.json");SessionStore store(path);QVERIFY(store.write(seed));
        MainWindow window(true,path);window.show();window.restoreLastSession();QTRY_VERIFY(window.allLoaded());
        auto* active=window.currentPage();window.setTabPinned(2,true);window.setTabPinned(4,true);
        QCOMPARE(window.sessionSnapshot().tabs[0].path,seed.tabs[2].path);QVERIFY(window.isTabPinned(1));QCOMPARE(window.currentPage(),active);
        QCOMPARE(window.findChildren<CodeEditor*>().size(),1);
        auto* tabs=window.findChild<FileTabs*>();tabs->moveTab(0,5);QVERIFY(window.isTabPinned(0));QVERIFY(window.isTabPinned(1));QVERIFY(!window.isTabPinned(2));
        QCOMPARE(window.currentPage(),active);window.closeTab(0);QCOMPARE(window.tabCount(),6);
        QTest::mouseClick(tabs,Qt::MiddleButton,Qt::NoModifier,tabs->tabRect(0).center());QCOMPARE(window.tabCount(),6);
        window.closeTabsToRight(3);QCOMPARE(window.tabCount(),4);QVERIFY(window.isTabPinned(0));
        QVERIFY(window.saveSession());const auto saved=window.sessionSnapshot();window.close();
        MainWindow reopened(true,path);reopened.show();reopened.restoreLastSession();QTRY_VERIFY(reopened.allLoaded());
        QCOMPARE(reopened.sessionSnapshot().serialize(),saved.serialize());QCOMPARE(reopened.findChildren<CodeEditor*>().size(),1);
        reopened.closeOtherTabs(2);QCOMPARE(reopened.tabCount(),3);reopened.closeAllTabs();QCOMPARE(reopened.tabCount(),2);
        QVERIFY(reopened.isTabPinned(0));QVERIFY(reopened.isTabPinned(1));reopened.setTabPinned(0,false);reopened.setTabPinned(0,false);reopened.closeAllTabs();QCOMPARE(reopened.tabCount(),0);reopened.close();
        const auto legacy=SessionSnapshot::deserialize(R"({"version":1,"tabs":[{"path":"C:/old.txt"}]})");QVERIFY(!legacy.tabs[0].pinned);
    }
    void monospacedMeasurementPreservesCharacterPositions() {
        const auto text=QStringLiteral("ASCII fi => != 0123456789\t中文🙂 e\u0301\n").repeated(20).toUtf8();
        for(const auto& family:QStringList{"Cascadia Code","Consolas","Arial"}) {
            CodeEditor fast,reference;auto appearance=EditorAppearance::defaults();appearance.family=family;
            fast.setAppearance(appearance);reference.setAppearance(appearance);
            fast.setContent(decodeFile(text),"widths.txt");reference.setContent(decodeFile(text),"widths.txt");
            for(int style=0;style<256;++style)reference.send(SCI_STYLESETCHECKMONOSPACED,style,0);
            for(sptr_t pos=0;pos<text.size();pos=fast.send(SCI_POSITIONAFTER,pos)) {
                QVERIFY(qAbs(fast.send(SCI_POINTXFROMPOSITION,0,pos)-reference.send(SCI_POINTXFROMPOSITION,0,pos))<=1);
                QCOMPARE(fast.send(SCI_POINTYFROMPOSITION,0,pos),reference.send(SCI_POINTYFROMPOSITION,0,pos));
            }
            QCOMPARE(fast.send(SCI_GETREADONLY),sptr_t(1));
        }
    }
    void preparedDocumentsPreserveUtf8AndCancellation() {
        QTemporaryDir dir;const auto path=dir.filePath("prepared.txt");
        QByteArray bytes(2*1024*1024-2,'x');bytes+=QStringLiteral("中文🙂\r\nlast marker\n").toUtf8();
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),qint64(bytes.size()));file.close();
        auto control=std::make_shared<LoadControl>();
        auto future=QtConcurrent::run([path,control] {return prepareFile(path,"auto",control);});
        QTRY_VERIFY(future.isFinished());auto result=future.result();QVERIFY(result.content.error.isEmpty());QVERIFY(result.document);QVERIFY(result.content.utf8.isEmpty());
        QCOMPARE(control->percent.load(),100);CodeEditor editor;editor.setPreparedContent(result,path);
        QCOMPARE(editor.send(SCI_GETLENGTH),sptr_t(bytes.size()));
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(editor.send(SCI_GETCHARACTERPOINTER)),bytes.size()),bytes);
        editor.setSearch(QStringLiteral("中文🙂"),true,false);QTRY_COMPARE(editor.matchCount(),1);QCOMPARE(editor.selectedText(),QStringLiteral("中文🙂"));
        QCOMPARE(editor.send(SCI_GETREADONLY),sptr_t(1));QCOMPARE(editor.send(SCI_GETUNDOCOLLECTION),sptr_t(0));
        control=std::make_shared<LoadControl>();control->canceled.store(true);
        const auto canceled=prepareFile(path,"auto",control);QVERIFY(canceled.content.canceled);QVERIFY(!canceled.document);
        QVERIFY(readFile("unreachable-file.txt","auto",control).canceled);
    }
    void canceledReloadKeepsVisibleDocumentAndReadingPosition() {
        QTemporaryDir dir;const auto path=dir.filePath("reload.cpp");
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));const auto bytes=QByteArray("int sample() {\n    return 42;\n}\n").repeated(300);file.write(bytes);file.close();
        MainWindow window(true);window.show();auto* page=window.openPath(path);QTRY_VERIFY(page->ready && !page->loading);
        auto* editor=page->editor;editor->setSearch("sample",true,true);QTRY_VERIFY(!editor->searchBusy());
        editor->send(SCI_FOLDLINE,0,SC_FOLDACTION_CONTRACT);editor->restoreReadingPosition({120,0,0,125,4,125,1});
        const auto position=page->readingPosition();const auto document=editor->send(SCI_GETDOCPOINTER);
        for(int i=0;i<4;++i){page->reload();QVERIFY(page->loading && page->ready);page->cancelLoad();QVERIFY(!page->loading && page->ready && page->wasCanceled());}
        page->setEncoding("gbk");QVERIFY(page->loading);page->cancelLoad();QCOMPARE(page->encoding(),QString("auto"));
        QTest::qWait(100);QCOMPARE(editor->send(SCI_GETDOCPOINTER),document);QCOMPARE(editor->send(SCI_GETFOLDEXPANDED,0),sptr_t(0));
        QCOMPARE(editor->matchCount(),300);QVERIFY(page->readingPosition()==position);
        page->reload();QTRY_VERIFY(!page->loading);QVERIFY(page->ready && !page->wasCanceled());QVERIFY(page->readingPosition()==position);
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),bytes);window.close();
    }
    void loadingProgressCancelRetryAndClose() {
        QTemporaryDir dir;const auto path=dir.filePath("large.log"),sessionPath=dir.filePath("session.json");
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));const auto block=QByteArray(1023,'x').append('\n').repeated(1024);
        for(int i=0;i<128;++i)QCOMPARE(file.write(block),qint64(block.size()));file.close();
        SessionStore store(sessionPath);QVERIFY(store.write({{{path,{120,0,0,125,4,125,1}}},path}));
        MainWindow window(true,sessionPath);window.show();window.restoreLastSession();auto* page=window.currentPage();QVERIFY(page);
        auto* progress=page->findChild<QProgressBar*>("loadProgress");auto* cancel=page->findChild<QToolButton*>("cancelLoad");QVERIFY(progress && cancel);
        QTRY_VERIFY_WITH_TIMEOUT(progress->isVisible() && progress->value()>0 && page->loading,5000);
        QTest::mouseClick(cancel,Qt::LeftButton);QVERIFY(!page->loading && !page->ready && page->wasCanceled());
        QVERIFY(window.saveSession());QCOMPARE(store.read().tabs[0].position.topLine,qint64(120));
        auto* retry=page->findChild<QPushButton*>("retryLoad");QVERIFY(retry->isVisible());QTest::mouseClick(retry,Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!page->loading,15000);QVERIFY2(page->ready,qPrintable(page->content.error));QVERIFY(!page->wasCanceled());
        QCOMPARE(page->readingPosition().topLine,qint64(120));QCOMPARE(page->editor->send(SCI_GETLENGTH),sptr_t(128*1024*1024));
        page->reload();window.closeTab(0);QCOMPARE(window.tabCount(),0);QTest::qWait(100);window.close();
    }
    void windowPlacementAcrossDisplays() {
        const QRect primary(0,0,1920,1040),left(-1280,-180,1280,960),scaled(1920,0,960,516);
        const QList<QRect> areas{primary,left,scaled};
        QCOMPARE(WindowPlacement::fit(QRect(-1200,-100,1000,700),areas),QRect(-1200,-100,1000,700));
        QCOMPARE(WindowPlacement::fit(QRect(2000,60,1180,780),areas),scaled);
        QCOMPARE(WindowPlacement::fit(QRect(-1000,-100,1180,780),{primary}),QRect(0,0,1180,780));
        QCOMPARE(WindowPlacement::fit(QRect(1800,-600,1000,780),{primary}),QRect(920,0,1000,780));
        const QRect compactScreen(0,40,960,476);
        QCOMPARE(WindowPlacement::fit(QRect(0,0,1180,780),{compactScreen}),compactScreen);
        QCOMPARE(WindowPlacement::fit(QRect(60,80,800,600),{primary}),QRect(60,80,800,600));
        // Gaps between screens are not usable desktop space.
        const auto fitted=WindowPlacement::fit(QRect(1800,1200,1100,700),{primary,QRect(2500,0,1200,900)});
        QVERIFY(primary.contains(fitted) || QRect(2500,0,1200,900).contains(fitted));
    }
    void windowsRemainReachableAfterDisplayChanges() {
        MainWindow window(true);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto fits=[](QWidget& widget){return widget.screen()->availableGeometry().contains(widget.frameGeometry());};
        auto change=[](QWidget& widget){QEvent event(QEvent::DevicePixelRatioChange);QCoreApplication::sendEvent(&widget,&event);};
        const auto area=window.screen()->availableGeometry();
        window.move(area.topLeft()-QPoint(900,600));change(window);QTRY_VERIFY(fits(window));
        window.showMaximized();QTRY_VERIFY(window.isMaximized());change(window);QTest::qWait(180);QVERIFY(window.isMaximized());
        window.showNormal();QTRY_VERIFY(!window.isMaximized() && fits(window));
        AppearanceDialog appearance(EditorAppearance::defaults(),"material",&window);appearance.show();
        appearance.move(area.topRight()+QPoint(200,0));change(appearance);QTRY_VERIFY(fits(appearance));appearance.close();
        RegistrySandbox registry;IntegrationDialog integration(ShellIntegration("C:/Zex/Zex.exe",registry.prefix),&window);integration.show();
        integration.move(area.topRight()+QPoint(200,0));change(integration);QTRY_VERIFY(fits(integration));integration.close();window.close();
    }
    void shellExecutesRegisteredCommand() {
        RegistrySandbox registry;QTemporaryDir directory;
        const auto probe=directory.filePath("中文 program space.exe");
        QVERIFY(QFile::copy(QCoreApplication::applicationDirPath()+"/zex_open_probe.exe",probe));
        ShellIntegration integration(probe,registry.prefix);QString error;QVERIFY2(integration.apply(true,{".txt"},&error),qPrintable(error));
        const auto document=directory.filePath("中文 & file name.txt"),report=directory.filePath("received.txt");
        QFile file(document);QVERIFY(file.open(QIODevice::WriteOnly));file.write("test");file.close();
        const auto old=qgetenv("ZEX_TEST_OPEN_REPORT");qputenv("ZEX_TEST_OPEN_REPORT",report.toUtf8());
        HKEY key=nullptr;const auto path=registry.prefix+"\\Software\\Classes\\sixunited.Zex.Text";
        QCOMPARE(RegOpenKeyExW(HKEY_CURRENT_USER,reinterpret_cast<LPCWSTR>(path.utf16()),0,KEY_READ,&key),LSTATUS(ERROR_SUCCESS));
        SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_CLASSKEY|SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;
        const auto native=QDir::toNativeSeparators(document).toStdWString();info.hkeyClass=key;info.lpVerb=L"open";info.lpFile=native.c_str();info.nShow=SW_HIDE;
        const bool launched=ShellExecuteExW(&info);RegCloseKey(key);
        if(old.isNull())qunsetenv("ZEX_TEST_OPEN_REPORT");else qputenv("ZEX_TEST_OPEN_REPORT",old);
        QVERIFY2(launched,qPrintable(QString::number(GetLastError())));QVERIFY(info.hProcess);
        const auto wait=WaitForSingleObject(info.hProcess,5000);DWORD code=1;GetExitCodeProcess(info.hProcess,&code);CloseHandle(info.hProcess);
        QCOMPARE(wait,DWORD(WAIT_OBJECT_0));QCOMPARE(code,DWORD(0));
        QFile output(report);QVERIFY(output.open(QIODevice::ReadOnly));const auto bytes=output.readAll();
        QCOMPARE(QString::fromUtf16(reinterpret_cast<const char16_t*>(bytes.constData()),bytes.size()/2),QString::fromStdWString(native));
        QVERIFY(integration.remove(&error));
    }
    void manualEncodingsAndInvalidSequences() {
        const auto text=QStringLiteral("中文与编码\nhello Zex\n");
        for(const auto& id:QStringList{"utf-8","gb18030","gbk","utf-16le","utf-16be","utf-32le","utf-32be"}) {
            const auto codecName=id.toUpper().toLatin1();auto* codec=QTextCodec::codecForName(codecName);QVERIFY(codec);
            QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);const auto bytes=codec->fromUnicode(text.constData(),text.size(),&state);
            const auto decoded=decodeFile(bytes,id);QVERIFY2(decoded.error.isEmpty(),qPrintable(id+":"+decoded.error));
            QCOMPARE(QString::fromUtf8(decoded.utf8),text);QCOMPARE(decoded.encoding,textEncodingName(id));
        }
        const auto gbk=QTextCodec::codecForName("GBK")->fromUnicode(text);QVERIFY(!decodeFile(gbk,"utf-8").error.isEmpty());
        QVERIFY(!decodeFile(QByteArray::fromHex("fffe2d4e"),"utf-16be").error.isEmpty());
        QVERIFY(!decodeFile(QByteArray::fromHex("ff"),"utf-16le").error.isEmpty());
        QVERIFY(!decodeFile(QByteArray::fromHex("8100"),"gbk").error.isEmpty());
        QCOMPARE(QString::fromUtf8(decodeFile(text.toUtf8(),"auto").utf8),text);
        const auto old=SessionSnapshot::deserialize(R"({"version":1,"tabs":[{"path":"D:/old.txt"}]})");QCOMPARE(old.tabs[0].encoding,QString("auto"));
    }
    void encodingSwitchRaceAndSession() {
        QTemporaryDir directory;const auto path=directory.filePath("中文 no bom.txt"),sessionPath=directory.filePath("session.json");
        const auto text=QStringLiteral("中文 reading position, enough columns\n").repeated(280);
        QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);const auto bytes=QTextCodec::codecForName("UTF-16LE")->fromUnicode(text.constData(),text.size(),&state);
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),qint64(bytes.size()));file.close();
        MainWindow window(true,sessionPath);window.show();window.restoreLastSession({path});QTRY_VERIFY(window.allLoaded());QVERIFY(!window.currentPage()->ready);
        window.setCurrentEncoding("utf-16le");QTRY_VERIFY(window.allLoaded());auto* page=window.currentPage();QVERIFY2(page->ready,qPrintable(page->content.error));
        QCOMPARE(page->content.encoding,QString("UTF-16 LE"));page->editor->restoreReadingPosition({120,0,0,125,4,125,1});
        const auto position=page->readingPosition();
        window.setCurrentEncoding("utf-8");window.setCurrentEncoding("gbk");window.setCurrentEncoding("utf-16le");QTRY_VERIFY(window.allLoaded());
        QVERIFY(page->ready);QCOMPARE(page->encoding(),QString("utf-16le"));QCOMPARE(page->editor->send(SCI_GETLINECOUNT),sptr_t(281));
        QVERIFY(page->readingPosition()==position);QCOMPARE(page->editor->send(SCI_GETREADONLY),sptr_t(1));
        window.setCurrentEncoding("utf-8");QTRY_VERIFY(window.allLoaded());QVERIFY(!page->ready);
        window.setCurrentEncoding("utf-16le");QTRY_VERIFY(window.allLoaded());QVERIFY(page->ready);QVERIFY(page->readingPosition()==position);
        window.close();
        MainWindow reopened(true,sessionPath);reopened.show();reopened.restoreLastSession();QTRY_VERIFY(reopened.allLoaded());
        QCOMPARE(reopened.currentPage()->encoding(),QString("utf-16le"));QVERIFY(reopened.currentPage()->ready);QVERIFY(reopened.currentPage()->readingPosition()==position);reopened.close();
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),bytes); // Every switch remains strictly read-only.
    }
    void openMessageValidationAndBatch() {
        const wchar_t packet[]=L"D:\\中文 文件.cpp\0D:\\second.txt\0";
        COPYDATASTRUCT data{openMessageId,sizeof(packet),const_cast<wchar_t*>(packet)};QStringList paths;
        QVERIFY(FileReceiver::decode(data,paths));QCOMPARE(paths,QStringList({"D:/中文 文件.cpp","D:/second.txt"}));
        data.cbData-=2;QVERIFY(!FileReceiver::decode(data,paths));data.cbData=sizeof(packet);data.dwData=0;QVERIFY(!FileReceiver::decode(data,paths));
        const wchar_t relative[]=L"relative.txt\0";data={openMessageId,sizeof(relative),const_cast<wchar_t*>(relative)};QVERIFY(!FileReceiver::decode(data,paths));
        FileReceiver receiver;const auto name=("ZexTest-"+QUuid::createUuid().toString()).toStdWString();QVERIFY(receiver.listen(name.c_str()));
        QSignalSpy requests(&receiver,&FileReceiver::filesRequested);data={openMessageId,sizeof(packet),const_cast<wchar_t*>(packet)};
        const auto hwnd=FindWindowExW(HWND_MESSAGE,nullptr,receiverClass,name.c_str());QVERIFY(hwnd);
        QCOMPARE(SendMessageW(hwnd,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&data)),LRESULT(TRUE));QTRY_COMPARE(requests.size(),1);
        receiver.close();QVERIFY(!FindWindowExW(HWND_MESSAGE,nullptr,receiverClass,name.c_str()));
        MainWindow window(true);window.restoreLastSession();window.openPaths({"D:/first.txt","D:/second.txt","d:/FIRST.txt"});
        QCOMPARE(window.tabCount(),2);QCOMPARE(window.currentPage()->path,QString("D:/first.txt"));
        QCOMPARE(window.findChildren<CodeEditor*>().size(),1);
    }
    void shellRegistrationRoundTrip() {
        RegistrySandbox registry;QTemporaryDir directory;const auto executable=directory.filePath("中文 Zex.exe");
        QFile file(executable);QVERIFY(file.open(QIODevice::WriteOnly));file.close();
        registry.settings.setValue("Software/Classes/.txt/Default","Other.Text");
        registry.settings.setValue("Software/Classes/.txt/OpenWithProgids/Other.Text",QString());
        registry.settings.setValue("Software/RegisteredApplications/OtherApp","Software/OtherApp/Capabilities");
        registry.settings.setValue("Software/Microsoft/Windows/CurrentVersion/Explorer/FileExts/.txt/UserChoice/ProgId","Other.Text");registry.settings.sync();
        ShellIntegration integration(executable,registry.prefix);QString error;
        QVERIFY2(integration.apply(true,{".txt",".cpp",".ps1"},&error),qPrintable(error));
        QCOMPARE(integration.state().extensions,QStringList({".cpp",".ps1",".txt"}));QVERIFY(integration.state().contextMenu);
        QCOMPARE(registry.value("Software/Classes/.txt/Default"),QString("Other.Text"));
        QCOMPARE(registry.value("Software/Classes/*/shell/sixunited.Zex/command/Default"),'"'+QDir::toNativeSeparators(executable)+"\" -- \"%1\"");
        QVERIFY2(integration.apply(true,{".cpp"},&error),qPrintable(error));
        registry.settings.sync();QVERIFY(!registry.settings.contains("Software/Classes/.txt/OpenWithProgids/sixunited.Zex.Text"));
        QVERIFY(registry.settings.contains("Software/Classes/.txt/OpenWithProgids/Other.Text"));
        const auto moved=directory.filePath("moved Zex.exe");QVERIFY(QFile::copy(executable,moved));
        ShellIntegration relocated(moved,registry.prefix);QVERIFY2(relocated.apply(true,{".cpp"},&error),qPrintable(error));
        QCOMPARE(relocated.state().executable,QDir::toNativeSeparators(moved));
        QVERIFY2(relocated.remove(&error),qPrintable(error));QVERIFY(!relocated.state().contextMenu);QVERIFY(relocated.state().extensions.isEmpty());
        registry.settings.sync();QVERIFY(!registry.settings.contains("Software/RegisteredApplications/Zex"));
        QVERIFY(!registry.settings.contains("Software/Classes/sixunited.Zex.Text/ZexOwner"));
        QCOMPARE(registry.value("Software/Classes/.txt/Default"),QString("Other.Text"));
        QCOMPARE(registry.value("Software/Microsoft/Windows/CurrentVersion/Explorer/FileExts/.txt/UserChoice/ProgId"),QString("Other.Text"));
        QCOMPARE(registry.value("Software/RegisteredApplications/OtherApp"),QString("Software/OtherApp/Capabilities"));
        QVERIFY(relocated.remove(&error)); // Idempotent cleanup.
        QVERIFY(!relocated.apply(true,{".exe"},&error));QVERIFY(!relocated.state().contextMenu);
    }
    void shellRegistrationConflictAndDialog() {
        RegistrySandbox registry;QTemporaryDir directory;const auto executable=directory.filePath("Zex.exe");
        QFile file(executable);QVERIFY(file.open(QIODevice::WriteOnly));file.close();ShellIntegration integration(executable,registry.prefix);QString error;
        registry.settings.setValue("Software/RegisteredApplications/Zex","Software/Another/Capabilities");registry.settings.sync();
        QVERIFY(!integration.apply(true,{".txt"},&error));QVERIFY(!error.isEmpty());QVERIFY(!integration.state().contextMenu);
        QCOMPARE(registry.value("Software/RegisteredApplications/Zex"),QString("Software/Another/Capabilities"));
        registry.settings.remove("Software/RegisteredApplications/Zex");registry.settings.sync();
        IntegrationDialog dialog(integration);dialog.show();
        dialog.findChild<QCheckBox*>("contextMenuEnabled")->setChecked(true);dialog.findChild<QCheckBox*>("extension.txt")->setChecked(true);
        QTest::mouseClick(dialog.findChild<QPushButton*>("integrationSave"),Qt::LeftButton);
        QVERIFY(integration.state().contextMenu);QCOMPARE(integration.state().extensions,QStringList({".txt"}));
        QTest::mouseClick(dialog.findChild<QPushButton*>("integrationRemove"),Qt::LeftButton);
        QVERIFY(!integration.state().contextMenu);QVERIFY(integration.state().extensions.isEmpty());dialog.reject();
    }
    void encoding_data() {
        QTest::addColumn<QByteArray>("bytes");QTest::addColumn<QString>("encoding");
        auto sample=QStringLiteral("中文代码\r\nconst answer = 42;\n");
        QTest::newRow("utf8")<<sample.toUtf8()<<QString("UTF-8");
        QTest::newRow("utf8-bom")<<(QByteArray::fromHex("efbbbf")+sample.toUtf8())<<QString("UTF-8 BOM");
        QTest::newRow("utf16-le")<<QTextCodec::codecForName("UTF-16LE")->fromUnicode(sample)<<QString("UTF-16 LE");
        QTest::newRow("utf16-be")<<QTextCodec::codecForName("UTF-16BE")->fromUnicode(sample)<<QString("UTF-16 BE");
        QTest::newRow("gbk")<<QTextCodec::codecForName("GB18030")->fromUnicode(sample)<<QString("GB18030 / GBK");
    }
    void encoding() {
        QFETCH(QByteArray,bytes);QFETCH(QString,encoding);
        const auto content=decodeFile(bytes);QVERIFY2(content.error.isEmpty(),qPrintable(content.error));
        QCOMPARE(content.encoding,encoding);QCOMPARE(QString::fromUtf8(content.utf8),QStringLiteral("中文代码\r\nconst answer = 42;\n"));QCOMPARE(content.lineEnding,QStringLiteral("混合换行"));
    }
    void rejectsBinaryAndMissing() {
        QVERIFY(!decodeFile(QByteArray("MZ\0binary",9)).error.isEmpty());
        QVERIFY(!readFile("Z:/definitely-missing-zex-file").error.isEmpty());
        QVERIFY(decodeFile(QByteArray()).error.isEmpty());
        QVERIFY(decodeFile(QByteArray(21000,'x')).large);
        QVERIFY(!decodeFile(QByteArray::fromHex("efbbbfe4b8")).error.isEmpty());
        QVERIFY(!decodeFile(QByteArray::fromHex("fffe4100ff")).error.isEmpty());
    }
    void readonlyAndHighlight() {
        CodeEditor editor;
        editor.setContent(decodeFile("// comment\nconst int answer = 42;\n"),"sample.cpp");
        QCOMPARE(editor.send(SCI_GETREADONLY),sptr_t(1));
        QCOMPARE(editor.send(SCI_GETSTYLEAT,0),sptr_t(SCE_C_COMMENTLINE));
        QCOMPARE(editor.send(SCI_GETSTYLEAT,11),sptr_t(SCE_C_WORD));
        QCOMPARE(editor.language(),QString("C++"));
        const auto before=editor.send(SCI_GETLENGTH);editor.show();editor.setFocus();QTest::keyClicks(&editor,"oops");QCOMPARE(editor.send(SCI_GETLENGTH),before);
    }
    void unicodeSearchAndWrapping() {
        CodeEditor editor;
        const auto text=QStringLiteral("中文 alpha ALPHA alphabet\n中文 alpha\n");
        editor.setContent(decodeFile(text.toUtf8()),"sample.txt");
        editor.setSearch(QStringLiteral("中文"),true,false);QTRY_COMPARE(editor.matchCount(),2);
        QCOMPARE(editor.selectedText(),QStringLiteral("中文"));
        QVERIFY(editor.findNext());QCOMPARE(editor.send(SCI_GETSELECTIONSTART),sptr_t(QStringLiteral("中文 alpha ALPHA alphabet\n").toUtf8().size()));
        QVERIFY(editor.findNext());QCOMPARE(editor.send(SCI_GETSELECTIONSTART),sptr_t(0));
        QVERIFY(editor.findNext(true));QVERIFY(editor.send(SCI_GETSELECTIONSTART)>0);
        editor.setSearch("alpha",false,true);QTRY_COMPARE(editor.matchCount(),3);
        editor.setSearch("alpha",true,true);QTRY_COMPARE(editor.matchCount(),2);
        editor.setSearch("alpha",false,false);QTRY_COMPARE(editor.matchCount(),4);
        editor.setSearch("no-such-word",true,false);QTRY_COMPARE(editor.matchSummary(),QStringLiteral("无结果"));QVERIFY(!editor.findNext());
        editor.clearSearch();QCOMPARE(editor.matchCount(),0);
    }
    void chunkedSearchBoundariesAndNavigation() {
        CodeEditor editor;const sptr_t chunk=512*1024;
        QByteArray bytes=QByteArray(63,'x').append('\n').repeated(6*chunk/64);
        const auto needle=QStringLiteral("中文AbC").toUtf8();
        const QList<sptr_t> positions{chunk-2,2*chunk+7,5*chunk-3};
        for(auto at:positions)bytes.replace(at-1,needle.size()+2," "+needle+" ");
        editor.setContent(decodeFile(bytes),"boundaries.txt");
        editor.setSearch(QStringLiteral("中文abc"),false,true);
        QTRY_VERIFY_WITH_TIMEOUT(!editor.searchBusy(),5000);QCOMPARE(editor.matchCount(),3);
        QCOMPARE(editor.send(SCI_GETSELECTIONSTART),positions[0]);
        QVERIFY(editor.findNext());QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.send(SCI_GETSELECTIONSTART),positions[1]);
        QVERIFY(editor.findNext());QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.send(SCI_GETSELECTIONSTART),positions[2]);
        QVERIFY(editor.findNext());QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.send(SCI_GETSELECTIONSTART),positions[0]);
        QVERIFY(editor.findNext(true));QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.send(SCI_GETSELECTIONSTART),positions[2]);
        QVERIFY(editor.findNext(true));QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.send(SCI_GETSELECTIONSTART),positions[1]);
        QVERIFY(editor.findNext(true));QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.send(SCI_GETSELECTIONSTART),positions[0]);
        // Whole-word rejection at a boundary, then a case-folded match crossing it.
        bytes.replace(positions[1]-1,1,"x");editor.setContent(decodeFile(bytes),"boundaries.txt");
        editor.setSearch(QStringLiteral("中文abc"),false,true);QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.matchCount(),2);
        bytes=QByteArray(63,'x').append('\n').repeated(chunk/64+2);
        bytes.replace(chunk-2,7,QStringLiteral(" Straße ").toUtf8());
        editor.setContent(decodeFile(bytes),"casefold.txt");editor.setSearch("STRASSE",false,true);
        QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.matchCount(),1);QCOMPARE(editor.selectedText(),QStringLiteral("Straße"));
    }
    void searchCancellationAndCappedNavigation() {
        CodeEditor editor;auto bytes=QByteArray("alpha\n").repeated(10050);
        editor.setContent(decodeFile(bytes),"capped.txt");editor.setSearch("alpha",true,true);
        QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.matchCount(),10000);
        editor.send(SCI_SETSEL,10020*6,10020*6+5);
        QVERIFY(editor.findNext());QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.send(SCI_GETSELECTIONSTART),sptr_t(10021*6));
        QVERIFY(editor.findNext(true));QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.send(SCI_GETSELECTIONSTART),sptr_t(10020*6));
        bytes=QByteArray("alpha xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n").repeated(200000);
        editor.setContent(decodeFile(bytes),"cancel.txt");
        bool heartbeat=false;QTimer::singleShot(0,&editor,[&]{heartbeat=true;});
        editor.setSearch("no such word",false,false);QVERIFY(editor.searchBusy());QTRY_VERIFY(heartbeat);
        editor.setSearch("alpha",true,true);QTRY_VERIFY(!editor.searchBusy());QCOMPARE(editor.matchCount(),10000);
        editor.setSearch("no such word",false,false);editor.clearSearch();QTest::qWait(30);
        QVERIFY(!editor.searchBusy());QCOMPARE(editor.matchCount(),0);
        editor.setSearch("missing again",false,false);editor.setContent(decodeFile("replacement\n"),"replacement.txt");
        QTest::qWait(30);QVERIFY(!editor.searchBusy());QCOMPARE(editor.matchCount(),0);
    }
    void tabsAndBackgroundLoading() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto first=dir.filePath(QStringLiteral("中文 文件.cpp"));const auto second=dir.filePath("config.json");
        QFile a(first);QVERIFY(a.open(QIODevice::WriteOnly));a.write("int value = 42;\n");a.close();
        QFile b(second);QVERIFY(b.open(QIODevice::WriteOnly));b.write("{\"value\": 42}\n");b.close();
        MainWindow window(true);window.show();
        auto* page=window.openPath(first);QTRY_VERIFY(page->ready);
        QCOMPARE(window.tabCount(),1);QCOMPARE(window.openPath(first),page);QCOMPARE(window.tabCount(),1);
        auto* other=window.openPath(second);QTRY_VERIFY(other->ready);QCOMPARE(window.tabCount(),2);QCOMPARE(window.currentPage(),other);
        window.showSearch();auto* field=window.findChild<QLineEdit*>("searchInput");QVERIFY(field);field->setText("value");QTRY_COMPARE(other->editor->matchCount(),1);
        window.openPath(first);QTRY_COMPARE(page->editor->matchCount(),1);
        window.closeTab(0);QCOMPARE(window.tabCount(),1);QCOMPARE(window.currentPage(),other);
        window.hideSearch();QCOMPARE(other->editor->matchCount(),0);
        window.closeTab(0);QCOMPARE(window.tabCount(),0);QVERIFY(!window.currentPage());
        // Closing while I/O is still pending must not deliver to a destroyed widget.
        window.openPath(first);window.closeTab(0);QTest::qWait(100);QCOMPARE(window.tabCount(),0);
    }
    void iconThemesAndFilenameMatching() {
        for(const auto& theme:FileIcons::themes()) {
            const auto cpp=FileIcons::iconId("D:/中文 路径/MAIN.CPP",theme);
            QCOMPARE(cpp,FileIcons::iconId("main.cpp",theme));
            QVERIFY(cpp!=FileIcons::iconId("main.py",theme));
            QVERIFY(FileIcons::iconId("CMakeLists.txt",theme)!=FileIcons::iconId("notes.txt",theme));
            for(const auto& file:{"main.cpp","main.py","index.js","config.json","script.ps1","README.md","CMakeLists.txt","unknown.zzz"}) {
                const auto pix=FileIcons::icon(file,theme).pixmap(36,36);
                QVERIFY(!pix.isNull());const auto image=pix.toImage();int visible=0;
                for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)if(qAlpha(image.pixel(x,y))>20)++visible;
                QVERIFY2(visible>20,qPrintable(theme+":"+file));
            }
        }
        QVERIFY(FileIcons::iconId("types.d.ts","material")!=FileIcons::iconId("types.ts","material"));
        QVERIFY(FileIcons::iconId("package.json","material")!=FileIcons::iconId("data.json","material"));
        QVERIFY(FileIcons::icon("main.py","seti").pixmap(36,36).toImage()!=FileIcons::icon("main.py","seti-mono").pixmap(36,36).toImage());
        FileIcons::setTheme("bad-theme");QCOMPARE(FileIcons::theme(),QString("material"));
    }
    void typographyPersistsWithoutReloading() {
        CodeEditor editor;editor.setContent(decodeFile("// comment\nconst int answer = 42;\n"),"main.cpp");
        editor.setSearch("answer",true,false);QTRY_COMPARE(editor.matchCount(),1);
        const auto selection=editor.send(SCI_GETSELECTIONSTART);
        const auto color=editor.send(SCI_STYLEGETFORE,SCE_C_WORD);
        auto appearance=EditorAppearance::defaults();appearance.family="Consolas";appearance.points=17;
        appearance.bold=true;appearance.commentItalics=false;appearance.linePadding=9;
        editor.setAppearance(appearance);
        QCOMPARE(editor.send(SCI_STYLEGETSIZE,SCE_C_WORD),sptr_t(17));
        QCOMPARE(editor.send(SCI_STYLEGETBOLD,SCE_C_WORD),sptr_t(1));
        QCOMPARE(editor.send(SCI_STYLEGETITALIC,SCE_C_COMMENTLINE),sptr_t(0));
        QCOMPARE(editor.send(SCI_GETEXTRAASCENT)+editor.send(SCI_GETEXTRADESCENT),sptr_t(9));
        QCOMPARE(editor.send(SCI_STYLEGETFORE,SCE_C_WORD),color);
        QCOMPARE(editor.send(SCI_GETSELECTIONSTART),selection);QCOMPARE(editor.matchCount(),1);
        QCOMPARE(editor.send(SCI_GETREADONLY),sptr_t(1));
        appearance.italic=true;editor.setAppearance(appearance);
        QCOMPARE(editor.send(SCI_STYLEGETITALIC,SCE_C_WORD),sptr_t(1));
        editor.setContent(decodeFile("# comment\nreturn 42\n"),"main.py");
        QCOMPARE(editor.send(SCI_STYLEGETSIZE,SCE_P_WORD),sptr_t(17));
        QCOMPARE(editor.send(SCI_STYLEGETBOLD,SCE_P_WORD),sptr_t(1));
        QCOMPARE(editor.send(SCI_STYLEGETITALIC,SCE_P_WORD),sptr_t(1));
        QTemporaryDir directory;
        {QSettings settings(directory.filePath("preferences.ini"),QSettings::IniFormat);appearance.save(settings);settings.sync();}
        QSettings reopened(directory.filePath("preferences.ini"),QSettings::IniFormat);
        QVERIFY(EditorAppearance::fromSettings(reopened)==appearance);
        reopened.setValue("editor/fontSize",999);reopened.setValue("editor/fontFamily","Missing Zex Test Font");
        QCOMPARE(EditorAppearance::fromSettings(reopened).points,32);
        QCOMPARE(EditorAppearance::fromSettings(reopened).family,Theme::codeFont());
    }
    void appearanceDialogAndWindowChrome() {
        QTemporaryDir directory;
        QFile file(directory.filePath("main.cpp"));QVERIFY(file.open(QIODevice::WriteOnly));file.write("int answer = 42;");file.close();
        MainWindow window(true);window.show();
        auto* page=window.openPath(file.fileName());QTRY_VERIFY(page->ready);
        AppearanceDialog dialog(page->editor->appearance(),FileIcons::theme(),&window);dialog.show();
        connect(&dialog,&AppearanceDialog::applyRequested,&window,[&]{window.applyAppearance(dialog.editorAppearance(),dialog.iconTheme());});
        auto* size=dialog.findChild<QSpinBox*>("fontSize");auto* icons=dialog.findChild<QComboBox*>("iconTheme");
        QVERIFY(size);QVERIFY(icons);size->setValue(18);icons->setCurrentIndex(icons->findData("seti-mono"));
        QCOMPARE(page->editor->pointSize(),12); // Preview is isolated until Apply / OK.
        QTest::mouseClick(dialog.findChild<QPushButton*>("appearanceApply"),Qt::LeftButton);
        QCOMPARE(page->editor->pointSize(),18);QCOMPARE(FileIcons::theme(),QString("seti-mono"));
        size->setValue(22);QTest::mouseClick(dialog.findChild<QPushButton*>("appearanceCancel"),Qt::LeftButton);
        QCOMPARE(page->editor->pointSize(),18);
        auto* second=window.openPath(directory.filePath("another.txt"));QTRY_VERIFY(!second->loading);
        QCOMPARE(second->editor->pointSize(),18);
        QCOMPARE(window.centralWidget()->layout()->contentsMargins(),QMargins(0,0,0,0));
        DWORD border=0,corner=0;const auto hwnd=reinterpret_cast<HWND>(window.winId());
        if(SUCCEEDED(DwmGetWindowAttribute(hwnd,34,&border,sizeof(border))))QCOMPARE(border,DWORD(0xFFFFFFFE));
        window.showMaximized();QTRY_VERIFY(window.isMaximized());
        if(SUCCEEDED(DwmGetWindowAttribute(hwnd,33,&corner,sizeof(corner))))QCOMPARE(corner,DWORD(1));
        window.showNormal();QTRY_VERIFY(!window.isMaximized());
        if(SUCCEEDED(DwmGetWindowAttribute(hwnd,33,&corner,sizeof(corner))))QCOMPARE(corner,DWORD(2));
    }
    void sessionStorageAndInvalidData() {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        const auto path=directory.filePath(QStringLiteral("子目录/会话.json"));
        SessionStore store(path);
        SessionSnapshot source{{{directory.filePath(QStringLiteral("中文 文件.cpp")),{83,2,125,91,12,90,4}},
            {directory.filePath("data.json"),{17,0,0,19,5,19,5}}},directory.filePath("data.json")};
        QString error;QVERIFY2(store.write(source,&error),qPrintable(error));
        const auto saved=store.read();QCOMPARE(saved.tabs.size(),2);QCOMPARE(saved.activePath,source.activePath);
        QCOMPARE(saved.tabs[0].path,source.tabs[0].path);QVERIFY(saved.tabs[0].position==source.tabs[0].position);
        QVERIFY(SessionSnapshot::deserialize("broken json").tabs.isEmpty());
        QVERIFY(SessionSnapshot::deserialize("{\"version\":42,\"tabs\":[]}").tabs.isEmpty());
        auto json=QJsonDocument::fromJson(source.serialize()).object();auto tabs=json["tabs"].toArray();
        tabs.append(QJsonObject{{"path",source.tabs[0].path.toUpper()}});
        tabs.append(QJsonObject{{"path","relative.txt"}});
        tabs.append(QJsonObject{{"path",directory.filePath("invalid.txt")},{"position",QJsonObject{{"topLine",-50},{"xOffset",-1}}}});
        json["tabs"]=tabs;const auto validated=SessionSnapshot::deserialize(QJsonDocument(json).toJson());
        QCOMPARE(validated.tabs.size(),3);QCOMPARE(validated.tabs.last().position.topLine,qint64(0));QCOMPARE(validated.tabs.last().position.xOffset,0);
        // A failed save must leave the previously committed snapshot intact.
        SessionStore invalidStore(path+"/not-a-directory.json");QVERIFY(!invalidStore.write(source,&error));
        QCOMPARE(store.read().serialize(),saved.serialize());
        QVERIFY(store.write({}));QVERIFY(store.read().tabs.isEmpty());
    }
    void readingPositionWrapUnicodeAndTruncation() {
        CodeEditor editor;editor.resize(480,360);editor.show();
        const auto line=QStringLiteral("\t中文 alpha beta gamma ").repeated(14).toUtf8()+"\n";
        editor.setContent(decodeFile(line.repeated(150)),"notes.txt");
        ReadingPosition saved{71,0,125,78,13,75,6};
        editor.restoreReadingPosition(saved);
        QTRY_VERIFY(editor.readingPosition()==saved);
        editor.setWordWrap(true);QTest::qWait(30);
        saved={51,2,0,57,13,54,6};editor.restoreReadingPosition(saved);
        QTRY_VERIFY(editor.readingPosition()==saved);
        editor.setContent(decodeFile(QStringLiteral("中文\nx").toUtf8()),"notes.txt");editor.restoreReadingPosition(saved);
        const auto shortened=editor.readingPosition();
        QVERIFY(shortened.topLine<=1);QCOMPARE(shortened.caretLine,qint64(1));QCOMPARE(shortened.caretColumn,qint64(1));
        QCOMPARE(shortened.anchorLine,qint64(1));QCOMPARE(shortened.anchorColumn,qint64(1));
    }
    void lazySessionOrderAndRepeatedRestart() {
        QTemporaryDir directory;SessionSnapshot seed;
        for(int i=0;i<5;++i) {
            const auto path=directory.filePath(QStringLiteral("阅读 %1.cpp").arg(i));
            QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QByteArray("// hello Zex reading position test\n").repeated(250));file.close();
            seed.tabs.append({path,{20+i*10,0,0,25+i*10,8,25+i*10,4}});
        }
        seed.activePath=seed.tabs[2].path;const auto sessionPath=directory.filePath("session.json");
        SessionStore store(sessionPath);QVERIFY(store.write(seed));
        MainWindow window(true,sessionPath);window.show();window.restoreLastSession();
        QCOMPARE(window.tabCount(),5);QCOMPARE(window.currentPage()->path,seed.activePath);
        QTRY_VERIFY(window.allLoaded());QVERIFY(window.currentPage()->ready);
        QCOMPARE(window.findChildren<CodeEditor*>().size(),1);
        QTRY_VERIFY(window.currentPage()->readingPosition()==seed.tabs[2].position);
        for(int i=0;i<5;++i)QCOMPARE(window.sessionSnapshot().tabs[i].path,seed.tabs[i].path);
        auto* bar=window.findChild<FileTabs*>();QVERIFY(bar);bar->moveTab(0,4);
        const auto moved=window.sessionSnapshot();QCOMPARE(moved.tabs[4].path,seed.tabs[0].path);
        QCOMPARE(moved.activePath,seed.activePath);
        // Applying preferences must not instantiate editors for background placeholders.
        auto appearance=EditorAppearance::defaults();appearance.points=15;window.applyAppearance(appearance,"seti");
        QCOMPARE(window.findChildren<CodeEditor*>().size(),1);
        QVERIFY(window.saveSession());window.close();
        MainWindow reopened(true,sessionPath);reopened.show();reopened.restoreLastSession();
        QTRY_VERIFY(reopened.allLoaded());QCOMPARE(reopened.findChildren<CodeEditor*>().size(),1);
        QCOMPARE(reopened.sessionSnapshot().serialize(),moved.serialize());
        auto* background=reopened.openPath(seed.tabs[0].path);QTRY_VERIFY(background->ready && !background->loading);
        QTRY_VERIFY(background->readingPosition()==seed.tabs[0].position);
        QCOMPARE(reopened.findChildren<CodeEditor*>().size(),2);
        QCOMPARE(reopened.tabCount(),5); // Opening an existing restored tab must not duplicate it.
        reopened.close();
        MainWindow again(true,sessionPath);again.show();again.restoreLastSession();QTRY_VERIFY(again.allLoaded());
        QCOMPARE(again.currentPage()->path,seed.tabs[0].path);QTRY_VERIFY(again.currentPage()->readingPosition()==seed.tabs[0].position);
        // Closing a dormant tab drops it from the next session; closing all stays empty.
        int unnecessaryLoads=0;
        for(auto* page:again.findChildren<FilePage*>())connect(page,&FilePage::editorCreated,&again,[&]{++unnecessaryLoads;});
        again.closeAllTabs();QCOMPARE(unnecessaryLoads,0);again.close();QVERIFY(store.read().tabs.isEmpty());
        MainWindow empty(true,sessionPath);empty.show();empty.restoreLastSession();QCOMPARE(empty.tabCount(),0);empty.close();
    }
    void sessionMissingFilesAndCommandLinePriority() {
        QTemporaryDir directory;const auto missing=directory.filePath("missing.txt");const auto present=directory.filePath("active.txt");
        QFile file(present);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QByteArray("line of code\n").repeated(100));file.close();
        SessionSnapshot seed{{{missing,{30,0,0,35,5,35,5}},{present,{10,0,0,15,3,15,3}}},missing};
        const auto sessionPath=directory.filePath("session.json");SessionStore store(sessionPath);QVERIFY(store.write(seed));
        MainWindow window(true,sessionPath);window.show();window.restoreLastSession({present});
        QTRY_VERIFY(window.allLoaded());QVERIFY(window.currentPage()->ready);QCOMPARE(window.currentPage()->path,present);
        QCOMPARE(window.tabCount(),2);QCOMPARE(window.findChildren<CodeEditor*>().size(),1);
        window.restoreLastSession({present});QCOMPARE(window.tabCount(),2);
        auto* unavailable=window.openPath(missing);QTRY_VERIFY(!unavailable->loading);
        QVERIFY(!unavailable->ready);QVERIFY(!unavailable->content.error.isEmpty());
        QVERIFY(unavailable->readingPosition()==seed.tabs[0].position);
        QFile returned(missing);QVERIFY(returned.open(QIODevice::WriteOnly));returned.write(QByteArray("returned file\n").repeated(100));returned.close();
        unavailable->reload();QTRY_VERIFY(unavailable->ready && !unavailable->loading);
        QTRY_VERIFY(unavailable->readingPosition()==seed.tabs[0].position);
        window.close();
    }
    void scrollingAndTabOrderAutosave() {
        QTemporaryDir directory;const auto path=directory.filePath("scroll.txt");
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QByteArray("reading position\n").repeated(250));file.close();
        const auto sessionPath=directory.filePath("session.json");SessionStore store(sessionPath);
        MainWindow window(true,sessionPath);window.show();window.restoreLastSession({path});
        QTRY_VERIFY(window.allLoaded());auto* editor=window.currentPage()->editor;
        editor->restoreReadingPosition({130,0,0,134,5,134,5});
        QTRY_VERIFY_WITH_TIMEOUT(!store.read().tabs.isEmpty() && store.read().tabs[0].position.topLine==130,3000);
        // Snapshotting before an asynchronous load completes must retain the saved viewport.
        window.currentPage()->reload();QVERIFY(window.saveSession());QCOMPARE(store.read().tabs[0].position.topLine,qint64(130));
        QTRY_VERIFY(window.allLoaded());window.close();
    }
    void themesPreserveDocumentAndLazyTabs() {
        Theme::apply(*qApp);QTemporaryDir directory;
        const auto path=directory.filePath("theme.cpp"),sessionPath=directory.filePath("session.json");
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QByteArray("// comment\nint sample() {\n    return 42;\n}\n").repeated(100));file.close();
        SessionStore store(sessionPath);QVERIFY(store.write({{{path,{}},{directory.filePath("later.json"),{}}},path}));
        MainWindow window(true,sessionPath);window.show();window.restoreLastSession();QTRY_VERIFY(window.allLoaded());
        auto* editor=window.currentPage()->editor;QVERIFY(editor);
        editor->setSearch("sample",true,true);QTRY_COMPARE(editor->matchCount(),100);
        editor->send(SCI_FOLDLINE,1,SC_FOLDACTION_CONTRACT);QCOMPARE(editor->send(SCI_GETFOLDEXPANDED,1),sptr_t(0));
        editor->restoreReadingPosition({120,0,0,125,4,125,0});QTest::qWait(20);
        const auto position=editor->readingPosition();const auto length=editor->send(SCI_GETLENGTH);
        for(const auto& id:Theme::themes()) {
            const auto& c=Theme::colors(id);
            window.applyAppearance(editor->appearance(),"seti-mono",id);QCoreApplication::processEvents();
            QCOMPARE(Theme::id(),id);QCOMPARE(editor->colorTheme(),id);
            QCOMPARE(qApp->palette().color(QPalette::Window),c.background);
            auto rgb=[](const QColor& value){return value.red()|(value.green()<<8)|(value.blue()<<16);};
            QCOMPARE(editor->send(SCI_STYLEGETBACK,STYLE_DEFAULT),sptr_t(rgb(c.background)));
            QCOMPARE(editor->send(SCI_STYLEGETFORE,SCE_C_WORD),sptr_t(rgb(c.keyword)));
            QCOMPARE(editor->send(SCI_STYLEGETFORE,SCE_C_COMMENTLINE),sptr_t(rgb(c.comment)));
            QCOMPARE(editor->send(SCI_GETSTYLEAT,11),sptr_t(SCE_C_WORD));
            QCOMPARE(editor->send(SCI_GETLENGTH),length);QCOMPARE(editor->matchCount(),100);
            QCOMPARE(editor->send(SCI_GETFOLDEXPANDED,1),sptr_t(0));QVERIFY(editor->readingPosition()==position);
            QCOMPARE(window.findChildren<CodeEditor*>().size(),1);QCOMPARE(editor->send(SCI_GETREADONLY),sptr_t(1));
            BOOL dark=FALSE;const auto hwnd=reinterpret_cast<HWND>(window.winId());
            if(SUCCEEDED(DwmGetWindowAttribute(hwnd,20,&dark,sizeof(dark)))) {
                QTRY_VERIFY((DwmGetWindowAttribute(hwnd,20,&dark,sizeof(dark)),bool(dark)==c.dark));
            }
            DWORD border=0;
            if(SUCCEEDED(DwmGetWindowAttribute(hwnd,34,&border,sizeof(border))))QCOMPARE(border,DWORD(0xFFFFFFFE));
        }
        auto* next=window.openPath(directory.filePath("later.json"));QTRY_VERIFY(!next->loading);
        QCOMPARE(next->editor->colorTheme(),QString("sand"));
        window.close();Theme::apply(*qApp);
    }
    void themePreviewApplyCancelAndPreference() {
        Theme::apply(*qApp);MainWindow window(true);window.show();
        AppearanceDialog dialog(EditorAppearance::defaults(),"material",&window);dialog.show();
        connect(&dialog,&AppearanceDialog::applyRequested,&window,[&]{window.applyAppearance(dialog.editorAppearance(),dialog.iconTheme(),dialog.colorTheme());});
        auto* themes=dialog.findChild<QComboBox*>("colorTheme");auto* preview=dialog.findChild<CodeEditor*>("fontPreview");
        QVERIFY(themes);QVERIFY(preview);QCOMPARE(themes->count(),6);
        themes->setCurrentIndex(themes->findData("paper"));QCOMPARE(preview->colorTheme(),QString("paper"));
        QCOMPARE(Theme::id(),QString("graphite")); // Dialog previews do not alter the main window.
        QCOMPARE(dialog.palette().color(QPalette::Window),Theme::colors("paper").background);
        QTest::mouseClick(dialog.findChild<QPushButton*>("appearanceApply"),Qt::LeftButton);QCOMPARE(Theme::id(),QString("paper"));
        themes->setCurrentIndex(themes->findData("violet"));QCOMPARE(preview->colorTheme(),QString("violet"));
        QTest::mouseClick(dialog.findChild<QPushButton*>("appearanceCancel"),Qt::LeftButton);QCOMPARE(Theme::id(),QString("paper"));
        QTemporaryDir directory;const auto path=directory.filePath("preferences.ini");
        {QSettings settings(path,QSettings::IniFormat);Theme::saveTheme(settings,Theme::id());settings.sync();}
        QSettings settings(path,QSettings::IniFormat);QCOMPARE(Theme::storedTheme(settings),QString("paper"));
        settings.setValue("colorTheme","missing-theme");QCOMPARE(Theme::storedTheme(settings),QString("graphite"));
        Theme::apply(*qApp);
    }
    void fileIconsAdaptToLightThemes() {
        QVERIFY(FileIcons::iconId("config.toml","material","graphite")!=FileIcons::iconId("config.toml","material","paper"));
        QVERIFY(FileIcons::iconId("main.py","seti","graphite")!=FileIcons::iconId("main.py","seti","paper"));
        for(const auto& icons:FileIcons::themes())for(const auto& theme:Theme::themes()) {
            for(const auto& path:{"main.py","config.toml","notes.unknown"}) {
                const auto image=FileIcons::icon(path,icons,theme).pixmap(36,36).toImage();QVERIFY(!image.isNull());
                int visible=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)if(qAlpha(image.pixel(x,y))>20)++visible;
                QVERIFY2(visible>20,qPrintable(icons+":"+theme+":"+path));
            }
        }
        QVERIFY(FileIcons::icon("main.py","seti-mono","paper").pixmap(36,36).toImage()!=FileIcons::icon("main.py","seti-mono","graphite").pixmap(36,36).toImage());
    }
};
int main(int argc,char** argv){QApplication app(argc,argv);Theme::apply(app);ZexTests tests;return QTest::qExec(&tests,argc,argv);}
#include "test_zex.moc"
