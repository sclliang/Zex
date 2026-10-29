#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>

struct FileContent {
    QByteArray utf8;
    QString encoding;
    QString lineEnding;
    QString error;
    qint64 originalSize = 0;
    bool large = false;
};
QStringList textEncodingIds();
QString normalizedTextEncoding(const QString& encoding);
QString textEncodingName(const QString& encoding);
FileContent decodeFile(const QByteArray& bytes,const QString& encoding = "auto");
FileContent readFile(const QString& path,const QString& encoding = "auto");
