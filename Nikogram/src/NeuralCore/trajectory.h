#pragma once
#include "movement.h"
#include "incoming_cues.h"
#include "strafe.h"
#include <map>
#include <span>
#include <set>
#include <string>
#include <tuple>

// Standalone context/sequence format. Version 1 movement files are never rewritten.
namespace movement::trajectory {
constexpr int Schema=3, LegacyFeatures=80, FeaturesCount=144, HiddenCount=24, Branches=4, Knots=10;
constexpr std::array<float,Knots> Horizons{.15f,.3f,.6f,1.2f,2.4f,4.8f,9.6f,19.2f,38.4f,76.8f};
using Input=std::array<float,FeaturesCount>;
// Schema 4 ("frame-v1"): data rows keep the full v3 context plus Extra; the model
// sees them rotated into the player's own movement frame, and predicts only the
// first FrameKnots knots (0.15-1.2 s). v2/v3 rows convert exactly (Extra unknown).
constexpr int FrameSchema=4, ExtraCount=10, FrameFeatures=123, FrameKnots=4;
// Raw received values, world frame. Every *Known flag is 0 or 1; unknown values stay 0.
struct Extra {
 float recorded=0,maxSpeed=0,maxSpeedKnown=0,shooterKnown=0,hostile=0,dx=0,dy=0,dz=0,yaw=0,yawKnown=0; // d* = shooter eye - target origin; yaw = target eye yaw (deg)
 std::array<float,ExtraCount> Values()const{return {recorded,maxSpeed,maxSpeedKnown,shooterKnown,hostile,dx,dy,dz,yaw,yawKnown};}
 static Extra From(const std::array<float,ExtraCount>&v){return {v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9]};}
};
bool ValidExtra(const Extra&)noexcept;
using FrameInput=std::array<float,FrameFeatures>;
struct Frame {float c=1,s=0;bool known=false;};
// Forward = current horizontal velocity (or the latest earlier one above 20 u/s, else the shooter direction).
Frame FrameOf(const Input&,const Extra&)noexcept;
FrameInput ToFrame(const Input&,const Extra&,const Frame&,bool mirror)noexcept;
Vec ToLocal(const Vec&,const Frame&,bool mirror)noexcept;
Vec ToWorld(const Vec&,const Frame&)noexcept;
struct Received { Observation observation; Input context{}; Extra extra{}; };
struct History { std::array<Received,64> points{}; size_t count=0; };
bool Features(const History&,Input&) noexcept;
struct Sequence {
 int schema=Schema;
 uint64_t anchor=0,player=0,group=0;
 Input x{}; std::array<Vec,Knots> displacement{};
 std::array<float,Knots> horizon{}; std::array<uint64_t,Knots> endGroup{};
 Extra extra{};
 // Ordinary movement-simulator prediction from the same received anchor (schema 4 'B' rows).
 std::array<Vec,FrameKnots> movesim{}; std::array<uint8_t,FrameKnots> paired{};
};
struct FrameNetwork {
 static constexpr int Outputs=Branches*(FrameKnots*3+1);
 std::array<float,FrameFeatures*HiddenCount> w1{}; std::array<float,HiddenCount>b1{};
 std::array<float,HiddenCount*Outputs>w2{}; std::array<float,Outputs>b2{};
 explicit FrameNetwork(unsigned seed=60917);
 std::array<float,Outputs> Forward(const FrameInput&)const noexcept;
 void Learn(const FrameInput&,const std::array<Vec,FrameKnots>&local,const std::array<float,FrameKnots>&horizon,unsigned branch,float rate,bool sharedNear)noexcept;
};
struct Network {
 int activeFeatures=FeaturesCount;
 static constexpr int Outputs=Branches*(Knots*3+1);
 std::array<float,FeaturesCount*HiddenCount> w1{}; std::array<float,HiddenCount>b1{};
 std::array<float,HiddenCount*Outputs>w2{}; std::array<float,Outputs>b2{};
 explicit Network(unsigned seed=53091);
 std::array<float,Outputs> Forward(const Input&)const noexcept;
 void Learn(const Sequence&,unsigned branch,float rate,bool sharedNear=false)noexcept;
};
struct Model {
 int schema=Schema;
 uint64_t owner=0,revision=0,rows=0; bool trained=false;
 Network network{}; Input low{},high{};
 std::array<std::array<uint64_t,Knots>,Branches> counts{};
 std::array<std::array<float,Knots>,Branches> radius{};
 std::array<Sequence,Branches> centers{};
 FrameNetwork frame{}; // schema 4 only
};
struct Branch { float probability=0; std::array<Vec,Knots> displacement{}; std::array<float,Knots> radius{};std::array<bool,Knots> supported{}; };
struct Distribution { std::array<Branch,Branches> branches{};bool valid=false;uint64_t revision=0;const char*reason="missing sequence model"; };
// Schema 4 models need the Extra of the same received snapshot (unknown = default).
Distribution Predict(const Model&,const Input&,const Extra& ={})noexcept;
bool Interpolate(const Branch&,float horizon,Vec&,float&radius)noexcept;
// Contiguous prefix accepted by the same probability/radius gates as the adapter.
// Used to bound general ballistic searches; never extrapolates or relaxes a gate.
float CoveredHorizon(const Branch&,float minimumProbability,float maximumRadius)noexcept;
int FlightTicks(float lifetime,float simulationLimit,float lead,float coveredHorizon,float tick)noexcept;
// Exact roots for each learned piecewise-linear path segment and a constant-speed projectile.
std::vector<float> Interceptions(const Branch&,Vec targetOrigin,Vec shotOrigin,Vec aimOffset,float speed,float lead,float physicalLimit);
// Conservative time intervals; padding includes muzzle displacement and target/splash extent.
std::vector<std::array<float,2>> ReachableIntervals(const Branch&,Vec targetOrigin,Vec shotOrigin,Vec aimOffset,float speed,float lead,float physicalLimit,float padding);
struct FitResult {Model model;bool fitted=false;std::string evaluation;};
struct TrainingPolicy {bool includeBots=false;double playerShare=.15;};
FitResult Fit(std::span<const Sequence>,uint64_t owner,const std::function<bool()>&cancel={});
int DatasetSchema(const std::filesystem::path&);
// A corpus file that exists but holds no bytes yet (e.g. created, then the game closed before the first flush).
bool EmptyDataset(const std::filesystem::path&);
void ValidateDataset(const std::filesystem::path&,const std::function<bool()>&cancel={});
FitResult FitDataset(const std::filesystem::path&,uint64_t owner,size_t selection,const std::function<bool()>&cancel={});
// Strafe-timing context for a received snapshot (class, shooter, incoming threat, leg statistics).
strafe::Context StrafeContext(const Received&,const strafe::Stats&)noexcept;
FitResult FitDatasets(std::span<const std::filesystem::path>,uint64_t owner,size_t selection,const std::function<bool()>&cancel={},const TrainingPolicy& ={});
// Strafe-timing rows ('R') of every schema-4 file given; older files have none.
strafe::FitResult FitStrafe(std::span<const std::filesystem::path>,const TrainingPolicy&,const std::function<bool()>&cancel={});
void SaveModel(const std::filesystem::path&,const Model&);
Model LoadModel(const std::filesystem::path&);
enum class Selection:int {Auto,Global,Personal,Imported};
struct Policy {bool train=false,force=false,allowMovesim=false,unvalidated=true;Selection selection=Selection::Auto;uint64_t imported=0;bool operator==(const Policy&)const=default;};
struct Registry {std::map<uint64_t,Policy> players;};
struct Models {std::map<uint64_t,Model> local,imported;};
struct Outcome;struct ShotPolicy;
const Model* Select(const Models&,const Registry&,uint64_t id,bool allowUnvalidated)noexcept;
struct Status {uint64_t observations=0,examples=0,anchors=0,diskBytes=0,dropped=0,paired=0,strafeRows=0;size_t queued=0,models=0;bool collecting=false,training=false,policiesReady=false,ready=false,strafeAccepted=false;std::string operation="Starting",error,evaluation,strafeEvaluation;};
class Service {
 struct Impl;std::unique_ptr<Impl>p;
public:
 explicit Service(std::filesystem::path);~Service();
 void Configure(bool collect,size_t samples=8192,bool all=false,bool includeBots=false);bool Observe(const Received&)noexcept;bool Train(uint64_t owner=0)noexcept;
 // The ordinary simulator path the aimbot used from a received anchor; pairs with sequence anchors.
 bool RecordBaseline(const Baseline&)noexcept;
 std::shared_ptr<const strafe::Model> StrafeModel()const;std::shared_ptr<const strafe::Stats> StrafeStats()const;
 bool SetPolicy(uint64_t,const Policy&)noexcept;bool Import(uint64_t catalogId)noexcept;
 bool RecordOutcome(const Outcome&)noexcept;bool ImportOutcomes()noexcept;
 std::shared_ptr<const ShotPolicy> Outcomes()const;
 std::shared_ptr<const Registry> Policies()const;std::shared_ptr<const Models> Published()const;
 Status GetStatus()const;void ResetHistory()noexcept;void Stop()noexcept;
 std::shared_ptr<storage::Snapshot> SnapshotData()const;
 std::array<std::shared_ptr<storage::Snapshot>,3> SnapshotCorpora()const; // v2, v3 (frozen), v4
 void Cancel()noexcept;
};
// Chosen-action evidence only. Unknown outcomes are censored, never misses.
enum class Objective:int{AnyDamage,ExpectedDamage,Kill};
enum class Splash:int{Include=1,Prefer=2,Only=3};
// Unknown legacy context is exactly the default sentinel. State bits 0..5:
// precision rune, blast jumping, crit boost, minicrit boost, center fire, ducking.
struct WeaponContext {
 int itemDef=-1;float speed=0,gravity=0,radius=0,charge=0;uint32_t state=0;float damageScale=1;
 bool operator==(const WeaponContext&)const=default;
};
bool KnownWeaponContext(const WeaponContext&)noexcept;
struct Outcome {uint64_t shot=0;unsigned weapon=0,rangeBand=0;bool air=false;Splash chosen=Splash::Include;bool known=false;float damage=0;bool killed=false;unsigned evidence=0;WeaponContext context{};};
// Read v2/v3 without mutation; every new write uses v3 and retains .previous.
std::vector<Outcome> ReadOutcomes(const std::filesystem::path&);
void WriteOutcomes(const std::filesystem::path&,std::span<const Outcome>);
struct Arm {uint64_t attempted=0,known=0,hits=0,kills=0;double damage=0;};
struct ShotPolicy {
 std::map<unsigned,std::array<Arm,3>> cells;
 using ContextKey=std::tuple<unsigned,int,int64_t,int64_t,int64_t,int64_t,int64_t,uint32_t,unsigned,bool>;
 std::map<ContextKey,std::array<Arm,3>> contextCells;
 std::set<uint64_t> seen;
 bool Add(const Outcome&);
 // Exploration is deterministic from completed chosen attempts, never from query count.
 // evidence=true only for exploitation; exploratory choices remain explicitly unvalidated.
 Splash Choose(unsigned weapon,unsigned rangeBand,bool air,Objective,bool&evidence,bool explore=false)const;
 Splash Choose(unsigned weapon,const WeaponContext&,unsigned rangeBand,bool air,Objective,bool&evidence,bool explore=false)const;
};
}
