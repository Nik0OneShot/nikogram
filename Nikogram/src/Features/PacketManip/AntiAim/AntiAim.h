#pragma once
#include "../../../SDK/SDK.h"
#include "BodyYawPolicy.h"
#include "LegitAAPolicy.h"

class CAntiAim
{
private:
	bool UsingLegitAA() const;
	LegitAAPolicy::Preset LegitPreset() const;
	bool UseMinWalk() const;
	void FakeShotAngles(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	float GetYawOffset(CTFPlayer* pEntity, bool bFake);
	float GetBaseYaw(CTFPlayer* pLocal, CUserCmd* pCmd, bool bFake);
	float GetYaw(CTFPlayer* pLocal, CUserCmd* pCmd, bool bFake);
	float GetPitch(float flCurPitch);
	void MinWalk(CTFPlayer* pLocal, CUserCmd* pCmd);
	BodyYawPolicy::State m_tBody = {};
	BodyYawPolicy::State m_tBodyBeforeCommand = {};
	BodyYawPolicy::RealPose m_tPreviousReal = {}, m_tBatchReal = {};
	BodyYawPolicy::RealPose m_tPreviousRealBeforeCommand = {}, m_tBatchRealBeforeCommand = {};
	CTFPlayer* m_pBodyLocal = nullptr;
	const void* m_pBodyModel = nullptr;
	int m_iBodyCommand = 0, m_iBodyChoke = 0, m_iBatchTicks = 0;
	bool m_bBodyValid = false;

public:
	bool AntiAimOn();
	bool YawOn();
	bool ShouldRun(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool bSendPacket, bool bPacketControl);
	void Draw(CTFPlayer* pLocal);

	int AntiAimTicks();

	Vec2 vFakeAngles = {};
	Vec2 vRealAngles = {};
	std::vector<std::pair<Vec3, Vec3>> vEdgeTrace = {};
};

ADD_FEATURE(CAntiAim, AntiAim);
