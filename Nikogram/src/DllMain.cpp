#include <Windows.h>
#include "MenuStartup.h"
#include "Core/Core.h"
#include "Utils/ExceptionHandler/ExceptionHandler.h"

DWORD WINAPI MainThread(LPVOID lpParam)
{
	MenuStartup::Capture(); // Before startup checks; the loader can safely close after acknowledgement.
	U::ExceptionHandler.Initialize(lpParam);

	U::Core.Load();
	U::Core.Loop();
	U::Core.Unload();
	if(!U::Core.m_bCanDetach)return EXIT_FAILURE;

	U::ExceptionHandler.Unload();

	FreeLibraryAndExitThread(static_cast<HMODULE>(lpParam), EXIT_SUCCESS);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		if (const auto hThread = CreateThread(nullptr, 0, MainThread, hinstDLL, 0, nullptr))
			CloseHandle(hThread);
	}

	return TRUE;
}
