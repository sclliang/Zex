#pragma once
#include "ScintillaEditBase.h"
#include "fileloader.h"
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
    void updateMargins();
    void updateBraces();
    void applyTypography();
    void applyColors();
    QString language_ = "Plain Text";
    QString colorTheme_;
    QByteArray lexer_ = "null";
    EditorAppearance appearance_ = EditorAppearance::defaults();
    QSet<int> commentStyles_;
    QTimer searchTimer_;
    QByteArray query_;
    int searchFlags_ = 0;
    sptr_t scanAt_ = 0;
    QVector<QPair<sptr_t, sptr_t>> matches_;
    bool counting_ = false;
    bool capped_ = false;
};
