#pragma once
#include "../../../SDK/SDK.h"
#include <deque>

// Render-only animation interpolation.
//
// Nikogram animates players in fixed simulation steps:
//  - the local player only animates when a packet is sent (CVisuals::LocalAnimations replays every queued command),
//  - resolver-driven players only animate on network updates (CEntities::Store),
//  - sequence transitions are skipped entirely (CBaseAnimating_MaintainSequenceTransitions).
// These remain client-side simulations, not a guarantee of identical server hitboxes.
//
// This feature preserves that simulation. It records completed local batches and individual remote ticks,
// plays those states back smoothly (with a small delay) while the frame is drawn, lets the game blend between
// sequences only while drawing, and puts the real state back as soon as drawing ends.

struct AnimFrame_t
{
	struct Layer_t
	{
		bool m_bValid = false;
		int m_nSequence = 0;
		float m_flPrevCycle = 0.f;
		float m_flWeight = 0.f;
		int m_nOrder = 0;
		float m_flPlaybackRate = 1.f;
		float m_flCycle = 0.f;
	};

	int m_iTick = 0;
	const void* m_pModel = nullptr; // sequences and pose parameters only make sense for the model they came from

	int m_nSequence = 0;
	float m_flCycle = 0.f;
	float m_flPlaybackRate = 1.f;
	bool m_bSequenceLoops = false;
	std::array<float, 24> m_aPoseParameters = {};
	Vec3 m_vRenderAngles = {};
	std::array<Layer_t, GESTURE_SLOT_COUNT> m_aLayers = {};
};

struct AnimTimeline_t
{
	CTFPlayer* m_pPlayer = nullptr;
	std::deque<AnimFrame_t> m_vFrames = {};
	int m_iNextTick = 0; // timeline tick given to the next recorded frame
	int m_iBurst = 0; // simulated ticks represented since the last EndBurst, not keyframe count
	bool m_bBatchEndpoints = false; // sparse local keyframes, one per completed command batch
	std::deque<std::pair<float, int>> m_vBursts = {}; // recent (realtime, size) of regular bursts

	bool m_bStarted = false;
	float m_flLastArrival = 0.f; // realtime of the last burst
	float m_flClock = 0.f; // estimate of "now" in timeline ticks
	float m_flDelay = 0.f; // current playback delay in ticks
	float m_flTargetDelay = 0.f; // delay we're easing towards
	float m_flCursor = 0.f; // playback position in timeline ticks
};

class CAnimInterp
{
private:
	struct Applied_t
	{
		CTFPlayer* m_pPlayer = nullptr;
		int m_iIndex = 0;
		AnimFrame_t m_tTrue = {}; // the real (simulated) state to put back after drawing
	};

	std::unordered_map<int, AnimTimeline_t> m_mTimelines = {};
	std::vector<Applied_t> m_vApplied = {};
	std::unordered_map<int, CBaseEntity*> m_mDirtyPlayers = {};
	float m_flLastRender = 0.f;
	bool m_bRendering = false;
	bool m_bBuildingFake = false;

	matrix3x4 m_aFakeBones[MAXSTUDIOBONES] = {};
	matrix3x4 m_aOriginalFakeBones[MAXSTUDIOBONES] = {};
	bool m_bFakeApplied = false;

	void Advance(AnimTimeline_t& tTimeline, float flDeltaTicks);
	bool Sample(const AnimTimeline_t& tTimeline, AnimFrame_t& tOut);
	void RebuildFakeAngle(CTFPlayer* pLocal, const AnimFrame_t& tFrame);
	void InvalidateAllBones();

public:
	class FakeBuildScope
	{
		CAnimInterp& m_owner;
		bool m_previous;
	public:
		explicit FakeBuildScope(CAnimInterp& owner) : m_owner(owner), m_previous(owner.m_bBuildingFake) { owner.m_bBuildingFake = true; }
		~FakeBuildScope() { m_owner.m_bBuildingFake = m_previous; }
		FakeBuildScope(const FakeBuildScope&) = delete;
		FakeBuildScope& operator=(const FakeBuildScope&) = delete;
	};
	void Forget(CTFPlayer* pPlayer);
	bool ShouldRebuildBones(CBaseEntity* pEntity);
	void BonesRebuilt(CBaseEntity* pEntity);
	bool Enabled();

	// Remote simulation records per tick. Local simulation records its completed batch only,
	// with elapsed ticks preserved so keyframe count never becomes the animation clock.
	void Record(CTFPlayer* pPlayer, int iElapsedTicks = 1, bool bBatchEndpoint = false);
	void EndBurst(CTFPlayer* pPlayer);

	// render side
	void RenderStart();
	void RenderEnd();
	void Restore();
	void Reset();

	bool ShouldTransition(CBaseEntity* pEntity);
};

ADD_FEATURE(CAnimInterp, AnimInterp);
