#include "ExceptionHandler.h"

#include "../../Features/Configs/Configs.h"

#include <ImageHlp.h>
#include <Psapi.h>
#include <deque>
#include <sstream>
#include <fstream>
#include <format>
#include <mutex>
#pragma comment(lib, "imagehlp.lib")

#define STATUS_RUNTIME_ERROR             ((DWORD   )0xE06D7363L)
#define DBG_THREAD_NAMING                ((DWORD   )0x406D1388L)

struct Frame_t
{
	std::string m_sModule = "";
	uintptr_t m_uBase = 0;
	uintptr_t m_uAddress = 0;
	std::string m_sFile = "";
	unsigned int m_uLine = 0;
	std::string m_sName = "";
};

static PVOID s_pHandle;
static LPVOID s_lpParam;
static int s_iExceptions = 0;

static inline std::deque<Frame_t> StackTrace(PCONTEXT pContext)
{
	std::deque<Frame_t> vTrace = {};

	HANDLE hProcess = GetCurrentProcess();
	HANDLE hThread = GetCurrentThread();

	if (!SymInitialize(hProcess, nullptr, TRUE))
		return vTrace;

	SymSetOptions(SYMOPT_LOAD_LINES);

	STACKFRAME64 tStackFrame = {};
	tStackFrame.AddrPC.Offset = pContext->Rip;
	tStackFrame.AddrFrame.Offset = pContext->Rbp;
	tStackFrame.AddrStack.Offset = pContext->Rsp;
	tStackFrame.AddrPC.Mode = AddrModeFlat;
	tStackFrame.AddrFrame.Mode = AddrModeFlat;
	tStackFrame.AddrStack.Mode = AddrModeFlat;

	CONTEXT tContext = *pContext;

	while (StackWalk64(IMAGE_FILE_MACHINE_AMD64, hProcess, hThread, &tStackFrame, &tContext, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
	{
		vTrace.push_back({ .m_uAddress = tStackFrame.AddrPC.Offset });
		Frame_t& tFrame = vTrace.back();

		if (auto hBase = HINSTANCE(SymGetModuleBase64(hProcess, tStackFrame.AddrPC.Offset)))
		{
			tFrame.m_uBase = uintptr_t(hBase);

			char buffer[MAX_PATH];
			if (GetModuleBaseName(hProcess, hBase, buffer, sizeof(buffer) / sizeof(char)))
				tFrame.m_sModule = buffer;
			else
				tFrame.m_sModule = std::format("{:#x}", tFrame.m_uBase);
		}

		{
			DWORD dwOffset = 0;
			IMAGEHLP_LINE64 line = {};
			line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
			if (SymGetLineFromAddr64(hProcess, tStackFrame.AddrPC.Offset, &dwOffset, &line))
			{
				tFrame.m_sFile = line.FileName;
				tFrame.m_uLine = line.LineNumber;
				auto iFind = tFrame.m_sFile.rfind("\\");
				if (iFind != std::string::npos)
					tFrame.m_sFile.replace(0, iFind + 1, "");
			}
		}

		{
			DWORD64 dwOffset = 0;
			char buf[sizeof(IMAGEHLP_SYMBOL64) + 255];
			auto symbol = PIMAGEHLP_SYMBOL64(buf);
			symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64) + 255;
			symbol->MaxNameLength = 254;
			if (SymGetSymFromAddr64(hProcess, tStackFrame.AddrPC.Offset, &dwOffset, symbol))
				tFrame.m_sName = symbol->Name;
		}
	}

	SymCleanup(hProcess);

	return vTrace;
}

// DbgHelp isn't thread safe, only one thread may walk at a time. returns false if another thread is walking
static std::mutex s_mDbgHelp;
static inline bool TryStackTrace(PCONTEXT pContext, std::deque<Frame_t>& vTrace)
{
	std::unique_lock tLock(s_mDbgHelp, std::try_to_lock);
	if (!tLock.owns_lock())
		return false;

	vTrace = StackTrace(pContext);
	return true;
}

static LONG APIENTRY ExceptionFilter(PEXCEPTION_POINTERS ExceptionInfo)
{
	const char* sError = "UNKNOWN";
	switch (ExceptionInfo->ExceptionRecord->ExceptionCode)
	{
	case STATUS_ACCESS_VIOLATION: sError = "ACCESS VIOLATION"; break;
	case STATUS_HEAP_CORRUPTION: sError = "HEAP CORRUPTION"; break;
	case STATUS_STACK_OVERFLOW: // almost no stack is left, so walking/formatting here would just fault again
	case STATUS_RUNTIME_ERROR:
	case EXCEPTION_BREAKPOINT:
	case DBG_PRINTEXCEPTION_C:
	case DBG_PRINTEXCEPTION_WIDE_C:
	case DBG_THREAD_NAMING: return EXCEPTION_CONTINUE_SEARCH;
	}

	if (!Vars::Debug::CrashLogging.Value)
		return EXCEPTION_CONTINUE_SEARCH;

	// guard against the handler faulting and re-entering itself
	static thread_local bool s_bInHandler = false;
	if (s_bInHandler)
		return EXCEPTION_CONTINUE_SEARCH;
	struct Guard_t { Guard_t() { s_bInHandler = true; } ~Guard_t() { s_bInHandler = false; } } tGuard;

	// a vectored handler sees every first-chance exception in the process, including ones the game
	// catches and handles itself. only report exceptions that happen in, or were called from, our module
	std::deque<Frame_t> vTrace = {};
	{
		MODULEINFO tInfo = {};
		if (GetModuleInformation(GetCurrentProcess(), HMODULE(s_lpParam), &tInfo, sizeof(tInfo)))
		{
			const uintptr_t uStart = uintptr_t(tInfo.lpBaseOfDll), uEnd = uStart + tInfo.SizeOfImage;
			auto fInModule = [&](uintptr_t uAddress) { return uAddress >= uStart && uAddress < uEnd; };

			// cheap check first, only walk the stack when the fault itself isn't in our module
			if (fInModule(uintptr_t(ExceptionInfo->ExceptionRecord->ExceptionAddress)))
				TryStackTrace(ExceptionInfo->ContextRecord, vTrace); // if busy, report without a trace
			else
			{
				if (!TryStackTrace(ExceptionInfo->ContextRecord, vTrace))
					return EXCEPTION_CONTINUE_SEARCH; // another thread is walking, can't tell whether it's ours

				bool bOurs = false;
				for (auto& tFrame : vTrace)
				{
					if (bOurs = fInModule(tFrame.m_uAddress))
						break;
				}
				if (!bOurs)
					return EXCEPTION_CONTINUE_SEARCH;
			}
		}
		else
			TryStackTrace(ExceptionInfo->ContextRecord, vTrace); // can't tell, err on the side of logging
	}

	std::stringstream ssErrorStream;
	ssErrorStream << std::format("Error: {} (0x{:X}) ({})\n", sError, ExceptionInfo->ExceptionRecord->ExceptionCode, ++s_iExceptions);
	ssErrorStream << "Built @ " __DATE__ ", " __TIME__ ", " __CONFIGURATION__ "\n";
	ssErrorStream << std::format("Time @ {}, {}\n", SDK::GetDate(), SDK::GetTime());

	ssErrorStream << "\n";
	if (U::Memory.GetOffsetFromBase(s_lpParam))
		ssErrorStream << std::format("This: {}\n", U::Memory.GetModuleOffset(s_lpParam));
	ssErrorStream << std::format("RIP: {:#x}\n", ExceptionInfo->ContextRecord->Rip);
	ssErrorStream << std::format("RAX: {:#x}\n", ExceptionInfo->ContextRecord->Rax);
	ssErrorStream << std::format("RCX: {:#x}\n", ExceptionInfo->ContextRecord->Rcx);
	ssErrorStream << std::format("RDX: {:#x}\n", ExceptionInfo->ContextRecord->Rdx);
	ssErrorStream << std::format("RBX: {:#x}\n", ExceptionInfo->ContextRecord->Rbx);
	ssErrorStream << std::format("RSP: {:#x}\n", ExceptionInfo->ContextRecord->Rsp);
	ssErrorStream << std::format("RBP: {:#x}\n", ExceptionInfo->ContextRecord->Rbp);
	ssErrorStream << std::format("RSI: {:#x}\n", ExceptionInfo->ContextRecord->Rsi);
	ssErrorStream << std::format("RDI: {:#x}\n", ExceptionInfo->ContextRecord->Rdi);

	ssErrorStream << "\n";
	if (!vTrace.empty())
	{
		for (int i = 0; i < vTrace.size(); i++)
		{
			Frame_t& tFrame = vTrace[i];

			ssErrorStream << std::format("{}: ", i + 1);
			if (tFrame.m_uBase)
				ssErrorStream << std::format("{}+{:#x}", tFrame.m_sModule, tFrame.m_uAddress - tFrame.m_uBase);
			else
				ssErrorStream << std::format("{:#x}", tFrame.m_uAddress);
			if (!tFrame.m_sFile.empty())
				ssErrorStream << std::format(" ({} L{})", tFrame.m_sFile, tFrame.m_uLine);
			if (!tFrame.m_sName.empty())
				ssErrorStream << std::format(" ({})", tFrame.m_sName);
			ssErrorStream << "\n";
		}
	}
	else
	{
		ssErrorStream << U::Memory.GetModuleOffset(ExceptionInfo->ExceptionRecord->ExceptionAddress);
		ssErrorStream << "\n";
	}

	try
	{
		std::ofstream file;
		file.open(F::Configs.m_sConfigPath + "crash_log.txt", std::ios_base::app);
		file << ssErrorStream.str() + "\n\n\n";
		file.close();

		ssErrorStream << "\n";
		ssErrorStream << "Ctrl + C to copy. \n";
		ssErrorStream << "Logged to Nikogram\\crash_log.txt. ";
	}
	catch (...) {}

	switch (ExceptionInfo->ExceptionRecord->ExceptionCode)
	{
	case STATUS_ACCESS_VIOLATION:
	case STATUS_HEAP_CORRUPTION:
		SDK::Output("Unhandled exception", ssErrorStream.str().c_str(), {}, OUTPUT_DEBUG, nullptr, MB_OK | MB_ICONERROR);
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

void CExceptionHandler::Initialize(LPVOID lpParam)
{
	s_pHandle = AddVectoredExceptionHandler(1, ExceptionFilter);
	s_lpParam = lpParam;
}
void CExceptionHandler::Unload()
{
	RemoveVectoredExceptionHandler(s_pHandle);
}