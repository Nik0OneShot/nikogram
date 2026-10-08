#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace SplashSearchPolicy
{
    enum Mode { Trace=0, Face=1, Dynamic=2 };
    inline int NormalizeMode(int mode) {return mode>=Trace && mode<=Dynamic ? mode : Trace;}
    template<class TraceSearch,class FaceSearch,class AllowFace>
    bool TryDynamic(TraceSearch trace,FaceSearch face,AllowFace allowFace)
    {
        if(trace()) return true;
        return allowFace() && face();
    }
    inline float SphereY(int index, int samples)
    {
        return samples <= 1 ? 0.f : 1.f - (index / (samples - 1.f)) * 2.f;
    }

    // Only unscaled, unrotated mathematical directions belong here. Never cache
    // world points, trace results or target state. Larger requests use the same
    // uncached calculation rather than changing their sampling distribution.
    template<class Point>
    class SphereDirectionCache
    {
    public:
        static constexpr int MaxSamples = 512;
        static constexpr size_t Slots = 4;
        struct Entry
        {
            int samples = 0, filled = 0;
            std::array<Point, MaxSamples> directions{};

            template<class Generate>
            Point Get(int index, Generate generate)
            {
                if (index < 0 || index >= samples) return generate();
                if (index < filled) return directions[index];
                const Point point = generate();
                // Fill only the requested prefix, after the caller's budget
                // check. An interrupted search never eagerly finishes a sphere.
                if (index == filled) directions[filled++] = point;
                return point;
            }
        };

        Entry* Find(int samples)
        {
            if (samples <= 0 || samples > MaxSamples) return nullptr;
            for (auto& entry : entries)
                if (entry.samples == samples) return &entry;
            auto& entry = entries[next];
            next = (next + 1) % Slots;
            entry.samples = samples;
            entry.filled = 0;
            return &entry;
        }

    private:
        std::array<Entry, Slots> entries{};
        size_t next = 0;
    };

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
