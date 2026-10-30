#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include "regexsearch.h"
#include <QElapsedTimer>
#include <algorithm>

namespace {
struct Guard { RegexControlPtr control;QElapsedTimer clock;int timeout;quint64 calls=0; };
int interrupt(pcre2_callout_block*,void* opaque) {
    auto& guard=*static_cast<Guard*>(opaque);
    return guard.control->canceled.load(std::memory_order_relaxed) || ((++guard.calls&1023)==0 && guard.clock.elapsed()>guard.timeout)?PCRE2_ERROR_CALLOUT:0;
}
qint64 nextCharacter(const QByteArray& text,qint64 at) {
    ++at;while(at<text.size() && (static_cast<unsigned char>(text[at])&0xc0)==0x80)++at;return at;
}
}
RegexResult searchRegex(const RegexRequest& request,const RegexControlPtr& control) {
    RegexResult result;Guard guard{control,{},request.timeoutMs};guard.clock.start();
    if(control->canceled.load()){result.canceled=true;return result;}
    if(!request.text.isValidUtf8()){result.error=QStringLiteral("搜索文本不是有效的 UTF-8。");return result;}
    if(request.pattern.size()>16384){result.error=QStringLiteral("正则表达式过长（最多 16 KiB）。");return result;}
    auto pattern=request.wholeWord?QByteArray("(?<!\\w)(?:")+request.pattern+")(?!\\w)":request.pattern;
    std::unique_ptr<pcre2_compile_context,decltype(&pcre2_compile_context_free)> compile(pcre2_compile_context_create(nullptr),pcre2_compile_context_free);
    if(!compile){result.error=QStringLiteral("内存不足，无法搜索。");return result;}
    pcre2_set_newline(compile.get(),PCRE2_NEWLINE_ANYCRLF);
    pcre2_set_parens_nest_limit(compile.get(),128);pcre2_set_max_pattern_length(compile.get(),20000);
    pcre2_set_max_pattern_compiled_length(compile.get(),1024*1024);
    int error=0;PCRE2_SIZE offset=0;
    const uint32_t options=PCRE2_UTF|PCRE2_UCP|PCRE2_MULTILINE|PCRE2_AUTO_CALLOUT|PCRE2_NO_START_OPTIMIZE|PCRE2_NEVER_BACKSLASH_C|(request.caseSensitive?0:PCRE2_CASELESS);
    std::unique_ptr<pcre2_code,decltype(&pcre2_code_free)> code(pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.constData()),pattern.size(),options,&error,&offset,compile.get()),pcre2_code_free);
    if(!code){PCRE2_UCHAR message[256]{};pcre2_get_error_message(error,message,sizeof(message));result.error=QStringLiteral("正则无效：%1").arg(QString::fromUtf8(reinterpret_cast<const char*>(message)));return result;}
    std::unique_ptr<pcre2_match_context,decltype(&pcre2_match_context_free)> context(pcre2_match_context_create(nullptr),pcre2_match_context_free);
    std::unique_ptr<pcre2_match_data,decltype(&pcre2_match_data_free)> data(pcre2_match_data_create_from_pattern(code.get(),nullptr),pcre2_match_data_free);
    if(!context || !data){result.error=QStringLiteral("内存不足，无法搜索。");return result;}
    pcre2_set_match_limit(context.get(),500000);pcre2_set_depth_limit(context.get(),500);pcre2_set_heap_limit(context.get(),65536);pcre2_set_callout(context.get(),interrupt,&guard);
    qint64 cursor=0;bool retryEmpty=false;SearchMatch first{-1,-1},last{-1,-1},previous{-1,-1},next{-1,-1};
    if(request.mode==RegexMode::Next)cursor=std::clamp<qint64>(request.origin,0,request.text.size());
    bool wrapped=false;
    while(cursor<=request.text.size()) {
        if(control->canceled.load()){result.canceled=true;break;}
        if(guard.clock.elapsed()>request.timeoutMs){result.error=QStringLiteral("搜索耗时过长，请缩小选区或简化表达式。");break;}
        const auto flags=PCRE2_NO_UTF_CHECK|(retryEmpty?(PCRE2_NOTEMPTY_ATSTART|PCRE2_ANCHORED):0);
        const auto found=pcre2_match(code.get(),reinterpret_cast<PCRE2_SPTR>(request.text.constData()),request.text.size(),cursor,flags,data.get(),context.get());
        if(found==PCRE2_ERROR_NOMATCH) {
            if(retryEmpty && cursor<request.text.size()){retryEmpty=false;cursor=nextCharacter(request.text,cursor);continue;}
            retryEmpty=false;
            if(request.mode==RegexMode::Next && !wrapped && request.origin>0){wrapped=true;cursor=0;continue;}
            break;
        }
        if(found<0) {if(control->canceled.load())result.canceled=true;else result.error=QStringLiteral("正则搜索达到复杂度或时间限制，请简化表达式或缩小选区。");break;}
        const auto* vector=pcre2_get_ovector_pointer(data.get());const SearchMatch match{qint64(vector[0]),qint64(vector[1])};
        if(first.first<0)first=match;last=match;
        if(match.first<request.origin || (request.includeOrigin && match.first==request.origin))previous=match;
        if(next.first<0 && (match.first>request.origin || (request.includeOrigin && match.first==request.origin)))next=match;
        if(request.mode==RegexMode::Collect) {if(result.matches.size()==10000){result.capped=true;break;}result.matches.append(match);}
        else if(request.mode==RegexMode::Next) {
            if(wrapped || match.first>request.origin || request.includeOrigin){result.target=match;break;}
        }
        cursor=match.second;retryEmpty=match.first==match.second;
    }
    if(request.mode==RegexMode::Collect)result.target=next.first>=0?next:first;
    else if(request.mode==RegexMode::Previous)result.target=previous.first>=0?previous:last;
    return result;
}
