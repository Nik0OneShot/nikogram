#include "outcome_tracker.h"
#include <algorithm>
#include <cmath>

namespace movement::outcomes {
namespace {
constexpr double MaxGap=.15,SpawnWindow=.35,MaxFlight=76.8,EventSlop=.05;
bool Time(double t){return std::isfinite(t)&&t>=0&&t<=1e9;}
bool Finite(Vec v){return std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2])&&std::abs(v[0])<=1e7&&std::abs(v[1])<=1e7&&std::abs(v[2])<=1e7;}
float Distance(Vec a,Vec b={}){double sum=0;for(int i=0;i<3;++i){double d=double(a[i])-b[i];sum+=d*d;}return float(std::sqrt(sum));}
bool TargetOK(const Target&t){return t.handle&&t.user>0&&t.present&&!t.dormant&&t.playerClass>=1&&t.playerClass<=9&&t.health>=0&&t.health<=10000&&t.alive==(t.health>0)&&Finite(t.position);}
bool RocketOK(const Rocket&r){return r.handle&&r.owner&&r.launcher&&r.index>0&&r.index<65536&&Finite(r.position)&&Finite(r.velocity)&&Distance(r.velocity)<=6000;}
}
const char* Name(Confidence c)noexcept{switch(c){case Confidence::InferredTerminal:return "client-inferred isolated terminal";case Confidence::DirectInflictorKill:return "matching death inflictor (damage completeness separate)";default:return "unknown / censored";}}
const char* Name(Reason r)noexcept{switch(r){case Reason::Complete:return "settled isolated target outcome";case Reason::Warmup:return "insufficient quiet observation warmup";case Reason::InvalidInput:return "invalid observation";case Reason::ContextChanged:return "local/weapon/target context changed";case Reason::Overlap:return "another local discharge or rocket";case Reason::Gap:return "incomplete, lossy, or discontinuous observation";case Reason::AmbiguousSpawn:return "no unique attributable rocket spawn";case Reason::SpawnTimeout:return "rocket spawn was not observed in time";case Reason::Reflection:return "rocket reflected or ownership changed";case Reason::TargetUnavailable:return "target unavailable or dormant";case Reason::Spy:return "Spy damage/death ambiguity";case Reason::Healing:return "target health increased";case Reason::DamageConflict:return "damage and target health disagree";case Reason::MissingTerminal:return "missing coherent explosion/removal";case Reason::AmbiguousExplosion:return "competing explosion evidence";case Reason::EventMismatch:return "damage event not attributable";case Reason::UnknownDeath:return "death lacks matching inflictor and full damage";case Reason::Overflow:return "bounded tracker capacity exceeded";default:return "tracking cancelled";}}
void Tracker::Finish(Reason reason,Confidence confidence)noexcept{
 if(!active)return;Completed c;c.outcome=state.launch.chosen;c.outcome.known=reason==Reason::Complete;c.outcome.damage=c.outcome.known?float(state.totalDamage):0;c.outcome.killed=c.outcome.known&&state.dead;c.confidence=confidence;c.reason=reason;c.rocket=state.bound?state.rocket.handle:0;c.target=state.launch.target;c.targetUser=state.launch.targetUser;c.reliableEvents=state.hurtCount&&state.reliable;
 if(outputCount<outputs.size()){outputs[(outputRead+outputCount)%outputs.size()]=c;++outputCount;}else ++dropped;
 active=false;state={};quietBatches=0;
}
bool Tracker::Clock(double now)noexcept{if(!Time(now)||now+EventSlop<lastTime){Finish(Reason::Gap);haveBatch=false;quietBatches=0;return false;}lastTime=std::max(lastTime,now);return true;}
bool Tracker::Context(const Batch&b)const noexcept{return b.local==state.launch.local&&b.weapon==state.launch.weapon&&b.weaponID==state.launch.weaponID&&b.itemDef==state.launch.itemDef&&b.eligibleSoldierLauncher;}
void Tracker::Reset(Reason reason)noexcept{Finish(reason);haveBatch=false;quietBatches=0;lastTime=0;previous={};}
bool Tracker::Take(Completed&out)noexcept{if(!outputCount)return false;out=outputs[outputRead];outputRead=(outputRead+1)%outputs.size();--outputCount;return true;}
void Tracker::NativeFire(const Launch&launch)noexcept{
 const bool overlap=active;if(active)Finish(Reason::Overlap);const bool clockOK=Clock(launch.time);
 const bool warmed=haveBatch&&quietBatches>=2&&launch.time-previous.time<=MaxGap&&launch.time>=previous.time-EventSlop&&previous.local==launch.local&&previous.weapon==launch.weapon&&previous.weaponID==launch.weaponID&&previous.itemDef==launch.itemDef&&previous.eligibleSoldierLauncher;
 quietBatches=0;if(!launch.selected)return;
 if(launch.chosen.shot&&launch.chosen.shot<=lastChosenShot){++dropped;return;}lastChosenShot=std::max(lastChosenShot,launch.chosen.shot);
 active=true;state={};state.launch=launch;state.lastRelevant=launch.time;state.lastBatch=previous.time;
 if(!clockOK||!launch.chosen.shot||!launch.local||!launch.weapon||!launch.target||launch.targetUser<=0||launch.weaponID<=0||launch.itemDef<0||!Finite(launch.muzzle)||!std::isfinite(launch.blastRadius)||launch.blastRadius<=0||launch.blastRadius>512||launch.chosen.weapon!=unsigned(launch.weaponID)||int(launch.chosen.chosen)<1||int(launch.chosen.chosen)>3||launch.chosen.rangeBand>15){Finish(Reason::InvalidInput);return;}
 if(overlap){Finish(Reason::Overlap);return;}if(!warmed){Finish(Reason::Warmup);return;}
 state.target=launch.targetState.present?launch.targetState:previous.target;
 if(!TargetOK(state.target)||!state.target.alive||state.target.health<=0||state.target.handle!=launch.target||state.target.user!=launch.targetUser){Finish(Reason::TargetUnavailable);return;}if(state.target.playerClass==8){Finish(Reason::Spy);return;}
 state.initialHealth=state.lastEventHealth=state.target.health;state.settle=std::max(.5f,previous.settleDelay);
}
void Tracker::Observe(const Batch&b)noexcept{
 if(!Clock(b.time))return;if(b.rockets.size()>128){Finish(Reason::Overflow);haveBatch=false;quietBatches=0;return;}bool sane=b.complete&&b.lossFree&&b.eligibleSoldierLauncher&&b.local&&b.weapon&&b.weaponID>0&&b.itemDef>=0&&std::isfinite(b.settleDelay)&&b.settleDelay>=0&&b.settleDelay<=2&&b.rockets.size()<=128;
 bool continuous=!haveBatch||(b.time>previous.time&&b.time-previous.time<=MaxGap&&b.local==previous.local&&b.weapon==previous.weapon&&b.weaponID==previous.weaponID&&b.itemDef==previous.itemDef);
 if(!sane||!continuous){Finish(b.rockets.size()>128?Reason::Overflow:Reason::Gap);quietBatches=0;}
 unsigned owned=0;const Rocket*only=nullptr;bool badRocket=false;for(const auto&r:b.rockets){if(r.owner==b.local){++owned;only=&r;}if(!RocketOK(r))badRocket=true;}
 if(badRocket){Finish(Reason::InvalidInput);sane=false;quietBatches=0;}
 if(active){
  if(!Context(b)){Finish(Reason::ContextChanged);}else if(!TargetOK(b.target)||b.target.handle!=state.launch.target||b.target.user!=state.launch.targetUser||b.target.playerClass!=state.target.playerClass){Finish(Reason::TargetUnavailable);}else if(b.target.playerClass==8){Finish(Reason::Spy);}else if(b.target.health>state.target.health){Finish(Reason::Healing);}else if(Distance(b.target.position,state.target.position)>3000*float(std::max(0.,b.time-state.lastBatch))+32){Finish(Reason::Gap);}else{
   state.target=b.target;state.lastBatch=b.time;state.settle=std::max(state.settle,std::max(.5f,b.settleDelay));
   if(!state.bound){
    if(owned>1){Finish(Reason::AmbiguousSpawn);}else if(owned==1){if(only->launcher!=state.launch.weapon||only->dormant||only->deflected||b.time-state.launch.time>SpawnWindow||Distance(only->position,state.launch.muzzle)>Distance(only->velocity)*float(std::max(0.,b.time-state.launch.time))+64){Finish(Reason::AmbiguousSpawn);}else{state.bound=true;state.rocket=*only;state.lastRocketTime=b.time;state.flightLimit=float(std::min(MaxFlight,32768.*std::sqrt(3.)/std::max(1.,double(Distance(only->velocity)))));}}
   }else{
    const Rocket*bound=nullptr;for(const auto&r:b.rockets)if(r.handle==state.rocket.handle){if(bound){Finish(Reason::AmbiguousSpawn);break;}bound=&r;}
    if(active){if(owned>1){Finish(Reason::Overlap);}else if(bound){if(state.removed||bound->index!=state.rocket.index){Finish(Reason::AmbiguousSpawn);}else if(bound->owner!=state.launch.local||bound->launcher!=state.launch.weapon||bound->deflected){Finish(Reason::Reflection);}else if(bound->dormant){Finish(Reason::Gap);}else if(Distance(bound->position,state.rocket.position)>std::max(Distance(bound->velocity),Distance(state.rocket.velocity))*float(b.time-state.lastRocketTime)+48){Finish(Reason::Gap);}else{state.rocket=*bound;state.lastRocketTime=b.time;}}
     else if(owned){Finish(Reason::Overlap);}else if(!state.removed){state.removed=true;state.removedTime=b.time;state.lastRelevant=std::max(state.lastRelevant,b.time);}
    }
   }
   if(active&&state.terminal&&state.removed&&b.time>std::max(state.terminalTime,state.removedTime))state.postBatches=std::min(3u,state.postBatches+1);
  }
 }
 if(!active&&sane&&continuous&&!owned){quietBatches=std::min(3u,quietBatches+1);}else if(owned||!sane)quietBatches=0;
 previous=b;previous.rockets={};haveBatch=sane;Settle(b.time);
}
void Tracker::OnExplosion(const Explosion&e)noexcept{
 if(!Clock(e.time)||!active)return;if(!Finite(e.position)||e.weaponID<=0||e.itemDef<0){Finish(Reason::InvalidInput);return;}
 if(!state.bound){if(e.time>=state.launch.time)Finish(Reason::AmbiguousExplosion);return;}
 const double dt=e.time-state.lastRocketTime;const bool near=dt>=-EventSlop&&dt<=MaxGap+.1&&Distance(e.position,state.rocket.position)<=Distance(state.rocket.velocity)*float(std::max(0.,dt))+64;
 const bool nearTarget=Distance(e.position,state.target.position)<=state.launch.blastRadius+64;
 if(!near){if(nearTarget)Finish(Reason::AmbiguousExplosion);return;}
 if(state.terminal||e.weaponID!=state.launch.weaponID||e.itemDef!=state.launch.itemDef){Finish(Reason::AmbiguousExplosion);return;}
 state.terminal=true;state.explosion=e.position;state.terminalTime=e.time;state.lastRelevant=std::max(state.lastRelevant,e.time);
 for(unsigned i=0;i<state.hurtCount;++i)if(std::abs(state.hurts[i].time-e.time)>state.settle||Distance(state.hurts[i].position,e.position)>state.launch.blastRadius+64){Finish(Reason::EventMismatch);return;}
}
void Tracker::OnHurt(const Hurt&e)noexcept{
 if(!Clock(e.time)||!active||e.victimUser!=state.launch.targetUser)return;
 if(e.attacker!=state.launch.local||e.weaponID!=state.launch.weaponID||e.damage<=0||e.damage>10000||e.health<0||e.health>10000||!Finite(e.position)||e.time<state.launch.time||Distance(e.position,state.target.position)>3000*float(std::max(0.,e.time-state.lastBatch))+64){Finish(Reason::EventMismatch);return;}
 if(!state.bound){Finish(Reason::EventMismatch);return;}
 if(state.hurtCount>=state.hurts.size()){Finish(Reason::Overflow);return;}
 for(unsigned i=0;i<state.hurtCount;++i){const auto&h=state.hurts[i];if(h.time==e.time&&h.damage==e.damage&&h.health==e.health){Finish(Reason::DamageConflict);return;}}
 // A single rocket has at most one damage event for the selected player. Extra events are ambiguous.
 if(state.hurtCount||e.health!=std::max(0,state.initialHealth-e.damage)){Finish(Reason::DamageConflict);return;}
 if(state.terminal&&(std::abs(e.time-state.terminalTime)>state.settle||Distance(e.position,state.explosion)>state.launch.blastRadius+64)){Finish(Reason::EventMismatch);return;}
 state.hurts[state.hurtCount++]=e;state.totalDamage=e.damage;state.lastEventHealth=e.health;state.reliable&=e.reliable;state.lastRelevant=std::max(state.lastRelevant,e.time);state.postBatches=0;
}
void Tracker::OnDeath(const Death&e)noexcept{
 if(!Clock(e.time)||!active||e.victimUser!=state.launch.targetUser)return;
 if(!state.bound||state.dead||e.feign||e.inflictorIndex!=state.rocket.index||e.time<state.launch.time||(state.terminal&&std::abs(e.time-state.terminalTime)>state.settle)){Finish(Reason::UnknownDeath);return;}
 state.dead=true;state.reliable&=e.reliable;state.lastRelevant=std::max(state.lastRelevant,e.time);state.postBatches=0;
}
void Tracker::Settle(double now)noexcept{
 if(!active)return;if(now-state.lastBatch>MaxGap){Finish(Reason::Gap);return;}
 if(!state.bound&&now-state.launch.time>SpawnWindow){Finish(Reason::SpawnTimeout);return;}
 if(now-state.launch.time>state.flightLimit+state.settle){Finish(Reason::MissingTerminal);return;}
 if(state.terminal&&!state.removed&&now-state.terminalTime>MaxGap+.1){Finish(Reason::MissingTerminal);return;}
 if(state.removed&&!state.terminal&&now-state.removedTime>state.settle){Finish(Reason::MissingTerminal);return;}
 if(!state.terminal||!state.removed||state.postBatches<3||now-state.lastRelevant<state.settle)return;
 if(!state.target.alive&&!state.dead){Finish(Reason::UnknownDeath);return;}
 if(state.dead&&(!state.hurtCount||state.lastEventHealth!=0||state.target.health!=0)){Finish(Reason::UnknownDeath,Confidence::DirectInflictorKill);return;}
 if(state.target.health!=std::max(0,state.initialHealth-state.totalDamage)||state.lastEventHealth!=state.target.health){Finish(Reason::DamageConflict);return;}
 if(state.hurtCount&&Distance(state.hurts[0].position,state.explosion)>state.launch.blastRadius+64){Finish(Reason::EventMismatch);return;}
 Finish(Reason::Complete,state.dead?Confidence::DirectInflictorKill:Confidence::InferredTerminal);
}
void Tracker::Poll(double now)noexcept{if(Clock(now))Settle(now);}
}
