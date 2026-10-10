#include "../SDK/SDK.h"
#include "../Core/Core.h"

#include "../Features/Aimbot/Aimbot.h"
#include "../Features/Backtrack/Backtrack.h"
#include "../Features/PacketManip/RealLag/RealLag.h"
#include "../Features/Statistics/Statistics.h"
#include "../Features/SkinChanger/SkinChanger.h"
#include "../Features/LearningAccess.h"
#include "../Features/Binds/Binds.h"
#include "../Features/CheatDetection/CheatDetection.h"
#include "../Features/CritHack/CritHack.h"
#include "../Features/Players/PlayerUtils.h"
#include "../Features/Simulation/MovementSimulation/MovementSimulation.h"
#include "../Features/Spectate/Spectate.h"
#include "../Features/Visuals/Visuals.h"
#include "../Features/Visuals/AnimInterp/AnimInterp.h"
#include "../Features/Visuals/FakeAngle/FakeAngle.h"
#include "../Features/Visuals/ESP/ESP.h"
#include "../Features/Visuals/Chams/Chams.h"
#include "../Features/Visuals/Glow/Glow.h"
#include "../Features/Visuals/Groups/Groups.h"
#include "../Features/Visuals/OffscreenArrows/OffscreenArrows.h"

MAKE_HOOK(CHLClient_FrameStageNotify, U::Memory.GetVirtual(I::Client, 35), void,
	void* rcx, ClientFrameStage_t curStage)
{
	// This is the only hook allowed to perform startup before the gate opens.
	HookLifetime::Scope hookLifetimeScope;
	if(curStage==FRAME_START)U::Core.ServiceStartup();
	if(!StartupPolicy::gate.Ready())return CALL_ORIGINAL(rcx,curStage);
	if(curStage==FRAME_START){F::AnimInterp.Restore();SkinChanger::ServiceUnload();}

	if (G::Unload)
	{
		F::AnimInterp.Restore();
		return CALL_ORIGINAL(rcx, curStage);
	}

	// Restore before engine callbacks can consume animation data or apply network updates.
	if (curStage == FRAME_START) { RealLag::Update(); Statistics::Tick(); PrivateLearning::Pulse(); SkinChanger::Tick(); }
	if (curStage == FRAME_NET_UPDATE_START || curStage == FRAME_RENDER_START || curStage == FRAME_RENDER_END)
		F::AnimInterp.Restore();
	CALL_ORIGINAL(rcx, curStage);

	switch (curStage)
	{
	case FRAME_NET_UPDATE_START:
	{
		auto pLocal = H::Entities.GetLocal();
		F::Spectate.NetUpdateStart(pLocal);

		H::Entities.Clear();
		break;
	}
	case FRAME_NET_UPDATE_END:
	{
		H::Entities.Store();
		F::PlayerUtils.Store();

		F::Backtrack.Store();
		F::MoveSim.Store();
		F::CritHack.Store();
		F::Aimbot.Store();
		PrivateLearning::Observe();

		auto pLocal = H::Entities.GetLocal();
		F::Groups.Store(pLocal);
		F::ESP.Store(pLocal);
		F::Chams.Store(pLocal);
		F::Glow.Store(pLocal);
		F::OffscreenArrows.Store();
		F::Visuals.Store();

		F::CheatDetection.Run();
		F::Spectate.NetUpdateEnd(pLocal);

		// Modulation is applied/restored at the scene boundary, including capture.
		F::Visuals.DrawHitboxes(1);
		break;
	}
	case FRAME_RENDER_START:
		F::AnimInterp.RenderStart();
		F::FakeAngle.SendPreviewHead(); // read the exact bones used by the yellow preview before Restore()
		for (auto& tBind : F::Binds.m_vBinds)
		{	// don't drop inputs for binds
			if (tBind.m_iType != BindEnum::Key)
				continue;

			auto& tKey = tBind.m_tKeyStorage;

			bool bOldIsDown = tKey.m_bIsDown;
			bool bOldIsPressed = tKey.m_bIsPressed;
			bool bOldIsDouble = tKey.m_bIsDouble;
			bool bOldIsReleased = tKey.m_bIsReleased;

			U::KeyHandler.StoreKey(tBind.m_iKey, &tKey);

			tKey.m_bIsDown = tKey.m_bIsDown || bOldIsDown;
			tKey.m_bIsPressed = tKey.m_bIsPressed || bOldIsPressed;
			tKey.m_bIsDouble = tKey.m_bIsDouble || bOldIsDouble;
			tKey.m_bIsReleased = tKey.m_bIsReleased || bOldIsReleased;
		}
		break;
	case FRAME_RENDER_END:
		F::AnimInterp.RenderEnd();
		break;
	}
}
