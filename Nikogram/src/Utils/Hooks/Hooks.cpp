#include "Hooks.h"

#include "../../Core/Core.h"
#include "../../Hooks/Direct3DDevice9.h"
#include <ranges>
#include <format>

CHook::CHook(const std::string& sName, void* pInitFunc)
{
	m_pInitFunc = pInitFunc;
	U::Hooks.m_mHooks[sName] = this;
}

bool CHooks::Initialize()
{
	MH_Initialize();

	WndProc::Initialize();
	for (auto& [sName, pHook] : m_mHooks)
	{
		reinterpret_cast<void(__cdecl*)()>(pHook->m_pInitFunc)();
		if (!pHook->m_pOriginal) // MH_CreateHook only sets the original on success
			U::Core.AppendFailText(std::format("CHooks::Initialize() failed to create hook:\n  {}", sName).c_str());
	}

	m_bFailed = MH_EnableHook(MH_ALL_HOOKS) != MH_OK;
	if (m_bFailed)
		U::Core.AppendFailText("MinHook failed to enable all hooks!");
	return !m_bFailed;
}

bool CHooks::Unload()
{
	// disable first so no new calls enter our hooks, then give calls already inside them time
	// to return before the hooks (and later the DLL) are torn down
	MH_DisableHook(MH_ALL_HOOKS);
	Sleep(100);

	m_bFailed = MH_Uninitialize() != MH_OK;
	if (m_bFailed)
		U::Core.AppendFailText("MinHook failed to unload all hooks!");
	WndProc::Unload();
	return !m_bFailed;
}