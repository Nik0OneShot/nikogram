#pragma once
#include <cmath>
#include <initializer_list>
#include <algorithm>
#include <array>
namespace ObservationPolicy
{
    inline bool AuditBudgetExpired(double elapsedMs)
    { return !std::isfinite(elapsedMs) || elapsedMs<0 || elapsedMs>=3.0; }
    inline float SegmentBox(const std::array<float,3>& from,const std::array<float,3>& to,const std::array<float,3>& low,const std::array<float,3>& high)
    {
        float enter=0,leave=1;
        for(int i=0;i<3;++i) {
            for(float v:{from[i],to[i],low[i],high[i]}) if(!std::isfinite(v)) return -1;
            if(low[i]>high[i]) return -1;
            const float delta=to[i]-from[i];
            if(std::abs(delta)<1e-6f) {if(from[i]<low[i] || from[i]>high[i]) return -1;continue;}
            float a=(low[i]-from[i])/delta,b=(high[i]-from[i])/delta;
            if(a>b) std::swap(a,b);
            enter=std::max(enter,a);leave=std::min(leave,b);
            if(enter>leave) return -1;
        }
        return enter;
    }
    inline float SteeringStep(float current,float desired,float dt,float maxSpeed)
    {
        for(float value:{current,desired,dt,maxSpeed}) if(!std::isfinite(value)) return 0.f;
        if(dt<=0 || dt>.1f || maxSpeed<=0) return 0.f;
        const float limit=maxSpeed*dt/.15f;
        return std::clamp(std::clamp(desired,-maxSpeed,maxSpeed)-current,-limit,limit);
    }
    inline bool AdjustmentClear(float fraction,bool startSolid,bool allSolid,bool supported,float normalZ)
    { return std::isfinite(fraction) && fraction==1.f && !startSolid && !allSolid
        && supported && std::isfinite(normalZ) && normalZ>=.7071068f; }
    inline float BoundedCorridorShift(float projected,float low,float high)
    {
        if(!std::isfinite(projected) || !std::isfinite(low) || !std::isfinite(high) || high<low) return 0.f;
        return std::clamp(std::clamp(projected,low,high)-projected,-64.f,64.f);
    }
    inline bool LandingReplayEligible(bool startGround,bool predictedGround,float horizon,float elapsed)
    { (void)predictedGround; // A real slide can already be airborne at the forecast endpoint.
      return !startGround && std::isfinite(horizon) && horizon>0.f && horizon<=1.5f
        && std::isfinite(elapsed) && elapsed>=1.f; }
    struct DirectionError { bool valid=false; float along=0.f,across=0.f; };
    inline DirectionError RelativeError(float vx,float vy,float ex,float ey)
    {
        for(float value:{vx,vy,ex,ey}) if(!std::isfinite(value)) return {};
        const float speed=std::hypot(vx,vy);
        if(!std::isfinite(speed) || speed<5.f) return {};
        return {true,ex*(vx/speed)+ey*(vy/speed),-ex*(vy/speed)+ey*(vx/speed)};
    }
    inline float Linear(float origin,float velocity,float horizon) { return origin+velocity*horizon; }
    // Positive means the selected prediction beat the constant-velocity baseline.
    inline float Improvement(float baselineError,float selectedError) { return baselineError-selectedError; }
    inline bool Bracket(float before,float expected,float after)
    {
        return std::isfinite(before) && std::isfinite(expected) && std::isfinite(after)
            && after>before && after-before<=.1f && expected>=before && expected<=after;
    }
    inline float Weight(float before,float expected,float after)
    { return Bracket(before,expected,after)?(expected-before)/(after-before):-1.f; }
}
