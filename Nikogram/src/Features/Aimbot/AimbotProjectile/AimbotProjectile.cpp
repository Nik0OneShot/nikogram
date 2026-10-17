#include "../AimObserver.h"
#include "../../../SDK/Helpers/ProjectileDisplay.h"
#include "AimbotProjectile.h"
#include "../../NeuralMovement/NeuralMovement.h"
#include "ProjectilePointPolicy.h"
#include "FeetPointPolicy.h"
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
#include "../BowCharge.h"
#include "../ArcPolicy.h"
#include "../AimbotAuditPolicy.h"
#include "../ProjectileAimPolicy.h"
#include "../AutoViewmodelSwitch.h"
#include "../ProjectileGuidePolicy.h"
#include "../ProjectileGuideCamera.h"
#include "../ProjectileCommandDiagnostics.h"
#include "../../ImGui/Menu/Menu.h"
#include "../../Spectate/Spectate.h"

#include "../Aimbot.h"
#include "../AutoFlarePunch.h"
#include "../ProjectileDiagnostics.h"
#include "../AmmoEvidenceDiagnostics.h"
#include "../AmmoLifetimeDiagnostics.h"
#include "../AmmoConservation.h"
#include "../KnockbackPrediction.h"
#include "../PredictionObservation.h"
#include "../LeadRestrictPolicy.h"
#include "../../Ticks/Ticks.h"
#include "../../EnginePrediction/EnginePrediction.h"
#include "../../LearningAccess.h"
#include "../../World/World.h"
#include "../AutoAirblast/AutoAirblast.h"
#include "../../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include <numeric>
#include <map>
#include <tuple>
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

static inline std::vector<Target_t> CollectProjectileTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool guide)
{
	std::vector<Target_t> vTargets;

	Vec3 vLocalPos = F::Ticks.GetShootPos();
	Vec3 vLocalAngles = I::EngineClient->GetViewAngles();
    if(guide)ProjectileGuideCamera::Ray(pLocal,vLocalPos,vLocalAngles);
    const auto guidePoint=[&](CBaseEntity* entity,float& fov,Vec3& point,Vec3& angle)
    {
        Vec3 direction;Math::AngleVectors(vLocalAngles,&direction);
        if(!ProjectileGuidePolicy::Focus(vLocalPos,direction,entity->m_vecOrigin(),entity->m_vecMins(),entity->m_vecMaxs(),point))return false;
        angle=Math::CalcAngle(vLocalPos,point);fov=Math::CalcFov(vLocalAngles,angle);
        return std::isfinite(fov);
    };


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
			if (!guide && F::AutoFlarePunch.Target()>=0 && pEntity->entindex()!=F::AutoFlarePunch.Target()) continue;
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon, iFunctionFlags))
				continue;

			bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
            // Friendly fire must not implicitly enable automatic Crossbow healing.
            if (bTeam && pWeapon->GetWeaponID() == TF_WEAPON_CROSSBOW && !Vars::Aimbot::Healing::AutoArrow.Value)
                continue;
			if (bTeam && bHeal)
			{
				if (pEntity->As<CTFPlayer>()->m_iHealth() >= pEntity->As<CTFPlayer>()->GetMaxHealth()
					|| Vars::Aimbot::Healing::HealPriority.Value == Vars::Aimbot::Healing::HealPriorityEnum::FriendsOnly && !H::Entities.IsFriend(pEntity->entindex()) && !H::Entities.InParty(pEntity->entindex()))
					continue;
			}

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!(guide ? guidePoint(pEntity,flFOVTo,vPos,vAngleTo) : F::AimbotGlobal.PlayerBoneInFOV(pEntity->As<CTFPlayer>(), vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo)))
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

		if (pWeapon->GetWeaponID() == TF_WEAPON_LUNCHBOX || (!guide && F::AutoFlarePunch.Target()>=0))
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
			if (!(guide ? guidePoint(pEntity,flFOVTo,vPos,vAngleTo) : F::AimbotGlobal.EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo)))
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
				if (!(guide ? guidePoint(pEntity,flFOVTo,vPos,vAngleTo) : F::AimbotGlobal.EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo)))
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
			if (!(guide ? guidePoint(pEntity,flFOVTo,vPos,vAngleTo) : F::AimbotGlobal.EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo)))
				continue;

			float flDistTo = vLocalPos.DistToSqr(vPos);
			vTargets.emplace_back(pEntity, TargetEnum::NPC, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	return vTargets;
}



static inline std::vector<Target_t> GetTargets(CTFPlayer* local,CTFWeaponBase* weapon)
{return CollectProjectileTargets(local,weapon,false);}

static float GuideChargeBegin(CTFWeaponBase* weapon)
{
    return weapon&&(weapon->GetWeaponID()==TF_WEAPON_COMPOUND_BOW||weapon->GetWeaponID()==TF_WEAPON_PIPEBOMBLAUNCHER)
        ?weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime():0.f;
}
static bool GuideOverheld(CTFPlayer* local,CTFWeaponBase* weapon)
{
    const float begin=GuideChargeBegin(weapon);
    return weapon->GetWeaponID()==TF_WEAPON_COMPOUND_BOW&&begin>0
        &&TICKS_TO_TIME(local->m_nTickBase())-begin>=TF_ARROW_MAX_CHARGE_TIME;
}

void CAimbotProjectile::UpdateLegitGuide(CTFPlayer* local, CTFWeaponBase* weapon, const CUserCmd* cmd)
{
    Guide next;
    const auto publish=[&] {std::lock_guard lock(m_guideMutex);m_guide=std::move(next);};
    if (!local || !weapon || !cmd || !local->IsAlive() || !local->CanAttack()
        || AimModes::Active!=AimModes::Legit || G::PrimaryWeaponType!=EWeaponType::PROJECTILE
        || (!Vars::AimModes::ProjectileMarker.Value && !Vars::AimModes::ProjectileImpact.Value && !Vars::AimModes::ProjectileTrajectory.Value)
        || G::Unload || F::Menu.m_bIsOpen || I::EngineVGui->IsGameUIVisible()
        || I::MatSystemSurface->IsCursorVisible() || !SDK::IsGameWindowInFocus()
        || !I::EngineClient->IsInGame() || SDK::CleanScreenshot() || F::Spectate.HasTarget()
        || cmd->weaponselect || Vars::Visuals::Trajectory::Override.Value)
    {publish();return;}

    // Prediction replaces tickcount with local tickbase until CreateMove ends.
    // Realtime is unchanged by that scope and is shared with the render reader.
    next.realtime=I::GlobalVars->realtime;
    next.owner=local->GetRefEHandle().ToInt(); next.weapon=weapon->GetRefEHandle().ToInt();
    next.spawn=local->m_iSpawnCounter();next.server=I::ClientState->m_nServerCount;next.chargeBegin=GuideChargeBegin(weapon);
    if (Vars::AimModes::ProjectileMarker.Value)
    {
        auto targets=CollectProjectileTargets(local,weapon,true);
        const Vec3 eye=local->GetShootPos();
        std::erase_if(targets,[&](const Target_t& t) {
            return !t.m_pEntity || t.m_pEntity->IsDormant()
                || !std::isfinite(t.m_flFOVTo);
        });
        std::stable_sort(targets.begin(),targets.end(),[&](const Target_t& a,const Target_t& b) {
            return a.m_flFOVTo!=b.m_flFOVTo ? a.m_flFOVTo<b.m_flFOVTo
                : a.m_flDistTo!=b.m_flDistTo ? a.m_flDistTo<b.m_flDistTo
                : a.m_pEntity->GetRefEHandle().ToInt()<b.m_pEntity->GetRefEHandle().ToInt();
        });
        // Reuse collision-validated prediction, never Run/Aim(CUserCmd*) or firing control.
        struct PreviewScope
        {
            bool& preview; bool& guide; bool oldPreview,oldGuide;
            PreviewScope(bool& p,bool& g):preview(p),guide(g),oldPreview(p),oldGuide(g){p=g=true;}
            ~PreviewScope(){preview=oldPreview;guide=oldGuide;}
        } preview(m_bPreviewOnly,m_bLegitGuide);
        ProjectilePerformancePolicy::SearchScope budget(m_SearchBudget,cmd->command_number,true);
        int attempts=0;
        for (size_t i=0;i<std::min(size_t(ProjectileGuidePolicy::MaxVisibility),targets.size()) && attempts<3;++i)
        {
            auto& target=targets[i];
            if(!SDK::VisPos(local,target.m_pEntity,eye,target.m_vPos))continue;
            if(!SearchAllowed(SearchWork::Prediction))break;
            ++attempts;
            m_vGuideOffset=target.m_vPos-target.m_pEntity->m_vecOrigin();
            m_vBoxes.clear();
            const int result=CanHit(target,local,weapon,true);
            m_vBoxes.clear();
            if (result!=1 || !ProjectileGuidePolicy::Valid(m_vAngleTo.x,m_vAngleTo.y,m_flTimeTo)) continue;
            Vec3 direction;Math::AngleVectors(m_vAngleTo,&direction);
            // Project the required LAUNCH direction, not the future enemy position.
            Vec3 cameraEye,cameraAngles;ProjectileGuideCamera::Ray(local,cameraEye,cameraAngles);
            next.markerDistance=std::max(128.f,eye.DistTo(m_vTarget));next.launchAngles=m_vAngleTo;
            next.marker=cameraEye+direction*next.markerDistance;
            next.target=target.m_pEntity->GetRefEHandle().ToInt();next.valid=true;
            next.targetSpawn=target.m_pEntity->IsPlayer()?target.m_pEntity->As<CTFPlayer>()->m_iSpawnCounter():-1;
            next.targetModel=target.m_pEntity->GetModel();next.focusOffset=m_vGuideOffset;
            next.targetCue=target.m_pEntity->m_vecOrigin()+Vec3(0,0,target.m_pEntity->m_vecMaxs().z+10.f);
            next.aligned=ProjectileGuidePolicy::Aligned(cmd->viewangles.x,cmd->viewangles.y,m_vAngleTo.x,m_vAngleTo.y);
            next.uncertain=GuideOverheld(local,weapon) || m_tMoveStorage.m_bFailed || m_flTimeTo>.5f
                || (target.m_pEntity->IsPlayer() && target.m_pEntity->As<CTFPlayer>()->m_vecVelocity().Length()>20.f);
            break;
        }
    }
    publish();
}

void CAimbotProjectile::RefreshGuidePath(CTFPlayer* local,CTFWeaponBase* weapon)
{
    if(m_guidePathFrame==I::GlobalVars->framecount && m_renderPath.owner==unsigned(local->GetRefEHandle().ToInt())
        && m_renderPath.weapon==unsigned(weapon->GetRefEHandle().ToInt()))return;
    m_guidePathFrame=I::GlobalVars->framecount;
    m_renderPath={};m_renderPath.owner=local->GetRefEHandle().ToInt();m_renderPath.weapon=weapon->GetRefEHandle().ToInt();
    if(!Vars::AimModes::ProjectileImpact.Value&&!Vars::AimModes::ProjectileTrajectory.Value)return;
    using Clock=std::chrono::steady_clock;
    const auto start=Clock::now();
    const auto elapsed=[&]{return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count();};
    ProjectileInfo info;
    const int flags=ProjSimEnum::Redirect|ProjSimEnum::InitCheck|ProjSimEnum::Interp|ProjSimEnum::NoRandomAngles|ProjSimEnum::DiagnosticDeterministic;
    // The existing render trajectory uses Interp fire setup. No player movement
    // prediction, command mutation, splash worker, or cached collision result here.
    if(!F::ProjSim.GetInfo(local,weapon,I::EngineClient->GetViewAngles(),info,flags)||!F::ProjSim.Initialize(info))return;
    CTraceFilterCollideable filter;filter.pSkip=local;
    int mask=MASK_SOLID;F::ProjSim.SetupTrace(filter,mask,weapon,0,true);
    Vec3 from=F::ProjSim.GetOrigin();CGameTrace trace{};
    SDK::TraceHull(from,from,-info.m_vHull,info.m_vHull,mask,&filter,&trace);
    if(trace.startsolid||trace.allsolid)return;
    m_renderPath.path.push_back(from);
    const int ticks=ProjectileGuidePolicy::PathTicks(info.m_flLifetime,TICK_INTERVAL);
    for(int n=1;n<=ticks && ProjectileGuidePolicy::PathStep(n-1,elapsed());++n)
    {
        F::ProjSim.RunTick(info,false);
        const Vec3 to=F::ProjSim.GetOrigin();
        if(!std::isfinite(to.x)||!std::isfinite(to.y)||!std::isfinite(to.z)){m_renderPath.path.clear();break;}
        F::ProjSim.SetupTrace(filter,mask,weapon,n,true);
        SDK::TraceHull(from,to,-info.m_vHull,info.m_vHull,mask,&filter,&trace);
        m_renderPath.path.push_back(trace.DidHit()?trace.endpos:to);
        if(trace.DidHit())
        {
            m_renderPath.impact=trace.endpos;
            m_renderPath.impactValid=!GuideOverheld(local,weapon)&&!trace.startsolid&&!trace.allsolid
                &&std::isfinite(trace.endpos.x)&&std::isfinite(trace.endpos.y)&&std::isfinite(trace.endpos.z);
            break;
        }
        from=to;
    }
}

void CAimbotProjectile::DrawLegitGuide(CTFPlayer* local)
{
    if (!local || !local->IsAlive() || AimModes::Active!=AimModes::Legit || F::Menu.m_bIsOpen
        || G::Unload || SDK::CleanScreenshot() || F::Spectate.HasTarget()
        || I::EngineVGui->IsGameUIVisible() || I::MatSystemSurface->IsCursorVisible() || !SDK::IsGameWindowInFocus()
        || !I::EngineClient->IsInGame() || !local->CanAttack() || Vars::Visuals::Trajectory::Override.Value)
    {m_renderPath={};m_guidePathFrame=-1;m_guideDisplay={};return;}
    Guide guide;
    {std::lock_guard lock(m_guideMutex);guide=m_guide;}
    auto* weapon=H::Entities.GetWeapon();
    if (!weapon || guide.owner!=unsigned(local->GetRefEHandle().ToInt())
        || guide.weapon!=unsigned(weapon->GetRefEHandle().ToInt())
        || guide.spawn!=local->m_iSpawnCounter() || guide.server!=I::ClientState->m_nServerCount
        || !ProjectileGuidePolicy::FreshTime(I::GlobalVars->realtime,guide.realtime))
    {m_renderPath={};m_guidePathFrame=-1;m_guideDisplay={};return;}
    RefreshGuidePath(local,weapon);
    guide.path=m_renderPath.path;guide.impact=m_renderPath.impact;guide.impactValid=m_renderPath.impactValid;
    if(guide.chargeBegin!=GuideChargeBegin(weapon)
        || !ProjectileGuidePolicy::FreshTime(I::GlobalVars->realtime,guide.realtime,ProjectileGuidePolicy::MarkerLifetime(TICK_INTERVAL)))guide.valid=false;
    if(guide.valid)
    {
        auto* client=I::ClientEntityList->GetClientEntityFromHandle(CBaseHandle(guide.target));
        auto* target=client?client->As<CBaseEntity>():nullptr;
        if(!target||target->IsDormant()||target->GetModel()!=guide.targetModel
            || (target->IsPlayer()&&(!target->As<CTFPlayer>()->IsAlive()||target->As<CTFPlayer>()->m_iSpawnCounter()!=guide.targetSpawn))
            || F::AimbotGlobal.ShouldIgnore(target,local,weapon)
            || !SDK::VisPos(local,target,local->GetShootPos(),target->m_vecOrigin()+guide.focusOffset))guide.valid=false;
    }
    if(guide.valid)
    {
        Vec3 direction;Math::AngleVectors(guide.launchAngles,&direction);
        const bool same=m_guideDisplay.target==guide.target&&m_guideDisplay.owner==guide.owner
            &&m_guideDisplay.weapon==guide.weapon&&m_guideDisplay.spawn==guide.spawn&&m_guideDisplay.server==guide.server
            &&m_guideDisplay.chargeBegin==guide.chargeBegin;
        if(m_guideDisplay.frame!=I::GlobalVars->framecount||!same)
        {
            const float dt=I::GlobalVars->realtime-m_guideDisplay.realtime;
            const float change=Math::CalcFov(Math::VectorAngles(m_guideDisplay.direction),guide.launchAngles);
            if(ProjectileGuidePolicy::BlendDisplay(same,dt,change))
                direction=(m_guideDisplay.direction+(direction-m_guideDisplay.direction)*ProjectileGuidePolicy::DisplayAlpha(dt)).Normalized();
            m_guideDisplay={direction,guide.target,guide.owner,guide.weapon,I::GlobalVars->framecount,guide.spawn,guide.server,I::GlobalVars->realtime,guide.chargeBegin};
        }
        Vec3 cameraEye,cameraAngles;ProjectileGuideCamera::Ray(local,cameraEye,cameraAngles);
        guide.marker=cameraEye+m_guideDisplay.direction*guide.markerDistance;
        const Vec3 shotAngles=I::EngineClient->GetViewAngles();
        guide.uncertain=guide.uncertain||GuideOverheld(local,weapon);
        guide.aligned=!guide.uncertain&&ProjectileGuidePolicy::Aligned(shotAngles.x,shotAngles.y,guide.launchAngles.x,guide.launchAngles.y);
    }
    else m_guideDisplay={};
    const float scale=ProjectileGuidePolicy::Scale(H::Draw.m_nScreenH);
    const Color_t outline(12,16,22,240),gold(255,201,112,255);
    const auto line=[&](int x1,int y1,int x2,int y2,Color_t color) {
        H::Draw.Line(x1-1,y1,x2-1,y2,outline);
        H::Draw.Line(x1+1,y1,x2+1,y2,outline);
        H::Draw.Line(x1,y1-1,x2,y2-1,outline);
        H::Draw.Line(x1,y1+1,x2,y2+1,outline);
        H::Draw.Line(x1,y1,x2,y2,color);
    };
    if (Vars::AimModes::ProjectileTrajectory.Value)
        for (size_t i=1;i<guide.path.size();++i)
        {
            Vec3 a,b;
            if (SDK::W2S(guide.path[i-1],a)&&SDK::W2S(guide.path[i],b))
                line(int(a.x),int(a.y),int(b.x),int(b.y),{255,201,112,180});
        }
    Vec3 point;
    if (Vars::AimModes::ProjectileImpact.Value && guide.impactValid && SDK::W2S(guide.impact,point))
    {
        const int x=int(point.x),y=int(point.y),r=std::max(5,int(7*scale));
        line(x,y-r,x+r,y,gold);line(x+r,y,x,y+r,gold);
        line(x,y+r,x-r,y,gold);line(x-r,y,x,y-r,gold);
    }
    if (!Vars::AimModes::ProjectileMarker.Value || !guide.valid || !SDK::W2S(guide.marker,point)) return;
    const Color_t color=Vars::AimModes::ProjectileRingColour.Value;
    const int x=int(point.x),y=int(point.y);
    const float r=ProjectileGuidePolicy::Radius(Vars::AimModes::ProjectileRingSize.Value,H::Draw.m_nScreenH);
    // Dark inner and outer edges keep the ring legible over sky and dark interiors.
    for(int band=-2;band<=2;++band) H::Draw.LineCircle(x,y,r+band,64,outline);
    H::Draw.LineCircle(x,y,r,64,color);H::Draw.LineCircle(x,y,r-1,64,color);
    H::Draw.FillRect(x-2,y-2,5,5,outline);H::Draw.FillRect(x-1,y-1,3,3,color);
    if(guide.uncertain)
        for(int n=0;n<12;++n)
        {
            const float a=n*6.2831853f/12.f,b=a+.15f,outer=r+4*scale;
            line(int(x+std::cos(a)*outer),int(y+std::sin(a)*outer),int(x+std::cos(b)*outer),int(y+std::sin(b)*outer),color);
        }
    if(guide.aligned)
    {
        const int inner=int(r+7*scale),outer=int(r+12*scale);
        line(x-inner,y,x-outer,y,color);line(x+inner,y,x+outer,y,color);
        line(x,y-inner,x,y-outer,color);line(x,y+inner,x,y+outer,color);
    }
    Vec3 cue;
    if(SDK::W2S(guide.targetCue,cue))
    {
        const int cx=int(cue.x),cy=int(cue.y),w=std::max(4,int(5*scale));
        line(cx-w,cy-w,cx,cy,color);line(cx,cy,cx+w,cy-w,color);
    }
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
		else if (pWeapon->GetWeaponID()==TF_WEAPON_FLAREGUN
                && pWeapon->As<CTFFlareGun>()->GetFlareGunType()==FLAREGUN_DETONATE
                && (Vars::Aimbot::Projectile::Modifiers.Value&Vars::Aimbot::Projectile::ModifiersEnum::AirSplash)
                && (Vars::Aimbot::Projectile::AutoDetonate.Value&Vars::Aimbot::Projectile::AutoDetonateEnum::Flares))
		{
			// Detonator bursts are triggered by auto detonate: plan inside its radius.
			flRadius = TF_FLARE_DET_RADIUS;
			flScale = std::min(flScale, Vars::Aimbot::Projectile::AutodetRadius.Value / 100.f);
		}
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

static float PhysicalStickyArmTime(CTFWeaponBase* weapon)
{
    if(!weapon||weapon->GetWeaponID()!=TF_WEAPON_PIPEBOMBLAUNCHER)return 0.f;
    static auto live=H::ConVars.FindVar("tf_grenadelauncher_livetime");
    const float value=live?SDK::AttribHookValue(live->GetFloat(),"sticky_arm_time",weapon):std::numeric_limits<float>::infinity();
    // A missing/invalid physical arm time cannot validate an air detonation.
    return std::isfinite(value)&&value>=0.f?value:std::numeric_limits<float>::infinity();
}
static inline float ArmTime(CTFWeaponBase* pWeapon)
{
    // A sticky can't be detonated before it arms (tf_grenadelauncher_livetime +
    // sticky_arm_time), so sticky placement is always planned around arming.
    const float value=PhysicalStickyArmTime(pWeapon);
    return std::isfinite(value)?value:0.f;
}

// High arcs only make sense for slow, heavy lobs. Arrows and flares have so
// little gravity that a high arc takes seconds, far past Max simulation time.
static bool LobWeapon(CTFWeaponBase* weapon)
{
    if(!weapon) return false;
    switch(weapon->GetWeaponID())
    {
    case TF_WEAPON_GRENADELAUNCHER: case TF_WEAPON_PIPEBOMBLAUNCHER: case TF_WEAPON_CANNON:
    case TF_WEAPON_JAR: case TF_WEAPON_JAR_MILK: case TF_WEAPON_JAR_GAS: case TF_WEAPON_CLEAVER:
    case TF_WEAPON_BAT_WOOD: case TF_WEAPON_BAT_GIFTWRAP: return true;
    }
    return false;
}
static inline bool ShouldLob(Info_t& tInfo)
{
	return !tInfo.m_bLegitGuide && LobWeapon(tInfo.m_pWeapon)
        && ArcPolicy::Eligible(bool(Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::LobAngles),tInfo.m_flGravity);
}

static inline bool ShouldLob(MoveStorage& tMoveStorage, Info_t& tInfo)
{
	return ShouldLob(tInfo); // Eligibility is independent of splash radius and underprediction.
}

static bool Detonator(CTFWeaponBase* weapon)
{return weapon&&weapon->GetWeaponID()==TF_WEAPON_FLAREGUN&&weapon->As<CTFFlareGun>()->GetFlareGunType()==FLAREGUN_DETONATE;}
static bool AutoDetonates(bool sticky,bool detonator)
{
    const int mode=Vars::Aimbot::Projectile::AutoDetonate.Value;
    return sticky?bool(mode&Vars::Aimbot::Projectile::AutoDetonateEnum::Stickies)
        :detonator&&bool(mode&Vars::Aimbot::Projectile::AutoDetonateEnum::Flares);
}
static inline bool AirSplash(CTFWeaponBase* pWeapon, Info_t& tInfo)
{
    // A mid-air burst has to be timed by auto detonate for that projectile type.
    const bool sticky=pWeapon&&pWeapon->GetWeaponID()==TF_WEAPON_PIPEBOMBLAUNCHER,detonator=Detonator(pWeapon);
    return ProjectilePointPolicy::Air(bool(Vars::Aimbot::Projectile::Modifiers.Value&Vars::Aimbot::Projectile::ModifiersEnum::AirSplash),
        tInfo.m_pProjectile!=nullptr,sticky&&AutoDetonates(true,false),detonator&&AutoDetonates(false,true));
}
// Earliest air burst. Stickies: their physical arm time. Detonator: auto detonate
// can only act on a flare it has received, i.e. one round trip plus a command.
static float AirArmTime(CTFWeaponBase* weapon)
{
    if(Detonator(weapon))
    {
        const float rtt=F::Backtrack.GetReal(MAX_FLOWS,false);
        return std::isfinite(rtt)&&rtt>=0.f?rtt+2.f*TICK_INTERVAL:std::numeric_limits<float>::infinity();
    }
    return PhysicalStickyArmTime(weapon);
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
static bool FeetFallbackWeapon(CTFWeaponBase* weapon)
{
    if(!weapon) return false;
    switch(weapon->GetWeaponID())
    {
    case TF_WEAPON_ROCKETLAUNCHER:
    case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
    case TF_WEAPON_PARTICLE_CANNON:
    case TF_WEAPON_COMPOUND_BOW: return true;
    }
    return false;
}
static bool DirectHitProjectile(const Info_t& info)
{
    return info.m_pWeapon && (info.m_pWeapon->GetWeaponID()==TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT
        || info.m_pWeapon->m_iItemDefinitionIndex()==Soldier_m_TheDirectHit);
}
static thread_local int learnedSplashMode=1;
static int EffectiveSplash(const Info_t& info)
{
    if(info.m_bLegitGuide)
    {
        const bool airborne=info.m_pTarget && info.m_pTarget->m_pEntity->IsPlayer()
            && !info.m_pTarget->m_pEntity->As<CTFPlayer>()->IsOnGround();
        return ProjectileAimPolicy::Splash(ProjectileGuidePolicy::Splash(true,airborne,0),DirectHitProjectile(info),
            bool(Vars::Aimbot::Projectile::Modifiers.Value&Vars::Aimbot::Projectile::ModifiersEnum::AllowDirectHitSplash));
    }
    int mode=Vars::Aimbot::Projectile::SplashPrediction.Value;
    if(mode==Vars::Aimbot::Projectile::SplashPredictionEnum::Learned)mode=learnedSplashMode;
    if (mode!=Vars::Aimbot::Projectile::SplashPredictionEnum::Only && Vars::Aimbot::Projectile::PrioritizeUbered.Value && info.m_pTarget && info.m_pTarget->m_pEntity->IsPlayer()
        && F::AimbotGlobal.IsUberTarget(info.m_pTarget->m_pEntity->As<CTFPlayer>()) && F::AimbotGlobal.CanDisplaceUber(info.m_pWeapon))
        mode=Vars::Aimbot::Projectile::SplashPredictionEnum::Prefer;
    return ProjectileAimPolicy::Splash(mode,DirectHitProjectile(info),
        bool(Vars::Aimbot::Projectile::Modifiers.Value&Vars::Aimbot::Projectile::ModifiersEnum::AllowDirectHitSplash));
}
static_assert(ProjectileAimPolicy::TrueFeet==Vars::Aimbot::Projectile::HitboxesEnum::TruePrioritizeFeet);
static_assert(ProjectileAimPolicy::SplashInclude==Vars::Aimbot::Projectile::SplashPredictionEnum::Include);

static bool ArrowWeapon(CTFWeaponBase* weapon)
{
    if(!weapon) return false;
    switch(weapon->GetWeaponID())
    {
    case TF_WEAPON_COMPOUND_BOW: case TF_WEAPON_CROSSBOW: case TF_WEAPON_SHOTGUN_BUILDING_RESCUE: return true;
    }
    return false;
}
static inline int GetHitboxPriority(int nHitbox, Target_t& tTarget, Info_t& tInfo, CBaseEntity* pProjectile = nullptr)
{
    int configured=ProjectileGuidePolicy::Hitboxes(tInfo.m_bLegitGuide,Vars::Aimbot::Projectile::Hitboxes.Value);
    // Arrows have no splash and a feet point tends to clip the ground: feet
    // preferences never apply to them. Body first, head as the fallback, unless
    // the Huntsman is going for the head (then the head logic below is kept).
    const bool arrow=!pProjectile&&ArrowWeapon(tInfo.m_pWeapon);
    if(arrow) configured&=~(Vars::Aimbot::Projectile::HitboxesEnum::TruePrioritizeFeet|Vars::Aimbot::Projectile::HitboxesEnum::PrioritizeFeet);
    const bool player=tTarget.m_iTargetType==TargetEnum::Player;
    const bool bodyWeapon=BodyProjectile(tInfo.m_pWeapon)||arrow;
    const int bodyParts=Vars::Aimbot::Projectile::HitboxesEnum::Head|Vars::Aimbot::Projectile::HitboxesEnum::Body|Vars::Aimbot::Projectile::HitboxesEnum::Feet;
    // A manual head-only Huntsman selection stays head-only (no body fallback).
    const bool manualHeadOnly=!(configured&Vars::Aimbot::Projectile::HitboxesEnum::Auto) && player && !pProjectile
        && tInfo.m_pWeapon->GetWeaponID()==TF_WEAPON_COMPOUND_BOW && (configured&bodyParts)==Vars::Aimbot::Projectile::HitboxesEnum::Head;
    bool headPlan=manualHeadOnly;
    const bool trueFeet=player && (configured&ProjectileAimPolicy::TrueFeet);
	if (!F::AimbotGlobal.IsHitboxValid(nHitbox, ProjectileAimPolicy::Hitboxes(configured,bodyWeapon&&!manualHeadOnly,player)))
		return -1;

	int iHeadPriority = 0;
	int iBodyPriority = 1;
	int iFeetPriority = 2;

	if (configured & Vars::Aimbot::Projectile::HitboxesEnum::Auto)
	{
		bool bHeadshot = !trueFeet && (configured & Vars::Aimbot::Projectile::HitboxesEnum::Head)
			&& tTarget.m_iTargetType == TargetEnum::Player;
		if (bHeadshot)
		{
			if (!pProjectile)
				bHeadshot = tInfo.m_pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW;
			else
				bHeadshot = pProjectile->GetClassID() == ETFClassID::CTFProjectile_Arrow && pProjectile->As<CTFProjectile_Arrow>()->CanHeadshot();

			if (configured & Vars::Aimbot::Projectile::HitboxesEnum::BodyaimIfLethal
				&& bHeadshot && !pProjectile && tInfo.m_pWeapon->m_hOwner().GetEntryIndex() == I::EngineClient->GetLocalPlayer())
			{
                auto target=tTarget.m_pEntity->As<CTFPlayer>();
                const float maxCharge=BowCharge::MaximumTime(tInfo.m_pWeapon,tInfo.m_pLocal);
                const float fraction=BowChargePlan::Fraction(BowChargePolicy::Fraction(I::GlobalVars->curtime,
                    tInfo.m_pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime(),maxCharge));
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
        headPlan=bHeadshot;

		bool bLower = configured & Vars::Aimbot::Projectile::HitboxesEnum::PrioritizeFeet
			&& tTarget.m_iTargetType == TargetEnum::Player && tTarget.m_pEntity->As<CTFPlayer>()->IsOnGround() /*&& tInfo.m_flRadius*/;

		iHeadPriority = bHeadshot ? 0 : 2;
		iBodyPriority = bHeadshot ? -1 : bLower ? 1 : 0;
		iFeetPriority = bHeadshot ? -1 : bLower ? 0 : 1;
	}

    // A manual head-only bow selection needs the same head geometry as Auto.
    // Mixed manual point sets keep their existing per-bounds behavior.
    if (manualHeadOnly)
        tTarget.m_nAimedHitbox=HITBOX_HEAD;
    // A Huntsman head plan keeps its own priorities (head only).
    const bool bodyPriority=bodyWeapon&&!(arrow&&headPlan);
    iHeadPriority=ProjectileAimPolicy::Priority(BOUNDS_HEAD,iHeadPriority,configured,bodyPriority,player);
    iBodyPriority=ProjectileAimPolicy::Priority(BOUNDS_BODY,iBodyPriority,configured,bodyPriority,player);
    iFeetPriority=ProjectileAimPolicy::Priority(BOUNDS_FEET,iFeetPriority,configured,bodyPriority,player);

	switch (nHitbox)
	{
	case BOUNDS_HEAD: return iHeadPriority;
	case BOUNDS_BODY: return iBodyPriority;
	case BOUNDS_FEET: return iFeetPriority;
	}

	return -1;
};

static TickRecord* ProjectileHeadRecord(CBaseEntity* entity)
{
    std::vector<TickRecord*> records;
    if (!F::Backtrack.GetRecords(entity,records) || records.empty() || !records.front()
        || records.front()->m_bInvalid || !std::isfinite(records.front()->m_flSimTime)) return nullptr;
    auto record=records.front();
    const auto& origin=record->m_vOrigin;
    return std::isfinite(origin.x) && std::isfinite(origin.y) && std::isfinite(origin.z)?record:nullptr;
}

Directs_t CAimbotProjectile::GetDirects()
{
	Directs_t mDirects = {};
    if(m_tInfo.m_bLegitGuide)
    {
        mDirects[0]={m_vGuideOffset,PointFlagsEnum::Regular};
        return mDirects;
    }
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
    const bool grenadeFeet=!m_tInfo.m_bLegitGuide && !m_tInfo.m_pProjectile && m_tInfo.m_pWeapon
        && m_tInfo.m_pWeapon->GetWeaponID()==TF_WEAPON_GRENADELAUNCHER
        && tTarget.m_iTargetType==TargetEnum::Player
        && (Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::PrioritizeFeet)
        && !(Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::TruePrioritizeFeet);
    int feetPriority=-1;
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
				auto pose = ProjectileHeadRecord(tTarget.m_pEntity);
				if (!pose)
					break;
				auto aBones = pose->m_aBones;

				//Vec3 vOff = tTarget.m_pEntity->As<CBaseAnimating>()->GetHitboxOrigin(aBones, HITBOX_HEAD) - tTarget.m_pEntity->m_vecOrigin();

				// https://www.youtube.com/watch?v=_PSGD-pJUrM, might be better??
				Vec3 vCenter, vBBoxMins, vBBoxMaxs; tTarget.m_pEntity->As<CBaseAnimating>()->GetHitboxInfo(aBones, HITBOX_HEAD, &vCenter, &vBBoxMins, &vBBoxMaxs);
				Vec3 vOff = vCenter + (vBBoxMins + vBBoxMaxs) / 2 - pose->m_vOrigin;

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
            feetPriority=iPriority;
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

    // Preserve every original destination; add one lower-body direct proposal only.
    if(feetPriority>=0 && !m_tInfo.m_pProjectile && tTarget.m_iTargetType==TargetEnum::Player
        && FeetFallbackWeapon(m_tInfo.m_pWeapon) && tTarget.m_nAimedHitbox!=HITBOX_HEAD
        && !(Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::TruePrioritizeFeet))
    {
        const auto primary=mDirects.at(feetPriority);
        const auto height=FeetPointPolicy::Height({vMins.x,vMins.y,vMins.z},{vMaxs.x,vMaxs.y,vMaxs.z},
            {m_tInfo.m_vHull.x,m_tInfo.m_vHull.y,m_tInfo.m_vHull.z},primary.m_vOffset.z);
        if(height)
        {
            Directs_t expanded;
            for(const auto& [priority,point]:mDirects) expanded[priority*2]=point;
            expanded[feetPriority*2+1]={Vec3(0,0,*height),primary.m_iFlags,true};
            mDirects=std::move(expanded);
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
        Vec3 xBasis, yBasis, zBasis;
        Rotate(float x,float y)
        {
            // Identical arithmetic to RotatePoint with a zero origin/roll,
            // but the six trig calls and basis construction happen once per sphere.
            float sp,sy,sr,cp,cy,cr;
            Math::SinCos(Math::Deg2Rad(x),sp,cp);
            Math::SinCos(Math::Deg2Rad(y),sy,cy);
            Math::SinCos(0.f,sr,cr);
            xBasis={cy*cp,cy*sp*sr-sy*cr,cy*sp*cr+sy*sr};
            yBasis={sy*cp,sy*sp*sr+cy*cr,sy*sp*cr-cy*sr};
            zBasis={-sp,cp*sr,cp*cr};
        }
        Vec3 operator()(const Vec3& point) const
        {
            const Vec3 offset=point-Vec3{};
            return Vec3(xBasis.Dot(offset),yBasis.Dot(offset),zBasis.Dot(offset))+Vec3{};
        }
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
		const TracePreparation::Rotate rotate(flRotateX,flRotateY);
		for (int n = 0; n < iSamples; n++)
		{
			if (!SearchAllowed(SearchWork::Sampling)) break;
			Vec3 vPoint = direction(n) * flRadius;
			vPoint = rotate(vPoint);

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

namespace SplashWorker
{
    // Bumped on level shutdown: candidate pools and prepared sphere patterns from
    // the previous map are never reused (entity handles get recycled).
    static std::atomic<uint64_t> epoch{0};
    void Start() { TracePreparation::pipeline.Start(); }
    void Stop() { TracePreparation::pipeline.Stop(); }
    void Invalidate() { epoch.fetch_add(1,std::memory_order_relaxed); TracePreparation::pipeline.Invalidate(); }
}

// Explosion-to-eye test for a lifted surface point, the same geometry TestAngle
// confirms later for the real impact (trace from endpos + normal * offset).
static bool SplashPointSeesEye(const Vec3& vPoint, const Vec3& vNormal, const Vec3& vTargetEye, const Info_t& tInfo, CGameTrace& trace, ITraceFilter& filter)
{
    if (I::EngineTrace->GetPointContents(vPoint) & MASK_SOLID) return false;
    const int iMask = F::ProjSim.m_bPhysics ? MASK_SHOT | CONTENTS_DISPSOLID : MASK_SHOT;
    SDK::Trace(vPoint + vNormal * tInfo.m_flNormalOffset, vTargetEye, iMask, &filter, &trace);
    if (ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.setupTraces;
    return trace.fraction == 1.f;
}

// ---------------------------------------------------------------------------
// Face sampler (Amalgam's, verbatim): random jittered samples per face by density,
// closest point / edge / interior, line of sight started one unit toward the eye
// for edge samples. Regenerated every command. 0.7.0.44: this is the only face
// sampler; the 0.7.0.37-.41 deterministic sampler, reachability prefilter and
// cross-command pool reuse are gone (they did not find the shots this one finds).
// ---------------------------------------------------------------------------
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


// 0.7.0.45 P1: TestAngle's straight pretrace, run at generation time so the shortlist
// (nearest SplashRestrict candidates) is not filled with points the rocket cannot
// reach (floor and walls on the target's side of the cover). Own straight
// projectiles only; muzzle from the view-rotated launch offset, lenient fraction
// (the real pretrace from the exact muzzle still decides).
static bool SplashPointReachable(const Vec3& vPoint, const Info_t& tInfo, CGameTrace& trace, ITraceFilter& filter)
{
    if (tInfo.m_flGravity || tInfo.m_pProjectile || F::ProjSim.m_bPhysics) return true;
    Vec3 vForward, vRight, vUp; Math::AngleVectors(Math::CalcAngle(tInfo.m_vLocalEye, vPoint), &vForward, &vRight, &vUp);
    const Vec3 vMuzzle = tInfo.m_vLocalEye + vForward * tInfo.m_vOffset.x + vRight * tInfo.m_vOffset.y + vUp * tInfo.m_vOffset.z;
    SDK::TraceHull(vMuzzle, vPoint, -tInfo.m_vHull, tInfo.m_vHull, MASK_SOLID, &filter, &trace);
    if (ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.setupTraces;
    const bool bReachable = Math::FullFraction(vMuzzle, vPoint, trace) >= 0.99f;
    if (!bReachable && ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.unreachable;
    return bReachable;
}

static void CheckFaceSample(const FaceSample& sample, SplashPool_t& pool,
    const Vec3& targetEye, const Vec3& targetCenter, float radius, Info_t& info,
    CGameTrace& trace, ITraceFilter& filter, uint8_t source = SplashSearchPolicy::SourceFace)
{
    const Vec3 point=sample.point;
    if(point.DistToSqr(targetCenter)>radius*radius) return;
    if(sample.inside && (I::EngineTrace->GetPointContents(point)&MASK_SOLID)) return;
    Vec3 normal=sample.normal;
    int mask=F::ProjSim.m_bPhysics ? MASK_SHOT|CONTENTS_DISPSOLID : MASK_SHOT;
    if(!sample.inside) { normal=(targetEye-point).Normalized(); mask&=~CONTENTS_MOVEABLE; }
    // No contents guard on this start on purpose (0.7.0.48): an edge sample whose start lands
    // inside the occluder traces through it and becomes a candidate that the real validation
    // then rejects as "explosion blocked". That near miss is what the refinement (N1) works
    // from; .47 guarded it and the search went blind (face_points=0, nothing to refine).
    SDK::Trace(point+normal*info.m_flNormalOffset,targetEye,mask,&filter,&trace);
    if(ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.setupTraces;
    if(trace.fraction!=1.f) return;
    if(!SplashPointReachable(point, info, trace, filter)) return;
    if(pool.Add(point, PointTypeEnum::Geometry, source) && ProjectileDiagnostics::current)
    {
        if(source==SplashSearchPolicy::SourceEdge) ++ProjectileDiagnostics::splash.edgePoints; else ++ProjectileDiagnostics::splash.facePoints;
        ProjectileDiagnostics::SplashCandidate(source, point, sample.normal);
    }
}

void CAimbotProjectile::RecordSplashResult(SplashPool_t& tPool, int iSetupId, bool bPassed, int iCommand)
{
    auto* pSetup = tPool.Find(iSetupId);
    if (!pSetup) return;
    ++pSetup->m_iAttempts;
    (bPassed ? pSetup->m_iLastPass : pSetup->m_iLastFail) = iCommand;
}

// Finds the candidate pool for the current target and weapon and regenerates it.
SplashPool_t& CAimbotProjectile::AcquireSplashPool(int searchMode, uint8_t iFlags, Vec3& vGeneratedAt, int iCommand)
{
    const uint64_t iEpoch = SplashWorker::epoch.load(std::memory_order_relaxed);
    if (iEpoch != m_iSplashPoolEpoch)
    {
        for (auto& tPool : m_SplashPools) tPool = {};
        m_iSplashPoolEpoch = iEpoch;
    }
    const int iTarget = m_tInfo.m_pTarget->m_pEntity->GetRefEHandle().ToInt();
    const int iWeapon = m_tInfo.m_pProjectile ? m_tInfo.m_pProjectile->GetRefEHandle().ToInt() : m_tInfo.m_pWeapon->GetRefEHandle().ToInt();
    // Launch side (viewmodel flip trials change the muzzle offset) is part of the key:
    // a trial for the other side must not write its pass/fail history into ours.
    // (0.7.0.38: taken from the trial override itself; m_vOffset is world-space and
    // its sign changes with the view angle, which regenerated the pool every command.)
    const int8_t iSide = ProjectileMuzzlePolicy::flipOverride.has_value() ? (*ProjectileMuzzlePolicy::flipOverride ? 1 : -1) : 0;
    SplashPool_t* pPool = nullptr, * pVictim = &m_SplashPools[0];
    for (auto& tPool : m_SplashPools)
    {
        if (tPool.m_iGeneratedCommand >= 0 && tPool.m_iTarget == iTarget && tPool.m_iWeapon == iWeapon && tPool.m_iMode == searchMode && tPool.m_iFlags == iFlags && tPool.m_iSide == iSide) { pPool = &tPool; break; }
        if (tPool.m_iLastCommand < pVictim->m_iLastCommand) pVictim = &tPool;
    }
    if (!pPool)
    {
        pPool = pVictim; *pPool = {};
        pPool->m_iTarget = iTarget; pPool->m_iWeapon = iWeapon; pPool->m_iMode = searchMode; pPool->m_iFlags = iFlags; pPool->m_iSide = iSide;
    }
    // Regenerated every command (Amalgam's behaviour). Candidates that validated
    // recently survive the regeneration: they are the first thing the new pool
    // tries, which keeps the aim steady across commands.
    std::vector<Setup_t> vSeeds;
    for (const auto& tSetup : pPool->m_vSetups)
        if (tSetup.m_iLastPass >= 0 && tSetup.m_iLastPass >= tSetup.m_iLastFail && iCommand - tSetup.m_iLastPass <= SplashSearchPolicy::PassMemoryCommands)
            vSeeds.push_back(tSetup);
    SetupSplashPoints(vGeneratedAt, *pPool, iFlags, searchMode);
    for (auto& tSeed : vSeeds)
    {
        bool bDuplicate = false;
        for (auto& tSetup : pPool->m_vSetups)
            if (tSetup.m_iType == tSeed.m_iType && tSetup.m_vPoint.DistToSqr(tSeed.m_vPoint) < 0.25f)
            {
                tSetup.m_iAttempts = tSeed.m_iAttempts; tSetup.m_iLastPass = tSeed.m_iLastPass; tSetup.m_iLastFail = tSeed.m_iLastFail;
                bDuplicate = true; break;
            }
        if (bDuplicate) continue;
        tSeed.m_iId = pPool->m_iNextId++;
        pPool->m_vSetups.push_back(tSeed);
    }
    pPool->m_iGeneratedCommand = iCommand;
    pPool->m_vGeneratedAt = vGeneratedAt;
    pPool->m_vLocalEye = m_tInfo.m_vLocalEye;
    pPool->m_flRadius = m_tInfo.m_flRadius;
    if (ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.poolRegenerated;
    pPool->m_iLastCommand = iCommand;
    return *pPool;
}

void CAimbotProjectile::SetupSplashPoints(Vec3& vOrigin, SplashPool_t& tPool, uint8_t iFlags, int searchMode)
{
    ProjectileDiagnostics::Profile profile(ProjectileDiagnostics::SplashSetup);
    tPool.m_vSetups.clear();
    tPool.m_iNextId = 0;

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};

	m_tInfo.m_pTarget->m_vPos = vOrigin;
	Vec3 vTargetEye = vOrigin + m_tInfo.m_vTargetEye;
	Vec3 vTargetCenter = vOrigin + m_tInfo.m_pTarget->m_pEntity->GetOffset() / 2;
	float flRadius = m_tInfo.m_flRadius + m_tInfo.m_pTarget->m_pEntity->GetSize().Length() / 2;
	bool bAirSplash = AirSplash(m_tInfo.m_pWeapon, m_tInfo);
    const bool surfaceSplash=ProjectilePointPolicy::Surface(!m_tInfo.m_pProjectile&&Detonator(m_tInfo.m_pWeapon));
    bool airCenterEmitted=false;
    const bool bWantTrace = SplashSearchPolicy::WantsTrace(searchMode);
    const bool bWantFace = SplashSearchPolicy::WantsFace(searchMode);

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

	// Trace: rays outward from the target hit surfaces that see it by construction.
	// Face-only mode keeps just the straight-down ray (and air samples).
	int iPoints = !bWantTrace && !bAirSplash ? 0
		: !m_tInfo.m_flGravity ? Vars::Aimbot::Projectile::SplashPointsDirect.Value : Vars::Aimbot::Projectile::SplashPointsArc.Value;
	{
        ProjectileDiagnostics::Profile sampling(ProjectileDiagnostics::SplashSampling);
		auto vPoints = ComputePoints(flRadius, iPoints);
        std::vector<Setup_t> vTracePoints;

		for (int i = 0; i < vPoints.size(); i++)
		{
			if (!SearchStep(SearchWork::Sampling)) break;
			auto fCheckPointTrace = [&]()
			{
				if (!surfaceSplash || !trace.m_pEnt || trace.fraction == 1.f || trace.surface.flags & SURF_SKY || !trace.m_pEnt->GetAbsVelocity().IsZero())
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
                    ProjectilePointPolicy::AirSamples(Vars::Aimbot::Projectile::SplashAirCount.Value,airCenterEmitted,
                        []{return SearchStep(SearchWork::Sampling);},
                        [&](float fraction){vTracePoints.emplace_back(vPoints[i]*fraction+vTargetCenter,PointTypeEnum::Air);},
                        []{return SDK::StdRandomFloat();});
				}
				return !bWantTrace && i || !trace.DidHit();
			};

			Vec3 vPoint = vPoints[i] + vTargetCenter;

			Solution_t tSolution; CalculateAngle(m_tInfo.m_vLocalEye, vPoint, 0, tSolution, iFlags);
			if (tSolution.m_iCalculated == CalculateResultEnum::Bad)
				continue;

			HandleTrace(vPoint, vTracePoints, vTargetEye, m_tInfo, fCheckPointTrace, fCheckPointAir, trace, filter);
		}
        for (const auto& tSetup : vTracePoints)
        {
            if (tSetup.m_iType != PointTypeEnum::Air && !SplashPointReachable(tSetup.m_vPoint, m_tInfo, trace, filter)) continue;
            if (tPool.Add(tSetup.m_vPoint, tSetup.m_iType, tSetup.m_iType == PointTypeEnum::Air ? SplashSearchPolicy::SourceAir : SplashSearchPolicy::SourceTrace))
                ProjectileDiagnostics::SplashCandidate(tSetup.m_iType == PointTypeEnum::Air ? SplashSearchPolicy::SourceAir : SplashSearchPolicy::SourceTrace, tSetup.m_vPoint, Vec3());
        }

        // Shooter rays (straight-flying projectiles): the same sphere of directions,
        // but traced from the muzzle toward each point around the target. Whatever
        // the rocket would hit first is reachable by construction; it is kept when
        // the explosion there sees the target's eye. This is what finds the floor
        // through a doorway or the surface just past a corner.
        if (bWantTrace && surfaceSplash && !m_tInfo.m_flGravity && !m_tInfo.m_pProjectile)
        {
            const float flRadiusSqr = flRadius * flRadius;
            for (size_t i = 0; i < vPoints.size(); ++i)
            {
                if (!SearchStep(SearchWork::Sampling)) break;
                const Vec3 vAim = vPoints[i] + vTargetCenter;
                Vec3 vForward, vRight, vUp; Math::AngleVectors(Math::CalcAngle(m_tInfo.m_vLocalEye, vAim), &vForward, &vRight, &vUp);
                const Vec3 vMuzzle = m_tInfo.m_vLocalEye + vForward * m_tInfo.m_vOffset.x + vRight * m_tInfo.m_vOffset.y + vUp * m_tInfo.m_vOffset.z;
                // Continue past the sphere point: the surface may lie just beyond it.
                const Vec3 vEnd = vMuzzle + (vAim - vMuzzle).Normalized() * (vMuzzle.DistTo(vAim) + flRadius);
                SDK::TraceHull(vMuzzle, vEnd, -m_tInfo.m_vHull, m_tInfo.m_vHull, MASK_SOLID, &filter, &trace);
                if (ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.setupTraces;
                if (!trace.DidHit() || trace.startsolid || trace.allsolid || !trace.m_pEnt || trace.surface.flags & SURF_SKY || !trace.m_pEnt->GetAbsVelocity().IsZero())
                    continue;
                if (trace.endpos.DistToSqr(vTargetCenter) > flRadiusSqr) continue;
                const Vec3 vHit = trace.endpos, vNormal = trace.plane.normal;
                if (!SearchStep(SearchWork::Sampling)) break;
                if (!SplashPointSeesEye(vHit, vNormal, vTargetEye, m_tInfo, trace, filter)) continue;
                if (tPool.Add(vHit, PointTypeEnum::Geometry, SplashSearchPolicy::SourceShooterRay))
                    ProjectileDiagnostics::SplashCandidate(SplashSearchPolicy::SourceShooterRay, vHit, vNormal);
            }
        }
	}

	// Face: every surface inside the blast sphere that faces the shooter, sampled now.
	if (surfaceSplash && bWantFace && SearchAllowed(SearchWork::Sampling))
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

        ProjectileDiagnostics::Profile sampling(ProjectileDiagnostics::SplashSampling);
        const float flDensity = !m_tInfo.m_flGravity ? Vars::Aimbot::Projectile::SplashDensityDirect.Value : Vars::Aimbot::Projectile::SplashDensityArc.Value;
        const float flCutoff = Vars::Aimbot::Projectile::SplashSamplesCutoff.Value * powf(float(vFaces.size()), 2);
        FaceRandom random(uint32_t(SDK::StdRandomInt(0, INT_MAX)));
        float total = random.Float();
        for (auto& tFace : vFaces)
            SampleFace(tFace, flDensity, flRadius, flCutoff, vTargetEye, vTargetCenter, vOrigin, m_tInfo.m_vHull,
                random, total, [](SearchWork) { return true; },
                [&](const FaceSample& sample) { CheckFaceSample(sample, tPool, vTargetEye, vTargetCenter, flRadius, m_tInfo, trace, filter); });

        // 0.7.0.46 C1: corner and edge search with the real terminal test.
        // What the .45 log showed: the pixelshot here is a brush junction (the wall's end
        // face x=-116, the wall face y=-1152 and a horizontal face at z=384 meet), a
        // sub-unit feature. Whether the explosion sees the eye depends on WHICH of those
        // faces the rocket hits (its normal sets the explosion point), so a sample is
        // judged with TestAngle's own test (point + face normal * offset -> eye) for every
        // face meeting there, and kept if any normal passes. The random sampler's edge
        // rule (sight started toward the eye) produced ~36 false candidates per command
        // (wall base, wall top) that then failed validation; this produces none of those.
        // Corners of occluder faces first (nearest to the eye), then their edges, within
        // one trace budget (SplashEdgeSamples). Own straight projectiles only.
        if (!m_tInfo.m_flGravity && !m_tInfo.m_pProjectile)
        {
            const float flRadiusSqr = flRadius * flRadius;
            const int iCap = std::max(16, Vars::Aimbot::Projectile::SplashEdgeSamples.Value);
            int iTraces = 0, iSamples = 0;
            // Vertex -> faces sharing it (all brush/prop faces, 1/8 u grid).
            auto fKey = [](const Vec3& v) { return std::tuple<int, int, int>(int(std::lround(v.x * 8.f)), int(std::lround(v.y * 8.f)), int(std::lround(v.z * 8.f))); };
            std::map<std::tuple<int, int, int>, std::vector<size_t>> mVertexFaces;
            std::vector<uint8_t> vOccluder(vFaces.size(), 0);
            for (size_t f = 0; f < vFaces.size(); ++f)
            {
                const auto& tFace = vFaces[f];
                if (tFace.m_vVertices.size() < 3 || tFace.m_iType == FaceTypeEnum::Displacement) continue;
                vOccluder[f] = tFace.m_vNormal.Dot(vTargetEye - tFace.m_vVertices[0]) < 0.f;
                for (const auto& v : tFace.m_vVertices) mVertexFaces[fKey(v)].push_back(f);
            }
            auto fNormalsAt = [&](const Vec3& v, size_t iOwn, std::vector<Vec3>& vNormals)
            {
                vNormals.clear(); vNormals.push_back(vFaces[iOwn].m_vNormal);
                auto it = mVertexFaces.find(fKey(v));
                if (it == mVertexFaces.end()) return;
                for (size_t f : it->second)
                {
                    const Vec3& n = vFaces[f].m_vNormal; bool bNew = true;
                    for (const auto& k : vNormals) if (k.Dot(n) > 0.999f) { bNew = false; break; }
                    if (bNew) vNormals.push_back(n);
                }
            };
            // The terminal test for one sample point with each candidate impact normal.
            auto fJudge = [&](const Vec3& vOn, size_t iFace, const std::vector<Vec3>& vNormals, uint8_t iSource)
            {
                const auto& tFace = vFaces[iFace];
                const float flEpsilon = random.Bool() ? CALC_EPSILON : DIST_EPSILON;
                Vec3 vPoint = vOn + Vec3(random.Float(-flEpsilon, flEpsilon), random.Float(-flEpsilon, flEpsilon), random.Float(-flEpsilon, flEpsilon));
                vPoint += tFace.m_vNormal * (m_tInfo.m_vHull + CALC_EPSILON);
                if (vPoint.DistToSqr(vTargetCenter) > flRadiusSqr) return;
                if (!std::isfinite(vPoint.x) || !std::isfinite(vPoint.y) || !std::isfinite(vPoint.z)) return;
                // .46 log: a sample a hair inside the floor at the wall's base passed, because a
                // trace that starts inside a brush ignores it and tunnelled under the wall.
                if (I::EngineTrace->GetPointContents(vPoint) & MASK_SOLID) return;
                ++iSamples;
                bool bSees = false;
                for (const auto& n : vNormals)
                {
                    if (iTraces >= iCap) return;
                    ++iTraces;
                    const Vec3 vStart = vPoint + n * m_tInfo.m_flNormalOffset;
                    if (I::EngineTrace->GetPointContents(vStart) & MASK_SOLID) continue;
                    SDK::Trace(vStart, vTargetEye, MASK_SHOT, &filter, &trace);
                    if (ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.setupTraces;
                    if (trace.fraction == 1.f) { bSees = true; break; }
                }
                if (!bSees) return;
                if (iTraces >= iCap) return;
                ++iTraces;
                if (!SplashPointReachable(vPoint, m_tInfo, trace, filter)) return;
                if (tPool.Add(vPoint, PointTypeEnum::Geometry, iSource) && ProjectileDiagnostics::current)
                {
                    ++ProjectileDiagnostics::splash.edgePoints;
                    ProjectileDiagnostics::SplashCandidate(iSource, vPoint, tFace.m_vNormal);
                }
            };
            // Corners of occluder faces inside the sphere, nearest to the eye first.
            struct Corner_t { float m_flDistanceSqr; size_t m_iFace; size_t m_iVertex; };
            std::vector<Corner_t> vCorners;
            for (size_t f = 0; f < vFaces.size(); ++f)
            {
                if (!vOccluder[f]) continue;
                const auto& vVertices = vFaces[f].m_vVertices;
                for (size_t i = 0; i < vVertices.size(); ++i)
                    if (vVertices[i].DistToSqr(vTargetCenter) <= flRadiusSqr) vCorners.push_back({ vVertices[i].DistToSqr(vTargetEye), f, i });
            }
            std::sort(vCorners.begin(), vCorners.end(), [](const Corner_t& a, const Corner_t& b) { return a.m_flDistanceSqr < b.m_flDistanceSqr; });
            std::vector<Vec3> vNormals;
            for (const auto& tCorner : vCorners)
            {
                if (iTraces >= iCap) break;
                const auto& vVertices = vFaces[tCorner.m_iFace].m_vVertices;
                const size_t n = vVertices.size(), i = tCorner.m_iVertex;
                const Vec3& v = vVertices[i];
                const Vec3 d1 = (vVertices[(i + 1) % n] - v).Normalized(), d2 = (vVertices[(i + n - 1) % n] - v).Normalized();
                fNormalsAt(v, tCorner.m_iFace, vNormals);
                // The corner itself, a hair inside; a unit along each edge; a unit into the face.
                fJudge(v + (d1 + d2) * 0.1f, tCorner.m_iFace, vNormals, SplashSearchPolicy::SourceCorner);
                fJudge(v + d1 * 1.f + d2 * 0.1f, tCorner.m_iFace, vNormals, SplashSearchPolicy::SourceCorner);
                fJudge(v + d2 * 1.f + d1 * 0.1f, tCorner.m_iFace, vNormals, SplashSearchPolicy::SourceCorner);
                fJudge(v + (d1 + d2) * 1.f, tCorner.m_iFace, vNormals, SplashSearchPolicy::SourceCorner);
            }
            const int iCornerTraces = iTraces;
            // Edges of occluder faces, clipped to the sphere, spread over what is left of the budget.
            struct EdgeSegment_t { size_t m_iFace; Vec3 m_vA, m_vB; float m_flLength; };
            std::vector<EdgeSegment_t> vSegments; float flTotalLength = 0.f;
            for (size_t f = 0; f < vFaces.size(); ++f)
            {
                if (!vOccluder[f]) continue;
                const auto& vVertices = vFaces[f].m_vVertices; const size_t n = vVertices.size();
                for (size_t i = 0; i < n; ++i)
                {
                    const Vec3& vA = vVertices[i]; const Vec3& vB = vVertices[(i + 1) % n];
                    const Vec3 vD = vB - vA, vM = vA - vTargetCenter;
                    const float a = vD.Dot(vD); if (a <= 1e-6f) continue;
                    const float b = 2.f * vM.Dot(vD), c = vM.Dot(vM) - flRadiusSqr;
                    const float flDisc = b * b - 4.f * a * c; if (flDisc < 0.f) continue;
                    const float flRoot = sqrtf(flDisc);
                    const float t0 = std::clamp((-b - flRoot) / (2.f * a), 0.f, 1.f), t1 = std::clamp((-b + flRoot) / (2.f * a), 0.f, 1.f);
                    if (t1 <= t0) continue;
                    const Vec3 vCA = vA + vD * t0, vCB = vA + vD * t1;
                    vSegments.push_back({ f, vCA, vCB, vCA.DistTo(vCB) }); flTotalLength += vSegments.back().m_flLength;
                }
            }
            // Two normals per edge sample on average; corners took their share already.
            const float flStep = std::max(4.f, flTotalLength / float(std::max(1, (iCap - iTraces) / 2)));
            for (const auto& tSegment : vSegments)
            {
                if (iTraces >= iCap) break;
                if (tSegment.m_flLength <= 0.f) continue;
                // Normals of the faces meeting at the edge's start (the edge's own face first).
                fNormalsAt(tSegment.m_vA, tSegment.m_iFace, vNormals);
                const Vec3 vDir = (tSegment.m_vB - tSegment.m_vA) / tSegment.m_flLength;
                for (float d = random.Float(0.f, flStep); d < tSegment.m_flLength && iTraces < iCap; d += flStep)
                    fJudge(tSegment.m_vA + vDir * d, tSegment.m_iFace, vNormals, SplashSearchPolicy::SourceEdge);
            }
            if (ProjectileDiagnostics::current)
            {
                ProjectileDiagnostics::splash.edgeSamples += iSamples;
                ProjectileDiagnostics::splash.cornerTraces += iCornerTraces;
                ProjectileDiagnostics::splash.corners += int(vCorners.size());
                ProjectileDiagnostics::splash.edgeStep = flStep;
            }
        }
	}

    if (ProjectileDiagnostics::current) ProjectileDiagnostics::splash.generated += int(tPool.m_vSetups.size());

#ifdef SPLASH_DEBUG3
	for (auto& tSetup : tPool.m_vSetups)
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
    const int iCommand = G::CurrentUserCmd ? G::CurrentUserCmd->command_number : 0;
    auto* entity = m_tInfo.m_pTarget->m_pEntity;
    // 0.7.0.45 S1: players are plain boxes; the nearest point is a clamp. Only
    // rotated collision (buildings) still goes through the entity.
    const bool bBox = entity->IsPlayer();
    const Vec3 vBoxMins = vOrigin + entity->m_vecMins(), vBoxMaxs = vOrigin + entity->m_vecMaxs();
    const Vec3 original = entity->GetAbsOrigin();
    if (!bBox) entity->SetAbsOrigin(vOrigin);
    for (const auto& setup : vSplashPoints)
    {
        if (!SearchAllowed(SearchWork::Selection)) break;
        Vec3 nearest;
        if (bBox)
            nearest = { std::clamp(setup.m_vPoint.x, vBoxMins.x, vBoxMaxs.x), std::clamp(setup.m_vPoint.y, vBoxMins.y, vBoxMaxs.y), std::clamp(setup.m_vPoint.z, vBoxMins.z, vBoxMaxs.z) };
        else
            entity->m_Collision()->CalcNearestPoint(setup.m_vPoint, &nearest);
        m_vSplashDistances.push_back(setup.m_vPoint.DistToSqr(nearest));
    }
    if (!bBox) entity->SetAbsOrigin(original);
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
        tPoint.m_iSetupId = it->m_iId;
        tPoint.m_iAttempts = it->m_iAttempts;
        tPoint.m_iLastFail = it->m_iLastFail;
        tPoint.m_iPriority = SplashSearchPolicy::PriorityOf(it->m_iAttempts, it->m_iLastPass, it->m_iLastFail, iCommand);
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
		else if (tPoint.m_iType == PointTypeEnum::Air && !ProjectilePointPolicy::AirReady(tPoint.m_tSolution.m_flTime,m_tInfo.m_flAirArmTime))
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
    if (m_bLegitGuide) iSimFlags |= ProjSimEnum::DiagnosticDeterministic;
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
    // Beyond the bounded local projection, future input/impulses are unknown.
    // Retain the configured damage threshold, but never report stationary safety.
    // The full possible local volume includes the blast unless proven otherwise.
    if(!usableHorizon)
        damage=std::max(damage,SelfDamage::Estimate(local,weapon,impact,impact,radius,diagnosticPriority));
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
    if(NeuralMovement::ForceEnabled()&&G::PrimaryWeaponType==EWeaponType::PROJECTILE)return false;
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
    // A candidate may be interrupted only at a safe boundary and is then rejected.
    if (!SearchStep(SearchWork::Validation)) return false;
    ProjectileDiagnostics::Add(ProjectileDiagnostics::Tests);
    m_tSplashMiss.m_bValid = false;
    if(iSimTime<=0) ProjectileDiagnostics::Add(ProjectileDiagnostics::NoTicks);
#ifdef NIKOGRAM_PRIVATE_LEARNING
    m_DiagnosticScratch.clear();m_DiagnosticScratchCount=0;
    m_DiagnosticRecording=!bSecondTest&&PrivateLearning::EngineReplayWanted(&m_tMoveStorage);
#endif
	auto pLocal = m_tInfo.m_pLocal;
	auto pWeapon = m_tInfo.m_pWeapon;
	auto& tTarget = *m_tInfo.m_pTarget;

	int iSimFlags = ProjSimEnum::Redirect | ProjSimEnum::InitCheck | ProjSimEnum::PredictCmdNum | (Vars::Aimbot::General::NoSpread.Value ? ProjSimEnum::CorrectRandomAngles : ProjSimEnum::NoRandomAngles);
    if (m_bLegitGuide) iSimFlags |= ProjSimEnum::DiagnosticDeterministic;
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
    // Native arrows spawn at the offset muzzle without sweeping from the eye.
    // Thin cover behind a clear muzzle must not reject a valid forward shot.
    // Still require the complete arrow hull to fit there; forward collision is
    // validated below. Other projectile spawn guards retain their behavior.
    CTraceFilterWorldAndPropsOnly muzzleFilter={}; muzzleFilter.pSkip=pLocal;
    CGameTrace muzzleTrace={};
    const Vec3 muzzleStart=reviewWeapon==TF_WEAPON_COMPOUND_BOW?m_tProjInfo.m_vPos:pLocal->GetShootPos();
    SDK::TraceHull(muzzleStart,m_tProjInfo.m_vPos,-m_tProjInfo.m_vHull,m_tProjInfo.m_vHull,MASK_SOLID,&muzzleFilter,&muzzleTrace);
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

	// Own stickies (arm-timed, not a reflected projectile) stick on first world contact.
	const bool stickyLanding = m_tInfo.m_iArmTime && !m_tInfo.m_pProjectile && pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER;
	// Native VPhysicsCollision sticks only to the world (worldspawn) or a prop_dynamic;
	// doors, brushes, physics props and sky make it bounce or vanish.
	const auto stickySurface = [&]
	{
		if (!trace.DidHit() || trace.startsolid || trace.allsolid || (trace.surface.flags & SURF_SKY)) return false;
		return !trace.m_pEnt || trace.m_pEnt->entindex() == 0 || trace.m_pEnt->GetClassID() == ETFClassID::CDynamicProp;
	};
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
        if (!SearchStep(SearchWork::Flight))
        {
            // bDidHit is set only after collision, head and safety validation.
            // Subsequent ticks extend the drawing path; a cosmetic tail must
            // not revoke the accepted shot. Pre-hit interruptions still fail.
            if (!bDidHit) grenadeReason="search_budget_limited";
            break; // common exit restores the temporarily moved target
        }
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
			bHit = (ProjectilePointPolicy::AirReady(TICKS_TO_TIME(n),m_tInfo.m_flAirArmTime) && trace.endpos.DistToSqr(vPoint) < flRadiusSqr) || trace.DidHit(); break;
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
				// A sticky sticks where it first lands and waits: any landing spot whose
				// blast reaches the target (checked below) is as good as the chosen point.
				bValid = stickyLanding && stickySurface() ? true : trace.endpos.DistToSqr(vPoint) < flRadiusSqr; break;
			case PointTypeEnum::Air:
				bValid = !trace.DidHit(); break;
			}
			// S4: a sticky aimed at the target that lands on the world short of it is
			// still a placement; judge it by its blast like any surface point.
			if (!bValid && !bTarget && iType == PointTypeEnum::Direct && stickyLanding && stickySurface())
			{
				iType = PointTypeEnum::Geometry;
				bValid = true;
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
                if(!bValid && !bSecondTest) m_tSplashMiss = { true, trace.endpos, trace.plane.normal }; // 0.7.0.47: a near miss worth refining
                if(!bValid) grenadeReason="splash_visibility";
                if(!bValid && !m_bPreviewOnly) ProjectileDiagnostics::SplashVisibility(vPoint,trace.endpos,trace.plane.normal,
                    trace.endpos + trace.plane.normal * m_tInfo.m_flNormalOffset,tTarget.m_vPos + m_tInfo.m_vTargetEye,trace2);
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

            // 0.7.0.45 R2: a straight flight is already covered exactly by the contiguous
            // coarse hull sweeps (a swept hull over a line is the same whether traced
            // in one piece or ten), so the per-tick retest only repeats them. It stays
            // for curved flight, where each coarse chord cuts the arc.
			if (bValid && Vars::Aimbot::Projectile::IntervalRetest.Value && iTraceInterval != 1 && (m_tProjInfo.m_flGravity || F::ProjSim.m_bPhysics))
			{
				CGameTrace trace2 = {}; Vec3 vOld, vNew;
				int iTicks = int(m_tProjInfo.m_vPath.size());
				if (m_tInfo.m_flGravity)
					iTicks -= iTimingTolerance;
                // 0.7.0.44 R1: only the path up to the impact is retested. The path points
                // after the impact lie beyond the surface the coarse trace just hit, so
                // retesting them walked through that surface and rejected every candidate
                // whose impact was not in the last sub-tick of the coarse segment
                // ((interval-1)/interval of valid splash candidates). The coarse trace
                // already judged the segment that contains the impact.
                if (iType != PointTypeEnum::Direct && trace.DidHit())
                {
                    const int iInterval = n % iTraceInterval ? n % iTraceInterval : iTraceInterval;
                    const int iBefore = int(std::floor(float(n - iInterval) + trace.fraction * float(iInterval))) + 1;
                    iTicks = std::min(iTicks, std::max(iBefore, 0));
                }

				for (int i = 1; i < iTicks; i++)
				{
                    if (!SearchStep(SearchWork::Flight)) {bValid=false;grenadeReason="search_budget_limited";break;}
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
					auto pose = ProjectileHeadRecord(tTarget.m_pEntity);
					auto aBones = pose?pose->m_aBones:nullptr;
					auto pSet = tTarget.m_pEntity->As<CTFPlayer>()->GetHitboxSet();
                    if(!aBones || !pSet) {
                        if(logHead) { ++capture->headReviews; ProjectileDiagnostics::Ledge("huntsman_head_check",std::format("target={} available=false gate={} reason=missing_bones_or_hitboxes",tTarget.m_pEntity->entindex(),headGate)); }
                        if(headGate) break;
                    }
                    else {

					Vec3 vOffset = pose->m_vOrigin - tTarget.m_vPos;
					Vec3 vPos = trace.endpos + F::ProjSim.GetVelocity().Normalized() * 16 + vOffset;

					float flLowestDistance = std::numeric_limits<float>::max(); int iClosest = -1;
                    float oldDistance=std::numeric_limits<float>::max(); int oldClosest=-1;
					for (int nHitbox = 0; nHitbox < pSet->numhitboxes; ++nHitbox)
					{
						auto pBox = pSet->pHitbox(nHitbox);
						if (!pBox || pBox->bone<0 || pBox->bone>=MAXSTUDIOBONES) continue;

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
				// A sticky that reached the target's hull falls on past it (the simulation
				// does not model the bounce): let it land anywhere up to the target tick.
				iSimTime = n + (stickyLanding ? std::max(iTimingTolerance / 2, TIME_TO_TICKS(.25f)) : iTimingTolerance / 2);
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
    if(m_bLegitGuide) return true; // Target is in the cone; gravity compensation need not be.
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

bool CAimbotProjectile::HandlePoint(const Vec3& vOrigin, int iSimTime, float flPitch, float flYaw, float flTime, const Vec3& vPoint, uint8_t iType, uint8_t iFlags, bool learned)
{
	bool bReturn = false;
    auto* arcCapture=ProjectileDiagnostics::current;
    const bool arcLog=iFlags==PointFlagsEnum::Lob && arcCapture && arcCapture->arcCandidates++<6;
    if (arcLog) ProjectileDiagnostics::Ledge("arc_candidate",std::format("target={} weapon={} type={} flight={} target_ticks={} target_time={} latency={} pitch={} yaw={} underpredict=0",m_tInfo.m_pTarget->m_pEntity->entindex(),m_tInfo.m_pWeapon->GetWeaponID(),iType,flTime,iSimTime,TICKS_TO_TIME(iSimTime),m_tInfo.m_flLatency,flPitch,flYaw));

	if(!m_tInfo.m_pProjectile&&SmoothAim::VisibleGuidance()
		&&!CandidateAngleAllowed({flPitch,flYaw,0.f},vPoint,vOrigin))return false;
	Vec3 vAngles; Aim(G::CurrentUserCmd->viewangles, { flPitch, flYaw, 0.f }, vAngles);
    if(m_ImpactFixedAngle)vAngles=*m_ImpactFixedAngle;
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

		// A sticky may land anywhere up to the target's tick and simply wait there;
		// TestAngle accepts any landing whose blast reaches the target at that tick.
		iSimTime = std::max(int(std::ceil(flTime / I::GlobalVars->interval_per_tick)), iSimTime);
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
	else if (!m_iResult && !m_tInfo.m_pProjectile && !m_ImpactFixedAngle)
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
	if (bReturn && !m_bUpdate && BowChargePlan::trial)
		m_vAngleTo=vAngles,m_vTarget=vPoint,m_vPredicted=vOrigin;
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
        m_bNeuralSelected=learned;
		m_vPlayerPath.clear(), m_vProjectilePath = m_tProjInfo.m_vPath;
        if(m_bNeuralForced)
        {
            // This path was sampled directly from the network model; there is
            // no engine target-movement path to borrow or silently substitute.
            size_t count=0;
            while(count<m_vNeuralPath.size()&&count<m_vNeuralFlightTicks.size()&&m_vNeuralFlightTicks[count]<=iOriginalSimTime)++count;
            m_vPlayerPath.assign(m_vNeuralPath.begin(),m_vNeuralPath.begin()+count);
            m_vPredicted=vOrigin;
            if(!m_vPlayerPath.empty())m_vPlayerPath.back()=vOrigin;
            return bReturn && m_iResult==1;
        }
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
    if (m_bNeuralForced)
    {
        // Learned paths retain later crossings. Try the best arrival-time match
        // within each point priority before spending flight traces on late poses.
        // Keep every candidate and the existing collision/timing acceptance gates.
        std::stable_sort(vDirectHistory.begin(), vDirectHistory.end(), [&](const Direct_t& a, const Direct_t& b) -> bool
        {
            if (a.m_iPriority != b.m_iPriority) return a.m_iPriority < b.m_iPriority;
            const float aError = std::fabs(std::ceil(a.m_flTime / TICK_INTERVAL) - a.m_iSimtime);
            const float bError = std::fabs(std::ceil(b.m_flTime / TICK_INTERVAL) - b.m_iSimtime);
            return aError != bError ? aError < bError : a.m_flTime < b.m_flTime;
        });
    }
    else
    {
	std::sort(vDirectHistory.begin(), vDirectHistory.end(), [&](const Direct_t& a, const Direct_t& b) -> bool
	{
		return a.m_iPriority!=b.m_iPriority ? a.m_iPriority<b.m_iPriority : a.m_flTime<b.m_flTime;
	});
    }
	m_flTimeTo = vDirectHistory.front().m_flTime + m_tInfo.m_flLatency;

	for (auto& tHistory : vDirectHistory)
	{
		if (!SearchAllowed()) break;
		if (diagnosticTests) ++*diagnosticTests;
		if (HandlePoint(tHistory.m_vOrigin, tHistory.m_iSimtime, tHistory.m_flPitch, tHistory.m_flYaw, tHistory.m_flTime, tHistory.m_vPoint, PointTypeEnum::Direct, iType, tHistory.m_bNeural))
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
    // Dynamic is one merged search (trace and face candidates in one ranked pool).
    // Reflected projectiles keep the plain trace search.
    const int mode = m_tInfo.m_pProjectile && selected==SplashSearchPolicy::Dynamic ? SplashSearchPolicy::Trace : selected;
    // Rockets already have a whole-command budget. Other fired projectiles get
    // one shared splash-only budget in Dynamic; never reset it between targets.
    std::optional<ProjectilePerformancePolicy::SearchScope> splashScope;
    if(selected==SplashSearchPolicy::Dynamic && !m_tInfo.m_pProjectile && !ProjectilePerformancePolicy::currentSearchBudget)
        splashScope.emplace(m_DynamicSplashBudget,G::CurrentUserCmd->command_number,true);
    // Own projectiles: the splash search runs to completion like stock Amalgam (no
    // per-command budget). Measured 0.7.0.44: ~1 us per trace, ~20 us per validation.
    struct BudgetOff
    {
        ProjectilePerformancePolicy::SearchBudget* budget=ProjectilePerformancePolicy::currentSearchBudget;
        ProjectilePerformancePolicy::TracePhaseBudget* phase=ProjectilePerformancePolicy::currentTracePhase;
        bool active=false;
        void On(){active=true;ProjectilePerformancePolicy::currentSearchBudget=nullptr;ProjectilePerformancePolicy::currentTracePhase=nullptr;}
        ~BudgetOff(){if(active){ProjectilePerformancePolicy::currentSearchBudget=budget;ProjectilePerformancePolicy::currentTracePhase=phase;}}
    } budgetOff;
    if(!m_tInfo.m_pProjectile) budgetOff.On();

    bool bReturn=false;
	uint8_t iFlags = CalculateFlagsEnum::None;
	if (iType == PointFlagsEnum::Lob)
		iFlags |= CalculateFlagsEnum::LobAngle;
	const uint8_t setupFlags = iFlags;
    const int iCommand = G::CurrentUserCmd->command_number;
	Vec3 generatedAt = vSplashHistory.front().m_vOrigin;
    // Candidates are regenerated every command; recent passes are carried over and
    // validated first (last pass first, then untested, then failed).
    SplashPool_t* pActive = &AcquireSplashPool(mode, setupFlags, generatedAt, iCommand);
    // GetSplashPoints consumes its input (solved setups are removed as histories
    // are processed), so each command works on a copy and only the pass/fail
    // bookkeeping goes back to the pool. The copy is ordered by rank class first.
    std::vector<Setup_t> vPool;
    const auto fPrepareWork = [&]
    {
        vPool = pActive->m_vSetups;
        std::stable_sort(vPool.begin(), vPool.end(), [&](const Setup_t& a, const Setup_t& b)
        {
            const int pa = SplashSearchPolicy::PriorityOf(a.m_iAttempts, a.m_iLastPass, a.m_iLastFail, iCommand);
            const int pb = SplashSearchPolicy::PriorityOf(b.m_iAttempts, b.m_iLastPass, b.m_iLastFail, iCommand);
            if (pa != pb) return pa < pb;
            if (pa == SplashSearchPolicy::PriorityFailed)
            {
                if (a.m_iAttempts != b.m_iAttempts) return a.m_iAttempts < b.m_iAttempts;
                return a.m_iLastFail < b.m_iLastFail;
            }
            return false;
        });
    };
    fPrepareWork();
	// Stickies: later (armed) histories regenerate candidates once the target has
	// moved away from where they were generated (bounded), instead of reusing one
	// origin's set. That generation goes to a scratch pool so the persistent one
	// keeps its origin and history. Other weapons keep their single generation.
	int regenerations = 0;
    int refineProbesLeft = (!m_tInfo.m_flGravity && !m_tInfo.m_pProjectile) ? std::max(0, Vars::Aimbot::Projectile::SplashRefineProbes.Value) : 0; // 0.7.0.49 N2
	const float regenerateDistance = std::max(m_tInfo.m_flRadius / 3.f, 24.f);
	if (!vPool.empty() || vSplashHistory.size() > 1)
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
            // Stickies: histories run earliest-first; the first valid placement wins.
            if (bReturn && m_tInfo.m_iArmTime) break;
            if (m_tInfo.m_iArmTime && !bReturn && regenerations < 2 && tHistory.m_vOrigin.DistTo(generatedAt) > regenerateDistance && SearchAllowed(SearchWork::Sampling))
            {
                generatedAt = tHistory.m_vOrigin;
                m_StickyScratchPool = {};
                m_StickyScratchPool.m_iLastCommand = iCommand;
                SetupSplashPoints(generatedAt, m_StickyScratchPool, setupFlags, mode);
                pActive = &m_StickyScratchPool;
                fPrepareWork();
                ++regenerations;
                if (ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.regenerated;
            }
            int batchSize = 1;
            GetSplashPoints(tHistory.m_vOrigin, vPool, vSplashPoints, tHistory.m_iSimtime, iFlags, bFirst, refill ? &batchSize : nullptr); bFirst = false;
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
                // With a shot in hand, a nearer candidate that failed recently is not
                // re-tried every command; it gets another chance a few commands later.
                if (bReturn && tPoint.m_iPriority == SplashSearchPolicy::PriorityFailed
                    && iCommand - tPoint.m_iLastFail < SplashSearchPolicy::RetryAfterPassCommands)
                    continue;

                ProjectileDiagnostics::Profile validation(ProjectileDiagnostics::SplashValidation);
                if (ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.validations;
				const bool bPassed = HandlePoint(tHistory.m_vOrigin, tHistory.m_iSimtime, tPoint.m_tSolution.m_flPitch, tPoint.m_tSolution.m_flYaw, tPoint.m_tSolution.m_flTime, tPoint.m_vPoint, tPoint.m_iType, iType, tHistory.m_bNeural);
                RecordSplashResult(*pActive, tPoint.m_iSetupId, bPassed, iCommand);
				if (bPassed)
				{
					bReturn = true;
					flLowestDistance = flDistance;
                    if (ProjectileDiagnostics::current) { const auto* pChosen = pActive->Find(tPoint.m_iSetupId); ProjectileDiagnostics::splash.chosenSource = pChosen ? int(pChosen->m_iSource) : -1; }
#ifdef NIKOGRAM_PRIVATE_LEARNING
                    SnapshotSelectedPath(tHistory,tPoint.m_tSolution.m_flTime,tPoint.m_iType,iType,tPoint.m_vPoint);
#endif
				}
                // 0.7.0.49 N2: near-miss scan. A candidate whose rocket reached the surface
                // but whose explosion was blocked is close to a working spot, but .48 showed
                // the feature is sub-unit (a chamfered wall-top corner: 26 probes on 0.3-2.5 u
                // rings all hit the same flat face and failed). So the neighbourhood is
                // scanned on a fine lattice in the hit face's plane (0.15 u, +-1.5 u, 441
                // points, centre outward), SplashRefineProbes per command, continuing across
                // commands while the same spot keeps coming up as a near miss. Every probe is
                // the real validation; the first pass is the shot and is seeded into the pool.
                else if (!bReturn && m_tSplashMiss.m_bValid && tPoint.m_iType == PointTypeEnum::Geometry
                    && !m_tInfo.m_flGravity && !m_tInfo.m_pProjectile && !m_tInfo.m_iArmTime
                    && refineProbesLeft > 0)
                {
                    static const auto& aLattice = []() -> const std::vector<std::pair<int, int>>&
                    {
                        static std::vector<std::pair<int, int>> v;
                        for (int i = -10; i <= 10; ++i) for (int j = -10; j <= 10; ++j) v.emplace_back(i, j);
                        std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second; });
                        return v;
                    }();
                    constexpr float flLatticeStep = 0.15f;
                    const int iTargetHandle = m_tInfo.m_pTarget->m_pEntity->GetRefEHandle().ToInt();
                    const Vec3 vImpact = m_tSplashMiss.m_vImpact, vN = m_tSplashMiss.m_vNormal;
                    auto& tScan = m_tSplashRefine;
                    const bool bSame = tScan.m_iTarget == iTargetHandle && tScan.m_vCenter.DistToSqr(vImpact) < 9.f && iCommand - tScan.m_iLastCommand <= 4;
                    if (!bSame)
                    {
                        // New spot: start a scan around it.
                        tScan = {};
                        tScan.m_iTarget = iTargetHandle; tScan.m_vCenter = vImpact; tScan.m_vNormal = vN;
                        tScan.m_vU = (fabsf(vN.z) < 0.9f ? vN.Cross(Vec3(0, 0, 1)) : vN.Cross(Vec3(1, 0, 0))).Normalized();
                        tScan.m_vV = vN.Cross(tScan.m_vU).Normalized();
                        if (ProjectileDiagnostics::current) ProjectileDiagnostics::Ledge("splash_refine_scan", std::format("target={} centre={},{},{} normal={},{},{}", m_tInfo.m_pTarget->m_pEntity->entindex(), vImpact.x, vImpact.y, vImpact.z, vN.x, vN.y, vN.z));
                    }
                    tScan.m_iLastCommand = iCommand;
                    if (tScan.m_iNext < int(aLattice.size()))
                    {
                        if (ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.refineMisses;
                        const int iTolerance = m_tInfo.m_iArmTime ? -1 : 0;
                        while (tScan.m_iNext < int(aLattice.size()) && refineProbesLeft > 0)
                        {
                            const auto [i, j] = aLattice[tScan.m_iNext++];
                            --refineProbesLeft;
                            const Vec3 vProbe = tScan.m_vCenter + tScan.m_vU * (float(i) * flLatticeStep) + tScan.m_vV * (float(j) * flLatticeStep);
                            Solution_t tSolution; CalculateAngle(m_tInfo.m_vLocalEye, vProbe, tHistory.m_iSimtime, tSolution, iFlags, iTolerance);
                            if (tSolution.m_iCalculated == CalculateResultEnum::Bad) continue;
                            if (ProjectileDiagnostics::current) ++ProjectileDiagnostics::splash.refineValidations;
                            if (!HandlePoint(tHistory.m_vOrigin, tHistory.m_iSimtime, tSolution.m_flPitch, tSolution.m_flYaw, tSolution.m_flTime, vProbe, PointTypeEnum::Geometry, iType, tHistory.m_bNeural))
                                continue;
                            bReturn = true;
                            flLowestDistance = tHistory.m_vOrigin.DistToSqr(vProbe);
                            if (pActive->Add(vProbe, PointTypeEnum::Geometry, SplashSearchPolicy::SourceRefined))
                                RecordSplashResult(*pActive, pActive->m_vSetups.back().m_iId, true, iCommand);
                            if (ProjectileDiagnostics::current)
                            {
                                ++ProjectileDiagnostics::splash.refineHits;
                                ProjectileDiagnostics::splash.chosenSource = SplashSearchPolicy::SourceRefined;
                                ProjectileDiagnostics::SplashCandidate(SplashSearchPolicy::SourceRefined, vProbe, vN);
                                ProjectileDiagnostics::Ledge("splash_refine_hit", std::format("target={} probe={},{},{} lattice={},{} scanned={}", m_tInfo.m_pTarget->m_pEntity->entindex(), vProbe.x, vProbe.y, vProbe.z, i, j, tScan.m_iNext));
                            }
#ifdef NIKOGRAM_PRIVATE_LEARNING
                            SnapshotSelectedPath(tHistory,tSolution.m_flTime,PointTypeEnum::Geometry,iType,vProbe);
#endif
                            tScan.m_iNext = int(aLattice.size()); // done with this spot
                            break;
                        }
                        if (tScan.m_iNext >= int(aLattice.size()) && !bReturn && ProjectileDiagnostics::current)
                            ProjectileDiagnostics::Ledge("splash_refine_exhausted", std::format("target={} centre={},{},{}", m_tInfo.m_pTarget->m_pEntity->entindex(), tScan.m_vCenter.x, tScan.m_vCenter.y, tScan.m_vCenter.z));
                    }
                }
			}
			if (m_tInfo.m_bIgnoreTiming && iType == PointFlagsEnum::Lob)
				break;
		}
	}
    // 0.7.0.45 diagnostics: commands from the start of a continuous search on this
    // target until the first command with a splash solution.
    if (ProjectileDiagnostics::current)
    {
        const int iTargetHandle = m_tInfo.m_pTarget->m_pEntity->GetRefEHandle().ToInt();
        if (m_SplashStreak.m_iTarget != iTargetHandle || iCommand - m_SplashStreak.m_iLastCommand > 2)
            m_SplashStreak = { iTargetHandle, iCommand, iCommand, false };
        m_SplashStreak.m_iLastCommand = iCommand;
        if (bReturn && !m_SplashStreak.m_bLocked)
        {
            m_SplashStreak.m_bLocked = true;
            ProjectileDiagnostics::Ledge("splash_lock", std::format("target={} commands_since_search_start={} candidates={} face_points={} edge_points={} edge_samples={} unreachable={} refine_misses={} refine_validations={} chosen_source={}",
                m_tInfo.m_pTarget->m_pEntity->entindex(), iCommand - m_SplashStreak.m_iStartCommand, int(pActive->m_vSetups.size()),
                ProjectileDiagnostics::splash.facePoints, ProjectileDiagnostics::splash.edgePoints, ProjectileDiagnostics::splash.edgeSamples,
                ProjectileDiagnostics::splash.unreachable, ProjectileDiagnostics::splash.refineMisses, ProjectileDiagnostics::splash.refineValidations, ProjectileDiagnostics::splash.chosenSource));
        }
    }
    mSplashHistory.erase(it);
    return bReturn;
}

int CAimbotProjectile::CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool bUpdate)
{
    m_bImpactConsidered=false; // read by the bow probe; never reuse another target's value
    struct ModeScope {int old=learnedSplashMode;~ModeScope(){learnedSplashMode=old;}} modeScope;
    const bool automatic=Vars::Aimbot::Projectile::SplashPrediction.Value==Vars::Aimbot::Projectile::SplashPredictionEnum::Learned;
    learnedSplashMode=automatic&&tTarget.m_iTargetType==TargetEnum::Player?NeuralMovement::LearnedSplash(tTarget.m_pEntity->As<CTFPlayer>(),pWeapon):1;
    const auto original=tTarget;
    int result=CanHitSelected(tTarget,pLocal,pWeapon,bUpdate);
    if(automatic&&learnedSplashMode==3&&result!=1&&SearchAllowed(SearchWork::Prediction)){
        learnedSplashMode=2;tTarget=original;result=CanHitSelected(tTarget,pLocal,pWeapon,bUpdate);
    }
    if(result==1)m_tInfo.m_iSelectedSplash=EffectiveSplash(m_tInfo);
    return result;
}

int CAimbotProjectile::CanHitSelected(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool bUpdate)
{
    // Saved per-player overrides (Force neural, fallback) are unknown until the
    // learning database has loaded. Only wait for them when neural aiming is
    // on; with it Off, players are aimed at with ordinary prediction.
    if(tTarget.m_iTargetType==TargetEnum::Player&&NeuralMovement::AimingEnabled()&&!NeuralMovement::PoliciesReady()){m_iResult=0;m_bNeuralSelected=false;return 0;}
    const bool richAuto=Vars::NeuralMovement::Use.Value&&bUpdate&&!m_bPreviewOnly&&!m_bLegitGuide&&!F::Aimbot.m_bRunningSecondary&&NeuralMovement::SupportsProjectile(pWeapon);
    if(tTarget.m_iTargetType==TargetEnum::Player&&(NeuralMovement::TargetForced(tTarget.m_pEntity->As<CTFPlayer>())||richAuto)){
        if(!NeuralMovement::TargetFallback(tTarget.m_pEntity->As<CTFPlayer>()))return CanHitForced(tTarget,pLocal,pWeapon,bUpdate);
        // Preserve a complete ordinary solution before optional neural work.
        const auto original=tTarget;const int baselineResult=CanHitOrdinary(tTarget,pLocal,pWeapon,bUpdate);
        if(m_bImpactConsidered)return baselineResult;
        // Auto: a valid ordinary shot is kept; the learned trial only runs when
        // the ordinary solve found nothing. A forced player with fallback still
        // prefers the learned shot and uses the ordinary one only as fallback.
        if(baselineResult==1&&!NeuralMovement::TargetForced(tTarget.m_pEntity->As<CTFPlayer>())){NeuralMovement::OrdinaryKept();return baselineResult;}
        const auto savedTarget=tTarget;const auto vectors=std::array{m_vAngleTo,m_vPredicted,m_vTarget};
        const auto playerPath=m_vPlayerPath,projectilePath=m_vProjectilePath;const auto boxes=m_vBoxes;
        const auto info=m_tInfo;const auto proj=m_tProjInfo;const auto time=m_flTimeTo;const bool world=m_bWorldBlocked;
        const auto observation=std::tuple{m_flObservationFlight,m_flObservationTime,m_bObservationGround,m_bObservationStartGround,m_nObservationHandle,m_iObservationEntity};
        tTarget=original;int trial=0;
        {ProjectilePerformancePolicy::TracePhaseScope boundedTrial;trial=CanHitForced(tTarget,pLocal,pWeapon,bUpdate);}
        if(trial==1)return trial;
        tTarget=savedTarget;m_vAngleTo=vectors[0];m_vPredicted=vectors[1];m_vTarget=vectors[2];
        m_vPlayerPath=playerPath;m_vProjectilePath=projectilePath;m_vBoxes=boxes;m_tInfo=info;m_tProjInfo=proj;m_flTimeTo=time;m_bWorldBlocked=world;m_iResult=baselineResult;m_bNeuralSelected=false;
        std::tie(m_flObservationFlight,m_flObservationTime,m_bObservationGround,m_bObservationStartGround,m_nObservationHandle,m_iObservationEntity)=observation;
        NeuralMovement::FallbackResult(baselineResult==1);return baselineResult;
    }
    // Global Force with a non-player target: unchanged, no automated solution.
    if(NeuralMovement::ForceEnabled())return CanHitForced(tTarget,pLocal,pWeapon,bUpdate);
    // Neither forced nor Auto-eligible: ordinary prediction only.
    m_bNeuralSelected=false;
    return CanHitOrdinary(tTarget,pLocal,pWeapon,bUpdate);
}

int CAimbotProjectile::CanHitForced(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool bUpdate)
{
    AimObserver::Capture observer(tTarget.m_iTargetType==TargetEnum::Player?tTarget.m_pEntity->As<CTFPlayer>():nullptr,pWeapon,bUpdate&&!m_bPreviewOnly&&!m_bLegitGuide&&!F::Aimbot.m_bRunningSecondary,false);
    ProjectilePerformancePolicy::SearchScope commandBudget(m_SearchBudget,G::CurrentUserCmd?G::CurrentUserCmd->command_number:G::OriginalCmd.command_number,true);
    const auto original=tTarget;
    m_iResult=0;m_bNeuralSelected=false;m_bAdaptivePass=false;
    m_iObservationEntity=-1;m_vPlayerPath.clear();m_vProjectilePath.clear();m_vNeuralPath.clear();m_vNeuralFlightTicks.clear();m_vBoxes.clear();
    m_flTimeTo=std::numeric_limits<float>::max();
    const auto reject=[&](const char* why){tTarget=original;m_iResult=0;m_bNeuralSelected=false;m_vPlayerPath.clear();m_vProjectilePath.clear();m_vBoxes.clear();NeuralMovement::ForcedResult(false,why);return 0;};
    if(!bUpdate||m_bPreviewOnly||m_bLegitGuide||F::Aimbot.m_bRunningSecondary
        ||tTarget.m_iTargetType!=TargetEnum::Player||!tTarget.m_pEntity||!pLocal||!pWeapon)
        return reject("forced mode requires a primary player-target solve");
    if(!NeuralMovement::SupportsProjectile(pWeapon))
        return reject("weapon or launch state unsupported by forced neural mode");
    const bool rocket=pWeapon->GetWeaponID()==TF_WEAPON_ROCKETLAUNCHER||pWeapon->GetWeaponID()==TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT;
    if(!SearchAllowed(SearchWork::Prediction))return reject("forced neural search budget exhausted");
    auto neural=NeuralMovement::BeginForced(tTarget.m_pEntity->As<CTFPlayer>(),pWeapon);
    if(!neural.valid)return reject(nullptr);
    SmoothAim::CandidateScope smoothing(tTarget.m_pEntity,true);
    struct ForcedScope{bool& value;bool previous;ForcedScope(bool& v):value(v),previous(v){value=true;}~ForcedScope(){value=previous;}} forced(m_bNeuralForced);
    m_bSearchedThisCommand=true;m_bUpdate=true;
    // Scratch is explicitly invalid: shared collision handlers may inspect it,
    // but no target engine simulation, baseline, or native restore is performed.
    m_tMoveStorage={};m_tMoveStorage.m_bFailed=true;
    m_tProjInfo={};
    if(!F::ProjSim.GetInfo(pLocal,pWeapon,{},m_tProjInfo,ProjSimEnum::NoRandomAngles|ProjSimEnum::PredictCmdNum)
        ||!F::ProjSim.Initialize(m_tProjInfo,false))return reject("projectile setup failed");
    m_tInfo={pLocal,pWeapon,&tTarget};
    m_tInfo.m_vLocalEye=pLocal->GetShootPos();m_tInfo.m_vTargetEye=tTarget.m_pEntity->As<CTFPlayer>()->GetViewOffset();
    const auto& anchor=neural.baseline.anchor.past[2];
    m_tInfo.m_flLatency=F::Backtrack.ProjectileLead(float(anchor.time),float(anchor.time));
    const Vec3 velocity=F::ProjSim.GetVelocity();m_tInfo.m_flVelocity=velocity.Length();
    if(!std::isfinite(m_tInfo.m_flVelocity)||m_tInfo.m_flVelocity<=0.f
        ||!std::isfinite(m_tInfo.m_flLatency)||m_tInfo.m_flLatency<0.f||m_tInfo.m_flLatency>=1.f)
        return reject("invalid forced projectile timing");
    m_tInfo.m_vAngFix=Math::VectorAngles(velocity);m_tInfo.m_vHull=m_tProjInfo.m_vHull.Min(3);
    m_tInfo.m_vOffset=m_tProjInfo.m_vPos-m_tInfo.m_vLocalEye;m_tInfo.m_vOffset.y*=-1;
    m_tInfo.m_flOffsetTime=m_tInfo.m_vOffset.Length()/m_tInfo.m_flVelocity;
    m_tInfo.m_flGravity=m_tProjInfo.m_flGravity;
    if(!std::isfinite(m_tInfo.m_flGravity)||m_tInfo.m_flGravity<0.f)return reject("invalid projectile gravity");
    if(rocket&&(m_tInfo.m_flGravity||F::ProjSim.m_bPhysics))return reject("unsupported altered rocket physics");
    const float launchLifetime=m_tProjInfo.m_flLifetime;
    m_tInfo.m_iSplashRestrict=m_tInfo.m_flGravity?Vars::Aimbot::Projectile::SplashRestrictArc.Value:Vars::Aimbot::Projectile::SplashRestrictDirect.Value;
    m_tInfo.m_flRadius=GetSplashRadius(pWeapon,pLocal,Vars::Aimbot::Projectile::SplashRadius.Value/100);
    m_tInfo.m_flBoundsTime=tTarget.m_pEntity->GetSize().Length()/m_tInfo.m_flVelocity;
    m_tInfo.m_flRadiusTime=m_tInfo.m_flBoundsTime+m_tInfo.m_flRadius/m_tInfo.m_flVelocity;
    m_tInfo.m_iArmTime=TIME_TO_TICKS(ArmTime(pWeapon));
    m_tInfo.m_flAirArmTime=AirArmTime(pWeapon);
    m_tInfo.m_flNormalOffset=rocket||pWeapon->GetWeaponID()==TF_WEAPON_PARTICLE_CANNON?1.f:0.f;
    m_tInfo.m_bIgnoreTiming=false;
    std::array<size_t,movement::trajectory::Branches> branches{0,1,2,3};
    if(neural.distribution.valid)std::stable_sort(branches.begin(),branches.end(),[&](size_t a,size_t b){return neural.distribution.branches[a].probability>neural.distribution.branches[b].probability;});
    const auto frozen=neural;bool incomplete=false;
    for(size_t attempt=0;attempt<(frozen.distribution.valid?branches.size():size_t(1));++attempt){
    neural=frozen;neural.branch=branches[attempt];m_iResult=0;m_bNeuralSelected=false;m_vNeuralPath.clear();m_vNeuralFlightTicks.clear();
    tTarget.m_vPos={anchor.position[0],anchor.position[1],anchor.position[2]};
    auto directs=GetDirects();const auto order=FeetPointPolicy::GenerationOrder(directs);
    auto splashes=GetSplashes();
    DirectHistory_t directHistory;SplashHistory_t splashHistory;
    if(!std::isfinite(TICK_INTERVAL)||TICK_INTERVAL<=0.f)return reject("invalid forced prediction interval");
    // Native Source coordinate bounds give a geometric range bound. The former
    // 60-second rocket value was a simulator default, not a native rocket TTL.
    const float physicalLimit=32768.f*std::sqrt(3.f)/m_tInfo.m_flVelocity;
    std::vector<int> schedule;
    if(!rocket){
        // Curves and drag do not obey the rocket reachability quadratic. Sample
        // the covered prediction prefix and let the existing native-family
        // angle/flight validators decide reachability, contact and splash.
        const float coverage=neural.distribution.valid
            ?movement::trajectory::CoveredHorizon(neural.distribution.branches[neural.branch],Vars::NeuralMovement::MinimumPathProbability.Value/100.f,Vars::NeuralMovement::MaximumPathUncertainty.Value):1.f;
        const int ticks=movement::trajectory::FlightTicks(launchLifetime,Vars::Aimbot::Projectile::MaxSimulationTime.Value,m_tInfo.m_flLatency,coverage,TICK_INTERVAL);
        for(int tick=1;tick<=ticks;++tick)schedule.push_back(tick);
    }else if(neural.distribution.valid){
        const auto&branch=neural.distribution.branches[neural.branch];
        if(branch.probability<Vars::NeuralMovement::MinimumPathProbability.Value/100.f)continue;
        // A possible launch position lies within the muzzle-offset sphere.
        // Include all reachable ticks, even if only splash contact is possible.
        auto roots=[&](Vec3 offset){
            const float padding=m_tInfo.m_vOffset.Length()+tTarget.m_pEntity->GetSize().Length()+m_tInfo.m_flRadius+2*m_tInfo.m_flVelocity*TICK_INTERVAL;
            const auto intervals=movement::trajectory::ReachableIntervals(branch,anchor.position,{m_tInfo.m_vLocalEye.x,m_tInfo.m_vLocalEye.y,m_tInfo.m_vLocalEye.z},{offset.x,offset.y,offset.z},m_tInfo.m_flVelocity,m_tInfo.m_flLatency,physicalLimit,padding);
            for(auto interval:intervals){const int first=std::max(1,int(std::floor(interval[0]/TICK_INTERVAL))),last=int(std::ceil(interval[1]/TICK_INTERVAL));
                for(int tick=first;tick<=last;++tick){if(schedule.size()>=16384){incomplete=true;return;}if(TICKS_TO_TIME(tick)<=physicalLimit)schedule.push_back(tick);}}
        };
        roots({});for(auto&entry:directs)roots(entry.second.m_vOffset);
        std::sort(schedule.begin(),schedule.end());schedule.erase(std::unique(schedule.begin(),schedule.end()),schedule.end());
    }else{const float horizon=std::min(1.f-m_tInfo.m_flLatency,physicalLimit);for(int i=1;i<=int(std::floor(horizon/TICK_INTERVAL));++i)schedule.push_back(i);}
    for(int i:schedule)
    {
        if(!SearchStep(SearchWork::Prediction)){incomplete=true;break;}
        const double time=anchor.time+double(m_tInfo.m_flLatency)+TICKS_TO_TIME(i);
        movement::Vec point{};const auto previous=neural.last.position;
        if(!NeuralMovement::ForcedPoint(neural,time,point,tTarget.m_pEntity->As<CTFPlayer>()))
        {
            if(neural.failed)break;
            continue; // Unsupported horizon/model output is never replaced by ordinary motion.
        }
        tTarget.m_vPos={point[0],point[1],point[2]};m_vNeuralPath.push_back(tTarget.m_vPos);m_vNeuralFlightTicks.push_back(i);
        for(const auto key:order)
        {
            auto it=directs.find(key);if(it==directs.end())continue;
            auto& offset=it->second;
            if(m_tInfo.m_iArmTime>i&&movement::Vec{point[0]-previous[0],point[1]-previous[1],point[2]-previous[2]}!=movement::Vec{})continue;
            auto aimPoint=tTarget.m_vPos+offset.m_vOffset;
            if(!rocket&&Vars::Aimbot::Projectile::HuntsmanPullPoint.Value&&tTarget.m_nAimedHitbox==HITBOX_HEAD){
                aimPoint=PullPoint(aimPoint,m_tInfo.m_vLocalEye,m_tInfo,tTarget.m_vPos,tTarget.m_pEntity->m_vecMins()+m_tInfo.m_vHull,tTarget.m_pEntity->m_vecMaxs()-m_tInfo.m_vHull);
                if(Vars::Aimbot::Projectile::HuntsmanPullNoZ.Value)aimPoint.z=tTarget.m_vPos.z+offset.m_vOffset.z;
            }
            for(uint8_t kind:{uint8_t(PointFlagsEnum::Regular),uint8_t(PointFlagsEnum::Lob)}){
            if(!(offset.m_iFlags&kind)||(kind==PointFlagsEnum::Lob&&(rocket||!ShouldLob(m_tInfo))))continue;
            const uint8_t flags=CalculateFlagsEnum::Accuracy|(kind==PointFlagsEnum::Lob?CalculateFlagsEnum::LobAngle:0);
            Solution_t solution;CalculateAngle(m_tInfo.m_vLocalEye,aimPoint,i,solution,flags);
            if(solution.m_iCalculated==CalculateResultEnum::Good)
            {
                directHistory[kind].emplace_back(History_t(tTarget.m_vPos,i),solution.m_flPitch,solution.m_flYaw,solution.m_flTime,aimPoint,key);
                directHistory[kind].back().m_bNeural=true;
            }
            }
            // Later reachable roots may remain valid after this point is occluded.
        }
        for(auto it=splashes.begin();it!=splashes.end();)
        {
            if(*it!=PointFlagsEnum::Regular&&(rocket||!ShouldLob(m_tInfo))){it=splashes.erase(it);continue;}
            const uint8_t flags=CalculateFlagsEnum::AccountDrag|(*it==PointFlagsEnum::Lob?CalculateFlagsEnum::LobAngle:0);
            Solution_t solution;CalculateAngle(m_tInfo.m_vLocalEye,tTarget.m_vPos,i,solution,flags);
            if(solution.m_iCalculated==CalculateResultEnum::Bad){++it;continue;}
            const float difference=solution.m_flTime-TICKS_TO_TIME(i);
            if(difference < -m_tInfo.m_flRadiusTime&&!m_tInfo.m_iArmTime){++it;continue;}
            if(difference<=m_tInfo.m_flRadiusTime&&m_tInfo.m_iArmTime<=i)
            {
                splashHistory[*it].emplace_back(History_t(tTarget.m_vPos,i),m_tInfo.m_iArmTime?TICKS_TO_TIME(i-m_tInfo.m_iArmTime):std::fabs(difference));
                splashHistory[*it].back().m_bNeural=true;
            }
            ++it;
        }
        if(directs.empty()&&splashes.empty())break;
    }
    if(neural.invalidated)return reject("target snapshot changed during solve");
    if(m_vNeuralPath.empty())continue;
    bool preferSplash=m_tInfo.m_flRadius&&EffectiveSplash(m_tInfo)>=Vars::Aimbot::Projectile::SplashPredictionEnum::Prefer;
    while(!directHistory.empty()||!splashHistory.empty())
    {
        if(preferSplash&&!DirectHitProjectile(m_tInfo)&&HandleSplash(splashHistory))break;
        if(HandleDirect(directHistory))break;
        if(HandleSplash(splashHistory))break;
    }
    if(m_iResult==1&&m_bNeuralSelected)break;
    if(!SearchAllowed(SearchWork::Prediction)){incomplete=true;break;}
    }
    if(incomplete&&m_iResult!=1)return reject("incomplete neural search: command budget exhausted");
    if(m_iResult!=1||!m_bNeuralSelected||!CandidateAngleAllowed(m_vAngleTo,m_vTarget,m_vPredicted))
        return reject("no collision-validated forced neural solution");
    tTarget.m_vPos=m_vTarget;tTarget.m_vAngleTo=m_vAngleTo;
    NeuralMovement::ForcedResult(true);return 1;
}

int CAimbotProjectile::CanHitOrdinary(Target_t& target,CTFPlayer* local,CTFWeaponBase* weapon,bool update)
{
    m_bImpactConsidered=false;m_flImpactReserve=0;
    auto* player=target.m_iTargetType==TargetEnum::Player?target.m_pEntity->As<CTFPlayer>():nullptr;
    if(!update||m_bPreviewOnly||m_bLegitGuide||F::Aimbot.m_bRunningSecondary||!KnockbackPrediction::Enabled(player))
        return CanHitOrdinaryBase(target,local,weapon,update);
    const float observed=F::EnginePrediction.m_bInPrediction&&local->GetPredDescMap()?F::EnginePrediction.m_flOldCurrentTime:I::GlobalVars->curtime;
    KnockbackPrediction::Snapshot snapshot(player,observed);
    if(!snapshot.complete){m_bImpactConsidered=true;m_iObservationEntity=-1;return m_iResult=0;}
    if(!snapshot.count)return CanHitOrdinaryBase(target,local,weapon,update);
    const auto original=target;
    KnockbackPrediction::Pass ordinary(snapshot,false);
    int result=0;
    {MovementImpact::Scope scope(KnockbackPrediction::Pass::Tick,&ordinary,false,&KnockbackPrediction::Pass::Before);result=CanHitOrdinaryBase(target,local,weapon,update);}
    if(!ordinary.evidence.complete||!snapshot.Current())
    {m_bImpactConsidered=true;m_iObservationEntity=-1;return m_iResult=0;}
    // A pill that could go off before this shot lands makes the predicted path
    // unreliable: don't fire. One that goes off only after impact is irrelevant.
    const float pillMargin=2.f*TICK_INTERVAL;
    if(result==1&&ordinary.evidence.Hazard(m_flTimeTo,pillMargin))
    {m_bImpactConsidered=true;m_iObservationEntity=-1;ProjectileDiagnostics::Stage("pill_fuse_hazard");return m_iResult=0;}
    if(!ordinary.evidence.Affects(m_flTimeTo))return result;
    m_bImpactConsidered=true; // Auto cannot overwrite impact context with an unaware model path.
    if(!ordinary.evidence.complete||!snapshot.Current()||!SearchAllowed(SearchWork::Prediction))return m_iResult=0;
    const auto savedTarget=target;const auto vectors=std::array{m_vAngleTo,m_vPredicted,m_vTarget};
    const auto playerPath=m_vPlayerPath,projectilePath=m_vProjectilePath;const auto boxes=m_vBoxes;
    const auto info=m_tInfo;const auto proj=m_tProjInfo;const auto time=m_flTimeTo;const bool world=m_bWorldBlocked;
    const auto observation=std::tuple{m_flObservationFlight,m_flObservationTime,m_bObservationGround,m_bObservationStartGround,m_nObservationHandle,m_iObservationEntity};
    const bool uncertain=!ordinary.evidence.High();
    // Search an impact-adjusted candidate first. An uncertain source may alter
    // the useful angle; publishing it still requires the exact same command to
    // pass a fresh no-impact simulation below. No midpoint path is fabricated.
    m_bImpactPass=true;
    struct EndPass{CAimbotProjectile& self;~EndPass(){self.m_bImpactPass=false;self.m_ImpactFixedAngle.reset();}} end{*this};
    target=original;m_vPlayerPath.clear();m_vProjectilePath.clear();m_vBoxes.clear();m_flTimeTo=std::numeric_limits<float>::max();
    KnockbackPrediction::Pass impacted(snapshot,true);int trial=0;
    {MovementImpact::Scope scope(KnockbackPrediction::Pass::Tick,&impacted,true,&KnockbackPrediction::Pass::Before);trial=CanHitOrdinaryBase(target,local,weapon,update);}
    if(trial==1&&impacted.evidence.Hazard(m_flTimeTo,pillMargin))trial=0;
    if(uncertain){
        if(trial!=1||!impacted.evidence.complete||!snapshot.Current()||!SearchAllowed(SearchWork::Prediction)){m_iObservationEntity=-1;return m_iResult=0;}
        const auto impactTarget=target;const auto impactVectors=std::array{m_vAngleTo,m_vPredicted,m_vTarget};
        const auto impactPlayerPath=m_vPlayerPath,impactProjectilePath=m_vProjectilePath;const auto impactBoxes=m_vBoxes;
        const auto impactInfo=m_tInfo;const auto impactProj=m_tProjInfo;const auto impactTime=m_flTimeTo;const bool impactWorld=m_bWorldBlocked;
        const auto impactObservation=std::tuple{m_flObservationFlight,m_flObservationTime,m_bObservationGround,m_bObservationStartGround,m_nObservationHandle,m_iObservationEntity};
        m_ImpactFixedAngle=m_vAngleTo;target=original;m_vPlayerPath.clear();m_vProjectilePath.clear();m_vBoxes.clear();m_flTimeTo=std::numeric_limits<float>::max();
        KnockbackPrediction::Pass verifiedOrdinary(snapshot,false);int verified=0;
        {MovementImpact::Scope scope(KnockbackPrediction::Pass::Tick,&verifiedOrdinary,true,&KnockbackPrediction::Pass::Before);verified=CanHitOrdinaryBase(target,local,weapon,update);}
        if(verified!=1||!verifiedOrdinary.evidence.complete||verifiedOrdinary.evidence.Hazard(m_flTimeTo,pillMargin)||!snapshot.Current()){m_iObservationEntity=-1;return m_iResult=0;}
        if(impacted.cannonEncounter){
            // IsDoubleDonk is server-only launcher history. Revalidate this same
            // outgoing angle with its alternate native minicrit damage force.
            // Evidence rejects multiple uncertain contacts rather than omitting
            // mixed combinations of independent hidden histories.
            if(!SearchAllowed(SearchWork::Prediction)){m_iObservationEntity=-1;return m_iResult=0;}
            target=original;m_vPlayerPath.clear();m_vProjectilePath.clear();m_vBoxes.clear();m_flTimeTo=std::numeric_limits<float>::max();
            KnockbackPrediction::Pass donk(snapshot,true,true);int verifiedDonk=0;
            {MovementImpact::Scope scope(KnockbackPrediction::Pass::Tick,&donk,true,&KnockbackPrediction::Pass::Before);verifiedDonk=CanHitOrdinaryBase(target,local,weapon,update);}
            if(verifiedDonk!=1||!donk.evidence.complete||donk.evidence.Hazard(m_flTimeTo,pillMargin)||!snapshot.Current()){m_iObservationEntity=-1;return m_iResult=0;}
        }
        // Restore only a candidate that passed both hypotheses. Uncertain pipes
        // never reserve expected damage or suppress future ammunition.
        target=impactTarget;m_vAngleTo=impactVectors[0];m_vPredicted=impactVectors[1];m_vTarget=impactVectors[2];
        m_vPlayerPath=impactPlayerPath;m_vProjectilePath=impactProjectilePath;m_vBoxes=impactBoxes;m_tInfo=impactInfo;m_tProjInfo=impactProj;m_flTimeTo=impactTime;m_bWorldBlocked=impactWorld;m_iResult=trial;
        std::tie(m_flObservationFlight,m_flObservationTime,m_bObservationGround,m_bObservationStartGround,m_nObservationHandle,m_iObservationEntity)=impactObservation;
        m_flImpactReserve=0;return trial;
    }
    const auto choice=KnockbackPolicy::Select(result==1,trial==1&&snapshot.Current(),ordinary.evidence,impacted.evidence,time);
    if(choice==KnockbackPolicy::Choice::Reject){m_iObservationEntity=-1;return m_iResult=0;}
    // Only near-term robust impacts, recomputed after preceding impulses, can
    // reserve damage. Miss/uncertain branches never reserve an expected kill.
    m_flImpactReserve=choice==KnockbackPolicy::Choice::Impact?impacted.evidence.Reserve():0.f;
    if(choice==KnockbackPolicy::Choice::Impact)return trial;
    target=savedTarget;m_vAngleTo=vectors[0];m_vPredicted=vectors[1];m_vTarget=vectors[2];
    m_vPlayerPath=playerPath;m_vProjectilePath=projectilePath;m_vBoxes=boxes;m_tInfo=info;m_tProjInfo=proj;m_flTimeTo=time;m_bWorldBlocked=world;m_iResult=result;
    std::tie(m_flObservationFlight,m_flObservationTime,m_bObservationGround,m_bObservationStartGround,m_nObservationHandle,m_iObservationEntity)=observation;
    return result;
}

int CAimbotProjectile::CanHitOrdinaryBase(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool bUpdate)
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
    // Knockback impact/verification passes re-simulate the hypotheses the ordinary
    // pass of this target used (one world per encounter); every other pass starts
    // from the primary hypotheses. Always back to 0 on exit for other movesim users.
    struct HypothesisReset {~HypothesisReset(){F::MoveSim.m_iReversalHypothesis=0;F::MoveSim.m_iLedgeHypothesis=0;}} hypothesisReset;
    F::MoveSim.m_iReversalHypothesis=m_bImpactPass?F::MoveSim.m_iChosenReversal:0;F::MoveSim.m_bReversalPlanned=false;
    F::MoveSim.m_iLedgeHypothesis=m_bImpactPass?F::MoveSim.m_iChosenLedge:0;F::MoveSim.m_bLedgeEncountered=false;
    if(!m_bImpactPass){F::MoveSim.m_iChosenReversal=0;F::MoveSim.m_iChosenLedge=0;}
    // Learned strafe timing is a projectile-aimbot feature; other movesim users keep the corridor model.
    struct TimingScope {bool previous;TimingScope():previous(F::MoveSim.m_bAllowLearnedTiming){F::MoveSim.m_bAllowLearnedTiming=true;}~TimingScope(){F::MoveSim.m_bAllowLearnedTiming=previous;}} timing;
    int result=CanHitPass(tTarget,pLocal,pWeapon,bUpdate);
    // Learned strafe timing: if the median reversal time gave no shot, try the
    // later guess (the target holds the strafe longer) before any adaptive retry.
    // Not inside knockback impact/verification passes: those must all use the same hypothesis.
    if(!result && !m_bImpactPass && F::MoveSim.m_bReversalPlanned && F::MoveSim.m_flPlannedFirst<1e8f && tTarget.m_iTargetType==TargetEnum::Player && SearchAllowed(SearchWork::Prediction))
    {
        struct HypothesisScope {~HypothesisScope(){F::MoveSim.m_iReversalHypothesis=0;}} hypothesis;
        tTarget=original;F::MoveSim.m_iReversalHypothesis=1;
        ProjectileDiagnostics::Stage("learned_timing_second_guess");
        result=CanHitPass(tTarget,pLocal,pWeapon,bUpdate);
        if(result)F::MoveSim.m_iChosenReversal=1;
    }
    // Ledge: the primary pass simulates the hypothesis the received motion supports
    // (hold at the edge only after an observed slowdown/turn). If it found no shot
    // and the target actually reached a drop, try the other hypothesis once.
    // Not inside knockback impact/verification passes (one hypothesis per encounter).
    if(!result && !m_bImpactPass && F::MoveSim.m_bLedgeEncountered && tTarget.m_iTargetType==TargetEnum::Player && SearchAllowed(SearchWork::Prediction))
    {
        struct LedgeScope {~LedgeScope(){F::MoveSim.m_iLedgeHypothesis=0;}} ledge;
        tTarget=original;F::MoveSim.m_iLedgeHypothesis=1;
        ProjectileDiagnostics::Stage("ledge_second_guess");
        result=CanHitPass(tTarget,pLocal,pWeapon,bUpdate);
        if(result)F::MoveSim.m_iChosenLedge=1;
    }
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
    AimObserver::Capture observer(tTarget.m_iTargetType==TargetEnum::Player?tTarget.m_pEntity->As<CTFPlayer>():nullptr,pWeapon,bUpdate&&!m_bPreviewOnly&&!m_bLegitGuide&&!m_bImpactPass&&!F::Aimbot.m_bRunningSecondary);
    // The legacy/sequence baseline is the primary hypothesis only (never the second timing guess).
    auto neural=NeuralMovement::Begin(!BowChargePlan::trial&&!m_bImpactPass&&!F::MoveSim.m_iReversalHypothesis&&!F::MoveSim.m_iLedgeHypothesis&&tTarget.m_iTargetType==TargetEnum::Player?tTarget.m_pEntity->As<CTFPlayer>():nullptr,pWeapon);
    struct NeuralCapture {NeuralMovement::Context& c;~NeuralCapture(){NeuralMovement::End(c);}} neuralCapture{neural};
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
	if (!F::MoveSim.Initialize(tTarget.m_pEntity, m_tMoveStorage, !m_bLegitGuide) && tTarget.m_iTargetType == TargetEnum::Player)
	{
        diagnostic.reason="movement_setup_failed";
		F::MoveSim.Restore(m_tMoveStorage);
		return false;
	}
    // Movesim baselines/pairs must stay plain movesim: skip them when learned timing steered this pass.
    if(m_tMoveStorage.m_bLearnedTiming)neural.valid=false;

	m_tProjInfo = {};
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, {}, m_tProjInfo, ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum | (m_bLegitGuide?ProjSimEnum::DiagnosticDeterministic:0))
		|| !F::ProjSim.Initialize(m_tProjInfo, false))
	{
        diagnostic.reason="projectile_setup_failed";
		F::MoveSim.Restore(m_tMoveStorage);
		return false;
	}

	m_tInfo = { pLocal, pWeapon, &tTarget };
	m_tInfo.m_bLegitGuide = m_bLegitGuide;
	m_tInfo.m_vLocalEye = pLocal->GetShootPos();
	m_tInfo.m_vTargetEye = tTarget.m_pEntity->As<CTFPlayer>()->GetViewOffset();
	m_tInfo.m_flLatency = F::Backtrack.ProjectileLead(m_tMoveStorage.m_flSimTime,
        m_tMoveStorage.m_pPlayer?m_tMoveStorage.m_pPlayer->m_flSimulationTime():0.f)+BowChargePlan::Wait();
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
    m_tInfo.m_flAirArmTime=AirArmTime(pWeapon);
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
		m_tInfo.m_flNormalOffset = 1.f;
	}
	m_tInfo.m_bIgnoreTiming = false; // Lob target history must correspond to actual arrival time.



	Directs_t mDirects = GetDirects();
    const auto directOrder=FeetPointPolicy::GenerationOrder(mDirects);
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
            tTarget.m_vPos = m_tMoveStorage.m_MoveData.m_vecAbsOrigin;
            observer.Point(m_tMoveStorage.m_flSimTime,tTarget.m_vPos.x,tTarget.m_vPos.y,tTarget.m_vPos.z,!m_tMoveStorage.m_bFailed);
            // Ordinary path is recorded as the legacy-learning baseline only.
            NeuralMovement::RecordPoint(neural,m_tMoveStorage.m_flSimTime,{tTarget.m_vPos.x,tTarget.m_vPos.y,tTarget.m_vPos.z});
		}
		// A zero-tick solution consumed the point without ever running a collision trace.
		if (!TargetPolicy::HasFlightTick(i))
			continue;

		for (const auto key : directOrder)
		{
            auto it=mDirects.find(key);
            if(it==mDirects.end()) continue;
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
					mDirects.erase(it);
					continue;
				}
			}
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
			// A sticky waits on its surface, so arriving early is fine once it can be
			// armed: keep every tick from arming to the end of the horizon (earliest
			// first, below) instead of only the single arming tick.
			if (flTimeTo < -m_tInfo.m_flRadiusTime && !m_tInfo.m_iArmTime)
			{
				it = vSplashes.erase(it);
				continue;
			}
			if (*it == PointFlagsEnum::Lob && !ShouldLob(m_tMoveStorage, m_tInfo))
			{
				++it;
				continue;
			}

			// Stickies: earliest armed tick first (shortest prediction horizon).
			mSplashHistory[*it].emplace_back(History_t(tTarget.m_vPos, i), m_tInfo.m_iArmTime ? TICKS_TO_TIME(i - m_tInfo.m_iArmTime) : fabsf(flTimeTo));
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

static CTFWeaponBase* ChargeCancelWeapon(CTFPlayer* local, CTFWeaponBase* weapon)
{
    for (int i=0;i<MAX_WEAPONS;++i)
    {
        auto* alternate=local->GetWeaponFromSlot(i);
        if (alternate && alternate!=weapon && alternate->CanBeSelected()) return alternate;
    }
    return nullptr;
}

static inline bool CancelShot(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, int& iLastTickCancel)
{
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_COMPOUND_BOW:
	{
		pCmd->buttons |= IN_ATTACK2;
		pCmd->buttons &= ~IN_ATTACK;
		return true;
	}
	case TF_WEAPON_CANNON:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	{
		if (auto* pSwap=ChargeCancelWeapon(pLocal,pWeapon))
		{
			pCmd->weaponselect = pSwap->entindex();
			iLastTickCancel = pWeapon->entindex();
			return true;
		}
	}
	}
	return false;
}

static bool NeuralChargeEnabled()
{ return Vars::NeuralMovement::Use.Value || NeuralMovement::ForceEnabled() || NeuralMovement::HasIndividualForce(); }

struct NeuralChargeState
{
    bool supported=false;
    float begin=0, now=0, duration=0;
};
static NeuralChargeState GetNeuralChargeState(CTFWeaponBase* weapon)
{
    if (!weapon) return {};
    if (weapon->GetWeaponID()==TF_WEAPON_PIPEBOMBLAUNCHER)
        return {true,weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime(),I::GlobalVars->curtime,
            SDK::AttribHookValue(4.f,"stickybomb_charge_rate",weapon)};
    if (weapon->GetWeaponID()==TF_WEAPON_CANNON)
    {
        const float duration=SDK::AttribHookValue(0.f,"grenade_launcher_mortar_mode",weapon);
        // Use the positive native deadline as the charge token. Translating now
        // by the duration makes Finish's expiry test exactly now+tick>=deadline.
        return {std::isfinite(duration)&&duration>0.f,weapon->As<CTFGrenadeLauncher>()->m_flDetonateTime(),
            I::GlobalVars->curtime+duration,duration};
    }
    return {};
}
static bool NeuralChargeActive(CTFPlayer* local)
{
    return NeuralChargeEnabled() && AimModes::Active!=AimModes::Legit && local->CanAttack()
        && !I::EngineVGui->IsGameUIVisible() && !(G::OriginalCmd.buttons&IN_USE)
        && Vars::Aimbot::General::AimType.Value
        && (Vars::Aimbot::General::AutoShoot.Value || bool(Vars::Aimbot::Projectile::Modifiers.Value&Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon));
}
static void ObserveNeuralCharge(BowReleasePolicy::Ownership& ownership,CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd,const NeuralChargeState& state)
{
    ownership.Observe({uint32_t(local->GetRefEHandle().ToInt()),uint32_t(weapon->GetRefEHandle().ToInt()),
        local->m_iSpawnCounter(),I::ClientState->m_nServerCount},cmd->command_number,local->IsAlive()&&!local->IsAGhost(),state.begin,
        bool(G::OriginalCmd.buttons&IN_ATTACK),bool(G::OriginalCmd.buttons&IN_ATTACK2)||G::OriginalCmd.weaponselect!=0);
}
static void FinishNeuralCharge(BowReleasePolicy::Ownership& ownership,CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd,
    const NeuralChargeState& state,bool validated,bool afterSearch,int& lastCancel)
{
    const auto action=ownership.Finish(state.begin,state.now,state.duration,TICK_INTERVAL,NeuralChargeActive(local),
        bool(G::OriginalCmd.buttons&IN_ATTACK),bool(G::OriginalCmd.buttons&IN_ATTACK2)||G::OriginalCmd.weaponselect!=0,
        bool(cmd->buttons&IN_ATTACK),cmd->weaponselect!=0,validated,
        afterSearch && bool(Vars::Aimbot::Projectile::Modifiers.Value&Vars::Aimbot::Projectile::ModifiersEnum::CancelCharge));
    if (action==BowReleasePolicy::Action::Unchanged) return;
    // A swap request can be deferred. Do not turn that request into an unaimed
    // release; ownership stays pending until a native charge/weapon transition.
    const bool cancel=action==BowReleasePolicy::Action::Cancel;
    if (cancel && state.begin==0.f) cmd->buttons&=~IN_ATTACK;
    else cmd->buttons|=IN_ATTACK;
    const bool cancelled=!cancel || state.begin==0.f || CancelShot(local,weapon,cmd,lastCancel);
    G::Attacking=0;F::Aimbot.m_bRan=false;
    ProjectileDiagnostics::Stage(action==BowReleasePolicy::Action::Hold?"neural_charge_hold":
        cancelled?"neural_charge_cancel":"neural_charge_cancel_unavailable");
}

static inline void DrawVisuals(int iResult, Target_t& tTarget, std::vector<Vec3>& vPlayerPath, std::vector<Vec3>& vProjectilePath, std::vector<DrawBox_t>& vBoxes)
{
	if (iResult == 1)
	{
		if (G::Attacking == 1)
		{
			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
			G::AimPoint = ProjectileDisplay::Make(tTarget.m_vPos,tTarget.m_pEntity,H::Entities.GetLocal(),H::Entities.GetWeapon(),32,true,tTarget.m_vAngleTo);
		}
		else
		{
			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
			G::AimPoint = ProjectileDisplay::Make(tTarget.m_vPos,tTarget.m_pEntity,H::Entities.GetLocal(),H::Entities.GetWeapon(),0,false,tTarget.m_vAngleTo);
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
            if(iResult==1) {
                for(auto& path:G::PathStorage)path.m_iProjectileDisplaySerial=ProjectileDisplay::serial;
                for(auto& box:G::BoxStorage)box.m_iProjectileDisplaySerial=ProjectileDisplay::serial;
            }
		}
	}
}

// Would the current-charge Huntsman arrow already kill? Same conservative
// visible-state estimate as "Bodyaim if lethal"; a headshot crits (x3).
static bool BowLethal(CTFPlayer* local,CTFWeaponBase* weapon,CBaseEntity* entity,float fraction,bool head)
{
    if(!entity||!entity->IsPlayer())return false;
    auto target=entity->As<CTFPlayer>();
    const float maxCharge=BowCharge::MaximumTime(weapon,local);
    const bool mini=local->IsMiniCritBoosted()||target->InCond(TF_COND_URINE)
        ||target->InCond(TF_COND_MARKEDFORDEATH)||target->InCond(TF_COND_MARKEDFORDEATH_SILENT);
    const float boost=head||local->IsCritBoosted()?3.f:mini?1.35f:1.f;
    float resistance=SDK::AttribHookValue(1.f,"mult_dmgtaken",target);
    resistance=SDK::AttribHookValue(resistance,"mult_dmgtaken_from_bullets",target);
    const bool uncertain=target->IsInvulnerable()||target->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST)
        ||target->InCond(TF_COND_MEDIGUN_SMALL_BULLET_RESIST)||target->InCond(TF_COND_DEFENSEBUFF)
        ||target->InCond(TF_COND_DEFENSEBUFF_NO_CRIT_BLOCK)||target->InCond(TF_COND_DEFENSEBUFF_HIGH)
        ||target->InCond(TF_COND_RUNE_RESIST)||target->InCond(TF_COND_STEALTHED_USER_BUFF_FADING)
        ||target->InCond(TF_COND_STEALTHED)||target->m_bFeignDeathReady()||!std::isfinite(maxCharge)||maxCharge<=0;
    const float damage=BowChargePolicy::BodyDamage(fraction,SDK::AttribHookValue(50.f,"mult_dmg",weapon),weapon->GetDamage(),boost,resistance);
    return BowChargePolicy::Lethal(damage,target->m_iHealth(),uncertain);
}

// Hold an automated Huntsman draw (instead of releasing a valid but weak
// current-charge shot) while the planned full-charge shot still hits. Release
// when charge is full, the current shot already kills, or the stronger shot
// can't be verified / stops working.
bool CAimbotProjectile::BowHoldForCharge(Target_t& target,CTFPlayer* local,CTFWeaponBase* weapon,float begin)
{
    const float maxTime=BowCharge::MaximumTime(weapon,local);
    const float fraction=BowChargePolicy::Fraction(I::GlobalVars->curtime,begin,maxTime);
    if(!std::isfinite(fraction)||fraction>=.999f)return false;
    if(BowLethal(local,weapon,target.m_pEntity,fraction,target.m_nAimedHitbox==HITBOX_HEAD))return false;
    const auto handle=uint32_t(target.m_pEntity->GetRefEHandle().ToInt());
    const int spawn=target.m_iTargetType==TargetEnum::Player?target.m_pEntity->As<CTFPlayer>()->m_iSpawnCounter():-1;
    const auto future=m_BowPlan.Future(handle,I::GlobalVars->curtime,TICK_INTERVAL,spawn);
    if(!future||!SearchAllowed(SearchWork::Prediction)||!SearchAllowed())return false;
    // The probe must not disturb the selected current-charge solution.
    const auto vectors=std::array{m_vAngleTo,m_vTarget,m_vPredicted};
    const auto info=m_tInfo;const auto proj=m_tProjInfo;const float timeTo=m_flTimeTo;const int resultNow=m_iResult;
    const auto playerPath=m_vPlayerPath,projectilePath=m_vProjectilePath;const auto boxes=m_vBoxes;const int observation=m_iObservationEntity;
    auto prospective=target;int result=0;
    {BowChargePlan::Scope scope(*future);result=CanHit(prospective,local,weapon,false);}
    m_vAngleTo=vectors[0];m_vTarget=vectors[1];m_vPredicted=vectors[2];m_tInfo=info;m_tProjInfo=proj;m_flTimeTo=timeTo;m_iResult=resultNow;
    m_vPlayerPath=playerPath;m_vProjectilePath=projectilePath;m_vBoxes=boxes;m_iObservationEntity=observation;
    if(result==1)ProjectileDiagnostics::Stage("bow_hold_for_charge",int(fraction*100));
    return result==1;
}

bool CAimbotProjectile::RunMain(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
    const int nWeaponID = pWeapon->GetWeaponID();
    const bool bowAuto=nWeaponID==TF_WEAPON_COMPOUND_BOW&&!F::Aimbot.m_bRunningSecondary
        &&!m_BowOwnership.manual&&!(G::OriginalCmd.buttons&(IN_ATTACK|IN_ATTACK2|IN_USE))&&!G::OriginalCmd.weaponselect
        &&Vars::Aimbot::General::AimType.Value
        &&(Vars::Aimbot::General::AutoShoot.Value||(Vars::Aimbot::Projectile::Modifiers.Value&Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon));
    if(bowAuto&&(m_BowOwnership.cancelPending||(m_BowPlan.valid&&pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime()==0.f&&I::GlobalVars->curtime>=m_BowPlan.deadline)))return false;
    const auto bowIdentity=BowReleasePolicy::Identity{uint32_t(pLocal->GetRefEHandle().ToInt()),uint32_t(pWeapon->GetRefEHandle().ToInt()),pLocal->m_iSpawnCounter(),I::ClientState->m_nServerCount};
    const int bowSettings=int(Vars::Aimbot::General::AutoShoot.Value)|(Vars::Aimbot::Projectile::Modifiers.Value<<1);

    if(NeuralMovement::ForceEnabled()&&!NeuralMovement::SupportsProjectile(pWeapon))return false;
    const auto neuralCharge=GetNeuralChargeState(pWeapon);
    const bool protectedCharge=neuralCharge.supported && (NeuralChargeEnabled() || m_NeuralChargeOwnership.owned);
    if (protectedCharge && m_NeuralChargeOwnership.cancelPending) return false;
    if (protectedCharge && !(G::OriginalCmd.buttons&(IN_ATTACK|IN_ATTACK2)) && !G::OriginalCmd.weaponselect
        && (!std::isfinite(neuralCharge.duration) || neuralCharge.duration<=0.f
            || !std::isfinite(neuralCharge.begin) || neuralCharge.begin<0.f)) return false;
    if (protectedCharge && !(G::OriginalCmd.buttons&(IN_ATTACK|IN_ATTACK2)) && !G::OriginalCmd.weaponselect
        && (G::OriginalCmd.buttons&IN_USE)) return false;
    // Do not start an automated release-to-fire charge with no cancellation
    // actuator. Existing/manual charges remain owned by their original source.
    if (protectedCharge && neuralCharge.begin==0.f && !(G::OriginalCmd.buttons&(IN_ATTACK|IN_ATTACK2))
        && !G::OriginalCmd.weaponselect && !ChargeCancelWeapon(pLocal,pWeapon)) return false;
    // Use suppresses bow automation. Maintenance explicitly cancels an owned
    // charge first; merely dropping IN_ATTACK here would launch the arrow.
    if (nWeaponID==TF_WEAPON_COMPOUND_BOW && (G::OriginalCmd.buttons&IN_USE)) return false;

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

	if (nWeaponID!=TF_WEAPON_COMPOUND_BOW&&!NeuralMovement::ForceEnabled()&&F::AimbotGlobal.ShouldHoldAttack(pWeapon))
		pCmd->buttons |= IN_ATTACK;
	if (!Vars::Aimbot::General::AimType.Value
		|| !F::AimbotGlobal.ShouldAim() && nWeaponID != TF_WEAPON_FLAMETHROWER)
	{
        AimObserver::Mark(!Vars::Aimbot::General::AimType.Value?AimObserver::Reason::AimInactive:AimObserver::Reason::WeaponNotReady);
        ProjectileDiagnostics::Stage("aim_gate");
		return false;
	}

	// Sticky "Charge weapon": charge only while a shot exists at the current or a
	// higher charge (probed per target below), never blindly; a full-charge auto
	// release would otherwise fire a wasted sticky. Neural-owned charges keep the
	// previous behaviour.
	const bool stickyAutoCharge=nWeaponID==TF_WEAPON_PIPEBOMBLAUNCHER && iRealAimType && !protectedCharge
		&& (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon)
		&& !F::Aimbot.m_bRunningSecondary && !(G::OriginalCmd.buttons&(IN_ATTACK|IN_ATTACK2|IN_USE)) && !G::OriginalCmd.weaponselect;
	if(nWeaponID==TF_WEAPON_PIPEBOMBLAUNCHER && !F::Aimbot.m_bRunningSecondary)
	{
		const float begin=pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime();
		if(!stickyAutoCharge || (begin==0.f && m_iStickyHoldCommand!=G::OriginalCmd.command_number-1))
			m_bStickyChargeOwned=false,m_iStickyChargeFails=0;
	}
	// Keep holding an owned sticky charge with no shot yet; cancel it (weapon
	// swap, native Holster resets the charge) after a short run of commands with
	// no shot at any charge, when "Cancel charge" is on.
	// Probe-authorized holds need a way out: native launches at full charge
	// (LaunchGrenade at GetChargeMaxTime), aimed wherever the view is.
	const bool stickyCanCancel=stickyAutoCharge
		&& (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::CancelCharge)
		&& ChargeCancelWeapon(pLocal,pWeapon)!=nullptr;
	const auto stickyNearFull=[&]
	{
		const float begin=pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime();
		const float full=SDK::AttribHookValue(4.f,"stickybomb_charge_rate",pWeapon);
		return begin>0.f && std::isfinite(full) && I::GlobalVars->curtime-begin+2.f*TICK_INTERVAL>=full;
	};
	const auto maintainStickyCharge=[&]
	{
		if(!stickyAutoCharge || !m_bStickyChargeOwned) return false;
		if(pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime()<=0.f) return false;
		if((++m_iStickyChargeFails>=StickyChargeFailCommands || stickyNearFull())
			&& stickyCanCancel && CancelShot(pLocal,pWeapon,pCmd,m_iLastTickCancel))
		{
			pCmd->buttons&=~IN_ATTACK; m_bStickyChargeOwned=false; m_iStickyChargeFails=0;
			G::Attacking=0; F::Aimbot.m_bRan=false; ProjectileDiagnostics::Stage("sticky_charge_cancel");
			return true;
		}
		pCmd->buttons|=IN_ATTACK; m_iStickyHoldCommand=G::OriginalCmd.command_number;
		G::Attacking=0; F::Aimbot.m_bRan=false; AimObserver::Mark(AimObserver::Reason::Charging);
		return true;
	};
	if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon && iRealAimType
		&& (nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER && protectedCharge || nWeaponID == TF_WEAPON_CANNON && G::LastUserCmd->buttons & IN_ATTACK))
	{
		pCmd->buttons |= IN_ATTACK;
		if (!G::CanPrimaryAttack && !G::Reloading && Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent)
			return false;
	}

	auto vTargets = F::AimbotGlobal.ManageTargets(GetTargets, pLocal, pWeapon);
    ProjectileDiagnostics::Stage("eligible_targets",int(vTargets.size()));
	if (vTargets.empty()){AimObserver::Mark(AimObserver::Reason::NoTarget);
        if (maintainStickyCharge()) return true;
        return !F::Aimbot.m_bRunningSecondary && ManageViewmodel(pLocal,pWeapon,pCmd,true);}
    AimObserver::Mark(AimObserver::Reason::NoSolution);

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
	AmmoConservation::Context conservation(pLocal,pWeapon,pCmd,F::EnginePrediction.m_bInPrediction && pLocal->GetPredDescMap()
        ?F::EnginePrediction.m_flOldCurrentTime:I::GlobalVars->curtime);
	const int& deferredTarget=conservation.deferredTarget;
	for (size_t targetIndex=0; targetIndex<vTargets.size(); ++targetIndex)
	{
		if (!SearchAllowed(SearchWork::Prediction) || !SearchAllowed()) break;
		auto& tTarget=vTargets[targetIndex];
        // Incoming damage is independent of solving another outgoing shot.
        // Skip covered targets before the expensive solver. If every remaining
        // alternative fails, Finish withholds using this same command's evidence.
        const bool impactAccounting=tTarget.m_iTargetType==TargetEnum::Player && KnockbackPrediction::Enabled(tTarget.m_pEntity->As<CTFPlayer>());
        if(!impactAccounting && conservation.SkipCovered(tTarget.m_pEntity)) continue;
        SmoothAim::CandidateScope smoothing(tTarget.m_pEntity);
        bool reuseMovement=!NeuralMovement::ForceEnabled() && AutoViewmodelSwitch::Enabled() && AutoViewmodelSwitch::Supported(pWeapon)
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
        // Prospective success authorizes holding only. Forced personal/global models
        // keep their current-charge path until their future horizon is supported.
        // The probe runs with bUpdate=false, which skips update-only gates
        // (knockback evidence). If the current solve was vetoed by one of them,
        // a prospective success would authorize a hold the release path can
        // never validate (draw -> hold -> deadline cancel -> redraw).
        if(bowAuto&&iResult!=1&&!m_bImpactConsidered&&!NeuralMovement::ForceEnabled()
            &&!(tTarget.m_iTargetType==TargetEnum::Player&&NeuralMovement::TargetForced(tTarget.m_pEntity->As<CTFPlayer>()))
            &&SearchAllowed(SearchWork::Prediction)&&SearchAllowed())
        {
            const float begin=pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime();
            auto plan=m_BowPlan;
            const auto handle=uint32_t(tTarget.m_pEntity->GetRefEHandle().ToInt());
            const int targetSpawn=tTarget.m_iTargetType==TargetEnum::Player?tTarget.m_pEntity->As<CTFPlayer>()->m_iSpawnCounter():-1;
            // Never start a draw during the LowerBow lockout (holding attack there
            // keeps native m_bNoFire set, so the draw would never begin).
            if(!plan.valid&&begin==0.f&&G::CanPrimaryAttack)
                plan.Start(bowIdentity,handle,G::OriginalCmd.command_number,Vars::Aimbot::General::AimType.Value,bowSettings,I::GlobalVars->curtime,BowCharge::MaximumTime(pWeapon,pLocal),targetSpawn);
            if(auto future=plan.Future(handle,I::GlobalVars->curtime,TICK_INTERVAL,targetSpawn))
            {
                auto prospective=tTarget;int result=0;
                {BowChargePlan::Scope scope(*future);result=CanHit(prospective,pLocal,pWeapon,false);}
                // bUpdate=false deliberately avoids shot/visual publication; retain
                // the same final-angle eligibility check explicitly for this probe.
                if(result==1&&CandidateAngleAllowed(m_vAngleTo,m_vTarget,m_vPredicted)
                    &&F::AimbotGlobal.AllowTargetSwitch(tTarget.m_pEntity))
                {
                    m_BowPlan=plan;m_bBowPlanValidated=true;
                    pCmd->buttons|=IN_ATTACK;G::Attacking=0;F::Aimbot.m_bRan=false;m_iObservationEntity=-1;
                    AimObserver::Mark(AimObserver::Reason::Charging);
                    return true;
                }
            }
            // A failed future probe may replace scratch geometry. Do not publish
            // an earlier aim-only bow result using that unrelated geometry.
            iResult=0;m_iObservationEntity=-1;
        }
        // Sticky: no shot at the current charge, so probe full charge. A shot there
        // authorizes holding (charging up) only; release still needs a current-charge solve.
        if(stickyAutoCharge&&stickyCanCancel&&iResult==0&&!stickyNearFull()&&SearchAllowed(SearchWork::Prediction)&&SearchAllowed())
        {
            auto* launcher=pWeapon->As<CTFPipebombLauncher>();
            const float chargeTime=SDK::AttribHookValue(4.f,"stickybomb_charge_rate",pWeapon);
            if(std::isfinite(chargeTime)&&chargeTime>0.f)
            {
                auto prospective=tTarget;int result=0;
                {
                    struct ChargeRestore{CTFPipebombLauncher* launcher;float begin;~ChargeRestore(){launcher->m_flChargeBeginTime()=begin;}} restore{launcher,launcher->m_flChargeBeginTime()};
                    launcher->m_flChargeBeginTime()=I::GlobalVars->curtime-chargeTime;
                    result=CanHit(prospective,pLocal,pWeapon,false);
                }
                // HandlePoint already applied the angle policy to every probe candidate.
                if(result==1&&F::AimbotGlobal.AllowTargetSwitch(tTarget.m_pEntity))
                {
                    pCmd->buttons|=IN_ATTACK;m_bStickyChargeOwned=true;m_iStickyHoldCommand=G::OriginalCmd.command_number;m_iStickyChargeFails=0;
                    G::Attacking=0;F::Aimbot.m_bRan=false;m_iObservationEntity=-1;
                    AimObserver::Mark(AimObserver::Reason::Charging);ProjectileDiagnostics::Stage("sticky_charge_hold",tTarget.m_pEntity->entindex());
                    return true;
                }
            }
            // The probe replaced scratch geometry; never publish an earlier aim-only result with it.
            iResult=0;m_iObservationEntity=-1;
        }
		if ((!protectedCharge || m_NeuralChargeOwnership.manual) && iResult != 1 && pWeapon->GetWeaponID() == TF_WEAPON_CANNON && Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon
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
		if(impactAccounting && conservation.SkipPredicted(tTarget.m_pEntity,m_flImpactReserve))continue;
		if (!iResult) continue;
		if (iResult == 2)
		{
            // An aim-only alternative must not displace a fire-capable original.
            if (deferredTarget>=0) continue;
            AimObserver::Mark(AimObserver::Reason::AimOnly);
            ProjectileDiagnostics::Stage("aim_only_no_fire",tTarget.m_pEntity->entindex());
            if (!F::AimbotGlobal.AllowTargetSwitch(tTarget.m_pEntity)){AimObserver::Mark(AimObserver::Reason::TargetDelay);break;}
			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
			DrawVisuals(iResult, tTarget, m_vPlayerPath, m_vProjectilePath, m_vBoxes);
			Aim(pCmd, tTarget.m_vAngleTo);
			break;
		}

		if (deferredTarget>=0 && !conservation.Allow())
		{
			conservation.Log("evidence_expired",deferredTarget,-1);
			continue; // Re-evaluate on the next command; never persist a wait timer.
		}
		if (deferredTarget>=0)
			conservation.Log("switch_validated",deferredTarget,tTarget.m_pEntity->entindex());
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
            m_CooldownDisplayPoint=ProjectileDisplay::Make(tTarget.m_vPos,tTarget.m_pEntity,pLocal,pWeapon,2,false,tTarget.m_vAngleTo);
            m_vCooldownPreviewBoxes = m_vBoxes;
        }
		AmmoEvidenceDiagnostics::Selected(pLocal, pWeapon, tTarget.m_pEntity, pCmd);
        AimObserver::Selected(tTarget.m_iTargetType==TargetEnum::Player?tTarget.m_pEntity->As<CTFPlayer>():nullptr);
        AimObserver::Mark(AimObserver::Reason::Evaluating);
        if (!F::AimbotGlobal.AllowTargetSwitch(tTarget.m_pEntity)){AimObserver::Mark(AimObserver::Reason::TargetDelay);break;}
        if(bowAuto)
        {
            const auto handle=uint32_t(tTarget.m_pEntity->GetRefEHandle().ToInt());
            const int targetSpawn=tTarget.m_iTargetType==TargetEnum::Player?tTarget.m_pEntity->As<CTFPlayer>()->m_iSpawnCounter():-1;
            const float begin=pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime();
            if(begin==0.f&&!m_BowPlan.valid&&G::CanPrimaryAttack)m_BowPlan.Start(bowIdentity,handle,G::OriginalCmd.command_number,Vars::Aimbot::General::AimType.Value,bowSettings,I::GlobalVars->curtime,BowCharge::MaximumTime(pWeapon,pLocal),targetSpawn);
            else if(m_BowPlan.valid){m_BowPlan.target=handle;m_BowPlan.targetSpawn=targetSpawn;}
            // ChargeWeapon-only retains hold; only existing AutoShoot logic may release.
            pCmd->buttons|=IN_ATTACK;
            // Release both buttons through the lockout so native m_bNoFire clears.
            if(begin==0.f&&!G::CanPrimaryAttack)pCmd->buttons&=~(IN_ATTACK|IN_ATTACK2);
        }
        if(tTarget.m_iTargetType==TargetEnum::Player&&(NeuralMovement::TargetForced(tTarget.m_pEntity->As<CTFPlayer>())||m_bNeuralSelected)&&!F::Aimbot.m_bRunningSecondary)F::Aimbot.RequireFreshProjectileShot();
        if(stickyAutoCharge)
        {
            // A current-charge shot: start the charge (autoshoot releases next
            // command) or, without autoshoot, keep holding as before.
            m_iStickyChargeFails=0;
            if(pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime()==0.f||!Vars::Aimbot::General::AutoShoot.Value)
            {pCmd->buttons|=IN_ATTACK;m_bStickyChargeOwned=true;m_iStickyHoldCommand=G::OriginalCmd.command_number;}
        }
		if (Vars::Aimbot::General::AutoShoot.Value)
		{
			switch (nWeaponID)
			{
			case TF_WEAPON_COMPOUND_BOW:
			{
				pCmd->buttons |= IN_ATTACK;
				const float begin = pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime();
				if (begin > 0.f)
				{
					// At >= 5 s native adds a server-side rand() error of up to 6 degrees:
					// unpredictable, so never auto-release an overcharged arrow.
					const bool overcharged = I::GlobalVars->curtime - begin + TICK_INTERVAL >= TF_ARROW_MAX_CHARGE_TIME;
					// An automated draw keeps charging while a stronger shot stays available.
					if (!overcharged && !(bowAuto && BowHoldForCharge(tTarget, pLocal, pWeapon, begin)))
						pCmd->buttons &= ~IN_ATTACK;
				}
				else if (!G::CanPrimaryAttack)
					pCmd->buttons &= ~IN_ATTACK; // lockout: let native m_bNoFire clear
				break;
			}
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
					if (!m_bNeuralSelected && (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon))
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

        if(Vars::Aimbot::General::AutoShoot.Value&&(pCmd->buttons&IN_ATTACK)){
            F::Aimbot.AutoFireTarget(false,tTarget.m_pEntity);
            if(tTarget.m_iTargetType==TargetEnum::Player)NeuralMovement::ShotOpportunity(tTarget.m_pEntity->As<CTFPlayer>(),pWeapon,m_tInfo.m_iSelectedSplash,G::OriginalCmd.command_number);
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
		if(iResult==1 && !F::Aimbot.m_bRunningSecondary)
			ProjectileCommandDiagnostics::Selected(pCmd,tTarget.m_pEntity,tTarget.m_vAngleTo,m_vTarget,m_vPredicted,m_flTimeTo);
		if (nWeaponID==TF_WEAPON_COMPOUND_BOW && !F::Aimbot.m_bRunningSecondary)
			m_bBowValidated=true;
        if (protectedCharge && !F::Aimbot.m_bRunningSecondary)
            m_bNeuralChargeValidated=true;
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

	if(conservation.Finish())
    {
        G::Attacking=0;F::Aimbot.m_bRan=false;m_iObservationEntity=-1;
        return true;
    }
	if(maintainStickyCharge()) return true;
	return false;
}

#include "../../LearningAccess.h"
void CAimbotProjectile::FinalizeBowCharge(CTFPlayer*local,CTFWeaponBase*weapon,CUserCmd*cmd){
    if(!local||!weapon||!cmd||weapon->GetWeaponID()!=TF_WEAPON_COMPOUND_BOW){m_BowReleaseCommit={};return;}
    const bool intendedRelease=m_BowReleaseCommit.pending;
    m_BowReleaseCommit.Seal(m_BowOwnership,{uint32_t(local->GetRefEHandle().ToInt()),uint32_t(weapon->GetRefEHandle().ToInt()),local->m_iSpawnCounter(),I::ClientState->m_nServerCount},
        G::OriginalCmd.command_number,local->IsAlive()&&!local->IsAGhost(),weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime(),
        bool(G::OriginalCmd.buttons&IN_ATTACK),bool(G::OriginalCmd.buttons&IN_ATTACK2),bool(cmd->buttons&IN_ATTACK),bool(cmd->buttons&IN_ATTACK2),bool(cmd->weaponselect));
    if(intendedRelease&&m_BowOwnership.owned&&(cmd->buttons&IN_ATTACK)&&!(cmd->buttons&IN_ATTACK2))
        AimObserver::Mark(AimObserver::Reason::ReleaseHeld);
}
void CAimbotProjectile::ObservePills()
{
    // Record each pill's first sighting so its fuse window is known by the
    // time a target is evaluated. Only needed by knockback prediction.
    if(Vars::Aimbot::Projectile::StrafePrediction.Value&Vars::Aimbot::Projectile::StrafePredictionEnum::Knockback)
        KnockbackPrediction::ObservePills();
}

void CAimbotProjectile::MaintainBowCharge(CTFPlayer* local, CTFWeaponBase* weapon, CUserCmd* cmd)
{
    m_BowReleaseCommit={};
    if (!local || !weapon || !cmd) m_BowPlan.Clear();
    if (!local || !weapon || !cmd) {m_BowOwnership={};m_NeuralChargeOwnership={};return;}
    const auto charge=GetNeuralChargeState(weapon);
    if (charge.supported && (NeuralChargeEnabled() || m_NeuralChargeOwnership.owned))
    {
        ObserveNeuralCharge(m_NeuralChargeOwnership,local,weapon,cmd,charge);
        // Maintenance cannot acquire ownership or initiate a charge.
        if (m_NeuralChargeOwnership.owned)
            FinishNeuralCharge(m_NeuralChargeOwnership,local,weapon,cmd,charge,false,false,m_iLastTickCancel);
    }
    else m_NeuralChargeOwnership={};
    if (!local || !weapon || !cmd || weapon->GetWeaponID()!=TF_WEAPON_COMPOUND_BOW)
    {m_BowOwnership={};m_BowPlan.Clear();return;}
    const float begin=weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime();
    const bool manual=bool(G::OriginalCmd.buttons&IN_ATTACK);
    const bool cancel=bool(G::OriginalCmd.buttons&IN_ATTACK2) || cmd->weaponselect!=0;
    m_BowOwnership.Observe({uint32_t(local->GetRefEHandle().ToInt()),uint32_t(weapon->GetRefEHandle().ToInt()),
        local->m_iSpawnCounter(),I::ClientState->m_nServerCount},cmd->command_number,local->IsAlive() && !local->IsAGhost(),begin,manual,cancel);
    const bool active=AimModes::Active!=AimModes::Legit && local->CanAttack() && !I::EngineVGui->IsGameUIVisible()
        && !(G::OriginalCmd.buttons&IN_USE)
        && Vars::Aimbot::General::AimType.Value
        && (Vars::Aimbot::General::AutoShoot.Value || bool(Vars::Aimbot::Projectile::Modifiers.Value&Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon));
    const bool hadPlan=m_BowPlan.valid;
    if(hadPlan)
    {
        auto* target=reinterpret_cast<CBaseEntity*>(I::ClientEntityList->GetClientEntityFromHandle(CBaseHandle(m_BowPlan.target)));
        if(!target||uint32_t(target->GetRefEHandle().ToInt())!=m_BowPlan.target
            ||(m_BowPlan.targetSpawn>=0&&(!target->IsPlayer()||!target->As<CTFPlayer>()->IsAlive()
                ||target->As<CTFPlayer>()->IsAGhost()||target->As<CTFPlayer>()->m_iSpawnCounter()!=m_BowPlan.targetSpawn)))m_BowPlan.Clear();
    }
    m_BowPlan.Observe({uint32_t(local->GetRefEHandle().ToInt()),uint32_t(weapon->GetRefEHandle().ToInt()),local->m_iSpawnCounter(),I::ClientState->m_nServerCount},
        G::OriginalCmd.command_number,Vars::Aimbot::General::AimType.Value,int(Vars::Aimbot::General::AutoShoot.Value)|(Vars::Aimbot::Projectile::Modifiers.Value<<1),
        I::GlobalVars->curtime,begin,local->IsAlive()&&!local->IsAGhost(),active,manual,cancel);
    if(hadPlan&&!m_BowPlan.valid&&m_BowOwnership.owned)
    {
        m_BowOwnership.cancelPending=begin>0.f;
        CancelShot(local,weapon,cmd,m_iLastTickCancel);G::Attacking=0;F::Aimbot.m_bRan=false;
        return;
    }
    // Establish the safe default before outer aimbot gates can skip Run. This
    // does not start a charge; only Run may acquire one from its outgoing hold.
    if (!m_BowOwnership.owned) return;
    const auto action=m_BowOwnership.Finish(begin,I::GlobalVars->curtime,TF_ARROW_MAX_CHARGE_TIME,TICK_INTERVAL,
        active,manual,cancel,bool(cmd->buttons&IN_ATTACK),bool(cmd->buttons&IN_ATTACK2),false,false);
    if (action==BowReleasePolicy::Action::Hold) cmd->buttons|=IN_ATTACK;
    else if (action==BowReleasePolicy::Action::Cancel) CancelShot(local,weapon,cmd,m_iLastTickCancel);
    if (action!=BowReleasePolicy::Action::Unchanged) {G::Attacking=0;F::Aimbot.m_bRan=false;}
}

void CAimbotProjectile::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
    if(NeuralMovement::ForceEnabled()&&!NeuralMovement::SupportsProjectile(pWeapon))
    {
        // Relinquish automation without rewriting this command's manual input.
        m_BowOwnership={};m_NeuralChargeOwnership={};m_bBowValidated=false;m_bSearchedThisCommand=false;m_bReuseCooldownPreview=false;
        m_vCooldownPreviewPoint.reset();m_vCooldownPreviewBoxes.clear();m_iObservationEntity=-1;
        NeuralMovement::ForcedResult(false,"weapon unsupported by forced neural mode");return;
    }
    // Plain/Silent ShouldAim skips RunMain's Context during weapon cooldown.
    // Maintain network history here so the next ready command can conserve.
    if(Vars::Aimbot::Projectile::AmmoConservation.Value)
        AmmoConservation::ObservePlayers(F::EnginePrediction.m_bInPrediction && pLocal->GetPredDescMap()
            ?F::EnginePrediction.m_flOldCurrentTime:I::GlobalVars->curtime);
    // Bound bow/Crossbow searches as well as ordinary rockets. The same
    // command budget covers targets, adaptive retries and cooldown previews.
    // Flight validation has safe interruption points; bow ownership below keeps
    // an interrupted search from releasing an automated charge without an aim.
    const int budgetWeapon = pWeapon->GetWeaponID();
    const auto neuralCharge=GetNeuralChargeState(pWeapon);
    const bool chargeProtection=neuralCharge.supported && (NeuralChargeEnabled() || m_NeuralChargeOwnership.owned);
    const bool boundedSearch = (NeuralChargeEnabled() && NeuralMovement::SupportsProjectile(pWeapon)) || chargeProtection || ((budgetWeapon == TF_WEAPON_ROCKETLAUNCHER || budgetWeapon == TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT)
        && pWeapon->m_iItemDefinitionIndex() != Soldier_m_TheBeggarsBazooka)
        || budgetWeapon == TF_WEAPON_CROSSBOW || budgetWeapon == TF_WEAPON_COMPOUND_BOW
        // Sticky searches were unbounded: every target, point and arc ran to
        // completion each command, so FPS fell with player count and with
        // occluded targets whose search never succeeds early.
        || budgetWeapon == TF_WEAPON_PIPEBOMBLAUNCHER
        || bool(Vars::Aimbot::Projectile::StrafePrediction.Value&Vars::Aimbot::Projectile::StrafePredictionEnum::Knockback);
    ProjectilePerformancePolicy::SearchScope searchScope(m_SearchBudget, pCmd->command_number, boundedSearch);
    m_bBowValidated=false;
    m_bBowPlanValidated=false;
    m_bNeuralChargeValidated=false;
    if (!F::Aimbot.m_bRunningSecondary)
    {
        if (chargeProtection) ObserveNeuralCharge(m_NeuralChargeOwnership,pLocal,pWeapon,pCmd,neuralCharge);
        else m_NeuralChargeOwnership={};
    }
    const bool bow=budgetWeapon==TF_WEAPON_COMPOUND_BOW;
    const float bowBegin=bow?pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime():0.f;
    const bool rawBowAttack=bool(G::OriginalCmd.buttons&IN_ATTACK);
    const bool rawBowCancel=bool(G::OriginalCmd.buttons&IN_ATTACK2) || pCmd->weaponselect!=0;
    if (bow && !F::Aimbot.m_bRunningSecondary)
        m_BowOwnership.Observe({uint32_t(pLocal->GetRefEHandle().ToInt()),uint32_t(pWeapon->GetRefEHandle().ToInt()),
            pLocal->m_iSpawnCounter(),I::ClientState->m_nServerCount},pCmd->command_number,pLocal->IsAlive() && !pLocal->IsAGhost(),
            bowBegin,rawBowAttack,rawBowCancel);
    else if (!F::Aimbot.m_bRunningSecondary) m_BowOwnership={};
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
    if (chargeProtection && !F::Aimbot.m_bRunningSecondary)
        FinishNeuralCharge(m_NeuralChargeOwnership,pLocal,pWeapon,pCmd,neuralCharge,m_bNeuralChargeValidated,true,m_iLastTickCancel);
    if (bow && !F::Aimbot.m_bRunningSecondary)
    {
        const bool automatic=bAimEnabled && !(G::OriginalCmd.buttons&IN_USE)
            && (Vars::Aimbot::General::AutoShoot.Value || bool(Vars::Aimbot::Projectile::Modifiers.Value&Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon));
        const auto beforeRelease=m_BowOwnership;
        // One failed command must not cancel a draw (each cancel costs the 1 s
        // LowerBow lockout). Count only real failures: a search cut short by the
        // command budget proves nothing. Cancel after a short run of them.
        const bool bowValidated=m_bBowValidated||m_bBowPlanValidated;
        if(bowValidated||!m_BowOwnership.owned)m_iBowFailures=m_iBowLimitedFailures=0;
        else if(!m_SearchBudget.limited)++m_iBowFailures;
        else ++m_iBowLimitedFailures; // budget-cut commands still end a hopeless draw eventually
        const bool cancelOnFailure=(m_BowPlan.valid||bool(Vars::Aimbot::Projectile::Modifiers.Value&Vars::Aimbot::Projectile::ModifiersEnum::CancelCharge))
            &&(m_iBowFailures>=BowFailCommands||m_iBowLimitedFailures>=BowLimitedFailCommands);
        const auto action=m_BowOwnership.Finish(bowBegin,I::GlobalVars->curtime,TF_ARROW_MAX_CHARGE_TIME,TICK_INTERVAL,
            automatic,rawBowAttack,rawBowCancel,bool(pCmd->buttons&IN_ATTACK),bool(pCmd->buttons&IN_ATTACK2),
            bowValidated,cancelOnFailure);
        if(action==BowReleasePolicy::Action::Cancel)m_iBowFailures=m_iBowLimitedFailures=0;
        m_BowReleaseCommit.Stage(beforeRelease,m_BowOwnership,G::OriginalCmd.command_number,m_bBowValidated,bool(pCmd->buttons&IN_ATTACK),bool(pCmd->buttons&IN_ATTACK2));
        if(action==BowReleasePolicy::Action::Cancel||(!m_BowOwnership.owned&&!m_BowReleaseCommit.pending))m_BowPlan.Clear();
        if (action==BowReleasePolicy::Action::Hold){AimObserver::Mark(AimObserver::Reason::Charging);pCmd->buttons|=IN_ATTACK;}
        else if (action==BowReleasePolicy::Action::Cancel){AimObserver::Mark(AimObserver::Reason::Cancelling);CancelShot(pLocal,pWeapon,pCmd,m_iLastTickCancel);}
        if (action!=BowReleasePolicy::Action::Unchanged)
        {
            G::Attacking=0;F::Aimbot.m_bRan=false;
            ProjectileDiagnostics::Stage(action==BowReleasePolicy::Action::Hold?"bow_auto_hold":"bow_auto_cancel");
        }
    }
    if (pWeapon->GetWeaponID()==TF_WEAPON_PIPEBOMBLAUNCHER || pWeapon->GetWeaponID()==TF_WEAPON_COMPOUND_BOW)
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
        AimObserver::Mark(AimObserver::Reason::Wall);
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
		if(unsafe) {
            AimObserver::Mark(AimObserver::Reason::Safety); pCmd->buttons&=~IN_ATTACK; G::Attacking=0; F::Aimbot.m_bRan=false; }
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
            if (bAimPreview) G::CooldownAimPoint=m_CooldownDisplayPoint;
            G::CooldownBoxStorage=m_vCooldownPreviewBoxes;
            for (auto& box:G::CooldownBoxStorage) {box.m_flTime=I::GlobalVars->curtime+TICKS_TO_TIME(2);box.m_iProjectileDisplaySerial=ProjectileDisplay::serial;}
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
				G::CooldownAimPoint = ProjectileDisplay::Make(target.m_vPos,target.m_pEntity,pLocal,pWeapon,2,false,target.m_vAngleTo);
			G::CooldownBoxStorage = m_vBoxes;
			for (auto& box : G::CooldownBoxStorage) {
                box.m_flTime = I::GlobalVars->curtime + TICKS_TO_TIME(2);box.m_iProjectileDisplaySerial=ProjectileDisplay::serial;
            }
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
	else if (pWeapon->GetWeaponID()!=TF_WEAPON_COMPOUND_BOW
        && !(chargeProtection && !m_NeuralChargeOwnership.manual && !(G::OriginalCmd.buttons&(IN_ATTACK|IN_ATTACK2)) && !G::OriginalCmd.weaponselect)
        && G::CanPrimaryAttack
        && Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::CancelCharge)
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
    if(NeuralMovement::ForceEnabled())return 0; // No hidden ordinary reflected-target prediction.
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
    const bool ignoreFOV=Vars::Aimbot::Projectile::AutoAirblast.Value&Vars::Aimbot::Projectile::AutoAirblastEnum::IgnoreFOV;
	auto vTargets = F::AimbotGlobal.ManageTargets(GetTargets, pLocal, pWeapon,
        Vars::Aimbot::General::TargetSelection.Value,Vars::Aimbot::General::MaxTargets.Value,!ignoreFOV);
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
