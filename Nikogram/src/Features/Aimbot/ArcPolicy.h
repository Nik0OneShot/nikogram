#pragma once
#include <cmath>
#include <algorithm>

namespace ArcPolicy
{
    inline bool Eligible(bool enabled,double gravity)
    {return enabled && std::isfinite(gravity) && gravity>0;}
    struct DragKey
    {
        float speed=0,overrideValue=0;unsigned type=0;
        bool physics=false,lob=false,noSpin=false;
        bool Matches(const DragKey& other) const
        {return speed==other.speed && overrideValue==other.overrideValue && type==other.type
            && physics==other.physics && lob==other.lob && noSpin==other.noSpin;}
    };
    struct Solution { double pitch=0,time=0; };
    inline bool Solve(double distance,double height,double speed,double gravity,bool lob,Solution& out)
    {
        out={};
        if (!std::isfinite(distance)||!std::isfinite(height)||!std::isfinite(speed)||!std::isfinite(gravity)
            || distance<0 || speed<=0 || gravity<0) return false;
        if (gravity==0)
        {out.pitch=std::atan2(height,distance);out.time=std::hypot(distance,height)/speed;}
        else if (distance<.001)
        {
            const double root=speed*speed-2*gravity*height;
            if (root<0 || !std::isfinite(root)) return false;
            const double r=std::sqrt(root);
            const bool upward=lob || height>0;
            out.pitch=upward?1.5707963267948966:-1.5707963267948966;
            out.time=lob?(speed+r)/gravity:2*std::abs(height)/(speed+r);
        }
        else
        {
            const double v2=speed*speed,v4=v2*v2;
            double root=v4-gravity*(gravity*distance*distance+2*height*v2);
            if (!std::isfinite(root) || root<-1e-12*v4) return false;
            root=std::sqrt(std::max(0.,root));
            // Rationalized lower branch avoids subtracting nearly equal values.
            out.pitch=lob?std::atan2(v2+root,gravity*distance)
                :std::atan2(gravity*distance*distance+2*height*v2,distance*(v2+root));
            out.time=distance/(speed*std::cos(out.pitch));
        }
        return std::isfinite(out.pitch)&&std::isfinite(out.time)&&out.time>0&&out.time<=60;
    }
    // Gravity-only chord deviation budget; simulation and hull traces remain authoritative.
    constexpr double CollisionChordError=.25;
    inline int TraceInterval(int requested,double gravity,double tick,double error=CollisionChordError)
    {
        if (requested<1 || !std::isfinite(gravity)||!std::isfinite(tick)||!std::isfinite(error)
            || gravity<0 || tick<=0 || error<=0) return 1;
        if (gravity==0) return requested;
        const double limit=std::sqrt(8*error/gravity)/tick;
        return limit>=requested?requested:std::max(1,int(limit));
    }
    inline bool FlightWithin(double time,double lifetime,double tick,int ticks)
    {
        return std::isfinite(time)&&std::isfinite(lifetime)&&std::isfinite(tick)
            && time>0 && lifetime>0 && tick>0 && ticks>0
            && time<=lifetime && ticks*tick<=lifetime+tick;
    }
}
