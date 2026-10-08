#include "AimbotProjectile.h"
#include "../SmoothAim.h"
#include "../SplashSearchPolicy.h"
#include "../SplashWorker.h"
#include "../TracePreparation.h"
#include <random>
#include "../TargetPolicy.h"
#include "../SelfDamage.h"
#include "../RocketSafetyGeometry.h"
#include "../GrenadeCollisionPolicy.h"
#include "../BowChargePolicy.h"
#include "../ArcPolicy.h"
#include "../AimbotAuditPolicy.h"
#include "../ProjectileAimPolicy.h"
#include "../AutoViewmodelSwitch.h"

#include "../Aimbot.h"
#include "../AutoFlarePunch.h"
#include "../ProjectileDiagnostics.h"
#include "../AmmoEvidenceDiagnostics.h"
#include "../AmmoConservation.h"
#include "../PredictionObservation.h"
#include "../LeadRestrictPolicy.h"
#include "../../Ticks/Ticks.h"
#include "../../EnginePrediction/EnginePrediction.h"
#include "../../LearningAccess.h"
#include "../../World/World.h"
#include "../AutoAirblast/AutoAirblast.h"
#include "../../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include <numeric>
using ProjectilePerformancePolicy::SearchAllowed;
using ProjectilePerformancePolicy::SearchStep;
using ProjectilePerformancePolicy::SearchWork;
#ifdef NIKOGRAM_PRIVATE_LEARNING
#include <chrono>
#include "../../../Private/Learning/ProjectileReplay.h"
#include "../../../Private/Learning/CollisionParity.h"
#include "../../../Private/Learning/FullPathParity.h"
#endif

//#define SPLASH_DEBUG1 // trace splash visualization
//#define SPLASH_DEBUG2 // plane splash visualization
//#define SPLASH_DEBUG3 // points visualization
//#define SPLASH_DEBUG4 // test visualization
//#define SPLASH_DEBUG5 // trace/face count

#ifdef SPLASH_DEBUG5
static std::map<std::string, int> s_mTraceCount = {};
#endif
#ifdef SPLASH_DEBUG2
//#include "../../Debug/Debug.h"
#endif

static inline std::vector<Target_t> GetTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	std::vector<Target_t> vTargets;

	const Vec3 vLocalPos = F::Ticks.GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	{
		auto eGroup = EntityEnum::Invalid;
		if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Players)
			eGroup = !SDK::FriendlyFire() || Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Team ? EntityEnum::PlayerEnemy : EntityEnum::PlayerAll;
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_CROSSBOW:
			if (Vars::Aimbot::Healing::AutoArrow.Value)
				eGroup = eGroup != EntityEnum::Invalid ? EntityEnum::PlayerAll : EntityEnum::PlayerTeam;
			break;
		case TF_WEAPON_LUNCHBOX:
			if (Vars::Aimbot::Healing::AutoSandvich.Value)
				eGroup = EntityEnum::PlayerTeam;
			break;
		}
		bool bHeal = pWeapon->GetWeaponID() == TF_WEAPON_CROSSBOW || pWeapon->GetWeaponID() == TF_WEAPON_LUNCHBOX;
		int iFunctionFlags = ShouldIgnoreEnum::Dormant | ShouldIgnoreEnum::Ignored;
		if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::TargetDormant)
			iFunctionFlags &= ~ShouldIgnoreEnum::Dormant;

		for (auto pEntity : H::Entities.GetGroup(eGroup))
		{
			if (F::AutoFlarePunch.Target()>=0 && pEntity->entindex()!=F::AutoFlarePunch.Target()) continue;
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon, iFunctionFlags))
				continue;

			bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
			if (bTeam && bHeal)
			{
				if (pEntity->As<CTFPlayer>()->m_iHealth() >= pEntity->As<CTFPlayer>()->GetMaxHealth()
					|| Vars::Aimbot::Healing::HealPriority.Value == Vars::Aimbot::Healing::HealPriorityEnum::FriendsOnly && !H::Entities.IsFriend(pEntity->entindex()) && !H::Entities.InParty(pEntity->entindex()))
					continue;
			}

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.PlayerBoneInFOV(pEntity->As<CTFPlayer>(), vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
				continue;

			float flDistTo = vLocalPos.DistToSqr(vPos);
			int iPriority = F::AimbotGlobal.GetPriority(pEntity->entindex());
			if (bTeam && bHeal)
			{
				iPriority = 0;
				switch (Vars::Aimbot::Healing::HealPriority.Value)
				{
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeFriends:
					if (H::Entities.IsFriend(pEntity->entindex()) || H::Entities.InParty(pEntity->entindex()))
						iPriority = std::numeric_limits<int>::max();
					break;
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeTeam:
					iPriority = std::numeric_limits<int>::max();
				}
			}
			iPriority=F::AimbotGlobal.GetPlayerPriority(pEntity->As<CTFPlayer>(),iPriority,EWeaponType::PROJECTILE,!bTeam||bHeal);
			vTargets.emplace_back(pEntity, TargetEnum::Player, vPos, vAngleTo, flFOVTo, flDistTo, iPriority);
		}

		if (pWeapon->GetWeaponID() == TF_WEAPON_LUNCHBOX || F::AutoFlarePunch.Target()>=0)
			return vTargets;
	}

	{
		auto eGroup = EntityEnum::Invalid;
		if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Building)
			eGroup = EntityEnum::BuildingEnemy;
		if (Vars::Aimbot::Healing::AutoRepair.Value && pWeapon->GetWeaponID() == TF_WEAPON_SHOTGUN_BUILDING_RESCUE)
			eGroup = eGroup != EntityEnum::Invalid ? EntityEnum::BuildingAll : EntityEnum::BuildingTeam;
		for (auto pEntity : H::Entities.GetGroup(eGroup))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
			if (bTeam && (pEntity->As<CBaseObject>()->m_iHealth() >= pEntity->As<CBaseObject>()->m_iMaxHealth() || pEntity->As<CBaseObject>()->m_bBuilding()))
				continue;

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
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

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Stickies)
	{
		bool bShouldAim = false;
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_PIPEBOMBLAUNCHER:
			if (SDK::AttribHookValue(0, "stickies_detonate_stickies", pWeapon) == 1)
				bShouldAim = true;
			break;
		case TF_WEAPON_FLAREGUN:
		case TF_WEAPON_FLAREGUN_REVENGE:
			if (pWeapon->As<CTFFlareGun>()->GetFlareGunType() == FLAREGUN_SCORCHSHOT)
				bShouldAim = true;
		}

		if (bShouldAim)
		{
			for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
			{
				if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
					continue;

				float flFOVTo; Vec3 vPos, vAngleTo;
				if (!F::AimbotGlobal.EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
					continue;

				float flDistTo = vLocalPos.DistToSqr(vPos);
				vTargets.emplace_back(pEntity, TargetEnum::Sticky, vPos, vAngleTo, flFOVTo, flDistTo);
			}
		}
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::NPCs) // does not predict movement
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldNPC))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
				continue;

			float flDistTo = vLocalPos.DistToSqr(vPos);
			vTargets.emplace_back(pEntity, TargetEnum::NPC, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	return vTargets;
}



float CAimbotProjectile::GetSplashRadius(CTFWeaponBase* pWeapon, CTFPlayer* pPlayer, float flScale)
{
	float flRadius = 0.f;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
		flRadius = TF_ROCKET_RADIUS;
		break;
	case TF_WEAPON_FLAREGUN:
	case TF_WEAPON_FLAREGUN_REVENGE:
		if (pWeapon->As<CTFFlareGun>()->GetFlareGunType() == FLAREGUN_SCORCHSHOT)
			flRadius = TF_FLARE_DET_RADIUS;
		break;
	case TF_WEAPON_JAR:
	case TF_WEAPON_JAR_MILK:
	case TF_WEAPON_JAR_GAS:
		return JAR_EXPLODE_RADIUS * flScale;
	}
	if (!flRadius)
		return 0.f;

	flRadius = SDK::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
		if (pPlayer->InCond(TF_COND_BLASTJUMPING) && SDK::AttribHookValue(1.f, "rocketjump_attackrate_bonus", pWeapon) != 1.f)
			flRadius *= 0.8f;
	}
	return flRadius * flScale;
}

float CAimbotProjectile::GetSplashRadius(CBaseEntity* pProjectile, CTFWeaponBase* pWeapon, CTFPlayer* pPlayer, float flScale)
{
	float flRadius = 0.f;
	switch (pProjectile->GetClassID())
	{
	case ETFClassID::CTFWeaponBaseGrenadeProj:
	case ETFClassID::CTFWeaponBaseMerasmusGrenade:
	case ETFClassID::CTFProjectile_Rocket:
	case ETFClassID::CTFProjectile_SentryRocket:
	case ETFClassID::CTFProjectile_EnergyBall:
		flRadius = TF_ROCKET_RADIUS;
		break;
	case ETFClassID::CTFGrenadePipebombProjectile:
		if (pProjectile->As<CTFGrenadePipebombProjectile>()->HasStickyEffects())
			flRadius = TF_ROCKET_RADIUS;
		break;
	case ETFClassID::CTFProjectile_Flare:
		if (pWeapon && pWeapon->As<CTFFlareGun>()->GetFlareGunType() == FLAREGUN_SCORCHSHOT)
			flRadius = TF_FLARE_DET_RADIUS;
		break;
	case ETFClassID::CTFProjectile_Jar:
	case ETFClassID::CTFProjectile_JarMilk:
	case ETFClassID::CTFProjectile_JarGas:
		return JAR_EXPLODE_RADIUS * flScale;
	}
	if (pPlayer && pWeapon)
	{
		flRadius = SDK::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);
		switch (pProjectile->GetClassID())
		{
		case ETFClassID::CTFProjectile_Rocket:
		case ETFClassID::CTFProjectile_SentryRocket:
			if (pPlayer->InCond(TF_COND_BLASTJUMPING) && SDK::AttribHookValue(1.f, "rocketjump_attackrate_bonus", pWeapon) != 1.f)
				flRadius *= 0.8f;
		}
	}
	return flRadius * flScale;
}

static inline float ArmTime(CTFWeaponBase* pWeapon)
{
	if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::UseArmTime && pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
	{
		static auto tf_grenadelauncher_livetime = H::ConVars.FindVar("tf_grenadelauncher_livetime");
		const float flLiveTime = tf_grenadelauncher_livetime->GetFloat();
		return SDK::AttribHookValue(flLiveTime, "sticky_arm_time", pWeapon);
	}

	return 0.f;
}

static inline bool ShouldLob(Info_t& tInfo)
{
	return ArcPolicy::Eligible(bool(Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::LobAngles),tInfo.m_flGravity);
}

static inline bool ShouldLob(MoveStorage& tMoveStorage, Info_t& tInfo)
{
	return ShouldLob(tInfo); // Eligibility is independent of splash radius and underprediction.
}

static inline bool AirSplash(CTFWeaponBase* pWeapon, Info_t& tInfo)
{
	if (!(Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::AirSplash) || tInfo.m_pProjectile)
		return false;

	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_FLAREGUN:
		return true;
	}

	return false;
}

static bool BodyProjectile(CTFWeaponBase* weapon)
{
    if(!weapon) return false;
    switch(weapon->GetWeaponID())
    {
    case TF_WEAPON_CROSSBOW:
    case TF_WEAPON_FLAREGUN:
    case TF_WEAPON_FLAREGUN_REVENGE: return true;
    }
    return false;
}
static bool DirectHitProjectile(const Info_t& info)
{
    return info.m_pWeapon && (info.m_pWeapon->GetWeaponID()==TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT
        || info.m_pWeapon->m_iItemDefinitionIndex()==Soldier_m_TheDirectHit);
}
static int EffectiveSplash(const Info_t& info)
{
    int mode=Vars::Aimbot::Projectile::SplashPrediction.Value;
    if (Vars::Aimbot::Projectile::PrioritizeUbered.Value && info.m_pTarget && info.m_pTarget->m_pEntity->IsPlayer()
        && F::AimbotGlobal.IsUberTarget(info.m_pTarget->m_pEntity->As<CTFPlayer>()) && F::AimbotGlobal.CanDisplaceUber(info.m_pWeapon))
        mode=Vars::Aimbot::Projectile::SplashPredictionEnum::Prefer;
    return ProjectileAimPolicy::Splash(mode,DirectHitProjectile(info),
        bool(Vars::Aimbot::Projectile::Modifiers.Value&Vars::Aimbot::Projectile::ModifiersEnum::AllowDirectHitSplash));
}
static_assert(ProjectileAimPolicy::TrueFeet==Vars::Aimbot::Projectile::HitboxesEnum::TruePrioritizeFeet);
static_assert(ProjectileAimPolicy::SplashInclude==Vars::Aimbot::Projectile::SplashPredictionEnum::Include);

static inline int GetHitboxPriority(int nHitbox, Target_t& tTarget, Info_t& tInfo, CBaseEntity* pProjectile = nullptr)
{
    const int configured=Vars::Aimbot::Projectile::Hitboxes.Value;
    const bool player=tTarget.m_iTargetType==TargetEnum::Player;
    const bool bodyWeapon=BodyProjectile(tInfo.m_pWeapon);
    const bool trueFeet=player && (configured&ProjectileAimPolicy::TrueFeet);
	if (!F::AimbotGlobal.IsHitboxValid(nHitbox, ProjectileAimPolicy::Hitboxes(configured,bodyWeapon,player)))
		return -1;

	int iHeadPriority = 0;
	int iBodyPriority = 1;
	int iFeetPriority = 2;

	if (Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::Auto)
	{
		bool bHeadshot = !trueFeet && (Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::Head)
			&& tTarget.m_iTargetType == TargetEnum::Player;
		if (bHeadshot)
		{
			if (!pProjectile)
				bHeadshot = tInfo.m_pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW;
			else
				bHeadshot = pProjectile->GetClassID() == ETFClassID::CTFProjectile_Arrow && pProjectile->As<CTFProjectile_Arrow>()->CanHeadshot();

			if (Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::BodyaimIfLethal
				&& bHeadshot && !pProjectile && tInfo.m_pWeapon->m_hOwner().GetEntryIndex() == I::EngineClient->GetLocalPlayer())
			{
                auto target=tTarget.m_pEntity->As<CTFPlayer>();
                const float maxCharge=tInfo.m_pWeapon->ApplyFireDelay(1.f);
                const float fraction=BowChargePolicy::Fraction(I::GlobalVars->curtime,
                    tInfo.m_pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime(),maxCharge);
                const bool mini=tInfo.m_pLocal->IsMiniCritBoosted() || target->InCond(TF_COND_URINE)
                    || target->InCond(TF_COND_MARKEDFORDEATH) || target->InCond(TF_COND_MARKEDFORDEATH_SILENT);
                const float boost=tInfo.m_pLocal->IsCritBoosted()?3.f:mini?1.35f:1.f;
                float resistance=SDK::AttribHookValue(1.f,"mult_dmgtaken",target);
                resistance=SDK::AttribHookValue(resistance,"mult_dmgtaken_from_bullets",target);
                const bool uncertain=target->IsInvulnerable() || target->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST)
                    || target->InCond(TF_COND_MEDIGUN_SMALL_BULLET_RESIST) || target->InCond(TF_COND_DEFENSEBUFF)
                    || target->InCond(TF_COND_DEFENSEBUFF_NO_CRIT_BLOCK) || target->InCond(TF_COND_DEFENSEBUFF_HIGH)
                    || target->InCond(TF_COND_RUNE_RESIST) || target->InCond(TF_COND_STEALTHED_USER_BUFF_FADING)
                    || target->InCond(TF_COND_STEALTHED) || target->m_bFeignDeathReady()
                    || !std::isfinite(maxCharge) || maxCharge<=0;
                const float damage=BowChargePolicy::BodyDamage(fraction,
                    SDK::AttribHookValue(50.f,"mult_dmg",tInfo.m_pWeapon),tInfo.m_pWeapon->GetDamage(),boost,resistance);
                const bool lethal=BowChargePolicy::Lethal(damage,target->m_iHealth(),uncertain);
                if(lethal) bHeadshot=false;
                if(ProjectileDiagnostics::MovementSample()) ProjectileDiagnostics::Ledge("huntsman_body_decision",
                    std::format("target={} fraction={} charge_max={} damage_estimate={} health={} boost={} resistance={} uncertain={} lethal={} policy=conservative_visible_state",target->entindex(),fraction,maxCharge,damage,target->m_iHealth(),boost,resistance,uncertain,lethal));
			}
		}
		if (bHeadshot)
			tTarget.m_nAimedHitbox = HITBOX_HEAD;

		bool bLower = Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::PrioritizeFeet
			&& tTarget.m_iTargetType == TargetEnum::Player && tTarget.m_pEntity->As<CTFPlayer>()->IsOnGround() /*&& tInfo.m_flRadius*/;

		iHeadPriority = bHeadshot ? 0 : 2;
		iBodyPriority = bHeadshot ? -1 : bLower ? 1 : 0;
		iFeetPriority = bHeadshot ? -1 : bLower ? 0 : 1;
	}

    iHeadPriority=ProjectileAimPolicy::Priority(BOUNDS_HEAD,iHeadPriority,configured,bodyWeapon,player);
    iBodyPriority=ProjectileAimPolicy::Priority(BOUNDS_BODY,iBodyPriority,configured,bodyWeapon,player);
    iFeetPriority=ProjectileAimPolicy::Priority(BOUNDS_FEET,iFeetPriority,configured,bodyWeapon,player);

	switch (nHitbox)
	{
	case BOUNDS_HEAD: return iHeadPriority;
	case BOUNDS_BODY: return iBodyPriority;
	case BOUNDS_FEET: return iFeetPriority;
	}

	return -1;
};

Directs_t CAimbotProjectile::GetDirects()
{
	Directs_t mDirects = {};
    if(ProjectileDiagnostics::current && ProjectileDiagnostics::current->candidates<=8)
    {
        const int configured=Vars::Aimbot::Projectile::Hitboxes.Value;
        const bool body=BodyProjectile(m_tInfo.m_pWeapon);
        const bool player=m_tInfo.m_pTarget->m_iTargetType==TargetEnum::Player;
        ProjectileDiagnostics::Ledge("projectile_aim_policy",std::format("weapon={} item={} body_weapon={} player={} configured_hitboxes={} effective_hitboxes={} true_feet={} direct_hit={} splash_mode={} viewmodel_switch={}",
            m_tInfo.m_pWeapon->GetWeaponID(),m_tInfo.m_pWeapon->m_iItemDefinitionIndex(),body,player,configured,
            ProjectileAimPolicy::Hitboxes(configured,body,player),bool(configured&ProjectileAimPolicy::TrueFeet),
            DirectHitProjectile(m_tInfo),EffectiveSplash(m_tInfo),AutoViewmodelSwitch::Enabled()));
    }

	if (EffectiveSplash(m_tInfo) == Vars::Aimbot::Projectile::SplashPredictionEnum::Only && m_tInfo.m_flRadius)
		return mDirects;

	auto& tTarget = *m_tInfo.m_pTarget;
	uint8_t iFlags = PointFlagsEnum::Regular;
	if (ShouldLob(m_tInfo))
		iFlags |= PointFlagsEnum::Lob;

	const Vec3 vMins = tTarget.m_pEntity->m_vecMins(), vMaxs = tTarget.m_pEntity->m_vecMaxs();
    const bool grenadeFeet=!m_tInfo.m_pProjectile && m_tInfo.m_pWeapon
        && m_tInfo.m_pWeapon->GetWeaponID()==TF_WEAPON_GRENADELAUNCHER
        && tTarget.m_iTargetType==TargetEnum::Player
        && (Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::PrioritizeFeet)
        && !(Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::TruePrioritizeFeet);
	for (int i = 0; i < 3; i++)
	{
		int iPriority = GetHitboxPriority(i, tTarget, m_tInfo, m_tInfo.m_pProjectile);
		if (iPriority == -1)
			continue;
        if (grenadeFeet) iPriority*=2; // Reserve the next slot for the lower-body fallback.

		switch (i)
		{
		case BOUNDS_HEAD:
			if (tTarget.m_nAimedHitbox == HITBOX_HEAD)
			{
				auto aBones = F::Backtrack.GetBones(tTarget.m_pEntity);
				if (!aBones)
					break;

				//Vec3 vOff = tTarget.m_pEntity->As<CBaseAnimating>()->GetHitboxOrigin(aBones, HITBOX_HEAD) - tTarget.m_pEntity->m_vecOrigin();

				// https://www.youtube.com/watch?v=_PSGD-pJUrM, might be better??
				Vec3 vCenter, vBBoxMins, vBBoxMaxs; tTarget.m_pEntity->As<CBaseAnimating>()->GetHitboxInfo(aBones, HITBOX_HEAD, &vCenter, &vBBoxMins, &vBBoxMaxs);
				Vec3 vOff = vCenter + (vBBoxMins + vBBoxMaxs) / 2 - tTarget.m_pEntity->m_vecOrigin();

				float flLow = 0.f;
				Vec3 vDelta = tTarget.m_vPos + m_tInfo.m_vTargetEye - m_tInfo.m_vLocalEye;
				if (vDelta.z > 0)
				{
					float flXY = vDelta.Length2D();
					if (flXY)
						flLow = Math::RemapVal(vDelta.z / flXY, 0.f, 0.5f, 0.f, 1.f);
					else
						flLow = 1.f;
				}

				float flLerp = (Vars::Aimbot::Projectile::HuntsmanLerp.Value + (Vars::Aimbot::Projectile::HuntsmanLerpLow.Value - Vars::Aimbot::Projectile::HuntsmanLerp.Value) * flLow) / 100.f;
				float flAdd = Vars::Aimbot::Projectile::HuntsmanAdd.Value + (Vars::Aimbot::Projectile::HuntsmanAddLow.Value - Vars::Aimbot::Projectile::HuntsmanAdd.Value) * flLow;
				vOff.z += flAdd;
				vOff.z = vOff.z + (vMaxs.z - vOff.z) * flLerp;

				vOff.x = std::clamp(vOff.x, vMins.x + Vars::Aimbot::Projectile::HuntsmanClamp.Value, vMaxs.x - Vars::Aimbot::Projectile::HuntsmanClamp.Value);
				vOff.y = std::clamp(vOff.y, vMins.y + Vars::Aimbot::Projectile::HuntsmanClamp.Value, vMaxs.y - Vars::Aimbot::Projectile::HuntsmanClamp.Value);
				vOff.z = std::clamp(vOff.z, vMins.z + Vars::Aimbot::Projectile::HuntsmanClamp.Value, vMaxs.z - Vars::Aimbot::Projectile::HuntsmanClamp.Value);
				mDirects[iPriority] = { vOff, iFlags };
			}
			else
				mDirects[iPriority] = { Vec3(0, 0, vMaxs.z - Vars::Aimbot::Projectile::VerticalShift.Value), iFlags };
			break;
		case BOUNDS_BODY:
			mDirects[iPriority] = { Vec3(0, 0, (vMins.z + vMaxs.z) / 2), iFlags };
			break;
		case BOUNDS_FEET:
            if (grenadeFeet)
            {
                const auto heights=GrenadeCollisionPolicy::FeetHeights(vMins.z,vMaxs.z,m_tInfo.m_vHull.z,Vars::Aimbot::Projectile::VerticalShift.Value);
                mDirects[iPriority]={Vec3(0,0,heights[0]),iFlags};
                if (heights[1]>heights[0]+.01f)
                    mDirects[iPriority+1]={Vec3(0,0,heights[1]),iFlags};
                ProjectileDiagnostics::Ledge("grenade_feet_points",std::format("entity={} primary_z={} fallback_z={} hull_z={} height={} priority={}",
                    tTarget.m_pEntity->entindex(),heights[0],heights[1],m_tInfo.m_vHull.z,vMaxs.z-vMins.z,iPriority));
            }
            else
                mDirects[iPriority] = { Vec3(0, 0, vMins.z + Vars::Aimbot::Projectile::VerticalShift.Value), iFlags };
			break;
		}
	}

	return mDirects;
}

Splashes_t CAimbotProjectile::GetSplashes()
{
	Splashes_t vSplashes = {};

	if (EffectiveSplash(m_tInfo) == Vars::Aimbot::Projectile::SplashPredictionEnum::Off || !m_tInfo.m_flRadius)
		return vSplashes;

	vSplashes.push_back(PointFlagsEnum::Regular);
	if (ShouldLob(m_tInfo))
		vSplashes.push_back(PointFlagsEnum::Lob);

	return vSplashes;
}

namespace TracePreparation
{
    struct Rotate
    {
        Vec3 operator()(const Vec3& point,float x,float y) const
        { return Math::RotatePoint(point,{}, {x,y}); }
    };
    static Pipeline<Vec3,Rotate> pipeline;
}

static inline std::vector<Vec3> ComputePoints(float flRadius, int iSamples)
{
	if (!SearchAllowed(SearchWork::Sampling)) return {};
	// Bound allocation as well as tracing. Keep the sphere distributed over all
	// directions when a large configured sample count exceeds this command's allowance.
	if (ProjectilePerformancePolicy::currentSearchBudget)
		iSamples = std::min(iSamples, int(ProjectilePerformancePolicy::SearchRemaining(SearchWork::Sampling)) - 1);
	std::vector<Vec3> vPoints = { { Vec3(0.f, 0.f, -1.f) * flRadius } };
    if (!std::isfinite(flRadius) || flRadius <= 0.f)
        return {};
	if (iSamples <= 0)
		return vPoints;

    // Prepared offsets contain no target state. World placement and every
    // collision query remain live below, even when many targets share a pattern.
    bool submitted=false;
    auto prepared=TracePreparation::pipeline.Get(
        {0,iSamples,flRadius,TracePreparation::Key::Rotation(Vars::Aimbot::Projectile::SplashRotateX.Value),
            TracePreparation::Key::Rotation(Vars::Aimbot::Projectile::SplashRotateY.Value)},
        G::CurrentUserCmd ? G::CurrentUserCmd->command_number : 0,
        []{return uint32_t(SDK::StdRandomInt(0,INT_MAX));},submitted);
    if(ProjectileDiagnostics::current)
    {
        ProjectileDiagnostics::splash.traceWorkerSubmitted+=submitted;
        if(prepared.empty()) ++ProjectileDiagnostics::splash.traceWorkerFallback;
        else ++ProjectileDiagnostics::splash.traceWorkerReady;
    }
    if(!prepared.empty()) return prepared;

	vPoints.reserve(iSamples + 1);

	float flRotateX = Vars::Aimbot::Projectile::SplashRotateX.Value < 0.f ? SDK::StdRandomFloat(0.f, 360.f) : Vars::Aimbot::Projectile::SplashRotateX.Value;
	float flRotateY = Vars::Aimbot::Projectile::SplashRotateY.Value < 0.f ? SDK::StdRandomFloat(0.f, 360.f) : Vars::Aimbot::Projectile::SplashRotateY.Value;
    if (!std::isfinite(flRotateX)) flRotateX = 0.f;
    if (!std::isfinite(flRotateY)) flRotateY = 0.f;
		
	float a = Math::PI * (3.f - sqrtf(5.f));
	// Per-thread ownership needs no locks or background engine calls. Radius and
	// random rotation stay live; only the sphere's unit directions are reused.
	static thread_local SplashSearchPolicy::SphereDirectionCache<Vec3> directionCache;
	auto* cached = directionCache.Find(iSamples);
	const auto generate = [&](int n)
	{
		float t = a * n;
		float y = SplashSearchPolicy::SphereY(n, iSamples);
		float r = sqrtf(std::max(0.f, 1 - y * y));
		float x = cosf(t) * r;
		float z = sinf(t) * r;
		return Vec3(x, y, z);
	};
	const auto appendPoints = [&](auto direction)
	{
		for (int n = 0; n < iSamples; n++)
		{
			if (!SearchAllowed(SearchWork::Sampling)) break;
			Vec3 vPoint = direction(n) * flRadius;
			vPoint = Math::RotatePoint(vPoint, {}, { flRotateX, flRotateY });

			vPoints.push_back(vPoint);
		}
	};
	// Choose once, keeping oversized uncached requests free of a per-point
	// cache branch as well as preserving their original point count.
	if (cached)
		appendPoints([&](int n) { return cached->Get(n, [&] { return generate(n); }); });
	else
		appendPoints(generate);

	return vPoints;
};

#if defined(SPLASH_DEBUG1) || defined(SPLASH_DEBUG2)
static inline void DrawTrace(bool bSuccess, Color_t tColor, CGameTrace& trace)
{
	Vec3 vMins = -Vec3::Get(bSuccess ? 1.f : 0.5f), vMaxs = Vec3::Get(bSuccess ? 1.f : 0.5f);
	Vec3 vAngles = Math::VectorAngles(trace.plane.normal);
	G::BoxStorage.emplace_back(trace.endpos, vMins, vMaxs, vAngles, I::GlobalVars->curtime + 60.f, tColor.Alpha(tColor.a / (bSuccess ? 1 : 10)), Color_t(0, 0, 0, 0));
	G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(trace.startpos, trace.endpos), I::GlobalVars->curtime + 60.f, tColor.Alpha(tColor.a / (bSuccess ? 1 : 10)));
};
#endif

static inline void HandleTrace(const Vec3& vPoint, std::vector<Setup_t>& vPoints, const Vec3& vTargetEye, Info_t& tInfo, std::function<bool()> fCheckPointTrace, std::function<bool()> fCheckPointAir, CGameTrace& trace, ITraceFilter& filter, int i = 0)
{
	// out
	SDK::TraceHull(vTargetEye, vPoint, -tInfo.m_vHull, tInfo.m_vHull, MASK_SOLID, &filter, &trace);
    if (ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.setupTraces;
#ifdef SPLASH_DEBUG5
	s_mTraceCount[__FUNCTION__": splash out"]++;
#endif

	if (fCheckPointAir())
		return;

	if (fCheckPointTrace())
#ifndef SPLASH_DEBUG1
		vPoints.emplace_back(trace.endpos);
#else
	{
		vPoints.emplace_back(trace.endpos);
		DrawTrace(true, Vars::Colors::IndicatorGood.Value, trace);
	}
	else
		DrawTrace(false, Vars::Colors::IndicatorGood.Value, trace);
#endif

	// in
	if ((tInfo.m_vLocalEye - vTargetEye).Dot(vTargetEye - vPoint) > 0.f)
		return;

	SDK::Trace(vPoint, vTargetEye, MASK_SHOT, &filter, &trace);
    if (ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.setupTraces;
#ifdef SPLASH_DEBUG5
	s_mTraceCount[__FUNCTION__": splash in check"]++;
#endif
#ifdef SPLASH_DEBUG1
	DrawTrace(!trace.DidHit(), Vars::Colors::IndicatorMid.Value, trace);
#endif
	if (trace.DidHit())
		return;

	SDK::TraceHull(vPoint, vTargetEye, -tInfo.m_vHull, tInfo.m_vHull, MASK_SOLID, &filter, &trace);
    if (ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.setupTraces;
#ifdef SPLASH_DEBUG5
	s_mTraceCount[__FUNCTION__": splash in"]++;
#endif

	if (fCheckPointTrace())
#ifndef SPLASH_DEBUG1
		vPoints.emplace_back(trace.endpos);
#else
	{
		vPoints.emplace_back(trace.endpos);
		DrawTrace(true, Vars::Colors::IndicatorBad.Value, trace);
	}
	else
		DrawTrace(false, Vars::Colors::IndicatorBad.Value, trace);
#endif
}

// No engine interfaces, entity pointers, Vars, or shared RNG are touched by
// this sampler. Both synchronous fallback and worker use this same math.
struct FaceRandom
{
    std::mt19937 generator;
    explicit FaceRandom(uint32_t seed) : generator(seed) {}
    float Float(float low=0.f, float high=1.f) { return std::uniform_real_distribution<float>(low,high)(generator); }
    int Int(int low,int high) { return std::uniform_int_distribution<int>(low,high)(generator); }
    bool Bool() { return Int(0,1)!=0; }
};
struct FaceSample { Vec3 point, normal; bool inside; };
template<class Step, class Emit>
static void SampleFace(Face_t& tFace, float flDensity, float flRadius, float flCutoff,
    const Vec3& vTargetEye, const Vec3& vTargetCenter, const Vec3& vTargetOrigin,
    const Vec3& hull, FaceRandom& random, float& total, Step step, Emit emit)
{
    if(tFace.m_vVertices.size()<3 || !std::isfinite(flRadius) || flRadius<=0.f) return;
	float flRadiusSqr = powf(flRadius, 2);
	float flRadius2Sqr = flRadiusSqr * 4;

	std::vector<Vec3> vVertices, vEpsilon;
	if (tFace.m_iType != FaceTypeEnum::Prop)
		vVertices = tFace.m_vVertices;
	else
		Math::ExpandPolygon(vVertices, tFace.m_vVertices, tFace.m_vNormal, random.Float(0.f, DIST_EPSILON), &vTargetEye);

	for (int i = 0, n = int(tFace.m_vVertices.size()), o = random.Int(0, n - 1); ++i < n - 1;)
	{
		if (!step(SearchWork::Geometry)) return;
		Vec3& vVertex1 = tFace.m_vVertices[o], &vVertex2 = tFace.m_vVertices[(o + i) % n], &vVertex3 = tFace.m_vVertices[(o + i + 1) % n];

		Vec3 vDir21 = vVertex2 - vVertex1, vDir31 = vVertex3 - vVertex1;
		float flArea = vDir21.Cross(vDir31).Length() / 2;
		float flSamples = flDensity * flArea / flRadius2Sqr;
        if (!std::isfinite(flSamples) || flSamples <= 0.f)
            continue;
        int iSamples = flCutoff <= 0.f || flSamples > flCutoff ? int(std::min(ceilf(flSamples), 65536.f)) : fmodf(total += flSamples, flCutoff) < flSamples;
		if (!iSamples)
			continue;

		// don't particularly like the hacky random epsilons
		int iFaceClosest = random.Int(iSamples < 2 ? 0 : iSamples < 4 ? 1 : 2, iSamples < 2 ? 1 : 2);
		//int iEdgeClosest = random.Int(iSamples < 8 ? 0 : iSamples < 16 ? 1 : 2, iSamples < 8 ? 1 : 2); // eats up a bit too much performance for my liking
		int iEdgeRandom = random.Int(iSamples < 8 ? 0 : iSamples < 16 ? 1 : 2, iSamples < 3 && iFaceClosest ? 0 : iSamples < 12 ? 1 : 2);
		iEdgeRandom += iFaceClosest; //iEdgeClosest += iFaceClosest, iEdgeRandom += iEdgeClosest; //, iSamples += iEdgeRandom;
		for (int s = 0; s < iSamples; s++)
		{
			if (!step(SearchWork::Sampling)) return;
			Vec3 vPoint, vNormal = tFace.m_vNormal; bool bInside = true;
			if (s < iFaceClosest) // closest point
			{
				float flEpsilon = s == 0 && (tFace.m_iType == FaceTypeEnum::BoxBrush || iFaceClosest != 1 || random.Bool()) ? CALC_EPSILON : DIST_EPSILON;
				vPoint = Math::ClosestPointOnTriangle(vTargetOrigin, vVertex1, vVertex2, vVertex3, &bInside);
				vPoint += { random.Float(-flEpsilon, flEpsilon), random.Float(-flEpsilon, flEpsilon), random.Float(-flEpsilon, flEpsilon) };
			}
			//else if (s < iEdgeClosest) // closest point on edge
			//{
			//	float flEpsilon = random.Bool() ? CALC_EPSILON : DIST_EPSILON; bInside = false;
			//	switch (random.Int(0, 2))
			//	{
			//	case 0: vPoint = Math::ClosestPointOnLine(vTargetOrigin, vVertex1, vVertex2); break;
			//	case 1: vPoint = Math::ClosestPointOnLine(vTargetOrigin, vVertex2, vVertex3); break;
			//	case 2: vPoint = Math::ClosestPointOnLine(vTargetOrigin, vVertex3, vVertex1); break;
			//	}
			//	vPoint += { random.Float(-flEpsilon, flEpsilon), random.Float(-flEpsilon, flEpsilon), random.Float(-flEpsilon, flEpsilon) };
			//}
			else if (s < iEdgeRandom) // random point on edge
			{
				float flEpsilon = random.Bool() ? CALC_EPSILON : DIST_EPSILON; bInside = false;
				switch (random.Int(0, 2))
				{
				case 0: vPoint = vVertex1.Lerp(vVertex2, random.Float()); break;
				case 1: vPoint = vVertex2.Lerp(vVertex3, random.Float()); break;
				case 2: vPoint = vVertex3.Lerp(vVertex1, random.Float()); break;
				}
				vPoint += { random.Float(-flEpsilon, flEpsilon), random.Float(-flEpsilon, flEpsilon), random.Float(-flEpsilon, flEpsilon) };
			}
			else // random point on face
			{
				float flRandom1 = random.Float(), flRandom2 = random.Float();
				if (flRandom1 + flRandom2 > 1)
					flRandom1 = 1 - flRandom1, flRandom2 = 1 - flRandom2;
				vPoint = vVertex1 + vDir21 * flRandom1 + vDir31 * flRandom2;
			}
			vPoint += vNormal * (hull + CALC_EPSILON);
			if (vPoint.DistToSqr(vTargetCenter) > flRadiusSqr)
				continue;

			if (!bInside)
			{
				if (vEpsilon.empty())
					Math::ExpandPolygon(vEpsilon = vVertices, tFace.m_vNormal, -DIST_EPSILON);
				Math::ClosestPointOnPolygon(vPoint, vEpsilon, vNormal, &bInside);

				if (tFace.m_iType == FaceTypeEnum::Prop && !bInside)
					vPoint += tFace.m_vNormal * random.Float(0.f, DIST_EPSILON);
			}


            if(std::isfinite(vPoint.x) && std::isfinite(vPoint.y) && std::isfinite(vPoint.z))
                emit(FaceSample{vPoint,vNormal,bInside});
        }
    }
}

struct FaceSnapshotKey
{
    uint64_t epoch=0;
    int command=0, target=0, weapon=0;
    uint8_t flags=0;
    Vec3 origin, eye, center, localEye, hull;
    float radius=0, density=0, cutoffSetting=0;
    std::chrono::steady_clock::time_point created;
    bool Matches(const FaceSnapshotKey& now) const
    {
        // Candidates are hints only. Reject old sessions, weapons, targets,
        // settings, and meaningful movement before doing live validation.
        return epoch==now.epoch && target==now.target && weapon==now.weapon && flags==now.flags
            && int64_t(now.command)-command>=0 && int64_t(now.command)-command<=2
            && now.created-created<std::chrono::milliseconds(50)
            && radius==now.radius && density==now.density && cutoffSetting==now.cutoffSetting
            && hull==now.hull && origin.DistToSqr(now.origin)<=16.f
            && eye.DistToSqr(now.eye)<=16.f && center.DistToSqr(now.center)<=16.f
            && localEye.DistToSqr(now.localEye)<=16.f;
    }
};
struct FaceResult { FaceSnapshotKey key; std::vector<FaceSample> points; };
struct FaceJob
{
    FaceSnapshotKey key;
    std::vector<Face_t> faces;
    uint32_t seed=0;
    FaceResult Run(const std::atomic<bool>& stopping)
    {
        FaceResult result{key,{}};
        result.points.reserve(512);
        FaceRandom random(seed);
        float total=random.Float();
        const float cutoff=key.cutoffSetting*float(faces.size())*float(faces.size());
        unsigned geometry=0, samples=0;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(2);
        const auto step=[&](SearchWork work)
        {
            if(stopping.load(std::memory_order_relaxed) || std::chrono::steady_clock::now()>=deadline) return false;
            return work==SearchWork::Geometry ? ++geometry<=2048 : ++samples<=512;
        };
        for(auto& face:faces)
        {
            if(geometry>=2048 || samples>=512 || stopping.load(std::memory_order_relaxed)
                || std::chrono::steady_clock::now()>=deadline) break;
            SampleFace(face,key.density,key.radius,cutoff,key.eye,key.center,key.origin,key.hull,
                random,total,step,[&](const FaceSample& sample){ result.points.push_back(sample); });
        }
        return result;
    }
};
namespace SplashWorker
{
    static Mailbox<FaceJob,FaceResult> worker;
    static std::atomic<uint64_t> epoch{0};
    void Start() { worker.Start(); TracePreparation::pipeline.Start(); }
    void Stop() { worker.Stop(); TracePreparation::pipeline.Stop(); }
    void Invalidate() { epoch.fetch_add(1,std::memory_order_relaxed); TracePreparation::pipeline.Invalidate(); }
}
static void CheckFaceSample(const FaceSample& sample, std::vector<Setup_t>& points,
    const Vec3& targetEye, const Vec3& targetCenter, float radius, Info_t& info,
    CGameTrace& trace, ITraceFilter& filter)
{
    // This function runs only on the game thread, including for worker results.
    const Vec3 point=sample.point;
    if(point.DistToSqr(targetCenter)>radius*radius) return;
    if(sample.inside && (I::EngineTrace->GetPointContents(point)&MASK_SOLID)) return;
    Vec3 normal=sample.normal;
    int mask=F::ProjSim.m_bPhysics ? MASK_SHOT|CONTENTS_DISPSOLID : MASK_SHOT;
    if(!sample.inside) { normal=(targetEye-point).Normalized(); mask&=~CONTENTS_MOVEABLE; }
    SDK::Trace(point+normal*info.m_flNormalOffset,targetEye,mask,&filter,&trace);
    if(ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.setupTraces;
    if(trace.fraction==1.f) points.emplace_back(point);
}

void CAimbotProjectile::SetupSplashPoints(Vec3& vOrigin, std::vector<Setup_t>& vSplashPoints, uint8_t iFlags, int searchMode)
{
    ProjectileDiagnostics::Profile profile(ProjectileDiagnostics::SplashSetup);
	vSplashPoints.clear();

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};

	m_tInfo.m_pTarget->m_vPos = vOrigin;
	Vec3 vTargetEye = vOrigin + m_tInfo.m_vTargetEye;
	Vec3 vTargetCenter = vOrigin + m_tInfo.m_pTarget->m_pEntity->GetOffset() / 2;
	float flRadius = m_tInfo.m_flRadius + m_tInfo.m_pTarget->m_pEntity->GetSize().Length() / 2;
	bool bAirSplash = AirSplash(m_tInfo.m_pWeapon, m_tInfo);

	auto fCheckNormal = [&](const Vec3& vNormal, const Vec3& vPoint, Vec3* pAngle = nullptr)
	{
		if (pAngle)
		{
			Vec3 vForward, vRight, vUp; Math::AngleVectors(*pAngle, &vForward, &vRight, &vUp);
			Vec3 vShootPos = m_tInfo.m_vLocalEye + vForward * m_tInfo.m_vOffset.x + vRight * m_tInfo.m_vOffset.y + vUp * m_tInfo.m_vOffset.z;
			vForward = (vShootPos - vPoint).Normalized();
			return vForward.Dot(vNormal) > 0;
		}
		else
		{
			Vec3 vForward = (m_tInfo.m_vLocalEye - vPoint).Normalized();
			return vForward.Dot(vNormal) > 0;
		}
	};

	// Trace
	int iPoints = searchMode == Vars::Aimbot::Projectile::SplashModeEnum::Face && !bAirSplash ? 0
		: !m_tInfo.m_flGravity ? Vars::Aimbot::Projectile::SplashPointsDirect.Value : Vars::Aimbot::Projectile::SplashPointsArc.Value;
	{
        ProjectileDiagnostics::Profile sampling(ProjectileDiagnostics::SplashSampling);
		auto vPoints = ComputePoints(flRadius, iPoints);

		for (int i = 0; i < vPoints.size(); i++)
		{
			if (!SearchStep(SearchWork::Sampling)) break;
			auto fCheckPointTrace = [&]()
			{
				if (!trace.m_pEnt || trace.fraction == 1.f || trace.surface.flags & SURF_SKY || !trace.m_pEnt->GetAbsVelocity().IsZero())
					return false;

				Vec3 vPoint = trace.endpos, vAngle;
				if (!m_tInfo.m_flGravity)
					vAngle = Math::CalcAngle(m_tInfo.m_vLocalEye, trace.endpos);
				else
				{
					Point_t tPoint = { vPoint, {} };
					CalculateAngle(m_tInfo.m_vLocalEye, tPoint.m_vPoint, 0, tPoint.m_tSolution, iFlags);
					if (tPoint.m_tSolution.m_iCalculated == CalculateResultEnum::Bad)
						return false;
					vPoint -= Vec3(0, 0, m_tInfo.m_flGravity * powf(tPoint.m_tSolution.m_flTime, 2) / 2);
					vAngle = Vec3(tPoint.m_tSolution.m_flPitch, tPoint.m_tSolution.m_flYaw);
				}
				return fCheckNormal(trace.plane.normal, vPoint, &vAngle);
			};

			auto fCheckPointAir = [&]()
			{
				if (bAirSplash && !trace.DidHit())
				{
					if (!Vars::Aimbot::Projectile::SplashAirCount.Value)
						vSplashPoints.emplace_back(vPoints[i] * SDK::StdRandomFloat() + vTargetCenter, PointTypeEnum::Air);
					else for (float r = 0; r < Vars::Aimbot::Projectile::SplashAirCount.Value; r++)
						vSplashPoints.emplace_back(vPoints[i] * r / Vars::Aimbot::Projectile::SplashAirCount.Value + vTargetCenter, PointTypeEnum::Air);
				}
				return searchMode == Vars::Aimbot::Projectile::SplashModeEnum::Face && i || !trace.DidHit();
			};

			Vec3 vPoint = vPoints[i] + vTargetCenter;

			Solution_t tSolution; CalculateAngle(m_tInfo.m_vLocalEye, vPoint, 0, tSolution, iFlags);
			if (tSolution.m_iCalculated == CalculateResultEnum::Bad)
				continue;

			HandleTrace(vPoint, vSplashPoints, vTargetEye, m_tInfo, fCheckPointTrace, fCheckPointAir, trace, filter);
		}
	}

	// Face
	if (float flDensity = !m_tInfo.m_flGravity ? Vars::Aimbot::Projectile::SplashDensityDirect.Value : Vars::Aimbot::Projectile::SplashDensityArc.Value;
		searchMode == Vars::Aimbot::Projectile::SplashModeEnum::Face && flDensity && SearchAllowed(SearchWork::Sampling))
	{
		Vec3 vMins = vTargetCenter - flRadius, vMaxs = vTargetCenter + flRadius;

		NormalValidCallback fNormalValid = [&](const std::vector<Vec3>& vVertices, const Vec3& vNormal)
		{
			Vec3 vPoint = std::reduce(vVertices.begin(), vVertices.end()) / vVertices.size(), vAngle;
			if (!m_tInfo.m_flGravity)
				vAngle = Math::CalcAngle(m_tInfo.m_vLocalEye, vPoint);
			else
			{
				Point_t tPoint = { vPoint, {} };
				CalculateAngle(m_tInfo.m_vLocalEye, tPoint.m_vPoint, 0, tPoint.m_tSolution, iFlags);
				if (tPoint.m_tSolution.m_iCalculated == CalculateResultEnum::Bad)
					return false;
				vPoint -= Vec3(0, 0, m_tInfo.m_flGravity * powf(tPoint.m_tSolution.m_flTime, 2) / 2);
				vAngle = Vec3(tPoint.m_tSolution.m_flPitch, tPoint.m_tSolution.m_flYaw);
			}
			return fCheckNormal(vNormal, vPoint, &vAngle);
		};
		F::World.SetNormalValidCallback(&fNormalValid);
        std::vector<Face_t> vFaces;
        {
            ProjectileDiagnostics::Profile geometry(ProjectileDiagnostics::SplashGeometry);
            vFaces = F::World.GetFacesInAABB(vMins, vMaxs, MASK_SOLID, &filter, FaceTypeEnum::All,
                ProjectilePerformancePolicy::currentSearchBudget ? ProjectilePerformancePolicy::SearchGeometryStep : nullptr);
        }
		F::World.SetNormalValidCallback();
        if (ProjectileDiagnostics::current) ProjectileDiagnostics::splash.faces += int(vFaces.size());
#ifdef SPLASH_DEBUG5
		SDK::Output("Faces", std::format("{}", vFaces.size()).c_str(), {}, OUTPUT_CONSOLE);
#endif

        FaceSnapshotKey key;
        key.epoch=SplashWorker::epoch.load(std::memory_order_relaxed);
        key.command=G::CurrentUserCmd->command_number;
        key.target=m_tInfo.m_pTarget->m_pEntity->GetRefEHandle().ToInt();
        key.weapon=m_tInfo.m_pWeapon->GetRefEHandle().ToInt();
        key.flags=iFlags; key.origin=vOrigin; key.eye=vTargetEye; key.center=vTargetCenter;
        key.localEye=m_tInfo.m_vLocalEye; key.hull=m_tInfo.m_vHull;
        key.radius=flRadius; key.density=flDensity;
        key.cutoffSetting=Vars::Aimbot::Projectile::SplashSamplesCutoff.Value;
        key.created=std::chrono::steady_clock::now();
        std::unique_ptr<FaceResult> prepared;
        // The first rollout covers the already-budgeted ordinary rocket path.
        // Charged, physics and reflected projectiles keep synchronous sampling.
        const bool threaded=ProjectilePerformancePolicy::currentSearchBudget && !m_tInfo.m_pProjectile && !m_tInfo.m_flGravity
            && (m_tInfo.m_pWeapon->GetWeaponID()==TF_WEAPON_ROCKETLAUNCHER || m_tInfo.m_pWeapon->GetWeaponID()==TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT)
            && m_tInfo.m_pWeapon->m_iItemDefinitionIndex()!=Soldier_m_TheBeggarsBazooka;
        if(threaded)
        {
            prepared=SplashWorker::worker.Take([&](const FaceResult& result){return result.key.Matches(key);});
            size_t vertices=0;
            for(const auto& face:vFaces) vertices+=face.m_vVertices.size();
            if(vFaces.size()<=2048 && vertices<=16384 && SearchAllowed(SearchWork::Sampling) && SplashWorker::worker.Available())
            {
                auto job=std::make_unique<FaceJob>();
                job->key=key; job->faces=vFaces; job->seed=uint32_t(SDK::StdRandomInt(0,INT_MAX));
                if(SplashWorker::worker.Submit(std::move(job)) && ProjectileDiagnostics::current)
                    ++ProjectileDiagnostics::splash.workerSubmitted;
            }
        }
        const size_t before=vSplashPoints.size();
        ProjectileDiagnostics::Profile sampling(ProjectileDiagnostics::SplashSampling);
        if(prepared)
        {
            if(ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.workerReady;
            for(const auto& sample:prepared->points)
            {
                if(!SearchStep(SearchWork::Sampling)) break;
                CheckFaceSample(sample,vSplashPoints,vTargetEye,vTargetCenter,flRadius,m_tInfo,trace,filter);
            }
        }
		float flCutoff = key.cutoffSetting * powf(vFaces.size(), 2);
        FaceRandom random(uint32_t(SDK::StdRandomInt(0,INT_MAX)));
        float total=random.Float();
        // Never hold a shot waiting for a background result. If it is missing
        // or yielded no live-visible candidates, use the remaining normal budget.
        if(vSplashPoints.size()==before) for (auto& tFace : vFaces)
		{
			if (!SearchAllowed(SearchWork::Sampling)) break;
            if(threaded && ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.workerFallbackFaces;
            SampleFace(tFace,flDensity,flRadius,flCutoff,vTargetEye,vTargetCenter,vOrigin,m_tInfo.m_vHull,
                random,total,[](SearchWork work){return SearchStep(work);},
                [&](const FaceSample& sample){CheckFaceSample(sample,vSplashPoints,vTargetEye,vTargetCenter,flRadius,m_tInfo,trace,filter);});
//#if defined(SPLASH_DEBUG2) && defined(WORLD_DEBUG)
//			F::World.DrawFace(tFace, DrawTypeEnum::Edges | DrawTypeEnum::Faces);
//#endif
		}
	}
	
    if (ProjectileDiagnostics::current) ProjectileDiagnostics::splash.generated += int(vSplashPoints.size());
	if (vSplashPoints.size() > 1)
		std::shuffle(vSplashPoints.begin() + 1, vSplashPoints.end(), SDK::Random);

#ifdef SPLASH_DEBUG3
	for (auto& tSetup : vSplashPoints)
		G::BoxStorage.emplace_back(tSetup.m_vPoint, Vec3::Get(-1), Vec3::Get(1), Vec3(), I::GlobalVars->curtime + 60.f, Vars::Colors::Local.Value, Color_t(0, 0, 0, 0));
#endif
}

static float SplashRadiusSqr(const Info_t& info, int simTime, bool air)
{
	float radius = info.m_flRadius;
	if (air && Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::AirSplash
		&& !info.m_pProjectile && info.m_pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
	{
		static auto tf_grenadelauncher_livetime = H::ConVars.FindVar("tf_grenadelauncher_livetime");
		static auto tf_sticky_radius_ramp_time = H::ConVars.FindVar("tf_sticky_radius_ramp_time");
		static auto tf_sticky_airdet_radius = H::ConVars.FindVar("tf_sticky_airdet_radius");
		const float liveTime = tf_grenadelauncher_livetime->GetFloat();
		radius *= Math::RemapVal(TICKS_TO_TIME(simTime), liveTime, liveTime + tf_sticky_radius_ramp_time->GetFloat(), tf_sticky_airdet_radius->GetFloat(), 1.f);
	}
	return powf(radius, 2);
}

static bool SplashInRange(const Info_t& info, const Vec3& impact, int simTime, bool air, const RestoreInfo_t& original)
{
	// The caller has placed the target at its predicted origin. Use its full
	// collision bounds, not the narrowed bounds used for direct-hit validation.
	auto* entity = info.m_pTarget->m_pEntity;
	const Vec3 mins = entity->m_vecMins(), maxs = entity->m_vecMaxs();
	entity->m_vecMins() = original.m_vMins;
	entity->m_vecMaxs() = original.m_vMaxs;
	Vec3 nearest;
	entity->m_Collision()->CalcNearestPoint(impact, &nearest);
	entity->m_vecMins() = mins;
	entity->m_vecMaxs() = maxs;
	const float distanceSqr = impact.DistToSqr(nearest);
	const float radiusSqr = SplashRadiusSqr(info, simTime, air);
	return std::isfinite(distanceSqr) && std::isfinite(radiusSqr) && distanceSqr < radiusSqr;
}

void CAimbotProjectile::GetSplashPoints(Vec3 vOrigin, std::vector<Setup_t>& vSplashPoints, std::vector<Point_t>& vPoints, int iSimTime, uint8_t iFlags, bool bFirst, int* batchSize)
{
    ProjectileDiagnostics::Profile selection(ProjectileDiagnostics::SplashSelection);
    vPoints.clear();
    vPoints.reserve(vSplashPoints.size());
    if (batchSize) *batchSize = 1;

	m_tInfo.m_pTarget->m_vPos = vOrigin;
	// Histories can outlive their candidate pool. No entity relocation or
	// collision queries are needed once all setups have been consumed.
	if (vSplashPoints.empty())
		return;
	const float flRadiusSqr = SplashRadiusSqr(m_tInfo, iSimTime, false);
	const float flRadiusAirSqr = SplashRadiusSqr(m_tInfo, iSimTime, true);
	bool bLob = iFlags & CalculateFlagsEnum::LobAngle;
	int iTolerance = m_tInfo.m_bIgnoreTiming && bLob ? std::numeric_limits<int>::max() : m_tInfo.m_iArmTime ? -1 : 0;
	int iLimit = !bFirst ? m_tInfo.m_iSplashRestrict : std::max(m_tInfo.m_iSplashRestrict, Vars::Aimbot::Projectile::SplashRestrictFirst.Value);
	bool bSort = !bLob || !m_tInfo.m_bIgnoreTiming;
    iLimit = std::max(1, iLimit);
    if (batchSize) *batchSize = iLimit;

    // Evaluate the original collision-based radius test at the predicted origin,
    // then restore the entity before any angle solver or launch trace runs.
    // Keep out-of-radius setups for later histories: a moving target may enter range.
    m_vSplashDistances.clear();
    m_vSplashDistances.reserve(vSplashPoints.size());
    auto* entity = m_tInfo.m_pTarget->m_pEntity;
    const Vec3 original = entity->GetAbsOrigin();
    entity->SetAbsOrigin(vOrigin);
    for (const auto& setup : vSplashPoints)
    {
        if (!SearchAllowed(SearchWork::Selection)) break;
        Vec3 nearest;
        entity->m_Collision()->CalcNearestPoint(setup.m_vPoint, &nearest);
        m_vSplashDistances.push_back(setup.m_vPoint.DistToSqr(nearest));
    }
    entity->SetAbsOrigin(original);
    // Never return from the distance pass while the entity is relocated.
    if (m_vSplashDistances.size() != vSplashPoints.size()) return;

	// stable in-place compaction (kept points retain their order) instead of erasing one by one
	auto itKeep = vSplashPoints.begin(), it = vSplashPoints.begin();
	for (; it != vSplashPoints.end(); ++it)
	{
		if (!SearchStep(SearchWork::Selection)) break;
		Point_t tPoint = { it->m_vPoint, {}, it->m_iType };
        const size_t index = size_t(it - vSplashPoints.begin());
        tPoint.m_iSearchOrder = index;
        if (ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.considered;
        const float radius = tPoint.m_iType == PointTypeEnum::Geometry ? flRadiusSqr : flRadiusAirSqr;
        if (!(m_vSplashDistances[index] < radius))
        {
            if (ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.radiusRejected;
            if (itKeep != it) *itKeep = *it;
            ++itKeep;
            continue;
        }

        if (ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.angleSolves;
        {
            ProjectileDiagnostics::Profile angles(ProjectileDiagnostics::SplashAngles);
            CalculateAngle(m_tInfo.m_vLocalEye, tPoint.m_vPoint, iSimTime, tPoint.m_tSolution, iFlags, iTolerance);
        }
		if (tPoint.m_tSolution.m_iCalculated != CalculateResultEnum::Good)
		{
			if (tPoint.m_tSolution.m_iCalculated != CalculateResultEnum::Bad)
			{	// keep
				if (itKeep != it)
					*itKeep = *it;
				++itKeep;
			}
			continue;
		}
		else if (tPoint.m_iType == PointTypeEnum::Air && tPoint.m_tSolution.m_flTime < TICKS_TO_TIME(m_tInfo.m_iArmTime))
			continue;

        // An angle that cannot be used must not occupy the bounded shortlist.
        // Keep its setup for later target histories, where the applied angle
        // may enter the user's cone. Use the same applied-angle policy as HandlePoint.
        Vec3 applied;
        Aim(G::CurrentUserCmd->viewangles,
            { tPoint.m_tSolution.m_flPitch, tPoint.m_tSolution.m_flYaw, 0.f }, applied);
        if (!CandidateAngleAllowed(applied, tPoint.m_vPoint, vOrigin))
        {
            ProjectileDiagnostics::Add(ProjectileDiagnostics::AngleRejected);
            if (ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.angleFiltered;
            ProjectileDiagnostics::AngleRejection(m_tInfo.m_pTarget->m_pEntity->entindex(),
                tPoint.m_iType, applied, tPoint.m_vPoint, vOrigin, "before_shortlist");
            if (itKeep != it) *itKeep = *it;
            ++itKeep;
            continue;
        }

		vPoints.push_back(tPoint);
		if (!bSort && vPoints.size() == iLimit)
		{
			++it;
			break;
		}
	}
	if (itKeep != it)
		vSplashPoints.erase(std::move(it, vSplashPoints.end(), itKeep), vSplashPoints.end());
	if (vPoints.empty())
		return;

	size_t shortlisted = vPoints.size();
	if (bSort)
	{
        if (batchSize)
            shortlisted = SplashSearchPolicy::NextNearestBatch(vPoints, vOrigin, 0, iLimit);
        else
        {
            SplashSearchPolicy::TakeNearest(vPoints, vOrigin, iLimit);
            shortlisted = vPoints.size();
        }
	}
    if (ProjectileDiagnostics::current) ProjectileDiagnostics::splash.shortlisted += int(shortlisted);
}

static inline Vec3 PullPoint(const Vec3& vPoint, Vec3 vLocalPos, Info_t& tInfo, const Vec3& vTargetPos, const Vec3& vMins, const Vec3& vMaxs)
{
	// A straight trajectory keeps the shooter's origin. If the gravity solve
	// fails, leave the aim point unchanged rather than constructing a bogus ray.
	const float flGrav = tInfo.m_flGravity;
	if (flGrav)
	{
		Vec3 vDelta = vTargetPos - vLocalPos;
		float flDist = vDelta.Length2D();

		ArcPolicy::Solution solution;
		if (!ArcPolicy::Solve(flDist,vDelta.z,tInfo.m_flVelocity,flGrav,false,solution))
			return vPoint;
		float flTime = float(solution.time) - tInfo.m_flOffsetTime;
		vLocalPos += Vec3(0, 0, flGrav * powf(flTime, 2) / 2);
	}
	Vec3 vForward, vRight, vUp; Math::AngleVectors(Math::CalcAngle(vLocalPos, vPoint), &vForward, &vRight, &vUp);
	vLocalPos += (vForward * tInfo.m_vOffset.x) + (vRight * tInfo.m_vOffset.y) + (vUp * tInfo.m_vOffset.z);
	return Math::PullPoint(vPoint, vLocalPos, vTargetPos, vMins, vMaxs);
}



static inline float GetDrag(float flVelocity, ProjectileInfo* pInfo, int iFlags)
{	// stupid, would like not to hardcode magic values
	if (!(iFlags & CalculateFlagsEnum::AccountDrag) || !F::ProjSim.m_bPhysics)
		return 0.f;

	static float flRegularDrag = 0.f, flLobDrag = 0.f;
    if (!pInfo || !pInfo->m_pWeapon) return 0.f;
    static ArcPolicy::DragKey cached;
    static bool cachedValid=false;
    const bool physics=F::ProjSim.m_bPhysics;
    const bool lob=bool(Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::LobAngles);
    const bool noSpin=pInfo->m_uType==FNV1A::Hash32Const("models/weapons/w_models/w_grenade_grenadelauncher.mdl")
        && SDK::AttribHookValue(0,"grenade_no_spin",pInfo->m_pWeapon);
    const float overrideDrag=Vars::Aimbot::Projectile::DragOverride.Value;
    const ArcPolicy::DragKey key={flVelocity,overrideDrag,pInfo->m_uType,physics,lob,noSpin};
	if (!cachedValid || !cached.Matches(key))
	{
		auto fGetDrag = [&](std::function<float()> fGetTypeDrag)
		{
			if (!F::ProjSim.m_bPhysics)
				return 0.f;

			if (Vars::Aimbot::Projectile::DragOverride.Value)
				return Vars::Aimbot::Projectile::DragOverride.Value;

			return fGetTypeDrag();
		};
		auto fGetRegularDrag = [&]()
		{
			switch (pInfo->m_uType)
			{
			case FNV1A::Hash32Const("models/weapons/w_models/w_grenade_grenadelauncher.mdl"):
				if (!SDK::AttribHookValue(0, "grenade_no_spin", pInfo->m_pWeapon))
					return Math::RemapVal(flVelocity, 1217.f, k_flMaxVelocity, 0.120f, 0.200f); // 0.120 normal, 0.200 capped, 0.300 v3000
				else
					return Math::RemapVal(flVelocity, 1217.f, k_flMaxVelocity, 0.060f, 0.085f); // 0.060 normal, 0.085 capped, 0.120 v3000
			case FNV1A::Hash32Const("models/weapons/w_models/w_cannonball.mdl"):
				return Math::RemapVal(flVelocity, 1454.f, k_flMaxVelocity, 0.385f, 0.530f); // 0.385 normal, 0.530 capped, 0.790 v3000
			case FNV1A::Hash32Const("models/weapons/w_models/w_stickybomb.mdl"):
				return Math::RemapVal(flVelocity, 922.f, k_flMaxVelocity, 0.090f, 0.190f); // 0.085 low, 0.190 capped, 0.230 v2400
			case FNV1A::Hash32Const("models/workshop_partner/weapons/c_models/c_sd_cleaver/c_sd_cleaver.mdl"):
				return 0.310f;
			case FNV1A::Hash32Const("models/weapons/w_models/w_baseball.mdl"):
				return 0.180f;
			case FNV1A::Hash32Const("models/weapons/c_models/c_xms_festive_ornament.mdl"):
				return 0.285f;
			case FNV1A::Hash32Const("models/weapons/c_models/urinejar.mdl"):
			case FNV1A::Hash32Const("models/workshop/weapons/c_models/c_madmilk/c_madmilk.mdl"):
			case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster.mdl"):
			case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster_milk.mdl"):
				return 0.057f;
			case FNV1A::Hash32Const("models/weapons/c_models/c_gascan/c_gascan.mdl"):
				return 0.530f;
			}
			return 0.f;
		};
		auto fGetLobDrag = [&]()
		{
			if (!(Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::LobAngles))
				return 0.f;

			switch (pInfo->m_uType)
			{
			case FNV1A::Hash32Const("models/weapons/w_models/w_grenade_grenadelauncher.mdl"):
				if (!SDK::AttribHookValue(0, "grenade_no_spin", pInfo->m_pWeapon))
					return Math::RemapVal(flVelocity, 1217.f, k_flMaxVelocity, 0.056f, 0.062f);
				else
					return Math::RemapVal(flVelocity, 1217.f, k_flMaxVelocity, 0.030f, 0.033f);
			case FNV1A::Hash32Const("models/weapons/w_models/w_cannonball.mdl"):
				return Math::RemapVal(flVelocity, 1454.f, k_flMaxVelocity, 0.099f, 0.092f);
			case FNV1A::Hash32Const("models/weapons/w_models/w_stickybomb.mdl"):
				return Math::RemapVal(flVelocity, 922.f, k_flMaxVelocity, 0.048f, 0.060f);
			case FNV1A::Hash32Const("models/workshop_partner/weapons/c_models/c_sd_cleaver/c_sd_cleaver.mdl"):
				return 0.075f;
			case FNV1A::Hash32Const("models/weapons/w_models/w_baseball.mdl"):
				return 0.057f;
			case FNV1A::Hash32Const("models/weapons/c_models/c_xms_festive_ornament.mdl"):
				return 0.072f;
			case FNV1A::Hash32Const("models/weapons/c_models/urinejar.mdl"):
			case FNV1A::Hash32Const("models/workshop/weapons/c_models/c_madmilk/c_madmilk.mdl"):
			case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster.mdl"):
			case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster_milk.mdl"):
				return 0.030f;
			case FNV1A::Hash32Const("models/weapons/c_models/c_gascan/c_gascan.mdl"):
				return 0.089f;
			}
			return 0.f;
		};

        cached=key;cachedValid=true;
		flRegularDrag = fGetDrag(fGetRegularDrag);
		flLobDrag = fGetDrag(fGetLobDrag);
	}

	return !(iFlags & CalculateFlagsEnum::LobAngle) ? flRegularDrag : flLobDrag;
}

static inline bool GetAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, int iFlags, Info_t& tInfo, float& flPitch, float& flYaw, float& flTime)
{
	float flVelocity = tInfo.m_flVelocity;
	float flGrav = tInfo.m_flGravity;
	Vec3 vDelta = vTargetPos - vLocalPos;
	float flDist = vDelta.Length2D();
	float flDrag = GetDrag(flVelocity, F::ProjSim.m_pCurrent, iFlags);
	if (!std::isfinite(vDelta.x)||!std::isfinite(vDelta.y)||!std::isfinite(vDelta.z)||!std::isfinite(flDrag)||flDrag<0) return false;

	Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vTargetPos);

	flYaw = vAngleTo.y;

	ArcPolicy::Solution solution;
    const bool lob=bool(iFlags & CalculateFlagsEnum::LobAngle);
    if (!ArcPolicy::Solve(flDist,vDelta.z,flVelocity,flGrav,lob,solution)) return false;
    flPitch=float(solution.pitch);flTime=float(solution.time);
	if (flGrav && flDrag)
		{
            // Keep the calibrated nonvertical drag model, but never feed zero or
            // negative effective speed into the ballistic solver.
            if (flDist<.001f) return false;
            const float initialTime=flTime;
            flVelocity*=1-flDrag*initialTime;
            if (!ArcPolicy::Solve(flDist,vDelta.z,flVelocity,flGrav,lob,solution)) return false;
            flPitch=float(solution.pitch);
            flVelocity=flVelocity/(1+.5f*(1-std::exp(-flDrag*initialTime)));
            flVelocity+=vDelta.z*TICK_INTERVAL*initialTime;
            if (!std::isfinite(flVelocity)||flVelocity<=0) return false;
            flTime=flDist/(flVelocity*std::cos(flPitch));
		}

	if (Vars::Aimbot::Projectile::TimeOverride.Value)
		flTime *= Vars::Aimbot::Projectile::TimeOverride.Value;

	return std::isfinite(flPitch)&&std::isfinite(flYaw)&&std::isfinite(flTime)&&flTime>0&&flTime<=60.f;
}

void CAimbotProjectile::CalculateAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, int iSimTime, Solution_t& tOut, uint8_t iFlags, int iTolerance)
{
	if (tOut.m_iCalculated != CalculateResultEnum::Pending)
		return;

	float flPitch, flYaw;
	{	// basic trajectory pass
		//Vec3 vForward, vRight, vUp; Math::AngleVectors(Math::CalcAngle(vLocalPos, vTargetPos), &vForward, &vRight, &vUp);
		//Vec3 vShootPos = vLocalPos + vForward * m_tInfo.m_vOffset.x + vRight * m_tInfo.m_vOffset.y + vUp * m_tInfo.m_vOffset.z;
		if (!GetAngle(vLocalPos, vTargetPos, iFlags, m_tInfo, flPitch, flYaw, tOut.m_flTime))
		{
			tOut.m_iCalculated = CalculateResultEnum::Bad;
			return;
		}

		tOut.m_flTime -= m_tInfo.m_flOffsetTime;
		tOut.m_flPitch = flPitch = -Math::Rad2Deg(flPitch) - m_tInfo.m_vAngFix.x;
		tOut.m_flYaw = flYaw -= m_tInfo.m_vAngFix.y;
	}
	if (!std::isfinite(tOut.m_flTime)||tOut.m_flTime<=0 || !std::isfinite(flPitch)||!std::isfinite(flYaw))
    {tOut.m_iCalculated=CalculateResultEnum::Bad;return;}

	int iTimeTo = ceilf(tOut.m_flTime / TICK_INTERVAL);
	bool bGood = iTolerance != -1 ? abs(iTimeTo - iSimTime) <= iTolerance : iTimeTo <= iSimTime;
	if (!(iFlags & CalculateFlagsEnum::TwoPass) || m_tInfo.m_vOffset.IsZero())
	{
		tOut.m_iCalculated = bGood ? CalculateResultEnum::Good : CalculateResultEnum::Time;
		return;
	}
	else if (!bGood)
	{
		tOut.m_iCalculated = CalculateResultEnum::Time;
		return;
	}

	int iSimFlags = (iFlags & CalculateFlagsEnum::SetupClip ? ProjSimEnum::Redirect : ProjSimEnum::None) | ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum;
#ifdef SPLASH_DEBUG5
	if (iSimFlags & ProjSimEnum::Redirect)
	{
		if (Vars::Visuals::Trajectory::Override.Value)
		{
			if (Vars::Visuals::Trajectory::ForwardRedirect.Value)
				s_mTraceCount[__FUNCTION__": setup trace"]++;
		}
		else
		{
			switch (m_tInfo.m_pWeapon->GetWeaponID())
			{
			case TF_WEAPON_ROCKETLAUNCHER:
			case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
			case TF_WEAPON_PARTICLE_CANNON:
			case TF_WEAPON_RAYGUN:
			case TF_WEAPON_DRG_POMSON:
			case TF_WEAPON_FLAREGUN:
			case TF_WEAPON_FLAREGUN_REVENGE:
			case TF_WEAPON_COMPOUND_BOW:
			case TF_WEAPON_CROSSBOW:
			case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
			case TF_WEAPON_SYRINGEGUN_MEDIC:
				s_mTraceCount[__FUNCTION__": setup trace"]++;
			}
		}
	}
#endif
	m_tProjInfo = {};
	if (!F::ProjSim.GetInfo(m_tInfo.m_pLocal, m_tInfo.m_pWeapon, { flPitch, flYaw, 0 }, m_tProjInfo, iSimFlags))
	{
		tOut.m_iCalculated = CalculateResultEnum::Bad;
		return;
	}

	{	// calculate trajectory from projectile origin
		if (!GetAngle(m_tProjInfo.m_vPos, vTargetPos, iFlags, m_tInfo, tOut.m_flPitch, tOut.m_flYaw, tOut.m_flTime))
		{
			tOut.m_iCalculated = CalculateResultEnum::Bad;
			return;
		}

		{	// correct yaw
			Vec3 vShootPos = (m_tProjInfo.m_vPos - vLocalPos).To2D();
			Vec3 vTarget = vTargetPos - vLocalPos;
			Vec3 vForward; Math::AngleVectors(m_tProjInfo.m_vAng, &vForward); vForward.Normalize2D();
			float flB = 2 * (vShootPos.x * vForward.x + vShootPos.y * vForward.y);
			float flC = vShootPos.Length2DSqr() - vTarget.Length2DSqr();
			float flSolution;
			if (Math::SolveQuadraticFirst(1.f, flB, flC, flSolution))
			{
				vShootPos += vForward * flSolution;
				tOut.m_flYaw = flYaw - (Math::Rad2Deg(atan2(vShootPos.y, vShootPos.x)) - flYaw);
				flYaw = Math::Rad2Deg(atan2(vShootPos.y, vShootPos.x));
			}
		}

		{	// correct pitch
			if (m_tInfo.m_flGravity)
			{
				flPitch -= m_tProjInfo.m_vAng.x;
				tOut.m_flPitch = -Math::Rad2Deg(tOut.m_flPitch) + flPitch - m_tInfo.m_vAngFix.x;
			}
			else
			{
				Vec3 vShootPos = Math::RotatePoint(m_tProjInfo.m_vPos - vLocalPos, {}, { 0, -flYaw, 0 }); vShootPos.y = 0;
				Vec3 vTarget = Math::RotatePoint(vTargetPos - vLocalPos, {}, { 0, -flYaw, 0 });
				Vec3 vForward; Math::AngleVectors(m_tProjInfo.m_vAng - Vec3(0, flYaw, 0), &vForward); vForward.y = 0; vForward.Normalize();
				float flB = 2 * (vShootPos.x * vForward.x + vShootPos.z * vForward.z);
				float flC = (powf(vShootPos.x, 2) + powf(vShootPos.z, 2)) - (powf(vTarget.x, 2) + powf(vTarget.z, 2));
				float flSolution;
				if (Math::SolveQuadraticFirst(1.f, flB, flC, flSolution))
				{
					vShootPos += vForward * flSolution;
					tOut.m_flPitch = flPitch - (Math::Rad2Deg(atan2(-vShootPos.z, vShootPos.x)) - flPitch);
				}
			}
		}
	}

	if (!std::isfinite(tOut.m_flTime)||tOut.m_flTime<=0 || !std::isfinite(tOut.m_flPitch)||!std::isfinite(tOut.m_flYaw))
    {tOut.m_iCalculated=CalculateResultEnum::Bad;return;}
	iTimeTo = ceilf(tOut.m_flTime / TICK_INTERVAL);
	bGood = iTolerance != -1 ? abs(iTimeTo - iSimTime) <= iTolerance : iTimeTo <= iSimTime;
	tOut.m_iCalculated = bGood ? CalculateResultEnum::Good : CalculateResultEnum::Time;
}



struct SafetyPlayerHull
{
    CTFPlayer* player;
    Vec3 origin, networkOrigin, mins, maxs;
};

static std::vector<SafetyPlayerHull> SafetyPlayers(CTFPlayer* local)
{
    std::vector<SafetyPlayerHull> result;
    for(auto entity:H::Entities.GetGroup(EntityEnum::PlayerAll))
    {
        auto player=entity->As<CTFPlayer>();
        if(player==local || !player->IsAlive() || player->IsAGhost() || player->IsDormant()
            || (!SDK::FriendlyFire() && player->m_iTeamNum()==local->m_iTeamNum())) continue;
        // Use collision bounds, not a chosen aim hitbox or a distant predicted target position.
        result.push_back({player,player->GetAbsOrigin(),player->m_vecOrigin(),player->m_vecMins(),player->m_vecMaxs()});
    }
    return result;
}

static bool SafetyPlayerCollision(const Vec3& from,const Vec3& to,const Vec3& hull,
    const std::vector<SafetyPlayerHull>& players,CGameTrace& trace)
{
    if(trace.startsolid || trace.allsolid) return false;
    const auto xyz=[](const Vec3& v)->RocketSafetyGeometry::V3{return {v.x,v.y,v.z};};
    bool changed=false;
    for(const auto& p:players)
    {
        for(const auto& origin:{p.origin,p.networkOrigin})
        {
            float fraction=1.f; RocketSafetyGeometry::V3 normal={};
            if(!RocketSafetyGeometry::SegmentBox(xyz(from),xyz(to),xyz(origin+p.mins-hull),xyz(origin+p.maxs+hull),fraction,normal)
                || fraction>trace.fraction) continue;
            trace.fraction=fraction; trace.endpos=from+(to-from)*fraction;
            trace.plane.normal={normal[0],normal[1],normal[2]}; trace.m_pEnt=p.player;
            changed=true;
        }
    }
    return changed;
}

static bool WouldSplashLocal(CTFPlayer* local, CTFWeaponBase* weapon, const Vec3& impact, float flightTime, bool diagnosticPriority=false)
{
    if (!(Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::PreventSelfDamage) ||
        Vars::Aimbot::Projectile::SelfDamageProtection.Value<=0 || local->IsInvulnerable())
        return false;
    if (weapon->m_iItemDefinitionIndex() == Soldier_m_RocketJumper ||
        weapon->m_iItemDefinitionIndex() == Demoman_s_StickyJumper)
        return false;
    float radius = 0.f;
    switch (weapon->GetWeaponID())
    {
    case TF_WEAPON_ROCKETLAUNCHER:
    case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
    case TF_WEAPON_PARTICLE_CANNON:
        // Attacker blast radius is distinct from reduced enemy splash radius (e.g. Direct Hit).
        radius = std::max(float(TF_ROCKET_RADIUS),F::AimbotProjectile.GetSplashRadius(weapon, local, 1.f)); break;
    case TF_WEAPON_GRENADELAUNCHER:
    case TF_WEAPON_CANNON:
        radius = SDK::AttribHookValue(TF_ROCKET_RADIUS, "mult_explosion_radius", weapon); break;
    case TF_WEAPON_FLAREGUN:
    case TF_WEAPON_FLAREGUN_REVENGE:
        if (weapon->As<CTFFlareGun>()->GetFlareGunType() == FLAREGUN_SCORCHSHOT)
            radius = SDK::AttribHookValue(TF_FLARE_DET_RADIUS, "mult_explosion_radius", weapon);
        break;
    default: return false; // Sticky/manual detonation has its own existing safety option.
    }
    // Bounded, world-colliding local projection. Do not nest another engine movesim
    // inside target prediction or assume that a straight path can pass through walls.
    const Vec3 origin = local->GetAbsOrigin();
    Vec3 predicted = origin, velocity = local->m_vecVelocity();
    bool grounded = local->IsOnGround();
    const bool usableHorizon = std::isfinite(flightTime) && flightTime > 0.f && flightTime <= 2.f
        && std::isfinite(velocity.x) && std::isfinite(velocity.y) && std::isfinite(velocity.z);
    if (usableHorizon)
    {
        const int steps = std::clamp(int(std::ceil(flightTime / TICK_INTERVAL)), 1, 16);
        const float dt = flightTime / steps;
        CTraceFilterWorldAndPropsOnly filter; filter.pSkip = local;
        for (int i=0;i<steps;++i)
        {
            if (grounded) velocity.z=0.f;
            Vec3 displacement = velocity * dt;
            if (!grounded && !local->IsSwimming())
            { displacement.z -= SDK::GetGravity()*dt*dt*.5f; velocity.z -= SDK::GetGravity()*dt; }
            CGameTrace trace = {};
            SDK::TraceHull(predicted, predicted+displacement, local->m_vecMins(), local->m_vecMaxs(), MASK_PLAYERSOLID, &filter, &trace);
            if (trace.startsolid || trace.allsolid) { predicted=origin; break; }
            predicted=trace.endpos;
            if (trace.DidHit()) break; // Conservative stop; no invented sliding or step-up.
            if (grounded)
            {
                CGameTrace floor = {};
                SDK::TraceHull(predicted, predicted-Vec3(0,0,2), local->m_vecMins(), local->m_vecMaxs(), MASK_PLAYERSOLID, &filter, &floor);
                grounded = floor.DidHit() && floor.plane.normal.z >= .707f;
            }
        }
    }
    // Never assume future movement alone will rescue a face-range shot.
    float damage=std::max(SelfDamage::Estimate(local,weapon,impact,origin,radius,diagnosticPriority),
        SelfDamage::Estimate(local,weapon,impact,predicted,radius,diagnosticPriority));
    const bool blocked=SelfDamage::Block(local,damage);
    if(SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("splash_decision",std::format("final_guard={} blocked={} damage={} protection={} hp={} maxhp={} flight={} projection_valid={} current={},{},{} predicted={},{},{}",diagnosticPriority,blocked,damage,Vars::Aimbot::Projectile::SelfDamageProtection.Value,local->m_iHealth(),local->GetMaxHealth(),flightTime,usableHorizon,origin.x,origin.y,origin.z,predicted.x,predicted.y,predicted.z),!diagnosticPriority);
    return blocked;
}

static bool OutgoingWallBlocked(CTFPlayer* local,CTFWeaponBase* weapon,const Vec3& angles,bool sideProbe=false,ProjectileMuzzlePolicy::Probe* evidence=nullptr)
{
    if(evidence) *evidence={};
    ProjectileInfo actual={};
    const int flags=sideProbe?ProjSimEnum::Redirect|ProjSimEnum::NoRandomAngles|ProjSimEnum::DiagnosticDeterministic
        :ProjSimEnum::Redirect|ProjSimEnum::PredictCmdNum;
    if(!F::ProjSim.GetInfo(local,weapon,angles,actual,flags)) return true;
    const auto finite=[](const Vec3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
    if(!finite(actual.m_vPos) || !finite(actual.m_vHull) || !std::isfinite(actual.m_flVelocity) || actual.m_flVelocity<=0) return true;
    CTraceFilterWorldAndPropsOnly world={};world.pSkip=local;
    CGameTrace launch={};
    SDK::TraceHull(local->GetShootPos(),actual.m_vPos,-actual.m_vHull,actual.m_vHull,MASK_SOLID,&world,&launch);
    if(!std::isfinite(launch.fraction) || !finite(launch.endpos)) return true;
    if(launch.startsolid || launch.allsolid || launch.fraction<1.f)
    {
        if(evidence) *evidence={true,true,launch.endpos.DistTo(local->GetShootPos()),0,"muzzle_obstructed"};
        return true;
    }
    if(!F::ProjSim.Initialize(actual)) return true;
    CTraceFilterCollideable filter={};filter.pSkip=local;filter.iPlayer=PLAYER_DEFAULT;filter.bMisc=true;
    int mask=MASK_SOLID;F::ProjSim.SetupTrace(filter,mask,weapon);
    Vec3 from=actual.m_vPos;
    // Bounded launch-only guard. Distant intentional splash remains available.
    const float range=sideProbe?ProjectileMuzzlePolicy::SwitchProbeDistance:ProjectileMuzzlePolicy::NearWallDistance;
    for(int tick=1;tick<=(sideProbe?16:8);++tick)
    {
        F::ProjSim.RunTick(actual);const Vec3 to=F::ProjSim.GetOrigin();CGameTrace hit={};
        if(!finite(to)) return true;
        SDK::TraceHull(from,to,-actual.m_vHull,actual.m_vHull,mask,&filter,&hit);
        if(!std::isfinite(hit.fraction) || !finite(hit.endpos)) return true;
        if(hit.DidHit() || hit.startsolid || hit.allsolid)
        {
            const bool actor=hit.m_pEnt && (hit.m_pEnt->IsPlayer() || hit.m_pEnt->IsBuilding());
            const float distance=hit.endpos.DistTo(actual.m_vPos);
            const bool blocked=sideProbe?ProjectileMuzzlePolicy::SwitchImpact(hit.DidHit(),hit.startsolid || hit.allsolid,actor,distance)
                :ProjectileMuzzlePolicy::RejectImpact(AutoViewmodelSwitch::DelayedBomb(weapon),hit.DidHit(),hit.startsolid || hit.allsolid,actor,hit.endpos.DistTo(actual.m_vPos));
            if(evidence) *evidence={true,blocked,distance,tick,actor?"actor_first":blocked?"path_obstructed":"impact_beyond_probe"};
            return blocked;
        }
        const float distance=to.DistTo(actual.m_vPos);
        if(distance>=range)
        {
            if(evidence) *evidence={true,false,distance,tick,"clear_to_range"};
            return false;
        }
        from=to;
    }
    if(evidence) evidence->reason="incomplete_probe";
    return false;
}

bool CAimbotProjectile::ManageViewmodel(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd,bool noTargets)
{
    if(!local || !weapon || !local->IsAlive() || local->IsAGhost()
        || local->IsTaunting() || cmd->weaponselect || I::EngineVGui->IsGameUIVisible()) return false;
    // Once a hitscan switch has taken effect, no projectile-side clearance is needed.
    if(G::PrimaryWeaponType==EWeaponType::HITSCAN)
    {
        auto* flip=H::ConVars.FindVar("cl_flipviewmodels");
        if(AutoViewmodelSwitch::Enabled() && AutoViewmodelSwitch::owned && flip && flip->GetBool()
            && !AutoViewmodelSwitch::Pending()) AutoViewmodelSwitch::Commit(false,cmd->command_number);
        return false;
    }
    if(G::PrimaryWeaponType!=EWeaponType::PROJECTILE) return false;
    const bool sticky=weapon->GetWeaponID()==TF_WEAPON_PIPEBOMBLAUNCHER;
    const bool charged=sticky || weapon->GetWeaponID()==TF_WEAPON_COMPOUND_BOW;
    const float charge=charged?weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime():0.f;
    // Only switches made before charging are supported. Never seize an existing
    // manual charge/release, or hold it until an unsafe maximum-charge deadline.
    if((AutoViewmodelSwitch::Immediate(weapon) || charged) && AutoViewmodelSwitch::Pending())
    {
        // Prediction may have begun a charge from raw input before this hook.
        // Keep that newly started charge held instead of accidentally releasing it.
        if(charged && charge>0.f) cmd->buttons|=IN_ATTACK;
        else cmd->buttons&=~IN_ATTACK;
        return true;
    }
    if(!AutoViewmodelSwitch::Supported(weapon)) return false;
    if(!AutoViewmodelSwitch::Enabled() || I::ClientState->chokedcommands || !I::EngineClient->IsConnected()) return false;
    // With active automated aiming, the target solver tests complete alternate
    // trajectories. With aim off/inactive (or manual fire), use current angles only.
    if(!noTargets && Vars::Aimbot::General::AimType.Value && F::AimbotGlobal.ShouldAim()
        && Vars::Aimbot::General::AutoShoot.Value && !(G::OriginalCmd.buttons&IN_ATTACK)) return false;
    // Check every fresh command, including idle aim-off commands. The former
    // 80 ms throttle could miss a short corner exposure before a shot.
    static int lastProbeCommand=-1;
    if(lastProbeCommand==cmd->command_number) return false;
    lastProbeCommand=cmd->command_number;
    ProjectileMuzzlePolicy::Probe current,alternateProbe;
    if(!ProjectileMuzzlePolicy::ChargedSwitchSafe(charged,charge)) return false;
    auto* preferredFlip=H::ConVars.FindVar("cl_flipviewmodels");
    if(AutoViewmodelSwitch::owned && preferredFlip && preferredFlip->GetBool())
    {
        ProjectileMuzzlePolicy::Probe right;
        {ProjectileMuzzlePolicy::Trial trial(false);OutgoingWallBlocked(local,weapon,cmd->viewangles,true,&right);}
        if(ProjectileMuzzlePolicy::ReturnRight(AutoViewmodelSwitch::owned,true,right)
            && AutoViewmodelSwitch::Commit(false,cmd->command_number))
        {cmd->buttons&=~IN_ATTACK;return true;}
    }
    OutgoingWallBlocked(local,weapon,cmd->viewangles,true,&current);
    if(!current.valid || !current.blocked)
    {
        if(SelfDamageDiagnostics::Enabled())
        {
            static unsigned long long lastClearLog=0;const auto now=GetTickCount64();
            if(!lastClearLog || now-lastClearLog>=500)
            {
                lastClearLog=now;
                SelfDamageDiagnostics::Write("projectile_viewmodel_probe",std::format(
                    "cmd={} aim={} weapon={} item={} current_valid={} current_blocked={} reason={} distance={} steps={} probe_range={} origin={},{},{} angles={},{},{}",
                    cmd->command_number,Vars::Aimbot::General::AimType.Value,weapon->GetWeaponID(),weapon->m_iItemDefinitionIndex(),
                    current.valid,current.blocked,current.reason,current.distance,current.steps,ProjectileMuzzlePolicy::SwitchProbeDistance,
                    local->GetShootPos().x,local->GetShootPos().y,local->GetShootPos().z,cmd->viewangles.x,cmd->viewangles.y,cmd->viewangles.z));
            }
        }
        return false;
    }
    auto* flip=H::ConVars.FindVar("cl_flipviewmodels");if(!flip) return false;
    const bool alternate=!flip->GetBool();
    {ProjectileMuzzlePolicy::Trial trial(alternate);OutgoingWallBlocked(local,weapon,cmd->viewangles,true,&alternateProbe);}
    const bool clear=ProjectileMuzzlePolicy::CanSwitch(current,alternateProbe);
    if(SelfDamageDiagnostics::Enabled())
    {
        static unsigned long long lastProbeLog=0;const auto now=GetTickCount64();
        if(clear || !lastProbeLog || now-lastProbeLog>=250)
        {
            lastProbeLog=now;
            SelfDamageDiagnostics::Write("projectile_viewmodel_manual_probe",std::format(
                "cmd={} aim={} weapon={} item={} sticky={} charge={} current_blocked=1 alternate_clear={} current_reason={} alternate_reason={} current_distance={} alternate_distance={} current_steps={} alternate_steps={} alternate_valid={} probe_range={} angles={},{},{}",
                cmd->command_number,Vars::Aimbot::General::AimType.Value,weapon->GetWeaponID(),weapon->m_iItemDefinitionIndex(),sticky,charge,clear,
                current.reason,alternateProbe.reason,current.distance,alternateProbe.distance,current.steps,alternateProbe.steps,alternateProbe.valid,
                ProjectileMuzzlePolicy::SwitchProbeDistance,cmd->viewangles.x,cmd->viewangles.y,cmd->viewangles.z));
        }
    }
    if(!clear || !AutoViewmodelSwitch::Commit(alternate,cmd->command_number)) return false;
    cmd->buttons&=~IN_ATTACK;
    return true;
}

bool CAimbotProjectile::TestAngle(const Vec3& vPoint, const Vec3& vAngles, int iSimTime, uint8_t iType, uint8_t iFlags, bool bSecondTest)
{
    // Once started, a candidate runs its complete collision/safety checks.
    if (!SearchStep(SearchWork::Validation)) return false;
    ProjectileDiagnostics::Add(ProjectileDiagnostics::Tests);
    if(iSimTime<=0) ProjectileDiagnostics::Add(ProjectileDiagnostics::NoTicks);
#ifdef NIKOGRAM_PRIVATE_LEARNING
    m_DiagnosticScratch.clear();m_DiagnosticScratchCount=0;
    m_DiagnosticRecording=!bSecondTest&&PrivateLearning::EngineReplayWanted(&m_tMoveStorage);
#endif
	auto pLocal = m_tInfo.m_pLocal;
	auto pWeapon = m_tInfo.m_pWeapon;
	auto& tTarget = *m_tInfo.m_pTarget;

	int iSimFlags = ProjSimEnum::Redirect | ProjSimEnum::InitCheck | ProjSimEnum::PredictCmdNum | (Vars::Aimbot::General::NoSpread.Value ? ProjSimEnum::CorrectRandomAngles : ProjSimEnum::NoRandomAngles);
    auto* capture=ProjectileDiagnostics::current;
    const int grenadeTest=capture && pWeapon->GetWeaponID()==TF_WEAPON_GRENADELAUNCHER ? ++capture->grenades[m_bPreviewOnly?1:0] : 0;
    const bool logGrenade=grenadeTest>0 && grenadeTest<=(m_bPreviewOnly?2:6);
    int grenadeComparisons=0;
    const int reviewWeapon=pWeapon->GetWeaponID();
    // GetInfo already establishes that this projectile is supported. Do not
    // maintain a second weapon allowlist here: every own-shot direct player
    // intercept needs the same recovery from stale spatial enumeration.
    const bool targetClipSupported=!m_tInfo.m_pProjectile;
    const bool logTarget=capture && !m_bPreviewOnly && targetClipSupported
        && iType==PointTypeEnum::Direct && tTarget.m_pEntity->IsPlayer() && capture->targetReviews++<3;
    int targetClipChecks=0,targetClipHits=0,targetRecoveries=0,targetBlocked=0,targetFilterDenied=0;
    const bool reviewSupported=reviewWeapon==TF_WEAPON_CROSSBOW || reviewWeapon==TF_WEAPON_COMPOUND_BOW
        || reviewWeapon==TF_WEAPON_FLAREGUN || reviewWeapon==TF_WEAPON_FLAREGUN_REVENGE;
    const bool logReview=capture && reviewSupported && !m_bPreviewOnly
        && iType==PointTypeEnum::Direct && tTarget.m_pEntity->IsPlayer() && capture->weaponReviews++<3;
    int reviewComparisons=0;
    int grenadeSteps=0,grenadeCollisionStep=-1;
    const char* grenadeReason="no_valid_intercept";
#ifdef SPLASH_DEBUG5
	if (Vars::Visuals::Trajectory::Override.Value)
	{
		if (Vars::Visuals::Trajectory::ForwardRedirect.Value)
			s_mTraceCount[__FUNCTION__": setup trace"]++;
	}
	else
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
		case TF_WEAPON_PARTICLE_CANNON:
		case TF_WEAPON_RAYGUN:
		case TF_WEAPON_DRG_POMSON:
		case TF_WEAPON_FLAREGUN:
		case TF_WEAPON_FLAREGUN_REVENGE:
		case TF_WEAPON_COMPOUND_BOW:
		case TF_WEAPON_CROSSBOW:
		case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
		case TF_WEAPON_SYRINGEGUN_MEDIC:
			s_mTraceCount[__FUNCTION__": setup trace"]++;
		}
	}
	s_mTraceCount[std::format(__FUNCTION__": setup clip ({}, {})", iType, iFlags)]++;
#endif
	m_tProjInfo = {};
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, vAngles, m_tProjInfo, iSimFlags & ~ProjSimEnum::InitCheck))
	{
        ProjectileDiagnostics::Add(ProjectileDiagnostics::SetupFailed);
        if(logGrenade) ProjectileDiagnostics::Ledge("grenade_setup_failed",std::format("test={} entity={} type={} flags={} expected_ticks={}",grenadeTest,tTarget.m_pEntity->entindex(),iType,iFlags,iSimTime));
		return false;
	}
    // A projectile cannot safely spawn through a wall, independently of splash
    // self-damage protection. Let the caller try another point/target/side.
    CTraceFilterWorldAndPropsOnly muzzleFilter={}; muzzleFilter.pSkip=pLocal;
    CGameTrace muzzleTrace={};
    SDK::TraceHull(pLocal->GetShootPos(),m_tProjInfo.m_vPos,-m_tProjInfo.m_vHull,m_tProjInfo.m_vHull,MASK_SOLID,&muzzleFilter,&muzzleTrace);
    if(muzzleTrace.startsolid || muzzleTrace.allsolid || muzzleTrace.fraction<1.f)
    {
        m_bWorldBlocked=true;
        ProjectileDiagnostics::Add(ProjectileDiagnostics::Obstructed);
        ProjectileDiagnostics::Trace("muzzle_path_blocked",muzzleTrace,tTarget.m_pEntity->entindex(),iType,0,iSimTime,0);
        return false;
    }
    if(!F::ProjSim.Initialize(m_tProjInfo))
    {ProjectileDiagnostics::Add(ProjectileDiagnostics::SetupFailed);return false;}

    if(logGrenade) {
        const auto velocity=F::ProjSim.GetVelocity();
        const auto mins=tTarget.m_pEntity->m_vecMins(),maxs=tTarget.m_pEntity->m_vecMaxs();
        ProjectileDiagnostics::Ledge("grenade_launch",std::format("test={} entity={} item={} type={} flags={} second_test={} expected_ticks={} latency={} nospread={} physics={} angle={},{},{} launch_angle={},{},{} muzzle={},{},{} velocity={},{},{} speed={} gravity={} lifetime={} hull={},{},{} aim_point={},{},{} target_origin={},{},{} target_mins={},{},{} target_maxs={},{},{}",grenadeTest,tTarget.m_pEntity->entindex(),pWeapon->m_iItemDefinitionIndex(),iType,iFlags,bSecondTest,iSimTime,m_tInfo.m_flLatency,Vars::Aimbot::General::NoSpread.Value,F::ProjSim.m_bPhysics,vAngles.x,vAngles.y,vAngles.z,m_tProjInfo.m_vAng.x,m_tProjInfo.m_vAng.y,m_tProjInfo.m_vAng.z,m_tProjInfo.m_vPos.x,m_tProjInfo.m_vPos.y,m_tProjInfo.m_vPos.z,velocity.x,velocity.y,velocity.z,m_tProjInfo.m_flVelocity,m_tProjInfo.m_flGravity,m_tProjInfo.m_flLifetime,m_tProjInfo.m_vHull.x,m_tProjInfo.m_vHull.y,m_tProjInfo.m_vHull.z,vPoint.x,vPoint.y,vPoint.z,tTarget.m_vPos.x,tTarget.m_vPos.y,tTarget.m_vPos.z,mins.x,mins.y,mins.z,maxs.x,maxs.y,maxs.z));
    }

    if (logTarget) {
        const auto velocity=F::ProjSim.GetVelocity();
        ProjectileDiagnostics::Ledge("projectile_target_launch",std::format(
            "weapon={} item={} target={} expected_ticks={} latency={} aim_fov={} general_fov={} speed={} gravity={} physics={} muzzle={},{},{} velocity={},{},{} aim_point={},{},{} predicted_origin={},{},{} live_origin={},{},{} lifetime={} hull={},{},{}",
            reviewWeapon,pWeapon->m_iItemDefinitionIndex(),tTarget.m_pEntity->entindex(),iSimTime,m_tInfo.m_flLatency,
            Vars::Aimbot::Projectile::AimFOV.Value,Vars::Aimbot::General::AimFOV.Value,m_tProjInfo.m_flVelocity,m_tProjInfo.m_flGravity,F::ProjSim.m_bPhysics,
            m_tProjInfo.m_vPos.x,m_tProjInfo.m_vPos.y,m_tProjInfo.m_vPos.z,velocity.x,velocity.y,velocity.z,
            vPoint.x,vPoint.y,vPoint.z,tTarget.m_vPos.x,tTarget.m_vPos.y,tTarget.m_vPos.z,
            tTarget.m_pEntity->GetAbsOrigin().x,tTarget.m_pEntity->GetAbsOrigin().y,tTarget.m_pEntity->GetAbsOrigin().z,
            m_tProjInfo.m_flLifetime,m_tProjInfo.m_vHull.x,m_tProjInfo.m_vHull.y,m_tProjInfo.m_vHull.z));
    }
	CGameTrace trace = {};
	CTraceFilterCollideable filter = {};
	filter.pSkip = iType == PointTypeEnum::Direct ? pLocal : tTarget.m_pEntity;
	filter.iPlayer = iType == PointTypeEnum::Direct ? PLAYER_DEFAULT : PLAYER_NONE;
	filter.bMisc = iType == PointTypeEnum::Direct;
	int nMask = MASK_SOLID;
	if (iType == PointTypeEnum::Direct && SDK::FriendlyFire())
	{
		switch (pWeapon->GetWeaponID())
		{	// only weapons that actually hit teammates properly
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
		case TF_WEAPON_PARTICLE_CANNON:
		case TF_WEAPON_DRG_POMSON:
		case TF_WEAPON_FLAREGUN:
		case TF_WEAPON_SYRINGEGUN_MEDIC:
			filter.iPlayer = PLAYER_ALL;
		}
	}
	F::ProjSim.SetupTrace(filter, nMask, pWeapon);

#ifdef SPLASH_DEBUG4
	Vec3 vHull = m_tProjInfo.m_vHull.Max(1);
	G::BoxStorage.emplace_back(vPoint, -vHull, vHull, Vec3(), I::GlobalVars->curtime + 5.f, Color_t(0, 0, 0), Color_t(0, 0, 0, 0));
#endif

	if (!m_tProjInfo.m_flGravity)
	{
		SDK::TraceHull(m_tProjInfo.m_vPos, vPoint, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace);
#ifdef NIKOGRAM_PRIVATE_LEARNING
        RecordDiagnosticQuery(m_tProjInfo.m_vPos,vPoint,-m_tProjInfo.m_vHull,m_tProjInfo.m_vHull,nMask,filter,trace,0,true);
#endif
#ifdef SPLASH_DEBUG5
		s_mTraceCount[__FUNCTION__": nograv trace"]++;
#endif
#ifdef SPLASH_DEBUG4
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(m_tProjInfo.m_vPos, trace.endpos), I::GlobalVars->curtime + 5.f, Color_t(0, 0, 0));
#endif
		if (Math::FullFraction(m_tProjInfo.m_vPos, vPoint, trace) < 0.999f && trace.m_pEnt != tTarget.m_pEntity)
		{
            if(!trace.m_pEnt || (!trace.m_pEnt->IsPlayer() && !trace.m_pEnt->IsBuilding())) m_bWorldBlocked=true;
            ProjectileDiagnostics::Add(ProjectileDiagnostics::Obstructed);
            ProjectileDiagnostics::Trace("pretrace_rejected",trace,tTarget.m_pEntity->entindex(),iType,0,iSimTime,0);
            ProjectileDiagnostics::BlockedPoint(tTarget.m_pEntity->entindex(), iType,
                m_tProjInfo.m_vPos, vPoint, tTarget.m_vPos, vAngles, trace);
			return false;
		}
	}

#ifdef SPLASH_DEBUG4
	G::BoxStorage.pop_back();
#endif

	bool bDidHit = false;
	Vec3 vNew = F::ProjSim.GetOrigin();
	int iTimingTolerance = TIME_TO_TICKS(m_tInfo.m_flBoundsTime);
	float flRadiusSqr = iType != PointTypeEnum::Direct ? powf(m_tProjInfo.m_flVelocity * TICK_INTERVAL + m_tProjInfo.m_vHull.z, 2) : std::numeric_limits<float>::max();
	uint8_t iTraceInterval = iFlags == PointFlagsEnum::Lob ? Vars::Aimbot::Projectile::LobTraceInterval.Value
		: iType != PointTypeEnum::Direct ? Vars::Aimbot::Projectile::SplashTraceInterval.Value
		: Vars::Aimbot::Projectile::DirectTraceInterval.Value;
    // Ordinary curved shots need the same corner/ledge precision as high lobs.
    iTraceInterval=byte(ArcPolicy::TraceInterval(iTraceInterval,m_tProjInfo.m_flGravity,TICK_INTERVAL));
#ifdef NIKOGRAM_PRIVATE_LEARNING
    const Vec3 diagnosticVelocity=F::ProjSim.GetVelocity();
    int diagnosticPreviousTick=0;
#endif

	const RestoreInfo_t tOriginal = { tTarget.m_pEntity->GetAbsOrigin(), tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs() };
	tTarget.m_pEntity->SetAbsOrigin(tTarget.m_vPos);
	tTarget.m_pEntity->m_vecMins() = { std::clamp(tTarget.m_pEntity->m_vecMins().x, -24.f, 0.f), std::clamp(tTarget.m_pEntity->m_vecMins().y, -24.f, 0.f), tTarget.m_pEntity->m_vecMins().z };
	tTarget.m_pEntity->m_vecMaxs() = { std::clamp(tTarget.m_pEntity->m_vecMaxs().x, 0.f, 24.f), std::clamp(tTarget.m_pEntity->m_vecMaxs().y, 0.f, 24.f), tTarget.m_pEntity->m_vecMaxs().z };
    // Predicted entities may not be enumerated by the world's spatial lookup.
    // Clip explicitly, but only replace a later world hit, never a nearer blocker.
    const bool clipPredictedTarget=GrenadeCollisionPolicy::TargetClip(iType==PointTypeEnum::Direct,tTarget.m_pEntity->IsPlayer(),targetClipSupported);
    auto mergePredictedTarget=[&](const Vec3& from,const Vec3& to,CGameTrace& world,int step,bool retest)
    {
        if (!clipPredictedTarget || world.m_pEnt==tTarget.m_pEntity) return;
        auto targetFilter=filter;
        const bool allowed=targetFilter.ShouldHitEntity(tTarget.m_pEntity,nMask);
        if (!allowed) {++targetFilterDenied;return;}
        Ray_t ray; ray.Init(from,to,-m_tProjInfo.m_vHull,m_tProjInfo.m_vHull);
        CGameTrace targetTrace={};
        I::EngineTrace->ClipRayToEntity(ray,nMask,tTarget.m_pEntity,&targetTrace);
        ++targetClipChecks;
        const bool targetHit=targetTrace.DidHit() && targetTrace.m_pEnt==tTarget.m_pEntity;
        if (targetHit) ++targetClipHits;
        const bool replace=GrenadeCollisionPolicy::PreferTarget(allowed,
            targetHit,targetTrace.fraction,
            world.DidHit(),world.fraction,world.startsolid,world.allsolid);
        if (targetHit && !replace) ++targetBlocked;
        if (logGrenade && targetTrace.DidHit())
            ProjectileDiagnostics::Ledge("grenade_target_merge",std::format(
                "test={} step={} retest={} target={} recovered={} world_entity={} world_fraction={} world_startsolid={} world_allsolid={} target_fraction={} target_startsolid={}",
                grenadeTest,step,retest,tTarget.m_pEntity->entindex(),replace,world.m_pEnt?world.m_pEnt->entindex():-1,
                world.fraction,world.startsolid,world.allsolid,targetTrace.fraction,targetTrace.startsolid));
        if (replace) {
            ++targetRecoveries;
            if(logTarget && targetRecoveries<=2)
                ProjectileDiagnostics::Ledge("projectile_target_recovery",std::format(
                    "weapon={} target={} step={} retest={} world_entity={} world_fraction={} target_fraction={} predicted_origin={},{},{}",
                    reviewWeapon,tTarget.m_pEntity->entindex(),step,retest,world.m_pEnt?world.m_pEnt->entindex():-1,world.fraction,targetTrace.fraction,
                    tTarget.m_vPos.x,tTarget.m_vPos.y,tTarget.m_vPos.z));
            world=targetTrace;
        }
    };
	for (int n = 1; n <= iSimTime; n++)
	{
        grenadeSteps=n;
		F::ProjSim.RunTick(m_tProjInfo);

		if (bDidHit)
		{
			trace.endpos = F::ProjSim.GetOrigin();
			continue;
		}
		if (iTraceInterval != 1 && n % iTraceInterval && n != iSimTime)
			continue;

		Vec3 vOld = vNew; vNew = F::ProjSim.GetOrigin();
#ifdef NIKOGRAM_PRIVATE_LEARNING
        const int diagnosticFrom=diagnosticPreviousTick;diagnosticPreviousTick=n;
        CGameTrace diagnosticVisibility={};bool diagnosticHasVisibility=false;
#endif
		SDK::TraceHull(vOld, vNew, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace);
        if(logReview && reviewComparisons<2 && (trace.DidHit() || n==iSimTime
            || (reviewComparisons==0 && vNew.DistTo(vPoint)<32.f))) {
            ++reviewComparisons;
            Ray_t ray; ray.Init(vOld,vNew,-m_tProjInfo.m_vHull,m_tProjInfo.m_vHull);
            CGameTrace clipped={};
            I::EngineTrace->ClipRayToEntity(ray,nMask,tTarget.m_pEntity,&clipped);
            auto checkFilter=filter;
            const bool allowed=checkFilter.ShouldHitEntity(tTarget.m_pEntity,nMask);
            const bool recoverable=GrenadeCollisionPolicy::PreferTarget(allowed,
                clipped.DidHit() && clipped.m_pEnt==tTarget.m_pEntity,clipped.fraction,
                trace.DidHit(),trace.fraction,trace.startsolid,trace.allsolid);
            ProjectileDiagnostics::Ledge("weapon_collision_compare",std::format(
                "weapon={} item={} target={} step={} expected={} local_team={} target_team={} filter_allowed={} live_entity={} live_fraction={} live_startsolid={} live_allsolid={} clip_entity={} clip_fraction={} clip_startsolid={} clip_allsolid={} recoverable={} speed={} gravity={} hull={},{},{} from={},{},{} to={},{},{} charge_begin={} note=read_only_not_hit_or_headshot_confirmation",
                reviewWeapon,pWeapon->m_iItemDefinitionIndex(),tTarget.m_pEntity->entindex(),n,iSimTime,
                pLocal->m_iTeamNum(),tTarget.m_pEntity->m_iTeamNum(),allowed,
                trace.m_pEnt?trace.m_pEnt->entindex():-1,trace.fraction,trace.startsolid,trace.allsolid,
                clipped.m_pEnt?clipped.m_pEnt->entindex():-1,clipped.fraction,clipped.startsolid,clipped.allsolid,
                recoverable && trace.m_pEnt!=tTarget.m_pEntity,m_tProjInfo.m_flVelocity,m_tProjInfo.m_flGravity,
                m_tProjInfo.m_vHull.x,m_tProjInfo.m_vHull.y,m_tProjInfo.m_vHull.z,
                vOld.x,vOld.y,vOld.z,vNew.x,vNew.y,vNew.z,
                reviewWeapon==TF_WEAPON_COMPOUND_BOW?pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime():0.f));
        }
        // Read-only comparison: never substitute these results for the live trace.
        if (logGrenade && !m_bPreviewOnly && iType == PointTypeEnum::Direct
            && tTarget.m_pEntity->IsPlayer() && grenadeComparisons < 2
            && (trace.DidHit() || n == iSimTime || (grenadeComparisons == 0 && vNew.DistTo(vPoint) < 32.f)))
        {
            ++grenadeComparisons;
            const int teamMask=nMask | CONTENTS_REDTEAM | CONTENTS_BLUETEAM;
            Ray_t ray; ray.Init(vOld,vNew,-m_tProjInfo.m_vHull,m_tProjInfo.m_vHull);
            CGameTrace clipped={},clippedTeams={},worldTeams={};
            I::EngineTrace->ClipRayToEntity(ray,nMask,tTarget.m_pEntity,&clipped);
            I::EngineTrace->ClipRayToEntity(ray,teamMask,tTarget.m_pEntity,&clippedTeams);
            auto teamFilter=filter;
            SDK::TraceHull(vOld,vNew,-m_tProjInfo.m_vHull,m_tProjInfo.m_vHull,teamMask,&teamFilter,&worldTeams);
            auto checkFilter=filter;
            const bool allowed=checkFilter.ShouldHitEntity(tTarget.m_pEntity,nMask);
            const auto origin=tTarget.m_pEntity->GetAbsOrigin();
            const auto mins=tTarget.m_pEntity->m_vecMins(),maxs=tTarget.m_pEntity->m_vecMaxs();
            ProjectileDiagnostics::Ledge("grenade_collision_compare",std::format(
                "test={} step={} target={} local_team={} target_team={} filter_allowed={} mask={} team_mask={} live_entity={} live_fraction={} clip_hit={} clip_entity={} clip_fraction={} clip_solid={} team_clip_hit={} team_clip_entity={} team_clip_fraction={} team_clip_solid={} team_world_entity={} team_world_fraction={} team_world_solid={} origin={},{},{} mins={},{},{} maxs={},{},{} from={},{},{} to={},{},{}",
                grenadeTest,n,tTarget.m_pEntity->entindex(),pLocal->m_iTeamNum(),tTarget.m_pEntity->m_iTeamNum(),allowed,nMask,teamMask,
                trace.m_pEnt?trace.m_pEnt->entindex():-1,trace.fraction,
                clipped.DidHit(),clipped.m_pEnt?clipped.m_pEnt->entindex():-1,clipped.fraction,clipped.startsolid,
                clippedTeams.DidHit(),clippedTeams.m_pEnt?clippedTeams.m_pEnt->entindex():-1,clippedTeams.fraction,clippedTeams.startsolid,
                worldTeams.m_pEnt?worldTeams.m_pEnt->entindex():-1,worldTeams.fraction,worldTeams.startsolid,
                origin.x,origin.y,origin.z,mins.x,mins.y,mins.z,maxs.x,maxs.y,maxs.z,vOld.x,vOld.y,vOld.z,vNew.x,vNew.y,vNew.z));
        }
        mergePredictedTarget(vOld,vNew,trace,n,false);
        const bool worldContact=!trace.m_pEnt || (!trace.m_pEnt->IsPlayer() && !trace.m_pEnt->IsBuilding());
        if(worldContact && ProjectileMuzzlePolicy::RejectImpact(AutoViewmodelSwitch::DelayedBomb(pWeapon),trace.DidHit(),trace.startsolid || trace.allsolid,
            trace.m_pEnt==tTarget.m_pEntity,trace.endpos.DistTo(m_tProjInfo.m_vPos)))
        {
            m_bWorldBlocked=true;
            ProjectileDiagnostics::Add(ProjectileDiagnostics::Obstructed);
            ProjectileDiagnostics::Trace("near_wall_rejected",trace,tTarget.m_pEntity->entindex(),iType,n,iSimTime,0);
            grenadeReason="near_wall_rejected";
            break; // Restore the temporarily moved target below, not an early return.
        }
#ifdef NIKOGRAM_PRIVATE_LEARNING
        RecordDiagnosticQuery(vOld,vNew,-m_tProjInfo.m_vHull,m_tProjInfo.m_vHull,nMask,filter,trace,1,true);
#endif
#ifdef SPLASH_DEBUG5
		s_mTraceCount[std::format(__FUNCTION__": trace ({})", iTraceInterval)]++;
#endif
#ifdef SPLASH_DEBUG4
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vOld, trace.endpos), I::GlobalVars->curtime + 5.f, Color_t(255, 0, 0));
#endif

		bool bHit = false;
		switch (iType)
		{
		case PointTypeEnum::Direct:
		case PointTypeEnum::Geometry:
			bHit = trace.DidHit(); break;
		case PointTypeEnum::Air:
			bHit = trace.endpos.DistToSqr(vPoint) < flRadiusSqr || trace.DidHit(); break;
		}

		if (bHit)
		{
            if(logGrenade && grenadeCollisionStep<0) {
                grenadeCollisionStep=n;
                ProjectileDiagnostics::Ledge("grenade_first_collision",std::format("test={} target={} hit_entity={} type={} step={} expected={} tolerance={} trace_interval={} mask={} fraction={} startsolid={} allsolid={} normal={},{},{} from={},{},{} to={},{},{} impact={},{},{} distance_to_aim={}",grenadeTest,tTarget.m_pEntity->entindex(),trace.m_pEnt?trace.m_pEnt->entindex():-1,iType,n,iSimTime,iTimingTolerance,iTraceInterval,nMask,trace.fraction,trace.startsolid,trace.allsolid,trace.plane.normal.x,trace.plane.normal.y,trace.plane.normal.z,vOld.x,vOld.y,vOld.z,vNew.x,vNew.y,vNew.z,trace.endpos.x,trace.endpos.y,trace.endpos.z,trace.endpos.DistTo(vPoint)));
            }
            ProjectileDiagnostics::Trace("terminal",trace,tTarget.m_pEntity->entindex(),iType,n,iSimTime,iTimingTolerance);
			bool bValid = false, bTarget = true;
			switch (iType)
			{
			case PointTypeEnum::Direct:
				bTarget = trace.m_pEnt == tTarget.m_pEntity, bValid = bTarget && iSimTime - n < iTimingTolerance; break;
			case PointTypeEnum::Geometry:
				bValid = trace.endpos.DistToSqr(vPoint) < flRadiusSqr; break;
			case PointTypeEnum::Air:
				bValid = !trace.DidHit(); break;
			}
            if(!bValid) grenadeReason=iType==PointTypeEnum::Direct ? (!bTarget?"direct_hit_wrong_entity":"direct_hit_timing") : "impact_outside_candidate";
            // A Demo arc hitting a nearby corner can fail the normal intercept
            // without being a rocket-style unsafe splash. Still permit a fully
            // validated opposite-side retry; don't redirect valid sticky placements.
            if(!bValid && worldContact && ProjectileMuzzlePolicy::SwitchImpact(trace.DidHit(),trace.startsolid || trace.allsolid,
                false,trace.endpos.DistTo(m_tProjInfo.m_vPos))) m_bWorldBlocked=true;

			// Proximity to the requested point is only a trajectory tolerance;
			// the actual explosion must also reach the predicted target.
			if (bValid && iType != PointTypeEnum::Direct)
			{
				bValid = SplashInRange(m_tInfo, trace.endpos, n, iType == PointTypeEnum::Air, tOriginal);
				if (!bValid) grenadeReason = "splash_out_of_range";
			}

			if (bValid && iType != PointTypeEnum::Direct)
			{
				CGameTrace trace2 = {};
				SDK::Trace(trace.endpos + trace.plane.normal * m_tInfo.m_flNormalOffset, tTarget.m_vPos + m_tInfo.m_vTargetEye, MASK_SHOT, &filter, &trace2);
#ifdef NIKOGRAM_PRIVATE_LEARNING
                RecordDiagnosticQuery(trace.endpos+trace.plane.normal*m_tInfo.m_flNormalOffset,tTarget.m_vPos+m_tInfo.m_vTargetEye,{},{},MASK_SHOT,filter,trace2,2,false);
#endif
				bValid = trace2.fraction == 1.f;
                if(!bValid) ProjectileDiagnostics::Add(ProjectileDiagnostics::VisibilityFailed);
                if(!bValid) grenadeReason="splash_visibility";
#ifdef NIKOGRAM_PRIVATE_LEARNING
                diagnosticVisibility=trace2;diagnosticHasVisibility=true;
#endif
#ifdef SPLASH_DEBUG5
				s_mTraceCount[__FUNCTION__": splash eye trace"]++;
#endif
#ifdef SPLASH_DEBUG4
				//G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(trace.endpos, trace.endpos + trace.plane.normal * m_tInfo.m_flNormalOffset), I::GlobalVars->curtime + 5.f, Color_t(255, 0, 255));
				G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(trace2.startpos, trace2.endpos), I::GlobalVars->curtime + 5.f, Color_t(255, 0, 255));
				G::BoxStorage.emplace_back(trace.endpos + m_tInfo.m_flNormalOffset * trace.plane.normal, -vHull, vHull, Vec3(), I::GlobalVars->curtime + 5.f, Color_t(255, 0, 255), Color_t(0, 0, 0, 0));
#endif
			}

#ifdef SPLASH_DEBUG4
			if (bValid)
				G::BoxStorage.emplace_back(vPoint, -vHull, vHull, Vec3(), I::GlobalVars->curtime + 5.f, Color_t(0, 255, 0), Color_t(0, 0, 0, 0));
			else
				G::BoxStorage.emplace_back(vPoint, -vHull, vHull, Vec3(), I::GlobalVars->curtime + 5.f, Color_t(255, 0, 0), Color_t(0, 0, 0, 0));
#endif

			if (bValid && Vars::Aimbot::Projectile::IntervalRetest.Value && iTraceInterval != 1)
			{
				CGameTrace trace2 = {}; Vec3 vOld, vNew;
				int iTicks = int(m_tProjInfo.m_vPath.size());
				if (m_tInfo.m_flGravity)
					iTicks -= iTimingTolerance;

				for (int i = 1; i < iTicks; i++)
				{
					vOld = m_tProjInfo.m_vPath[i - 1], vNew = m_tProjInfo.m_vPath[i];
					SDK::TraceHull(vOld, vNew, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace2);
                    mergePredictedTarget(vOld,vNew,trace2,i,true);
#ifdef NIKOGRAM_PRIVATE_LEARNING
                    RecordDiagnosticQuery(vOld,vNew,-m_tProjInfo.m_vHull,m_tProjInfo.m_vHull,nMask,filter,trace2,3,true);
#endif
					bValid = !trace2.DidHit();
#ifdef SPLASH_DEBUG5
					s_mTraceCount[__FUNCTION__": trace (retest)"]++;
#endif
#ifdef SPLASH_DEBUG4
					G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(trace2.startpos, trace2.endpos), I::GlobalVars->curtime + 5.f, Color_t(255, 255, 0));
					if (!bValid)
						G::BoxStorage.emplace_back(trace2.endpos, -vHull, vHull, Vec3(), I::GlobalVars->curtime + 5.f, Color_t(255, 255, 0), Color_t(0, 0, 0, 0));
#endif

					if (!bValid)
						break;
				}
			}

			if (bValid)
			{
				if (iTraceInterval != 1 && iType != PointTypeEnum::Direct)
				{
					int iInterval = n % iTraceInterval ? n % iTraceInterval : iTraceInterval;
					int iPopCount = ceilf(iInterval - trace.fraction * iInterval);
					for (int i = 0; i < iPopCount && !m_tProjInfo.m_vPath.empty(); i++)
						m_tProjInfo.m_vPath.pop_back();
				}

				const bool headGate=Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Smooth
                    || Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Assistive;
                const bool logHead=reviewWeapon==TF_WEAPON_COMPOUND_BOW && capture && capture->headReviews<3;
				if (tTarget.m_nAimedHitbox == HITBOX_HEAD && !bSecondTest && (headGate || logHead))
				{	// loop and see if closest hitbox is head
					auto aBones = F::Backtrack.GetBones(tTarget.m_pEntity);
					auto pSet = tTarget.m_pEntity->As<CTFPlayer>()->GetHitboxSet();
                    if(!aBones || !pSet) {
                        if(logHead) { ++capture->headReviews; ProjectileDiagnostics::Ledge("huntsman_head_check",std::format("target={} available=false gate={} reason=missing_bones_or_hitboxes",tTarget.m_pEntity->entindex(),headGate)); }
                        if(headGate) break;
                    }
                    else {

					Vec3 vOffset = tOriginal.m_vOrigin - tTarget.m_vPos;
					Vec3 vPos = trace.endpos + F::ProjSim.GetVelocity().Normalized() * 16 + vOffset;

					float flLowestDistance = std::numeric_limits<float>::max(); int iClosest = -1;
                    float oldDistance=std::numeric_limits<float>::max(); int oldClosest=-1;
					for (int nHitbox = 0; nHitbox < pSet->numhitboxes; ++nHitbox)
					{
						auto pBox = pSet->pHitbox(nHitbox);
						if (!pBox) continue;

                        Vec3 boneOrigin; Math::VectorTransform({},aBones[pBox->bone],boneOrigin);
                        const float boneDistance=vPos.DistToSqr(boneOrigin);
                        if(boneDistance<oldDistance) {oldDistance=boneDistance;oldClosest=nHitbox;}
                        const Vec3 localCenter={BowChargePolicy::Center(pBox->bbmin.x,pBox->bbmax.x),
                            BowChargePolicy::Center(pBox->bbmin.y,pBox->bbmax.y),BowChargePolicy::Center(pBox->bbmin.z,pBox->bbmax.z)};
						Vec3 vCenter; Math::VectorTransform(localCenter, aBones[pBox->bone], vCenter);

						const float flDistance = vPos.DistToSqr(vCenter);
						if (flDistance < flLowestDistance)
							iClosest = nHitbox, flLowestDistance = flDistance;
					}
                    if(logHead) {
                        ++capture->headReviews;
                        ProjectileDiagnostics::Ledge("huntsman_head_check",std::format("target={} available=true gate={} closest_center={} closest_bone={} head={} center_distance={} bone_distance={} step={} charge_begin={} note=nearest_center_heuristic_not_server_headshot",tTarget.m_pEntity->entindex(),headGate,iClosest,oldClosest,int(HITBOX_HEAD),std::sqrt(flLowestDistance),std::sqrt(oldDistance),n,pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime()));
                    }
					if (headGate && iClosest != HITBOX_HEAD)
						break;
                    }
				}

#ifdef NIKOGRAM_PRIVATE_LEARNING
                // A bounded paired check inside the baseline's existing target
                // simulation boundary. Never changes target, filter, path, or result.
                if(!bSecondTest&&!m_tProjInfo.m_flGravity&&!F::ProjSim.m_bPhysics&&!m_tInfo.m_iArmTime
                    &&iFlags==PointFlagsEnum::Regular&&(pWeapon->GetWeaponID()==TF_WEAPON_ROCKETLAUNCHER||pWeapon->GetWeaponID()==TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT)
                    &&PrivateLearning::EngineCollisionReserve(&m_tMoveStorage))
                {
                    using namespace PrivateLearning::CollisionParity;
                    const auto started=std::chrono::steady_clock::now();Result result;result.path=iType;result.from=diagnosticFrom;result.to=n;
                    const auto vec=[](const Vec3& p)->V3{return {p.x,p.y,p.z};};
                    const auto encode=[&](const CGameTrace& t)->Trace{return {vec(t.endpos),vec(t.plane.normal),t.fraction,t.m_pEnt?t.m_pEnt->entindex():-1,t.startsolid,t.allsolid};};
                    result.baseline=encode(trace);V3 a{},b{};
                    if(!Segment(vec(m_tProjInfo.m_vPos),vec(diagnosticVelocity),TICK_INTERVAL,diagnosticFrom,n,a,b))result.reason="segment_out_of_bounds";
                    else if(Distance(a,vec(vOld))>.01||Distance(b,vec(vNew))>.01)result.reason="segment_kinematics_mismatch";
                    else
                    {
                        CGameTrace replay={};auto replayFilter=filter;
                        SDK::TraceHull(Vec3(a[0],a[1],a[2]),Vec3(b[0],b[1],b[2]),-m_tProjInfo.m_vHull,m_tProjInfo.m_vHull,nMask,&replayFilter,&replay);
                        result.traces=1;result.replay=encode(replay);result.endpointError=Distance(result.baseline.end,result.replay.end);
                        result.reason=Match(result.baseline,result.replay)?"terminal_collision_match_unvalidated":"terminal_collision_mismatch";
                        if(diagnosticHasVisibility)
                        {
                            CGameTrace visibility={};
                            SDK::Trace(replay.endpos+replay.plane.normal*m_tInfo.m_flNormalOffset,tTarget.m_vPos+m_tInfo.m_vTargetEye,MASK_SHOT,&replayFilter,&visibility);
                            ++result.traces;result.visibility=true;result.baselineVisibility=encode(diagnosticVisibility);result.replayVisibility=encode(visibility);
                            if(!Match(result.baselineVisibility,result.replayVisibility))result.reason="splash_visibility_mismatch";
                        }
                    }
                    result.micros=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-started).count();
                    PrivateLearning::EngineCollisionResult(&m_tMoveStorage,result);
                }
#endif
				// Keep this separate from hit feasibility; disabled means no self-damage restriction.
				if (!m_bPreviewOnly && WouldSplashLocal(pLocal, pWeapon, trace.endpos + trace.plane.normal * .5f,
					TICKS_TO_TIME(n) + m_tInfo.m_flLatency))
				{
                    ProjectileDiagnostics::Add(ProjectileDiagnostics::SafetyRejected);
                    grenadeReason="self_damage";
					break;
				}
				bDidHit = true;
			}
			else if (bTarget && iType == PointTypeEnum::Direct && m_tInfo.m_iArmTime)
			{	// run for more ticks to check for splash
				iSimTime = n + iTimingTolerance / 2;
				iType = PointTypeEnum::Geometry;
				filter.pSkip = tTarget.m_pEntity;
				filter.iPlayer = PLAYER_NONE;
				filter.bMisc = false;
				continue;
			}
			else
				break;

			if (iType == PointTypeEnum::Direct)
				trace.endpos = vNew;

			if (!bTarget || iType != PointTypeEnum::Direct)
				break;
		}
	}
	tTarget.m_pEntity->SetAbsOrigin(tOriginal.m_vOrigin);
	tTarget.m_pEntity->m_vecMins() = tOriginal.m_vMins;
	tTarget.m_pEntity->m_vecMaxs() = tOriginal.m_vMaxs;
	m_tProjInfo.m_vPath.push_back(trace.endpos);

    ProjectileDiagnostics::Add(bDidHit ? ProjectileDiagnostics::Hit : ProjectileDiagnostics::Miss);
    if(logTarget) {
        const auto end=F::ProjSim.GetOrigin();
        ProjectileDiagnostics::Ledge("projectile_target_result",std::format(
            "weapon={} target={} hit={} reason={} steps={} expected_ticks={} tolerance={} clip_checks={} clip_hits={} recovered={} blocked={} filter_denied={} final_distance_to_aim={} effective_fov={}",
            reviewWeapon,tTarget.m_pEntity->entindex(),bDidHit,bDidHit?"accepted":grenadeReason,grenadeSteps,iSimTime,iTimingTolerance,
            targetClipChecks,targetClipHits,targetRecoveries,targetBlocked,targetFilterDenied,end.DistTo(vPoint),Vars::Aimbot::Projectile::AimFOV.Value));
    }
    if(logGrenade) {
        const auto end=F::ProjSim.GetOrigin();
        ProjectileDiagnostics::Ledge("grenade_result",std::format("test={} entity={} hit={} reason={} steps={} first_collision_step={} expected_ticks={} final_origin={},{},{} distance_to_aim={}",grenadeTest,tTarget.m_pEntity->entindex(),bDidHit,bDidHit?"accepted":grenadeReason,grenadeSteps,grenadeCollisionStep,iSimTime,end.x,end.y,end.z,end.DistTo(vPoint)));
    }
	return bDidHit;
}

bool CAimbotProjectile::CandidateAngleAllowed(const Vec3& angle,const Vec3& point,const Vec3& origin)
{
    if(m_tInfo.m_pProjectile) return true; // Do not change auto-airblast behavior.
    if(!std::isfinite(angle.x)||!std::isfinite(angle.y)||!std::isfinite(angle.z)) return false;
    if(F::AimbotGlobal.ShouldAimAtAngle(angle)) return true;
    if(SmoothAim::VisibleGuidance())return false; // no adaptive exception to the visible guidance cone
    if(!m_bAdaptivePass || m_tInfo.m_pProjectile) return false;
    const Vec3 relative=point-origin;
    const Vec3 nearest={std::clamp(relative.x,m_vAdaptiveMins.x,m_vAdaptiveMaxs.x),std::clamp(relative.y,m_vAdaptiveMins.y,m_vAdaptiveMaxs.y),std::clamp(relative.z,m_vAdaptiveMins.z,m_vAdaptiveMaxs.z)};
    const float shotFov=Math::CalcFov(I::EngineClient->GetViewAngles(),angle);
    const char* reason=LeadRestrictPolicy::Exception(m_flAdaptiveTargetFov,shotFov,Vars::Aimbot::Projectile::AimFOV.Value,m_flAdaptiveDistance,relative.DistTo(nearest),m_bAdaptiveRetained);
    // At most a few detailed reasons per sampled command, not per simulation tick.
    if(ProjectileDiagnostics::current && ProjectileDiagnostics::current->angles++<4)
        SelfDamageDiagnostics::Write("lead_restrict",std::format("cmd={} reason={} target_fov={} shot_fov={} limit={} distance={}",ProjectileDiagnostics::current->command,reason,m_flAdaptiveTargetFov,shotFov,Vars::Aimbot::Projectile::AimFOV.Value,m_flAdaptiveDistance));
    return std::string_view(reason)=="close_range_exception";
}

bool CAimbotProjectile::HandlePoint(const Vec3& vOrigin, int iSimTime, float flPitch, float flYaw, float flTime, const Vec3& vPoint, uint8_t iType, uint8_t iFlags)
{
	bool bReturn = false;
    auto* arcCapture=ProjectileDiagnostics::current;
    const bool arcLog=iFlags==PointFlagsEnum::Lob && arcCapture && arcCapture->arcCandidates++<6;
    if (arcLog) ProjectileDiagnostics::Ledge("arc_candidate",std::format("target={} weapon={} type={} flight={} target_ticks={} target_time={} latency={} pitch={} yaw={} underpredict=0",m_tInfo.m_pTarget->m_pEntity->entindex(),m_tInfo.m_pWeapon->GetWeaponID(),iType,flTime,iSimTime,TICKS_TO_TIME(iSimTime),m_tInfo.m_flLatency,flPitch,flYaw));

	if(!m_tInfo.m_pProjectile&&SmoothAim::VisibleGuidance()
		&&!CandidateAngleAllowed({flPitch,flYaw,0.f},vPoint,vOrigin))return false;
	Vec3 vAngles; Aim(G::CurrentUserCmd->viewangles, { flPitch, flYaw, 0.f }, vAngles);
    if(auto* capture=ProjectileDiagnostics::current;capture && !m_tInfo.m_pProjectile && m_tInfo.m_pWeapon->GetWeaponID()==TF_WEAPON_GRENADELAUNCHER && capture->points[m_bPreviewOnly?1:0]++<(m_bPreviewOnly?2:6))
        ProjectileDiagnostics::Ledge("grenade_candidate",std::format("entity={} type={} flags={} flight={} sim_ticks={} latency={} calculated_angle={},{} applied_angle={},{},{} point={},{},{} predicted_target={},{},{}",m_tInfo.m_pTarget->m_pEntity->entindex(),iType,iFlags,flTime,iSimTime,m_tInfo.m_flLatency,flPitch,flYaw,vAngles.x,vAngles.y,vAngles.z,vPoint.x,vPoint.y,vPoint.z,vOrigin.x,vOrigin.y,vOrigin.z));
    // Reject this candidate before it can replace the chosen solution or stop
    // the search. Collision and self-damage validation still follow normally.
    if(!CandidateAngleAllowed(vAngles,vPoint,vOrigin))
    {
        ProjectileDiagnostics::Add(ProjectileDiagnostics::AngleRejected);
        ProjectileDiagnostics::AngleRejection(m_tInfo.m_pTarget->m_pEntity->entindex(),
            iType, vAngles, vPoint, vOrigin, "before_collision");
        return false;
    }
	m_tInfo.m_pTarget->m_vPos = vOrigin;

	int iOriginalSimTime = iSimTime;
	if (m_tInfo.m_iArmTime && iType == PointTypeEnum::Geometry && iFlags!=PointFlagsEnum::Lob)
	{
		if (flTime > m_tProjInfo.m_flLifetime)
			return false;

		iSimTime = std::ceil(flTime / I::GlobalVars->interval_per_tick); //TIME_TO_TICKS(flTime);
	}
    if (iFlags==PointFlagsEnum::Lob && !ArcPolicy::FlightWithin(flTime,m_tProjInfo.m_flLifetime,TICK_INTERVAL,iSimTime))
    {if(arcLog) ProjectileDiagnostics::Ledge("arc_rejected",std::format("reason=lifetime flight={} lifetime={} ticks={}",flTime,m_tProjInfo.m_flLifetime,iSimTime));return false;}

	if (!m_tInfo.m_pProjectile
		? TestAngle(vPoint, vAngles, iSimTime, iType, iFlags)
		: TestAngle(m_tInfo.m_pProjectile, vPoint, vAngles, iSimTime, iType, iFlags))
	{
		bReturn = m_iResult = true;
		m_flTimeTo = flTime + m_tInfo.m_flLatency;
	}
	else if (!m_iResult && !m_tInfo.m_pProjectile)
	{
		switch (Vars::Aimbot::General::AimType.Value)
		{
		case Vars::Aimbot::General::AimTypeEnum::Smooth:
			if (!SmoothAim::Combined() && Vars::Aimbot::General::SmoothFormula.Value == Vars::Aimbot::General::SmoothFormulaEnum::Default && Vars::Aimbot::General::AssistStrength.Value == 100.f)
				break;
			[[fallthrough]];
		case Vars::Aimbot::General::AimTypeEnum::Assistive:
		{
			Vec3 vPlainAngles = { flPitch, flYaw, 0.f };
			if (TestAngle(vPoint, vPlainAngles, iSimTime, iType, iFlags, true))
				bReturn = m_iResult = 2;
		}
		}
	}

    if (arcLog) ProjectileDiagnostics::Ledge("arc_result",std::format("target={} result={} flight={} trace_interval={} lifetime={}",m_tInfo.m_pTarget->m_pEntity->entindex(),bReturn?m_iResult:0,flTime,ArcPolicy::TraceInterval(Vars::Aimbot::Projectile::LobTraceInterval.Value,m_tProjInfo.m_flGravity,TICK_INTERVAL),m_tProjInfo.m_flLifetime));
	if (bReturn && m_bUpdate)
	{
        if(!m_bPreviewOnly) m_iObservationEntity=-1; // Splash or a later candidate must not reuse a direct sample.
		m_vAngleTo = vAngles, m_vTarget = vPoint, m_vPredicted = vOrigin;
        const auto& tTarget=*m_tInfo.m_pTarget;
        if (ProjectileDiagnostics::current && m_tInfo.m_pWeapon->GetWeaponID()==TF_WEAPON_GRENADELAUNCHER && tTarget.m_pEntity->IsPlayer())
        {
            const auto actual=m_tMoveStorage.m_vDiagnosticStartOrigin;
            const auto velocity=m_tMoveStorage.m_vDiagnosticStartVelocity;
            const auto direct=Math::CalcAngle(m_tInfo.m_vLocalEye,actual+m_tInfo.m_vTargetEye);
            if(ProjectileDiagnostics::current && m_tMoveStorage.m_CounterStrafe.valid) {
                const auto& c=m_tMoveStorage.m_CounterStrafe;
                const float start=actual.x*c.ax+actual.y*c.ay;
                const float projected=vOrigin.x*c.ax+vOrigin.y*c.ay;
                const float lateral=velocity.x*c.ax+velocity.y*c.ay;
                const float elapsed=m_tMoveStorage.m_flCounterTime;
                const float driftOffset=CounterStrafe::DriftOffset(c,elapsed);
                ProjectileDiagnostics::Ledge("counter_selected_decomposition",std::format("entity={} flight={} counter_elapsed={} start_lateral={} initial_velocity={} selected_offset={} center_offset={} drift_offset={} projected_center_offset={} residual_to_projected_center={} opposite_current_velocity={} confidence={} note=geometric_breakdown_not_causal_attribution",tTarget.m_pEntity->entindex(),flTime,elapsed,start,lateral,projected-start,c.center-start,driftOffset,c.center+driftOffset-start,projected-(c.center+driftOffset),std::fabs(lateral)>5.f && (projected-start)*lateral<0.f,c.confidence));
            }
            ProjectileDiagnostics::Ledge("grenade_selected_prediction",std::format(
                "entity={} result={} flight={} ticks={} target_grounded={} counter={} yaw_per_tick={} actual={},{},{} predicted={},{},{} displacement={} velocity={},{},{} direct_angle={},{} selected_angle={},{} yaw_delta={}",
                tTarget.m_pEntity->entindex(),m_iResult,flTime,iSimTime,m_tMoveStorage.m_bDiagnosticStartGrounded,
                m_tMoveStorage.m_CounterStrafe.valid,m_tMoveStorage.m_flAverageYaw,actual.x,actual.y,actual.z,
                vOrigin.x,vOrigin.y,vOrigin.z,actual.DistTo(vOrigin),velocity.x,velocity.y,velocity.z,direct.x,direct.y,
                vAngles.x,vAngles.y,std::remainder(vAngles.y-direct.y,360.f)));
        }
		m_vPlayerPath.clear(), m_vProjectilePath = m_tProjInfo.m_vPath;
		if (m_tMoveStorage.m_vPath.empty())
			return true;

		if (m_tInfo.m_iArmTime && iType != PointTypeEnum::Air)
		{
			int iCount = std::min(iOriginalSimTime + TIME_TO_TICKS(m_tInfo.m_flLatency) + 1, int(m_tMoveStorage.m_vPath.size()));
			m_vPlayerPath.insert(m_vPlayerPath.end(), m_tMoveStorage.m_vPath.begin(), m_tMoveStorage.m_vPath.begin() + iCount);
		}
		else
		{
			size_t iCount = std::min(m_tProjInfo.m_vPath.size() + TIME_TO_TICKS(m_tInfo.m_flLatency), m_tMoveStorage.m_vPath.size());
			m_vPlayerPath.insert(m_vPlayerPath.end(), m_tMoveStorage.m_vPath.begin(), m_tMoveStorage.m_vPath.begin() + iCount);
			m_vPredicted = m_vPlayerPath.back();
		}
	}

	return bReturn && m_iResult == 1;
}

#ifdef NIKOGRAM_PRIVATE_LEARNING
void CAimbotProjectile::RecordDiagnosticQuery(const Vec3& start,const Vec3& end,const Vec3& mins,const Vec3& maxs,int mask,const CTraceFilterCollideable& filter,const CGameTrace& result,int kind,bool hull)
{
    if(!m_DiagnosticRecording)return;
    ++m_DiagnosticScratchCount;
    if(m_DiagnosticScratch.size()>=PrivateLearning::FullPathParity::MaxQueries)return;
    auto entity=m_tInfo.m_pTarget->m_pEntity;
    m_DiagnosticScratch.push_back({start,end,mins,maxs,entity->GetAbsOrigin(),entity->m_vecMins(),entity->m_vecMaxs(),filter,result,mask,kind,hull});
}
void CAimbotProjectile::ReplayFinalPath()
{
    if(!PrivateLearning::EngineReplayWanted(&m_tMoveStorage))return;
    using namespace PrivateLearning;FullPathParity::Result result;result.recorded=m_DiagnosticSelectedCount;result.path=m_DiagnosticSelectedType;
    const auto started=std::chrono::steady_clock::now();
    if(!m_DiagnosticSelectedSupported)result.reason="unsupported_or_missing_final_path";
    else if(result.recorded>FullPathParity::MaxQueries)result.reason="query_budget_exceeded";
    else
    {
        auto entity=m_tInfo.m_pTarget->m_pEntity;
        const Vec3 origin=entity->GetAbsOrigin(),mins=entity->m_vecMins(),maxs=entity->m_vecMaxs();
        {
            struct Restore {CBaseEntity* entity;Vec3 origin,mins,maxs;~Restore(){entity->SetAbsOrigin(origin);entity->m_vecMins()=mins;entity->m_vecMaxs()=maxs;}} restore{entity,origin,mins,maxs};
            const auto encode=[](const CGameTrace& t)->CollisionParity::Trace{return {{t.endpos.x,t.endpos.y,t.endpos.z},{t.plane.normal.x,t.plane.normal.y,t.plane.normal.z},t.fraction,t.m_pEnt?t.m_pEnt->entindex():-1,t.startsolid,t.allsolid};};
            for(const auto& q:m_DiagnosticSelected)
            {
                entity->SetAbsOrigin(q.targetOrigin);entity->m_vecMins()=q.targetMins;entity->m_vecMaxs()=q.targetMaxs;
                auto filter=q.filter;CGameTrace replay={};
                if(q.hull)SDK::TraceHull(q.start,q.end,q.mins,q.maxs,q.mask,&filter,&replay);
                else SDK::Trace(q.start,q.end,q.mask,&filter,&replay);
                result.Compare(q.kind,encode(q.result),encode(replay));
            }
        }
        result.restored=entity->GetAbsOrigin()==origin&&entity->m_vecMins()==mins&&entity->m_vecMaxs()==maxs;
        result.Finish();
    }
    result.micros=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-started).count();
    EngineFullPathResult(&m_tMoveStorage,result);
}
void CAimbotProjectile::SnapshotSelectedPath(const History_t& history,float flight,uint8_t type,uint8_t flags,const Vec3& targetPoint)
{
    if(!PrivateLearning::EngineReplayWanted(&m_tMoveStorage))return;
    PrivateLearning::ProjectileReplay::Input input;
    input.weapon=m_tInfo.m_pWeapon->GetWeaponID();input.pathType=type;
    input.targetPoint={targetPoint.x,targetPoint.y,targetPoint.z};input.grounded=history.m_bDiagnosticGround;
    const auto diagnosticVec=[](const Vec3& v)->PrivateLearning::ProjectileReplay::V3{return {v.x,v.y,v.z};};
    input.targetOrigin=diagnosticVec(history.m_vOrigin);input.targetMins=diagnosticVec(m_tInfo.m_pTarget->m_pEntity->m_vecMins());input.targetMaxs=diagnosticVec(m_tInfo.m_pTarget->m_pEntity->m_vecMaxs());
    input.splashRadius=m_tInfo.m_flRadius;
    if(!m_tProjInfo.m_vPath.empty())input.impact=diagnosticVec(m_tProjInfo.m_vPath.back());
    input.supported=!m_tInfo.m_pProjectile&&flags==PointFlagsEnum::Regular&&!m_tProjInfo.m_flGravity
        &&!m_tInfo.m_iArmTime&&!F::ProjSim.m_bPhysics&&m_iResult==1
        &&(input.weapon==TF_WEAPON_ROCKETLAUNCHER||input.weapon==TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT);
    m_DiagnosticSelectedSupported=input.supported;m_DiagnosticSelectedType=type;
    m_DiagnosticSelected=m_DiagnosticScratch;m_DiagnosticSelectedCount=m_DiagnosticScratchCount;
    input.tick=TICK_INTERVAL;input.selectedSim=history.m_flDiagnosticSim;input.networkSim=history.m_flDiagnosticNetworkSim;
    input.flight=flight;input.latency=m_tInfo.m_flLatency;
    const auto velocity=F::ProjSim.GetVelocity();
    input.start={m_tProjInfo.m_vPos.x,m_tProjInfo.m_vPos.y,m_tProjInfo.m_vPos.z};
    input.velocity={velocity.x,velocity.y,velocity.z};
    // RunTick stores pre-step positions. The last appended point is a trace
    // endpoint (possibly clipped); exclude it from kinematic parity.
    input.count=m_tProjInfo.m_vPath.empty()?0:m_tProjInfo.m_vPath.size()-1;
    if(input.supported&&input.count<=PrivateLearning::ProjectileReplay::MaxPoints)
        for(size_t i=0;i<input.count;++i){const auto& p=m_tProjInfo.m_vPath[i];input.observed[i]={p.x,p.y,p.z};}
    PrivateLearning::EngineSelectedPath(&m_tMoveStorage,input);
}
#endif
bool CAimbotProjectile::HandleDirect(DirectHistory_t& mDirectHistory, size_t* diagnosticTests)
{
    ProjectileDiagnostics::Profile profile(ProjectileDiagnostics::DirectSearch);
	bool bReturn = false;
	if (mDirectHistory.empty())
		return bReturn;

	auto it = mDirectHistory.begin();
    if (auto regular=mDirectHistory.find(PointFlagsEnum::Regular);regular!=mDirectHistory.end()) it=regular;
	uint8_t iType = it->first;
	auto& vDirectHistory = it->second;

	if (F::ProjSim.m_bPhysics)
	{
		for (auto& tHistory : vDirectHistory)
		{
			if (I::EngineTrace->GetPointContents(tHistory.m_vPoint) & CONTENTS_WATER)
				tHistory.m_iPriority = COORD_EXTENT - tHistory.m_vPoint.z;
		}
	}
	std::sort(vDirectHistory.begin(), vDirectHistory.end(), [&](const Direct_t& a, const Direct_t& b) -> bool
	{
		return a.m_iPriority!=b.m_iPriority ? a.m_iPriority<b.m_iPriority : a.m_flTime<b.m_flTime;
	});
	m_flTimeTo = vDirectHistory.front().m_flTime + m_tInfo.m_flLatency;

	for (auto& tHistory : vDirectHistory)
	{
		if (!SearchAllowed()) break;
		if (diagnosticTests) ++*diagnosticTests;
		if (HandlePoint(tHistory.m_vOrigin, tHistory.m_iSimtime, tHistory.m_flPitch, tHistory.m_flYaw, tHistory.m_flTime, tHistory.m_vPoint, PointTypeEnum::Direct, iType))
		{
            if(!m_bPreviewOnly && m_iResult==1 && !m_tMoveStorage.m_bFailed) {
                m_flObservationTime=tHistory.m_flObservationTime;
                m_flObservationFlight=tHistory.m_flTime;
                m_bObservationGround=tHistory.m_bObservationGround;
                m_bObservationStartGround=m_tMoveStorage.m_bDiagnosticStartGrounded;
                m_iObservationEntity=m_tInfo.m_pTarget->m_pEntity->entindex();
                m_nObservationHandle=m_tInfo.m_pTarget->m_pEntity->GetRefEHandle().ToInt();
            }
#ifdef NIKOGRAM_PRIVATE_LEARNING
            SnapshotSelectedPath(tHistory,tHistory.m_flTime,PointTypeEnum::Direct,iType,tHistory.m_vPoint);
            // Read-only preflight. Do NOT call CalculateAngle/TestAngle again:
            // they reinitialize shared projectile/physics state.
            const auto weapon=m_tInfo.m_pWeapon->GetWeaponID();
            const bool supported=m_iResult==1&&!m_tInfo.m_pProjectile&&iType==PointFlagsEnum::Regular
                && !m_tInfo.m_flGravity&&!m_tInfo.m_iArmTime&&!F::ProjSim.m_bPhysics
                && (weapon==TF_WEAPON_ROCKETLAUNCHER||weapon==TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT)
                && m_tInfo.m_pTarget->m_iTargetType==TargetEnum::Player;
            float dx=0,dy=0;
            if(PrivateLearning::EngineIntercept(&m_tMoveStorage,tHistory.m_flDiagnosticSim,tHistory.m_flDiagnosticNetworkSim,tHistory.m_bDiagnosticGround,supported,tHistory.m_flTime,m_tInfo.m_flLatency,dx,dy))
            {
                const auto start=std::chrono::steady_clock::now();
                const Vec3 corrected=tHistory.m_vOrigin+Vec3(dx,dy,0);
                auto entity=m_tInfo.m_pTarget->m_pEntity;
                CTraceFilterWorldAndPropsOnly filter={};filter.pSkip=entity;
                CGameTrace sweep={},ground={};const char* reason="geometry_clear_unvalidated";
                SDK::TraceHull(tHistory.m_vOrigin,corrected,entity->m_vecMins(),entity->m_vecMaxs(),MASK_PLAYERSOLID,&filter,&sweep);
                if(sweep.startsolid||sweep.allsolid||sweep.fraction<1.f)reason="target_hull_blocked";
                else
                {
                    SDK::TraceHull(corrected+Vec3(0,0,2),corrected-Vec3(0,0,4),entity->m_vecMins(),entity->m_vecMaxs(),MASK_PLAYERSOLID,&filter,&ground);
                    if(ground.startsolid||ground.allsolid||ground.fraction>=1.f||ground.plane.normal.z<.7f)reason="ground_support_failed";
                }
                PrivateLearning::EngineGeometry(&m_tMoveStorage,reason,std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());
            }
#endif
			bReturn = true;
			break;
		}
	}

	mDirectHistory.erase(it);
	return bReturn;
}

bool CAimbotProjectile::HandleSplash(SplashHistory_t& mSplashHistory)
{
    ProjectileDiagnostics::Profile profile(ProjectileDiagnostics::SplashSearch);
	if (mSplashHistory.empty())
		return false;

	auto it = mSplashHistory.begin();
    if (auto regular=mSplashHistory.find(PointFlagsEnum::Regular);regular!=mSplashHistory.end()) it=regular;
	uint8_t iType = it->first;
	auto& vSplashHistory = it->second;

	std::sort(vSplashHistory.begin(), vSplashHistory.end(), [&](const Splash_t& a, const Splash_t& b) -> bool
	{
		return a.m_flTimeTo < b.m_flTimeTo;
	});

    const int selected=SplashSearchPolicy::NormalizeMode(Vars::Aimbot::Projectile::SplashMode.Value);
    const bool dynamic=selected==SplashSearchPolicy::Dynamic && !m_tInfo.m_pProjectile;
    // Rockets already have a whole-command budget. Other fired projectiles get
    // one shared splash-only budget; never reset it between modes or targets.
    std::optional<ProjectilePerformancePolicy::SearchScope> splashScope;
    if(dynamic && !ProjectilePerformancePolicy::currentSearchBudget)
        splashScope.emplace(m_DynamicSplashBudget,G::CurrentUserCmd->command_number,true);
    const auto search=[&](int mode)
    {
        bool bReturn=false;
	uint8_t iFlags = CalculateFlagsEnum::None;
	if (iType == PointFlagsEnum::Lob)
		iFlags |= CalculateFlagsEnum::LobAngle;
	SetupSplashPoints(vSplashHistory.front().m_vOrigin, m_vSplashPoints, iFlags, mode);
	if (!m_vSplashPoints.empty())
	{
		iFlags |= CalculateFlagsEnum::Accuracy;
		float flLowestDistance = std::numeric_limits<float>::max(); bool bFirst = true;
        std::vector<Point_t> vSplashPoints;
		// Only the already-bounded ordinary rocket path gets replacement batches.
		// This backlog belongs to one predicted history in this command, never a
		// later command or a different target/muzzle state.
		const bool refill = ProjectilePerformancePolicy::currentSearchBudget
			&& !m_tInfo.m_pProjectile && iType == PointFlagsEnum::Regular;
		for (auto& tHistory : vSplashHistory)
		{
            if (!SearchAllowed()) break;
            int batchSize = 1;
            GetSplashPoints(tHistory.m_vOrigin, m_vSplashPoints, vSplashPoints, tHistory.m_iSimtime, iFlags, bFirst, refill ? &batchSize : nullptr); bFirst = false;
			size_t batchEnd = refill ? std::min(size_t(batchSize), vSplashPoints.size()) : vSplashPoints.size();

			for (size_t pointIndex = 0; pointIndex < vSplashPoints.size(); ++pointIndex)
			{
				if (!SearchAllowed()) break;
				if (pointIndex == batchEnd)
				{
					// A validated result needs no speculative replacement. Keep the
					// old first-batch decisions and spend only spare budget on misses.
					if (!refill || bReturn || !SearchAllowed(SearchWork::Selection)) break;
					batchEnd = SplashSearchPolicy::NextNearestBatch(vSplashPoints, tHistory.m_vOrigin, pointIndex, batchSize);
					if (ProjectileDiagnostics::current) ProjectileDiagnostics::splash.shortlisted += int(batchEnd - pointIndex);
				}
				auto& tPoint = vSplashPoints[pointIndex];
				float flDistance = tHistory.m_vOrigin.DistToSqr(tPoint.m_vPoint);
				if (flDistance > flLowestDistance)
					continue;

                ProjectileDiagnostics::Profile validation(ProjectileDiagnostics::SplashValidation);
                if (ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.validations;
				if (HandlePoint(tHistory.m_vOrigin, tHistory.m_iSimtime, tPoint.m_tSolution.m_flPitch, tPoint.m_tSolution.m_flYaw, tPoint.m_tSolution.m_flTime, tPoint.m_vPoint, tPoint.m_iType, iType))
				{
					bReturn = !dynamic || m_iResult==1;
					if(!dynamic || m_iResult==1) flLowestDistance = flDistance;
#ifdef NIKOGRAM_PRIVATE_LEARNING
                    SnapshotSelectedPath(tHistory,tPoint.m_tSolution.m_flTime,tPoint.m_iType,iType,tPoint.m_vPoint);
#endif
                    if(dynamic && m_iResult==1) return true;
				}
			}
			if (m_tInfo.m_bIgnoreTiming && iType == PointFlagsEnum::Lob)
				break;
		}
	}
        return bReturn;
    };
    bool result=false;
    if(dynamic)
    {
        result=SplashSearchPolicy::TryDynamic([&]
        {
            ProjectilePerformancePolicy::TracePhaseScope tracePhase;
            if(ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.dynamicTrace;
            return search(SplashSearchPolicy::Trace);
        },[&]
        {
            if(ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.dynamicFace;
            return search(SplashSearchPolicy::Face);
        },[]{return SearchAllowed(SearchWork::Sampling) && SearchAllowed();});
    }
    else result=search(selected==SplashSearchPolicy::Dynamic ? SplashSearchPolicy::Trace : selected);
    mSplashHistory.erase(it);
    return result;
}

int CAimbotProjectile::CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool bUpdate)
{
    SmoothAim::CandidateScope smoothing(tTarget.m_pEntity,true);
    if (!SearchAllowed(SearchWork::Prediction)) return 0;
    if(!m_bPreviewOnly) m_bSearchedThisCommand = true;
    const auto original=tTarget;
    const auto position=tTarget.m_pEntity->GetAbsOrigin();
    m_vAdaptiveMins=tTarget.m_pEntity->m_vecMins();
    m_vAdaptiveMaxs=tTarget.m_pEntity->m_vecMaxs();
    m_flAdaptiveDistance=position.DistTo(pLocal->GetAbsOrigin());
    m_flAdaptiveTargetFov=Math::CalcFov(I::EngineClient->GetViewAngles(),Math::CalcAngle(pLocal->GetShootPos(),position+(m_vAdaptiveMins+m_vAdaptiveMaxs)*.5f));
    const int tick=I::GlobalVars->tickcount;
    m_bAdaptiveRetained=m_iAdaptiveEntity==tTarget.m_pEntity->entindex() && tick>=m_iAdaptiveTick && tick-m_iAdaptiveTick<=TIME_TO_TICKS(.25f);
    m_bAdaptivePass=false;
    int result=CanHitPass(tTarget,pLocal,pWeapon,bUpdate);
    if(result || !SearchAllowed(SearchWork::Prediction) || Vars::Aimbot::General::LeadAndRestrict.Value!=Vars::Aimbot::General::LeadAndRestrictEnum::Adaptive || tTarget.m_iTargetType!=TargetEnum::Player)
    {
        if(result) ProjectileDiagnostics::Stage(Vars::Aimbot::General::LeadAndRestrict.Value?"strict_solution":"unrestricted_solution",result);
        return result;
    }
    // No strict solution: retry only for a nearby target already inside the
    // user's cone. Every candidate must still pass the bounded angle policy.
    if(!(m_flAdaptiveTargetFov<Vars::Aimbot::Projectile::AimFOV.Value) || m_flAdaptiveDistance>(m_bAdaptiveRetained?128.f:112.f)) return result;
    tTarget=original;
    m_bAdaptivePass=true;
    ProjectileDiagnostics::Stage("adaptive_search");
    result=CanHitPass(tTarget,pLocal,pWeapon,bUpdate);
    m_bAdaptivePass=false;
    if(result==1 && bUpdate && !m_bPreviewOnly)
    {
        m_iAdaptiveEntity=tTarget.m_pEntity->entindex(); m_iAdaptiveTick=tick;
        ProjectileDiagnostics::Stage("adaptive_solution");
    }
    return result;
}

int CAimbotProjectile::CanHitPass(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool bUpdate)
{
    ProjectileDiagnostics::Candidate diagnostic(tTarget.m_pEntity,pLocal,pWeapon);
    diagnostic.preview=m_bPreviewOnly; diagnostic.adaptive=m_bAdaptivePass;
    if(ProjectileDiagnostics::current) {ProjectileDiagnostics::current->preview=m_bPreviewOnly;ProjectileDiagnostics::current->adaptive=m_bAdaptivePass;}
#ifdef NIKOGRAM_PRIVATE_LEARNING
    m_DiagnosticSelected.clear();m_DiagnosticSelectedCount=0;m_DiagnosticSelectedSupported=false;
#endif
    PrivateLearning::EngineScope engineDiagnostic(&m_tMoveStorage,tTarget.m_pEntity?tTarget.m_pEntity->entindex():0);
	//if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Unsimulated && H::Entities.GetChoke(tTarget.m_pEntity->entindex()) > Vars::Aimbot::General::TickTolerance.Value)
	//	return false;

	m_tMoveStorage = {};
	if (!F::MoveSim.Initialize(tTarget.m_pEntity, m_tMoveStorage) && tTarget.m_iTargetType == TargetEnum::Player)
	{
        diagnostic.reason="movement_setup_failed";
		F::MoveSim.Restore(m_tMoveStorage);
		return false;
	}

	m_tProjInfo = {};
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, {}, m_tProjInfo, ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum)
		|| !F::ProjSim.Initialize(m_tProjInfo, false))
	{
        diagnostic.reason="projectile_setup_failed";
		F::MoveSim.Restore(m_tMoveStorage);
		return false;
	}

	m_tInfo = { pLocal, pWeapon, &tTarget };
	m_tInfo.m_vLocalEye = pLocal->GetShootPos();
	m_tInfo.m_vTargetEye = tTarget.m_pEntity->As<CTFPlayer>()->GetViewOffset();
	m_tInfo.m_flLatency = F::Backtrack.GetReal() + TICKS_TO_TIME(F::Backtrack.GetAnticipatedChoke());
	tTarget.m_vPos = tTarget.m_pEntity->m_vecOrigin();

	Vec3 vVelocity = F::ProjSim.GetVelocity();
	m_tInfo.m_flVelocity = vVelocity.Length();
	if (!m_tInfo.m_flVelocity)
	{
        diagnostic.reason="zero_projectile_speed";
		F::MoveSim.Restore(m_tMoveStorage);
		return false;
	}
	m_tInfo.m_vAngFix = Math::VectorAngles(vVelocity);

	m_tInfo.m_vHull = m_tProjInfo.m_vHull.Min(3);
	m_tInfo.m_vOffset = m_tProjInfo.m_vPos - m_tInfo.m_vLocalEye; m_tInfo.m_vOffset.y *= -1;
	m_tInfo.m_flOffsetTime = m_tInfo.m_vOffset.Length() / m_tInfo.m_flVelocity;

	m_tInfo.m_flGravity = m_tProjInfo.m_flGravity;
	m_tInfo.m_iSplashRestrict = !m_tInfo.m_flGravity ? Vars::Aimbot::Projectile::SplashRestrictDirect.Value : Vars::Aimbot::Projectile::SplashRestrictArc.Value;
	m_tInfo.m_flRadius = GetSplashRadius(pWeapon, pLocal, Vars::Aimbot::Projectile::SplashRadius.Value / 100);
	m_tInfo.m_flBoundsTime = tTarget.m_pEntity->GetSize().Length() / m_tInfo.m_flVelocity;
	m_tInfo.m_flRadiusTime = m_tInfo.m_flBoundsTime + m_tInfo.m_flRadius / m_tInfo.m_flVelocity;
	m_tInfo.m_iArmTime = TIME_TO_TICKS(ArmTime(pWeapon));
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
		m_tInfo.m_flNormalOffset = 1.f;
	}
	m_tInfo.m_bIgnoreTiming = false; // Lob target history must correspond to actual arrival time.



	Directs_t mDirects = GetDirects();
	Splashes_t vSplashes = GetSplashes();

	DirectHistory_t mDirectHistory = {};
	SplashHistory_t mSplashHistory = {};

	int iMaxTime = TIME_TO_TICKS(std::min(m_tProjInfo.m_flLifetime, Vars::Aimbot::Projectile::MaxSimulationTime.Value));
	for (int i = 1 - TIME_TO_TICKS(m_tInfo.m_flLatency); i <= iMaxTime; i++)
	{
        // Stop only between completed prediction ticks, then validate the
        // histories already collected and use the normal restoration path.
        if (!SearchStep(SearchWork::Prediction)) break;
		if (!m_tMoveStorage.m_bFailed)
		{
			F::MoveSim.RunTick(m_tMoveStorage);
			tTarget.m_vPos = m_tMoveStorage.m_vPredictedOrigin;
		}
		// A zero-tick solution consumed the point without ever running a collision trace.
		if (!TargetPolicy::HasFlightTick(i))
			continue;

		for (auto it = mDirects.begin(); it != mDirects.end();)
		{
			if (m_tInfo.m_iArmTime && m_tInfo.m_iArmTime > i && !m_tMoveStorage.m_MoveData.m_vecVelocity.IsZero())
				break;

			auto& [iIndex, tOffset] = *it;

			Vec3& vOffset = tOffset.m_vOffset;
			uint8_t iType = tOffset.m_iFlags & -tOffset.m_iFlags;

			Vec3 vPoint = tTarget.m_vPos + vOffset;
			if (Vars::Aimbot::Projectile::HuntsmanPullPoint.Value && tTarget.m_nAimedHitbox == HITBOX_HEAD)
			{
				vPoint = PullPoint(vPoint, m_tInfo.m_vLocalEye, m_tInfo, tTarget.m_vPos, tTarget.m_pEntity->m_vecMins() + m_tInfo.m_vHull, tTarget.m_pEntity->m_vecMaxs() - m_tInfo.m_vHull);
				if (Vars::Aimbot::Projectile::HuntsmanPullNoZ.Value)
					vPoint.z = tTarget.m_vPos.z + vOffset.z;
			}

			uint8_t iFlags = CalculateFlagsEnum::Accuracy;
			if (iType == PointFlagsEnum::Lob)
				iFlags |= CalculateFlagsEnum::LobAngle;
			int iTolerance = m_tInfo.m_bIgnoreTiming && iType == PointFlagsEnum::Lob ? std::numeric_limits<int>::max() : -1;

			Solution_t tSolution;
			switch (iType)
			{
			case PointFlagsEnum::Lob:
				if (ShouldLob(m_tMoveStorage, m_tInfo))
					goto end;
				tSolution.m_iCalculated = CalculateResultEnum::Bad; break;
			default: end:
				CalculateAngle(m_tInfo.m_vLocalEye, vPoint, i, tSolution, iFlags, iTolerance);
			}
			switch (tSolution.m_iCalculated)
			{
			case CalculateResultEnum::Good:
				mDirectHistory[iType].emplace_back(History_t(tTarget.m_vPos, i), tSolution.m_flPitch, tSolution.m_flYaw, tSolution.m_flTime, vPoint, iIndex);
                mDirectHistory[iType].back().m_flObservationTime=m_tMoveStorage.m_flDiagnosticNetworkOriginTime;
                mDirectHistory[iType].back().m_bObservationGround=m_tMoveStorage.m_pPlayer && m_tMoveStorage.m_pPlayer->IsOnGround();
#ifdef NIKOGRAM_PRIVATE_LEARNING
                {auto& history=mDirectHistory[iType].back();history.m_flDiagnosticSim=m_tMoveStorage.m_flSimTime;history.m_flDiagnosticNetworkSim=m_tMoveStorage.m_flDiagnosticNetworkOriginTime;
                history.m_bDiagnosticGround=!m_tMoveStorage.m_bFailed&&m_tMoveStorage.m_pPlayer&&m_tMoveStorage.m_pPlayer->IsOnGround()&&!m_tMoveStorage.m_pPlayer->IsSwimming();}
#endif
				[[fallthrough]];
			case CalculateResultEnum::Bad:
				tOffset.m_iFlags &= ~iType;
				if (!(tOffset.m_iFlags /*& (PointFlagsEnum::Regular | PointFlagsEnum::Lob)*/))
				{
					it = mDirects.erase(it);
					continue;
				}
			}
			++it;
		}

		for (auto it = vSplashes.begin(); it != vSplashes.end();)
		{
			uint8_t iFlags = CalculateFlagsEnum::AccountDrag;
			if (*it == PointFlagsEnum::Lob && !m_tInfo.m_bIgnoreTiming)
				iFlags |= CalculateFlagsEnum::LobAngle;

			Solution_t tSolution; CalculateAngle(m_tInfo.m_vLocalEye, tTarget.m_vPos, i, tSolution, iFlags);
			if (tSolution.m_iCalculated == CalculateResultEnum::Bad && mDirects.empty())
			{
				it = vSplashes.erase(it);
				continue;
			}

			const float flTimeTo = tSolution.m_flTime - TICKS_TO_TIME(i);
			if (flTimeTo > m_tInfo.m_flRadiusTime || m_tInfo.m_iArmTime && m_tInfo.m_iArmTime > i)
			{
				++it;
				continue;
			}
			if (flTimeTo < -m_tInfo.m_flRadiusTime && (!m_tInfo.m_iArmTime || m_tInfo.m_iArmTime < i))
			{
				it = vSplashes.erase(it);
				continue;
			}
			if (*it == PointFlagsEnum::Lob && !ShouldLob(m_tMoveStorage, m_tInfo))
			{
				++it;
				continue;
			}

			mSplashHistory[*it].emplace_back(History_t(tTarget.m_vPos, i), fabsf(flTimeTo));
#ifdef NIKOGRAM_PRIVATE_LEARNING
            {auto& history=mSplashHistory[*it].back();history.m_flDiagnosticSim=m_tMoveStorage.m_flSimTime;history.m_flDiagnosticNetworkSim=m_tMoveStorage.m_flDiagnosticNetworkOriginTime;
            history.m_bDiagnosticGround=!m_tMoveStorage.m_bFailed&&m_tMoveStorage.m_pPlayer&&m_tMoveStorage.m_pPlayer->IsOnGround()&&!m_tMoveStorage.m_pPlayer->IsSwimming();}
#endif
			++it;
		}

		if (mDirects.empty() && vSplashes.empty())
			break;
	}

#ifdef NIKOGRAM_PRIVATE_LEARNING
    size_t diagnosticGenerated=0,diagnosticTests=0;
    float diagnosticMin=std::numeric_limits<float>::max(),diagnosticMax=0;
    bool diagnosticSplash=false;
    for(const auto& [type,history]:mDirectHistory)for(const auto& h:history)
    {++diagnosticGenerated;diagnosticMin=std::min(diagnosticMin,h.m_flDiagnosticSim);diagnosticMax=std::max(diagnosticMax,h.m_flDiagnosticSim);}
#endif
	m_iResult = false, m_bUpdate = bUpdate;
	if (!m_tInfo.m_flRadius || EffectiveSplash(m_tInfo) < Vars::Aimbot::Projectile::SplashPredictionEnum::Prefer)
		goto direct;
	else
		goto splash;
	while (!mDirectHistory.empty() || !mSplashHistory.empty())
	{
		direct: if (HandleDirect(mDirectHistory
#ifdef NIKOGRAM_PRIVATE_LEARNING
            , &diagnosticTests
#endif
        )) break;
		splash: if(DirectHitProjectile(m_tInfo) && !mDirectHistory.empty()) goto direct;
        if (HandleSplash(mSplashHistory)) {
#ifdef NIKOGRAM_PRIVATE_LEARNING
            diagnosticSplash=true;
#endif
            break;
        }
	}
#ifdef NIKOGRAM_PRIVATE_LEARNING
    PrivateLearning::EnginePathAudit(&m_tMoveStorage,diagnosticGenerated,diagnosticTests,diagnosticSplash,diagnosticMin,diagnosticMax);
#endif
	F::MoveSim.Restore(m_tMoveStorage);
	if (m_iResult && bUpdate && !CandidateAngleAllowed(m_vAngleTo,m_vTarget,m_vPredicted))
	{
        diagnostic.reason="angle_rejected";
		return false;
	}
    diagnostic.result=m_iResult;
    diagnostic.reason=m_iResult==1?"solution":m_iResult==2?"aim_only":"no_solution";
#ifdef NIKOGRAM_PRIVATE_LEARNING
    if(m_iResult==1)ReplayFinalPath();
#endif
	PrivateLearning::EngineSolution(&m_tMoveStorage,m_iResult,m_flTimeTo,m_tInfo.m_flLatency,pWeapon->GetWeaponID());
	if (!bUpdate)
		return m_iResult;

	tTarget.m_vPos = m_vTarget;
	tTarget.m_vAngleTo = m_vAngleTo;

	bool bMain = m_iResult == 1;
	bool bAny = m_iResult;
	if (bAny && (Vars::Colors::BoundHitboxEdge.Value.a || Vars::Colors::BoundHitboxFace.Value.a || Vars::Colors::BoundHitboxEdgeIgnoreZ.Value.a || Vars::Colors::BoundHitboxFaceIgnoreZ.Value.a))
	{
		float flProjectileTime = 0.f, flTargetTime = 0.f;
		bool bBox = m_bPreviewOnly || (Vars::Visuals::Hitbox::BoundsEnabled.Value & Vars::Visuals::Hitbox::BoundsEnabledEnum::OnShot);
		bool bPoint = Vars::Visuals::Hitbox::BoundsEnabled.Value & Vars::Visuals::Hitbox::BoundsEnabledEnum::AimPoint;
		bool bTimed = !Vars::Visuals::Prediction::PlayerDrawDuration.Value;
		if (bTimed)
		{
			flProjectileTime = TICKS_TO_TIME(m_vProjectilePath.size());
			flTargetTime = m_tMoveStorage.m_bFailed ? flProjectileTime : TICKS_TO_TIME(m_vPlayerPath.size());
		}
		if (bBox)
		{
			float flDuration = bTimed ? flTargetTime : Vars::Visuals::Hitbox::DrawDuration.Value;
			if (Vars::Colors::BoundHitboxEdgeIgnoreZ.Value.a || Vars::Colors::BoundHitboxFaceIgnoreZ.Value.a)
				m_vBoxes.emplace_back(m_vPredicted, tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs(), Vec3(), I::GlobalVars->curtime + flDuration, Vars::Colors::BoundHitboxEdgeIgnoreZ.Value, Vars::Colors::BoundHitboxFaceIgnoreZ.Value);
			if (Vars::Colors::BoundHitboxEdge.Value.a || Vars::Colors::BoundHitboxFace.Value.a)
				m_vBoxes.emplace_back(m_vPredicted, tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs(), Vec3(), I::GlobalVars->curtime + flDuration, Vars::Colors::BoundHitboxEdge.Value, Vars::Colors::BoundHitboxFace.Value, true);
		}
		if (bMain && bPoint)
		{
			float flDuration = bTimed ? flProjectileTime : Vars::Visuals::Hitbox::DrawDuration.Value;
			if (Vars::Colors::BoundHitboxEdgeIgnoreZ.Value.a || Vars::Colors::BoundHitboxFaceIgnoreZ.Value.a)
				m_vBoxes.emplace_back(m_vTarget, Vec3::Get(-1), Vec3::Get(1), Vec3(), I::GlobalVars->curtime + flDuration, Vars::Colors::BoundHitboxEdgeIgnoreZ.Value, Vars::Colors::BoundHitboxFaceIgnoreZ.Value);
			if (Vars::Colors::BoundHitboxEdge.Value.a || Vars::Colors::BoundHitboxFace.Value.a)
				m_vBoxes.emplace_back(m_vTarget, Vec3::Get(-1), Vec3::Get(1), Vec3(), I::GlobalVars->curtime + flDuration, Vars::Colors::BoundHitboxEdge.Value, Vars::Colors::BoundHitboxFace.Value, true);
		}
	}

	return m_iResult;
}



bool CAimbotProjectile::Aim(const Vec3& vCurAngle, const Vec3& vToAngle, Vec3& vOut, int iMethod)
{
	// Prediction-only solves need a deterministic angle even when the aim bind is off.
	if (m_bPreviewOnly) { vOut = vToAngle; Math::ClampAngles(vOut); return false; }
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
		if (!SmoothAim::Preview(vToAngle,vOut))
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
void CAimbotProjectile::Aim(CUserCmd* pCmd, Vec3& vAngles, int iMethod)
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
        if(iMethod==Vars::Aimbot::General::AimTypeEnum::Smooth)SmoothAim::Select(vAngles);
		break;
	case Vars::Aimbot::General::AimTypeEnum::Silent:
		if (auto pWeapon = H::Entities.GetWeapon();
			G::Attacking == 1 || bUnsure || pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_FLAMETHROWER)
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

static inline void CancelShot(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, int& iLastTickCancel)
{
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_COMPOUND_BOW:
	{
		pCmd->buttons |= IN_ATTACK2;
		pCmd->buttons &= ~IN_ATTACK;
		break;
	}
	case TF_WEAPON_CANNON:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	{
		for (int i = 0; i < MAX_WEAPONS; i++)
		{
			auto pSwap = pLocal->GetWeaponFromSlot(i);
			if (!pSwap || pSwap == pWeapon || !pSwap->CanBeSelected())
				continue;

			pCmd->weaponselect = pSwap->entindex();
			iLastTickCancel = pWeapon->entindex();
			break;
		}
	}
	}
}

static inline void DrawVisuals(int iResult, Target_t& tTarget, std::vector<Vec3>& vPlayerPath, std::vector<Vec3>& vProjectilePath, std::vector<DrawBox_t>& vBoxes)
{
	if (iResult == 1)
	{
		if (G::Attacking == 1)
		{
			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
			G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };
		}
		else
		{
			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
			G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount, 0 };
		}
	}

	if (G::Attacking == 1 || iResult != 1 || !Vars::Aimbot::General::AutoShoot.Value || Vars::Debug::Info.Value)
	{
		bool bPlayerPath = Vars::Visuals::Prediction::PlayerPath.Value;
		bool bProjectilePath = Vars::Visuals::Prediction::ProjectilePath.Value && (G::Attacking == 1 || Vars::Debug::Info.Value) && iResult == 1;
		bool bBoxes = Vars::Visuals::Hitbox::BoundsEnabled.Value & (Vars::Visuals::Hitbox::BoundsEnabledEnum::OnShot | Vars::Visuals::Hitbox::BoundsEnabledEnum::AimPoint);
		bool bRealPath = Vars::Visuals::Prediction::RealPath.Value && iResult == 1;
		if (bPlayerPath || bProjectilePath || bBoxes || bRealPath)
		{
			G::PathStorage.clear();
			G::BoxStorage.clear();
			G::LineStorage.clear();

			if (bPlayerPath)
			{
				float flDuration = Vars::Visuals::Prediction::PlayerDrawDuration.Value;
				if (Vars::Colors::PlayerPathIgnoreZ.Value.a)
					G::PathStorage.emplace_back(vPlayerPath, !flDuration ? -int(vPlayerPath.size()) : I::GlobalVars->curtime + flDuration, Vars::Colors::PlayerPathIgnoreZ.Value, Vars::Visuals::Prediction::PlayerPath.Value);
				if (Vars::Colors::PlayerPath.Value.a)
					G::PathStorage.emplace_back(vPlayerPath, !flDuration ? -int(vPlayerPath.size()) : I::GlobalVars->curtime + flDuration, Vars::Colors::PlayerPath.Value, Vars::Visuals::Prediction::PlayerPath.Value, true);
			}
			if (bProjectilePath)
			{
				float flDuration = Vars::Visuals::Prediction::ProjectileDrawDuration.Value;
				if (Vars::Colors::ProjectilePathIgnoreZ.Value.a)
					G::PathStorage.emplace_back(vProjectilePath, !flDuration ? -int(vProjectilePath.size()) - TIME_TO_TICKS(F::Backtrack.GetReal()) : I::GlobalVars->curtime + flDuration, Vars::Colors::ProjectilePathIgnoreZ.Value, Vars::Visuals::Prediction::ProjectilePath.Value);
				if (Vars::Colors::ProjectilePath.Value.a)
					G::PathStorage.emplace_back(vProjectilePath, !flDuration ? -int(vProjectilePath.size()) - TIME_TO_TICKS(F::Backtrack.GetReal()) : I::GlobalVars->curtime + flDuration, Vars::Colors::ProjectilePath.Value, Vars::Visuals::Prediction::ProjectilePath.Value, true);
			}
			if (bBoxes)
				G::BoxStorage.insert(G::BoxStorage.end(), vBoxes.begin(), vBoxes.end());
			if (bRealPath)
				F::Aimbot.Store(tTarget.m_pEntity, vPlayerPath.size());
		}
	}
}

bool CAimbotProjectile::RunMain(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	const int nWeaponID = pWeapon->GetWeaponID();

	static int iStaticAimType = Vars::Aimbot::General::AimType.Value;
	const int iLastAimType = iStaticAimType;
	const int iRealAimType = Vars::Aimbot::General::AimType.Value;

	switch (nWeaponID)
	{
	case TF_WEAPON_COMPOUND_BOW:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_CANNON:
		if (!Vars::Aimbot::General::AutoShoot.Value && G::Attacking && !iRealAimType && iLastAimType)
			Vars::Aimbot::General::AimType.Value = iLastAimType;
		break;
	default:
		if (G::Throwing && !iRealAimType && iLastAimType)
			Vars::Aimbot::General::AimType.Value = iLastAimType;
	}
	iStaticAimType = Vars::Aimbot::General::AimType.Value;

    if(AutoViewmodelSwitch::Supported(pWeapon) && AutoViewmodelSwitch::Pending()
        && Vars::Aimbot::General::AutoShoot.Value && !(G::OriginalCmd.buttons&IN_ATTACK))
    {
        pCmd->buttons&=~IN_ATTACK;
        ProjectileDiagnostics::Stage("viewmodel_update_pending");
        return false;
    }

	if (F::AimbotGlobal.ShouldHoldAttack(pWeapon))
		pCmd->buttons |= IN_ATTACK;
	if (!Vars::Aimbot::General::AimType.Value
		|| !F::AimbotGlobal.ShouldAim() && nWeaponID != TF_WEAPON_FLAMETHROWER)
	{
        ProjectileDiagnostics::Stage("aim_gate");
		return false;
	}

	if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon && iRealAimType
		&& (nWeaponID == TF_WEAPON_COMPOUND_BOW || nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER || nWeaponID == TF_WEAPON_CANNON && G::LastUserCmd->buttons & IN_ATTACK))
	{
		pCmd->buttons |= IN_ATTACK;
		if (!G::CanPrimaryAttack && !G::Reloading && Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent)
			return false;
	}

	auto vTargets = F::AimbotGlobal.ManageTargets(GetTargets, pLocal, pWeapon);
    ProjectileDiagnostics::Stage("eligible_targets",int(vTargets.size()));
	if (vTargets.empty())
        return !F::Aimbot.m_bRunningSecondary && ManageViewmodel(pLocal,pWeapon,pCmd,true);

#if defined(SPLASH_DEBUG1) || defined(SPLASH_DEBUG2) || defined(SPLASH_DEBUG4)
	G::LineStorage.clear();
#endif
#if defined(SPLASH_DEBUG1) || defined(SPLASH_DEBUG2) || defined(SPLASH_DEBUG3) || defined(SPLASH_DEBUG4)
	G::BoxStorage.clear();
#endif
#if defined(SPLASH_DEBUG2) && defined(WORLD_DEBUG)
	G::TriangleStorage.clear();
#endif
#if defined(SPLASH_DEBUG2) && defined(DEBUG_TEXT)
	F::Debug.ClearText();
#endif
	AmmoConservation::Context conservation(pLocal,pWeapon,pCmd);
	const size_t originalCount=vTargets.size();
	vTargets.reserve(originalCount+1);
	int deferredTarget=-1;
	for (size_t targetIndex=0; targetIndex<vTargets.size(); ++targetIndex)
	{
		if (!SearchAllowed(SearchWork::Prediction) || !SearchAllowed()) break;
		auto& tTarget=vTargets[targetIndex];
        SmoothAim::CandidateScope smoothing(tTarget.m_pEntity);
		const bool replay=targetIndex>=originalCount;
        bool reuseMovement=AutoViewmodelSwitch::Enabled() && AutoViewmodelSwitch::Supported(pWeapon)
            && G::CanPrimaryAttack && Vars::Aimbot::General::AutoShoot.Value && !(G::OriginalCmd.buttons&IN_ATTACK)
            && !F::Aimbot.m_bRunningSecondary && !I::ClientState->chokedcommands
            && tTarget.m_iTargetType==TargetEnum::Player && tTarget.m_pEntity!=pLocal
            && !tTarget.m_pEntity->As<CTFPlayer>()->IsSwimming()
            && !tTarget.m_pEntity->As<CTFPlayer>()->InCond(TF_COND_SHIELD_CHARGE);
#ifdef NIKOGRAM_PRIVATE_LEARNING
        reuseMovement=false; // Learning/replay consumers require genuine EngineTick calls.
#endif
        CMovementSimulation::ReuseScope movementReuse(F::MoveSim,reuseMovement?tTarget.m_pEntity:nullptr);
		m_flTimeTo = std::numeric_limits<float>::max();
		m_vPlayerPath.clear(); m_vProjectilePath.clear(); m_vBoxes.clear();

        m_bWorldBlocked=false;
        std::optional<bool> alternateSide;
        ProjectilePerformancePolicy::SideAttempts sideAttempts;
        int iResult=0;
        auto* preferredFlip=H::ConVars.FindVar("cl_flipviewmodels");
        if(AutoViewmodelSwitch::Enabled() && AutoViewmodelSwitch::owned && preferredFlip && preferredFlip->GetBool()
            && G::CanPrimaryAttack && AutoViewmodelSwitch::Supported(pWeapon)
            && Vars::Aimbot::General::AutoShoot.Value && !(G::OriginalCmd.buttons&IN_ATTACK)
            && !F::Aimbot.m_bRunningSecondary && !I::ClientState->chokedcommands)
        {
            const auto originalTarget=tTarget;
            sideAttempts.Mark(false);
            {ProjectileMuzzlePolicy::Trial trial(false);iResult=CanHit(tTarget,pLocal,pWeapon);}
            if(iResult==1) alternateSide=false;
            else tTarget=originalTarget;
        }
        if(!alternateSide.has_value())
        {
            m_bWorldBlocked=false;m_flTimeTo=std::numeric_limits<float>::max();
            m_vPlayerPath.clear();m_vProjectilePath.clear();m_vBoxes.clear();
            sideAttempts.Mark(ProjectileMuzzlePolicy::flipOverride.value_or(preferredFlip && preferredFlip->GetBool()));
            iResult=CanHit(tTarget,pLocal,pWeapon);
        }
        if(G::CanPrimaryAttack && AutoViewmodelSwitch::Supported(pWeapon) && ProjectileMuzzlePolicy::Retry(AutoViewmodelSwitch::Enabled(),
            Vars::Aimbot::General::AutoShoot.Value,bool(G::OriginalCmd.buttons&IN_ATTACK),F::Aimbot.m_bRunningSecondary,
            I::ClientState->chokedcommands!=0,m_bWorldBlocked,iResult))
        {
            auto* flip=H::ConVars.FindVar("cl_flipviewmodels");
            if(flip)
            {
                const auto originalTarget=tTarget;
                const bool candidateSide=!flip->GetBool();
                if(sideAttempts.Mark(candidateSide))
                {ProjectileMuzzlePolicy::Trial trial(candidateSide);iResult=CanHit(tTarget,pLocal,pWeapon);}
                else
                {
                    // The preferred-side trial already failed for this target.
                    // Do not run right/current/right and reroll the same search.
                    iResult=0;
                    ProjectileDiagnostics::Stage("viewmodel_duplicate_side_skipped",candidateSide);
                }
                if(iResult==1) alternateSide=candidateSide;
                else {tTarget=originalTarget;iResult=0;} // An aim-only trial must not leak into real firing.
                ProjectileDiagnostics::Stage("viewmodel_alternate_result",iResult);
            }
        }
		if (iResult != 1 && pWeapon->GetWeaponID() == TF_WEAPON_CANNON && Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon
			&& !(G::OriginalCmd.buttons & (IN_ATTACK | IN_USE)))
		{
			float flTime = m_flTimeTo - GRENADE_CHECK_INTERVAL;
			float flCharge = pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() > 0.f
				? pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() - I::GlobalVars->curtime
				: 1.f;
			flCharge = floorf(flCharge / GRENADE_CHECK_INTERVAL) * GRENADE_CHECK_INTERVAL + F::ProjSim.GetDesync();
			if (flCharge < flTime)
			{
				if (pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() > 0.f)
					CancelShot(pLocal, pWeapon, pCmd, m_iLastTickCancel);
			}
			else
			{
				pCmd->buttons |= IN_ATTACK;
				if (m_iLastTickCancel)
					pCmd->weaponselect = m_iLastTickCancel = 0;
				G::OriginalCmd.buttons |= IN_USE;
			}
		}
		if (!iResult) continue;
		if (iResult == 2)
		{
            // An aim-only alternative must not displace a fire-capable original.
            if (deferredTarget>=0 && !replay) continue;
            ProjectileDiagnostics::Stage("aim_only_no_fire",tTarget.m_pEntity->entindex());
			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
			DrawVisuals(iResult, tTarget, m_vPlayerPath, m_vProjectilePath, m_vBoxes);
			Aim(pCmd, tTarget.m_vAngleTo);
			break;
		}

		if (deferredTarget>=0 && !replay && !conservation.Allow())
		{
			conservation.Log("action_window_expired",deferredTarget,-1);
			continue; // Re-solve the original instead of switching on stale evidence.
		}
		if (conservation.Covered(tTarget.m_pEntity) && conservation.Allow())
		{
			if (!replay)
			{
				if (deferredTarget<0)
				{
					deferredTarget=tTarget.m_pEntity->entindex();
					conservation.Remember(deferredTarget);
					// Re-run the original solver at the end if no replacement passes.
					// Never reuse simulation/aim state overwritten by another candidate.
					vTargets.push_back(tTarget);
				}
				continue;
			}
			if (Vars::Aimbot::Projectile::AmmoConservationFallback.Value==Vars::Aimbot::Projectile::AmmoConservationFallbackEnum::BrieflyWait && conservation.Wait())
			{
				G::Attacking=0; F::Aimbot.m_bRan=false;
				m_iObservationEntity=-1;
				return true;
			}
		}
		if (deferredTarget>=0)
			conservation.Log(replay?"resume_original":"switch_validated",deferredTarget,replay?-1:tTarget.m_pEntity->entindex());
        if(alternateSide)
        {
            const bool committed=AutoViewmodelSwitch::Commit(*alternateSide,pCmd->command_number);
            pCmd->buttons&=~IN_ATTACK; G::Attacking=0; F::Aimbot.m_bRan=false;
            m_iObservationEntity=-1;
            ProjectileDiagnostics::Stage(committed?"viewmodel_committed_wait":"viewmodel_commit_failed");
            return true; // Re-solve using the real side only after its update is acknowledged.
        }
        // Save only the selected, fully validated cooldown result. Later
        // weapon handling must not overwrite the cosmetic preview snapshot.
        if (m_bReuseCooldownPreview)
        {
            m_vCooldownPreviewPoint = tTarget.m_vPos;
            m_vCooldownPreviewBoxes = m_vBoxes;
        }
		AmmoEvidenceDiagnostics::Selected(pLocal, pWeapon, tTarget.m_pEntity, pCmd);
		if (Vars::Aimbot::General::AutoShoot.Value)
		{
			switch (nWeaponID)
			{
			case TF_WEAPON_COMPOUND_BOW:
			case TF_WEAPON_PIPEBOMBLAUNCHER:
				pCmd->buttons |= IN_ATTACK;
				if (pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() > 0.f)
					pCmd->buttons &= ~IN_ATTACK;
				break;
			case TF_WEAPON_CANNON:
				pCmd->buttons |= IN_ATTACK;
				if (pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() > 0.f)
				{
					if (m_iLastTickCancel)
						pCmd->weaponselect = m_iLastTickCancel = 0;
					if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon)
					{	// rerun, if we won't hit in the future, fire
						int iResult2 = 0;

						float flCharge = pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() - I::GlobalVars->curtime;
						flCharge = floorf(flCharge / GRENADE_CHECK_INTERVAL) * GRENADE_CHECK_INTERVAL + F::ProjSim.GetDesync();
						if (flCharge > GRENADE_CHECK_INTERVAL)
						{
							auto tTarget2 = tTarget;
							float flOldDetonateTime = pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime();

							pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() -= GRENADE_CHECK_INTERVAL;
							iResult2 = CanHit(tTarget2, pLocal, pWeapon, false);

							pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() = flOldDetonateTime;
						}

						if (iResult2 != 1)
							pCmd->buttons &= ~IN_ATTACK;
					}
					else
						pCmd->buttons &= ~IN_ATTACK;
				}
				break;
			case TF_WEAPON_BAT_WOOD:
			case TF_WEAPON_BAT_GIFTWRAP:
			case TF_WEAPON_LUNCHBOX:
				pCmd->buttons |= IN_ATTACK2, pCmd->buttons &= ~IN_ATTACK;
				break;
			case TF_WEAPON_ROCKETLAUNCHER:
			case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
			case TF_WEAPON_PARTICLE_CANNON:
			case TF_WEAPON_RAYGUN:
			case TF_WEAPON_DRG_POMSON:
			case TF_WEAPON_CROSSBOW:
				pCmd->buttons |= IN_ATTACK, pCmd->buttons &= ~IN_ATTACK2;
				if (pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka)
				{
					if (pWeapon->m_iClip1() > 0)
						pCmd->buttons &= ~IN_ATTACK;
				}
				break;
			case TF_WEAPON_GRAPPLINGHOOK:
				break;
			default:
				pCmd->buttons |= IN_ATTACK;
			}
		}

		if (nWeaponID != TF_WEAPON_GRAPPLINGHOOK)
			F::Aimbot.m_bRan = G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
		else
		{
			Vec3 vOriginalAngles = pCmd->viewangles; int iOriginalButtons = pCmd->buttons;
			pCmd->viewangles = tTarget.m_vAngleTo, pCmd->buttons |= IN_ATTACK;
			F::Aimbot.m_bRan = G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
			pCmd->viewangles = vOriginalAngles, pCmd->buttons = iOriginalButtons;
			if (!G::Attacking)
				continue;

			if (Vars::Aimbot::General::AutoShoot.Value)
				pCmd->buttons |= IN_ATTACK;
		}
		DrawVisuals(iResult, tTarget, m_vPlayerPath, m_vProjectilePath, m_vBoxes);

		Aim(pCmd, tTarget.m_vAngleTo);
		// Stamp the actual validated primary-shot command, not a visual target tick.
		// Shot() still checks final attack buttons after all safety guards have run.
		if (iResult==1 && !F::Aimbot.m_bRunningSecondary)
			F::AutoFlarePunch.Validated(pWeapon,pCmd,tTarget.m_pEntity->entindex());
		if (G::PSilentAngles)
		{
			switch (nWeaponID)
			{
			case TF_WEAPON_FLAMETHROWER: // angles show up anyways
			case TF_WEAPON_CLEAVER: // can't psilent with these weapons, they use SetContextThink
			case TF_WEAPON_JAR:
			case TF_WEAPON_JAR_MILK:
			case TF_WEAPON_JAR_GAS:
			case TF_WEAPON_BAT_WOOD:
			case TF_WEAPON_BAT_GIFTWRAP:
				G::PSilentAngles = false, G::SilentAngles = true;
			}
		}
		return true;
	}

	return false;
}

#include "../../LearningAccess.h"
void CAimbotProjectile::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
    // Initial rollout: ordinary rockets only. Charged launchers and reflected
    // projectiles retain their existing timing/firing paths.
    const int budgetWeapon = pWeapon->GetWeaponID();
    const bool boundedSearch = (budgetWeapon == TF_WEAPON_ROCKETLAUNCHER || budgetWeapon == TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT)
        && pWeapon->m_iItemDefinitionIndex() != Soldier_m_TheBeggarsBazooka;
    ProjectilePerformancePolicy::SearchScope searchScope(m_SearchBudget, pCmd->command_number, boundedSearch);
    m_bSearchedThisCommand = false;
    m_bReuseCooldownPreview = false;
    m_vCooldownPreviewPoint.reset();
    m_vCooldownPreviewBoxes.clear();
    PredictionObservation::Poll();
    m_iObservationEntity=-1;
    ProjectileDiagnostics::Command diagnosticCommand(pCmd);
	// RunMain may temporarily restore a charged weapon's previous aim mode.
	const bool bAimEnabled = Vars::Aimbot::General::AimType.Value != 0;
    const bool bAimPreview = (Vars::Visuals::Viewmodel::CrosshairAim.Value && Vars::Visuals::Viewmodel::CrosshairCooldown.Value)
        || (Vars::Visuals::Viewmodel::ViewmodelAim.Value && Vars::Visuals::Viewmodel::ViewmodelCooldown.Value);
    const bool previewRequested = bAimPreview || (Vars::Visuals::Hitbox::BoundsEnabled.Value & Vars::Visuals::Hitbox::BoundsEnabledEnum::PredictDuringCooldown);
    const int aimMethod=Vars::Aimbot::General::AimType.Value;
    const bool rawAngles=aimMethod==Vars::Aimbot::General::AimTypeEnum::Plain
        || aimMethod==Vars::Aimbot::General::AimTypeEnum::Silent
        || aimMethod==Vars::Aimbot::General::AimTypeEnum::Locking;
    const int weaponID=pWeapon->GetWeaponID();
    const bool mutableLaunch=weaponID==TF_WEAPON_COMPOUND_BOW || weaponID==TF_WEAPON_PIPEBOMBLAUNCHER
        || weaponID==TF_WEAPON_CANNON || pWeapon->m_iItemDefinitionIndex()==Soldier_m_TheBeggarsBazooka;
    // Smooth/assistive aiming differs from the unsmoothed preview. Charged
    // weapons may also alter launch state in RunMain, so keep their fresh path.
    m_bReuseCooldownPreview=previewRequested && bAimEnabled && !G::CanPrimaryAttack
        && rawAngles && !mutableLaunch && !F::Aimbot.m_bRunningSecondary;
	SelfDamageDiagnostics::Snapshot("projectile_entry",pLocal,pWeapon,pCmd);
	const bool bSuccess = RunMain(pLocal, pWeapon, pCmd);
    if (pWeapon->GetWeaponID()==TF_WEAPON_PIPEBOMBLAUNCHER)
        AmmoConservationPolicy::chargeOwnership.Record(bool(pCmd->buttons&IN_ATTACK),
            Vars::Aimbot::General::AutoShoot.Value && bAimEnabled && F::AimbotGlobal.ShouldAim()
            && !(G::OriginalCmd.buttons&(IN_ATTACK|IN_ATTACK2|IN_USE)));
    if(bSuccess && ProjectileDiagnostics::current && pWeapon->GetWeaponID()==TF_WEAPON_GRENADELAUNCHER
        && m_iObservationEntity>=0 && m_iResult==1 && G::CanPrimaryAttack && (pCmd->buttons&IN_ATTACK))
        PredictionObservation::Queue(pCmd->command_number,m_iObservationEntity,m_nObservationHandle,m_flObservationTime,
            m_vPredicted,m_bObservationStartGround,m_bObservationGround,pLocal,pWeapon,m_vAngleTo,m_vTarget,m_flObservationFlight);
    ProjectileDiagnostics::Stage("run_main_success",bSuccess);
    if(bAimEnabled && F::AimbotGlobal.ShouldAim() && G::CanPrimaryAttack && (pCmd->buttons&IN_ATTACK)
        && AutoViewmodelSwitch::Immediate(pWeapon) && OutgoingWallBlocked(pLocal,pWeapon,pCmd->viewangles))
    {
        pCmd->buttons&=~IN_ATTACK;G::Attacking=0;F::Aimbot.m_bRan=false;
        ProjectileDiagnostics::Stage("outgoing_wall_guard_blocked");
    }
	SelfDamageDiagnostics::Snapshot("after_aim",pLocal,pWeapon,pCmd);
	if(SelfDamageDiagnostics::Enabled() && G::CanPrimaryAttack && ((pCmd->buttons&IN_ATTACK) || (G::OriginalCmd.buttons&IN_ATTACK)))
		SelfDamageDiagnostics::Write("guard_inputs",std::format("cmd={} aim_enabled={} should_aim={} can_fire={} attack={} modifier={} protection={} invuln={} jumper={} beggars={} weapon={} aim_success={}",pCmd->command_number,bAimEnabled,F::AimbotGlobal.ShouldAim(),G::CanPrimaryAttack,bool(pCmd->buttons&IN_ATTACK),bool(Vars::Aimbot::Projectile::Modifiers.Value&Vars::Aimbot::Projectile::ModifiersEnum::PreventSelfDamage),Vars::Aimbot::Projectile::SelfDamageProtection.Value,pLocal->IsInvulnerable(),pWeapon->m_iItemDefinitionIndex()==Soldier_m_RocketJumper,pWeapon->m_iItemDefinitionIndex()==Soldier_m_TheBeggarsBazooka,pWeapon->GetWeaponID(),bSuccess));
	// Validate the actual outgoing rocket direction, including held attack and smoothed aim.
	// Rejecting an aim candidate alone does not cancel an already-set IN_ATTACK button.
	if(bAimEnabled && F::AimbotGlobal.ShouldAim() && G::CanPrimaryAttack && (pCmd->buttons & IN_ATTACK)
		&& (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::PreventSelfDamage)
		&& Vars::Aimbot::Projectile::SelfDamageProtection.Value>0 && !pLocal->IsInvulnerable()
		&& pWeapon->m_iItemDefinitionIndex()!=Soldier_m_RocketJumper
		&& (pWeapon->GetWeaponID()==TF_WEAPON_ROCKETLAUNCHER || pWeapon->GetWeaponID()==TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT
			|| pWeapon->GetWeaponID()==TF_WEAPON_PARTICLE_CANNON)
		&& pWeapon->m_iItemDefinitionIndex()!=Soldier_m_TheBeggarsBazooka)
	{
		ProjectileInfo actual={};
		bool unsafe=!F::ProjSim.GetInfo(pLocal,pWeapon,pCmd->viewangles,actual,ProjSimEnum::Redirect|ProjSimEnum::InitCheck|ProjSimEnum::PredictCmdNum)
			|| !F::ProjSim.Initialize(actual);
		bool collided=false; int collisionTick=0;
		if(SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("guard_sim",std::format("cmd={} setup_failed={} muzzle={},{},{}",pCmd->command_number,unsafe,actual.m_vPos.x,actual.m_vPos.y,actual.m_vPos.z));
		if(!unsafe)
		{
			const auto players=SafetyPlayers(pLocal);
			CTraceFilterCollideable filter={}; filter.pSkip=pLocal; filter.iPlayer=SDK::FriendlyFire()?PLAYER_ALL:PLAYER_DEFAULT; filter.bMisc=true;
			int mask=MASK_SOLID|CONTENTS_REDTEAM|CONTENTS_BLUETEAM; F::ProjSim.SetupTrace(filter,mask,pWeapon);
			Vec3 from=actual.m_vPos;
			if(SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("guard_launch",std::format("cmd={} angle={},{},{} speed={} hull={},{},{} mask={} player_hulls={}",pCmd->command_number,actual.m_vAng.x,actual.m_vAng.y,actual.m_vAng.z,actual.m_flVelocity,actual.m_vHull.x,actual.m_vHull.y,actual.m_vHull.z,mask,players.size()));
			for(int tick=1;tick<=TIME_TO_TICKS(1.f);++tick)
			{
				F::ProjSim.RunTick(actual); const Vec3 to=F::ProjSim.GetOrigin(); CGameTrace hit={};
				SDK::TraceHull(from,to,-actual.m_vHull,actual.m_vHull,mask,&filter,&hit);
				const float engineFraction=hit.fraction;
				const int engineEntity=hit.m_pEnt?hit.m_pEnt->entindex():-1;
				const bool hullContact=SafetyPlayerCollision(from,to,actual.m_vHull,players,hit);
				if(hullContact && SelfDamageDiagnostics::Enabled())
					SelfDamageDiagnostics::Write("guard_player_hull",std::format("cmd={} step={} engine_fraction={} engine_entity={} hull_fraction={} hull_entity={} from={},{},{} to={},{},{}",pCmd->command_number,tick,engineFraction,engineEntity,hit.fraction,hit.m_pEnt->entindex(),from.x,from.y,from.z,to.x,to.y,to.z));
				if(hullContact || hit.DidHit() || hit.startsolid || hit.allsolid)
				{
					collided=true; collisionTick=tick;
					unsafe=hit.startsolid || hit.allsolid || WouldSplashLocal(pLocal,pWeapon,hit.endpos+hit.plane.normal*.5f,TICKS_TO_TIME(tick)+F::Backtrack.GetReal(),true);
					if(SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("guard_collision",std::format("cmd={} step={} unsafe={} startsolid={} allsolid={} fraction={} entity={} impact={},{},{} normal={},{},{}",pCmd->command_number,tick,unsafe,hit.startsolid,hit.allsolid,hit.fraction,hit.m_pEnt?hit.m_pEnt->entindex():-1,hit.endpos.x,hit.endpos.y,hit.endpos.z,hit.plane.normal.x,hit.plane.normal.y,hit.plane.normal.z));
					break;
				}
				from=to;
			}
		}
		if(unsafe) { pCmd->buttons&=~IN_ATTACK; G::Attacking=0; F::Aimbot.m_bRan=false; }
		if(SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("guard_result",std::format("cmd={} blocked={} collided={} collision_tick={} outgoing_attack={}",pCmd->command_number,unsafe,collided,collisionTick,bool(pCmd->buttons&IN_ATTACK)));
	}
#ifdef NIKOGRAM_PRIVATE_LEARNING
	if (PrivateLearning::WantsAimObservation() && !F::Aimbot.m_bRunningSecondary)
	{
		int target = 0;
		if (bAimEnabled)
		{
			if (G::AimTarget.m_iTickCount == I::GlobalVars->tickcount) target = G::AimTarget.m_iEntIndex;
			else
			{
				// Read-only candidate selection continues during weapon cooldown.
				// No CanHit, aim-angle, attack-button or projectile-path changes.
				auto candidates = F::AimbotGlobal.ManageTargets(GetTargets, pLocal, pWeapon);
				if (!candidates.empty()) target = candidates.front().m_pEntity->entindex();
			}
		}
		PrivateLearning::Aim(target, bool(G::OriginalCmd.buttons & IN_ATTACK) || bool(pCmd->buttons & IN_ATTACK) || G::Attacking == 1 || (bAimEnabled && Vars::Aimbot::General::AutoShoot.Value));
	}
#endif
    if (previewRequested && m_bSearchedThisCommand && G::CanPrimaryAttack)
        ProjectileDiagnostics::Stage("preview_skipped_live_search");
    if (m_bReuseCooldownPreview && m_bSearchedThisCommand && G::Attacking!=1)
    {
        ProjectileDiagnostics::Stage("preview_skipped_cooldown_search",m_vCooldownPreviewPoint.has_value());
        if (m_vCooldownPreviewPoint)
        {
            if (bAimPreview) G::CooldownAimPoint={*m_vCooldownPreviewPoint,I::GlobalVars->tickcount,2};
            G::CooldownBoxStorage=m_vCooldownPreviewBoxes;
            for (auto& box:G::CooldownBoxStorage) box.m_flTime=I::GlobalVars->curtime+TICKS_TO_TIME(2);
            ProjectileDiagnostics::Stage("cooldown_preview_reused",int(m_vCooldownPreviewBoxes.size()));
        }
    }
    if (ProjectilePerformancePolicy::NeedsPreview(bAimEnabled,F::Aimbot.m_bRunningSecondary,G::Attacking==1,
        previewRequested,m_bSearchedThisCommand,G::CanPrimaryAttack,m_bReuseCooldownPreview))
	{
		// Use the existing target/prediction rules without executing any aim or fire commands.
		auto targets = F::AimbotGlobal.ManageTargets(GetTargets, pLocal, pWeapon);
		m_bPreviewOnly = true;
		for (auto& target : targets)
		{
			if (!SearchAllowed(SearchWork::Prediction) || !SearchAllowed()) break;
			m_vPlayerPath.clear(); m_vProjectilePath.clear(); m_vBoxes.clear();
			m_flTimeTo = std::numeric_limits<float>::max();
			if (CanHit(target, pLocal, pWeapon) != 1) continue;
			if (bAimPreview)
				G::CooldownAimPoint = { target.m_vPos, I::GlobalVars->tickcount, 2 };
			G::CooldownBoxStorage = m_vBoxes;
			for (auto& box : G::CooldownBoxStorage)
				box.m_flTime = I::GlobalVars->curtime + TICKS_TO_TIME(2);
			break;
		}
		m_bPreviewOnly = false;
	}
#ifdef SPLASH_DEBUG5
	if (Vars::Aimbot::General::AimType.Value && !s_mTraceCount.empty())
	{
		int iTraceCount = 0;
		for (auto& iTraces : s_mTraceCount | std::views::values)
			iTraceCount += iTraces;
		SDK::Output("Traces", std::format("{}", iTraceCount).c_str());
		for (auto& [sType, iTraces] : s_mTraceCount)
			SDK::Output("Traces", std::format("{}: {}", sType, iTraces).c_str());
	}
	s_mTraceCount.clear();
#endif

	float flAmount = 0.f;
	if (pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
	{
		const float flCharge = pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() > 0.f ? I::GlobalVars->curtime - pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() : 0.f;
		flAmount = Math::RemapVal(flCharge, 0.f, SDK::AttribHookValue(4.f, "stickybomb_charge_rate", pWeapon), 0.f, 1.f);
	}
	else if (pWeapon->GetWeaponID() == TF_WEAPON_CANNON)
	{
		const float flMortar = SDK::AttribHookValue(0.f, "grenade_launcher_mortar_mode", pWeapon);
		const float flCharge = pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() > 0.f ? I::GlobalVars->curtime - pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() : -flMortar;
		flAmount = flMortar ? Math::RemapVal(flCharge, -flMortar, 0.f, 0.f, 1.f) : 0.f;
	}

	if (pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER && G::OriginalCmd.buttons & IN_ATTACK && Vars::Aimbot::Projectile::AutoRelease.Value && flAmount > Vars::Aimbot::Projectile::AutoRelease.Value / 100)
		pCmd->buttons &= ~IN_ATTACK;
	else if (G::CanPrimaryAttack && Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::CancelCharge)
	{
		if (m_bLastTickHeld && (G::LastUserCmd->buttons & IN_ATTACK && !(pCmd->buttons & IN_ATTACK) && !bSuccess || flAmount > 0.95f))
			CancelShot(pLocal, pWeapon, pCmd, m_iLastTickCancel);
	}

    if (boundedSearch && m_SearchBudget.limited)
        ProjectileDiagnostics::Stage("search_budget_limited");
	m_bLastTickHeld = Vars::Aimbot::General::AimType.Value;
}



// TestAngle and CanHit shares a bunch of code, possibly merge somehow

bool CAimbotProjectile::TestAngle(CBaseEntity* pProjectile, const Vec3& vPoint, Vec3& vAngles, int iSimTime, uint8_t iType, uint8_t iFlags)
{
	auto pLocal = m_tInfo.m_pLocal;
	auto pWeapon = m_tInfo.m_pWeapon;
	auto& tTarget = *m_tInfo.m_pTarget;

	m_tProjInfo = {};
	F::ProjSim.GetInfo(pProjectile, m_tProjInfo);
	CGameTrace trace = {};
	{
		CTraceFilterWorldAndPropsOnly filter = {};

		Vec3 vEyePos = pLocal->GetShootPos(); // m_tInfo.m_vLocalEye is not actually our shootpos here
		m_tProjInfo.m_vPos = pProjectile->GetAbsOrigin();

		Vec3 vPos = vPoint;
		if (m_tInfo.m_flGravity)
			vPos += Vec3(0, 0, m_tInfo.m_flGravity * pow(TICKS_TO_TIME(iSimTime), 2) / 2);
		Vec3 vForward = (vPos - m_tProjInfo.m_vPos).Normalized();
		m_tProjInfo.m_vAng = Math::VectorAngles(vForward);

		SDK::Trace(m_tProjInfo.m_vPos, m_tProjInfo.m_vPos + vForward * MAX_TRACE_LENGTH, MASK_SOLID, &filter, &trace);
		vAngles = Math::CalcAngle(vEyePos, trace.endpos);
		vForward = (vEyePos - trace.endpos).Normalized();
		if (vForward.Dot(trace.plane.normal) <= 0)
			return false;

		SDK::Trace(vEyePos, trace.endpos, MASK_SOLID, &filter, &trace);
		if (Math::FullFraction(vEyePos, trace.endpos, trace) < 0.999f)
			return false;

		if (!m_tInfo.m_pReflector || !F::AutoAirblast.CanAirblastEntity(pLocal, m_tInfo.m_pReflector, pProjectile, vAngles))
			return false;
	}
	if (!F::ProjSim.Initialize(m_tProjInfo, false, true))
		return false;

	CTraceFilterCollideable filter = {};
	filter.pSkip = iType == PointTypeEnum::Direct ? pLocal : tTarget.m_pEntity;
	filter.iPlayer = iType == PointTypeEnum::Direct ? PLAYER_DEFAULT : PLAYER_NONE;
	int nMask = MASK_SOLID;
	F::ProjSim.SetupTrace(filter, nMask, pProjectile);

	if (!m_tProjInfo.m_flGravity)
	{
		SDK::TraceHull(m_tProjInfo.m_vPos, vPoint, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace);
		if (Math::FullFraction(m_tProjInfo.m_vPos, vPoint, trace) < 0.999f && trace.m_pEnt != tTarget.m_pEntity)
			return false;
	}

	bool bDidHit = false;
	Vec3 vNew = F::ProjSim.GetOrigin();
	int iTimingTolerance = TIME_TO_TICKS(m_tInfo.m_flBoundsTime);
	float flRadiusSqr = powf(m_tProjInfo.m_flVelocity * TICK_INTERVAL + m_tProjInfo.m_vHull.z, 2);
	uint8_t iTraceInterval = iFlags == PointFlagsEnum::Lob ? Vars::Aimbot::Projectile::LobTraceInterval.Value
		: iType != PointTypeEnum::Direct ? Vars::Aimbot::Projectile::SplashTraceInterval.Value
		: Vars::Aimbot::Projectile::DirectTraceInterval.Value;
    iTraceInterval=byte(ArcPolicy::TraceInterval(iTraceInterval,m_tProjInfo.m_flGravity,TICK_INTERVAL));

	const RestoreInfo_t tOriginal = { tTarget.m_pEntity->GetAbsOrigin(), tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs() };
	tTarget.m_pEntity->SetAbsOrigin(tTarget.m_vPos);
	tTarget.m_pEntity->m_vecMins() = { std::clamp(tTarget.m_pEntity->m_vecMins().x, -24.f, 0.f), std::clamp(tTarget.m_pEntity->m_vecMins().y, -24.f, 0.f), tTarget.m_pEntity->m_vecMins().z };
	tTarget.m_pEntity->m_vecMaxs() = { std::clamp(tTarget.m_pEntity->m_vecMaxs().x, 0.f, 24.f), std::clamp(tTarget.m_pEntity->m_vecMaxs().y, 0.f, 24.f), tTarget.m_pEntity->m_vecMaxs().z };
	for (int n = 1; n <= iSimTime; n++)
	{
		F::ProjSim.RunTick(m_tProjInfo);

		if (bDidHit)
		{
			trace.endpos = F::ProjSim.GetOrigin();
			continue;
		}
		if (iTraceInterval != 1 && n % iTraceInterval && n != iSimTime)
			continue;

		Vec3 vOld = vNew; vNew = F::ProjSim.GetOrigin();
		SDK::TraceHull(vOld, vNew, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace);

		bool bHit = false;
		switch (iType)
		{
		case PointTypeEnum::Direct:
		case PointTypeEnum::Geometry:
			bHit = trace.DidHit(); break;
		case PointTypeEnum::Air:
			bHit = trace.endpos.DistToSqr(vPoint) < flRadiusSqr || trace.DidHit(); break;
		}

		if (bHit)
		{
			bool bValid = false, bTarget = true;
			switch (iType)
			{
			case PointTypeEnum::Direct:
				bTarget = trace.m_pEnt == tTarget.m_pEntity, bValid = bTarget && iSimTime - n < iTimingTolerance; break;
			case PointTypeEnum::Geometry:
				bValid = trace.endpos.DistToSqr(vPoint) < flRadiusSqr; break;
			case PointTypeEnum::Air:
				bValid = !trace.DidHit(); break;
			}

			if (bValid && iType != PointTypeEnum::Direct)
				bValid = SplashInRange(m_tInfo, trace.endpos, n, iType == PointTypeEnum::Air, tOriginal);

			if (bValid && iType != PointTypeEnum::Direct)
			{
				CGameTrace trace2 = {};
				SDK::Trace(trace.endpos + trace.plane.normal * m_tInfo.m_flNormalOffset, tTarget.m_vPos + m_tInfo.m_vTargetEye, MASK_SHOT, &filter, &trace2);
				bValid = trace2.fraction == 1.f;
			}

			if (bValid && Vars::Aimbot::Projectile::IntervalRetest.Value && iTraceInterval != 1)
			{
				CGameTrace trace2 = {}; Vec3 vOld, vNew;
				int iTicks = int(m_tProjInfo.m_vPath.size());
				if (m_tInfo.m_flGravity)
					iTicks -= iTimingTolerance;

				for (int i = 1; i < iTicks; i++)
				{
					vOld = m_tProjInfo.m_vPath[i - 1], vNew = m_tProjInfo.m_vPath[i];
					SDK::TraceHull(vOld, vNew, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace2);
					bValid = !trace2.DidHit();

					if (!bValid)
						break;
				}
			}

			if (bValid)
			{
				if (iTraceInterval != 1 && iType != PointTypeEnum::Direct)
				{
					int iInterval = n % iTraceInterval ? n % iTraceInterval : iTraceInterval;
					int iPopCount = ceilf(iInterval - trace.fraction * iInterval);
					for (int i = 0; i < iPopCount && !m_tProjInfo.m_vPath.empty(); i++)
						m_tProjInfo.m_vPath.pop_back();
				}

				bDidHit = true;
			}
			else
				break;

			if (iType == PointTypeEnum::Direct)
				trace.endpos = vNew;

			if (!bTarget || iType != PointTypeEnum::Direct)
				break;
		}
	}
	tTarget.m_pEntity->SetAbsOrigin(tOriginal.m_vOrigin);
	tTarget.m_pEntity->m_vecMins() = tOriginal.m_vMins;
	tTarget.m_pEntity->m_vecMaxs() = tOriginal.m_vMaxs;
	m_tProjInfo.m_vPath.push_back(trace.endpos);

	return bDidHit;
}


bool CAimbotProjectile::CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pProjectile)
{
    SmoothAim::CandidateScope smoothing(tTarget.m_pEntity,true);
	m_tMoveStorage = {};
	if (!F::MoveSim.Initialize(tTarget.m_pEntity, m_tMoveStorage) && tTarget.m_iTargetType == TargetEnum::Player)
	{
		F::MoveSim.Restore(m_tMoveStorage);
		return false;
	}

	m_tProjInfo = {};
	F::ProjSim.GetInfo(pProjectile, m_tProjInfo);
	if (!m_tProjInfo.m_pWeapon || !std::isfinite(m_tProjInfo.m_flVelocity) || m_tProjInfo.m_flVelocity<=0 || !F::ProjSim.Initialize(m_tProjInfo, false, true))
	{
		F::MoveSim.Restore(m_tMoveStorage);
		return false;
	}
	const Vec3 vProjectileOrigin = pProjectile->GetAbsOrigin();

	m_tInfo = { pLocal, m_tProjInfo.m_pWeapon, &tTarget, pProjectile };
	m_tInfo.m_pReflector=pWeapon;
	m_tInfo.m_flLatency = F::Backtrack.GetReal() + TICKS_TO_TIME(F::Backtrack.GetAnticipatedChoke());
	const bool timed=pProjectile->GetClassID()==ETFClassID::CTFGrenadePipebombProjectile;
	const float lifetime=AimbotAuditPolicy::RedirectLifetime(timed,-1.f,m_tInfo.m_flLatency,Vars::Aimbot::Projectile::MaxSimulationTime.Value);
	if(lifetime<=0.f)
	{
		F::MoveSim.Restore(m_tMoveStorage);
		return false; // Unknown fuse: caller retains ordinary defensive airblast.
	}
	m_tProjInfo.m_flLifetime=lifetime;
	m_tInfo.m_vHull = pProjectile->m_vecMaxs().Min(3);
	{
		CGameTrace trace = {};
		CTraceFilterWorldAndPropsOnly filter = {};

		for (int i = TIME_TO_TICKS(m_tInfo.m_flLatency); i > 0; i--)
		{
			Vec3 vOld = F::ProjSim.GetOrigin();
			F::ProjSim.RunTick(m_tProjInfo);
			Vec3 vNew = F::ProjSim.GetOrigin();

			SDK::TraceHull(vOld, vNew, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, MASK_SOLID, &filter, &trace);
			if(!AimbotAuditPolicy::ClearAdvance(trace.fraction,trace.startsolid,trace.allsolid))
			{
				F::MoveSim.Restore(m_tMoveStorage);
				return false; // Do not continue the simulation beyond an unresolved collision.
			}
			m_tProjInfo.m_vPos = trace.endpos;
		}
		m_tInfo.m_vLocalEye = m_tProjInfo.m_vPos; // just assume from the projectile without any offset, check validity later
		pProjectile->SetAbsOrigin(m_tProjInfo.m_vPos);
	}
	m_tInfo.m_vTargetEye = tTarget.m_pEntity->As<CTFPlayer>()->GetViewOffset();
	tTarget.m_vPos = tTarget.m_pEntity->m_vecOrigin();

	m_tInfo.m_flVelocity = m_tProjInfo.m_flVelocity;

	m_tInfo.m_flGravity = m_tProjInfo.m_flGravity;
	m_tInfo.m_iSplashRestrict = !m_tInfo.m_flGravity ? Vars::Aimbot::Projectile::SplashRestrictDirect.Value : Vars::Aimbot::Projectile::SplashRestrictArc.Value;

	// The projectile's launcher determines splash type/radius. The held Pyro
	// weapon is used only for reflection reach/eligibility, not flare classification.
	m_tInfo.m_flRadius = GetSplashRadius(pProjectile, m_tInfo.m_pWeapon, pLocal, Vars::Aimbot::Projectile::SplashRadius.Value / 100);
	m_tInfo.m_flBoundsTime = tTarget.m_pEntity->GetSize().Length() / m_tInfo.m_flVelocity;
	m_tInfo.m_flRadiusTime = m_tInfo.m_flBoundsTime + m_tInfo.m_flRadius / m_tInfo.m_flVelocity;
	m_tInfo.m_bIgnoreTiming = false; // Existing-projectile lob checks use matching target time too.



	Directs_t mDirects = GetDirects();
	Splashes_t vSplashes = GetSplashes();

	DirectHistory_t mDirectHistory = {};
	SplashHistory_t mSplashHistory = {};

	int iMaxTime = int(std::floor(lifetime/TICK_INTERVAL));
	for (int i = 1 - TIME_TO_TICKS(m_tInfo.m_flLatency); i <= iMaxTime; i++)
	{
		if (!m_tMoveStorage.m_bFailed)
		{
			F::MoveSim.RunTick(m_tMoveStorage);
			tTarget.m_vPos = m_tMoveStorage.m_vPredictedOrigin;
		}
		if (!TargetPolicy::HasFlightTick(i))
			continue;

		for (auto it = mDirects.begin(); it != mDirects.end();)
		{
			auto& [iIndex, tOffset] = *it;
			Vec3& vOffset = tOffset.m_vOffset;
			uint8_t iType = tOffset.m_iFlags & -tOffset.m_iFlags;

			Vec3 vPoint = tTarget.m_vPos + vOffset;
			if (Vars::Aimbot::Projectile::HuntsmanPullPoint.Value && tTarget.m_nAimedHitbox == HITBOX_HEAD)
			{
				vPoint = PullPoint(vPoint, m_tInfo.m_vLocalEye, m_tInfo, tTarget.m_vPos, tTarget.m_pEntity->m_vecMins() + m_tInfo.m_vHull, tTarget.m_pEntity->m_vecMaxs() - m_tInfo.m_vHull);
				if (Vars::Aimbot::Projectile::HuntsmanPullNoZ.Value)
					vPoint.z = tTarget.m_vPos.z + vOffset.z;
			}

			uint8_t iFlags = CalculateFlagsEnum::Accuracy;
			if (iType == PointFlagsEnum::Lob)
				iFlags |= CalculateFlagsEnum::LobAngle;
			int iTolerance = m_tInfo.m_bIgnoreTiming && iType == PointFlagsEnum::Lob ? std::numeric_limits<int>::max() : -1;

			Solution_t tSolution;
			switch (iType)
			{
			case PointFlagsEnum::Lob:
				if (ShouldLob(m_tMoveStorage, m_tInfo))
					goto end;
				tSolution.m_iCalculated = CalculateResultEnum::Bad; break;
			default: end:
				CalculateAngle(m_tInfo.m_vLocalEye, vPoint, i, tSolution, iFlags, iTolerance);
			}
			switch (tSolution.m_iCalculated)
			{
			case CalculateResultEnum::Good:
				mDirectHistory[iType].emplace_back(History_t(tTarget.m_vPos, i), tSolution.m_flPitch, tSolution.m_flYaw, tSolution.m_flTime, vPoint, iIndex);
				[[fallthrough]];
			case CalculateResultEnum::Bad:
				tOffset.m_iFlags &= ~iType;
				if (!(tOffset.m_iFlags /*& (PointFlagsEnum::Regular | PointFlagsEnum::Lob)*/))
				{
					it = mDirects.erase(it);
					continue;
				}
			}
			++it;
		}

		for (auto it = vSplashes.begin(); it != vSplashes.end();)
		{
			uint8_t iFlags = CalculateFlagsEnum::AccountDrag;
			if (*it == PointFlagsEnum::Lob && !m_tInfo.m_bIgnoreTiming)
				iFlags |= CalculateFlagsEnum::LobAngle;

			Solution_t tSolution; CalculateAngle(m_tInfo.m_vLocalEye, tTarget.m_vPos, i, tSolution, iFlags);
			if (tSolution.m_iCalculated == CalculateResultEnum::Bad && mDirects.empty())
			{
				it = vSplashes.erase(it);
				continue;
			}

			const float flTimeTo = tSolution.m_flTime - TICKS_TO_TIME(i);
			if (flTimeTo > m_tInfo.m_flRadiusTime)
			{
				++it;
				continue;
			}
			if (flTimeTo < -m_tInfo.m_flRadiusTime)
			{
				it = vSplashes.erase(it);
				continue;
			}
			if (*it == PointFlagsEnum::Lob && !ShouldLob(m_tMoveStorage, m_tInfo))
			{
				++it;
				continue;
			}

			mSplashHistory[*it].emplace_back(History_t(tTarget.m_vPos, i), fabsf(flTimeTo));
			++it;
		}

		if (mDirects.empty() && vSplashes.empty())
			break;
	}

	m_iResult = false, m_bUpdate = true;
	if (!m_tInfo.m_flRadius || EffectiveSplash(m_tInfo) < Vars::Aimbot::Projectile::SplashPredictionEnum::Prefer)
		goto direct;
	else
		goto splash;
	while (!mDirectHistory.empty() || !mSplashHistory.empty())
	{
		direct: if (HandleDirect(mDirectHistory)) break;
		splash: if(DirectHitProjectile(m_tInfo) && !mDirectHistory.empty()) goto direct;
        if (HandleSplash(mSplashHistory)) break;
	}
	F::MoveSim.Restore(m_tMoveStorage);
	pProjectile->SetAbsOrigin(vProjectileOrigin); // don't compound the latency offset onto later targets

	tTarget.m_vPos = m_vTarget;
	tTarget.m_vAngleTo = m_vAngleTo;
		
	bool bMain = m_iResult == 1;
	if (bMain)
	{
		if (Vars::Colors::BoundHitboxEdge.Value.a || Vars::Colors::BoundHitboxFace.Value.a || Vars::Colors::BoundHitboxEdgeIgnoreZ.Value.a || Vars::Colors::BoundHitboxFaceIgnoreZ.Value.a)
		{
			float flProjectileTime = 0.f, flTargetTime = 0.f;
			bool bBox = Vars::Visuals::Hitbox::BoundsEnabled.Value & Vars::Visuals::Hitbox::BoundsEnabledEnum::OnShot;
			bool bPoint = Vars::Visuals::Hitbox::BoundsEnabled.Value & Vars::Visuals::Hitbox::BoundsEnabledEnum::AimPoint;
			bool bTimed = !Vars::Visuals::Prediction::PlayerDrawDuration.Value;
			if (bTimed)
			{
				flProjectileTime = TICKS_TO_TIME(m_vProjectilePath.size());
				flTargetTime = m_tMoveStorage.m_bFailed ? flProjectileTime : TICKS_TO_TIME(m_vPlayerPath.size());
			}
			if (bBox)
			{
				float flDuration = bTimed ? flTargetTime : Vars::Visuals::Hitbox::DrawDuration.Value;
				if (Vars::Colors::BoundHitboxEdgeIgnoreZ.Value.a || Vars::Colors::BoundHitboxFaceIgnoreZ.Value.a)
					m_vBoxes.emplace_back(m_vPredicted, tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs(), Vec3(), I::GlobalVars->curtime + flDuration, Vars::Colors::BoundHitboxEdgeIgnoreZ.Value, Vars::Colors::BoundHitboxFaceIgnoreZ.Value);
				if (Vars::Colors::BoundHitboxEdge.Value.a || Vars::Colors::BoundHitboxFace.Value.a)
					m_vBoxes.emplace_back(m_vPredicted, tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs(), Vec3(), I::GlobalVars->curtime + flDuration, Vars::Colors::BoundHitboxEdge.Value, Vars::Colors::BoundHitboxFace.Value, true);
			}
			if (bPoint)
			{
				float flDuration = bTimed ? flProjectileTime : Vars::Visuals::Hitbox::DrawDuration.Value;
				if (Vars::Colors::BoundHitboxEdgeIgnoreZ.Value.a || Vars::Colors::BoundHitboxFaceIgnoreZ.Value.a)
					m_vBoxes.emplace_back(m_vTarget, Vec3::Get(-1), Vec3::Get(1), Vec3(), I::GlobalVars->curtime + flDuration, Vars::Colors::BoundHitboxEdgeIgnoreZ.Value, Vars::Colors::BoundHitboxFaceIgnoreZ.Value);
				if (Vars::Colors::BoundHitboxEdge.Value.a || Vars::Colors::BoundHitboxFace.Value.a)
					m_vBoxes.emplace_back(m_vTarget, Vec3::Get(-1), Vec3::Get(1), Vec3(), I::GlobalVars->curtime + flDuration, Vars::Colors::BoundHitboxEdge.Value, Vars::Colors::BoundHitboxFace.Value, true);
			}
		}
	}

	return m_iResult;
}

bool CAimbotProjectile::AutoAirblast(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, CBaseEntity* pProjectile)
{
    // An unverified grenade fuse cannot support a safe redirected intercept.
    // Returning false preserves the caller's ordinary airblast fallback.
    if(!pProjectile || pProjectile->GetClassID()==ETFClassID::CTFGrenadePipebombProjectile)
        return false;
	auto vTargets = F::AimbotGlobal.ManageTargets(GetTargets, pLocal, pWeapon);
	if (vTargets.empty())
		return false;

	//if (!G::AimTarget.m_iEntIndex)
	//	G::AimTarget = { vTargets.front().m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };

	for (auto& tTarget : vTargets)
	{
		m_flTimeTo = std::numeric_limits<float>::max();
		m_vPlayerPath.clear(); m_vProjectilePath.clear(); m_vBoxes.clear();

		const bool bResult = CanHit(tTarget, pLocal, pWeapon, pProjectile);
		if (!bResult) continue;

		G::Attacking = true;
		DrawVisuals(1, tTarget, m_vPlayerPath, m_vProjectilePath, m_vBoxes);

		Aim(pCmd, tTarget.m_vAngleTo, Vars::Aimbot::General::AimTypeEnum::Silent);
		return true;
	}

	return false;
}
