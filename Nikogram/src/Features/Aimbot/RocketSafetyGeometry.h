#pragma once
#include <array>
#include <algorithm>
#include <cmath>

namespace RocketSafetyGeometry
{
    using V3=std::array<float,3>;
    // Swept point vs player collision AABB; no studio hitboxes or entity mutation.
    // Returns the earliest contact on [0,1], including a muzzle starting inside a hull.
    inline bool SegmentBox(const V3& from,const V3& to,const V3& mins,const V3& maxs,float& fraction,V3& normal)
    {
        float enter=0.f, leave=1.f; normal={};
        for(int axis=0;axis<3;++axis)
        {
            if(!std::isfinite(from[axis]) || !std::isfinite(to[axis]) || !std::isfinite(mins[axis])
                || !std::isfinite(maxs[axis]) || mins[axis]>maxs[axis]) return false;
            const float delta=to[axis]-from[axis];
            if(std::fabs(delta)<1e-7f)
            { if(from[axis]<mins[axis] || from[axis]>maxs[axis]) return false; continue; }
            float first=(mins[axis]-from[axis])/delta, last=(maxs[axis]-from[axis])/delta;
            float sign=-1.f;
            if(first>last) {std::swap(first,last);sign=1.f;}
            if(first>enter) {enter=first;normal={};normal[axis]=sign;}
            leave=std::min(leave,last);
            if(enter>leave) return false;
        }
        fraction=enter;
        return true;
    }

    struct Heartbeat
    {
        unsigned long long last=0; bool initialized=false;
        bool Due(unsigned long long now,bool active)
        {
            if(active || !initialized || now<last || now-last>=5000)
            {last=now;initialized=true;return true;}
            return false;
        }
    };
}
