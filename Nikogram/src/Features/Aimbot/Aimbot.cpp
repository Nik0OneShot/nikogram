#include "Aimbot.h"
#include "AutoDetonateDiagnostics.h"
#include "MeleeDiagnostics.h"
#include "AutoFlarePunch.h"
#include "AimFOVVisualPolicy.h"
#include "SmoothAim.h"
#include "../Ticks/Ticks.h"

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
	MeleeDiagnostics::Event("entry_check");
	// Keep the original short-circuit order; values identify the blocking gate.
	if (!pWeapon) { AutoDetonateDiagnostics::Event(AutoDetonateDiagnostics::Gate,-1,1); return false; }
	if (!pLocal->CanAttack()) { AutoDetonateDiagnostics::Event(AutoDetonateDiagnostics::Gate,-1,2); return false; }
	if (!SDK::AttribHookValue(1, "mult_dmg", pWeapon)) { AutoDetonateDiagnostics::Event(AutoDetonateDiagnostics::Gate,-1,3); return false; }
	if (I::EngineVGui->IsGameUIVisible()) { AutoDetonateDiagnostics::Event(AutoDetonateDiagnostics::Gate,-1,4); return false; }
	if (pCmd->weaponselect) { AutoDetonateDiagnostics::Event(AutoDetonateDiagnostics::Gate,-1,5); return false; }

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
	m_bRunningSecondary = false;
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
	AutoDetonateDiagnostics::Command autodetDiagnostic(pCmd);
	MeleeDiagnostics::Command meleeDiagnostic(pWeapon,pCmd);
	if (!ShouldRun(pLocal, pWeapon, pCmd))
	{ MeleeDiagnostics::Event("entry_rejected"); F::AutoFlarePunch.Diagnostic("aim_entry_blocked",pLocal,pWeapon,pCmd); return; }

	F::AutoDetonate.Run(pLocal, pCmd);
	F::AutoAirblast.Run(pLocal, pWeapon, pCmd);
	if (F::AutoAirblast.m_bPlayerBlast) {F::AutoFlarePunch.Diagnostic("player_airblast_reserved",pLocal,pWeapon,pCmd);return;}
	if (F::AutoFlarePunch.Run(pLocal,pWeapon,pCmd)) return;
	F::AutoHeal.Run(pLocal, pWeapon, pCmd);

	RunAimbot(pLocal, pWeapon, pCmd);
	RunAimbot(pLocal, pWeapon, pCmd, true);
}

#include "AutoViewmodelSwitch.h"
void CAimbot::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
    SmoothAim::Begin(pLocal,pWeapon,pCmd,!F::Ticks.GetShootAngle());
	m_bHitscanAssisted = false;
	F::AutoAirblast.m_bPlayerBlast=false;
	m_bRunningSecondary=false;
	AutoViewmodelSwitch::Frame();
	// Manual selection can short-circuit RunMain before the combo gets a turn.
	if (G::OriginalCmd.weaponselect) F::AutoFlarePunch.Reset("manual_weapon_selection");
    // Lifecycle/timeout handling must run even when aim/viewmodel gates skip RunMain.
    // This never selects a weapon or bypasses attack safety checks.
    F::AutoFlarePunch.Maintain(pLocal,pWeapon,pCmd);
    const bool viewmodelWait=F::AimbotProjectile.ManageViewmodel(pLocal,pWeapon,pCmd);
	Store(false);

	if(!viewmodelWait) RunMain(pLocal, pWeapon, pCmd);
    else {m_bRan=false;F::AutoFlarePunch.Diagnostic("viewmodel_frame_blocked",pLocal,pWeapon,pCmd);}

	G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
	if (F::AutoAirblast.m_bPlayerBlast) G::Attacking=1;
	F::AutoFlarePunch.Shot(pWeapon,pCmd);
    SmoothAim::Finish();
}

void CAimbot::Draw(CTFPlayer* pLocal)
{
	// This is a visual aid, independent of aim activation or weapon readiness.
	if (!Vars::Aimbot::General::FOVCircle.Value || !Vars::Colors::FOVCircle.Value.a)
		return;

	if (H::Draw.m_nScreenW <= 0 || H::Draw.m_nScreenH <= 0 || G::FOV <= 0.f)
		return;

    const float configuredFOV=G::PrimaryWeaponType==EWeaponType::MELEE?Vars::Aimbot::Melee::AimFOV.Value:F::AimbotGlobal.GetConfiguredAimFOV();
    const auto boundary=AimFOVVisualPolicy::Project(configuredFOV,G::FOV,H::Draw.m_nScreenW,H::Draw.m_nScreenH);
    if (!boundary.visible) return;
    if (boundary.fullViewport)
        H::Draw.LineRect(1,1,H::Draw.m_nScreenW-2,H::Draw.m_nScreenH-2,Vars::Colors::FOVCircle.Value);
    else
        H::Draw.LineCircle(H::Draw.m_nScreenW / 2,H::Draw.m_nScreenH / 2,float(boundary.radius),128,Vars::Colors::FOVCircle.Value);
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
