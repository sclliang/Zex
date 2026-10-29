#include "instance.h"
#include <sddl.h>

InstanceGate::~InstanceGate() {
    if (owned_) ReleaseMutex(mutex_);
    if (mutex_) CloseHandle(mutex_);
}
InstanceGate::Result InstanceGate::enter(const std::vector<std::wstring>& paths, const std::wstring& testChannel) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) { error=L"无法读取当前用户信息。"; return Failed; }
    DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);
    std::vector<BYTE> buffer(size);
    const bool gotUser=GetTokenInformation(token,TokenUser,buffer.data(),size,&size);
    CloseHandle(token);
    LPWSTR sid=nullptr;
    if (!gotUser || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,&sid)) {
        error=L"无法确定单实例通信范围。";return Failed;
    }
    name_=L"sixunited.Zex."+std::wstring(sid);LocalFree(sid);
    if(!testChannel.empty())name_+=L".Test."+testChannel;
    mutex_=CreateMutexW(nullptr,TRUE,(L"Local\\"+name_).c_str());
    const auto status=GetLastError();
    if(!mutex_) { error=L"无法创建单实例锁。";return Failed; }
    if(status!=ERROR_ALREADY_EXISTS) { owned_=true;return Primary; }
    std::vector<wchar_t> packet;
    for(const auto& path:paths) {
        std::vector<wchar_t> absolute(32768);
        const auto length=GetFullPathNameW(path.c_str(),DWORD(absolute.size()),absolute.data(),nullptr);
        if(!length || length>=absolute.size()) { error=L"文件路径过长或无效。";return Failed; }
        packet.insert(packet.end(),absolute.begin(),absolute.begin()+length);packet.push_back(0);
    }
    packet.push_back(0);if(paths.empty())packet.push_back(0);
    if(packet.size()*sizeof(wchar_t)>maximumOpenBytes) { error=L"一次打开的文件路径过多。";return Failed; }
    const auto until=GetTickCount64()+10000;
    do {
        const auto wait=WaitForSingleObject(mutex_,0);
        if(wait==WAIT_OBJECT_0 || wait==WAIT_ABANDONED) { owned_=true;return Primary; }
        if(wait==WAIT_FAILED)break;
        if(const auto receiver=FindWindowExW(HWND_MESSAGE,nullptr,receiverClass,name_.c_str())) {
            DWORD pid=0;GetWindowThreadProcessId(receiver,&pid);AllowSetForegroundWindow(pid);
            COPYDATASTRUCT data{openMessageId,DWORD(packet.size()*sizeof(wchar_t)),packet.data()};
            DWORD_PTR accepted=0;
            if(SendMessageTimeoutW(receiver,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&data),
                SMTO_ABORTIFHUNG|SMTO_BLOCK|SMTO_ERRORONEXIT,3000,&accepted) && accepted)return Forwarded;
            // A receiver that exited can be replaced; a live but busy one must not create a duplicate window.
            if(IsWindow(receiver)) { error=L"已有 Zex 窗口暂时无法接收文件，请稍后重试。请使用相同的用户权限运行 Zex。";return Failed; }
        }
        const auto waitReady=WaitForSingleObject(mutex_,40);
        if(waitReady==WAIT_OBJECT_0 || waitReady==WAIT_ABANDONED) { owned_=true;return Primary; }
    } while(GetTickCount64()<until);
    error=L"已有 Zex 窗口仍在启动或没有响应，请稍后重试。";return Failed;
}
