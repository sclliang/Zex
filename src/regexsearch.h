#pragma once
#include <QByteArray>
#include <QPair>
#include <QString>
#include <QVector>
#include <atomic>
#include <memory>

using SearchMatch=QPair<qint64,qint64>;
struct RegexControl { std::atomic<bool> canceled{false}; };
using RegexControlPtr=std::shared_ptr<RegexControl>;
enum class RegexMode { Collect, Next, Previous };
struct RegexRequest {
    QByteArray text,pattern;
    bool caseSensitive=false,wholeWord=false;
    RegexMode mode=RegexMode::Collect;
    qint64 origin=0;
    bool includeOrigin=true;
    int timeoutMs=3000;
};
struct RegexResult {
    QVector<SearchMatch> matches;
    QString error;
    bool capped=false,canceled=false;
    SearchMatch target{-1,-1};
};
RegexResult searchRegex(const RegexRequest& request,const RegexControlPtr& control);
