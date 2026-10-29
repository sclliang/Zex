#pragma once
#include <QColor>
#include <QIcon>
#include <QString>
#include <QStringList>
class QApplication;
class QWidget;
class QSettings;
namespace Theme {
struct Colors {
    QString id, name;
    bool dark;
    QColor background, chrome, elevated, text, muted, subtle, accent, accentText;
    QColor border, input, hover, selection, scrollbar;
    QColor comment, keyword, string, number, type, preprocessor, error;
};
QStringList themes();
QString normalizedTheme(const QString& id);
QString id();
const Colors& colors(const QString& id = {});
QString storedTheme(QSettings& settings);
void saveTheme(QSettings& settings, const QString& id);
void apply(QApplication& app, const QString& id = "graphite");
void preview(QWidget& widget, const QString& id);
QIcon swatch(const QString& id);
QIcon icon(const QString& name, QColor color = colors().text, int size = 18);
QIcon fileIcon(const QString& path);
QString codeFont();
}
