#include "../SDK/SDK.h"

#include "../Features/EnginePrediction/EnginePrediction.h"
#include "../Features/Simulation/MovementSimulation/MovementSimulation.h"
#include "../Features/Spectate/Spectate.h"
#include "../Features/Blockbot/Blockbot.h"
#include "../Features/Visuals/AnimInterp/AnimInterp.h"
#include "../Features/Visuals/Visuals.h"
#include "../Features/Aimbot/AutoViewmodelSwitch.h"
#include "../Features/Backtrack/Backtrack.h"
#include "../Features/Triggerbot/Triggerbot.h"
#include "../Features/Aimbot/AutoFlarePunch.h"

MAKE_HOOK(CHLClient_LevelShutdown, U::Memory.GetVirtual(I::Client, 7), void,
	void* rcx)
{
	DEBUG_RETURN(CHLClient_LevelShutdown, rcx);

	F::AnimInterp.Restore();
    AutoViewmodelSwitch::Reset();
    F::Backtrack.Reset();
	F::Triggerbot.Reset();
	F::AutoFlarePunch.Reset();
	F::AnimInterp.Reset();
	F::Visuals.ResetLocalAnimationQueue();
	H::Entities.Clear(true);
	F::MoveSim.Clear();
	F::EnginePrediction.Unload();
	F::Spectate.Reset();
	F::Blockbot.Reset();
	H::Draw.ClearAvatarCache(); // free avatar textures between maps instead of keeping them forever

	CALL_ORIGINAL(rcx);
}
