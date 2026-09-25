#include "../SDK/SDK.h"

#include "../Features/Backtrack/Backtrack.h"
#include "../Features/Visuals/AnimInterp/AnimInterp.h"

MAKE_SIGNATURE(CBaseAnimating_SetupBones, "client.dll", "48 8B C4 44 89 40 ? 48 89 50 ? 55 53", 0x0);

MAKE_HOOK(CBaseAnimating_SetupBones, S::CBaseAnimating_SetupBones(), bool,
	void* rcx, matrix3x4* pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime)
{
	DEBUG_RETURN(CBaseAnimating_SetupBones, rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

	if (!Vars::Misc::Game::SetupBonesOptimization.Value || F::Backtrack.IsSettingUpBones())
		return CALL_ORIGINAL(rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

	auto pAnimating = reinterpret_cast<CBaseEntity*>(uintptr_t(rcx) - 8);
	if (!pAnimating)
		return CALL_ORIGINAL(rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

	auto pOwner = pAnimating->GetRootMoveParent();
	auto pEntity = pOwner ? pOwner : pAnimating;
	if (F::AnimInterp.ShouldRebuildBones(pEntity))
	{
		const bool bResult = CALL_ORIGINAL(rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);
		// A wearable's setup isn't proof that its owner's own cache has been rebuilt.
		if (bResult && pAnimating == pEntity)
			F::AnimInterp.BonesRebuilt(pEntity);
		return bResult;
	}
	if (!pEntity->IsPlayer() || pEntity->entindex() == I::EngineClient->GetLocalPlayer())
		return CALL_ORIGINAL(rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);

	if (pBoneToWorldOut)
	{
		auto& aBones = pEntity->As<CBaseAnimating>()->m_CachedBoneData();
		if (aBones.Count() <= 0) // no cached bones yet
			return CALL_ORIGINAL(rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);
		if (nMaxBones >= aBones.Count())
			memcpy(pBoneToWorldOut, aBones.Base(), sizeof(matrix3x4) * aBones.Count());
		else
			return false;
	}

	return true;
}
