#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
namespace PredictionPolicy
{
    inline bool Fresh(float now, float previous) { return std::isfinite(now) && now > previous; }
    inline bool Discontinuity(float delta, float distance, float speed)
    {
        return !std::isfinite(delta) || delta <= 0.f || delta > 1.f ||
            !std::isfinite(distance) || !std::isfinite(speed) || distance > 128.f + std::max(0.f, speed) * delta * 2.f;
    }
    inline float Confidence(int windows, int failed)
    {
        return windows > 0 ? std::clamp(1.f - float(failed) / windows, 0.f, 1.f) : 0.f;
    }
    inline float LandingYaw(float) { return 0.f; } // Landing invalidates the airborne turn hypothesis.
    inline float LandingForward(float speed,float maxSpeed)
    {
        if (!std::isfinite(speed) || !std::isfinite(maxSpeed) || maxSpeed<=0.f || speed<=maxSpeed*.015f)
            return 0.f;
        return std::clamp(speed,0.f,maxSpeed);
    }
    struct LandingInput { float x=0.f,y=0.f,speed=0.f; };
    inline bool UseCompressedHull(bool replay,bool nominal) { return !(replay && nominal); }
    inline bool ApplyLandingAlternative(bool candidate,bool replay,bool uncorrected)
    { return candidate && replay && !uncorrected; }
    inline constexpr float LandingSweepTolerance=.125f;
    inline bool NearLandingSupport(float sweepZ,float supportZ)
    {
        const float gap=sweepZ-supportZ;
        return std::isfinite(sweepZ) && std::isfinite(supportZ)
            && gap>=0.f && gap<=LandingSweepTolerance;
    }
    inline bool VerticalLandingDeflection(float vx,float vy,float vz,float afterX,float afterY,float nx,float ny,float nz)
    {
        for(float value:{vx,vy,vz,afterX,afterY,nx,ny,nz}) if(!std::isfinite(value)) return false;
        if(vz>-100.f || nz<.7071068f || nz>.99f) return false;
        if(std::abs(nx*nx+ny*ny+nz*nz-1.f)>.01f) return false;
        // Only near-tangential horizontal approaches: no correction of running into a slope.
        if(std::abs(vx*nx+vy*ny)>5.f) return false;
        const float dx=afterX-vx,dy=afterY-vy;
        if(std::hypot(dx,dy)<80.f) return false;
        // Require the new velocity to match downward velocity projected onto this plane.
        const float expectedX=-vz*nz*nx,expectedY=-vz*nz*ny;
        return std::hypot(dx-expectedX,dy-expectedY)<=std::max(8.f,std::hypot(expectedX,expectedY)*.1f);
    }
    inline LandingInput PreContactIntent(float x,float y,float maxSpeed)
    {
        if(!std::isfinite(x) || !std::isfinite(y)) return {};
        const float speed=std::hypot(x,y);
        const float input=LandingForward(speed,maxSpeed);
        if(input<=0.f) return {};
        return {x*(input/speed),y*(input/speed),input};
    }
    template<class F> struct ScopeExit
    {
        F restore;
        ~ScopeExit() { restore(); }
        ScopeExit(const ScopeExit&) = delete;
        explicit ScopeExit(F action) : restore(action) {}
    };
}
