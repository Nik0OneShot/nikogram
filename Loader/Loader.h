#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <span>
#include <filesystem>
#include <cstdint>

namespace loader {
struct Target {
 DWORD pid=0; uint64_t born=0;
 bool ready=false,legacyAttempt=false;
 std::wstring status=L"waiting for tf2";
};
struct Result { bool success=false; std::wstring message; };
std::span<const unsigned char> Resource(int id);
std::wstring Hash(std::span<const unsigned char> bytes);
bool VerifyPayloads();
Target Detect();
Result Inject(Target expected, bool confirmLegacyNativeUnload=false);
void Export(int resource, const std::filesystem::path& path, bool overwrite);
std::wstring Error(DWORD error);
}
