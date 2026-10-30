#include "logreader.h"
#include <QDir>
#include <QStringDecoder>
#include <windows.h>

namespace {
constexpr qint64 maximum=256ll*1024*1024;
constexpr qint64 appendBatch=256*1024;
constexpr qint64 sampleBytes=4096;
struct NativeFile {
    HANDLE handle=INVALID_HANDLE_VALUE;
    explicit NativeFile(const QString& path) {
        auto native=QDir::toNativeSeparators(path);
        if(!native.startsWith("\\\\?\\"))native=native.startsWith("\\\\")?"\\\\?\\UNC\\"+native.mid(2):"\\\\?\\"+native;
        handle=CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    }
    ~NativeFile(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
    QByteArray read(qint64 offset,qint64 count) const {
        LARGE_INTEGER position;position.QuadPart=offset;
        if(!SetFilePointerEx(handle,position,nullptr,FILE_BEGIN))return {};
        QByteArray bytes(count,Qt::Uninitialized);DWORD got=0;
        if(!ReadFile(handle,bytes.data(),DWORD(count),&got,nullptr))return {};
        bytes.resize(got);return bytes;
    }
};
struct Identity {
    DWORD volume=0,high=0,low=0;
    bool operator==(const Identity&) const = default;
};
quint64 timestamp(const FILETIME& time){return (quint64(time.dwHighDateTime)<<32)|time.dwLowDateTime;}
QString bomEncoding(const QByteArray& bytes) {
    if(bytes.startsWith(QByteArray::fromHex("fffe0000")))return "utf-32le";
    if(bytes.startsWith(QByteArray::fromHex("0000feff")))return "utf-32be";
    if(bytes.startsWith(QByteArray::fromHex("fffe")))return "utf-16le";
    if(bytes.startsWith(QByteArray::fromHex("feff")))return "utf-16be";
    if(bytes.startsWith(QByteArray::fromHex("efbbbf")))return "utf-8";
    return {};
}
bool incompleteBom(const QByteArray& bytes) {
    if(bytes.isEmpty())return false;
    for(const auto* hex:{"fffe0000","0000feff","fffe","feff","efbbbf"}) {
        const auto bom=QByteArray::fromHex(hex);
        if(bytes.size()<bom.size() && bom.startsWith(bytes))return true;
    }
    return false;
}
}

struct LogReader::State {
    QString requested,encoding;
    Identity identity;
    quint64 written=0;
    qint64 offset=0;
    QByteArray head,tail;
    bool initialized=false;
    std::unique_ptr<QStringDecoder> unicode;
    bool legacy=false;
    QByteArray legacyPending;
    QChar highSurrogate;
    bool firstText=true,crlf=false,lf=false,cr=false,lastCr=false;
    qsizetype lineLength=0,longest=0;

    void decoder(const QString& selected) {
        encoding=selected;unicode.reset();legacyPending.clear();legacy=false;highSurrogate=QChar();firstText=true;
        if(selected=="gb18030" || selected=="gbk")legacy=true;
        else unicode=std::make_unique<QStringDecoder>(selected.toLatin1().constData());
    }
    QString decode(const QByteArray& bytes,bool& valid) {
        QString text;
        if(unicode){text=(*unicode)(bytes);valid=unicode->isValid() && !unicode->hasError();}
        else if(legacy){
            // Keep incomplete GB characters until the next write; decode complete
            // sequences with Windows, which preserves supplementary GB18030 characters.
            const auto input=legacyPending+bytes;qsizetype complete=0;
            while(complete<input.size()) {
                const auto lead=static_cast<unsigned char>(input[complete]);qsizetype length=1;
                if(lead>=0x81 && lead<=0xfe) {
                    if(complete+1>=input.size())break;
                    const auto second=static_cast<unsigned char>(input[complete+1]);length=encoding=="gb18030" && second>=0x30 && second<=0x39?4:2;
                }
                if(complete+length>input.size())break;complete+=length;
            }
            legacyPending=input.mid(complete);text=decodeLegacyText(QByteArrayView(input).first(complete),encoding,valid);
        }
        else valid=false;
        if(!highSurrogate.isNull()){text.prepend(highSurrogate);highSurrogate=QChar();}
        if(!text.isEmpty() && text.back().isHighSurrogate()){highSurrogate=text.back();text.chop(1);}
        if(firstText && !text.isEmpty()){firstText=false;if(text.front()==QChar(0xfeff))text.remove(0,1);}
        if(text.contains(QChar(0)))valid=false;
        return text;
    }
    void measure(const QString& text) {
        for(const auto c:text) {
            if(lastCr){if(c=='\n')crlf=true;else cr=true;}
            if(c=='\r' || c=='\n'){longest=qMax(longest,lineLength);lineLength=0;if(c=='\n' && !lastCr)lf=true;}
            else ++lineLength;
            lastCr=c=='\r';
        }
        longest=qMax(longest,lineLength);
    }
    FileContent metadata() const {
        FileContent result;result.originalSize=offset;result.encoding=textEncodingName(encoding);
        const bool hasCr=cr || lastCr;
        result.lineEnding=int(crlf)+int(lf)+int(hasCr)>1?QStringLiteral("混合换行"):crlf?"CRLF":hasCr?"CR":"LF";
        result.large=offset>8*1024*1024 || longest>20000;return result;
    }
};

LogReader::LogReader(QString encoding):state_(std::make_unique<State>()){state_->requested=normalizedTextEncoding(encoding);}
LogReader::~LogReader()=default;

LogUpdate LogReader::poll(const QString& path,const LoadControlPtr& control) {
    LogUpdate result;
    auto wait=[&](const QString& message){result.waiting=true;result.notice=message;return result;};
    auto fail=[&](const QString& message){result.failed=true;result.notice=message;return result;};
    try {
        auto& state=*state_;
        if(control->canceled.load())return result;
        NativeFile file(path);BY_HANDLE_FILE_INFORMATION info{};
        if(file.handle==INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file.handle,&info))return wait(QStringLiteral("文件暂时不可用，正在等待恢复…"));
        if(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)return fail(QStringLiteral("无法跟随文件夹。"));
        const auto size=qint64((quint64(info.nFileSizeHigh)<<32)|info.nFileSizeLow);
        if(size>maximum)return fail(QStringLiteral("已停止跟随：文件超过 256 MiB。原内容仍可阅读。"));
        const Identity identity{info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow};const auto written=timestamp(info.ftLastWriteTime);
        bool reset=!state.initialized || !(identity==state.identity) || size<state.offset || (!state.offset && size>0) || (size==state.offset && written!=state.written);
        if(!reset && state.offset) {
            reset=file.read(0,state.head.size())!=state.head || file.read(state.offset-state.tail.size(),state.tail.size())!=state.tail;
        }
        if(!reset && size==state.offset){result.content=state.metadata();return result;}
        const auto begin=reset?0:state.offset;const auto end=reset?size:qMin(size,begin+appendBatch);
        QByteArray bytes;bytes.reserve(end-begin);
        for(qint64 offset=begin;offset<end;) {
            if(control->canceled.load())return result;
            const auto count=qMin<qint64>(2*1024*1024,end-offset);auto block=file.read(offset,count);
            if(block.size()!=count){state.initialized=false;return wait(QStringLiteral("文件正在改写，等待下一次读取…"));}
            bytes.append(block);offset+=count;
        }
        if(control->canceled.load())return result;
        const auto head=reset?bytes.left(sampleBytes):(state.head+bytes.left(sampleBytes)).left(qMin(sampleBytes,end));
        const auto tail=reset?bytes.right(sampleBytes):(state.tail+bytes).right(qMin(sampleBytes,end));
        if(file.read(0,head.size())!=head || file.read(end-tail.size(),tail.size())!=tail){state.initialized=false;return wait(QStringLiteral("文件正在改写，等待下一次读取…"));}
        QString text;bool valid=true;
        if(reset) {
            const auto requested=state.requested;state=State{};state.requested=requested;
            if(incompleteBom(bytes))return wait(QStringLiteral("正在等待完整的编码标记…"));
            const auto bom=bomEncoding(bytes);
            if(requested!="auto" && !bom.isEmpty() && requested!=bom)return fail(QStringLiteral("编码与文件的 BOM 不一致，请选择匹配的编码后重新开启跟随。"));
            auto selected=requested=="auto"?(bom.isEmpty()?QString("utf-8"):bom):requested;
            state.decoder(selected);text=state.decode(bytes,valid);
            if(!valid && requested=="auto" && bom.isEmpty()){state.decoder("gb18030");text=state.decode(bytes,valid);}
        } else text=state.decode(bytes,valid);
        if(!valid)return fail(QStringLiteral("已停止跟随：新增内容包含无效字符或空字符。请检查文件编码后重试。"));
        if(control->canceled.load())return result;
        state.measure(text);state.offset=end;state.identity=identity;state.written=written;
        state.head=head;state.tail=tail;
        result.content=state.metadata();result.reset=reset;result.more=end<size;
        if(reset) {auto content=result.content;content.utf8=text.toUtf8();result.replacement=prepareDocument(std::move(content),control);if(!result.replacement.content.error.isEmpty())return fail(result.replacement.content.error);}
        else result.appended=text.toUtf8();
        state.initialized=true;
    } catch(...) {return fail(QStringLiteral("已停止跟随：无法继续读取，可能没有足够的内存。"));}
    return result;
}
