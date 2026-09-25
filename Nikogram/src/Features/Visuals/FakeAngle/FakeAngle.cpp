#include "FakeAngle.h"

#include "../../PacketManip/AntiAim/AntiAim.h"
#include "../../Ticks/Ticks.h"
#include "../Groups/Groups.h"
#include "../AnimInterp/AnimInterp.h"

void CFakeAngle::Run(CTFPlayer* pLocal)
{
	if (!pLocal || !pLocal->IsAlive() || pLocal->IsAGhost()
		|| !F::AntiAim.AntiAimOn() && (!Vars::Fakelag::Fakelag.Value || F::Ticks.m_iShiftedTicks == F::Ticks.m_iMaxShift))
	{
		bBonesSetup = false;
		return;
	}

	Group_t* pGroup = nullptr;
	if (!F::Groups.GetGroup(TargetsEnum::FakeAngle, pGroup) || !pGroup->m_tChams(true) && !pGroup->m_tGlow())
	{
		bBonesSetup = false;
		return;
	}

	auto pAnimState = pLocal->m_PlayerAnimState();
	if (!pAnimState)
	{
		bBonesSetup = false;
		return;
	}

	float flOldFrameTime = I::GlobalVars->frametime;
	int nOldSequence = pLocal->m_nSequence();
	float flOldCycle = pLocal->m_flCycle();
	float flOldPlaybackRate = pLocal->m_flPlaybackRate();
	bool bOldSequenceLoops = pLocal->m_bSequenceLoops();
	float flOldTauntYaw = pLocal->m_flTauntYaw();
	auto pOldPoseParams = pLocal->m_flPoseParameter();
	char pOldAnimState[sizeof(CTFPlayerAnimState)];
	memcpy(pOldAnimState, pAnimState, sizeof(CTFPlayerAnimState));
	const int nSlots = std::min(pAnimState->m_aGestureSlots.Count(), int(GESTURE_SLOT_COUNT));
	std::array<CAnimationLayer, GESTURE_SLOT_COUNT> aOldLayers = {};
	for (int i = 0; i < nSlots; i++)
	{
		if (auto pLayer = pAnimState->m_aGestureSlots[i].m_pAnimLayer)
			aOldLayers[i] = *pLayer;
	}
	CAnimInterp::FakeBuildScope fakeScope(F::AnimInterp);

	I::GlobalVars->frametime = 0.f;
	Vec2 vAngle = { std::clamp(F::AntiAim.vFakeAngles.x, -89.f, 89.f), F::AntiAim.vFakeAngles.y };
	if (pLocal->IsTaunting() && pLocal->m_bAllowMoveDuringTaunt())
		pLocal->m_flTauntYaw() = vAngle.y;
	pAnimState->Update(pAnimState->m_flCurrentFeetYaw = /*pAnimState->m_flEyeYaw =*/ vAngle.y, vAngle.x);
	pLocal->InvalidateBoneCache();
	bBonesSetup = pLocal->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);
	vOrigin = pLocal->GetAbsOrigin();
	vAngles = vAngle;
	flCurTime = I::GlobalVars->curtime;

	I::GlobalVars->frametime = flOldFrameTime;
	pLocal->m_nSequence() = nOldSequence;
	pLocal->m_flCycle() = flOldCycle;
	pLocal->m_flPlaybackRate() = flOldPlaybackRate;
	pLocal->m_bSequenceLoops() = bOldSequenceLoops;
	pLocal->m_flTauntYaw() = flOldTauntYaw;
	pLocal->m_flPoseParameter() = pOldPoseParams;
	memcpy(pAnimState, pOldAnimState, sizeof(CTFPlayerAnimState));
	for (int i = 0; i < nSlots; i++)
	{
		if (auto pLayer = pAnimState->m_aGestureSlots[i].m_pAnimLayer)
			*pLayer = aOldLayers[i];
	}
	pLocal->InvalidateBoneCache();
}
