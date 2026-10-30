#pragma once
#include <QString>
class QSettings;

struct EditorAppearance {
    QString family;
    int points = 11;
    bool bold = false;
    bool italic = false;
    bool commentItalics = false;
    int linePadding = 3;
    static EditorAppearance defaults();
    static EditorAppearance fromSettings(QSettings& settings);
    EditorAppearance sanitized() const;
    void save(QSettings& settings) const;
    bool operator==(const EditorAppearance&) const = default;
};
