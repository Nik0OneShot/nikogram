#pragma once
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <atomic>

namespace CrashLog
{
    inline HANDLE File = INVALID_HANDLE_VALUE;
    inline wchar_t Path[32768] = {};
    inline HANDLE LifecycleFile = INVALID_HANDLE_VALUE;
    inline std::atomic<const char*> CurrentStage{"before_initialization"};
    inline std::atomic<DWORD> StageThread{0};

    // Stage names are static literals: the crash path never follows a string
    // owned by an allocation that could already have been corrupted.
    inline void Stage(const char* stage)
    {
        StageThread.store(GetCurrentThreadId(),std::memory_order_relaxed);
        CurrentStage.store(stage,std::memory_order_release);
        if(LifecycleFile==INVALID_HANDLE_VALUE)return;
        char record[256];SYSTEMTIME time;GetLocalTime(&time);
        const auto size=_snprintf_s(record,sizeof(record),_TRUNCATE,
            "%04u-%02u-%02u %02u:%02u:%02u.%03u PID=%lu TID=%lu stage=%s\r\n",
            time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,
            GetCurrentProcessId(),GetCurrentThreadId(),stage);
        DWORD written=0;if(size>0){WriteFile(LifecycleFile,record,DWORD(size),&written,nullptr);FlushFileBuffers(LifecycleFile);}
    }

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
        wchar_t lifecycle[32768];wcscpy_s(lifecycle,Path);
        auto filename=wcsrchr(lifecycle,L'\\');if(filename)wcscpy_s(filename+1,30,L"lifecycle_log.txt");
        LifecycleFile=CreateFileW(lifecycle,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
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
            "RSI: 0x%llx RDI: 0x%llx\r\nLifecycle stage: %s Stage TID: %lu\r\n",
            time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,
            build,GetCurrentProcessId(),GetCurrentThreadId(),e.ExceptionCode,e.ExceptionFlags,
            e.ExceptionAddress,module,access ? Access(e.ExceptionInformation[0]) : "N/A",
            access ? static_cast<unsigned long long>(e.ExceptionInformation[1]) : 0ull,e.NumberParameters,
            c.Rip,c.Rsp,c.Rbp,c.Rax,c.Rbx,c.Rcx,c.Rdx,c.Rsi,c.Rdi,
            CurrentStage.load(std::memory_order_acquire),StageThread.load(std::memory_order_relaxed));
        const bool saved=size>0&&Write(record,DWORD(size));
        // Record exception payload and a bounded raw stack snapshot even for
        // heap corruption. These are candidates, not an unwound call stack.
        // No DbgHelp, streams, containers, engine interfaces or symbol loading.
        for(DWORD i=0;i<e.NumberParameters&&i<EXCEPTION_MAXIMUM_PARAMETERS;++i)
        {
            const auto length=_snprintf_s(record,sizeof(record),_TRUNCATE,"Exception parameter[%lu]: 0x%llx\r\n",i,static_cast<unsigned long long>(e.ExceptionInformation[i]));
            if(length>0)Write(record,DWORD(length));
        }
        unsigned long long words[32]{};SIZE_T bytes=0;
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(c.Rsp),words,sizeof(words),&bytes);
        for(SIZE_T i=0;i<bytes/sizeof(words[0]);++i)
        {
            MEMORY_BASIC_INFORMATION region{};char moduleName[MAX_PATH]{};
            const bool image=VirtualQuery(reinterpret_cast<void*>(words[i]),&region,sizeof(region))&&region.Type==MEM_IMAGE;
            if(image)GetModuleFileNameA(static_cast<HMODULE>(region.AllocationBase),moduleName,_countof(moduleName));
            const auto filename=strrchr(moduleName,'\\');
            const auto length=_snprintf_s(record,sizeof(record),_TRUNCATE,"Raw stack[%llu]: 0x%llx%s%s+0x%llx\r\n",
                static_cast<unsigned long long>(i),words[i],image?" image=":" ",image?(filename?filename+1:moduleName):"non-image",
                image?words[i]-reinterpret_cast<unsigned long long>(region.AllocationBase):0ull);
            if(length>0)Write(record,DWORD(length));
        }
        return saved;
    }
    inline void Close()
    {
        if (File != INVALID_HANDLE_VALUE) CloseHandle(File);
        File = INVALID_HANDLE_VALUE;
        if(LifecycleFile!=INVALID_HANDLE_VALUE)CloseHandle(LifecycleFile);
        LifecycleFile=INVALID_HANDLE_VALUE;
    }
}
