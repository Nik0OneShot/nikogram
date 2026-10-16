#include "Memory.h"

#include <format>
#include <Psapi.h>
#include <cstring>

std::vector<byte> CMemory::PatternToByte(const char* szPattern)
{
	std::vector<byte> vPattern = {};

	const auto pStart = const_cast<char*>(szPattern);
	const auto pEnd = const_cast<char*>(szPattern) + strlen(szPattern);
	for (char* pCurrent = pStart; pCurrent < pEnd; ++pCurrent)
		vPattern.push_back(byte(std::strtoul(pCurrent, &pCurrent, 16)));

	return vPattern;
}

std::vector<int> CMemory::PatternToInt(const char* szPattern)
{
	std::vector<int> vPattern = {};

	const auto pStart = const_cast<char*>(szPattern);
	const auto pEnd = const_cast<char*>(szPattern) + strlen(szPattern);
	for (char* pCurrent = pStart; pCurrent < pEnd; ++pCurrent)
	{
		if (*pCurrent == '?') // Is current byte a wildcard? Simply ignore that that byte later
		{
			++pCurrent;
			if (*pCurrent == '?') // Check if following byte is also a wildcard
				++pCurrent;

			vPattern.push_back(-1);
		}
		else
			vPattern.push_back(std::strtoul(pCurrent, &pCurrent, 16));
	}

	return vPattern;
}

uintptr_t CMemory::FindSignature(const char* szModule, const char* szPattern)
{
	if (const auto hModule = GetModuleHandle(szModule))
	{
		// Get module information to search in the given module
		MODULEINFO lpModuleInfo;
		if (!GetModuleInformation(GetCurrentProcess(), hModule, &lpModuleInfo, sizeof(MODULEINFO)))
			return 0x0;

		// The region where we will search for the byte sequence
		const auto dwImageSize = lpModuleInfo.SizeOfImage;

		// Check if the image is faulty
		if (!dwImageSize)
			return 0x0;

		// Convert IDA-Style signature to a byte sequence
		const auto vPattern = PatternToInt(szPattern);
		const auto iPatternSize = vPattern.size();
		const int* iPatternBytes = vPattern.data();

		const auto pImageBytes = reinterpret_cast<byte*>(hModule);

		// Now loop through all bytes and check if the byte sequence matches
		if (!iPatternSize || iPatternSize > dwImageSize)
			return 0x0;

		for (auto i = 0ul; i <= dwImageSize - iPatternSize; ++i)
		{
			auto bFound = true;

			// Go through all bytes from the signature and check if it matches
			for (auto j = 0ul; j < iPatternSize; ++j)
			{
				if (pImageBytes[i + j] != iPatternBytes[j] // Bytes don't match
					&& iPatternBytes[j] != -1)             // Byte isn't a wildcard either
				{
					bFound = false;
					break;
				}
			}

			if (bFound)
				return uintptr_t(&pImageBytes[i]);
		}

		return 0x0;
	}

	return 0x0;
}

uintptr_t CMemory::FindSignatureWithReference(const char* szModule, const char* szPattern, int iReferenceOffset, const char* szReference)
{
	if (!szReference || iReferenceOffset < 0)
		return 0x0;
	const auto hModule = GetModuleHandle(szModule);
	if (!hModule)
		return 0x0;
	MODULEINFO lpModuleInfo;
	if (!GetModuleInformation(GetCurrentProcess(), hModule, &lpModuleInfo, sizeof(MODULEINFO)) || !lpModuleInfo.SizeOfImage)
		return 0x0;

	const auto vPattern = PatternToInt(szPattern);
	const size_t iPatternSize = vPattern.size();
	const size_t dwImageSize = lpModuleInfo.SizeOfImage;
	const size_t iReferenceLength = strlen(szReference) + 1; // include the terminator: exact string only
	if (!iPatternSize || iPatternSize > dwImageSize || size_t(iReferenceOffset) + 4 > iPatternSize)
		return 0x0;

	const auto pImageBytes = reinterpret_cast<const byte*>(hModule);
	for (size_t i = 0; i <= dwImageSize - iPatternSize; ++i)
	{
		bool bFound = true;
		for (size_t j = 0; j < iPatternSize; ++j)
		{
			if (vPattern[j] != -1 && pImageBytes[i + j] != vPattern[j])
			{
				bFound = false;
				break;
			}
		}
		if (!bFound)
			continue;

		// Resolve the operand and check it stays inside this module before reading.
		int32_t iDisplacement;
		memcpy(&iDisplacement, pImageBytes + i + iReferenceOffset, sizeof(iDisplacement));
		const int64_t iTarget = int64_t(i) + iReferenceOffset + 4 + iDisplacement;
		if (iTarget < 0 || uint64_t(iTarget) + iReferenceLength > dwImageSize)
			continue;
		if (!memcmp(pImageBytes + iTarget, szReference, iReferenceLength))
			return uintptr_t(pImageBytes + i);
	}
	return 0x0;
}

using CreateInterfaceFn = void*(*)(const char* pName, int* pReturnCode);

PVOID CMemory::FindInterface(const char* szModule, const char* szObject)
{
	const auto CreateInterface = GetModuleExport<CreateInterfaceFn>(szModule, "CreateInterface");
	if (!CreateInterface) // module not loaded or no CreateInterface export
		return nullptr;
	return CreateInterface(szObject, nullptr);
}

std::string CMemory::GetModuleOffset(uintptr_t uAddress)
{
	HMODULE hModule;
	if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, LPCSTR(uAddress), &hModule))
		return std::format("{:#x}", uAddress);

	uintptr_t uBase = uintptr_t(hModule);
	if (char buffer[MAX_PATH]; GetModuleBaseName(GetCurrentProcess(), hModule, buffer, sizeof(buffer) / sizeof(char)))
		return std::format("{}+{:#x}", buffer, uAddress - uBase);

	return std::format("{:#x}+{:#x}", uBase, uAddress - uBase);
}

uintptr_t CMemory::GetOffsetFromBase(uintptr_t uAddress)
{
	HMODULE hModule;
	if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, LPCSTR(uAddress), &hModule))
		return -1;

	uintptr_t uBase = uintptr_t(hModule);
	return uAddress - uBase;
}