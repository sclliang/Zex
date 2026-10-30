#include "session.h"
#include "fileloader.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <limits>
#include <algorithm>

namespace {
constexpr qint64 maximumBytes = 4 * 1024 * 1024;
constexpr int maximumTabs = 4096;
qint64 nonnegative(const QJsonObject& object, const char* name) {
    return std::clamp<qint64>(object[name].toInteger(),0,std::numeric_limits<int>::max());
}
QJsonObject positionJson(const ReadingPosition& p){return {{"topLine",p.topLine},{"wrapOffset",p.wrapOffset},{"xOffset",p.xOffset},{"caretLine",p.caretLine},{"caretColumn",p.caretColumn},{"anchorLine",p.anchorLine},{"anchorColumn",p.anchorColumn}};}
ReadingPosition readPosition(const QJsonObject& p){return {nonnegative(p,"topLine"),int(nonnegative(p,"wrapOffset")),int(nonnegative(p,"xOffset")),nonnegative(p,"caretLine"),nonnegative(p,"caretColumn"),nonnegative(p,"anchorLine"),nonnegative(p,"anchorColumn")};}
}

QByteArray SessionSnapshot::serialize() const {
    QJsonArray items;
    for (const auto& tab : tabs) {
        const auto& p = tab.position;
        QJsonObject position{{"topLine",p.topLine},{"wrapOffset",p.wrapOffset},{"xOffset",p.xOffset},
            {"caretLine",p.caretLine},{"caretColumn",p.caretColumn},{"anchorLine",p.anchorLine},{"anchorColumn",p.anchorColumn}};
        QJsonArray bookmarks;for(auto line:tab.bookmarks)bookmarks.append(line);
        items.append(QJsonObject{{"path",tab.path},{"position",position},{"encoding",normalizedTextEncoding(tab.encoding)},{"pinned",tab.pinned},{"bookmarks",bookmarks}});
    }
    QJsonObject splitJson{{"enabled",split.enabled},{"activeRight",split.activeRight},{"leftPath",split.leftPath},{"rightPath",split.rightPath},{"rightPosition",positionJson(split.rightPosition)},{"rightEncoding",normalizedTextEncoding(split.rightEncoding)},{"leftPercent",split.leftPercent}};
    return QJsonDocument(QJsonObject{{"version",1},{"activePath",activePath},{"tabs",items},{"split",splitJson}}).toJson(QJsonDocument::Compact);
}

SessionSnapshot SessionSnapshot::deserialize(const QByteArray& bytes) {
    SessionSnapshot session;
    if (bytes.size() > maximumBytes) return session;
    const auto root = QJsonDocument::fromJson(bytes).object();
    if (root["version"].toInt() != 1 || !root["tabs"].isArray()) return session;
    QSet<QString> paths;
    for (const auto& value : root["tabs"].toArray()) {
        if (session.tabs.size() >= maximumTabs) break;
        const auto item = value.toObject();
        auto path = item["path"].toString();
        if (path.isEmpty() || path.size() > 32767 || path.contains(QChar(0)) || !QDir::isAbsolutePath(path)) continue;
        // Lexical normalization only: never stat a restored file or network share on the UI thread.
        path = QDir::cleanPath(QDir::fromNativeSeparators(path));
        if (paths.contains(path.toCaseFolded())) continue;
        paths.insert(path.toCaseFolded());
        const auto p = item["position"].toObject();
        session.tabs.append({path, {nonnegative(p,"topLine"),int(nonnegative(p,"wrapOffset")),int(nonnegative(p,"xOffset")),
            nonnegative(p,"caretLine"),nonnegative(p,"caretColumn"),nonnegative(p,"anchorLine"),nonnegative(p,"anchorColumn")},normalizedTextEncoding(item["encoding"].toString()),item["pinned"].toBool()});
        auto& bookmarks=session.tabs.last().bookmarks;
        for(const auto& value:item["bookmarks"].toArray()){const auto line=value.toInteger(-1);if(line>=0 && line<=std::numeric_limits<int>::max())bookmarks.append(line);if(bookmarks.size()>=1000)break;}
        std::sort(bookmarks.begin(),bookmarks.end());bookmarks.erase(std::unique(bookmarks.begin(),bookmarks.end()),bookmarks.end());
    }
    const auto active = QDir::cleanPath(QDir::fromNativeSeparators(root["activePath"].toString()));
    for (const auto& tab : session.tabs)
        if (tab.path.compare(active,Qt::CaseInsensitive) == 0) { session.activePath = tab.path; break; }
    if (session.activePath.isEmpty() && !session.tabs.isEmpty()) session.activePath = session.tabs.first().path;
    const auto split=root["split"].toObject();
    auto findPath=[&](const QString& raw){const auto path=QDir::cleanPath(QDir::fromNativeSeparators(raw));for(const auto& tab:session.tabs)if(tab.path.compare(path,Qt::CaseInsensitive)==0)return tab.path;return QString();};
    const auto left=findPath(split["leftPath"].toString()),right=findPath(split["rightPath"].toString());
    if(split["enabled"].toBool() && !left.isEmpty() && !right.isEmpty())session.split={true,split["activeRight"].toBool(),left,right,readPosition(split["rightPosition"].toObject()),normalizedTextEncoding(split["rightEncoding"].toString()),qBound(20,split["leftPercent"].toInt(50),80)};
    return session;
}

SessionStore::SessionStore(QString path) : path_(std::move(path)) {}
QString SessionStore::defaultPath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("session.json");
}
SessionSnapshot SessionStore::read() const {
    if (!enabled()) return {};
    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumBytes) return {};
    return SessionSnapshot::deserialize(file.read(maximumBytes + 1));
}
bool SessionStore::write(const SessionSnapshot& session, QString* error) {
    if (!enabled()) return true;
    const auto bytes = session.serialize();
    if (bytes == lastWritten_) return true;
    auto fail = [error](const QString& message) { if (error) *error = message; return false; };
    if (bytes.size() > maximumBytes || session.tabs.size() > maximumTabs)
        return fail(QStringLiteral("会话超过可保存的大小。"));
    if (!QDir().mkpath(QFileInfo(path_).absolutePath()))
        return fail(QStringLiteral("无法创建会话保存目录。"));
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    if (file.write(bytes) != bytes.size() || !file.commit()) return fail(file.errorString());
    lastWritten_ = bytes;
    return true;
}
