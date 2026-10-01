#define NOMINMAX
#include "../../Nikogram/src/Utils/ExceptionHandler/CrashLog.h"
#include <filesystem>
#include <fstream>
#include <cassert>
#include <string>
#include <iostream>

int main()
{
    const auto root = std::filesystem::temp_directory_path() /
        (L"nikogram-crash-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    assert(std::filesystem::create_directory(root));
    assert(!CrashLog::Open(L"relative.exe"));
    assert(CrashLog::Open((root / L"tf_win64.exe").c_str()));
    assert(std::filesystem::path(CrashLog::Path) == root / L"Nikogram" / L"crash_log.txt");
    EXCEPTION_RECORD record = {};
    CONTEXT context = {};
    EXCEPTION_POINTERS info = { &record, &context };
    record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    record.NumberParameters = 2;
    record.ExceptionInformation[0] = 8;
    record.ExceptionInformation[1] = 0x12345678;
    context.Rip = 0x11223344;
    assert(CrashLog::Basic(&info, nullptr, "isolated-test"));
    record.ExceptionInformation[0] = 0;
    assert(CrashLog::Basic(&info, nullptr, "second-record"));
    CrashLog::Close();
    assert(!CrashLog::Write("closed", 6));
    assert(CrashLog::Open((root / L"tf_win64.exe").c_str()));
    assert(CrashLog::Write("append-check", 12));
    CrashLog::Close();
    const auto path = root / L"Nikogram" / L"crash_log.txt";
    std::ifstream stream(path);
    const std::string contents((std::istreambuf_iterator<char>(stream)), {});
    stream.close();
    assert(contents.find("Access: EXECUTE Target: 0x12345678") != std::string::npos);
    assert(contents.find("Access: READ") != std::string::npos);
    assert(contents.find("RIP: 0x11223344") != std::string::npos);
    assert(contents.find("isolated-test") != std::string::npos);
    assert(contents.find("second-record") != std::string::npos);
    assert(contents.find("append-check") != std::string::npos);
    assert(std::string(CrashLog::Access(1)) == "WRITE");
    std::filesystem::remove(path);
    std::filesystem::remove(root / L"Nikogram");
    std::filesystem::remove(root);
    std::cout << "CrashLog: executable-relative path, directory creation, exception fields, append, reopen and failure checks passed\n";
}
