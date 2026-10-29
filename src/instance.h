#pragma once
#include <windows.h>
#include <string>
#include <vector>

inline constexpr wchar_t receiverClass[] = L"SixunitedZexOpenReceiverV1";
inline constexpr ULONG_PTR openMessageId = 0x5a455831;
inline constexpr DWORD maximumOpenBytes = 256 * 1024;

// Runs before Qt is loaded; the mutex stays owned until the primary process exits.
class InstanceGate {
public:
    enum Result { Primary, Forwarded, Failed };
    ~InstanceGate();
    Result enter(const std::vector<std::wstring>& paths, const std::wstring& testChannel = {});
    const wchar_t* name() const { return name_.c_str(); }
    std::wstring error;
private:
    HANDLE mutex_ = nullptr;
    bool owned_ = false;
    std::wstring name_;
};
