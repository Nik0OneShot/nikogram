#pragma once
#include "BowReleasePolicy.h"
#include <algorithm>
#include <cmath>
#include <optional>

// Prospective physics only. A successful trial may authorize holding, never release.
namespace BowChargePlan {
struct Physics { float fraction=0, wait=0; };
inline thread_local std::optional<Physics> trial;
struct Scope {
    std::optional<Physics> previous=trial;
    explicit Scope(Physics value){trial=value;}
    ~Scope(){trial=previous;}
};
inline float Fraction(float actual){return trial?trial->fraction:actual;}
inline float Wait(){return trial?trial->wait:0.f;}
struct Plan {
    BowReleasePolicy::Identity identity{};
    uint32_t target=0;
    int command=0, mode=0, settings=0, targetSpawn=-1;
    float deadline=0, duration=0, fraction=0, observedBegin=0;
    bool valid=false;
    void Clear(){*this={};}
    bool Matches(BowReleasePolicy::Identity id,int cmd,int aim,int flags)const {
        return valid&&identity==id&&cmd>0&&(cmd==command||int64_t(cmd)==int64_t(command)+1)
            &&mode==aim&&settings==flags;
    }
    bool Observe(BowReleasePolicy::Identity id,int cmd,int aim,int flags,float now,float begin,
                 bool alive,bool active,bool manual,bool cancel) {
        if(!Matches(id,cmd,aim,flags)||!alive||!active||manual||cancel||!std::isfinite(now)
            ||!std::isfinite(begin)||begin<0){Clear();return false;}
        // Native acknowledgement fixes the physical charge start once. Subsequent
        // revalidation must never keep moving the planned deadline forward.
        if(begin>0&&observedBegin==0){observedBegin=begin;deadline=begin+duration;}
        else if(observedBegin>0&&begin!=observedBegin){Clear();return false;}
        command=cmd;
        return true;
    }
    bool Start(BowReleasePolicy::Identity id,uint32_t handle,int cmd,int aim,int flags,
               float now,float maxTime,int spawn=-1) {
        if(!handle||!id.owner||!id.weapon||cmd<=0||!std::isfinite(now)||!std::isfinite(maxTime)||maxTime<=0)
            return false;
        identity=id;target=handle;targetSpawn=spawn;command=cmd;mode=aim;settings=flags;
        duration=std::min(maxTime,1.f);fraction=std::clamp(duration/maxTime,0.f,1.f);
        deadline=now+duration;observedBegin=0;valid=std::isfinite(deadline);return valid;
    }
    std::optional<Physics> Future(uint32_t handle,float now,float tick,int spawn=-1)const {
        if(!valid||target!=handle||targetSpawn!=spawn||!std::isfinite(now)||!std::isfinite(tick)||tick<=0
            ||now>=deadline||deadline-now>duration+tick)return {};
        return Physics{fraction,deadline-now};
    }
};
}
