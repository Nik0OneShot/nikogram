#pragma once
#include "../../../SDK/SDK.h"
#include <functional>
#include <array>
#include <chrono>
#include "CounterStrafe.h"
#include "../PredictionRestore.h"

namespace MoveSimulationHost { inline CBasePlayer* Current = nullptr; }
// Scoped, diagnostic-only replay controls; never used to select an aim point.
namespace LandingReplay { inline thread_local bool active=false,uncorrected=false,nominalHull=false; inline thread_local int command=0; }

Enum(Move, Ground, Air, Swim)

using RunTickCallback = const std::function<void(CMoveData&)>;

struct MoveStorage
{
	CTFPlayer* m_pPlayer = nullptr;
	CMoveData m_MoveData = {};
	byte* m_pData = nullptr;
	CUserCmd* m_pOriginalCommand = nullptr;
    PredictionRestore::Snapshot m_RestoreSnapshot;

	float m_flAverageYaw = 0.f;
	CounterStrafe::Estimate m_CounterStrafe;
	float m_flCounterTime = 0.f;
    // Learned strafe timing (Auto): movesim keeps the corridor physics and native
    // acceleration; only the lateral reversal times come from the learned model.
    bool m_bLearnedTiming=false;float m_flTimingFirst=0.f,m_flTimingLeg=0.f;int m_iTimingSign=0;
    float m_flTimingBraking=0.f; // native time from reversed wish to the lateral zero crossing
    Vec3 m_vObservedWish={};
    bool m_bObservedWish=false;
    Vec3 m_vDiagnosticStartOrigin={},m_vDiagnosticStartVelocity={};
    bool m_bDiagnosticStartGrounded=false;
	bool m_bLedgeAware = false;
	bool m_bLedgeBraking = false;
    bool m_bLedgeImpulse = false;
    int m_iLedgeLanding = 0; // Native contacts: 1 grounded/landed, -1 airborne, 0 not evaluated
	float m_flLedgeStartTime = 0.f;
    // Ledge hypotheses: hold (actively brake and stay at the edge) or walk off.
    // m_bLedgeEvidence: received slowdown/turn before the edge; m_bLedgeHold: the
    // hypothesis this pass simulates; m_bLedgeSeen: this pass reached an edge.
    bool m_bLedgeEvidence = false, m_bLedgeHold = false, m_bLedgeStopped = false, m_bLedgeSeen = false;
    bool m_bLedgeAheadSupport = true; int m_iLedgeProbeAge = 0; Vec3 m_vLedgeProbeDirection = {};
	bool m_bBunnyHop = false;

	float m_flSimTime = 0.f;
	float m_flPredictedDelta = 0.f;
	float m_flPredictedSimTime = 0.f;
    float m_flDiagnosticNetworkOriginTime = 0.f;
	bool m_bDirectMove = true;

	bool m_bPredictNetworked = true;
	Vec3 m_vPredictedOrigin = {};

	std::vector<Vec3> m_vPath = {};

	bool m_bFailed = false;
	bool m_bInitFailed = false;
	bool m_bInitialized = false; // set once state has been stored, cleared by Restore
};

struct MoveData
{
	Vec3 m_vDirection = {};
	float m_flSimTime = 0.f;
	int m_iMode = 0;
	Vec3 m_vVelocity = {};
	Vec3 m_vOrigin = {};
	bool m_bInputDirection = false;
};

class CMovementSimulation
{
private:
    struct ReuseFrame
    {
        MoveStorage state;
        std::vector<byte> packed;
        Vec3 pathPoint;
        Vec3 absoluteOrigin;
    };
    CBaseEntity* m_pReuseTarget=nullptr;
    std::vector<ReuseFrame> m_vReuseFrames;
    std::vector<byte> m_vReuseInitial,m_vReuseScratch;
    MoveStorage m_ReuseInitial;
    Vec3 m_vReuseInitialAbs;
    size_t m_nReuseCount=0,m_nReuseCursor=0,m_nReuseBytes=0;
    bool m_bReuseChecked=false,m_bReuseAllowed=false;
    bool m_bReuseDisabled=false,m_bReuseValidation=false;
    int m_iReuseTick=0;
    float m_flReuseInterval=0.f;
    unsigned long m_nReuseHandle=0;
    void* m_pReuseMap=nullptr;
    bool PackReuse(MoveStorage& storage,std::vector<byte>& packed);
    bool SameReuseState(const MoveStorage& a,const MoveStorage& b);
    void CopyReuseState(MoveStorage& destination,const MoveStorage& source);
    bool TryReuseTick(MoveStorage& storage,bool path,RunTickCallback* callback);
    void CaptureReuseTick(MoveStorage& storage,bool path,RunTickCallback* callback);
    void BeginReuse(CBaseEntity* target);
    void EndReuse();
	bool Store(MoveStorage& tMoveStorage);
	bool AcceptRecord(CTFPlayer* player, float time, const Vec3& origin, const Vec3& velocity);
    bool HistoryCurrent(CTFPlayer* player) const;
    bool PatternHistoryFresh(CTFPlayer* player) const;
    void StoreCounterHistory(int index,bool continuing);
    bool LedgeEvidence(CTFPlayer* player);
	void Reset(MoveStorage& tMoveStorage);

	void SetupMoveData(MoveStorage& tMoveStorage, bool observedPattern = false);
	bool CheckStuck(MoveStorage& tMoveStorage);
	void PredictLedge(MoveStorage& storage);
	void GetAverageYaw(MoveStorage& tMoveStorage, int iSamples);
	bool StrafePrediction(MoveStorage& tMoveStorage, bool bStrafe = true, bool bHitchance = false);

	void SetBounds(CTFPlayer* pPlayer);
	void RestoreBounds(CTFPlayer* pPlayer);

	std::unordered_map<int, std::deque<MoveData>> m_mRecords = {};
    std::unordered_map<int, std::deque<MoveData>> m_mCounterRecords;
	std::unordered_map<int, std::deque<float>> m_mSimTimes = {};
	struct HistoryIdentity {
        unsigned long handle=0; int spawn=0,server=0;
        bool operator==(const HistoryIdentity&) const = default;
    };
    std::unordered_map<int, HistoryIdentity> m_mIdentities;
    struct MotionReceipt { std::chrono::steady_clock::time_point received{}; Vec3 velocity{}; float time=0; bool valid=false,impulse=false; };
    std::unordered_map<int,MotionReceipt> m_mMotionReceipts;
	std::vector<std::array<Vec3, 4>> m_vBounds;

public:
    // 0 = median learned reversal time, 1 = later guess. Set by the projectile aimbot around a pass.
    int m_iReversalHypothesis=0;bool m_bReversalPlanned=false,m_bAllowLearnedTiming=false;float m_flPlannedFirst=0.f;
    // 0 = ledge hypothesis backed by received evidence (hold only with evidence), 1 = the other one.
    // m_bLedgeEncountered: a pass reached an edge, so the other hypothesis is worth a try.
    int m_iLedgeHypothesis=0;bool m_bLedgeEncountered=false;
    // Hypotheses the ordinary (non-impact) pass actually used; knockback impact and
    // verification passes re-simulate the same world instead of resetting to 0.
    int m_iChosenReversal=0,m_iChosenLedge=0;
    struct ReuseScope
    {
        CMovementSimulation& owner;
        ReuseScope(CMovementSimulation& simulation,CBaseEntity* target):owner(simulation) {owner.BeginReuse(target);}
        ~ReuseScope() {owner.EndReuse();}
        ReuseScope(const ReuseScope&)=delete;
        ReuseScope& operator=(const ReuseScope&)=delete;
    };
    CounterStrafe::Audit DiagnosticCounterAudit(CTFPlayer* player);
	void Clear();
	void Store();
	void StorePlayer(CTFPlayer* pPlayer, CMoveData& tMoveData, float flTime);

	bool Initialize(CBaseEntity* pEntity, MoveStorage& tMoveStorage, bool bHitchance = true, bool bStrafe = true, bool bPredict = true);
	bool SetDuck(MoveStorage& tMoveStorage, bool bDuck);
	void RunTick(MoveStorage& tMoveStorage, bool bPath = true, RunTickCallback* pCallback = nullptr);
	void RunTick(MoveStorage& tMoveStorage, bool bPath, RunTickCallback fCallback);
	void Restore(MoveStorage& tMoveStorage);

	float GetPredictedDelta(CBaseEntity* pEntity);
};

ADD_FEATURE(CMovementSimulation, MoveSim);
