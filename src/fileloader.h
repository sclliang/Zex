#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>

enum class LoadStage { Reading, Decoding, Preparing };
struct LoadControl {
    std::atomic<bool> canceled{false};
    std::atomic<int> percent{0};
    std::atomic<LoadStage> stage{LoadStage::Reading};
    void update(LoadStage next,int value) { stage.store(next);percent.store(value); }
};
using LoadControlPtr=std::shared_ptr<LoadControl>;

struct FileContent {
    QByteArray utf8;
    QString encoding;
    QString lineEnding;
    QString error;
    qint64 originalSize = 0;
    bool large = false;
    bool canceled = false;
};
QStringList textEncodingIds();
QString normalizedTextEncoding(const QString& encoding);
QString textEncodingName(const QString& encoding);
QString decodeLegacyText(QByteArrayView bytes,const QString& encoding,bool& valid);
FileContent decodeFile(const QByteArray& bytes,const QString& encoding = "auto",const LoadControlPtr& control = {});
FileContent readFile(const QString& path,const QString& encoding = "auto",const LoadControlPtr& control = {});
