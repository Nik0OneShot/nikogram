#pragma once
#include "../../../SDK/SDK.h"

#include "../AimbotGlobal/AimbotGlobal.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"

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
	float m_flNormalOffset = 0.f;
	bool m_bIgnoreTiming = false;
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
};
struct Point_t
{
	Vec3 m_vPoint = {};
	Solution_t m_tSolution = {};
	uint8_t m_iType = PointTypeEnum::Direct;
};

struct Offset_t
{
	Vec3 m_vOffset;
	uint8_t m_iFlags;
};
using Directs_t = std::unordered_map<uint8_t, Offset_t>;
using Splashes_t = std::vector<uint8_t>;

struct History_t
{
	Vec3 m_vOrigin;
	int m_iSimtime;
    float m_flObservationTime=0.f;
    bool m_bObservationGround=false;
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
	void SetupSplashPoints(Vec3& vOrigin, std::vector<Setup_t>& vSplashPoints, uint8_t iFlags = CalculateFlagsEnum::None);
	std::vector<Point_t> GetSplashPoints(Vec3 vOrigin, std::vector<Setup_t>& vSplashPoints, int iSimTime, uint8_t iFlags = CalculateFlagsEnum::Accuracy, bool bFirst = false);

	void CalculateAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, int iSimTime, Solution_t& tOut, uint8_t iFlags = CalculateFlagsEnum::Accuracy, int iTolerance = -1);
	bool TestAngle(const Vec3& vPoint, const Vec3& vAngles, int iSimTime, uint8_t iType, uint8_t iFlags, bool bSecondTest = false);

	bool HandlePoint(const Vec3& vOrigin, int iSimTime, float flPitch, float flYaw, float flTime, const Vec3& vPoint, uint8_t iType = PointTypeEnum::Direct, uint8_t iFlags = PointFlagsEnum::Regular);
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
    int CanHitPass(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool bUpdate);
    bool CandidateAngleAllowed(const Vec3& angle,const Vec3& point,const Vec3& origin);
    bool m_bAdaptivePass=false, m_bAdaptiveRetained=false;
    float m_flAdaptiveTargetFov=180.f,m_flAdaptiveDistance=10000.f;
    int m_iAdaptiveEntity=-1,m_iAdaptiveTick=-1000;
    Vec3 m_vAdaptiveMins={},m_vAdaptiveMaxs={};
	bool RunMain(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);

	bool CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pProjectile);
	bool TestAngle(CBaseEntity* pProjectile, const Vec3& vPoint, Vec3& vAngles, int iSimTime, uint8_t iType, uint8_t iFlags);

	bool Aim(const Vec3& vCurAngle, const Vec3& vToAngle, Vec3& vOut, int iMethod = Vars::Aimbot::General::AimType.Value);
	void Aim(CUserCmd* pCmd, Vec3& vAngles, int iMethod = Vars::Aimbot::General::AimType.Value);

	Info_t m_tInfo = {};
	MoveStorage m_tMoveStorage = {};
	ProjectileInfo m_tProjInfo = {};
	std::vector<Setup_t> m_vSplashPoints = {};

	bool m_bLastTickHeld = false;

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
    float m_flObservationFlight=0.f;
    float m_flObservationTime=0.f;
    bool m_bObservationGround=false;
    bool m_bObservationStartGround=false;
    unsigned long m_nObservationHandle=0;
    int m_iObservationEntity=-1;

public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	float GetSplashRadius(CTFWeaponBase* pWeapon, CTFPlayer* pPlayer, float flScale = 1.f);

	bool AutoAirblast(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, CBaseEntity* pProjectile);
	float GetSplashRadius(CBaseEntity* pProjectile, CTFWeaponBase* pWeapon = nullptr, CTFPlayer* pPlayer = nullptr, float flScale = 1.f);

	int m_iLastTickCancel = 0;
};

ADD_FEATURE(CAimbotProjectile, AimbotProjectile);
