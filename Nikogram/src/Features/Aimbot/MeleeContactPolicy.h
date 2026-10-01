#pragma once
#include <array>
#include <algorithm>
#include <cmath>

namespace MeleeContactPolicy
{
    using Point=std::array<double,3>;
    inline bool InFOV(double angle,double limit)
    {return std::isfinite(angle) && std::isfinite(limit) && angle>=0 && limit>0 && angle<limit;}

    // Closest box point to the view segment. Exact slab intersection first;
    // bounded convex-distance search otherwise. This selects candidates only:
    // the game's range, hull, obstacle and backstab checks remain authoritative.
    inline bool ViewPoint(const Point& eye,const Point& end,const Point& mins,const Point& maxs,Point& out)
    {
        out={};
        double enter=0,leave=1,length=0;
        bool intersects=true;
        for(int axis=0;axis<3;++axis)
        {
            if(!std::isfinite(eye[axis]) || !std::isfinite(end[axis]) || !std::isfinite(mins[axis])
                || !std::isfinite(maxs[axis]) || mins[axis]>maxs[axis]) return false;
            const double d=end[axis]-eye[axis];length+=d*d;
            if(std::abs(d)<1e-9)
            {if(eye[axis]<mins[axis] || eye[axis]>maxs[axis]) intersects=false;}
            else
            {
                double a=(mins[axis]-eye[axis])/d,b=(maxs[axis]-eye[axis])/d;
                if(a>b) std::swap(a,b);
                enter=std::max(enter,a);leave=std::min(leave,b);
            }
        }
        if(!std::isfinite(length) || length<1e-12) return false;
        auto sample=[&](double t,Point* point=nullptr)
        {
            double squared=0;
            for(int axis=0;axis<3;++axis)
            {
                const double value=eye[axis]+(end[axis]-eye[axis])*t;
                const double clamped=std::clamp(value,mins[axis],maxs[axis]);
                squared+=(value-clamped)*(value-clamped);
                if(point) (*point)[axis]=clamped;
            }
            return squared;
        };
        if(intersects && enter<=leave)
        {sample((enter+leave)*.5,&out);return true;}
        double low=0,high=1;
        for(int step=0;step<32;++step)
        {
            const double a=(2*low+high)/3,b=(low+2*high)/3;
            if(sample(a)<=sample(b)) high=b;else low=a;
        }
        double best=(low+high)*.5;
        if(sample(0)<sample(best)) best=0;
        if(sample(1)<sample(best)) best=1;
        sample(best,&out);return true;
    }
}
