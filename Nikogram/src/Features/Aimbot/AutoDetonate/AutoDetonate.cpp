#include "AutoDetonate.h"
#include "AutoDetonatePolicy.h"
#include "../SelfDamage.h"
#include "../AmmoLifetimeDiagnostics.h"
#include "../AutoDetonateDiagnostics.h"
#include "../AutoDetonateCandidates.h"
namespace AD = AutoDetonateDiagnostics;

void CAutoDetonate::PredictPlayers(CTFPlayer* pLocal, bool predicted, bool bLocal, float localDelay)
{
	if (!m_mRestore.empty())
		RestorePlayers();

	auto& cache=m_PositionCache[bLocal?2:predicted?0:1];
	if(!cache.ready || cache.latency!=localDelay)
	{
		cache.positions.clear(); cache.latency=localDelay; cache.ready=true;
		for(auto entity:H::Entities.GetGroup(EntityEnum::PlayerAll))
		{
			auto player=entity->As<CTFPlayer>();
			if(!player->IsAlive() || player->IsAGhost() || (bLocal?player!=pLocal:player==pLocal)) continue;
			cache.positions.emplace_back(player,SDK::PredictOrigin(bLocal?player->GetAbsOrigin():player->m_vecOrigin(),player->m_vecVelocity(),bLocal?localDelay:(predicted?Lead(player):0.f),true,
				player->m_vecMins()+PLAYER_ORIGIN_COMPRESSION,player->m_vecMaxs()-PLAYER_ORIGIN_COMPRESSION,player->SolidMask()));
		}
	}
	for(const auto& [player,position]:cache.positions)
	{
		m_mRestore.emplace_back(player,player->GetAbsOrigin());
		player->SetAbsOrigin(position);
	}
}

void CAutoDetonate::RestorePlayers()
{
	for (auto& [pEntity, vRestore] : m_mRestore)
		pEntity->SetAbsOrigin(vRestore);
	m_mRestore.clear();
}

float CAutoDetonate::Lead(CBaseEntity* entity)
{
    const float simulation=entity->m_flSimulationTime();
    return F::Backtrack.ProjectileLead(simulation,simulation);
}

bool CAutoDetonate::ValidProjectile(CTFPlayer* local,CBaseEntity* projectile,EntityEnum::EntityEnum group)
{
    if(!projectile||projectile->IsDormant()||(projectile->m_fEffects()&EF_NODRAW))return false;
    if(group==EntityEnum::LocalStickies)
    {
        auto pipe=projectile->As<CTFGrenadePipebombProjectile>();
        return pipe->m_hThrower().Get()==local&&pipe->m_iType()==TF_GL_MODE_REMOTE_DETONATE;
    }
    return projectile->m_hOwnerEntity().Get()==local;
}

bool CAutoDetonate::GetRadius(EntityEnum::EntityEnum eGroup, CBaseEntity* pProjectile, float& flRadius, CTFWeaponBase*& pWeapon, bool safety, float* exposureDelay)
{
    const float now=I::GlobalVars->curtime;
    if(exposureDelay)*exposureDelay=std::max(0.f,m_flSafetyTime-now);
    auto launcher=eGroup==EntityEnum::LocalStickies
        ?pProjectile->As<CTFGrenadePipebombProjectile>()->m_hOriginalLauncher().Get()
        :pProjectile->As<CTFProjectile_Flare>()->m_hLauncher().Get();
    pWeapon=launcher?launcher->As<CTFWeaponBase>():nullptr;
    // An unrelated active weapon cannot supply the missing launcher's attributes.
    if(!pWeapon){AD::Event(AD::NoLauncher);flRadius=NAN;return safety;}
    if(eGroup==EntityEnum::LocalStickies)
    {
        if(pWeapon->GetWeaponID()!=TF_WEAPON_PIPEBOMBLAUNCHER)return false;
        auto pipe=pProjectile->As<CTFGrenadePipebombProjectile>();
        static auto live=H::ConVars.FindVar("tf_grenadelauncher_livetime");
        static auto ramp=H::ConVars.FindVar("tf_sticky_radius_ramp_time");
        static auto air=H::ConVars.FindVar("tf_sticky_airdet_radius");
        if(!live||!ramp||!air){flRadius=NAN;return safety;}
        const float created=pipe->m_flCreationTime(),baseLive=live->GetFloat();
        float arm=SDK::AttribHookValue(baseLive,"sticky_arm_time",pWeapon);
        // No verified game-mode accessor here: targets keep the conservative
        // base arm time. Safety includes the faster native powerup possibility.
        auto owner=pipe->m_hThrower().Get();
        if(safety&&owner)
        {
            auto player=owner->As<CTFPlayer>();
            if(player->InCond(TF_COND_RUNE_HASTE))arm*=.5f;
            else if(player->InCond(TF_COND_RUNE_KING)||player->InCond(TF_COND_KING_BUFFED))arm*=.75f;
        }
        const float at=safety?AutoDetonatePolicy::ExposureTime(now,m_flSafetyTime,created,arm):now;
        if(!AutoDetonatePolicy::ArmData(at,created,arm)){flRadius=NAN;return safety;}
        if(!AutoDetonatePolicy::Armed(at,created,arm))
        {AD::Event(AD::Unarmed,-1,now-created);return false;}
        if(exposureDelay)*exposureDelay=std::max(0.f,at-now);
        float scale=1.f;
        if(!pipe->m_bTouched())
        {
            scale=AutoDetonatePolicy::AirScale(at-created,baseLive,ramp->GetFloat(),air->GetFloat());
            // A flying sticky can land before this estimated exposure. Preserve
            // native maximum radius for safety; target checks retain air ramp.
            if(safety&&at>now)scale=std::max(scale,1.f);
        }
        flRadius*=TF_ROCKET_RADIUS*scale;
    }
    else
    {
        if(pWeapon->GetWeaponID()!=TF_WEAPON_FLAREGUN||pWeapon->As<CTFFlareGun>()->GetFlareGunType()!=FLAREGUN_DETONATE)return false;
        // Unlike Demoman's class detonation, flares require their active launcher.
        if(pWeapon!=H::Entities.GetWeapon())return false;
        flRadius*=TF_FLARE_DET_RADIUS;
    }
    flRadius=SDK::AttribHookValue(flRadius,"mult_explosion_radius",pWeapon);
    if(!std::isfinite(flRadius)||flRadius<=0){flRadius=NAN;return safety;}
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

	for (auto pProjectile : vProjectiles)
	{
		if(!ValidProjectile(pLocal,pProjectile,eGroup))continue;
		if(AD::current) { AD::current->projectile=pProjectile->entindex(); AD::current->phase="arming"; }
		float flRadius = flRadiusScale;
		CTFWeaponBase* pWeapon = nullptr;
		if (!GetRadius(eGroup, pProjectile, flRadius, pWeapon))
			continue;

		PredictPlayers(pLocal, true);
		if(AD::current) AD::current->phase="predicted";
		bool bCheck = CheckEntities(pLocal, pWeapon, nullptr, pProjectile, flRadius, GetOrigin(pProjectile, eGroup, Lead(pProjectile)));
		if(!bCheck) AD::Event(AD::PredictedMiss,-1,Lead(pProjectile));
		if (bCheck)
		{	// only run the current position checks if the predicted ones passed
			PredictPlayers(pLocal, false);
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

	float totalDamage=0.f;
	for (auto pProjectile : vProjectiles)
	{
		if(!ValidProjectile(pLocal,pProjectile,eGroup))continue;
        float flRadius=1.f,flLatency=0.f;
        CTFWeaponBase* pWeapon=nullptr;
        if(!GetRadius(eGroup,pProjectile,flRadius,pWeapon,true,&flLatency))continue;
        if(!std::isfinite(flRadius)||!std::isfinite(flLatency)||!pWeapon)return SelfDamage::Block(pLocal,NAN); // Preserve protection-off semantics; otherwise unknown exposure vetoes.
        const float extra=std::max(0.f,I::GlobalVars->curtime+flLatency-m_flSafetyTime);
        const Vec3 current=pLocal->GetAbsOrigin();
        const Vec3 explosionNow=GetOrigin(pProjectile,eGroup),explosionLater=GetOrigin(pProjectile,eGroup,Lead(pProjectile)+extra);
		float damage=std::max(SelfDamage::Estimate(pLocal,pWeapon,explosionNow,current,flRadius),
			SelfDamage::Estimate(pLocal,pWeapon,explosionLater,current,flRadius));
		PredictPlayers(pLocal, true, true, flLatency);
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
	for(auto& cache:m_PositionCache){cache.positions.clear();cache.ready=false;}
    m_flSafetyTime=AutoDetonatePolicy::SafetyTime(I::GlobalVars->curtime,TICKS_TO_TIME(F::Backtrack.m_iTickCount),
        F::Backtrack.GetReal(FLOW_OUTGOING),TICKS_TO_TIME(F::Backtrack.GetAnticipatedChoke()));
	if (!Vars::Aimbot::Projectile::AutoDetonate.Value)
		return;

	m_vAimPos = std::nullopt;
    if(AD::current)SelfDamageDiagnostics::Write("autodet_timing",std::format(
        "cmd={} now={} clock={} estimated_exposure={} outgoing={} budget={} choke_ticks={} estimate_not_server_confirmation=1",
        pCmd->command_number,I::GlobalVars->curtime,TICKS_TO_TIME(F::Backtrack.m_iTickCount),m_flSafetyTime,
        F::Backtrack.GetReal(FLOW_OUTGOING),F::Backtrack.GetReal(),F::Backtrack.GetAnticipatedChoke()));
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
	for(auto& cache:m_PositionCache){cache.positions.clear();cache.ready=false;} // Never retain cached entity pointers beyond this command.
}
