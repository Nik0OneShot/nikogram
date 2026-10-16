#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>

// Observational values only. No command, entity, simulation, or learning writes.
namespace AimObservation {
struct Vec { float x=0,y=0,z=0; };
inline bool Finite(Vec p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
inline float Distance(Vec a,Vec b){return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));}
inline Vec Lerp(Vec a,Vec b,double t){return {float(a.x+(b.x-a.x)*t),float(a.y+(b.y-a.y)*t),float(a.z+(b.z-a.z)*t)};}
struct Identity {uint32_t handle=0;int spawn=0,server=0,mode=0;bool operator==(const Identity&)const=default;};
struct Point {double time=0;Vec position{};};
struct Path {
    std::array<Point,80> points{};size_t count=0;
    bool Add(double time,Vec p){if(count==points.size()||!std::isfinite(time)||!Finite(p)||(count&&time<=points[count-1].time))return false;points[count++]={time,p};return true;}
    bool At(double time,Vec& out)const {
        if(!count||time<points[0].time||time>points[count-1].time)return false;
        for(size_t i=0;i<count;++i){if(time==points[i].time){out=points[i].position;return true;}if(points[i].time>time){const auto&a=points[i-1];const auto&b=points[i];if(b.time-a.time>.06)return false;out=Lerp(a.position,b.position,(time-a.time)/(b.time-a.time));return true;}}
        return false;
    }
};
enum class Availability { Pending,Available,Disabled,NoModel,History,Unsupported,Uncertainty,NotRun,Gap,Partial };
inline const char* Name(Availability a){switch(a){case Availability::Pending:return "pending";case Availability::Available:return "available";case Availability::Disabled:return "disabled";case Availability::NoModel:return "no model";case Availability::History:return "history unavailable";case Availability::Unsupported:return "unsupported";case Availability::Uncertainty:return "uncertainty rejected";case Availability::NotRun:return "not evaluated";case Availability::Gap:return "observation gap";case Availability::Partial:return "partial coverage";}return "unavailable";}
struct Cohort {
    uint64_t serial=0;Identity identity{};int entity=0,command=0;double anchor=0,created=0,received=0;
    Path ordinary{},neural{},observed{};Availability ordinaryState=Availability::Available,neuralState=Availability::NotRun,outcome=Availability::Pending;
};
class Comparisons {
public:
    static constexpr size_t Capacity=16;
    std::deque<Cohort> rows;
    uint64_t serial=0;
    void Clear(){rows.clear();}
    bool Publish(Cohort row){
        if(!row.identity.handle||!row.ordinary.count||!std::isfinite(row.anchor)||!std::isfinite(row.created))return false;
        if(!rows.empty()&&rows.back().identity!=row.identity)Clear();
        if(!rows.empty()&&row.anchor<=rows.back().anchor)return false;
        row.serial=++serial;row.observed={};row.outcome=Availability::Pending;
        if(rows.size()==Capacity)rows.pop_front();rows.push_back(row);return true;
    }
    bool AttachOrdinary(const Cohort&c){
        if(rows.empty())return false;auto&last=rows.back();
        if(last.serial!=c.serial||last.identity!=c.identity||last.anchor!=c.anchor||last.command!=c.command||last.observed.count||last.ordinaryState!=Availability::NotRun)return false;
        last.ordinary=c.ordinary;last.ordinaryState=c.ordinaryState;return true;
    }
    void Observe(Identity id,double time,Vec p,double received){
        if(!Finite(p)||!std::isfinite(time)||!std::isfinite(received))return;
        for(auto& row:rows){
            if(row.identity!=id||row.outcome==Availability::Gap||received<row.created||received<row.received||time<=row.anchor||time>row.anchor+.8)continue;
            if(row.observed.count){const auto&prev=row.observed.points[row.observed.count-1];if(time<=prev.time)continue;if(time-prev.time>.12){row.outcome=Availability::Gap;continue;}}
            else if(time-row.anchor>.12){row.outcome=Availability::Gap;continue;}
            if(row.observed.Add(time,p)){row.received=received;row.outcome=Availability::Available;}
        }
    }
    const Cohort* Display(double now)const {
        const Cohort* pending=nullptr;
        for(auto it=rows.rbegin();it!=rows.rend();++it){
            if(now-it->created>1.5)continue;if(!pending)pending=&*it;
            if(it->observed.count&&it->observed.points[it->observed.count-1].time-it->anchor>=.3)return &*it;
        }
        return pending;
    }
};
inline bool Error(const Cohort& c,bool neural,float& error,double& horizon){
    if(!c.observed.count||c.outcome!=Availability::Available)return false;
    const auto& path=neural?c.neural:c.ordinary;
    for(size_t n=c.observed.count;n>0;--n){const auto&o=c.observed.points[n-1];Vec p;if(path.At(o.time,p)){error=Distance(p,o.position);horizon=o.time-c.anchor;return true;}}
    return false;
}
inline bool PairedError(const Cohort&c,float&ordinary,float&neural,double&horizon){
    if(c.outcome!=Availability::Available)return false;
    for(size_t n=c.observed.count;n>0;--n){const auto&o=c.observed.points[n-1];Vec a,b;
        if(c.ordinary.At(o.time,a)&&c.neural.At(o.time,b)){ordinary=Distance(a,o.position);neural=Distance(b,o.position);horizon=o.time-c.anchor;return true;}}
    return false;
}
// Frame-only opacity. Neither measurements nor stored world-space vertices change.
inline float Fade(double now,double changed){return float(std::clamp((now-changed)/.08,0.,1.));}
enum class Reason { Evaluating,AimInactive,Manual,VisualOnly,WeaponNotReady,NoWeapon,PlayerBlocked,Menu,Switching,NoTarget,NoSolution,AimOnly,TargetDelay,Charging,ChargeRequested,ReleaseHeld,SwingWait,Cancelling,Scope,ChargeWait,HeadshotWait,TapWait,Safety,Wall,Uncertainty,SearchLimit,ShotRequested,ReleaseRequested,SwingRequested,Withdrawn,Reserved };
inline const char* Text(Reason r){switch(r){
case Reason::AimInactive:return "Not requested: aim inactive";case Reason::Manual:return "Not requested: Auto shoot off";case Reason::VisualOnly:return "Not requested: visual guidance only";
case Reason::WeaponNotReady:return "Waiting: weapon not ready";case Reason::NoWeapon:return "No active weapon";case Reason::PlayerBlocked:return "Blocked: player cannot attack";case Reason::Menu:return "Paused: game menu";case Reason::Switching:return "Waiting: weapon switch";
case Reason::NoTarget:return "No eligible target";case Reason::NoSolution:return "No validated shot";case Reason::AimOnly:return "Aim-only solution";case Reason::TargetDelay:return "Waiting: target switch";
case Reason::Charging:return "Holding charge";case Reason::ChargeRequested:return "Charge command requested";case Reason::ReleaseHeld:return "Release postponed: final command holds charge";case Reason::SwingWait:return "Waiting: melee swing timing";case Reason::Cancelling:return "Cancelling charge";case Reason::Scope:return "Waiting: scope / spin";case Reason::ChargeWait:return "Waiting: charge";case Reason::HeadshotWait:return "Waiting: headshot readiness";case Reason::TapWait:return "Waiting: tapfire interval";
case Reason::Safety:return "Withheld: safety validation";case Reason::Wall:return "Withheld: launch blocked";case Reason::Uncertainty:return "Withheld: prediction uncertainty";case Reason::SearchLimit:return "Search incomplete: work limit";
case Reason::ShotRequested:return "Shot command requested";case Reason::ReleaseRequested:return "Release command requested";case Reason::SwingRequested:return "Swing command requested";case Reason::Withdrawn:return "Shot request withdrawn";case Reason::Reserved:return "Another action owns this command";default:return "No shot issued";}}
struct Decision {
    bool enabled=false,live=false,selected=false;Reason reason=Reason::Evaluating;int command=0;double published=0;
    uint32_t owner=0,weapon=0;int spawn=0,server=0,mode=0,style=0;uint32_t target=0;
    void Set(Reason r){if(!enabled||!live)return;
        if((reason==Reason::Manual||reason==Reason::AimInactive)&&(r==Reason::NoTarget||r==Reason::NoSolution||r==Reason::Evaluating||r==Reason::WeaponNotReady||r==Reason::AimOnly||r==Reason::TargetDelay))return;
        reason=r;}
};
}
