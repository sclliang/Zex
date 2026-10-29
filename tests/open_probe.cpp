#include <windows.h>
#include <shellapi.h>
// Test-only native executable: records the single path supplied by a Shell verb.
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    int count=0;auto** args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!args || count!=3 || lstrcmpW(args[1],L"--")!=0)return 1;
    wchar_t report[32768]{};if(!GetEnvironmentVariableW(L"ZEX_TEST_OPEN_REPORT",report,32768)){LocalFree(args);return 2;}
    const auto file=CreateFileW(report,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){LocalFree(args);return 3;}
    DWORD written=0;const DWORD length=lstrlenW(args[2])*sizeof(wchar_t);
    const auto ok=WriteFile(file,args[2],length,&written,nullptr);CloseHandle(file);LocalFree(args);
    return ok && written==length?0:4;
}
