#pragma once
#include "../../../SDK/SDK.h"

#include "../AimbotGlobal/AimbotGlobal.h"
#include "../ProjectilePerformancePolicy.h"
#include "../BowReleasePolicy.h"
#include "../BowChargePlan.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include "../../World/World.h"
#include <array>
#include <mutex>

Enum(PointFlags, None = 0, Regular = 1 << 0, Lob = 1 << 1)
Enum(PointType, Direct, Geometry, Air)
Enum(CalculateFlags, None = 0, TwoPass = 1 << 0, SetupClip = 1 << 1, AccountDrag = 1 << 2, LobAngle = 1 << 3, Accuracy = TwoPass | SetupClip | AccountDrag)
Enum(CalculateResult, Pending, Good, Time, Bad)

struct Info_t
{
	CTFPlayer* m_pLocal = nullptr;
	CTFWeaponBase* m_pWeapon = nullptr;
	Target_t* m_pTarget = nullptr;
	CBaseEntity* m_pProjectile = nullptr;
	CTFWeaponBase* m_pReflector = nullptr; // Source launcher is m_pWeapon; airblast attributes belong here.

	Vec3 m_vLocalEye = {};
	Vec3 m_vTargetEye = {};

	float m_flLatency = 0.f;
	Vec3 m_vHull = {};
	Vec3 m_vOffset = {};
	Vec3 m_vAngFix = {};
	float m_flVelocity = 0.f;
	float m_flGravity = 0.f;
	float m_flRadius = 0.f;
	float m_flRadiusTime = 0.f;
	float m_flBoundsTime = 0.f;
	float m_flOffsetTime = 0.f;
	int m_iSplashRestrict = 0;
	int m_iArmTime = 0;
    float m_flAirArmTime = 0.f; // Physical minimum, independent of ground-placement timing preference.
	float m_flNormalOffset = 0.f;
	bool m_bIgnoreTiming = false;
    bool m_bLegitGuide = false;
    int m_iSelectedSplash=0;
};

#pragma pack(1)
struct Solution_t
{
	float m_flPitch = 0.f;
	float m_flYaw = 0.f;
	float m_flTime = 0.f;
	uint8_t m_iCalculated = CalculateResultEnum::Pending;
};
#pragma pack()

struct Setup_t
{
	Vec3 m_vPoint = {};
	uint8_t m_iType = PointTypeEnum::Geometry;
    uint8_t m_iSource = 0;      // SplashSearchPolicy::Source
    int m_iId = -1;             // stable within its pool
    // Validation history across commands (command numbers, -1 = never).
    int m_iAttempts = 0, m_iLastPass = -1, m_iLastFail = -1;
};
struct Point_t
{
	Vec3 m_vPoint = {};
	Solution_t m_tSolution = {};
	uint8_t m_iType = PointTypeEnum::Direct;
    size_t m_iSearchOrder = 0; // Deterministic ties in the bounded splash shortlist.
    int m_iSetupId = -1;       // Setup_t::m_iId this point was solved from
    int m_iPriority = 1, m_iAttempts = 0, m_iLastFail = -1; // SplashSearchPolicy ranking inputs
};

// Splash candidates for one target and weapon. Regenerated every command;
// candidates that passed recently are carried over (seeded) so the aim point
// stays steady, and pass/fail history drives the validation order.
struct SplashPool_t
{
    static constexpr size_t MaxSetups = 512;
    int m_iTarget = 0, m_iWeapon = 0, m_iMode = -1;
    uint8_t m_iFlags = 0;
    int8_t m_iSide = 0;        // sign of the launch offset's lateral component
    float m_flRadius = 0.f;
    Vec3 m_vGeneratedAt = {}, m_vLocalEye = {};
    int m_iGeneratedCommand = -1, m_iLastCommand = -1, m_iNextId = 0;
    std::vector<Setup_t> m_vSetups;

    // Dedupes against existing candidates; returns false when nothing was added.
    bool Add(const Vec3& vPoint, uint8_t iType, uint8_t iSource)
    {
        if (m_vSetups.size() >= MaxSetups) return false;
        for (const auto& tSetup : m_vSetups)
            if (tSetup.m_iType == iType && tSetup.m_vPoint.DistToSqr(vPoint) < 0.25f) return false;
        Setup_t tSetup; tSetup.m_vPoint = vPoint; tSetup.m_iType = iType; tSetup.m_iSource = iSource; tSetup.m_iId = m_iNextId++;
        m_vSetups.push_back(tSetup);
        return true;
    }
    Setup_t* Find(int iId)
    {
        for (auto& tSetup : m_vSetups) if (tSetup.m_iId == iId) return &tSetup;
        return nullptr;
    }
};

struct Offset_t
{
	Vec3 m_vOffset;
	uint8_t m_iFlags;
    bool m_bDeferred = false; // Additional feet sample never precedes original generation.
};
using Directs_t = std::unordered_map<uint8_t, Offset_t>;
using Splashes_t = std::vector<uint8_t>;

struct History_t
{
	Vec3 m_vOrigin;
	int m_iSimtime;
    float m_flObservationTime=0.f;
    bool m_bObservationGround=false;
    bool m_bNeural=false;
#ifdef NIKOGRAM_PRIVATE_LEARNING
    float m_flDiagnosticSim=0, m_flDiagnosticNetworkSim=0;
    bool m_bDiagnosticGround=false;
#endif
};
struct Direct_t : History_t
{
	float m_flPitch;
	float m_flYaw;
	float m_flTime;
	Vec3 m_vPoint;
	int m_iPriority;
};
struct Splash_t : History_t
{
	float m_flTimeTo;
};
using DirectHistory_t = std::unordered_map<uint8_t, std::vector<Direct_t>>;
using SplashHistory_t = std::unordered_map<uint8_t, std::vector<Splash_t>>;

class CAimbotProjectile
{
private:
	Directs_t GetDirects();
	Splashes_t GetSplashes();
	void SetupSplashPoints(Vec3& vOrigin, SplashPool_t& tPool, uint8_t iFlags, int searchMode);
    SplashPool_t& AcquireSplashPool(int searchMode, uint8_t iFlags, Vec3& vGeneratedAt, int iCommand);
    static void RecordSplashResult(SplashPool_t& tPool, int iSetupId, bool bPassed, int iCommand);
    struct SplashStreak_t { int m_iTarget = -1, m_iStartCommand = -1, m_iLastCommand = -1; bool m_bLocked = false; } m_SplashStreak; // diagnostics only
    // 0.7.0.47: last splash candidate that reached its surface but whose explosion was blocked (set by TestAngle).
    struct SplashMiss_t { bool m_bValid = false; Vec3 m_vImpact = {}, m_vNormal = {}; } m_tSplashMiss;
    // 0.7.0.49: fine lattice scan around a recurring near miss, continued across commands.
    struct SplashRefine_t { int m_iTarget = -1, m_iLastCommand = -1, m_iNext = 0; Vec3 m_vCenter = {}, m_vNormal = {}, m_vU = {}, m_vV = {}; } m_tSplashRefine;
    void GetSplashPoints(Vec3 vOrigin, std::vector<Setup_t>& vSplashPoints, std::vector<Point_t>& vPoints, int iSimTime, uint8_t iFlags = CalculateFlagsEnum::Accuracy, bool bFirst = false, int* batchSize = nullptr);

	void CalculateAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, int iSimTime, Solution_t& tOut, uint8_t iFlags = CalculateFlagsEnum::Accuracy, int iTolerance = -1);
	bool TestAngle(const Vec3& vPoint, const Vec3& vAngles, int iSimTime, uint8_t iType, uint8_t iFlags, bool bSecondTest = false);

	bool HandlePoint(const Vec3& vOrigin, int iSimTime, float flPitch, float flYaw, float flTime, const Vec3& vPoint, uint8_t iType = PointTypeEnum::Direct, uint8_t iFlags = PointFlagsEnum::Regular, bool learned = false);
	bool HandleDirect(DirectHistory_t& vDirectHistory, size_t* diagnosticTests = nullptr);
	bool HandleSplash(SplashHistory_t& vSplashHistory);
#ifdef NIKOGRAM_PRIVATE_LEARNING
    void SnapshotSelectedPath(const History_t& history,float flight,uint8_t type,uint8_t flags,const Vec3& targetPoint);
    struct DiagnosticQuery
    {
        Vec3 start,end,mins,maxs,targetOrigin,targetMins,targetMaxs;
        CTraceFilterCollideable filter;CGameTrace result;int mask=0,kind=0;bool hull=false;
    };
    std::vector<DiagnosticQuery> m_DiagnosticScratch,m_DiagnosticSelected;
    size_t m_DiagnosticScratchCount=0,m_DiagnosticSelectedCount=0;
    bool m_DiagnosticRecording=false,m_DiagnosticSelectedSupported=false;
    int m_DiagnosticSelectedType=0;
    void RecordDiagnosticQuery(const Vec3& start,const Vec3& end,const Vec3& mins,const Vec3& maxs,int mask,const CTraceFilterCollideable& filter,const CGameTrace& result,int kind,bool hull);
    void ReplayFinalPath();
#endif

	int CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool bUpdate = true);
    bool m_bNeuralSelected=false;
    bool m_bNeuralForced=false;
    std::vector<Vec3> m_vNeuralPath;
    std::vector<int> m_vNeuralFlightTicks;
    int CanHitSelected(Target_t&,CTFPlayer*,CTFWeaponBase*,bool);
    int CanHitForced(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool bUpdate);
    int CanHitOrdinary(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool bUpdate);
    int CanHitOrdinaryBase(Target_t&,CTFPlayer*,CTFWeaponBase*,bool);
    bool m_bImpactConsidered=false,m_bImpactPass=false;
    float m_flImpactReserve=0;
    std::optional<Vec3> m_ImpactFixedAngle;
    int CanHitPass(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool bUpdate);
    bool CandidateAngleAllowed(const Vec3& angle,const Vec3& point,const Vec3& origin);
    bool m_bAdaptivePass=false, m_bAdaptiveRetained=false;
    bool m_bReuseCooldownPreview=false;
    std::optional<Vec3> m_vCooldownPreviewPoint;
    AimPoint_t m_CooldownDisplayPoint;
    std::vector<DrawBox_t> m_vCooldownPreviewBoxes;
    float m_flAdaptiveTargetFov=180.f,m_flAdaptiveDistance=10000.f;
    int m_iAdaptiveEntity=-1,m_iAdaptiveTick=-1000;
    Vec3 m_vAdaptiveMins={},m_vAdaptiveMaxs={};
	bool RunMain(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);

	bool CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pProjectile);
	bool TestAngle(CBaseEntity* pProjectile, const Vec3& vPoint, Vec3& vAngles, int iSimTime, uint8_t iType, uint8_t iFlags);

	bool Aim(const Vec3& vCurAngle, const Vec3& vToAngle, Vec3& vOut, int iMethod = Vars::Aimbot::General::AimType.Value);
	void Aim(CUserCmd* pCmd, Vec3& vAngles, int iMethod = Vars::Aimbot::General::AimType.Value);

	Info_t m_tInfo = {};
    ProjectilePerformancePolicy::SearchBudget m_SearchBudget;
    ProjectilePerformancePolicy::SearchBudget m_DynamicSplashBudget;
	MoveStorage m_tMoveStorage = {};
	ProjectileInfo m_tProjInfo = {};
    std::array<SplashPool_t, 8> m_SplashPools = {};
    SplashPool_t m_StickyScratchPool = {}; // later-history sticky regeneration, never persisted
    uint64_t m_iSplashPoolEpoch = 0;
    std::vector<float> m_vSplashDistances;

	bool m_bLastTickHeld = false;
    BowReleasePolicy::Ownership m_BowOwnership;
    BowChargePlan::Plan m_BowPlan;
    bool m_bBowPlanValidated=false;
    BowReleasePolicy::ReleaseCommit m_BowReleaseCommit;
    bool m_bBowValidated = false;
    BowReleasePolicy::Ownership m_NeuralChargeOwnership;
    bool m_bNeuralChargeValidated = false;

	float m_flTimeTo = std::numeric_limits<float>::max();
	std::vector<Vec3> m_vPlayerPath = {};
	std::vector<Vec3> m_vProjectilePath = {};
	std::vector<DrawBox_t> m_vBoxes = {};

	Vec3 m_vAngleTo = {};
	Vec3 m_vPredicted = {};
	Vec3 m_vTarget = {};

	int m_iResult = false;
	bool m_bUpdate = true;
	bool m_bPreviewOnly = false;
    bool m_bLegitGuide = false;
    struct Guide
    {
        float realtime=-1,chargeBegin=0;
        int spawn=-1,server=-1,targetSpawn=-1;
        const model_t* targetModel=nullptr;
        Vec3 focusOffset;
        unsigned target=0, weapon=0, owner=0;
        Vec3 marker, targetCue, impact, launchAngles;
        float markerDistance=128;
        bool valid=false, uncertain=false, aligned=false, impactValid=false;
        std::vector<Vec3> path;
    };
    Guide m_guide;
    Guide m_renderPath;
    struct GuideDisplay
    {
        Vec3 direction;
        unsigned target=0,owner=0,weapon=0;
        int frame=-1,spawn=-1,server=-1;
        float realtime=-1,chargeBegin=0;
    } m_guideDisplay;
    int m_guidePathFrame=-1;
    Vec3 m_vGuideOffset;
    void RefreshGuidePath(CTFPlayer* local,CTFWeaponBase* weapon);
    std::mutex m_guideMutex;
    bool m_bSearchedThisCommand = false;
    bool m_bWorldBlocked=false;
    float m_flObservationFlight=0.f;
    float m_flObservationTime=0.f;
    bool m_bObservationGround=false;
    bool m_bObservationStartGround=false;
    unsigned long m_nObservationHandle=0;
    int m_iObservationEntity=-1;

public:
    void FinalizeBowCharge(CTFPlayer*,CTFWeaponBase*,CUserCmd*);
	void MaintainBowCharge(CTFPlayer* local, CTFWeaponBase* weapon, CUserCmd* cmd);
    void ObservePills();
	void UpdateLegitGuide(CTFPlayer* local, CTFWeaponBase* weapon, const CUserCmd* cmd);
	void DrawLegitGuide(CTFPlayer* local);
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
    bool ManageViewmodel(CTFPlayer* pLocal,CTFWeaponBase* pWeapon,CUserCmd* pCmd,bool noTargets=false);
	float GetSplashRadius(CTFWeaponBase* pWeapon, CTFPlayer* pPlayer, float flScale = 1.f);

	bool AutoAirblast(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, CBaseEntity* pProjectile);
	float GetSplashRadius(CBaseEntity* pProjectile, CTFWeaponBase* pWeapon = nullptr, CTFPlayer* pPlayer = nullptr, float flScale = 1.f);

	int m_iLastTickCancel = 0;
    // Sticky "Charge weapon" ownership: only charges this aimbot started.
    static constexpr int StickyChargeFailCommands = 8;
    bool m_bStickyChargeOwned = false;
    int m_iStickyHoldCommand = 0, m_iStickyChargeFails = 0;
    // Huntsman: consecutive real (not budget-cut) failures before a draw is cancelled.
    static constexpr int BowFailCommands = 8, BowLimitedFailCommands = 30;
    int m_iBowFailures = 0, m_iBowLimitedFailures = 0;
    bool BowHoldForCharge(Target_t& target, CTFPlayer* local, CTFWeaponBase* weapon, float begin);
};

ADD_FEATURE(CAimbotProjectile, AimbotProjectile);
