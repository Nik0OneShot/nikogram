#pragma once
#include "../../../SDK/SDK.h"
#include "BodyYawPolicy.h"
#include "LegitAAPolicy.h"
#include "AntiAimPacketPolicy.h"

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
	AntiAimPacketPolicy::Ticket m_tPacket = {};
	int m_iPacketRepairs = 0, m_iSerializedCommand = -1;
	float m_flSerializedYaw = 0.f;
	bool m_bPacketQueued = false;

public:
	bool AntiAimOn();
	bool YawOn();
	bool ShouldRun(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool bSendPacket, bool bPacketControl, bool externalCorrection = false);
	void Draw(CTFPlayer* pLocal);
	void SealCommand(int sequence, const CUserCmd* command, bool send);
	bool PrepareForSend(int sequence, CUserCmd* command);
	void AuditSerialized(int sequence, const CUserCmd* command, bool queued);
	void ResetPacketState();

	int AntiAimTicks();

	Vec2 vFakeAngles = {};
	Vec2 vRealAngles = {};
	std::vector<std::pair<Vec3, Vec3>> vEdgeTrace = {};
};

ADD_FEATURE(CAntiAim, AntiAim);
