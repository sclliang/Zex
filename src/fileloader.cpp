#include "fileloader.h"
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>
#include <QTextCodec>

QStringList textEncodingIds() { return {"auto","utf-8","gb18030","gbk","utf-16le","utf-16be","utf-32le","utf-32be"}; }
QString normalizedTextEncoding(const QString& encoding) { return textEncodingIds().contains(encoding)?encoding:"auto"; }
QString textEncodingName(const QString& encoding) {
    if(encoding=="utf-8")return "UTF-8";
    if(encoding=="gb18030")return "GB18030 / GBK";
    if(encoding=="gbk")return "GBK";
    if(encoding=="utf-16le")return "UTF-16 LE";
    if(encoding=="utf-16be")return "UTF-16 BE";
    if(encoding=="utf-32le")return "UTF-32 LE";
    if(encoding=="utf-32be")return "UTF-32 BE";
    return QStringLiteral("自动识别");
}

FileContent decodeFile(const QByteArray& bytes,const QString& requested) {
    FileContent result;
    result.originalSize = bytes.size();
    QString text;
    auto decode = [&](QStringConverter::Encoding encoding, const QString& name) {
        const int unit = encoding==QStringConverter::Utf8?1:(encoding == QStringConverter::Utf32LE || encoding == QStringConverter::Utf32BE) ? 4 : 2;
        if (bytes.size() % unit != 0) {
            result.error = QStringLiteral("文件的 %1 字符序列不完整。").arg(name);
            return;
        }
        QStringDecoder decoder(encoding, QStringConverter::Flag::Stateless);
        text = decoder(bytes);
        result.encoding = name;
        if (decoder.hasError()) result.error = QStringLiteral("文件包含无效的 %1 字符序列。").arg(name);
    };
    auto decodeLegacy=[&](const QString& id) {
        QTextCodec::ConverterState state;
        auto* codec=QTextCodec::codecForName(id=="gbk"?"GBK":"GB18030");
        result.encoding=textEncodingName(id);
        if(!codec){result.error=QStringLiteral("无法加载 %1 编码支持。").arg(result.encoding);return;}
        text=codec->toUnicode(bytes.constData(),bytes.size(),&state);
        if(state.invalidChars || state.remainingChars)result.error=QStringLiteral("文件包含无效或不完整的 %1 字符。请尝试其他编码。").arg(result.encoding);
    };
    QString bom;
    if(bytes.startsWith(QByteArray::fromHex("fffe0000")))bom="utf-32le";
    else if(bytes.startsWith(QByteArray::fromHex("0000feff")))bom="utf-32be";
    else if(bytes.startsWith(QByteArray::fromHex("fffe")))bom="utf-16le";
    else if(bytes.startsWith(QByteArray::fromHex("feff")))bom="utf-16be";
    else if(bytes.startsWith(QByteArray::fromHex("efbbbf")))bom="utf-8";
    const auto selected=normalizedTextEncoding(requested);
    if(selected!="auto" && !bom.isEmpty() && selected!=bom) {
        result.error=QStringLiteral("文件的 BOM 标记为 %1，与所选 %2 不一致。请选择自动识别或匹配的编码。").arg(textEncodingName(bom),textEncodingName(selected));return result;
    }
    const auto encoding=selected=="auto"?bom:selected;
    if(encoding=="utf-32le")decode(QStringConverter::Utf32LE,"UTF-32 LE");
    else if(encoding=="utf-32be")decode(QStringConverter::Utf32BE,"UTF-32 BE");
    else if(encoding=="utf-16le")decode(QStringConverter::Utf16LE,"UTF-16 LE");
    else if(encoding=="utf-16be")decode(QStringConverter::Utf16BE,"UTF-16 BE");
    else if(encoding=="gb18030" || encoding=="gbk")decodeLegacy(encoding);
    else {
        if (bytes.contains('\0')) {
            result.error = QStringLiteral("文件包含空字符。若是无 BOM 的 UTF-16 / UTF-32 文件，请手动选择编码；Zex 仅支持文本和代码文件。");
            return result;
        }
        QStringDecoder decoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
        text = decoder(bytes);
        result.encoding = bytes.startsWith(QByteArray::fromHex("efbbbf")) ? "UTF-8 BOM" : "UTF-8";
        if (decoder.hasError()) {
            if (selected!="auto" || !bom.isEmpty()) {
                result.error = QStringLiteral("文件包含不完整或无效的 UTF-8 字符。请尝试其他编码。");
                return result;
            }
            decodeLegacy("gb18030");
        }
    }
    if (!result.error.isEmpty()) return result;
    if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);
    if (text.contains(QChar(0))) {
        result.error = QStringLiteral("文件含有空字符，可能不是文本文件。");
        return result;
    }
    bool crlf = false, lf = false, cr = false;
    qsizetype lineLength = 0, longest = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const auto c = text.at(i);
        if (c == '\r' || c == '\n') {
            longest = qMax(longest, lineLength);
            lineLength = 0;
            if (c == '\r') {
                if (i + 1 < text.size() && text.at(i + 1) == '\n') { crlf = true; ++i; }
                else cr = true;
            } else lf = true;
        } else ++lineLength;
    }
    longest = qMax(longest, lineLength);
    result.lineEnding = int(crlf) + int(lf) + int(cr) > 1 ? "混合换行" : crlf ? "CRLF" : cr ? "CR" : "LF";
    result.large = bytes.size() > 8 * 1024 * 1024 || longest > 20000;
    result.utf8 = text.toUtf8();
    return result;
}

FileContent readFile(const QString& path,const QString& encoding) {
    FileContent result;
    const QFileInfo info(path);
    if (!info.isFile()) {
        result.error = QStringLiteral("文件不存在或不是普通文件。\n%1").arg(path);
        return result;
    }
    constexpr qint64 maximum = 256ll * 1024 * 1024;
    if (info.size() > maximum) {
        result.error = QStringLiteral("首版支持最大 256 MB 的文本文件。");
        return result;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("无法读取文件：%1").arg(file.errorString());
        return result;
    }
    auto bytes = file.read(maximum + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > maximum || !file.atEnd()) {
        result.error = QStringLiteral("文件读取失败或读取期间文件大小超出限制。");
        return result;
    }
    return decodeFile(bytes,encoding);
}
