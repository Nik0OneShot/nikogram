#include "Aimbot.h"

#include "AimbotHitscan/AimbotHitscan.h"
#include "AimbotProjectile/AimbotProjectile.h"
#include "AimbotMelee/AimbotMelee.h"
#include "AutoDetonate/AutoDetonate.h"
#include "AutoAirblast/AutoAirblast.h"
#include "AutoHeal/AutoHeal.h"
#include "AutoRocketJump/AutoRocketJump.h"
#include "../Misc/Misc.h"
#include "../Visuals/Visuals.h"

bool CAimbot::ShouldRun(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pWeapon || !pLocal->CanAttack()
		|| !SDK::AttribHookValue(1, "mult_dmg", pWeapon)
		|| I::EngineVGui->IsGameUIVisible()
		|| pCmd->weaponselect)
		return false;

	return true;
}

void CAimbot::RunAimbot(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool bSecondaryType)
{
	m_bRunningSecondary = bSecondaryType;
	EWeaponType eWeaponType = !m_bRunningSecondary ? G::PrimaryWeaponType : G::SecondaryWeaponType;

	bool bOriginal;
	if (m_bRunningSecondary)
		bOriginal = G::CanPrimaryAttack, G::CanPrimaryAttack = G::CanSecondaryAttack;

	switch (eWeaponType)
	{
	case EWeaponType::HITSCAN: F::AimbotHitscan.Run(pLocal, pWeapon, pCmd); break;
	case EWeaponType::PROJECTILE: F::AimbotProjectile.Run(pLocal, pWeapon, pCmd); break;
	case EWeaponType::MELEE: F::AimbotMelee.Run(pLocal, pWeapon, pCmd); break;
	}

	if (m_bRunningSecondary)
		G::CanPrimaryAttack = bOriginal;
}

void CAimbot::RunMain(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (F::AimbotProjectile.m_iLastTickCancel)
	{
		pCmd->weaponselect = F::AimbotProjectile.m_iLastTickCancel;
		F::AimbotProjectile.m_iLastTickCancel = 0;
	}

	m_bRan = false;
	G::CooldownBoxStorage.clear();
	G::CooldownAimPoint = {};
	G::ExtrapolationGuide = {};
	if (abs(G::AimTarget.m_iTickCount - I::GlobalVars->tickcount) > G::AimTarget.m_iDuration)
		G::AimTarget = {};
	if (abs(G::AimPoint.m_iTickCount - I::GlobalVars->tickcount) > G::AimPoint.m_iDuration)
		G::AimPoint = {};

	F::AutoRocketJump.Run(pLocal, pWeapon, pCmd);
	if (!ShouldRun(pLocal, pWeapon, pCmd))
		return;

	F::AutoDetonate.Run(pLocal, pCmd);
	F::AutoAirblast.Run(pLocal, pWeapon, pCmd);
	F::AutoHeal.Run(pLocal, pWeapon, pCmd);

	RunAimbot(pLocal, pWeapon, pCmd);
	RunAimbot(pLocal, pWeapon, pCmd, true);
}

void CAimbot::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	Store(false);

	RunMain(pLocal, pWeapon, pCmd);

	G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
}

void CAimbot::Draw(CTFPlayer* pLocal)
{
	// This is a visual aid, independent of aim activation or weapon readiness.
	if (!Vars::Aimbot::General::FOVCircle.Value || !Vars::Colors::FOVCircle.Value.a)
		return;

	if (H::Draw.m_nScreenW <= 0 || H::Draw.m_nScreenH <= 0 || G::FOV <= 0.f)
		return;

	// Very wide FOVs cannot have a finite on-screen perspective boundary.
	// Keep a capped indicator visible instead of hiding it when an aim bind releases.
	const float flDrawFOV = std::clamp(Vars::Aimbot::General::AimFOV.Value, 0.1f, 89.f);
	float flRadius = tanf(Math::Deg2Rad(flDrawFOV)) / tanf(Math::Deg2Rad(G::FOV) / 2) * float(H::Draw.m_nScreenH) * (2.f / 3.f);
	flRadius = std::min(flRadius, float(std::min(H::Draw.m_nScreenW, H::Draw.m_nScreenH)) * 0.48f);
	H::Draw.LineCircle(H::Draw.m_nScreenW / 2, H::Draw.m_nScreenH / 2, flRadius, 68, Vars::Colors::FOVCircle.Value);
}

void CAimbot::Store(CBaseEntity* pEntity, size_t iSize)
{
	if (!Vars::Visuals::Prediction::RealPath.Value)
		return;

	if (!pEntity->IsPlayer())
		return;

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	int iUserID = pResource->m_iUserID(pEntity->entindex());
	float flDuration = Vars::Visuals::Prediction::PlayerDrawDuration.Value ? Vars::Visuals::Prediction::PlayerDrawDuration.Value : 5.f;
	m_mRealPaths[iUserID] = {
		{ { pEntity->m_vecOrigin() }, I::GlobalVars->curtime + flDuration, Color_t(), Vars::Visuals::Prediction::RealPath.Value },
		iSize
	};
}

void CAimbot::Store(bool bFrameStageNotify)
{
	if (!Vars::Visuals::Prediction::RealPath.Value)
		return;

	int iLag = 1;
	if (bFrameStageNotify)
	{
		static int iStaticTickcout = I::GlobalVars->tickcount;
		iLag = I::GlobalVars->tickcount - iStaticTickcout;
		iStaticTickcout = I::GlobalVars->tickcount;
	}

	for (auto it = m_mRealPaths.begin(); it != m_mRealPaths.end();)
	{
		auto& [iUserID, tPath] = *it;
		if (tPath.m_tPath.m_vPath.size() >= tPath.m_iSize || tPath.m_tPath.m_flTime < I::GlobalVars->curtime)
		{
			if (tPath.m_tPath.m_tColor = Vars::Colors::RealPath.Value, tPath.m_tPath.m_bZBuffer = true; tPath.m_tPath.m_tColor.a)
				G::PathStorage.push_back(tPath.m_tPath);
			if (tPath.m_tPath.m_tColor = Vars::Colors::RealPathIgnoreZ.Value, tPath.m_tPath.m_bZBuffer = false; tPath.m_tPath.m_tColor.a)
				G::PathStorage.push_back(tPath.m_tPath);
			it = m_mRealPaths.erase(it);
			continue;
		}
		++it;

		int iIndex = I::EngineClient->GetPlayerForUserID(iUserID);
		if (iIndex <= 0) // player left, 0 would be the world
			continue;
		if (bFrameStageNotify ? iIndex == I::EngineClient->GetLocalPlayer() : iIndex != I::EngineClient->GetLocalPlayer())
			continue;

		auto pPlayer = I::ClientEntityList->GetClientEntity(iIndex)->As<CTFPlayer>();
		if (!pPlayer)
			continue;

		for (int i = 0; i < iLag && tPath.m_tPath.m_vPath.size() < tPath.m_iSize; i++)
			tPath.m_tPath.m_vPath.push_back(pPlayer->m_vecOrigin());
	}
}
