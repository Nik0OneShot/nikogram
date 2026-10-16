#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace FeetPointPolicy
{
    // Authored lower-body sampling, not an engine tolerance or enlarged hitbox.
    inline std::optional<float> Height(const std::array<float,3>& mins,
        const std::array<float,3>& maxs,const std::array<float,3>& hull,float primary)
    {
        if(!std::isfinite(primary)) return {};
        for(int axis=0;axis<3;++axis)
            if(!std::isfinite(mins[axis]) || !std::isfinite(maxs[axis]) || !std::isfinite(hull[axis])
                || hull[axis]<0.f || maxs[axis]<=mins[axis]) return {};
        // Existing points use x=y=0 relative to the target; keep the hull inside.
        for(int axis=0;axis<2;++axis)
            if(mins[axis]>-hull[axis] || maxs[axis]<hull[axis]) return {};
        const double center=(double(mins[2])+maxs[2])*.5;
        const double lower=std::max(double(primary),double(mins[2])+hull[2]);
        const double upper=center-hull[2];
        if(primary<mins[2] || primary>=center || upper<=lower) return {};
        const float height=float(lower+(upper-lower)*.5);
        if(!std::isfinite(height) || height<=primary || height+hull[2]>=center) return {};
        return height;
    }

    template<class Map> auto GenerationOrder(const Map& points)
    {
        std::vector<typename Map::key_type> keys;
        keys.reserve(points.size());
        bool deferred=false;
        for(const auto& [key,point]:points) {keys.push_back(key);deferred|=point.m_bDeferred;}
        // Preserve old unordered traversal when no new destination was added.
        if(deferred) std::sort(keys.begin(),keys.end(),[&](auto a,auto b)
        {
            const bool ad=points.at(a).m_bDeferred,bd=points.at(b).m_bDeferred;
            return ad!=bd ? !ad : a<b;
        });
        return keys;
    }
}
