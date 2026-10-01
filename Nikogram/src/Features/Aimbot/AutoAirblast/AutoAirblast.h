#pragma once
#include "../../../SDK/SDK.h"

class CAutoAirblast
{
public:
	bool m_bPlayerBlast=false;
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	bool CanAirblastEntity(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pEntity, const Vec3& vAngle);
};

ADD_FEATURE(CAutoAirblast, AutoAirblast);
