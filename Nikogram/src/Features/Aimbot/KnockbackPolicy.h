#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

// Received-projectile hypotheses, never future button prediction. Constants
// describe the public Source TF implementation, not a remote-server guarantee.
namespace KnockbackPolicy {
using V=std::array<float,3>;
inline constexpr size_t MaxProjectiles=8, MaxTicks=64, MaxImpacts=8;
inline constexpr float Horizon=.8f, FreshAge=.1f;
inline bool Finite(V v){return std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2]);}
inline V Add(V a,V b){return {a[0]+b[0],a[1]+b[1],a[2]+b[2]};}
inline V Sub(V a,V b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
inline V Mul(V a,float s){return {a[0]*s,a[1]*s,a[2]*s};}
inline float Length(V a){return std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);}
inline bool Fresh(float age){return std::isfinite(age)&&age>=-.02f&&age<=FreshAge;}
inline float Entry(V start,V end,V mins,V maxs,float erosion=0){
    if(!Finite(start)||!Finite(end)||!Finite(mins)||!Finite(maxs)||!std::isfinite(erosion))return -1;
    float lo=0,hi=1;
    for(int k=0;k<3;++k){const float a=mins[k]+erosion,b=maxs[k]-erosion,d=end[k]-start[k];
        if(a>b)return -1;
        if(std::abs(d)<1e-6f){if(start[k]<a||start[k]>b)return -1;continue;}
        float u=(a-start[k])/d,v=(b-start[k])/d;if(u>v)std::swap(u,v);
        lo=std::max(lo,u);hi=std::min(hi,v);if(lo>hi)return -1;
    }return lo;
}
inline float HullDistance(V point,V origin,V mins,V maxs){
    V delta=Sub(point,origin),nearest{};for(int k=0;k<3;++k)nearest[k]=std::clamp(delta[k],mins[k],maxs[k]);
    return Length(Sub(delta,nearest));
}
inline float BlastDamage(float base,float distance,float radius){
    if(!std::isfinite(base)||!std::isfinite(distance)||!std::isfinite(radius)||base<=0||distance<0||radius<=0)return 0;
    return base*(1.f-.5f*std::clamp(distance/radius,0.f,1.f));
}
inline float RocketDistanceDamage(float damage,float distance,float range,bool noCloseMod=false,bool minicrit=false){
    if(!std::isfinite(damage)||!std::isfinite(distance)||!std::isfinite(range)||damage<=0||distance<0||range<0||range>1)return 0;
    float center=std::clamp(1.f-distance/1024.f,0.f,1.f);
    if(noCloseMod&&center>.5f)center=.5f+(center-.5f)*.3f;
    // Mini-crits keep close-range ramp-up but never fall off with distance.
    if(minicrit)center=std::max(center,.5f);
    const float x=std::max(0.f,center-.1f)+.1f; // native disabled-random-spread midpoint
    float amount=damage*range;if(x>.5f)amount*=.5f;
    const float spline=x*x*(3.f-2.f*x);
    return damage+amount*(2.f*spline-1.f);
}
inline V Force(V explosion,V center,V hull,float damage,float scale,float multiplier){
    if(!Finite(explosion)||!Finite(center)||!Finite(hull)||!std::isfinite(damage)||!std::isfinite(scale)
        ||!std::isfinite(multiplier)||damage<=0||scale<=0||multiplier<0||multiplier>4)return {};
    const float volume=hull[0]*hull[1]*hull[2];if(volume<1||!std::isfinite(volume))return {};
    V away=Sub(center,Add(explosion,{0,0,-10}));const float length=Length(away);if(length<1e-4f)return {};
    const float magnitude=std::min(1000.f,damage*(48.f*48.f*82.f)/volume*scale)*multiplier;
    return Mul(away,magnitude/length);
}
struct Evidence {
    float first=std::numeric_limits<float>::infinity(), last=0, lowerDamage=0;
    unsigned impacts=0, uncertain=0;bool complete=true;
    void Add(float time,bool strong,float damage){
        first=std::min(first,time);last=std::max(last,time);++impacts;if(!strong)++uncertain;
        if(strong&&time<=.15f&&std::isfinite(damage)&&damage>0)lowerDamage+=std::floor(damage);
        if(impacts>MaxImpacts||uncertain>1||(uncertain&&impacts>1))complete=false;
    }
    bool High()const{return complete&&impacts&&uncertain==0;}
    bool Affects(float shotTime)const{return impacts&&std::isfinite(shotTime)&&first<=shotTime;}
    float Reserve()const{return High()?lowerDamage:0;}
};
enum class Choice { Ordinary, Impact, Reject };
inline Choice Select(bool baselineValid,bool branchValid,const Evidence& base,const Evidence& hit,float shotTime){
    if(!base.Affects(shotTime))return baselineValid?Choice::Ordinary:Choice::Reject;
    if(!base.complete||!hit.complete)return Choice::Reject;
    // One uncertain encounter: caller has constrained the hit branch to the
    // exact ordinary command angle. Multiple independent uncertain outcomes
    // cannot be represented by these two paths and must not be averaged.
    if(base.High())return branchValid&&hit.High()?Choice::Impact:Choice::Reject;
    return baselineValid&&branchValid?Choice::Ordinary:Choice::Reject;
}
}
