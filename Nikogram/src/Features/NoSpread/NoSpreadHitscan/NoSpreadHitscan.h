#pragma once
#include "../../../SDK/SDK.h"
#include "SeedClockPolicy.h"
#include "BurstPlanPolicy.h"

//#define SEEDPRED_DEBUG

// Hitscan no-spread: predicts the server's bullet seed for the command being built.
// The server stamps every usercmd with float(Plat_FloatTime()*1000) when it reads
// our packet and uses the low byte as the seed. The stamp is tied to the server
// frame that reads the packet, so the prediction works in server-tick space:
//   - FRAME_NET_UPDATE_END: which server tick acknowledged which command of ours
//   - playerperf (second block): ProcessUsercmds stamps of our recent packets
//   - FX_FireBullets for other humans: the real seed the server used in a tick
// See SeedClockPolicy.h for the model.
//
// The seed is settled when the packet is actually written (FinalizePacket, from
// CNetChannel::SendNetMsg), not when a command is built: only then are the packet,
// its send time and its engine tick known exactly. Run applies a provisional
// correction (the client's own predicted shot is drawn with it) and records the
// shot; FinalizePacket re-plans every shot of the packet with the packet's seed and
// the server's aim hold (BurstPlanPolicy.h) and rewrites the commands before they
// are serialized.
class CNoSpreadHitscan
{
private:
	bool ShouldRun(CTFWeaponBase* pWeapon = nullptr);
	enum class SeedMode { Unknown, Command, Time };
	SeedMode GetSeedMode();
	std::string GetFormat(int iServerTime);
	void FinalizeReply(double dNow, bool bForce);
	bool AcksFresh(double dNow) const;
	bool StepUsable() const;
	bool Loopback() const;
	double LoopbackMs() const;         // the shared clock as the listen server will stamp it
	bool FixedPattern(CTFWeaponBase* pWeapon, int iBulletsPerShot);
	void Log(const char* sEvent, const std::string& sMessage);
	int HoldAimTicks();                 // sv_maxusrcmdprocessticks_holdaim (listen server), 1 elsewhere

	SeedClockPolicy::FrameClock m_Clock{};
	std::deque<SeedClockPolicy::SentPacket> m_dSent{};          // our packets, newest last
	std::vector<SeedClockPolicy::ReplyLine> m_vReplyLines{};    // current reply, newest first
	bool m_bRequestPending = false;     // "playerperf" queued, waits for the packet that carries it
	bool m_bWaitingForReply = false;
	int m_iRequestCmd = -1;             // command number of the packet carrying the request
	double m_dRequestTime = 0.0;
	double m_dLastChunkTime = 0.0;
	double m_dLastReplyTime = 0.0;
	double m_dLastAckTime = 0.0;
	double m_dLastEchoTime = 0.0;
	int m_iSnapshotsThisFrame = 0;
	int m_iLastServerTick = -1;
	int m_iLastOffset = std::numeric_limits<int>::min();
	int m_iEchoesSeen = 0, m_iEchoesHuman = 0;
	int m_iRepliesSeen = 0, m_iRepliesAligned = 0, m_iRejectedReplies = 0;
	// Listen server: the server stamps our packet in the same host frame, so the shared
	// Plat_FloatTime plus the measured send-to-read delay is the stamp itself.
	std::deque<double> m_dLoopbackDeltas{};
	double m_dLoopbackDeltaMs = 0.2;

	// Client engine ticks (CL_Move calls), the clock packets are counted in for the
	// send-tick offset; doubletap does not move it.
	int m_iEngineTick = 0;
	// FinalizePacket's time and engine tick for the packet it settled, for NotePacketSent.
	int m_iFinalizedCmd = -1;
	// Engine tick in which a packet with a shot left: a later packet in the same tick (a
	// burst longer than one packet) is run in the same server tick, aim held.
	int m_iHoldEngineTick = -1;
	// Listen server: every packet of an engine tick is read after the LAST one is sent (the
	// server frame runs after CL_Move), so an earlier packet of a long doubletap is stamped
	// when the tail of the burst has been sent. Measured: ms per command still to be made.
	double m_dTailMsPerCmd = 0.16;
	int m_iTailTick = -1, m_iTailFirstRemaining = 0;
	double m_dTailFirstMs = 0.0;
	static int RemainingThisTick();     // commands the tick-shift loop will still make in this engine tick
	double m_dFinalizedMs = 0.0;
	int m_iPacketsFinalized = 0, m_iShotsResolved = 0, m_iSeedsChanged = 0;

	// A firing command waiting for its packet: everything needed to re-solve it there.
	struct PendingShot
	{
		int cmd = -1;
		int seedRun = 0;                // provisional seed Run used (or the final one, once settled)
		int commandSeed = 0;            // cmd->random_seed & 255 (command seed mode)
		Vec3 wantedView = {};           // the aimbot's view before any correction
		Vec3 viewRun = {};              // the view the command holds after our last write
		Vec3 punch = {};
		Vec3 eye = {};
		float spread = 0.f;
		float firstVariance = 0.5f;     // pellet 0's variance (0 = exact first shot)
		float range = 8192.f;
		int bullets = 1;
		int fireTicks = 1;              // ticks between shots (for shots still to come in a burst)
		bool perfectFirst = false;
		bool fixedPattern = false;
		// Pattern placement target (the hitscan aimbot's record for this command).
		CBaseEntity* pTarget = nullptr;
		int targetIndex = 0;
		// matrix3x4 is a raw float[3][4] array, which a vector cannot hold: same layout, wrapped.
		struct Bone { float m[3][4]; };
		std::vector<Bone> bones;
		const matrix3x4* Bones() const { return reinterpret_cast<const matrix3x4*>(bones.data()); }
	};
	std::deque<PendingShot> m_dPending{};
	static void BuildPattern(const PendingShot& tShot, int iSeed, std::vector<BurstPlanPolicy::Pellet>& vOut);
	// Plans `vShots` (one locked group, or a single shot) for the given seeds; writes the
	// chosen view (without punch) to `vView`.
	BurstPlanPolicy::Plan PlanShots(CTFPlayer* pLocal, const std::vector<const PendingShot*>& vShots, const std::vector<int>& vSeeds, Vec3& vView) const;

public:
	Vec2 m_vIndicatorSize = {100, 40};
	void Reset();

	void AskForPlayerPerf();                       // CreateMove, after the features
	void NotePacketSent(int iCommand, int iNewCommands); // CNetChannel::SendNetMsg(clc_Move), after queueing
	void FinalizePacket(int iCommand, int iNewCommands); // CNetChannel::SendNetMsg(clc_Move), before the commands are written
	void OnEngineTick() { m_iEngineTick++; }        // CL_Move (once per engine tick)
	bool ParsePlayerPerf(const std::string& sMsg);  // TextMsg, whole message (several lines)
	void FrameStart();                              // FRAME_START
	void ObserveNetUpdate();                        // FRAME_NET_UPDATE_END
	void ObserveEcho(int iPlayer, int iSeed);       // FX_FireBullets for another player

	// The hitscan aimbot's target for this command: entity, the record's bones the
	// server will be rewound to, and the point it aimed at.
	void SetTarget(CBaseEntity* pEntity, const matrix3x4* aBones, const Vec3& vAimPoint, int iCommand);

	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Draw(CTFPlayer* pLocal);

	int m_iSeed = 0;
	float m_flMantissaStep = 0.f;
	// The command the seed was predicted for. The FX_FireBullets hook paints our own
	// predicted shot with m_iSeed only while it belongs to the command being built;
	// a stale seed would draw the same pattern every shot and mean nothing.
	int m_iSeedCommand = -1;

private:
	struct PatternTarget
	{
		CBaseEntity* pEntity = nullptr;
		int iEntityIndex = 0;
		const matrix3x4* aBones = nullptr;
		Vec3 vAimPoint = {};
		int iCommand = -1;
	} m_tTarget{};
	// View such that pellet (x, y) leaves along `vWantedBase` (angles including punch);
	// returns the base angles (view + punch) after a few fixed-point passes.
	static Vec3 SolvePellet(const Vec3& vWantedBase, float x, float y, float flSpread);
	int CountHits(CTFPlayer* pLocal, CBaseEntity* pTarget, const matrix3x4* aBones, const Vec3& vEye, const Vec3& vBase,
		const std::vector<std::pair<float, float>>& vPellets, float flSpread, float flRange, bool bTrace, std::vector<bool>* pHitMask) const;
public:
	bool SeedActive() const { return G::CurrentUserCmd && m_iSeedCommand == G::CurrentUserCmd->command_number; }
};

ADD_FEATURE(CNoSpreadHitscan, NoSpreadHitscan);
