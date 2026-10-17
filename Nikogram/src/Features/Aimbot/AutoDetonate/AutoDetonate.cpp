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

// --- Smart detonation -------------------------------------------------------
static float SmartLookahead() { return 2 * TICK_INTERVAL; }
static constexpr float StickyMaxWait = .3f, FlareMaxWait = .12f;
static float HullDistance(const Vec3& point, const Vec3& origin, const Vec3& mins, const Vec3& maxs)
{
	const Vec3 delta = point - origin;
	const Vec3 nearest = { std::clamp(delta.x, mins.x, maxs.x), std::clamp(delta.y, mins.y, maxs.y), std::clamp(delta.z, mins.z, maxs.z) };
	return (delta - nearest).Length();
}
// Native radius damage: inside if the hull's nearest point is within the radius;
// damage uses min(distance to centre, distance to origin) with half falloff.
static float BlastAt(const Vec3& source, float base, float radius, const Vec3& origin, const Vec3& mins, const Vec3& maxs)
{
	if (HullDistance(source, origin, mins, maxs) > radius) return 0.f;
	const float distance = std::min(source.DistTo(origin), source.DistTo(origin + (mins + maxs) * .5f));
	return AutoDetonatePolicy::HalfFalloff(base, distance, radius);
}
// Native ApplyToEntity traces explosion -> BodyTarget (world/props only).
static bool BlastVisible(const Vec3& source, const Vec3& spot, CBaseEntity* skip)
{
	CGameTrace trace = {}; CTraceFilterWorldAndPropsOnly filter = {}; filter.pSkip = skip;
	SDK::Trace(source, spot, MASK_SHOT & ~CONTENTS_HITBOX, &filter, &trace);
	return trace.fraction >= 1.f;
}
static bool SmartEnemy(CBaseEntity* entity, CTFPlayer* local, CTFWeaponBase* weapon)
{
	if (entity == local || !entity->IsPlayer()) return false;
	auto player = entity->As<CTFPlayer>();
	if (!player->IsAlive() || player->IsAGhost() || player->IsDormant()) return false;
	if (!SDK::FriendlyFire() && player->m_iTeamNum() == local->m_iTeamNum()) return false;
	if (F::AimbotGlobal.ShouldIgnore(player, local, weapon)) return false;
	if (Vars::Aimbot::Projectile::AutoDetonate.Value & Vars::Aimbot::Projectile::AutoDetonateEnum::IgnoreInvisible
		&& player->IsInvisible(Vars::Aimbot::General::IgnoreInvisible.Value / 100.f)) return false;
	return true;
}

// A wait is continuous while it was renewed within the last 0.1 s (time-based,
// so skipped commands can't reset it and stretch the maximum wait).
float CAutoDetonate::Waited(int key, int)
{
	const float now = I::GlobalVars->curtime;
	for (auto& state : m_aWait)
		if (state.key == key && now - state.last <= .1f && now >= state.since)
			return now - state.since;
	return 0.f;
}

void CAutoDetonate::MarkWait(int key, int, bool waiting)
{
	const float now = I::GlobalVars->curtime;
	WaitState* slot = nullptr;
	for (auto& state : m_aWait)
		if (state.key == key) { slot = &state; break; }
	if (!waiting) { if (slot) *slot = {}; return; }
	if (slot && now - slot->last <= .1f && now >= slot->since) { slot->last = now; return; }
	if (!slot)
	{
		slot = &m_aWait[0];
		for (auto& state : m_aWait)
			if (state.key < 0 || state.last < slot->last) slot = &state;
	}
	*slot = { key, now, now };
}

// All armed stickies go off together (native DetonateStickies), so the decision
// sums every armed sticky's blast per enemy.
bool CAutoDetonate::StickyWait(CTFPlayer* pLocal, float flRadiusScale)
{
	struct Sticky { Vec3 origin; float radius, damage; CBaseEntity* entity; };
	std::vector<Sticky> stickies;
	CTFWeaponBase* pWeapon = nullptr;
	for (auto pProjectile : H::Entities.GetGroup(EntityEnum::LocalStickies))
	{
		if (!ValidProjectile(pLocal, pProjectile, EntityEnum::LocalStickies)) continue;
		float flRadius = flRadiusScale; CTFWeaponBase* pLauncher = nullptr;
		if (!GetRadius(EntityEnum::LocalStickies, pProjectile, flRadius, pLauncher) || !pLauncher) continue;
		// Scottish Resistance detonates the stickies it looks at: keep its timing.
		if (pLauncher->As<CTFPipebombLauncher>()->GetDetonateType() == TF_DETONATE_MODE_DOT) return false;
		auto pipe = pProjectile->As<CTFGrenadePipebombProjectile>();
		float damage = pipe->m_flDamage();
		if (pipe->m_bCritical()) damage *= 3.f;
		if (!std::isfinite(damage) || damage <= 0.f) continue;
		stickies.push_back({ GetOrigin(pProjectile, EntityEnum::LocalStickies, Lead(pProjectile)), flRadius, damage, pProjectile });
		pWeapon = pLauncher;
	}
	if (stickies.empty() || !pWeapon) return false;
	// A building in range is never worth waiting on: detonate as before.
	for (auto entity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		if (!entity || entity->IsDormant()) continue;
		for (const auto& sticky : stickies)
		{
			Vec3 nearest; entity->m_Collision()->CalcNearestPoint(sticky.origin, &nearest);
			if (sticky.origin.DistTo(nearest) <= sticky.radius) return false;
		}
	}

	std::vector<AutoDetonatePolicy::Exposure> exposures;
	for (auto entity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		if (!SmartEnemy(entity, pLocal, pWeapon)) continue;
		auto player = entity->As<CTFPlayer>();
		const Vec3 mins = player->m_vecMins(), maxs = player->m_vecMaxs(), velocity = player->m_vecVelocity();
		const Vec3 now = SDK::PredictOrigin(player->m_vecOrigin(), velocity, Lead(player), true,
			mins + PLAYER_ORIGIN_COMPRESSION, maxs - PLAYER_ORIGIN_COMPRESSION, player->SolidMask());
		const Vec3 next = SDK::PredictOrigin(now, velocity, SmartLookahead(), true,
			mins + PLAYER_ORIGIN_COMPRESSION, maxs - PLAYER_ORIGIN_COMPRESSION, player->SolidMask());
		AutoDetonatePolicy::Exposure exposure;
		for (const auto& sticky : stickies)
		{
			const float a = BlastAt(sticky.origin, sticky.damage, sticky.radius, now, mins, maxs);
			const float b = BlastAt(sticky.origin, sticky.damage, sticky.radius, next, mins, maxs);
			if ((a <= 0.f && b <= 0.f) || !BlastVisible(sticky.origin, now + (mins + maxs) * .5f, sticky.entity)) continue;
			exposure.now += a; exposure.next += b;
		}
		// Conservative lethal estimate (distance/resistance modifiers unknown here).
		exposure.lethal = exposure.now * .9f >= float(player->m_iHealth());
		if (exposure.now > 0.f) exposures.push_back(exposure);
	}
	const int command = G::CurrentUserCmd ? G::CurrentUserCmd->command_number : 0;
	constexpr int key = -2; // the whole sticky group
	const bool wait = AutoDetonatePolicy::Decide(exposures, false, Waited(key, command), StickyMaxWait) == AutoDetonatePolicy::Timing::Wait;
	MarkWait(key, command, wait);
	if (wait) AD::Event(AD::SmartWait, -1, Waited(key, command));
	return wait;
}

// Detonator: hold for a coming direct hit (full damage, burning mini-crit);
// otherwise go off at the flare's closest approach or before it hits the world.
bool CAutoDetonate::FlareWait(CTFPlayer* pLocal, CBaseEntity* pFlare, float flRadius)
{
	const Vec3 origin = GetOrigin(pFlare, EntityEnum::LocalFlares, Lead(pFlare));
	const Vec3 velocity = pFlare->GetAbsVelocity(), next = origin + velocity * SmartLookahead();
	CGameTrace world = {}; CTraceFilterWorldAndPropsOnly filter = {}; filter.pSkip = pFlare;
	SDK::Trace(origin, next, MASK_SHOT, &filter, &world);
	const bool worldSoon = world.DidHit() || world.startsolid;
	std::vector<AutoDetonatePolicy::Exposure> exposures;
	for (auto entity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		if (!SmartEnemy(entity, pLocal, H::Entities.GetWeapon())) continue;
		auto player = entity->As<CTFPlayer>();
		const Vec3 mins = player->m_vecMins(), maxs = player->m_vecMaxs(), playerVelocity = player->m_vecVelocity();
		const Vec3 now = SDK::PredictOrigin(player->m_vecOrigin(), playerVelocity, Lead(player), true,
			mins + PLAYER_ORIGIN_COMPRESSION, maxs - PLAYER_ORIGIN_COMPRESSION, player->SolidMask());
		const Vec3 then = now + playerVelocity * SmartLookahead();
		AutoDetonatePolicy::Exposure exposure;
		exposure.now = BlastAt(origin, 1.f, flRadius, now, mins, maxs);
		exposure.next = BlastAt(next, 1.f, flRadius, then, mins, maxs);
		if (exposure.now > 0.f && !BlastVisible(origin, now + (mins + maxs) * .5f, pFlare)) exposure.now = 0.f;
		// The flare's path enters the target's (moving) hull before the lookahead ends.
		for (int step = 1; step <= 4 && !exposure.directSoon; ++step)
		{
			const float f = step / 4.f;
			// Only if the target is reached before any world contact on the way.
			if (worldSoon && world.fraction < f) break;
			exposure.directSoon = HullDistance(origin + (next - origin) * f, now + (then - now) * f, mins, maxs) <= 1.f;
		}
		if (exposure.now > 0.f || exposure.directSoon) exposures.push_back(exposure);
	}
	const int command = G::CurrentUserCmd ? G::CurrentUserCmd->command_number : 0;
	const int key = pFlare->entindex();
	const bool direct = std::any_of(exposures.begin(), exposures.end(), [](const auto& e) { return e.directSoon; });
	// Hitting the world first ends the flare: detonate now unless a direct hit comes first.
	const bool wait = AutoDetonatePolicy::Decide(exposures, worldSoon && !direct, Waited(key, command), FlareMaxWait) == AutoDetonatePolicy::Timing::Wait;
	MarkWait(key, command, wait);
	if (wait) AD::Event(AD::SmartWait, pFlare->entindex(), Waited(key, command));
	return wait;
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
		{
			// Someone is in range: is this the best moment, or are they still coming closer?
			if (eGroup == EntityEnum::LocalStickies ? StickyWait(pLocal, flRadiusScale) : FlareWait(pLocal, pProjectile, flRadius))
			{
				m_vAimPos.reset();
				if (eGroup == EntityEnum::LocalStickies) return false; // one decision for the whole group
				continue;
			}
			return true;
		}
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
