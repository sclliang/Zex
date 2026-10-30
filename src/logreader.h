#pragma once
#include "documentloader.h"
#include <memory>

struct LogUpdate {
    PreparedFile replacement;
    QByteArray appended;
    FileContent content;
    QString notice;
    bool reset = false;
    bool waiting = false;
    bool failed = false;
    bool more = false;
};

// A reader is owned by one follow generation and used by at most one worker at a time.
// It holds decoder state and small boundary samples, not an open file or the whole log.
class LogReader {
public:
    explicit LogReader(QString encoding);
    ~LogReader();
    LogUpdate poll(const QString& path,const LoadControlPtr& control);
private:
    struct State;
    std::unique_ptr<State> state_;
};
