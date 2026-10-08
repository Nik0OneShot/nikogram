#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>

// An ephemeral, same-session UI preference. No gameplay configuration or remote
// memory is changed. Process birth time prevents PID reuse from reusing a choice.
namespace MenuStartup
{
    inline int Override = -1;
    struct Message { std::uint32_t magic = 0x4E4B4D53, version = 1; int style = -1; };
    inline std::wstring Name(DWORD pid, std::uint64_t birth)
    { return L"Local\\Nikogram.MenuStartup." + std::to_wstring(pid) + L"." + std::to_wstring(birth); }
    class Request
    {
        HANDLE mapping = nullptr, read = nullptr;
        bool prepared = false;
    public:
        Request(DWORD pid, std::uint64_t birth, int style)
        {
            if (style == -1) return;
            if (style < 0 || style > 1) return;
            const auto name = Name(pid, birth);
            mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Message), name.c_str());
            if (!mapping) return;
            if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mapping); mapping = nullptr; return; }
            read = CreateEventW(nullptr, TRUE, FALSE, (name + L".Read").c_str());
            if (!read || GetLastError() == ERROR_ALREADY_EXISTS) return;
            auto* data = static_cast<Message*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(Message)));
            if (!data) { CloseHandle(read); read = nullptr; return; }
            *data = Message{0x4E4B4D53, 1, style};
            UnmapViewOfFile(data);
            prepared = true;
        }
        ~Request() { if (read) CloseHandle(read); if (mapping) CloseHandle(mapping); }
        Request(const Request&) = delete;
        Request& operator=(const Request&) = delete;
        bool Ready() const { return prepared; }
        bool Wait() const { return read && WaitForSingleObject(read, 10000) == WAIT_OBJECT_0; }
    };
    inline void Capture()
    {
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return;
        const auto birth = (std::uint64_t(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
        const auto name = Name(GetCurrentProcessId(), birth);
        HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
        if (!mapping) return;
        if (const auto* data = static_cast<const Message*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(Message))))
        {
            if (data->magic == 0x4E4B4D53 && data->version == 1 && data->style >= 0 && data->style <= 1)
                Override = data->style;
            UnmapViewOfFile(data);
        }
        CloseHandle(mapping);
        if (Override >= 0)
            if (HANDLE read = OpenEventW(EVENT_MODIFY_STATE, FALSE, (name + L".Read").c_str()))
            { SetEvent(read); CloseHandle(read); }
    }
}
