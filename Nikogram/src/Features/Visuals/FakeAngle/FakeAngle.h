#pragma once
#include "../../../SDK/SDK.h"

struct AnimFrame_t;

class CFakeAngle
{
public:
	bool Run(CTFPlayer* pLocal, AnimFrame_t* frame = nullptr);
	void Invalidate();
	void BeginRender() { m_bRenderReady = false; }
	bool ValidSnapshot(CTFPlayer* local) const;
	uint64_t Generation() const { return m_uGeneration; }
	void PublishRender(CTFPlayer* local, const matrix3x4* bones, int count);
	matrix3x4* RenderBones(const model_t* model) { return m_bRenderReady && I::GlobalVars->framecount == m_nRenderFrame && model == m_pBuiltModel ? m_aRenderBones : nullptr; }
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
	matrix3x4 m_aRenderBones[MAXSTUDIOBONES] = {};
	bool m_bRenderReady = false;
	int m_nRenderFrame = -1;
	uint64_t m_uGeneration = 1;
	uint32_t m_uBuiltHandle = 0, m_uBuiltWeapon = 0;
	int m_nBuiltSpawn = 0, m_nBuiltServer = 0, m_nBuiltTeam = 0;
	int m_nFeedToken = 0, m_nFeedSequence = 0, m_nFeedUserId = 0, m_nFeedServerCount = 0;
	INetChannelInfo* m_pFeedChannel = nullptr;
	float m_flNextFeed = 0.f, m_flBuiltRealtime = -1.f;
	const model_t* m_pBuiltModel = nullptr;
	int m_nBuiltClass = 0;
};

ADD_FEATURE(CFakeAngle, FakeAngle);
