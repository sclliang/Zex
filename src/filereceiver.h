#pragma once
#include "instance.h"
#include <QObject>
#include <QStringList>

class FileReceiver : public QObject {
    Q_OBJECT
public:
    explicit FileReceiver(QObject* parent=nullptr) : QObject(parent) {}
    ~FileReceiver();
    bool listen(const wchar_t* name);
    void close();
    static bool decode(const COPYDATASTRUCT& data, QStringList& paths);
signals:
    void filesRequested(const QStringList& paths);
private:
    static LRESULT CALLBACK windowProc(HWND,UINT,WPARAM,LPARAM);
    HWND window_=nullptr;
};
