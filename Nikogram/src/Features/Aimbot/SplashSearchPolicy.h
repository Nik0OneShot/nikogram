#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace SplashSearchPolicy
{
    inline float SphereY(int index, int samples)
    {
        return samples <= 1 ? 0.f : 1.f - (index / (samples - 1.f)) * 2.f;
    }

    template<class Point, class Origin>
    void TakeNearest(std::vector<Point>& points, const Origin& origin, int limit)
    {
        const size_t count = std::min(size_t(std::max(1, limit)), points.size());
        std::partial_sort(points.begin(), points.begin() + count, points.end(), [&](const auto& a, const auto& b)
        {
            const float da = a.m_vPoint.DistToSqr(origin), db = b.m_vPoint.DistToSqr(origin);
            return da != db ? da < db : a.m_iSearchOrder < b.m_iSearchOrder;
        });
        points.resize(count);
    }
}
