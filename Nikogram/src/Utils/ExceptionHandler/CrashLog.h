#pragma once
#include <Windows.h>
#include <cstdio>
#include <cstring>

namespace CrashLog
{
    inline HANDLE File = INVALID_HANDLE_VALUE;
    inline wchar_t Path[32768] = {};

    inline bool Open(const wchar_t* executable)
    {
        if (File != INVALID_HANDLE_VALUE) return true;
        if (!executable || wcslen(executable) + 32 >= _countof(Path)) return false;
        wcscpy_s(Path, executable);
        auto slash = wcsrchr(Path, L'\\');
        if (!slash) return false;
        slash[1] = 0;
        wcscat_s(Path, L"Nikogram");
        if (!CreateDirectoryW(Path, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
        wcscat_s(Path, L"\\crash_log.txt");
        File = CreateFileW(Path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        return File != INVALID_HANDLE_VALUE;
    }
    inline bool Write(const char* data, DWORD size)
    {
        if (File == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        return WriteFile(File, data, size, &written, nullptr) && written == size && FlushFileBuffers(File);
    }
    inline const char* Access(ULONG_PTR kind)
    {
        return kind == 0 ? "READ" : kind == 1 ? "WRITE" : kind == 8 ? "EXECUTE" : "UNKNOWN";
    }
    // No streams, heap allocations, symbol lookup, game interfaces or debug output.
    inline bool Basic(PEXCEPTION_POINTERS info, const void* module, const char* build)
    {
        char record[2048];
        SYSTEMTIME time; GetLocalTime(&time);
        const auto& e = *info->ExceptionRecord;
        const auto& c = *info->ContextRecord;
        const bool access = (e.ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
            e.ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && e.NumberParameters >= 2;
        const int size = _snprintf_s(record, sizeof(record), _TRUNCATE,
            "\r\n=== Observed exception (first-chance; may be handled) ===\r\n"
            "Time: %04u-%02u-%02u %02u:%02u:%02u.%03u local\r\n"
            "Build: %s\r\nPID: %lu TID: %lu\r\nCode: 0x%08lX Flags: 0x%08lX\r\n"
            "Exception address: %p Module base: %p\r\nAccess: %s Target: 0x%llx Parameters: %lu\r\n"
            "RIP: 0x%llx RSP: 0x%llx RBP: 0x%llx\r\n"
            "RAX: 0x%llx RBX: 0x%llx RCX: 0x%llx RDX: 0x%llx\r\n"
            "RSI: 0x%llx RDI: 0x%llx\r\n",
            time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,
            build,GetCurrentProcessId(),GetCurrentThreadId(),e.ExceptionCode,e.ExceptionFlags,
            e.ExceptionAddress,module,access ? Access(e.ExceptionInformation[0]) : "N/A",
            access ? static_cast<unsigned long long>(e.ExceptionInformation[1]) : 0ull,e.NumberParameters,
            c.Rip,c.Rsp,c.Rbp,c.Rax,c.Rbx,c.Rcx,c.Rdx,c.Rsi,c.Rdi);
        return size > 0 && Write(record, DWORD(size));
    }
    inline void Close()
    {
        if (File != INVALID_HANDLE_VALUE) CloseHandle(File);
        File = INVALID_HANDLE_VALUE;
    }
}
