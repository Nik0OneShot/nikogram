#pragma once
#include "../../SDK/SDK.h"
#include "../Simulation/PredictionRestore.h"

struct DatamapRestore_t
{
	byte* m_pData = nullptr;
	size_t m_iSize = 0;
};

struct RestoreInfo_t
{
	Vec3 m_vOrigin = {};
	Vec3 m_vMins = {};
	Vec3 m_vMaxs = {};
};

class CEnginePrediction
{
private:
	void Simulate(CTFPlayer* pLocal, CUserCmd* pCmd);

	CMoveData m_tMoveData = {};
	DatamapRestore_t m_tLocal = {};
    PredictionRestore::Snapshot m_RestoreSnapshot;
    CTFPlayer* m_pRestorePlayer=nullptr;
    bool m_bRestorePending=false;
	std::unordered_map<CTFPlayer*, RestoreInfo_t> m_mRestore = {};

public:
	void Start(CTFPlayer* pLocal, CUserCmd* pCmd);
	void End(CTFPlayer* pLocal, CUserCmd* pCmd);

	void Unload();

	void AdjustPlayers(CBaseEntity* pLocal);
	void RestorePlayers();

	bool m_bInPrediction = false;

	int m_nOldTickCount = 0;
	float m_flOldCurrentTime = 0.f;
	float m_flOldFrameTime = 0.f;
};

ADD_FEATURE(CEnginePrediction, EnginePrediction);