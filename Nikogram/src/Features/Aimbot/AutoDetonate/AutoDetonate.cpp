#include "AutoDetonate.h"
#include "../SelfDamage.h"
#include "../AmmoLifetimeDiagnostics.h"
#include "../AutoDetonateDiagnostics.h"
#include "../AutoDetonateCandidates.h"
namespace AD = AutoDetonateDiagnostics;

void CAutoDetonate::PredictPlayers(CTFPlayer* pLocal, float flLatency, bool bLocal)
{
	if (!m_mRestore.empty())
		RestorePlayers();

	auto& cache=m_PositionCache[bLocal?2:flLatency==0.f?1:0];
	if(!cache.ready || cache.latency!=flLatency)
	{
		cache.positions.clear(); cache.latency=flLatency; cache.ready=true;
		for(auto entity:H::Entities.GetGroup(EntityEnum::PlayerAll))
		{
			auto player=entity->As<CTFPlayer>();
			if(!player->IsAlive() || player->IsAGhost() || (bLocal?player!=pLocal:player==pLocal)) continue;
			cache.positions.emplace_back(player,SDK::PredictOrigin(player->m_vecOrigin(),player->m_vecVelocity(),flLatency,true,
				player->m_vecMins()+PLAYER_ORIGIN_COMPRESSION,player->m_vecMaxs()-PLAYER_ORIGIN_COMPRESSION,player->SolidMask()));
		}
	}
	for(const auto& [player,position]:cache.positions)
	{
		m_mRestore[player]=player->GetAbsOrigin();
		player->SetAbsOrigin(position);
	}
}

void CAutoDetonate::RestorePlayers()
{
	for (auto& [pEntity, vRestore] : m_mRestore)
		pEntity->SetAbsOrigin(vRestore);
	m_mRestore.clear();
}

bool CAutoDetonate::GetRadius(EntityEnum::EntityEnum eGroup, CBaseEntity* pProjectile, float& flRadius, CTFWeaponBase*& pWeapon)
{
	if (eGroup == EntityEnum::LocalStickies)
		pWeapon = pProjectile->As<CTFGrenadePipebombProjectile>()->m_hOriginalLauncher()->As<CTFWeaponBase>();
	else
		pWeapon = pProjectile->As<CTFProjectile_Flare>()->m_hLauncher()->As<CTFWeaponBase>();
	if (!pWeapon)
	{
		pWeapon = H::Entities.GetWeapon();
		if (!pWeapon)
		{ AD::Event(AD::NoLauncher); return false; }
	}

	if (eGroup == EntityEnum::LocalStickies)
	{
		auto pPipebomb = pProjectile->As<CTFGrenadePipebombProjectile>();
		if (!pPipebomb->m_flCreationTime() || I::GlobalVars->curtime < pPipebomb->m_flCreationTime() + SDK::AttribHookValue(0.8f, "sticky_arm_time", pWeapon))
		{ AD::Event(AD::Unarmed,-1,I::GlobalVars->curtime-pPipebomb->m_flCreationTime()); return false; }

		flRadius *= TF_ROCKET_RADIUS;
		if (!pPipebomb->m_bTouched())
		{
			static auto tf_grenadelauncher_livetime = H::ConVars.FindVar("tf_grenadelauncher_livetime");
			static auto tf_sticky_radius_ramp_time = H::ConVars.FindVar("tf_sticky_radius_ramp_time");
			static auto tf_sticky_airdet_radius = H::ConVars.FindVar("tf_sticky_airdet_radius");
			float flLiveTime = tf_grenadelauncher_livetime->GetFloat();
			float flRampTime = tf_sticky_radius_ramp_time->GetFloat();
			float flAirdetRadius = tf_sticky_airdet_radius->GetFloat();
			flRadius *= Math::RemapVal(I::GlobalVars->curtime - pPipebomb->m_flCreationTime(), flLiveTime, flLiveTime + flRampTime, flAirdetRadius, 1.f);
		}
	}
	else
		flRadius *= TF_FLARE_DET_RADIUS;
	flRadius = SDK::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);
	return true;
}

Vec3 CAutoDetonate::GetOrigin(CBaseEntity* pProjectile, EntityEnum::EntityEnum eGroup, float flLatency)
{
	Vec3 vOrigin = SDK::PredictOrigin(pProjectile->m_vecOrigin(), pProjectile->GetAbsVelocity(), flLatency);

	if (eGroup == EntityEnum::LocalStickies)
	{	// why is this even a thing?
		CGameTrace trace = {};
		CTraceFilterWorldAndPropsOnly filter = {};

		SDK::Trace(vOrigin + Vec3(0, 0, 8), vOrigin - Vec3(0, 0, 24), MASK_SHOT_HULL, &filter, &trace);
		if (trace.fraction != 1.0)
			return trace.endpos + trace.plane.normal;
	}

	return vOrigin;
}

bool CAutoDetonate::CheckEntity(CBaseEntity* pEntity, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, CBaseEntity* pProjectile, float flRadius, Vec3 vOrigin)
{
	// CEntitySphereQuery actually does a box test so we need to make sure the distance is less than the radius first
	Vec3 vPos; pEntity->m_Collision()->CalcNearestPoint(vOrigin, &vPos);
	float flRadiusSqr = powf(flRadius, 2);
	if (vOrigin.DistToSqr(vPos) > flRadiusSqr)
	{ AD::Event(AD::Range,pEntity->entindex(),vOrigin.DistTo(vPos)); return false; }

	if (pEntity != pLocal
		? !SDK::VisPosCollideable(pProjectile, pEntity, vOrigin, pEntity->IsPlayer() ? pEntity->GetAbsOrigin() + pEntity->As<CTFPlayer>()->GetViewOffset() : pEntity->GetCenter(), MASK_SHOT)
		: !SDK::VisPosWorld(pProjectile, pEntity, vOrigin, pEntity->GetAbsOrigin() + pEntity->As<CTFPlayer>()->m_vecViewOffset(), MASK_SHOT))
	{ AD::Event(AD::Visibility,pEntity->entindex()); return false; }

	if (pCmd && pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER && pWeapon->As<CTFPipebombLauncher>()->GetDetonateType() == TF_DETONATE_MODE_DOT)
	{
		if (G::Attacking == 1 || I::ClientState->chokedcommands)
		{ AD::Event(AD::SelectiveBusy,pEntity->entindex()); return false; }

		m_vAimPos = vOrigin;
	}

	AD::Event(AD::Accepted,pEntity->entindex());
	return true;
}

bool CAutoDetonate::CheckEntities(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, CBaseEntity* pProjectile, float flRadius, Vec3 vOrigin)
{
	flRadius -= 1;
	int enumerated=0, directPlayers=0;
	auto checkCandidate = [&](CBaseEntity* pEntity)
	{
		// Preserve short-circuit order; filter values: local/dead/team/general-ignore.
		if (pEntity == pLocal) { AD::Event(AD::Filtered,pEntity->entindex(),1); return false; }
		if (pEntity->IsPlayer() && (!pEntity->As<CTFPlayer>()->IsAlive() || pEntity->As<CTFPlayer>()->IsAGhost()))
		{ AD::Event(AD::Filtered,pEntity->entindex(),2); return false; }
		if (!SDK::FriendlyFire() && pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
		{ AD::Event(AD::Filtered,pEntity->entindex(),3); return false; }
		if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
		{ AD::Event(AD::Filtered,pEntity->entindex(),4); return false; }

		if (Vars::Aimbot::Projectile::AutoDetonate.Value & Vars::Aimbot::Projectile::AutoDetonateEnum::IgnoreInvisible && pEntity->IsPlayer() && pEntity->As<CTFPlayer>()->IsInvisible(Vars::Aimbot::General::IgnoreInvisible.Value / 100.f))
		{ AD::Event(AD::Invisible,pEntity->entindex()); return false; }

		return CheckEntity(pEntity, pLocal, pWeapon, pCmd, pProjectile, flRadius, vOrigin);
	};
	// PredictPlayers changes player bounds before both passes. Read those bounds
	// directly instead of relying on a possibly stale spatial-partition query.
	if (AutoDetonateCandidates::CheckPlayers(H::Entities.GetGroup(EntityEnum::PlayerAll), [&](CBaseEntity* player)
		{ ++directPlayers; return checkCandidate(player); }))
	{ AD::Query(pLocal,vOrigin,flRadius,enumerated,directPlayers); return true; }

	CBaseEntity* pEntity;
	for (CEntitySphereQuery sphere(vOrigin, flRadius);
		pEntity = sphere.GetCurrentEntity(); sphere.NextEntity())
	{
		++enumerated;
		if (pEntity->IsPlayer()) continue; // Already checked through the direct list.
		if (checkCandidate(pEntity))
		{ AD::Query(pLocal,vOrigin,flRadius,enumerated,directPlayers); return true; }
	}

	AD::Query(pLocal,vOrigin,flRadius,enumerated,directPlayers);
	return false;
}

bool CAutoDetonate::CheckTargets(CTFPlayer* pLocal, EntityEnum::EntityEnum eGroup, float flRadiusScale, CUserCmd* pCmd)
{
	auto& vProjectiles = H::Entities.GetGroup(eGroup);
	if (vProjectiles.empty())
	{ AD::Event(AD::Empty); return false; }

	float flLatency = F::Backtrack.GetReal();
	for (auto pProjectile : vProjectiles)
	{
		if(AD::current) { AD::current->projectile=pProjectile->entindex(); AD::current->phase="arming"; }
		float flRadius = flRadiusScale;
		CTFWeaponBase* pWeapon = nullptr;
		if (!GetRadius(eGroup, pProjectile, flRadius, pWeapon))
			continue;

		PredictPlayers(pLocal, flLatency);
		if(AD::current) AD::current->phase="predicted";
		bool bCheck = CheckEntities(pLocal, pWeapon, nullptr, pProjectile, flRadius, GetOrigin(pProjectile, eGroup, flLatency));
		if(!bCheck) AD::Event(AD::PredictedMiss,-1,flLatency);
		if (bCheck)
		{	// only run the current position checks if the predicted ones passed
			PredictPlayers(pLocal, 0.f);
			if(AD::current) AD::current->phase="current";
			bCheck = CheckEntities(pLocal, pWeapon, pCmd, pProjectile, flRadius, GetOrigin(pProjectile, eGroup));
			if(!bCheck) AD::Event(AD::CurrentMiss);
		}
		RestorePlayers();
		if (bCheck)
			return true;
	}

	return false;
}

bool CAutoDetonate::CheckSelf(CTFPlayer* pLocal, EntityEnum::EntityEnum eGroup)
{
	if (!(Vars::Aimbot::Projectile::AutoDetonate.Value & Vars::Aimbot::Projectile::AutoDetonateEnum::PreventSelfDamage) || !pLocal->IsAlive() || pLocal->IsAGhost() || pLocal->IsInvulnerable())
		return false;

	auto& vProjectiles = H::Entities.GetGroup(eGroup);
	if (vProjectiles.empty())
		return false;

	float flLatency = F::Backtrack.GetReal();

	float totalDamage=0.f;
	for (auto pProjectile : vProjectiles)
	{
		float flRadius = 1.f;
		CTFWeaponBase* pWeapon = nullptr;
		if (!GetRadius(eGroup, pProjectile, flRadius, pWeapon))
			continue;

		const Vec3 current=pLocal->GetAbsOrigin();
		const Vec3 explosionNow=GetOrigin(pProjectile,eGroup), explosionLater=GetOrigin(pProjectile,eGroup,flLatency);
		float damage=std::max(SelfDamage::Estimate(pLocal,pWeapon,explosionNow,current,flRadius),
			SelfDamage::Estimate(pLocal,pWeapon,explosionLater,current,flRadius));
		PredictPlayers(pLocal, flLatency, true);
		damage=std::max(damage,SelfDamage::Estimate(pLocal,pWeapon,explosionLater,pLocal->GetAbsOrigin(),flRadius));
		RestorePlayers();
		totalDamage+=damage;
		if(SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("autodet_self",std::format("projectile={} damage={} total={} protection={} blocked={}",pProjectile->entindex(),damage,totalDamage,Vars::Aimbot::Projectile::SelfDamageProtection.Value,SelfDamage::Block(pLocal,totalDamage)),true);
		if (SelfDamage::Block(pLocal,totalDamage))
			return true;
	}

	return false;
}

bool CAutoDetonate::Check(CTFPlayer* pLocal, CUserCmd* pCmd, EntityEnum::EntityEnum eGroup, int iFlag)
{
	if(AD::current) { AD::current->source=iFlag; AD::current->projectile=-1; AD::current->phase="source"; }
	if (!(Vars::Aimbot::Projectile::AutoDetonate.Value & iFlag))
	{ AD::Event(AD::Disabled); return false; }

	if(!CheckTargets(pLocal, eGroup, Vars::Aimbot::Projectile::AutodetRadius.Value / 100, pCmd)) return false;
	if(AD::current) AD::current->phase="self";
	if(CheckSelf(pLocal,eGroup)) { m_vAimPos.reset(); AD::Event(AD::SelfBlocked); return false; }
	return true;
}

void CAutoDetonate::Run(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	m_PositionCache={};
	if (!Vars::Aimbot::Projectile::AutoDetonate.Value)
		return;

	m_vAimPos = std::nullopt;
	const bool stickyRequest = Check(pLocal, pCmd, EntityEnum::LocalStickies, Vars::Aimbot::Projectile::AutoDetonateEnum::Stickies);
	const bool flareRequest = !stickyRequest && Check(pLocal, pCmd, EntityEnum::LocalFlares, Vars::Aimbot::Projectile::AutoDetonateEnum::Flares);
	if (stickyRequest || flareRequest)
	{
		AD::Event(AD::Requested);
		pCmd->buttons |= IN_ATTACK2;
		AmmoLifetimeDiagnostics::AutoRequest(pCmd->command_number, stickyRequest ? 1 : 2); // 1: sticky, 2: flare

		if (m_vAimPos)
		{
			Vec3 vAngleTo = Math::CalcAngle(pLocal->GetShootPos(), *m_vAimPos);
			SDK::FixMovement(pCmd, vAngleTo);
			pCmd->viewangles = vAngleTo;
			G::PSilentAngles = true;
		}
	}
	m_PositionCache={}; // Never retain cached entity pointers beyond this command.
}
