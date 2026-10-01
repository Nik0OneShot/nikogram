#include "AutoAirblast.h"
#include "../SelfDamageDiagnostics.h"

#include "../AimbotProjectile/AimbotProjectile.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include "../../Backtrack/Backtrack.h"

static inline bool ShouldTarget(CBaseEntity* pProjectile, CTFPlayer* pLocal)
{
	if (pProjectile->m_iTeamNum() == pLocal->m_iTeamNum())
		return false;

	switch (pProjectile->GetClassID())
	{
	case ETFClassID::CTFGrenadePipebombProjectile:
		if (pProjectile->As<CTFGrenadePipebombProjectile>()->m_bTouched())
			return false;
	}

	if (auto pWeapon = F::ProjSim.GetEntities(pProjectile).first)
	{
		if (!SDK::AttribHookValue(1, "mult_dmg", pWeapon))
			return false;
	}

	return true;
}

bool CAutoAirblast::CanAirblastEntity(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pEntity, const Vec3& vAngle)
{
	auto flRadius = SDK::AttribHookValue(1, "deflection_size_multiplier", pWeapon) * 128.f;

	Vec3 vForward; Math::AngleVectors(vAngle, &vForward);
	Vec3 vOrigin = pLocal->GetShootPos() + vForward * flRadius;

	CBaseEntity* pTarget;
	for (CEntitySphereQuery sphere(vOrigin, flRadius);
		pTarget = sphere.GetCurrentEntity();
		sphere.NextEntity())
	{
		if (pTarget == pEntity)
			break;
	}

	return pTarget == pEntity && SDK::VisPosWorld(pLocal, pEntity, pLocal->GetShootPos(), pEntity->GetAbsOrigin());
}

void CAutoAirblast::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	m_bPlayerBlast=false;
	const bool reflectEnabled=Vars::Aimbot::Projectile::AutoAirblast.Value & Vars::Aimbot::Projectile::AutoAirblastEnum::Enabled;
	if ((!reflectEnabled && !Vars::Aimbot::Projectile::PrioritizeUbered.Value) || !G::CanSecondaryAttack)
		return;

	const int iWeaponID = pWeapon->GetWeaponID();
	if (iWeaponID != TF_WEAPON_FLAMETHROWER && iWeaponID != TF_WEAPON_FLAME_BALL || SDK::AttribHookValue(0, "airblast_disabled", pWeapon))
		return;

	static auto tf_flamethrower_burstammo = H::ConVars.FindVar("tf_flamethrower_burstammo");
	int iAmmoPerShot = tf_flamethrower_burstammo->GetInt() * SDK::AttribHookValue(1, "mult_airblast_cost", pWeapon);
	int iAmmo = pLocal->GetAmmoCount(pWeapon->m_iPrimaryAmmoType());
	int iBuffType = SDK::AttribHookValue(0, "set_buff_type", pWeapon);
	int iChargedAirblast = SDK::AttribHookValue(0, "set_charged_airblast", pWeapon);
	if (iAmmo < iAmmoPerShot || iBuffType || iChargedAirblast)
		return;

	bool bShouldBlast = false;
	const Vec3 vEyePos = pLocal->GetShootPos();

	float flLatency = std::max(F::Backtrack.GetReal() - 0.03f, 0.f);
	for (auto pProjectile : H::Entities.GetGroup(EntityEnum::WorldProjectile))
	{
		if (!reflectEnabled) break;
		if (!ShouldTarget(pProjectile, pLocal))
			continue;

		Vec3 vOrigin;
		if (!SDK::PredictOrigin(vOrigin, pProjectile->m_vecOrigin(), F::ProjSim.GetVelocity(pProjectile), flLatency))
			continue;

		if (!(Vars::Aimbot::Projectile::AutoAirblast.Value & Vars::Aimbot::Projectile::AutoAirblastEnum::IgnoreFOV)
			&& Math::CalcFov(I::EngineClient->GetViewAngles(), Math::CalcAngle(vEyePos, vOrigin)) > Vars::Aimbot::Projectile::AimFOV.Value)
			continue;

		Vec3 vRestoreOrigin = pProjectile->GetAbsOrigin();
		pProjectile->SetAbsOrigin(vOrigin);
		if (Vars::Aimbot::Projectile::AutoAirblast.Value & Vars::Aimbot::Projectile::AutoAirblastEnum::Redirect)
		{
			Vec3 vAngles = Math::CalcAngle(vEyePos, vOrigin);
			if (CanAirblastEntity(pLocal, pWeapon, pProjectile, vAngles))
			{
				bShouldBlast = true;
				if (!F::AimbotProjectile.AutoAirblast(pLocal, pWeapon, pCmd, pProjectile))
				{
					SDK::FixMovement(pCmd, vAngles);
					pCmd->viewangles = vAngles;
					G::PSilentAngles = true;
				}
			}
		}
		else if (CanAirblastEntity(pLocal, pWeapon, pProjectile, pCmd->viewangles))
			bShouldBlast = true;
		pProjectile->SetAbsOrigin(vRestoreOrigin);

		if (bShouldBlast)
			break;
	}

	if (!bShouldBlast && Vars::Aimbot::Projectile::PrioritizeUbered.Value)
	{
		CTFPlayer* selected=nullptr;Vec3 angles;float nearest=std::numeric_limits<float>::max();
		int priority=std::numeric_limits<int>::min();
		for (auto entity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
		{
			auto player=entity->As<CTFPlayer>();
			if (!F::AimbotGlobal.IsUberTarget(player)
				|| F::AimbotGlobal.ShouldIgnore(player,pLocal,pWeapon,ShouldIgnoreEnum::Dormant|ShouldIgnoreEnum::Ignored,
					Vars::Aimbot::General::Target.Value,Vars::Aimbot::General::Ignore.Value&~Vars::Aimbot::General::IgnoreEnum::Invulnerable)) continue;
			const Vec3 aim=Math::CalcAngle(vEyePos,player->GetCenter());
			if (!(Math::CalcFov(I::EngineClient->GetViewAngles(),aim)<Vars::Aimbot::Projectile::AimFOV.Value)
				|| !CanAirblastEntity(pLocal,pWeapon,player,aim)) continue;
			const int rank=F::AimbotGlobal.GetPlayerPriority(player,F::AimbotGlobal.GetPriority(player->entindex()),EWeaponType::PROJECTILE);
			const float distance=vEyePos.DistToSqr(player->GetCenter());
			if (rank<priority || (rank==priority && distance>=nearest)) continue;
			selected=player;angles=aim;priority=rank;nearest=distance;
		}
		if (selected)
		{
			SDK::FixMovement(pCmd,angles);pCmd->viewangles=angles;
			pCmd->buttons&=~IN_ATTACK;G::SilentAngles=true;G::PSilentAngles=false;
			bShouldBlast=m_bPlayerBlast=true;
			if (SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("uber_airblast",std::format("cmd={} target={} distance={} secondary_ready=1 ammo={} cost={} request_not_confirmation=1",pCmd->command_number,selected->entindex(),std::sqrt(nearest),iAmmo,iAmmoPerShot));
		}
	}
	if (bShouldBlast)
	{
		G::Attacking = true;
		pCmd->buttons |= IN_ATTACK2;
	}
}
