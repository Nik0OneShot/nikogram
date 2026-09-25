#include "../SDK/SDK.h"
#include "../Features/Visuals/AnimInterp/AnimInterp.h"

MAKE_SIGNATURE(CBaseAnimating_MaintainSequenceTransitions, "client.dll", "4C 89 4C 24 ? 41 56", 0x0);

MAKE_HOOK(CBaseAnimating_MaintainSequenceTransitions, S::CBaseAnimating_MaintainSequenceTransitions(), void,
	void* rcx, void* boneSetup, float flCycle, Vec3 pos[], Vector4D q[])
{
	DEBUG_RETURN(CBaseAnimating_MaintainSequenceTransitions, rcx, boneSetup, flCycle, pos, q);
	if (F::AnimInterp.ShouldTransition(reinterpret_cast<CBaseEntity*>(rcx)))
		CALL_ORIGINAL(rcx, boneSetup, flCycle, pos, q);
}
