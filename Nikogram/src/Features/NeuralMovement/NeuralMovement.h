#pragma once
#include "../../NeuralCore/movement.h"
#include "../../NeuralCore/trajectory.h"
#include "../../NeuralCore/strafe.h"
#include <array>
#include "../Aimbot/ObserverPolicy.h"
class CUserCmd; class CTFPlayer; class CTFWeaponBase; class IGameEvent;
namespace movement::outcomes {struct Completed;}
namespace NeuralMovement {
// Read-only observer snapshot; does not initialize services or spend aim budgets.
struct ObserverModel {
    movement::Anchor anchor{};
    std::shared_ptr<const movement::Pool> pool,candidates;
    movement::trajectory::Distribution distribution{};
    movement::trajectory::Policy policy{};
    AimObservation::Availability state=AimObservation::Availability::NoModel;
    size_t branch=0;float probability=0,radius=0;bool experimental=false;
};
ObserverModel ObserverBegin(CTFPlayer*,CTFWeaponBase*,double time,const movement::Vec& origin);
bool ObserverPoint(const ObserverModel&,double time,const movement::Vec& ordinary,movement::Vec& out);
void TrackedOutcome(const movement::outcomes::Completed&);
bool HasIndividualTraining();
void Frame();
void ObserveNetwork();
void Draw();
int LearnedSplash(CTFPlayer*,CTFWeaponBase*,bool*evidence=nullptr);
movement::trajectory::WeaponContext ShooterWeaponContext(CTFPlayer*,CTFWeaponBase*);
void ShotOpportunity(CTFPlayer*,CTFWeaponBase*,int mode,int command);
void CommitOpportunity(CTFPlayer*,CTFWeaponBase*,CUserCmd*);
void ShotFired(CTFPlayer*,CTFWeaponBase*);
void DrawPlayer(uint32_t account,bool fake);
bool PoliciesReady();
bool TargetForced(CTFPlayer*);
void FallbackResult(bool available);
bool TargetEnabled(CTFPlayer*);
bool TargetFallback(CTFPlayer*);
bool HasIndividualForce();
movement::trajectory::Policy TargetPolicy(CTFPlayer*);
void Shutdown();
void ResetSession();
struct Context {
    movement::Baseline baseline;
    std::shared_ptr<const movement::Pool> pool;
    std::shared_ptr<const movement::Pool> candidates;
    bool trial=false,valid=false,changed=false,failed=false,invalidated=false,lastLearned=false;
    movement::Point last{};
    int entity=0,spawn=-1,server=-1;uint32_t handle=0;uint64_t session=0,account=0;
    movement::trajectory::Distribution distribution;
    size_t branch=0;
    float probability=1.f,uncertainty=0.f;
    movement::trajectory::Policy policy;
};
// Ordinary-solve baseline recording for legacy (v1) learning. Never changes the aim.
Context Begin(CTFPlayer*,CTFWeaponBase*);
void End(Context&);
void RecordPoint(Context&,double time,const movement::Vec& ordinary);
void OrdinaryKept();
bool ForceEnabled();
bool AimingEnabled();
// Learned strafe timing for movesim (Auto, accepted model, ground strafes only).
// hypothesis 0 = median reversal time, 1 = later guess. first/leg in seconds from the latest snapshot.
// direction = the current leg's world-space lateral direction (unit), for agreement checks.
bool ReversalPlan(CTFPlayer*,int hypothesis,float& first,float& leg,float& directionX,float& directionY);
const char* ReversalReason();
bool SupportsProjectile(CTFWeaponBase*);
Context BeginForced(CTFPlayer*, CTFWeaponBase*);
bool ForcedPoint(Context&, double absoluteTime, movement::Vec& out, CTFPlayer*);
void ForcedResult(bool applied, const char* detail = nullptr);
}
