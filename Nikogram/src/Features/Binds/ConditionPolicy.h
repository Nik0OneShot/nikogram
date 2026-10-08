#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace ConditionPolicy
{
    inline const char* ClassName(int id)
    {
        constexpr const char* names[]={"Any enemy", "Scout", "Sniper", "Soldier", "Demoman", "Medic", "Heavy", "Pyro", "Spy", "Engineer"};
        return names[std::clamp(id,0,9)];
    }
    struct Options
    {
        int enemyClass=0; // TF class ID; zero means any enemy class.
        float behindRange=1200.f, behindArc=180.f;
        bool behindVisible=false;
        float aimTolerance=2.f, projectileWindow=.1f;
        bool sniperAnyAim=false; // Any visible enemy Sniper; retain the saved option name for compatibility.
        bool operator==(const Options&)const=default;
    };
    inline float Bounded(float value,float fallback,float low,float high)
    {return std::isfinite(value)?std::clamp(value,low,high):fallback;}
    inline Options Normalize(Options o)
    {
        o.enemyClass=std::clamp(o.enemyClass,0,9);
        o.behindRange=Bounded(o.behindRange,1200,1,10000);
        o.behindArc=Bounded(o.behindArc,180,1,180);
        o.aimTolerance=Bounded(o.aimTolerance,2,0,10);
        o.projectileWindow=Bounded(o.projectileWindow,.1f,.01f,1.f);
        return o;
    }
    inline bool Behind(float x,float y,float z,float yaw,int enemyClass,const Options& raw)
    {
        const auto o=Normalize(raw);
        if((o.enemyClass&&o.enemyClass!=enemyClass)||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(yaw))return false;
        const float horizontal=std::hypot(x,y);
        if(horizontal<.001f||x*x+y*y+z*z>o.behindRange*o.behindRange)return false;
        constexpr float rad=.01745329252f;
        const float rearDot=-(x*std::cos(yaw*rad)+y*std::sin(yaw*rad))/horizontal;
        return rearDot>0.f&&rearDot+1.e-6f>=std::cos(o.behindArc*.5f*rad);
    }
    inline bool Lethal(float damage,float health)
    {return std::isfinite(damage)&&std::isfinite(health)&&health>0&&damage>=health;}
    inline bool RequiresAim(bool sniper,const Options& options)
    {return !sniper||!options.sniperAnyAim;}
    inline float SniperDamage(float elapsed,float rate)
    {return Bounded(std::max(0.f,elapsed-.3f)*rate,50.f,50.f,150.f);}
    // Resistance to critical damage changes the bonus, not the base damage.
    inline float Damage(float base,float multiplier,float general,float type,float crit,float vaccine,bool blockCrit)
    {
        const float bonus=blockCrit?0.f:base*std::max(multiplier-1.f,0.f)*(multiplier>=3.f?crit:1.f);
        return std::max(0.f,(base*type*vaccine+bonus)*general);
    }
    // Entry fraction into a swept target box; infinity means no contact.
    inline float Contact(const std::array<float,3>& start,const std::array<float,3>& end,const std::array<float,3>& low,const std::array<float,3>& high)
    {
        float enter=0,leave=1;
        for(int i=0;i<3;++i)
        {
            if(!std::isfinite(start[i])||!std::isfinite(end[i])||!std::isfinite(low[i])||!std::isfinite(high[i])||low[i]>high[i])return INFINITY;
            const float d=end[i]-start[i];
            if(std::abs(d)<1.e-6f){if(start[i]<low[i]||start[i]>high[i])return INFINITY;continue;}
            float a=(low[i]-start[i])/d,b=(high[i]-start[i])/d;
            if(a>b)std::swap(a,b);enter=std::max(enter,a);leave=std::min(leave,b);
            if(enter>leave)return INFINITY;
        }
        return enter;
    }
    inline float Splash(float damage,float distance,float radius)
    {
        if(!std::isfinite(damage)||!std::isfinite(distance)||!std::isfinite(radius)||radius<=0||distance<0||distance>radius)return 0;
        return damage*(1.f-.5f*distance/radius);
    }
}
