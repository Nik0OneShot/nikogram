#include "AimbotMelee.h"
#include "../MeleeDiagnostics.h"
#include "../MeleePredictionPolicy.h"
#include "../MeleeTrace.h"
#include "../MeleeContactPolicy.h"
namespace MD = MeleeDiagnostics;

#include "../Aimbot.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../../EnginePrediction/EnginePrediction.h"
#include "../../Ticks/Ticks.h"
#include "../../Visuals/Visuals.h"
#include "../../AntiCheatCompatibility/AntiCheatCompatibility.h"

static inline bool AimFriendlyBuilding(CBaseObject* pBuilding)
{
	if (!pBuilding->m_bMiniBuilding() && pBuilding->m_iUpgradeLevel() != 3 || pBuilding->m_iHealth() < pBuilding->m_iMaxHealth() || pBuilding->m_bHasSapper())
		return true;

	if (pBuilding->IsSentrygun())
	{
		int iShells, iMaxShells, iRockets, iMaxRockets; pBuilding->As<CObjectSentrygun>()->GetAmmoCount(iShells, iMaxShells, iRockets, iMaxRockets);
		if (iShells < iMaxShells || iRockets < iMaxRockets)
			return true;
	}

	return false;
}

static bool MeleeViewPoint(const Vec3& eye,const Vec3& angles,const Vec3& mins,const Vec3& maxs,Vec3& point)
{
    Vec3 forward;Math::AngleVectors(angles,&forward);
    const float distance=std::max(1.f,eye.DistTo((mins+maxs)*.5f)+(maxs-mins).Length());
    const Vec3 end=eye+forward*distance;
    MeleeContactPolicy::Point result;
    if(!MeleeContactPolicy::ViewPoint({eye.x,eye.y,eye.z},{end.x,end.y,end.z},
        {mins.x,mins.y,mins.z},{maxs.x,maxs.y,maxs.z},result)) return false;
    point={float(result[0]),float(result[1]),float(result[2])};return true;
}

static inline std::vector<Target_t> GetTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	std::vector<Target_t> vTargets;

	const Vec3 vLocalPos = F::Ticks.GetShootPos();
    const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();
    const float selectionFOV=Vars::Aimbot::Melee::AimFOV.Value;

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Players)
	{
		auto eGroup = !SDK::FriendlyFire() || Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Team ? EntityEnum::PlayerEnemy : EntityEnum::PlayerAll;
		if (Vars::Aimbot::Melee::WhipTeam.Value &&
			!SDK::FriendlyFire() && SDK::AttribHookValue(0, "speed_buff_ally", pWeapon) > 0)
			eGroup = EntityEnum::PlayerAll;

		for (auto pEntity : H::Entities.GetGroup(eGroup))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
			{ MD::Event("target_filtered",pEntity->entindex()); continue; }

            float flFOVTo=180.f; Vec3 vPos, vAngleTo;
            // The shared helper calculates centres even when its general-FOV
            // result is false. Melee evaluates those outputs against its own cone.
            F::AimbotGlobal.PlayerBoneInFOV(pEntity->As<CTFPlayer>(), vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo);
            if (!MeleeContactPolicy::InFOV(flFOVTo,selectionFOV))
            {
                // A narrow cone can intersect the body without containing any
                // hitbox centre. Admit its view-nearest contact point instead.
                const auto origin=pEntity->GetAbsOrigin();
                if(!MeleeViewPoint(vLocalPos,vLocalAngles,origin+pEntity->m_vecMins(),origin+pEntity->m_vecMaxs(),vPos)
                    || vPos.DistToSqr(vLocalPos)<.0001f)
                {MD::Event("selection_geometry_invalid",pEntity->entindex());continue;}
                vAngleTo=Math::CalcAngle(vLocalPos,vPos);flFOVTo=Math::CalcFov(vLocalAngles,vAngleTo);
                MD::Geometry("selection_bounds",pEntity->entindex(),flFOVTo,vLocalPos.DistTo(vPos),0);
                if(!MeleeContactPolicy::InFOV(flFOVTo,selectionFOV))
                { MD::Event("bone_and_bounds_fov_rejected",pEntity->entindex(),flFOVTo); continue; }
                MD::Event("bounds_fov_recovered",pEntity->entindex(),flFOVTo);
            }

			float flDistTo = vLocalPos.DistToSqr(vPos);
			bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
			int iPriority = F::AimbotGlobal.GetPriority(pEntity->entindex());
			if (bTeam && !SDK::FriendlyFire())
				iPriority = 0;
			iPriority=F::AimbotGlobal.GetPlayerPriority(pEntity->As<CTFPlayer>(),iPriority,EWeaponType::MELEE);
			vTargets.emplace_back(pEntity, TargetEnum::Player, vPos, vAngleTo, flFOVTo, flDistTo, iPriority);
		}
	}

	{
		auto eGroup = EntityEnum::Invalid;
		if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Building)
			eGroup = EntityEnum::BuildingEnemy;
		bool bWrench = pWeapon->GetWeaponID() == TF_WEAPON_WRENCH, bSapper = SDK::AttribHookValue(0, "set_dmg_apply_to_sapper", pWeapon);
		if (Vars::Aimbot::Healing::AutoRepair.Value && (bWrench || bSapper))
			eGroup = eGroup != EntityEnum::Invalid ? EntityEnum::BuildingAll : EntityEnum::BuildingTeam;
		for (auto pEntity : H::Entities.GetGroup(eGroup))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
			if (bTeam && (bWrench && !AimFriendlyBuilding(pEntity->As<CBaseObject>()) || bSapper && !pEntity->As<CBaseObject>()->m_bHasSapper()))
				continue;

			float flFOVTo; Vec3 vPos, vAngleTo;
            F::AimbotGlobal.EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo);
            if (!MeleeContactPolicy::InFOV(flFOVTo,selectionFOV))
				continue;

			int iPriority = 0;
			if (bTeam)
			{
				int iOwner = pEntity->As<CBaseObject>()->m_hBuilder().GetEntryIndex();
				switch (Vars::Aimbot::Healing::HealPriority.Value)
				{
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeFriends:
					if (iOwner == I::EngineClient->GetLocalPlayer() || H::Entities.IsFriend(iOwner) || H::Entities.InParty(iOwner))
						iPriority = std::numeric_limits<int>::max();
					break;
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeTeam:
					iPriority = std::numeric_limits<int>::max();
				}
			}

			float flDistTo = vLocalPos.DistToSqr(vPos);
			vTargets.emplace_back(pEntity, pEntity->IsSentrygun() ? TargetEnum::Sentry : pEntity->IsDispenser() ? TargetEnum::Dispenser : TargetEnum::Teleporter, vPos, vAngleTo, flFOVTo, flDistTo, iPriority);
		}
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::NPCs)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldNPC))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			float flFOVTo; Vec3 vPos, vAngleTo;
            F::AimbotGlobal.EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo);
            if (!MeleeContactPolicy::InFOV(flFOVTo,selectionFOV))
				continue;

			float flDistTo = vLocalPos.DistToSqr(vPos);
			vTargets.emplace_back(pEntity, TargetEnum::NPC, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	return vTargets;
}



static inline int GetSwingTime(CTFWeaponBase* pWeapon, bool bVar = true)
{
	return pWeapon->GetWeaponID() == TF_WEAPON_KNIFE ? 0
		: bVar ? Vars::Aimbot::Melee::SwingTicks.Value
		: ceilf(pWeapon->GetSmackDelay() / TICK_INTERVAL);
}

void CAimbotMelee::UpdateInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, const std::vector<Target_t>& vTargets)
{
	m_mRecordMap.clear(); m_mPaths.clear();
	m_vEyePos = pLocal->GetShootPos();
	m_flRange = pWeapon->GetSwingRange();
	m_iSimulatedTicks = GetSwingTime(pWeapon), m_iSwingTicks = GetSwingTime(pWeapon, false);
	m_iDoubletapTicks = F::Ticks.GetTicks(pWeapon);
	m_bShouldSwing = m_iDoubletapTicks <= m_iSwingTicks || Vars::Doubletap::AntiWarp.Value && pLocal->m_hGroundEntity();
	m_bSimulatedLocal = false;

	if ((!Vars::Aimbot::Melee::SwingPrediction.Value || !m_iSimulatedTicks) && !m_iDoubletapTicks || !G::CanPrimaryAttack || pWeapon->m_flSmackTime() > 0.f)
	{
		m_iSimulatedTicks = m_iSwingTicks = 0;
		return;
	}

	std::unordered_map<int, MoveStorage> mMoveStorage;
	const Vec3 liveEye=m_vEyePos;
	const float liveRange=m_flRange;
	auto fallback=[&]()
	{
		for(auto& storage : mMoveStorage | std::views::values) F::MoveSim.Restore(storage);
		m_mRecordMap.clear(); m_mPaths.clear();
		m_vEyePos=liveEye; m_flRange=liveRange; m_bSimulatedLocal=false;
		m_iSimulatedTicks=m_iSwingTicks=m_iDoubletapTicks=0;
		m_bShouldSwing=G::CanPrimaryAttack;
	};
	auto usable=[](const MoveStorage& storage)
	{
		const auto& p=storage.m_MoveData.m_vecAbsOrigin;
		return MeleePredictionPolicy::Usable(storage.m_bInitialized,storage.m_bFailed,p.x,p.y,p.z);
	};

	F::MoveSim.Initialize(pLocal, mMoveStorage[pLocal->entindex()], false, !m_iDoubletapTicks, false);
	if(!usable(mMoveStorage[pLocal->entindex()]))
	{ MD::Event("local_init_failed_live_fallback"); fallback(); return; }
	for (auto& tTarget : vTargets)
		F::MoveSim.Initialize(tTarget.m_pEntity, mMoveStorage[tTarget.m_pEntity->entindex()], false, true, false);

	int iMax = std::max(m_iSimulatedTicks, m_iDoubletapTicks), iTicks = iMax; bool bSwung = false;
	Vec3 vLocalOrigin = mMoveStorage[pLocal->entindex()].m_MoveData.m_vecAbsOrigin;
	for (int i = 0; i < iTicks; i++) // intended for plocal to collide with targets
	{
		{
			auto& tMoveStorage = mMoveStorage[pLocal->entindex()];
			if (!bSwung && (!m_iDoubletapTicks || Vars::Doubletap::AntiWarp.Value && pLocal->m_hGroundEntity() || iMax - i <= m_iSwingTicks))
			{
				iTicks = std::min(i + m_iSwingTicks, iMax), bSwung = true;
				if (!m_iSwingTicks)
					break;

				if (pLocal->InCond(TF_COND_SHIELD_CHARGE))
				{	// demo charge fix for swing pred
					pLocal->RemoveCond(TF_COND_SHIELD_CHARGE);
					tMoveStorage.m_MoveData.m_flMaxSpeed = tMoveStorage.m_MoveData.m_flClientMaxSpeed = SDK::MaxSpeed(pLocal);
					pLocal->m_flMaxspeed() = tMoveStorage.m_MoveData.m_flMaxSpeed;
				}
			}
			if (m_iDoubletapTicks && Vars::Doubletap::AntiWarp.Value && pLocal->m_hGroundEntity())
				F::Ticks.AntiWarp(pLocal, pCmd->viewangles.y, tMoveStorage.m_MoveData.m_flForwardMove, tMoveStorage.m_MoveData.m_flSideMove, iMax - i - 1);

			F::MoveSim.RunTick(tMoveStorage);
			if(!usable(tMoveStorage))
			{ MD::Event("local_tick_failed_live_fallback"); fallback(); return; }

			vLocalOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
			m_bSimulatedLocal = true;
		}

		if (i < m_iSimulatedTicks - m_iDoubletapTicks)
		{
			for (auto& tTarget : vTargets)
			{
				auto& tMoveStorage = mMoveStorage[tTarget.m_pEntity->entindex()];
				if (tMoveStorage.m_bFailed)
					continue;

				F::MoveSim.RunTick(tMoveStorage);
				if (Vars::Aimbot::Melee::SwingPredictLag.Value && !tMoveStorage.m_bPredictNetworked)
					continue;

				Vec3 vOrigin = Vars::Aimbot::Melee::SwingPredictLag.Value ? tMoveStorage.m_vPredictedOrigin : tMoveStorage.m_MoveData.m_vecAbsOrigin;
				m_mRecordMap[tTarget.m_pEntity->entindex()].emplace_front(
					tTarget.m_pEntity->m_flSimulationTime() + TICKS_TO_TIME(i + 1), vOrigin, tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs()
				);
			}
		}
	}
	m_vEyePos = vLocalOrigin + pLocal->m_vecViewOffset();
	m_flRange = pWeapon->GetSwingRange();

	if (Vars::Visuals::Prediction::SwingLines.Value && Vars::Visuals::Prediction::PlayerPath.Value)
	{
		for (auto& [iIndex, tMoveStorage] : mMoveStorage)
			m_mPaths[iIndex] = tMoveStorage.m_vPath;

		const bool bAlwaysDraw = !Vars::Aimbot::General::AutoShoot.Value || Vars::Debug::Info.Value;
		if (bAlwaysDraw)
		{
			G::LineStorage.clear();
			G::BoxStorage.clear();
			G::PathStorage.clear();

			for (auto& vPath : m_mPaths | std::views::values)
			{
				float flDuration = Vars::Visuals::Prediction::PlayerDrawDuration.Value;
				if (Vars::Colors::PlayerPathIgnoreZ.Value.a)
					G::PathStorage.emplace_back(vPath, !flDuration ? -int(vPath.size()) : I::GlobalVars->curtime + flDuration, Vars::Colors::PlayerPathIgnoreZ.Value, Vars::Visuals::Prediction::PlayerPath.Value);
				if (Vars::Colors::PlayerPath.Value.a)
					G::PathStorage.emplace_back(vPath, !flDuration ? -int(vPath.size()) : I::GlobalVars->curtime + flDuration, Vars::Colors::PlayerPath.Value, Vars::Visuals::Prediction::PlayerPath.Value, true);
			}
		}
	}

	for (auto& tMoveStorage : mMoveStorage | std::views::values)
		F::MoveSim.Restore(tMoveStorage);
}

bool CAimbotMelee::CanBackstab(CBaseEntity* pTarget, CTFPlayer* pLocal, Vec3 vEyeAngles)
{
    if (!pTarget->IsPlayer() || pTarget->m_iTeamNum() == pLocal->m_iTeamNum())
    {MD::Event("backstab_invalid_target",pTarget->entindex());return false;}

	if (Vars::Aimbot::Melee::IgnoreRazorback.Value)
	{
		CUtlVector<CBaseEntity*> itemList;
		int iBackstabShield = SDK::AttribHookValue(0, "set_blockbackstab_once", pTarget, &itemList);
		if (iBackstabShield && itemList.Count())
		{
			CBaseEntity* pEntity = itemList.Element(0);
            if (pEntity && pEntity->ShouldDraw())
            {MD::Event("backstab_razorback",pTarget->entindex());return false;}
		}
	}

	Vec3 vEyePos = m_vEyePos;
	const float flCompDist = PLAYER_ORIGIN_COMPRESSION / 2;
	const float flSqCompDist = 0.0884f;

	if (auto pCmd = G::CurrentUserCmd;
		!m_bSimulatedLocal && pCmd->viewangles != vEyeAngles && G::CanPrimaryAttack)
	{	// repredict, prevent prediction error potentially causing miss
		CUserCmd tOldCmd = *pCmd;
		Vec3 vOldAngles = I::EngineClient->GetViewAngles();
		int iOldAttacking = G::Attacking;
		bool bOldSilent = G::PSilentAngles;
		F::EnginePrediction.End(pLocal, pCmd);

		G::Attacking = true;
		Aim(pCmd, vEyeAngles);
		F::Ticks.Start(pLocal, pCmd);
		vEyePos = pLocal->GetShootPos();
		F::EnginePrediction.End(pLocal, pCmd);

		*pCmd = tOldCmd;
		I::EngineClient->SetViewAngles(vOldAngles);
		G::Attacking = iOldAttacking;
		G::PSilentAngles = bOldSilent;
		F::Ticks.Start(pLocal, pCmd);
	}

	Vec3 vToTarget = (pTarget->GetAbsOrigin() - vEyePos).To2D();
	const float flDist = vToTarget.Normalize();
    if (flDist < flSqCompDist)
    {MD::Event("backstab_overlap",pTarget->entindex(),flDist);return false;}

	const float flExtra = 2.f * flCompDist / flDist; // account for origin compression
	float flPosVsTargetViewMinDot = 0.f + 0.0031f + flExtra;
	float flPosVsOwnerViewMinDot = 0.5f + flExtra;
	float flViewAnglesMinDot = -0.3f + 0.0031f; // 0.00306795676297 ?

	auto fTestDots = [&](Vec3 vTargetAngles)
	{
		Vec3 vOwnerForward; Math::AngleVectors(vEyeAngles, &vOwnerForward);
		vOwnerForward.Normalize2D();

		Vec3 vTargetForward; Math::AngleVectors(vTargetAngles, &vTargetForward);
		vTargetForward.Normalize2D();

		const float flPosVsTargetViewDot = vToTarget.Dot(vTargetForward); // Behind?
		const float flPosVsOwnerViewDot = vToTarget.Dot(vOwnerForward); // Facing?
        const float flViewAnglesDot = vTargetForward.Dot(vOwnerForward); // Facestab?
        MD::Backstab(pTarget->entindex(),flPosVsTargetViewDot,flPosVsOwnerViewDot,flViewAnglesDot,
            flPosVsTargetViewMinDot,flPosVsOwnerViewMinDot,flViewAnglesMinDot);
        if(!(flPosVsTargetViewDot>flPosVsTargetViewMinDot)) {MD::Event("backstab_not_behind",pTarget->entindex());return false;}
        if(!(flPosVsOwnerViewDot>flPosVsOwnerViewMinDot)) {MD::Event("backstab_not_facing",pTarget->entindex());return false;}
        if(!(flViewAnglesDot>flViewAnglesMinDot)) {MD::Event("backstab_alignment_rejected",pTarget->entindex());return false;}
        return true;
	};

	Vec3 vTargetAngles = { 0.f, H::Entities.GetEyeAngles(pTarget->entindex()).y, 0.f };
	if (!(Vars::Aimbot::Melee::BackstabFlags.Value & Vars::Aimbot::Melee::BackstabFlagsEnum::AccountPing))
	{
		if (!fTestDots(vTargetAngles))
			return false;
	}
	else
	{
		if (Vars::Aimbot::Melee::BackstabFlags.Value & Vars::Aimbot::Melee::BackstabFlagsEnum::DoubleTest && !fTestDots(vTargetAngles))
			return false;

		vTargetAngles.y += H::Entities.GetDeltaAngles(pTarget->entindex()).y;
		if (!fTestDots(vTargetAngles))
			return false;
	}

	return true;
}

int CAimbotMelee::CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Unsimulated && H::Entities.GetChoke(tTarget.m_pEntity->entindex()) > Vars::Aimbot::General::TickTolerance.Value)
	{ MD::Event("unsimulated_rejected",tTarget.m_pEntity->entindex()); return false; }

	float flRange = SDK::AttribHookValue(m_flRange, "melee_range_multiplier", pWeapon);
	float flHull = SDK::AttribHookValue(18, "melee_bounds_multiplier", pWeapon);
	if (pLocal->m_flModelScale() > 1.0f)
	{
		flRange *= pLocal->m_flModelScale();
		flHull *= pLocal->m_flModelScale();
	}
	if (pWeapon->GetWeaponID() == TF_WEAPON_WRENCH && tTarget.m_pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
	{
		flRange = 70;
		flHull = 18;
	}
    Vec3 vSwingMins = { -flHull, -flHull, -flHull };
    Vec3 vSwingMaxs = { flHull, flHull, flHull };
    if(!std::isfinite(flRange) || !std::isfinite(flHull) || flRange<=0.f || flHull<0.f)
    {MD::Event("invalid_swing_geometry",tTarget.m_pEntity->entindex());return false;}
	auto& vSimRecords = m_mRecordMap[tTarget.m_pEntity->entindex()];

	std::vector<TickRecord*> vRecords = {};
	if (F::Backtrack.GetRecords(tTarget.m_pEntity, vRecords))
	{
		std::vector<int> vTimeMods = {};
		switch (Vars::Aimbot::Melee::SwingValidateMode.Value)
		{
		case Vars::Aimbot::Melee::SwingValidateModeEnum::Both:
			if (m_iSimulatedTicks != m_iSwingTicks)
				vTimeMods.push_back(m_iSimulatedTicks);
			[[fallthrough]];
		case Vars::Aimbot::Melee::SwingValidateModeEnum::Swing:
			vTimeMods.push_back(m_iSwingTicks);
			break;
		case Vars::Aimbot::Melee::SwingValidateModeEnum::Simulated:
			vTimeMods.push_back(m_iSimulatedTicks);
			break;
		}

		for (auto& tRecord : vSimRecords)
			vRecords.push_back(&tRecord);
		for (auto iTimeMod : vTimeMods)
			vRecords = F::Backtrack.GetValidRecords(vRecords, pLocal, true, -TICKS_TO_TIME(iTimeMod));
		if (vRecords.empty())
		{ MD::Event("no_valid_records",tTarget.m_pEntity->entindex()); return false; }
	}
	else
	{
		F::Backtrack.m_tRecord = { tTarget.m_pEntity->m_flSimulationTime(), tTarget.m_pEntity->m_vecOrigin(), tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs() };
		if (!tTarget.m_pEntity->SetupBones(F::Backtrack.m_tRecord.m_aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, tTarget.m_pEntity->m_flSimulationTime()))
		{ MD::Event("bones_failed",tTarget.m_pEntity->entindex()); return false; }

		vRecords = { &F::Backtrack.m_tRecord };
	}

	CGameTrace trace = {};
    CTraceFilterHitscan filter = {};
    filter.pSkip = pLocal;
    bool aimOnly=false;
    Vec3 aimOnlyPoint,aimOnlyAngle;
    for (auto pRecord : vRecords)
    {
        // Restore recorded entity geometry on every exit, including aim-only
        // fallback; all candidate traces must use the same validated record.
        struct RestoreRecord
        {
            CBaseEntity* entity;Vec3 origin,mins,maxs;
            ~RestoreRecord(){entity->SetAbsOrigin(origin);entity->m_vecMins()=mins;entity->m_vecMaxs()=maxs;}
        } restore{tTarget.m_pEntity,tTarget.m_pEntity->GetAbsOrigin(),tTarget.m_pEntity->m_vecMins(),tTarget.m_pEntity->m_vecMaxs()};
        tTarget.m_pEntity->SetAbsOrigin(pRecord->m_vOrigin);
        tTarget.m_pEntity->m_vecMins() = pRecord->m_vMins + PLAYER_ORIGIN_COMPRESSION;
        tTarget.m_pEntity->m_vecMaxs() = pRecord->m_vMaxs - PLAYER_ORIGIN_COMPRESSION;
        const Vec3 mins=pRecord->m_vOrigin+tTarget.m_pEntity->m_vecMins();
        const Vec3 maxs=pRecord->m_vOrigin+tTarget.m_pEntity->m_vecMaxs();
        Vec3 viewPoint;
        if(!MeleeViewPoint(m_vEyePos,I::EngineClient->GetViewAngles(),mins,maxs,viewPoint))
        {MD::Event("record_geometry_invalid",tTarget.m_pEntity->entindex());continue;}
        const bool backstab=Vars::Aimbot::Melee::AutoBackstab.Value && pWeapon->GetWeaponID()==TF_WEAPON_KNIFE;
        Vec3 nearest=m_vEyePos.Clamp(mins,maxs);
        Vec3 original=nearest;
        if(backstab) original.x=pRecord->m_vOrigin.x,original.y=pRecord->m_vOrigin.y;
        const std::array<Vec3,3> points={viewPoint,original,nearest};
        for(size_t index=0;index<points.size();++index)
        {
            bool duplicate=false;
            for(size_t prior=0;prior<index;++prior) if(points[index].DistToSqr(points[prior])<.0001f) duplicate=true;
            if(duplicate || points[index].DistToSqr(m_vEyePos)<.0001f) continue;
            tTarget.m_vPos=points[index];
            const Vec3 goal=Math::CalcAngle(m_vEyePos,tTarget.m_vPos);
            Aim(G::CurrentUserCmd->viewangles,goal,tTarget.m_vAngleTo);
            const float angle=Math::CalcFov(I::EngineClient->GetViewAngles(),tTarget.m_vAngleTo);
            MD::Geometry("contact",tTarget.m_pEntity->entindex(),angle,m_vEyePos.DistTo(tTarget.m_vPos),flRange,
                I::GlobalVars->curtime-pRecord->m_flSimTime,int(index));
            // The executed swing must also stay inside the separate melee cone
            // after prediction and any smoothing have changed its contact angle.
            if(!MeleeContactPolicy::InFOV(angle,Vars::Aimbot::Melee::AimFOV.Value))
            {MD::Event("angle_rejected",tTarget.m_pEntity->entindex(),angle);continue;}
            Vec3 forward;Math::AngleVectors(tTarget.m_vAngleTo,&forward);
            Vec3 end=m_vEyePos+forward*flRange;
            SDK::TraceHull(m_vEyePos,end,{},{},MASK_SOLID,&filter,&trace);
            bool hit=MeleeTrace::Confirm(tTarget.m_pEntity,m_vEyePos,end,{},{},trace,filter);
            if(!hit)
            {
                SDK::TraceHull(m_vEyePos,end,vSwingMins,vSwingMaxs,MASK_SOLID,&filter,&trace);
                hit=MeleeTrace::Confirm(tTarget.m_pEntity,m_vEyePos,end,vSwingMins,vSwingMaxs,trace,filter);
            }
            const bool contact=hit;
            if(hit && backstab) hit=CanBackstab(tTarget.m_pEntity,pLocal,tTarget.m_vAngleTo);
            if(hit)
            {
                MD::Event("contact_validated",tTarget.m_pEntity->entindex(),float(index));
                tTarget.m_pRecord=pRecord;tTarget.m_bBacktrack=tTarget.m_iTargetType==TargetEnum::Player;
                return true;
            }
            MD::Event(contact?"contact_backstab_rejected":trace.DidHit()?"contact_obstructed":"contact_not_reached",tTarget.m_pEntity->entindex(),trace.fraction);
            const int method=Vars::Aimbot::General::AimType.Value;
            if(!aimOnly && (method==Vars::Aimbot::General::AimTypeEnum::Smooth || method==Vars::Aimbot::General::AimTypeEnum::Assistive)
                && MeleeContactPolicy::InFOV(Math::CalcFov(I::EngineClient->GetViewAngles(),goal),Vars::Aimbot::Melee::AimFOV.Value))
            {
                Math::AngleVectors(goal,&forward);end=m_vEyePos+forward*flRange;
                SDK::TraceHull(m_vEyePos,end,vSwingMins,vSwingMaxs,MASK_SOLID,&filter,&trace);
                if(MeleeTrace::Confirm(tTarget.m_pEntity,m_vEyePos,end,vSwingMins,vSwingMaxs,trace,filter)
                    && (!backstab || CanBackstab(tTarget.m_pEntity,pLocal,goal)))
                {aimOnly=true;aimOnlyPoint=tTarget.m_vPos;aimOnlyAngle=tTarget.m_vAngleTo;}
            }
        }
    }
    if(aimOnly)
    {tTarget.m_vPos=aimOnlyPoint;tTarget.m_vAngleTo=aimOnlyAngle;MD::Event("aim_only_not_aligned",tTarget.m_pEntity->entindex());return 2;}
    return false;
}



bool CAimbotMelee::Aim(const Vec3& vCurAngle, const Vec3& vToAngle, Vec3& vOut, int iMethod)
{
	/*
	if (Vec3* pHoldAngle = F::Ticks.GetShootAngle())
	{
		vOut = *pHoldAngle;
		return true;
	}
	*/

	bool bReturn = false;
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
	case Vars::Aimbot::General::AimTypeEnum::Silent:
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		vOut = vToAngle;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
		vOut = vCurAngle.LerpAngle(vToAngle, Vars::Aimbot::General::AssistStrength.Value / 100.f);
		bReturn = true;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		Vec3 vMouseDelta = G::CurrentUserCmd->viewangles.DeltaAngle(G::LastUserCmd->viewangles);
		Vec3 vTargetDelta = vToAngle.DeltaAngle(G::LastUserCmd->viewangles);
		float flMouseDelta = vMouseDelta.Length2DSqr(), flTargetDelta = vTargetDelta.Length2DSqr();
		vTargetDelta = vTargetDelta.Normalized() * sqrtf(std::min(flMouseDelta, flTargetDelta));
		vOut = vCurAngle - vMouseDelta + vMouseDelta.LerpAngle(vTargetDelta, Vars::Aimbot::General::AssistStrength.Value / 100.f);
		bReturn = true;
		break;
	}

	if (iMethod != Vars::Aimbot::General::AimTypeEnum::Silent || F::AntiCheatCompatibility.Active())
		Math::ClampAngles(vOut);
	return bReturn;
}

// assume angle calculated outside with other overload
void CAimbotMelee::Aim(CUserCmd* pCmd, Vec3& vAngles, int iMethod)
{
	bool bUnsure = F::Ticks.IsTimingUnsure();
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
		if (G::Attacking != 1 && !bUnsure)
			break;
		[[fallthrough]];
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		pCmd->viewangles = vAngles;
		I::EngineClient->SetViewAngles(vAngles);
		break;
	case Vars::Aimbot::General::AimTypeEnum::Silent:
		if (G::Attacking == 1 || bUnsure)
		{
			SDK::FixMovement(pCmd, vAngles);
			pCmd->viewangles = vAngles;
			G::PSilentAngles = true;
		}
		break;
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		SDK::FixMovement(pCmd, vAngles);
		pCmd->viewangles = vAngles;
		G::SilentAngles = true;
	}
}

static inline void DrawVisuals(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, Target_t& tTarget, std::unordered_map<int, std::vector<Vec3>>& mPaths)
{
	G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
	G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };

	bool bPath = Vars::Visuals::Prediction::SwingLines.Value && Vars::Visuals::Prediction::PlayerPath.Value;
	bool bLine = Vars::Visuals::Line::TracersEnabled.Value;
	bool bBoxes = Vars::Visuals::Hitbox::BonesEnabled.Value & Vars::Visuals::Hitbox::BonesEnabledEnum::OnShot;
	bool bRealPath = Vars::Visuals::Prediction::RealPath.Value;
	if (bPath || bLine || bBoxes || bRealPath)
	{
		if (pCmd->buttons & IN_ATTACK && G::CanPrimaryAttack && pWeapon->m_flSmackTime() < 0.f)
		{
			G::LineStorage.clear();
			G::BoxStorage.clear();
			G::PathStorage.clear();

			if (bPath)
			{
				float flDuration = Vars::Visuals::Prediction::PlayerDrawDuration.Value;
				if (Vars::Colors::PlayerPathIgnoreZ.Value.a)
				{
					G::PathStorage.emplace_back(mPaths[pLocal->entindex()], !flDuration ? -int(mPaths[pLocal->entindex()].size()) : I::GlobalVars->curtime + flDuration, Vars::Colors::PlayerPathIgnoreZ.Value, Vars::Visuals::Prediction::PlayerPath.Value);
					G::PathStorage.emplace_back(mPaths[tTarget.m_pEntity->entindex()], !flDuration ? -int(mPaths[tTarget.m_pEntity->entindex()].size()) : I::GlobalVars->curtime + flDuration, Vars::Colors::PlayerPathIgnoreZ.Value, Vars::Visuals::Prediction::PlayerPath.Value);
				}
				if (Vars::Colors::PlayerPath.Value.a)
				{
					G::PathStorage.emplace_back(mPaths[pLocal->entindex()], !flDuration ? -int(mPaths[pLocal->entindex()].size()) : I::GlobalVars->curtime + flDuration, Vars::Colors::PlayerPath.Value, Vars::Visuals::Prediction::PlayerPath.Value, true);
					G::PathStorage.emplace_back(mPaths[tTarget.m_pEntity->entindex()], !flDuration ? -int(mPaths[tTarget.m_pEntity->entindex()].size()) : I::GlobalVars->curtime + flDuration, Vars::Colors::PlayerPath.Value, Vars::Visuals::Prediction::PlayerPath.Value, true);
				}
			}
			if (int iSwingTime = GetSwingTime(pWeapon, false); bRealPath && iSwingTime)
			{
				F::Aimbot.Store(pLocal, iSwingTime);
				F::Aimbot.Store(tTarget.m_pEntity, iSwingTime);
			}
		}
		if (G::Attacking == 1)
		{
			if (bLine)
			{
				Vec3 vEyePos = pLocal->GetShootPos();
				float flDist = vEyePos.DistTo(tTarget.m_vPos);
				Vec3 vForward; Math::AngleVectors(tTarget.m_vAngleTo, &vForward);

				if (Vars::Colors::LineIgnoreZ.Value.a)
					G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vEyePos, vEyePos + vForward * flDist), I::GlobalVars->curtime + Vars::Visuals::Line::DrawDuration.Value, Vars::Colors::LineIgnoreZ.Value);
				if (Vars::Colors::Line.Value.a)
					G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vEyePos, vEyePos + vForward * flDist), I::GlobalVars->curtime + Vars::Visuals::Line::DrawDuration.Value, Vars::Colors::Line.Value, true);
			}
			// simulated (swing pred) records have zeroed bones, don't draw those at the world origin
			if (bBoxes && (tTarget.m_pRecord->m_aBones[0][0][0] || tTarget.m_pRecord->m_aBones[0][1][0] || tTarget.m_pRecord->m_aBones[0][2][0]))
			{
				auto vBoxes = F::Visuals.GetHitboxes(tTarget.m_pRecord->m_aBones, tTarget.m_pEntity->As<CBaseAnimating>());
				G::BoxStorage.insert(G::BoxStorage.end(), vBoxes.begin(), vBoxes.end());

				//if (Vars::Colors::BoneHitboxEdgeIgnoreZ.Value.a || Vars::Colors::BoneHitboxFaceIgnoreZ.Value.a)
				//	G::BoxStorage.emplace_back(tTarget.m_pRecord->m_vOrigin, tTarget.m_pRecord->m_vMins, tTarget.m_pRecord->m_vMaxs, Vec3(), I::GlobalVars->curtime + Vars::Visuals::Hitbox::DrawDuration.Value, Vars::Colors::BoneHitboxEdgeIgnoreZ.Value, Vars::Colors::BoneHitboxFaceIgnoreZ.Value);
				//if (Vars::Colors::BoneHitboxEdge.Value.a || Vars::Colors::BoneHitboxFace.Value.a)
				//	G::BoxStorage.emplace_back(tTarget.m_pRecord->m_vOrigin, tTarget.m_pRecord->m_vMins, tTarget.m_pRecord->m_vMaxs, Vec3(), I::GlobalVars->curtime + Vars::Visuals::Hitbox::DrawDuration.Value, Vars::Colors::BoneHitboxEdge.Value, Vars::Colors::BoneHitboxFace.Value, true);
			}
		}
	}
}

void CAimbotMelee::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	static int iStaticAimType = Vars::Aimbot::General::AimType.Value;
	const int iLastAimType = iStaticAimType;
	const int iRealAimType = Vars::Aimbot::General::AimType.Value;

	if (pWeapon->m_flSmackTime() > 0.f && !iRealAimType && iLastAimType)
		Vars::Aimbot::General::AimType.Value = iLastAimType;
	iStaticAimType = Vars::Aimbot::General::AimType.Value;

	if (F::AimbotGlobal.ShouldHoldAttack(pWeapon))
		pCmd->buttons |= IN_ATTACK;
	if (!Vars::Aimbot::General::AimType.Value
		|| !F::AimbotGlobal.ShouldAim() && pWeapon->m_flSmackTime() < 0.f)
    { MD::Event("aim_inactive"); return; }

    if(!std::isfinite(Vars::Aimbot::Melee::AimFOV.Value) || Vars::Aimbot::Melee::AimFOV.Value<=0.f)
    {MD::Event("melee_fov_disabled");return;}

	if (RunSapper(pLocal, pWeapon, pCmd))
		return;

	auto vTargets = F::AimbotGlobal.ManageTargets(GetTargets, pLocal, pWeapon, Vars::Aimbot::General::TargetSelectionEnum::Distance);
	MD::Event("candidate_count",-1,float(vTargets.size()));
	if (vTargets.empty())
		return;

	//if (!G::AimTarget.m_iEntIndex)
	//	G::AimTarget = { vTargets.front().m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };

    UpdateInfo(pLocal, pWeapon, pCmd, vTargets);
    MD::Event("prediction_ticks",-1,float(m_iSimulatedTicks));
    MD::Event("swing_delay_ticks",-1,float(m_iSwingTicks));
    MD::Event("doubletap_ticks",-1,float(m_iDoubletapTicks));
	for (auto& tTarget : vTargets)
	{
		const auto iResult = CanHit(tTarget, pLocal, pWeapon);
		MD::Event("can_hit_result",tTarget.m_pEntity->entindex(),float(iResult));
		if (!iResult) continue;
		if (iResult == 2)
		{
			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
			Aim(pCmd, tTarget.m_vAngleTo);
			break;
		}

        if (Vars::Aimbot::General::AutoShoot.Value && pWeapon->m_flSmackTime() < 0.f)
		{
			MD::Event(m_bShouldSwing?"swing_allowed":"swing_timing_blocked",tTarget.m_pEntity->entindex(),float(m_iDoubletapTicks));
			if (m_bShouldSwing)
				pCmd->buttons |= IN_ATTACK;
            if (m_iDoubletapTicks)
                F::Ticks.m_bDoubletap = true;
        }
        else MD::Event(Vars::Aimbot::General::AutoShoot.Value?"swing_already_in_progress":"autoshoot_disabled",tTarget.m_pEntity->entindex());
        if(!G::CanPrimaryAttack) MD::Event("weapon_cooldown",tTarget.m_pEntity->entindex());

		G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
		if (G::Attacking == 1)
		{
            if (tTarget.m_bBacktrack)
            {
                pCmd->tick_count = TIME_TO_TICKS(tTarget.m_pRecord->m_flSimTime) + TIME_TO_TICKS(F::Backtrack.GetFakeInterp());
                F::Backtrack.ReportSelection(tTarget.m_pRecord,pCmd,tTarget.m_pEntity->entindex());
            }
			// bug: fast old records seem to be progressively more unreliable ?
		}
		else
		{
			m_vEyePos = pLocal->GetShootPos();
			Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(m_vEyePos, tTarget.m_vPos), tTarget.m_vAngleTo);
		}
		DrawVisuals(pLocal, pWeapon, pCmd, tTarget, m_mPaths);

		Aim(pCmd, tTarget.m_vAngleTo);
		break;
	}
}

static inline int GetAttachment(CBaseObject* pBuilding, int i)
{
	int iAttachment = pBuilding->GetBuildPointAttachmentIndex(i);
	if (pBuilding->IsSentrygun() && pBuilding->m_iUpgradeLevel() > 1)
		iAttachment = 3; // idk why this is needed
	return iAttachment;
}
bool CAimbotMelee::FindNearestBuildPoint(CBaseObject* pBuilding, CTFPlayer* pLocal, Vec3& vPoint)
{
	bool bFoundPoint = false;

	m_vEyePos = pLocal->GetShootPos();
	static auto tf_obj_max_attach_dist = H::ConVars.FindVar("tf_obj_max_attach_dist");
	float flNearestPoint = tf_obj_max_attach_dist->GetFloat();

	for (int i = 0; i < pBuilding->GetNumBuildPoints(); i++)
	{
		Vector vOrigin;
		if (pBuilding->GetAttachment(GetAttachment(pBuilding, i), vOrigin))
		{
			if (!SDK::VisPos(pLocal, pBuilding, m_vEyePos, vOrigin))
				continue;

			float flDist = (vOrigin - pLocal->m_vecOrigin()).Length();
			if (flDist < flNearestPoint)
			{
				flNearestPoint = flDist;
				vPoint = vOrigin;
				bFoundPoint = true;
			}
		}
	}

	return bFoundPoint;
}

bool CAimbotMelee::RunSapper(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (pWeapon->GetWeaponID() != TF_WEAPON_BUILDER)
		return false;

	const Vec3 vLocalPos = F::Ticks.GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	std::vector<Target_t> vTargets;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
			continue;

		auto pBuilding = pEntity->As<CBaseObject>();
		if (pBuilding->m_bHasSapper() || !pBuilding->IsInValidTeam())
			continue;

		Vec3 vPoint;
		if (!FindNearestBuildPoint(pBuilding, pLocal, vPoint))
			continue;

		Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPoint);
		const float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
		const float flDistTo = vLocalPos.DistToSqr(vPoint);
        if (!MeleeContactPolicy::InFOV(flFOVTo,Vars::Aimbot::Melee::AimFOV.Value))
			continue;

		vTargets.emplace_back(pBuilding, TargetEnum::Unknown, vPoint, vAngleTo, flFOVTo, flDistTo);
	}
	F::AimbotGlobal.SortTargetsPre(vTargets, Vars::Aimbot::General::TargetSelectionEnum::Distance);
	if (vTargets.empty())
		return true;

	auto& tTarget = vTargets.front();

	bool bShouldAim = true;
	if (Vars::Aimbot::General::AutoShoot.Value)
		pCmd->buttons |= IN_ATTACK;
	else
		bShouldAim = pCmd->buttons & IN_ATTACK;
	if (Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent)
		bShouldAim &= !I::ClientState->chokedcommands && F::Ticks.CanChoke(true);
		
	if (bShouldAim)
	{
		G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
		G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };

		G::Attacking = true;

		Aim(pCmd->viewangles, Math::CalcAngle(m_vEyePos, tTarget.m_vPos), tTarget.m_vAngleTo);
		tTarget.m_vAngleTo.x = pCmd->viewangles.x; // we don't need to care about pitch
		Aim(pCmd, tTarget.m_vAngleTo);
	}

	return true;
}
