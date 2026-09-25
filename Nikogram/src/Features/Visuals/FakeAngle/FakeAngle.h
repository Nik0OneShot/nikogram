#pragma once
#include "../../../SDK/SDK.h"

class CFakeAngle
{
public:
	void Run(CTFPlayer* pLocal);

	matrix3x4 aBones[MAXSTUDIOBONES];
	bool bBonesSetup = false;
	// Snapshot metadata for the render-only preview. These are not server hitbox measurements.
	Vec3 vOrigin = {};
	Vec2 vAngles = {};
	float flCurTime = 0.f;

	bool bDrawChams = false;
};

ADD_FEATURE(CFakeAngle, FakeAngle);
