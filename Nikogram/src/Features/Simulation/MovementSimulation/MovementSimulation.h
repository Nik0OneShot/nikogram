#pragma once
#include "../../../SDK/SDK.h"
#include <functional>
#include <array>
#include "CounterStrafe.h"

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

	float m_flAverageYaw = 0.f;
	CounterStrafe::Estimate m_CounterStrafe;
	float m_flCounterTime = 0.f;
    Vec3 m_vDiagnosticStartOrigin={},m_vDiagnosticStartVelocity={};
    bool m_bDiagnosticStartGrounded=false;
	bool m_bLedgeAware = false;
	bool m_bLedgeBraking = false;
	float m_flLedgeStartTime = 0.f;
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
	bool Store(MoveStorage& tMoveStorage);
	bool AcceptRecord(CTFPlayer* player, float time, const Vec3& origin, const Vec3& velocity);
    void StoreCounterHistory(int index);
	void Reset(MoveStorage& tMoveStorage);

	void SetupMoveData(MoveStorage& tMoveStorage);
	bool CheckStuck(MoveStorage& tMoveStorage);
	void PredictLedge(MoveStorage& storage);
	void GetAverageYaw(MoveStorage& tMoveStorage, int iSamples);
	bool StrafePrediction(MoveStorage& tMoveStorage, bool bStrafe = true, bool bHitchance = false);

	void SetBounds(CTFPlayer* pPlayer);
	void RestoreBounds(CTFPlayer* pPlayer);

	std::unordered_map<int, std::deque<MoveData>> m_mRecords = {};
    std::unordered_map<int, std::deque<MoveData>> m_mCounterRecords;
	std::unordered_map<int, std::deque<float>> m_mSimTimes = {};
	std::unordered_map<int, unsigned long> m_mIdentities;
	std::vector<std::array<Vec3, 4>> m_vBounds;

public:
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
