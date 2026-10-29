#include "filereceiver.h"
#include <QDir>

FileReceiver::~FileReceiver() { close(); }
void FileReceiver::close() { if(window_){DestroyWindow(window_);window_=nullptr;} }
bool FileReceiver::listen(const wchar_t* name) {
    WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=receiverClass;
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    window_=CreateWindowExW(0,receiverClass,name,0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,this);
    return window_!=nullptr;
}
bool FileReceiver::decode(const COPYDATASTRUCT& data,QStringList& paths) {
    paths.clear();
    if(data.dwData!=openMessageId || !data.lpData || data.cbData<4 || data.cbData>maximumOpenBytes || data.cbData%2)return false;
    // Copy before returning to the sender. QString also avoids unaligned wchar_t reads.
    const auto bytes=QByteArray(static_cast<const char*>(data.lpData),data.cbData);
    const auto text=QString::fromUtf16(reinterpret_cast<const char16_t*>(bytes.constData()),bytes.size()/2);
    if(!text.endsWith(QString(2,QChar(0))))return false;
    if(text.size()==2)return true;
    const auto items=text.left(text.size()-2).split(QChar(0));
    if(items.size()>4096)return false;
    for(const auto& path:items) {
        if(path.isEmpty() || path.size()>32767 || !QDir::isAbsolutePath(path))return false;
        paths.append(QDir::cleanPath(QDir::fromNativeSeparators(path)));
    }
    return true;
}
LRESULT CALLBACK FileReceiver::windowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_NCCREATE)SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self=reinterpret_cast<FileReceiver*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_COPYDATA && self && lp) {
        QStringList paths;if(!decode(*reinterpret_cast<COPYDATASTRUCT*>(lp),paths))return FALSE;
        QMetaObject::invokeMethod(self,[self,paths]{emit self->filesRequested(paths);},Qt::QueuedConnection);
        return TRUE;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
