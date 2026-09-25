#include "../SDK/SDK.h"

#include "../Features/Aimbot/Aimbot.h"
#include "../Features/Backtrack/Backtrack.h"
#include "../Features/CritHack/CritHack.h"
#include "../Features/EnginePrediction/EnginePrediction.h"
#include "../Features/Misc/Misc.h"
#include "../Features/NoSpread/NoSpread.h"
#include "../Features/NoSpread/NoSpreadHitscan/NoSpreadHitscan.h"
#include "../Features/PacketManip/PacketManip.h"
#include "../Features/Resolver/Resolver.h"
#include "../Features/Ticks/Ticks.h"
#include "../Features/Visuals/Visuals.h"
#include "../Features/Visuals/FakeAngle/FakeAngle.h"
#include "../Features/Visuals/AnimInterp/AnimInterp.h"
#include "../Features/Spectate/Spectate.h"
#include "../Features/AntiCheatCompatibility/AntiCheatCompatibility.h"

static no_inline void UpdateInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	G::PSilentAngles = G::SilentAngles = G::Attacking = G::Throwing = false;
	G::LastUserCmd = G::CurrentUserCmd ? G::CurrentUserCmd : pCmd;
	G::CurrentUserCmd = pCmd;
	G::OriginalCmd = *pCmd;

	if (!pWeapon)
		return;

	SDK::CanAttack(pLocal, pWeapon, pCmd, G::CanPrimaryAttack, G::CanSecondaryAttack, G::Reloading);
	G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd);
	G::PrimaryWeaponType = SDK::GetWeaponType(pWeapon, &G::SecondaryWeaponType);
	G::CanHeadshot = pWeapon->CanHeadshot() || pWeapon->AmbassadorCanHeadshot(TICKS_TO_TIME(pLocal->m_nTickBase()));
}

/*
	CL_Move keeps its local bSendPacket in a register (dil in the current engine.dll) across the call to
	CHLClient::CreateMove, then checks it with `test dil, dil` to decide between sending and choking.
	The old approach wrote to _AddressOfReturnAddress() + 0x20, which only lands on the saved rdi if this
	function happens to be compiled to spill rdi into that exact home slot. It isn't in current builds,
	so choking silently did nothing (fakelag/anti-aim broken, local animations frozen).

	Instead, CreateMove is hooked with a tiny generated stub that copies dil into s_bSendPacket, calls
	Func, and loads s_bSendPacket back into dil before returning to CL_Move. That doesn't depend on how
	this file gets compiled. Func verifies once that CL_Move really tests dil after the call; if a game
	update changes that, choking is disabled (dil is left untouched) instead of corrupting the register.
*/
namespace Hooks
{
	namespace CHLClient_CreateMove
	{
		void Init();
		inline CHook Hook("CHLClient_CreateMove", Init);
		using FN = void(__fastcall*)(void*, int, float, bool);
		void __fastcall Func(void* rcx, int sequence_number, float input_sample_frametime, bool active);

		inline bool s_bSendPacket = true; // CL_Move's bSendPacket, shuttled in and out by the stub
		inline uintptr_t s_uReturnAddress = 0; // return site in CL_Move, for the layout check
		inline void* s_pStub = nullptr;
	}
}
#ifdef DEBUG_HOOKS
DEBUG_VAR(CHLClient_CreateMove)
#endif

static void* CreateSendPacketStub(void* pTarget)
{
	auto fImm64 = [](std::vector<uint8_t>& vCode, uint64_t uValue)
	{
		for (int i = 0; i < 8; i++)
			vCode.push_back(uint8_t(uValue >> (i * 8)));
	};

	std::vector<uint8_t> vCode = {};
	vCode.insert(vCode.end(), { 0x48, 0xB8 }); fImm64(vCode, uintptr_t(&Hooks::CHLClient_CreateMove::s_uReturnAddress)); // mov rax, &s_uReturnAddress
	vCode.insert(vCode.end(), { 0x4C, 0x8B, 0x14, 0x24 }); // mov r10, [rsp]
	vCode.insert(vCode.end(), { 0x4C, 0x89, 0x10 }); // mov [rax], r10
	vCode.insert(vCode.end(), { 0x48, 0xB8 }); fImm64(vCode, uintptr_t(&Hooks::CHLClient_CreateMove::s_bSendPacket)); // mov rax, &s_bSendPacket
	vCode.insert(vCode.end(), { 0x40, 0x88, 0x38 }); // mov [rax], dil
	vCode.insert(vCode.end(), { 0x48, 0x83, 0xEC, 0x28 }); // sub rsp, 28h (shadow space + alignment)
	vCode.insert(vCode.end(), { 0x48, 0xB8 }); fImm64(vCode, uintptr_t(pTarget)); // mov rax, Func
	vCode.insert(vCode.end(), { 0xFF, 0xD0 }); // call rax
	vCode.insert(vCode.end(), { 0x48, 0x83, 0xC4, 0x28 }); // add rsp, 28h
	vCode.insert(vCode.end(), { 0x48, 0xB8 }); fImm64(vCode, uintptr_t(&Hooks::CHLClient_CreateMove::s_bSendPacket)); // mov rax, &s_bSendPacket
	vCode.insert(vCode.end(), { 0x40, 0x8A, 0x38 }); // mov dil, [rax]
	vCode.push_back(0xC3); // ret

	void* pStub = VirtualAlloc(nullptr, vCode.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!pStub)
		return nullptr;
	memcpy(pStub, vCode.data(), vCode.size());
	DWORD dwOld;
	if (!VirtualProtect(pStub, vCode.size(), PAGE_EXECUTE_READ, &dwOld))
	{
		VirtualFree(pStub, 0, MEM_RELEASE);
		return nullptr;
	}
	FlushInstructionCache(GetCurrentProcess(), pStub, vCode.size());
	return pStub;
}

// true if CL_Move tests dil (40 84 FF) shortly after the CreateMove call returns
static bool VerifySendPacketRegister(uintptr_t uReturnAddress)
{
	if (!uReturnAddress)
		return false;
	auto pBytes = reinterpret_cast<const uint8_t*>(uReturnAddress);
	for (int i = 0; i < 0x60; i++)
	{
		if (pBytes[i] == 0x40 && pBytes[i + 1] == 0x84 && pBytes[i + 2] == 0xFF)
			return true;
	}
	return false;
}

void Hooks::CHLClient_CreateMove::Init()
{
	if (!s_pStub)
		s_pStub = CreateSendPacketStub(reinterpret_cast<void*>(Func));
	// fall back to hooking Func directly: everything still runs, choking just won't take effect
	Hook.Create(U::Memory.GetVirtual(I::Client, 21), s_pStub ? s_pStub : reinterpret_cast<void*>(Func));
}

void __fastcall Hooks::CHLClient_CreateMove::Func(void* rcx, int sequence_number, float input_sample_frametime, bool active)
{
	DEBUG_RETURN(CHLClient_CreateMove, rcx, sequence_number, input_sample_frametime, active);

	// Compensation must see the simulated animstate, never the interpolated presentation.
	F::AnimInterp.Restore();
	CALL_ORIGINAL(rcx, sequence_number, input_sample_frametime, active);

	auto pLocal = H::Entities.GetLocal();
	auto pWeapon = H::Entities.GetWeapon();
	if (!pLocal)
	{
		F::Visuals.ResetLocalAnimationQueue();
		return;
	}

	// 0 = unchecked, 1 = CL_Move tests dil so writing it works, -1 = unknown layout, never write it
	static int s_iVerified = 0;
	if (!s_iVerified && s_pStub)
	{
		s_iVerified = VerifySendPacketRegister(s_uReturnAddress) ? 1 : -1;
		if (s_iVerified == -1)
			SDK::Output("Nikogram", "CL_Move layout changed, packet choking (fakelag, anti-aim) is disabled", { 255, 100, 100, 255 });
	}
	const bool bEngineSendPacket = s_bSendPacket;
	bool* pSendPacket = &s_bSendPacket;
	CUserCmd* pCmd = &I::Input->m_pCommands[sequence_number % MULTIPLAYER_BACKUP];

	I::Prediction->Update(I::ClientState->m_nDeltaTick, I::ClientState->m_nDeltaTick > 0, I::ClientState->last_command_ack, I::ClientState->lastoutgoingcommand + I::ClientState->chokedcommands);

	UpdateInfo(pLocal, pWeapon, pCmd);
		F::Spectate.CreateMove(pCmd);
		F::Backtrack.CreateMove(pCmd);
		F::Misc.RunPre(pLocal, pCmd);
	F::Ticks.Start(pLocal, pCmd);
		F::Aimbot.Run(pLocal, pWeapon, pCmd);
	F::Ticks.End(pLocal, pCmd);
		F::CritHack.Run(pLocal, pWeapon, pCmd);
		F::NoSpread.Run(pLocal, pWeapon, pCmd);
		F::Misc.RunPost(pLocal, pCmd);
		F::PacketManip.Run(pLocal, pWeapon, pCmd, pSendPacket);
		F::Ticks.CreateMove(pLocal, pWeapon, pCmd, pSendPacket);
		F::AntiAim.Run(pLocal, pWeapon, pCmd, *pSendPacket);
		F::AntiCheatCompatibility.CreateMove(pCmd, pSendPacket);
		F::Visuals.CreateMove(pLocal, pWeapon);
		F::Visuals.LocalAnimations(pLocal, pWeapon, pCmd, *pSendPacket);
	F::EnginePrediction.End(pLocal, pCmd);
		F::Resolver.CreateMove();
		F::NoSpreadHitscan.AskForPlayerPerf();
	G::Choking = !*pSendPacket, G::LastUserCmd = pCmd;
	if (s_iVerified != 1)
		s_bSendPacket = bEngineSendPacket, G::Choking = false; // can't choke safely, hand the engine back its own value
}
