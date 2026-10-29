#include "fileicons.h"
#include "theme.h"
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSvgRenderer>

// Explicitly reference the resource object so static-library linking retains it.
static void initializeIconResources() { Q_INIT_RESOURCE(file_icons); }

namespace {
struct Catalog { QJsonObject definitions, extensions, names; QString fallback; };
QString selectedTheme = "material";
const Catalog& catalog(const QString& id, bool light) {
    static const bool initialized = [] { initializeIconResources(); return true; }();
    Q_UNUSED(initialized);
    static QHash<QString,Catalog> catalogs;
    const QString base = id == "material" ? "material" : "seti";
    const QString key = base + (light ? ":light" : ":dark");
    if (!catalogs.contains(key)) {
        QFile file(":/file-icons/" + base + ".json"); file.open(QIODevice::ReadOnly);
        const auto json = QJsonDocument::fromJson(file.readAll()).object();
        Catalog data{json["definitions"].toObject(),json["extensions"].toObject(),json["names"].toObject(),json["default"].toString()};
        if(light) {
            const auto overrides=json["light"].toObject();
            const auto extensions=overrides["extensions"].toObject(),names=overrides["names"].toObject();
            for(auto it=extensions.begin();it!=extensions.end();++it)data.extensions[it.key()]=it.value();
            for(auto it=names.begin();it!=names.end();++it)data.names[it.key()]=it.value();
            if(overrides.contains("default"))data.fallback=overrides["default"].toString();
        }
        catalogs.insert(key,data);
    }
    return catalogs.constFind(key).value();
}
QString setiFamily() {
    static const auto family = [] {
        const int id = QFontDatabase::addApplicationFont(":/file-icons/seti.ttf");
        return QFontDatabase::applicationFontFamilies(id).value(0);
    }();
    return family;
}
}

QStringList FileIcons::themes() { return {"material", "seti", "seti-mono"}; }
QString FileIcons::themeName(const QString& id) {
    if (id == "seti") return QStringLiteral("Seti · 彩色");
    if (id == "seti-mono") return QStringLiteral("Seti · 单色");
    return QStringLiteral("Material · 彩色");
}
QString FileIcons::normalizedTheme(const QString& id) { return themes().contains(id) ? id : "material"; }
void FileIcons::setTheme(const QString& id) { selectedTheme = normalizedTheme(id); }
QString FileIcons::theme() { return selectedTheme; }
QString FileIcons::iconId(const QString& path, const QString& themeId, const QString& colorTheme) {
    const auto& data = catalog(normalizedTheme(themeId.isEmpty() ? selectedTheme : themeId),!Theme::colors(colorTheme).dark);
    const QString name = QFileInfo(path).fileName().toLower();
    if (data.names.contains(name)) return data.names[name].toString();
    // Prefer compound suffixes (d.ts, test.js, etc.) before the final suffix.
    for (auto dot = name.indexOf('.'); dot >= 0; dot = name.indexOf('.', dot + 1)) {
        const auto suffix = name.mid(dot + 1);
        if (data.extensions.contains(suffix)) return data.extensions[suffix].toString();
    }
    return data.fallback;
}
QIcon FileIcons::icon(const QString& path, const QString& themeId, const QString& colorTheme) {
    const auto id = normalizedTheme(themeId.isEmpty() ? selectedTheme : themeId);
    const auto& colors=Theme::colors(colorTheme);
    const auto key = iconId(path, id, colorTheme);
    const bool generic=key==catalog(id,!colors.dark).fallback;
    static QHash<QString,QIcon> cache;
    const auto cacheKey = id + ':' + key + (colors.dark ? ":dark" : ":light") + (id=="seti-mono" || generic?colors.muted.name():QString());
    if (const auto found = cache.constFind(cacheKey); found != cache.cend()) return found.value();
    const auto definition = catalog(id,!colors.dark).definitions[key].toObject();
    if (definition.isEmpty()) return Theme::icon("file", colors.muted);
    QIcon result;
    QSvgRenderer svg;
    if (id == "material") svg.load(":/file-icons/" + definition["svg"].toString());
    for (const qreal ratio : {1.0, 1.5, 2.0, 3.0}) {
        QPixmap pix(qRound(18 * ratio), qRound(18 * ratio));
        pix.setDevicePixelRatio(ratio); pix.fill(Qt::transparent);
        QPainter painter(&pix); painter.setRenderHint(QPainter::Antialiasing);
        if (id == "material") {
            svg.render(&painter, QRectF(0,0,18,18));
            if(generic){painter.setCompositionMode(QPainter::CompositionMode_SourceIn);painter.fillRect(QRectF(0,0,18,18),colors.muted);}
        }
        else {
            QFont font(setiFamily()); font.setPixelSize(27);
            painter.setFont(font);
            painter.setPen(id == "seti-mono" || generic ? colors.muted : QColor(definition["color"].toString()));
            painter.drawText(QRectF(0,0,18,18), Qt::AlignCenter, definition["glyph"].toString());
        }
        painter.end(); result.addPixmap(pix);
    }
    cache.insert(cacheKey, result);
    return result;
}
