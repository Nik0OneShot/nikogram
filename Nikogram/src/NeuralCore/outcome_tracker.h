#pragma once
#include "trajectory.h"
#include <array>
#include <span>

// Client-observed attribution, never server-authoritative proof. No native calls or IO.
namespace movement::outcomes {
using Handle=uint32_t;
enum class Confidence : unsigned { Unknown=0, InferredTerminal=1, DirectInflictorKill=2 };
enum class Reason : unsigned { Complete, Warmup, InvalidInput, ContextChanged, Overlap, Gap,
 AmbiguousSpawn, SpawnTimeout, Reflection, TargetUnavailable, Spy, Healing, DamageConflict,
 MissingTerminal, AmbiguousExplosion, EventMismatch, UnknownDeath, Overflow, Cancelled };
const char* Name(Confidence) noexcept;
const char* Name(Reason) noexcept;
struct Rocket { Handle handle=0,owner=0,launcher=0; int index=0,deflected=0; bool dormant=false; Vec position{},velocity{}; };
struct Target { Handle handle=0; int user=0,health=0,playerClass=0; bool present=false,dormant=false,alive=false; Vec position{}; };
struct Batch {
 double time=0; bool complete=false,lossFree=false,eligibleSoldierLauncher=false;
 Handle local=0,weapon=0; int weaponID=0,itemDef=0;
 // Measured incoming+outgoing delay+interpolation. Core floors settling at .5s, rejects >2s.
 float settleDelay=.5f; Target target{}; std::span<const Rocket> rockets{};
};
struct Launch {
 double time=0; Handle local=0,weapon=0,target=0; int targetUser=0,weaponID=0,itemDef=0;
 Vec muzzle{}; Target targetState{}; float blastRadius=200; bool selected=false;
 trajectory::Outcome chosen{};
};
struct Explosion { double time=0; Vec position{}; int weaponID=0,itemDef=0; };
struct Hurt { double time=0; Handle attacker=0; int victimUser=0,weaponID=0,damage=0,health=0;
 Vec position{}; bool reliable=false; };
struct Death { double time=0; int victimUser=0,inflictorIndex=0; bool feign=false,reliable=false; };
struct Completed { trajectory::Outcome outcome{}; Confidence confidence=Confidence::Unknown;
 Reason reason=Reason::InvalidInput; Handle rocket=0,target=0; int targetUser=0; bool reliableEvents=false; };
class Tracker {
 struct State {
  Launch launch{}; Rocket rocket{}; Target target{}; bool bound=false,removed=false,terminal=false,dead=false;
  double lastRocketTime=0,terminalTime=0,removedTime=0,lastRelevant=0,lastBatch=0;
  Vec explosion{}; int initialHealth=0,totalDamage=0,lastEventHealth=0; unsigned postBatches=0,hurtCount=0;
  bool reliable=true; float settle=.5f,flightLimit=76.8f;
  std::array<Hurt,8> hurts{};
 } state{};
 bool active=false,haveBatch=false; Batch previous{}; // previous.rockets always empty (no borrowed spans retained).
 unsigned quietBatches=0; double lastTime=0; uint64_t lastChosenShot=0;
 std::array<Completed,8> outputs{}; unsigned outputRead=0,outputCount=0; uint64_t dropped=0;
 void Finish(Reason,Confidence=Confidence::Unknown) noexcept;
 bool Context(const Batch&)const noexcept;
 bool Clock(double)noexcept;
 void Settle(double)noexcept;
public:
 void Observe(const Batch&)noexcept;
 // Call for EVERY committed local native discharge, including manual/unselected shots.
 // Selected chosen.shot IDs must strictly increase for this Tracker lifetime; repeats are not re-emitted.
 void NativeFire(const Launch&)noexcept;
 void OnExplosion(const Explosion&)noexcept;
 void OnHurt(const Hurt&)noexcept;
 void OnDeath(const Death&)noexcept;
 void Poll(double now)noexcept;
 void Reset(Reason reason=Reason::Cancelled)noexcept;
 bool Take(Completed&)noexcept;
 Handle BoundHandle()const noexcept{return active&&state.bound?state.rocket.handle:0;}
 bool Pending()const noexcept{return active;}
 uint64_t Dropped()const noexcept{return dropped;}
};
}

