#pragma once
#include "ScintillaEditBase.h"
#include "fileloader.h"
#include "documentloader.h"
#include "appearance.h"
#include "session.h"
#include "regexsearch.h"
#include <QSet>
#include <QTimer>
#include <QVector>
#include <optional>

class CodeEditor : public ScintillaEditBase {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr);
    ~CodeEditor() override;
    void setContent(const FileContent& content, const QString& path);
    void setPreparedContent(const PreparedFile& file,const QString& path);
    bool appendLogText(const QByteArray& utf8,const QString& path,bool large);
    bool atDocumentEnd() const;
    void scrollToEnd();
    QVector<qint64> bookmarks() const;
    void setBookmarks(const QVector<qint64>& lines);
    void toggleBookmark(qint64 line = -1);
    bool nextBookmark(bool backwards = false);
    QString language() const { return language_; }
    void setWordWrap(bool on);
    void setPointSize(int points);
    int pointSize() const { return appearance_.points; }
    void setAppearance(const EditorAppearance& appearance);
    EditorAppearance appearance() const { return appearance_; }
    void setSearch(const QString& query, bool caseSensitive, bool wholeWord, bool regex = false, std::optional<SearchMatch> scope = {}, bool navigate = true);
    bool findNext(bool backwards = false);
    void clearSearch();
    void cancelSearch();
    bool searchStopped() const { return stopped_; }
    QString searchError() const { return searchError_; }
    quint64 searchGeneration() const { return searchGeneration_; }
    QString resultText(int index) const;
    bool selectResult(int index);
    int matchCount() const { return matches_.size(); }
    bool searchBusy() const { return counting_ || navigating_ || regexBusy_; }
    QString matchSummary() const;
    QString selectedText() const;
    QString positionSummary() const;
    void goToLine(int line);
    ReadingPosition readingPosition() const;
    void restoreReadingPosition(const ReadingPosition& position);
    void setColorTheme(const QString& id);
    QString colorTheme() const { return colorTheme_; }
signals:
    void searchChanged();
    void positionChanged();
    void readingPositionChanged();
    void verticalPositionChanged();
    void fileDropped(const QString& path);
    void bookmarksChanged();
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
private:
    void copyRegexSnapshot(quint64 generation, qint64 offset);
    void runRegex(RegexMode mode, bool includeOrigin = true);
    bool navigateToMatch(const SearchMatch& match);
    bool regex_ = false,regexBusy_ = false,stopped_ = false,regexAutoNavigate_ = true;
    QString searchError_;
    qint64 searchStart_ = 0,searchEnd_ = 0;
    bool searchInSelection_ = false;
    QByteArray regexSnapshot_;
    RegexControlPtr regexControl_;
    quint64 searchGeneration_ = 0;
    sptr_t regexCaret_ = 0,regexAnchor_ = 0;
    std::optional<SearchMatch> lastRegexMatch_;
    void configureLanguage(const QString& path, bool large);
    void countMatches();
    sptr_t searchChunk(sptr_t& cursor, sptr_t limit, bool backwards, bool& done);
    bool beginNavigation(bool backwards, bool includeSelection);
    void advanceNavigation();
    void updateMargins();
    void updateBraces();
    void applyTypography();
    void applyColors();
    QString language_ = "Plain Text";
    QString colorTheme_;
    QByteArray lexer_ = "null";
    EditorAppearance appearance_ = EditorAppearance::defaults();
    DocumentPtr documentOwner_;
    QSet<int> commentStyles_;
    QTimer searchTimer_;
    QByteArray query_;
    int searchFlags_ = 0;
    sptr_t scanAt_ = 0;
    QVector<SearchMatch> matches_;
    bool counting_ = false;
    bool capped_ = false;
    bool navigating_ = false;
    bool navigationBackwards_ = false;
    bool navigationWrapped_ = false;
    bool navigationFound_ = false;
    sptr_t navigationAt_ = 0, navigationOrigin_ = 0, navigationLimit_ = 0;
    sptr_t navigationCaret_ = 0, navigationAnchor_ = 0;
};
