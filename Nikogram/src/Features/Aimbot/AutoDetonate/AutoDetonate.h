#pragma once
#include "../../../SDK/SDK.h"

#include "../AimbotGlobal/AimbotGlobal.h"

class CAutoDetonate
{
private:
	bool CheckEntity(CBaseEntity* pEntity, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, CBaseEntity* pProjectile, float flRadius, Vec3 vOrigin);
	bool CheckEntities(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, CBaseEntity* pProjectile, float flRadius, Vec3 vOrigin);
	bool CheckTargets(CTFPlayer* pLocal, EntityEnum::EntityEnum eGroup, float flRadiusScale, CUserCmd* pCmd);
	bool CheckSelf(CTFPlayer* pLocal, EntityEnum::EntityEnum eGroup);
	bool Check(CTFPlayer* pLocal, CUserCmd* pCmd, EntityEnum::EntityEnum eGroup, int iFlag);

	bool GetRadius(EntityEnum::EntityEnum eGroup, CBaseEntity* pProjectile, float& flRadius, CTFWeaponBase*& pWeapon, bool safety=false, float* exposureDelay=nullptr);
	Vec3 GetOrigin(CBaseEntity* pProjectile, EntityEnum::EntityEnum eGroup, float flLatency = 0.f);

	void PredictPlayers(CTFPlayer* pLocal, bool predicted, bool bLocal=false, float localDelay=0.f);
	void RestorePlayers();

	std::vector<std::pair<CBaseEntity*,Vec3>> m_mRestore = {}; // Reuse storage; each cached player occurs once.
	struct PositionCache { bool ready=false; float latency=0.f; std::vector<std::pair<CBaseEntity*,Vec3>> positions; };
	std::array<PositionCache,3> m_PositionCache = {}; // Predicted enemies, current enemies, predicted local; one Run only.
	std::optional<Vec3> m_vAimPos = {};
	float m_flSafetyTime = 0.f;
	bool ValidProjectile(CTFPlayer* local,CBaseEntity* projectile,EntityEnum::EntityEnum group);
	float Lead(CBaseEntity* entity);

public:
	void Run(CTFPlayer* pLocal, CUserCmd* pCmd);
};

ADD_FEATURE(CAutoDetonate, AutoDetonate);
