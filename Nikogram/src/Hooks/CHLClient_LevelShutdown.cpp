#include "../SDK/SDK.h"

#include "../Features/EnginePrediction/EnginePrediction.h"
#include "../Features/Spectate/Spectate.h"
#include "../Features/Visuals/AnimInterp/AnimInterp.h"
#include "../Features/Visuals/Visuals.h"

MAKE_HOOK(CHLClient_LevelShutdown, U::Memory.GetVirtual(I::Client, 7), void,
	void* rcx)
{
	DEBUG_RETURN(CHLClient_LevelShutdown, rcx);

	F::AnimInterp.Restore();
	F::AnimInterp.Reset();
	F::Visuals.ResetLocalAnimationQueue();
	H::Entities.Clear(true);
	F::EnginePrediction.Unload();
	F::Spectate.Reset();
	H::Draw.ClearAvatarCache(); // free avatar textures between maps instead of keeping them forever

	CALL_ORIGINAL(rcx);
}
