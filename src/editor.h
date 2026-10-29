#pragma once
#include "ScintillaEditBase.h"
#include "fileloader.h"
#include "documentloader.h"
#include "appearance.h"
#include "session.h"
#include <QSet>
#include <QTimer>
#include <QVector>

class CodeEditor : public ScintillaEditBase {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr);
    void setContent(const FileContent& content, const QString& path);
    void setPreparedContent(const PreparedFile& file,const QString& path);
    QString language() const { return language_; }
    void setWordWrap(bool on);
    void setPointSize(int points);
    int pointSize() const { return appearance_.points; }
    void setAppearance(const EditorAppearance& appearance);
    EditorAppearance appearance() const { return appearance_; }
    void setSearch(const QString& query, bool caseSensitive, bool wholeWord);
    bool findNext(bool backwards = false);
    void clearSearch();
    int matchCount() const { return matches_.size(); }
    bool searchBusy() const { return counting_ || navigating_; }
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
    void fileDropped(const QString& path);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
private:
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
    QVector<QPair<sptr_t, sptr_t>> matches_;
    bool counting_ = false;
    bool capped_ = false;
    bool navigating_ = false;
    bool navigationBackwards_ = false;
    bool navigationWrapped_ = false;
    bool navigationFound_ = false;
    sptr_t navigationAt_ = 0, navigationOrigin_ = 0, navigationLimit_ = 0;
    sptr_t navigationCaret_ = 0, navigationAnchor_ = 0;
};
