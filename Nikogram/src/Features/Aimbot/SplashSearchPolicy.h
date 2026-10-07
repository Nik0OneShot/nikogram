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
    size_t NextNearestBatch(std::vector<Point>& points, const Origin& origin, size_t offset, int limit)
    {
        if (offset >= points.size()) return points.size();
        const size_t end = offset + std::min(size_t(std::max(1, limit)), points.size() - offset);
        // Keep the unvalidated tail. Refill only after the caller has exhausted
        // this batch and checked its remaining work budget.
        std::partial_sort(points.begin() + offset, points.begin() + end, points.end(), [&](const auto& a, const auto& b)
        {
            const float da = a.m_vPoint.DistToSqr(origin), db = b.m_vPoint.DistToSqr(origin);
            return da != db ? da < db : a.m_iSearchOrder < b.m_iSearchOrder;
        });
        return end;
    }

    template<class Point, class Origin>
    void TakeNearest(std::vector<Point>& points, const Origin& origin, int limit)
    {
        points.resize(NextNearestBatch(points, origin, 0, limit));
    }
}
