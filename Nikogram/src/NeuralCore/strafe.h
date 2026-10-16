#pragma once
// Standalone C++20. Learned strafe-reversal timing that feeds the ordinary
// movement simulator: movesim keeps all physics, this only predicts WHEN the
// target's next lateral reversal happens. No engine or third-party code.
#include "movement.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace movement::strafe {
constexpr int Schema=1, Inputs=28, Hidden=12, MaxLegs=6, MaxWindows=16, Samples=96;
// Landmarks every 0.1 s of a leg; the model predicts how long the leg still lasts.
constexpr float Window=.1f, MaxLeg=1.5f, MinLeg=.05f, AxisSeconds=1.f;
// Per-player blending: personal mean leg length counts like this many global legs.
constexpr float PriorLegs=20.f;
// Longest time written to a row; longer legs are recorded as censored at this bound (Decode accepts it).
constexpr float MaxRecorded=30.f;

struct Sample {double time=0;float x=0,y=0,vx=0,vy=0;bool ground=false;};
struct Reversal {bool happened=false;double time=0;float duration=-1;};

// Online lateral leg tracker. Trivially copyable: lives inside fixed per-player
// track arrays on the game thread and inside the learning worker.
struct LegTracker {
 std::array<Sample,Samples> samples{};size_t count=0;
 float ax=1,ay=0,variance=0;bool axisValid=false;
 int sign=0,pending=0;double pendingTime=0,crossing=-1,legStart=-1;
 float lastU=0;double lastTime=-1;float peak=0,accel=0;
 std::array<float,MaxLegs> legs{};int legCount=0;
 void Reset()noexcept{*this=LegTracker{};}
 // Feed one received snapshot (time strictly increasing). Returns a confirmed reversal, if any.
 Reversal Update(const Sample&)noexcept;
 bool Strafing()const noexcept{return axisValid&&legStart>=0&&sign!=0&&count;}
 float Lateral()const noexcept{return count?samples[count-1].vx*ax+samples[count-1].vy*ay:0;}
 float Along()const noexcept{return count?-samples[count-1].vx*ay+samples[count-1].vy*ax:0;}
 float MeanLeg(int n=4)const noexcept;float StdLeg(int n=4)const noexcept;
};

// Per-player leg statistics. Only legs that ended in a confirmed reversal count.
struct PlayerLegs {uint64_t n=0;double sum=0,square=0;double Mean()const{return n?sum/double(n):0;}};
struct Stats {std::map<uint64_t,PlayerLegs> players;PlayerLegs global;};
// Blended typical leg length and its personal-evidence weight in [0,1).
std::array<float,2> Blend(const Stats&,uint64_t player)noexcept;

// Everything outside the tracker that the features need. Unknown stays zero.
struct Context {
 int cls=0;bool ground=false;
 float playerMean=0,playerConfidence=0;
 bool shooterKnown=false;float shooterDx=0,shooterDy=0,shooterDistance=0;
 float threat=0; // 0 none .. 1 a received projectile passing within its blast radius soon
};
using Features=std::array<float,Inputs>;
bool MakeFeatures(const LegTracker&,const Context&,double now,float extraElapsed,Features&)noexcept;

// One landmark per 0.1 s of a leg. remaining>=0: the leg ended that long after the
// landmark. Otherwise observed>=0: the pattern was lost that long after it (censored).
struct Row {uint64_t id=0,player=0,group=0;Features x{};float remaining=-1,observed=-1;};
std::string Encode(const Row&);
bool Decode(const std::string&,Row&)noexcept; // text after the 'R ' record tag

// Distribution over the time until the current leg ends: 16 bins of 0.1 s, then ">= 1.6 s".
constexpr int Bins=17;
using Output=std::array<float,Bins>;
int Bin(float seconds)noexcept;
struct Network {
 std::array<float,Hidden*Inputs> w1{};std::array<float,Hidden> b1{};std::array<float,Hidden*Bins> w2{};Output b2{};
 explicit Network(uint32_t seed=40213);
 Output Probabilities(const Features&)const noexcept;
 void Learn(const Row&,float rate)noexcept;
};
struct Model {
 int schema=Schema;uint64_t revision=0,rows=0;bool trained=false,accepted=false;
 Network net;Features low{},high{};
 double testMae=0,baselineMae=0;float fallbackLeg=.35f;
};
// False when the state is outside training coverage.
bool Distribution(const Model&,const Features&,Output&)noexcept;
// Time until the current leg ends at the given quantile (0.5 = median).
// +inf when it most likely outlasts the last bin, NaN when no prediction applies.
float Remaining(const Model&,const Features&,float quantile)noexcept;
float TimeToReversal(const Model&,const LegTracker&,const Context&,double now,float quantile)noexcept;
struct Plan {bool valid=false;float first=0,leg=0;const char*reason="strafe timing unavailable";};
Plan MakePlan(const Model&,const LegTracker&,const Context&,double now,float quantile)noexcept;

struct FitResult {Model model;bool fitted=false;std::string evaluation;};
struct Policy {bool includeBots=false;double playerShare=.15;};
// rows: every decoded 'R' record of the corpus, any order. Splits by time group.
FitResult Fit(const std::function<void(const std::function<void(const Row&)>&)>& rows,const Policy&,const std::function<bool()>& cancel={});
void SaveModel(const std::filesystem::path&,const Model&);
Model LoadModel(const std::filesystem::path&);
void SaveStats(const std::filesystem::path&,const Stats&);
Stats LoadStats(const std::filesystem::path&);

// Collection-side: one landmark per 0.1 s of each leg, features frozen at the
// landmark. Landmarks are buffered until the leg ends (remaining known) or the
// pattern is lost (censored at the observed time). No future value leaks back.
struct Collector {
 struct Pending {double start=0;Features x{};};
 std::array<Pending,MaxWindows> windows{};int count=0;double next=-1,lastStrafing=-1;
 void Reset()noexcept{count=0;next=-1;lastStrafing=-1;}
 // Call after LegTracker::Update with the same snapshot. emit(features, remaining, observed).
 template<class Emit> void Step(const LegTracker&t,const Reversal&r,const Context&c,double now,Emit&&emit){
  if(r.happened){
   // Landmarks opened after the (interpolated) crossing belong to the new leg: dropped.
   for(int i=0;i<count;++i){const auto&w=windows[i];if(w.start>r.time)break;const float remaining=float(r.time-w.start);if(remaining<=MaxRecorded)emit(w.x,remaining,-1.f);else emit(w.x,-1.f,MaxRecorded);}
   count=0;next=t.legStart;
  }
  if(!t.Strafing()){Abandon(emit);return;}
  if(next<0)next=t.legStart;
  // One landmark per snapshot at most, timed at that snapshot: features and label share one clock.
  if(next<=now&&count<MaxWindows&&now-t.legStart<MaxLeg){Features x;if(!MakeFeatures(t,c,now,0,x)){Abandon(emit);return;}windows[count++]={now,x};while(next<=now)next+=Window;}
  lastStrafing=now;
 }
 // Pattern lost: censored at the last time the pattern was still observed (never across a gap).
 template<class Emit> void Abandon(Emit&&emit){for(int i=0;i<count;++i)if(lastStrafing>windows[i].start)emit(windows[i].x,-1.f,std::min(float(lastStrafing-windows[i].start),MaxRecorded));count=0;next=-1;lastStrafing=-1;}
};
}
