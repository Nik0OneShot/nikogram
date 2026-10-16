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
    // Diagnostic reads never dereference a game pointer or call an engine method.
    // Byte counts are fixed; inaccessible pages are reported as unavailable.
    inline bool ReadBytes(unsigned long long address, void* out, size_t size)
    {
        if(!address || address > ~0ull-size)return false;
        SIZE_T read=0;
        return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),out,size,&read)&&read==size;
    }
    inline void MemoryWindow(const char* label,unsigned long long address,size_t size)
    {
        unsigned char bytes[384]{};char record[256];
        if(size>sizeof(bytes))size=sizeof(bytes);
        const bool available=ReadBytes(address,bytes,size);
        auto length=_snprintf_s(record,sizeof(record),_TRUNCATE,
            "Memory %s address=0x%llx bytes=%llu status=%s\r\n",label,address,
            static_cast<unsigned long long>(size),available?"read":"unavailable");
        if(length>0)Write(record,DWORD(length));
        if(!available)return;
        for(size_t offset=0;offset<size;offset+=32)
        {
            int used=_snprintf_s(record,sizeof(record),_TRUNCATE,"  +%03llx:",static_cast<unsigned long long>(offset));
            for(size_t i=offset;i<size&&i<offset+32;++i)
                used+=_snprintf_s(record+used,sizeof(record)-used,_TRUNCATE," %02x",bytes[i]);
            record[used++]='\r';record[used++]='\n';Write(record,DWORD(used));
        }
    }
    // Layout is valid only for the exact client build and fault instructions
    // independently verified against the two preserved dumps/disassemblies.
    // Reader and sink are injectable for isolated, non-game regression tests.
    template<class Reader,class Sink>
    inline bool ShadowMemory(unsigned long long base,const CONTEXT& c,DWORD code,Reader read,Sink emit)
    {
        if(!base || base>~0ull-0x1338000)return false;
        IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
        if(!read(base,&dos,sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>0x1000
            ||!read(base+dos.e_lfanew,&nt,sizeof(nt))||nt.Signature!=IMAGE_NT_SIGNATURE
            ||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.FileHeader.TimeDateStamp!=0x6ac7e56d
            ||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||nt.OptionalHeader.SizeOfImage!=0x1338000)return false;
        const bool divide=code==EXCEPTION_INT_DIVIDE_BY_ZERO&&c.Rip==base+0x24ad8e;
        const bool nullBounds=code==EXCEPTION_ACCESS_VIOLATION&&c.Rip==base+0x2477fd&&c.Rdx==0;
        const unsigned char divCode[]{0xf7,0xf9,0x48,0x8b,0xcb,0x44,0x8b,0xe8};
        const unsigned char nullCode[]{0x48,0x8b,0x02,0x48,0x8b,0xda,0xff,0x50,0x38};
        unsigned char found[sizeof(nullCode)]{};
        const auto expected=divide?divCode:nullCode;
        const auto length=divide?sizeof(divCode):sizeof(nullCode);
        if((!divide&&!nullBounds)||!read(c.Rip,found,length)||memcmp(found,expected,length))return false;
        const auto manager=base+0x10e2e80;
        if((divide&&c.Rbp!=manager)||(nullBounds&&c.Rdi!=manager))return false;
        emit("shadow.manager",manager,0x140);
        unsigned long long record=0;
        if(divide)record=c.Rsi;
        else
        {
            unsigned long long array=0;
            if(c.R12>=0xffff||!read(manager+0x38,&array,sizeof(array))||array!=c.R15||array>~0ull-c.R12*0x88)return true;
            record=array+c.R12*0x88;
        }
        emit("shadow.record",record,0x88);
        if(!divide)return true;
        unsigned short texture=0,fragment=0,block=0;
        unsigned long long textures=0,fragments=0;
        if(record>~0ull-0x6c||!read(record+0x6c,&texture,sizeof(texture))||texture==0xffff
            ||!read(manager+0x68,&textures,sizeof(textures))||!textures||textures>~0ull-texture*10ull)return true;
        const auto textureRecord=textures+texture*10ull;
        emit("shadow.texture",textureRecord,10);
        if(!read(textureRecord,&fragment,sizeof(fragment))||fragment==0xffff
            ||!read(manager+0x90,&fragments,sizeof(fragments))||!fragments||fragments>~0ull-fragment*16ull)return true;
        const auto fragmentRecord=fragments+fragment*16ull;
        emit("shadow.fragment",fragmentRecord,16);
        if(!read(fragmentRecord,&block,sizeof(block)))return true;
        emit("shadow.fragment_power",manager+0xf2+block*2ull,1);
        return true;
    }
    inline bool TakeMemoryBudget(std::atomic<unsigned>& count)
    {
        unsigned value=count.load(std::memory_order_relaxed);
        while(value<8)if(count.compare_exchange_weak(value,value+1,std::memory_order_relaxed))return true;
        return false;
    }
    inline void DiagnosticMemory(const CONTEXT& c,DWORD code)
    {
        // Bound extra output over this DLL lifetime, even if a first-chance
        // exception is repeatedly handled by its owner.
        static std::atomic<unsigned> genericCount{0},shadowCount{0};
        MEMORY_BASIC_INFORMATION region{};
        if(shadowCount.load(std::memory_order_relaxed)<8&&VirtualQuery(reinterpret_cast<void*>(c.Rip),&region,sizeof(region))&&region.Type==MEM_IMAGE)
        {
            const auto base=reinterpret_cast<unsigned long long>(region.AllocationBase);
            bool reserved=false,allowed=false;
            const auto sink=[&](const char* label,unsigned long long address,size_t size)
            {
                if(!reserved){reserved=true;allowed=TakeMemoryBudget(shadowCount);}
                if(allowed)MemoryWindow(label,address,size);
            };
            ShadowMemory(base,c,code,ReadBytes,sink);
        }
        if(!TakeMemoryBudget(genericCount))return;
        MemoryWindow("RBP",c.Rbp,128);MemoryWindow("RSI",c.Rsi,128);
        MemoryWindow("RDI",c.Rdi,128);MemoryWindow("RBX",c.Rbx,128);
        MemoryWindow("R15",c.R15,128);MemoryWindow("stack.extended",c.Rsp,384);
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
            "RSI: 0x%llx RDI: 0x%llx\r\n"
            "R8: 0x%llx R9: 0x%llx R10: 0x%llx R11: 0x%llx\r\n"
            "R12: 0x%llx R13: 0x%llx R14: 0x%llx R15: 0x%llx\r\nLifecycle stage: %s Stage TID: %lu\r\n",
            time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,
            build,GetCurrentProcessId(),GetCurrentThreadId(),e.ExceptionCode,e.ExceptionFlags,
            e.ExceptionAddress,module,access ? Access(e.ExceptionInformation[0]) : "N/A",
            access ? static_cast<unsigned long long>(e.ExceptionInformation[1]) : 0ull,e.NumberParameters,
            c.Rip,c.Rsp,c.Rbp,c.Rax,c.Rbx,c.Rcx,c.Rdx,c.Rsi,c.Rdi,
            c.R8,c.R9,c.R10,c.R11,c.R12,c.R13,c.R14,c.R15,
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
        DiagnosticMemory(c,e.ExceptionCode);
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
