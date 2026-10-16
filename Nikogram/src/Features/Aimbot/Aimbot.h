#pragma once
#include "../../SDK/SDK.h"
#include "AutoFirePolicy.h"

struct RealPath_t
{
	DrawPath_t m_tPath = {};
	size_t m_iSize = 0;
};

class CAimbot
{
private:
    bool m_freshProjectile=false;
    int m_latchedTarget=0,m_requestedTarget=0;uint32_t m_latchedHandle=0,m_requestedHandle=0;
	bool ShouldRun(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void RunMain(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void RunAimbot(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool bSecondaryType = false);
    AutoFirePolicy::Latch m_autoFire;
    bool m_autoFireTarget=false,m_autoFirePaced=false,m_autoFireAcquire=false;
    void BeginAutoFire(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd);
    bool AutoFireReady(CTFPlayer* local,CTFWeaponBase* weapon);
    void ContinueAutoFire(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd);
    void FinishAutoFire(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd);

public:
    void RequireFreshProjectileShot();
    bool FreshProjectileShot()const{return m_freshProjectile;} 
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Draw(CTFPlayer* pLocal);
	void Store(CBaseEntity* pEntity, size_t iSize);
	void Store(bool bFrameStageNotify = true);
    // Called only by a primary, validated target's AutoShoot branch.
    void AutoFireTarget(bool paced=false,CBaseEntity* target=nullptr) {if(!m_bRunningSecondary){m_autoFireTarget=true;m_autoFirePaced=paced;m_requestedTarget=target?target->entindex():0;m_requestedHandle=target?target->GetRefEHandle().ToInt():0;}}
    void FinalizeAutoFire(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd,bool compatibilityAdded,bool beforeCompatibility);

	bool m_bRan = false;
	// Reset each command; hitscan assistance owns its aim and selected rewind.
	bool m_bHitscanAssisted = false;
	bool m_bRunningSecondary = false;

	std::unordered_map<int, RealPath_t> m_mRealPaths = {};
};

ADD_FEATURE(CAimbot, Aimbot);
