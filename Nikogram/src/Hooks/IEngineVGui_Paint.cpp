#include "../SDK/SDK.h"

#include "../Features/Visuals/ESP/ESP.h"
#include "../Features/Visuals/OffscreenArrows/OffscreenArrows.h"
#include "../Features/Visuals/CameraWindow/CameraWindow.h"
#include "../Features/Visuals/Visuals.h"
#include "../Features/Ticks/Ticks.h"
#include "../Features/CritHack/CritHack.h"
#include "../Features/Visuals/SpectatorList/SpectatorList.h"
#include "../Features/Visuals/Radar/Radar.h"
#include "../Features/Backtrack/Backtrack.h"
#include "../Features/Visuals/PlayerConditions/PlayerConditions.h"
#include "../Features/NoSpread/NoSpreadHitscan/NoSpreadHitscan.h"
#include "../Features/Aimbot/Aimbot.h"
#include "../Features/PacketManip/AntiAim/AntiAim.h"
#include "../Features/Aimbot/AutoHeal/AutoHeal.h"
#include "../Features/Debug/Debug.h"
#include "../Features/ImGui/MoonlitHud.h"

MAKE_HOOK(IEngineVGui_Paint, U::Memory.GetVirtual(I::EngineVGui, 14), void,
	void* rcx, int iMode)
{
	DEBUG_RETURN(IEngineVGui_Paint, rcx, iMode);

	if (G::Unload)
		return CALL_ORIGINAL(rcx, iMode);
	if (SDK::CleanScreenshot()) MoonlitHud::InvalidateBadge();

	if (iMode & PAINT_UIPANELS)
	{
		H::Draw.UpdateKeyStrings();
	}
	else if (iMode & PAINT_INGAMEPANELS && !SDK::CleanScreenshot())
	{
		MoonlitHud::BeginBadges();
		H::Draw.UpdateScreenSize();
		H::Draw.UpdateW2SMatrix();

		H::Draw.Start(true);
		if (auto pLocal = H::Entities.GetLocal())
		{
			F::CameraWindow.Draw();

			F::AntiAim.Draw(pLocal);
			F::Visuals.DrawPickupTimers();
			F::ESP.Draw();
			F::Visuals.DrawExtrapolationGuide();
			F::OffscreenArrows.Draw(pLocal);
			F::Aimbot.Draw(pLocal);

#ifdef DEBUG_VACCINATOR
			F::AutoHeal.Draw(pLocal);
#endif
			F::NoSpreadHitscan.Draw(pLocal);
			F::PlayerConditions.Draw(pLocal);
			F::Backtrack.Draw(pLocal);
			F::SpectatorList.Draw(pLocal);
			if (!F::Radar.SuppressCrit()) F::CritHack.Draw(pLocal);
			if (!F::Radar.SuppressTicks()) F::Ticks.Draw(pLocal);
			F::Radar.Draw(pLocal);

#ifdef DEBUG_INFO
			F::Debug.Draw(pLocal);
#endif
		}
		H::Draw.End();
		MoonlitHud::EndBadges();
	}

	CALL_ORIGINAL(rcx, iMode);

}
