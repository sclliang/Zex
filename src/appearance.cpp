#include "appearance.h"
#include "theme.h"
#include <QFontDatabase>
#include <QSettings>

EditorAppearance EditorAppearance::defaults() {
    EditorAppearance result; result.family = Theme::codeFont(); return result;
}
EditorAppearance EditorAppearance::sanitized() const {
    auto result = *this;
    if (!QFontDatabase::families().contains(result.family, Qt::CaseInsensitive)) result.family = Theme::codeFont();
    result.points = qBound(8, result.points, 32);
    result.linePadding = qBound(0, result.linePadding, 16);
    return result;
}
EditorAppearance EditorAppearance::fromSettings(QSettings& settings) {
    auto result = defaults();
    result.family = settings.value("editor/fontFamily", result.family).toString();
    result.points = settings.value("editor/fontSize", settings.value("fontSize",12)).toInt();
    result.bold = settings.value("editor/bold", false).toBool();
    result.italic = settings.value("editor/italic", false).toBool();
    result.commentItalics = settings.value("editor/commentItalics", true).toBool();
    result.linePadding = settings.value("editor/linePadding", 6).toInt();
    return result.sanitized();
}
void EditorAppearance::save(QSettings& settings) const {
    const auto value = sanitized();
    settings.setValue("editor/fontFamily", value.family);
    settings.setValue("editor/fontSize", value.points);
    settings.setValue("editor/bold", value.bold);
    settings.setValue("editor/italic", value.italic);
    settings.setValue("editor/commentItalics", value.commentItalics);
    settings.setValue("editor/linePadding", value.linePadding);
    settings.setValue("fontSize", value.points); // Compatibility with 0.1 preferences.
}
