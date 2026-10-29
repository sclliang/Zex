#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "instance.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const auto launchTick=GetTickCount64();
    std::wstring path(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!n || n >= path.size()) return 1;
    path.resize(n);
    int argc = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wide) return 3;
    std::vector<std::wstring> wideArguments, files;
    for(int i=0;i<argc;++i)wideArguments.emplace_back(wide[i]);
    LocalFree(wide);
    bool diagnostic=false,verbatim=false;std::wstring testChannel;
    for(int i=1;i<argc;++i) {
        const auto& value=wideArguments[i];
        if(!verbatim && value==L"--") { verbatim=true;continue; }
        if(!verbatim && (value.rfind(L"--verify-",0)==0 || value==L"--capture" || value==L"--capture-appearance" || value==L"--find")) {
            if(value!=L"--find")diagnostic=true;
            if(i+1<argc) { if(value==L"--verify-instance")testChannel=wideArguments[i+1];++i; }
        } else files.push_back(value);
    }
    if(testChannel.size()>64 || testChannel.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::wstring::npos)return 4;
    InstanceGate gate;const wchar_t* receiverName=nullptr;
    if(!diagnostic || !testChannel.empty()) {
        const auto result=gate.enter(files,testChannel);
        if(result==InstanceGate::Forwarded)return 0;
        if(result==InstanceGate::Failed) { MessageBoxW(nullptr,gate.error.c_str(),L"Zex",MB_OK|MB_ICONERROR);return 4; }
        receiverName=gate.name();
    }
    const auto bin = path.substr(0, path.find_last_of(L"\\/")) + L"\\bin";
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
    AddDllDirectory(bin.c_str());
    const auto dllPath = bin + L"\\zex_app.dll";
    HMODULE library = LoadLibraryExW(dllPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!library) {
        const auto message = L"Zex 无法加载运行组件。请完整解压发布包，保留 EXE 旁的 bin 文件夹。\n\n错误码：" + std::to_wstring(GetLastError());
        MessageBoxW(nullptr, message.c_str(), L"Zex", MB_OK | MB_ICONERROR);
        return 1;
    }
    auto run = reinterpret_cast<int (*)(int, char**, unsigned long long,const wchar_t*)>(GetProcAddress(library, "runZex"));
    if (!run) return 2;
    std::vector<std::string> arguments;
    for (int i = 0; i < argc; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, wideArguments[i].c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string value(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wideArguments[i].c_str(), -1, value.data(), size, nullptr, nullptr);
        arguments.push_back(std::move(value));
    }
    std::vector<char*> argv;
    for (auto& value : arguments) argv.push_back(value.data());
    argv.push_back(nullptr);
    return run(argc, argv.data(), launchTick,receiverName);
}
