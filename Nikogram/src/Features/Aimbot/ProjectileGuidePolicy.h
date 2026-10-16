#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace ProjectileGuidePolicy
{
    inline bool Fresh(int now, int generated, int lifetime)
    { return generated >= 0 && now >= generated && now-generated <= lifetime; }
    inline float Rank(float fov, std::uint32_t /*handle*/, std::uint32_t /*retained*/)
    { return std::isfinite(fov) ? fov : 10000.f; }
    inline bool Valid(float pitch, float yaw, float flight)
    { return std::isfinite(pitch) && std::isfinite(yaw) && std::isfinite(flight) && flight>0.f; }
    // Guide settings are independent of the automated firing policy.
    inline int Splash(bool guide, bool /*airborne*/, int configured)
    { return guide ? 0 : configured; }
    inline int Hitboxes(bool guide, int configured)
    { return guide ? 7 /* Auto | Head | Body */ : configured; }
    inline bool FreshTime(float now,float generated,float lifetime=.1f)
    {return std::isfinite(now)&&std::isfinite(generated)&&std::isfinite(lifetime)&&lifetime>0
        &&generated>=0 && now>=generated && now-generated<=lifetime;}
    inline float MarkerLifetime(float tickInterval)
    {return std::isfinite(tickInterval)&&tickInterval>0?std::clamp(2*tickInterval,1.f/30.f,.1f):1.f/30.f;}
    constexpr int MaxVisibility=16, MaxPathTicks=192;
    constexpr long long PathBudgetUs=750;
    inline int PathTicks(float lifetime,float tick)
    {
        if(!std::isfinite(lifetime)||!std::isfinite(tick)||lifetime<=0||tick<=0)return 0;
        return int(std::min(float(MaxPathTicks),std::floor(std::min(lifetime,2.5f)/tick)));
    }
    inline bool PathStep(int completed,long long elapsed)
    {return completed<MaxPathTicks && elapsed>=0 && elapsed<PathBudgetUs;}
    // Closest point on the crosshair ray to an inset collision box. A hit uses
    // the middle of the ray's box interval, preserving its vertical aim height.
    // Misses maximize angular proximity over box edges with a fixed arithmetic budget.
    template<class V> bool Focus(const V& eye,const V& direction,const V& origin,const V& mins,const V& maxs,V& out)
    {
        float e[3]{eye.x,eye.y,eye.z},d[3]{direction.x,direction.y,direction.z};
        float lo[3]{origin.x+mins.x,origin.y+mins.y,origin.z+mins.z};
        float hi[3]{origin.x+maxs.x,origin.y+maxs.y,origin.z+maxs.z};
        float entryDistance=0,exitDistance=8192,projection=0,norm=0;
        for(int i=0;i<3;++i)
        {
            if(!std::isfinite(e[i])||!std::isfinite(d[i])||!std::isfinite(lo[i])||!std::isfinite(hi[i])||hi[i]<lo[i])return false;
            const float inset=std::min(2.f,(hi[i]-lo[i])*.1f);lo[i]+=inset;hi[i]-=inset;
            projection+=((lo[i]+hi[i])*.5f-e[i])*d[i];norm+=d[i]*d[i];
        }
        if(norm<.99f||norm>1.01f||projection<=0)return false;
        bool hit=true;
        for(int i=0;i<3;++i)
            if(std::abs(d[i])<1e-6f){if(e[i]<lo[i]||e[i]>hi[i])hit=false;}
            else{float a=(lo[i]-e[i])/d[i],b=(hi[i]-e[i])/d[i];if(a>b)std::swap(a,b);entryDistance=std::max(entryDistance,a);exitDistance=std::min(exitDistance,b);}
        float t=0;
        if(hit&&entryDistance<=exitDistance)t=(entryDistance+exitDistance)*.5f;
        else
        {
            float best=-2;float chosen[3]{};
            // A non-intersecting ray's angular optimum is on a box edge.
            // Evaluate endpoints and the analytic cosine extremum on 12 edges.
            for(int axis=0;axis<3;++axis)for(int side=0;side<4;++side)
            {
                float v[3];int bit=0;
                for(int i=0;i<3;++i)v[i]=(i==axis?lo[i]:((side&(1<<bit++))?hi[i]:lo[i]))-e[i];
                float dot=0,length2=0;for(int i=0;i<3;++i){dot+=d[i]*v[i];length2+=v[i]*v[i];}
                const float edge=hi[axis]-lo[axis],denom=d[axis]*v[axis]-dot;
                float candidate[3]{0,edge,denom!=0?(dot*v[axis]-d[axis]*length2)/denom:-1};
                for(float offset:candidate)
                {
                    if(!std::isfinite(offset)||offset<0||offset>edge)continue;
                    const float squared=length2+2*v[axis]*offset+offset*offset;
                    if(squared<=0)continue;
                    const float cosine=(dot+d[axis]*offset)/std::sqrt(squared);
                    if(cosine>best){best=cosine;for(int i=0;i<3;++i)chosen[i]=e[i]+v[i]+(i==axis?offset:0);}
                }
            }
            if(best<=0)return false;
            out={chosen[0],chosen[1],chosen[2]};return true;
        }
        out={std::clamp(e[0]+d[0]*t,lo[0],hi[0]),std::clamp(e[1]+d[1]*t,lo[1],hi[1]),std::clamp(e[2]+d[2]*t,lo[2],hi[2])};
        return true;
    }
    inline bool BlendDisplay(bool same,float dt,float change)
    {return same&&std::isfinite(dt)&&dt>0&&dt<=.05f&&std::isfinite(change)&&change<=12.f;}
    inline float DisplayAlpha(float dt)
    {return std::isfinite(dt)&&dt>0?-std::expm1(-dt/.012f):1.f;}
    inline bool Aligned(float pitch, float yaw, float aimPitch, float aimYaw)
    {
        return std::isfinite(pitch) && std::isfinite(yaw)
            && std::isfinite(aimPitch) && std::isfinite(aimYaw)
            && std::hypot(pitch-aimPitch,std::remainder(yaw-aimYaw,360.f)) <= .5f;
    }
    inline float Scale(int height)
    { return std::clamp(height/1080.f,.75f,2.f); }
    inline float Radius(float configured, int height)
    { return (std::isfinite(configured)?std::clamp(configured,8.f,32.f):17.f)*Scale(height); }
}
