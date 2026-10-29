#pragma once
#include <QIcon>
#include <QStringList>

namespace FileIcons {
QStringList themes();
QString themeName(const QString& id);
QString normalizedTheme(const QString& id);
void setTheme(const QString& id);
QString theme();
QString iconId(const QString& path, const QString& themeId = {}, const QString& colorTheme = {});
QIcon icon(const QString& path, const QString& themeId = {}, const QString& colorTheme = {});
}
