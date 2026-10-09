#pragma once
#include "../../../SDK/SDK.h"

class CFakeAngle
{
public:
	void Run(CTFPlayer* pLocal);
	void PreviewFeedCommand(const char* token);
	void SendPreviewHead(); // after render-pose reconstruction, before bone restoration

	matrix3x4 aBones[MAXSTUDIOBONES];
	bool bBonesSetup = false;
	// Snapshot metadata for the render-only preview. These are not server hitbox measurements.
	Vec3 vOrigin = {};
	Vec2 vAngles = {};
	float flCurTime = 0.f;

	bool bDrawChams = false;

private:
	int m_nFeedToken = 0, m_nFeedSequence = 0, m_nFeedUserId = 0, m_nFeedServerCount = 0;
	INetChannelInfo* m_pFeedChannel = nullptr;
	float m_flNextFeed = 0.f, m_flBuiltRealtime = -1.f;
	const model_t* m_pBuiltModel = nullptr;
	int m_nBuiltClass = 0;
};

ADD_FEATURE(CFakeAngle, FakeAngle);
