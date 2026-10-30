#include "editor.h"
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <algorithm>

namespace {constexpr int searchIndicator=8;}
CodeEditor::~CodeEditor(){if(regexControl_)regexControl_->canceled.store(true);}
void CodeEditor::cancelSearch() {
    if(regexControl_)regexControl_->canceled.store(true);regexControl_.reset();++searchGeneration_;
    searchTimer_.stop();counting_=false;navigating_=false;regexBusy_=false;stopped_=true;emit searchChanged();
}
void CodeEditor::copyRegexSnapshot(quint64 generation,qint64 offset) {
    if(generation!=searchGeneration_)return;
    const auto count=qMin<qint64>(512*1024,searchEnd_-searchStart_-offset);
    if(count) {
        Sci_TextRangeFull range{{searchStart_+offset,searchStart_+offset+count},regexSnapshot_.data()+offset};
        send(SCI_GETTEXTRANGEFULL,0,reinterpret_cast<sptr_t>(&range));offset+=count;
    }
    if(offset<searchEnd_-searchStart_){QTimer::singleShot(1,this,[this,generation,offset]{copyRegexSnapshot(generation,offset);});return;}
    regexSnapshot_.chop(1);runRegex(RegexMode::Collect);
}
void CodeEditor::runRegex(RegexMode mode,bool includeOrigin) {
    if(regexControl_)regexControl_->canceled.store(true);
    regexControl_=std::make_shared<RegexControl>();const auto control=regexControl_;
    const auto generation=++searchGeneration_;regexBusy_=true;stopped_=false;searchError_.clear();
    if(mode!=RegexMode::Collect){regexCaret_=send(SCI_GETCURRENTPOS);regexAnchor_=send(SCI_GETANCHOR);}
    auto origin=qBound<qint64>(0,qMin(regexCaret_,regexAnchor_)-searchStart_,regexSnapshot_.size());
    if(mode==RegexMode::Next && !includeOrigin) {
        origin=qBound<qint64>(0,qMax(regexCaret_,regexAnchor_)-searchStart_,regexSnapshot_.size());
        includeOrigin=regexCaret_!=regexAnchor_ || !lastRegexMatch_ || *lastRegexMatch_!=SearchMatch{regexCaret_,regexCaret_};
    }
    RegexRequest request{regexSnapshot_,query_,bool(searchFlags_&SCFIND_MATCHCASE),bool(searchFlags_&SCFIND_WHOLEWORD),mode,origin,includeOrigin};
    auto* future=new QFutureWatcher<RegexResult>(this);
    connect(future,&QFutureWatcher<RegexResult>::finished,this,[this,future,generation,mode,origin] {
        future->deleteLater();if(generation!=searchGeneration_)return;
        regexBusy_=false;const auto result=future->result();searchError_=result.error;
        if(result.canceled){stopped_=true;emit searchChanged();return;}
        if(mode==RegexMode::Collect) {
            matches_=result.matches;capped_=result.capped;send(SCI_SETINDICATORCURRENT,searchIndicator);
            for(auto& match:matches_){match.first+=searchStart_;match.second+=searchStart_;if(match.second>match.first)send(SCI_INDICATORFILLRANGE,match.first,match.second-match.first);}
        }
        if(result.error.isEmpty() && (mode!=RegexMode::Collect || regexAutoNavigate_)
            && send(SCI_GETCURRENTPOS)==regexCaret_ && send(SCI_GETANCHOR)==regexAnchor_) {
            if(mode==RegexMode::Collect && result.capped && !matches_.isEmpty() && origin>matches_.last().first-searchStart_){runRegex(RegexMode::Next,true);return;}
            if(result.target.first>=0)navigateToMatch({result.target.first+searchStart_,result.target.second+searchStart_});
        }
        emit searchChanged();
    });
    future->setFuture(QtConcurrent::run([request=std::move(request),control]{try{return searchRegex(request,control);}catch(...){RegexResult result;result.error=QStringLiteral("内存不足，无法完成搜索。");return result;}}));
    emit searchChanged();
}
bool CodeEditor::navigateToMatch(const SearchMatch& match) {
    if(match.first<0 || match.second>send(SCI_GETLENGTH))return false;
    if(regex_)lastRegexMatch_=match;
    send(SCI_ENSUREVISIBLEENFORCEPOLICY,send(SCI_LINEFROMPOSITION,match.first));send(SCI_SETSEL,match.first,match.second);send(SCI_SCROLLCARET);emit searchChanged();return true;
}
bool CodeEditor::selectResult(int index){return index>=0 && index<matches_.size() && navigateToMatch(matches_[index]);}
QString CodeEditor::resultText(int index) const {
    if(index<0 || index>=matches_.size())return {};
    const auto position=matches_[index].first,line=send(SCI_LINEFROMPOSITION,position),lineStart=send(SCI_POSITIONFROMLINE,line);
    auto start=qMax<sptr_t>(lineStart,position-60);while(start>lineStart && (send(SCI_GETCHARAT,start)&0xc0)==0x80)--start;
    auto end=qMin<sptr_t>(send(SCI_GETLINEENDPOSITION,line),start+300);while(end>start && (send(SCI_GETCHARAT,end)&0xc0)==0x80)--end;
    QByteArray text(qMax<sptr_t>(0,end-start)+1,Qt::Uninitialized);Sci_TextRangeFull range{{start,end},text.data()};send(SCI_GETTEXTRANGEFULL,0,reinterpret_cast<sptr_t>(&range));text.chop(1);
    return QStringLiteral("%1:%2    %3%4").arg(line+1).arg(send(SCI_GETCOLUMN,position)+1).arg(start>lineStart?QStringLiteral("…"):QString()).arg(QString::fromUtf8(text).replace('\t',QStringLiteral("  ")));
}
