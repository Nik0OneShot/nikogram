#include "AimObserver.h"
#include "../../SDK/Helpers/ProjectileDisplay.h"
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
#include "../ImGui/Menu/Menu.h"
#include "../NeuralMovement/NeuralMovement.h"

static bool ForceProjectileAutoFire()
{
    return G::PrimaryWeaponType==EWeaponType::PROJECTILE && (NeuralMovement::ForceEnabled()||F::Aimbot.FreshProjectileShot());
}

static bool AutoFireWeapon(CTFWeaponBase* weapon)
{
    if(!weapon || weapon->m_iItemDefinitionIndex()==Soldier_m_TheBeggarsBazooka)return false;
    // Only native press-to-fire primary attacks. Release/charge weapons, tools,
    // throws and secondary abilities retain their existing ownership lifecycle.
    switch(weapon->GetWeaponID())
    {
    case TF_WEAPON_SHOTGUN_PRIMARY: case TF_WEAPON_SHOTGUN_SOLDIER:
    case TF_WEAPON_SHOTGUN_HWG: case TF_WEAPON_SHOTGUN_PYRO:
    case TF_WEAPON_SCATTERGUN: case TF_WEAPON_SNIPERRIFLE: case TF_WEAPON_SNIPERRIFLE_DECAP:
    case TF_WEAPON_MINIGUN: case TF_WEAPON_SMG: case TF_WEAPON_PISTOL: case TF_WEAPON_PISTOL_SCOUT:
    case TF_WEAPON_REVOLVER: case TF_WEAPON_SENTRY_REVENGE:
    case TF_WEAPON_HANDGUN_SCOUT_PRIMARY: case TF_WEAPON_HANDGUN_SCOUT_SECONDARY:
    case TF_WEAPON_SODA_POPPER: case TF_WEAPON_PEP_BRAWLER_BLASTER: case TF_WEAPON_MECHANICAL_ARM:
    case TF_WEAPON_ROCKETLAUNCHER: case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
    case TF_WEAPON_GRENADELAUNCHER: case TF_WEAPON_FLAMETHROWER: case TF_WEAPON_FLAME_BALL:
    case TF_WEAPON_FLAREGUN: case TF_WEAPON_FLAREGUN_REVENGE: case TF_WEAPON_RAYGUN:
    case TF_WEAPON_DRG_POMSON: case TF_WEAPON_CROSSBOW: case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
    case TF_WEAPON_SYRINGEGUN_MEDIC:return true;
    default:return false;
    }
}

void CAimbot::BeginAutoFire(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd)
{
    m_freshProjectile=false;m_requestedTarget=0;m_requestedHandle=0;
    if(G::PrimaryWeaponType==EWeaponType::PROJECTILE&&m_autoFire.armed&&m_latchedTarget){
        auto entity=reinterpret_cast<CBaseEntity*>(I::ClientEntityList->GetClientEntity(m_latchedTarget));
        if(!entity||entity->GetRefEHandle().ToInt()!=m_latchedHandle)m_autoFire.armed=false;
        else if(entity->IsPlayer()&&NeuralMovement::TargetEnabled(entity->As<CTFPlayer>()))m_freshProjectile=true;
    }
    m_autoFireTarget=m_autoFirePaced=m_autoFireAcquire=false;
    const int style=Vars::Aimbot::General::AimType.Value;
    const bool mode=style&&(AimModes::Active==AimModes::Legit||SmoothAim::VisibleGuidance());
    // Force mode requires a freshly validated neural shot on each command.
    // Begin(false) drops the old latch without changing manual attack buttons.
    const bool allowed=!ForceProjectileAutoFire()&&local&&weapon&&cmd&&local->IsAlive()&&!local->IsAGhost()&&local->CanAttack()
        &&mode&&Vars::Aimbot::General::AutoShoot.Value&&AutoFireWeapon(weapon)
        &&(G::PrimaryWeaponType==EWeaponType::HITSCAN
            ||(G::PrimaryWeaponType==EWeaponType::PROJECTILE&&AimModes::Active!=AimModes::Legit))
        &&!I::EngineVGui->IsGameUIVisible()&&!F::Menu.m_bIsOpen&&SDK::IsGameWindowInFocus()
        &&!cmd->weaponselect&&!G::OriginalCmd.weaponselect
        &&!(G::OriginalCmd.buttons&IN_USE)
        &&SDK::AttribHookValue(1,"mult_dmg",weapon)>0.f;
    m_autoFire.Begin({local?uint32_t(local->GetRefEHandle().ToInt()):0,
        weapon?uint32_t(weapon->GetRefEHandle().ToInt()):0,local?local->m_iSpawnCounter():0,
        I::ClientState->m_nServerCount,AimModes::Active,style},cmd?G::OriginalCmd.command_number:-1,allowed);
}

void CAimbot::RequireFreshProjectileShot()
{
    m_freshProjectile=true;m_autoFire.armed=false;m_autoFireAcquire=false;
    // Remove only continuation owned by this latch on this exact command.
    if(G::CurrentUserCmd&&m_autoFire.ownedCommand==m_autoFire.command&&!(G::OriginalCmd.buttons&IN_ATTACK))
        G::CurrentUserCmd->buttons&=~IN_ATTACK;
    m_autoFire.ownedCommand=-1;
}

bool CAimbot::AutoFireReady(CTFPlayer* local,CTFWeaponBase* weapon)
{
    if(!local||!weapon||!G::CanPrimaryAttack||!weapon->HasPrimaryAmmoForShot())return false;
    if(G::PrimaryWeaponType!=EWeaponType::HITSCAN)return true;
    namespace H=Vars::Aimbot::Hitscan;
    const int id=weapon->GetWeaponID();
    const bool sniper=id==TF_WEAPON_SNIPERRIFLE||id==TF_WEAPON_SNIPERRIFLE_DECAP;
    if(sniper)
    {
        if(!local->InCond(TF_COND_ZOOMED)&&((H::Modifiers.Value&H::ModifiersEnum::ScopedOnly)
            ||SDK::AttribHookValue(0,"sniper_only_fire_zoomed",weapon)))return false;
        auto rifle=weapon->As<CTFSniperRifle>();
        if((H::Modifiers.Value&H::ModifiersEnum::WaitForHeadshot)&&!G::CanHeadshot
            &&local->InCond(TF_COND_AIMING)&&rifle->GetRifleType()!=RIFLE_JARATE)return false;
        // Without a current target there is no valid health-based charge shortcut.
        if((H::Modifiers.Value&H::ModifiersEnum::WaitForCharge)&&local->InCond(TF_COND_AIMING)
            &&rifle->m_flChargedDamage()<150.f)return false;
    }
    if(id==TF_WEAPON_REVOLVER&&(H::Modifiers.Value&H::ModifiersEnum::WaitForHeadshot)
        &&SDK::AttribHookValue(0,"set_weapon_mode",weapon)==1&&!weapon->AmbassadorCanHeadshot())return false;
    if(id==TF_WEAPON_MINIGUN)
    {
        const int state=weapon->As<CTFMinigun>()->m_iWeaponState();
        if(state!=AC_STATE_FIRING&&state!=AC_STATE_SPINNING)return false;
    }
    if(m_autoFire.paced&&(H::Modifiers.Value&H::ModifiersEnum::Tapfire)&&weapon->GetWeaponSpread()!=0.f
        &&!local->InCond(TF_COND_RUNE_PRECISION))
    {
        const float elapsed=local->m_nTickBase()*TICK_INTERVAL-weapon->m_flLastFireTime();
        if(elapsed<=(weapon->GetBulletsPerShot()>1?.25f:1.25f))return false;
    }
    return true;
}

void CAimbot::ContinueAutoFire(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd)
{
    if(ForceProjectileAutoFire())return;
    if(m_autoFire.Continue(AutoFireReady(local,weapon),bool(G::OriginalCmd.buttons&IN_ATTACK),
        bool(cmd->buttons&(IN_ATTACK|IN_USE))))
    {
        cmd->buttons|=IN_ATTACK;
        if(G::PrimaryWeaponType==EWeaponType::HITSCAN)m_bHitscanAssisted=true;
    }
}

void CAimbot::FinishAutoFire(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd)
{
    // Acquisition is a request from this command's validated AutoShoot branch;
    // commit only after its final weapon guards have left a ready shot intact.
    m_autoFireAcquire=!ForceProjectileAutoFire()&&m_autoFireTarget&&G::CanPrimaryAttack&&weapon&&weapon->HasPrimaryAmmoForShot()
        &&!cmd->weaponselect&&bool(cmd->buttons&IN_ATTACK)&&SDK::IsAttacking(local,weapon,cmd,true)==1;
}

void CAimbot::FinalizeAutoFire(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd,bool compatibilityAdded,bool beforeCompatibility)
{
    if(!cmd)return;
    // Crit selection can rewrite the outgoing number; use the original input
    // identity for acquisition and sequential-command lifecycle tracking.
    const bool same=local&&weapon&&m_autoFire.command==G::OriginalCmd.command_number
        &&m_autoFire.context.owner==uint32_t(local->GetRefEHandle().ToInt())
        &&m_autoFire.context.weapon==uint32_t(weapon->GetRefEHandle().ToInt())
        &&m_autoFire.context.spawn==local->m_iSpawnCounter()&&m_autoFire.context.server==I::ClientState->m_nServerCount;
    // Later command writers can veto the initial request. Never acquire from
    // compatibility's restored button, a Triggerbot shot, or a stale marker.
    m_autoFire.Acquire(!ForceProjectileAutoFire()&&m_autoFireAcquire&&same,beforeCompatibility&&G::CanPrimaryAttack,
        bool(G::OriginalCmd.buttons&IN_ATTACK),!cmd->weaponselect&&bool(cmd->buttons&IN_ATTACK),m_autoFirePaced);
    if(m_autoFire.armed&&m_autoFireAcquire){m_latchedTarget=m_requestedTarget;m_latchedHandle=m_requestedHandle;}
    if(m_autoFire.Finalize(compatibilityAdded,beforeCompatibility,bool(G::OriginalCmd.buttons&IN_ATTACK)))
    {
        cmd->buttons&=~IN_ATTACK;
        G::Attacking=local&&weapon?SDK::IsAttacking(local,weapon,cmd,true):0;
    }
    NeuralMovement::CommitOpportunity(local,weapon,cmd);
}

bool CAimbot::ShouldRun(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	MeleeDiagnostics::Event("entry_check");
	// Keep the original short-circuit order; values identify the blocking gate.
	if (!pWeapon) { AimObserver::Mark(AimObserver::Reason::NoWeapon); AutoDetonateDiagnostics::Event(AutoDetonateDiagnostics::Gate,-1,1); return false; }
	if (!pLocal->CanAttack()) { AimObserver::Mark(AimObserver::Reason::PlayerBlocked); AutoDetonateDiagnostics::Event(AutoDetonateDiagnostics::Gate,-1,2); return false; }
	if (!SDK::AttribHookValue(1, "mult_dmg", pWeapon)) { AimObserver::Mark(AimObserver::Reason::PlayerBlocked); AutoDetonateDiagnostics::Event(AutoDetonateDiagnostics::Gate,-1,3); return false; }
	if (I::EngineVGui->IsGameUIVisible()) { AimObserver::Mark(AimObserver::Reason::Menu); AutoDetonateDiagnostics::Event(AutoDetonateDiagnostics::Gate,-1,4); return false; }
	if (pCmd->weaponselect) { AimObserver::Mark(AimObserver::Reason::Switching); AutoDetonateDiagnostics::Event(AutoDetonateDiagnostics::Gate,-1,5); return false; }

	return true;
}

void CAimbot::RunAimbot(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool bSecondaryType)
{
    AimObserver::Scope observation(!bSecondaryType);
	m_bRunningSecondary = bSecondaryType;
	EWeaponType eWeaponType = !m_bRunningSecondary ? G::PrimaryWeaponType : G::SecondaryWeaponType;
    if(!m_bRunningSecondary)ContinueAutoFire(pLocal,pWeapon,pCmd);

	bool bOriginal;
	if (m_bRunningSecondary)
		bOriginal = G::CanPrimaryAttack, G::CanPrimaryAttack = G::CanSecondaryAttack;

	switch (eWeaponType)
	{
	case EWeaponType::HITSCAN: F::AimbotHitscan.Run(pLocal, pWeapon, pCmd); break;
	case EWeaponType::PROJECTILE:
        if (AimModes::Active!=AimModes::Legit) F::AimbotProjectile.Run(pLocal, pWeapon, pCmd);
        else AimObserver::Mark(AimObserver::Reason::VisualOnly);
        break;
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

    {
        AimModes::AuxiliaryScope helpers(Vars::Aimbot::Projectile::AutoDetonate.Value&&AimModes::DetonateAllowed());
        if (AimModes::DetonateAllowed()) F::AutoDetonate.Run(pLocal, pCmd);
    }
    {
        AimModes::AuxiliaryScope helpers(Vars::Aimbot::Projectile::AutoAirblast.Value&Vars::Aimbot::Projectile::AutoAirblastEnum::Enabled);
        F::AutoAirblast.Run(pLocal, pWeapon, pCmd);
    }
	if (F::AutoAirblast.m_bPlayerBlast) {F::AutoFlarePunch.Diagnostic("player_airblast_reserved",pLocal,pWeapon,pCmd);return;}
	if (F::AutoFlarePunch.Run(pLocal,pWeapon,pCmd)) return;
	F::AutoHeal.Run(pLocal, pWeapon, pCmd);

	RunAimbot(pLocal, pWeapon, pCmd);
	RunAimbot(pLocal, pWeapon, pCmd, true);
}

#include "AutoViewmodelSwitch.h"
void CAimbot::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
    AimObserver::Begin(pLocal,pWeapon,pCmd);
    ProjectileDisplay::BeginCommand();
    BeginAutoFire(pLocal,pWeapon,pCmd);
    F::AimbotGlobal.BeginTargetSwitch(pLocal, pWeapon, pCmd);
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
    F::AimbotProjectile.MaintainBowCharge(pLocal,pWeapon,pCmd);
    F::AimbotProjectile.ObservePills(); // pill fuse clock: every command, aiming or not
    const bool visualProjectile=AimModes::Active==AimModes::Legit&&G::PrimaryWeaponType==EWeaponType::PROJECTILE;
    if (visualProjectile) F::AimbotProjectile.m_iLastTickCancel=0;
    const bool viewmodelWait=!visualProjectile&&F::AimbotProjectile.ManageViewmodel(pLocal,pWeapon,pCmd);
	Store(false);

	if(!viewmodelWait) RunMain(pLocal, pWeapon, pCmd);
    else {AimObserver::Mark(AimObserver::Reason::Switching);m_bRan=false;F::AutoFlarePunch.Diagnostic("viewmodel_frame_blocked",pLocal,pWeapon,pCmd);}

    F::AimbotGlobal.EndTargetSwitch();
	G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
	if (F::AutoAirblast.m_bPlayerBlast) G::Attacking=1;
    FinishAutoFire(pLocal,pWeapon,pCmd);
	F::AutoFlarePunch.Shot(pWeapon,pCmd);
    SmoothAim::Finish();
    {AimObserver::Scope preview(false);F::AimbotProjectile.UpdateLegitGuide(pLocal,pWeapon,pCmd);}
}

void CAimbot::Draw(CTFPlayer* pLocal)
{
    F::AimbotProjectile.DrawLegitGuide(pLocal);
	// This is a visual aid, independent of aim activation or weapon readiness.
	if (!Vars::Aimbot::General::FOVCircle.Value || (!AimModes::Banked && !Vars::Colors::FOVCircle.Value.a))
		return;

	if (H::Draw.m_nScreenW <= 0 || H::Draw.m_nScreenH <= 0 || G::FOV <= 0.f)
		return;

    const auto draw=[&](float configuredFOV,Color_t colour)
    {
        if(!colour.a)return;
        const auto boundary=AimFOVVisualPolicy::Project(configuredFOV,G::FOV,H::Draw.m_nScreenW,H::Draw.m_nScreenH);
        if(!boundary.visible)return;
        if(boundary.fullViewport)H::Draw.LineRect(1,1,H::Draw.m_nScreenW-2,H::Draw.m_nScreenH-2,colour);
        else H::Draw.LineCircle(H::Draw.m_nScreenW/2,H::Draw.m_nScreenH/2,float(boundary.radius),128,colour);
    };
    if(AimModes::Banked)
    {
        for(int mode: {AimModes::Rage,AimModes::Legit})
        {
            if(!AimModes::Get(Vars::Aimbot::General::FOVCircle,mode).Value)continue;
            float fov=AimModes::Get(Vars::Aimbot::General::AimFOV,mode).Value;
            if(G::PrimaryWeaponType==EWeaponType::PROJECTILE)fov=std::min(fov,AimModes::Get(Vars::Aimbot::Projectile::AimFOV,mode).Value);
            else if(G::PrimaryWeaponType==EWeaponType::MELEE)fov=std::min(fov,AimModes::Get(Vars::Aimbot::Melee::AimFOV,mode).Value);
            draw(fov,Vars::AimModes::CircleOverride.Value?Vars::AimModes::CircleColour.Value:AimModes::Get(Vars::Colors::FOVCircle,mode).Value);
        }
    }
    else draw(G::PrimaryWeaponType==EWeaponType::MELEE?Vars::Aimbot::Melee::AimFOV.Value:F::AimbotGlobal.GetConfiguredAimFOV(),Vars::Colors::FOVCircle.Value);
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
