#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>

struct ReadingPosition {
    qint64 topLine = 0;
    int wrapOffset = 0;
    int xOffset = 0;
    qint64 caretLine = 0;
    qint64 caretColumn = 0;
    qint64 anchorLine = 0;
    qint64 anchorColumn = 0;
    bool operator==(const ReadingPosition&) const = default;
};

struct SessionTab {
    QString path;
    ReadingPosition position;
    QString encoding = "auto";
};

struct SessionSnapshot {
    QVector<SessionTab> tabs;
    QString activePath;
    QByteArray serialize() const;
    static SessionSnapshot deserialize(const QByteArray& bytes);
};

class SessionStore {
public:
    explicit SessionStore(QString path = {});
    static QString defaultPath();
    SessionSnapshot read() const;
    bool write(const SessionSnapshot& session, QString* error = nullptr);
    bool enabled() const { return !path_.isEmpty(); }
private:
    QString path_;
    QByteArray lastWritten_;
};
