#pragma once
#include "../../SDK/SDK.h"

struct Projectile_t
{
	std::vector<Vec3> m_vPath = {};
	float m_flTime = 0.f;
	float m_flRadius = 0.f;
	Vec3 m_vNormal = { 0, 0, 1 };
	Color_t m_tColor = {};
	int m_iFlags = 0b0;
};

struct Sightline_t
{
	Vec3 m_vStart = {};
	Vec3 m_vEnd = {};
	Color_t m_tColor = {};
	bool m_bZBuffer = false;
};

struct PickupData_t
{
	int m_iType = 0;
	float m_flTime = 0.f;
	Vec3 m_vLocation;
};

class CVisuals
{
private:
	std::unordered_map<CBaseEntity*, Projectile_t> m_mProjectiles = {};
	std::vector<Sightline_t> m_vSightLines = {};
	std::vector<PickupData_t> m_vPickups = {};
	std::vector<Vec3> m_vAngles = {};
	CTFPlayer* m_pQueuedLocal = nullptr;
	const void* m_pQueuedModel = nullptr;

	bool m_bStoredDefaultFOV = false, m_bStoredCamIdealLag = false;
	float m_flOriginalDefaultFOV = 0.f, m_flOriginalCamIdealLag = 0.f;

	void PruneDrawStorages();

public:
	void Event(IGameEvent* pEvent, uint32_t uHash);
	void Store();
	void Tick();

	void ProjectileTrace(CTFPlayer* pPlayer, CTFWeaponBase* pWeapon, const bool bInterp = true);
	void DrawPickupTimers();

	std::vector<DrawBox_t> GetHitboxes(matrix3x4* aBones, CBaseAnimating* pEntity, std::vector<int> vHitboxes = {}, int iTarget = -1);
	void DrawEffects();
	void DrawExtrapolationGuide();
	void DrawHitboxes(int iStore = 0);

	void FOV(CTFPlayer* pLocal, CViewSetup* pView);
	void ThirdPerson(CTFPlayer* pLocal, CViewSetup* pView);

	void OverrideWorldTextures();
	void Modulate();
	void RestoreWorldModulation();
	void RestoreConVars();

	void CreateMove(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void LocalAnimations(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool bSendPacket);
	void ResetLocalAnimationQueue();
};

ADD_FEATURE(CVisuals, Visuals);
