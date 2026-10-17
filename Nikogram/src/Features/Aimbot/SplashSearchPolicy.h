#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace SplashSearchPolicy
{
    enum Mode { Trace=0, Face=1, Dynamic=2 };
    inline int NormalizeMode(int mode) {return mode>=Trace && mode<=Dynamic ? mode : Trace;}
    // Dynamic is one merged search: trace candidates and face candidates share a
    // pool and are validated in rank order. Nothing runs "after" anything else.
    inline bool WantsTrace(int mode) {return mode!=Face;}
    inline bool WantsFace(int mode) {return mode!=Trace;}

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

    // ------------------------------------------------------------------
    // Candidate bookkeeping. Candidates are regenerated every command; each
    // one remembers how it did so a shot found last command is re-validated
    // first (steady aim) and recent failures are not re-tried every tick.
    // ------------------------------------------------------------------
    enum Source : uint8_t
    {
        SourceTrace = 0,      // ray from the target outward hit a surface
        SourceAir = 1,        // mid-air detonation point
        SourceFace = 2,       // face sampler (random jittered samples per face)
        SourceShooterRay = 3, // ray from the muzzle toward the target's surroundings
        SourceEdge = 4,       // systematic sample along an occluder's edge, judged with the terminal test (0.7.0.45/.46)
        SourceCorner = 5,     // sample at an occluder's corner, judged with every face normal meeting there (0.7.0.46)
        SourceRefined = 6     // found by refining a near miss around its impact (0.7.0.47)
    };

    // Validation order classes. Lower runs first.
    enum Priority { PriorityPassed = 0, PriorityUntested = 1, PriorityFailed = 2 };
    // A pass older than this many commands no longer jumps the queue.
    constexpr int PassMemoryCommands = 33;
    // Once a command has a validated shot, a nearer candidate that failed within
    // this many commands is left alone (it is re-tried later, not every tick).
    constexpr int RetryAfterPassCommands = 8;

    inline int PriorityOf(int attempts, int lastPass, int lastFail, int command)
    {
        if (lastPass >= 0 && lastPass >= lastFail && command - lastPass <= PassMemoryCommands) return PriorityPassed;
        if (attempts <= 0) return PriorityUntested;
        return PriorityFailed;
    }

    // Shortlist ordering: priority class, then (failed only) fewest attempts and
    // oldest failure, then nearest to the target, then generation order.
    template<class Point>
    inline bool RankBefore(const Point& a, const Point& b, float da, float db)
    {
        if (a.m_iPriority != b.m_iPriority) return a.m_iPriority < b.m_iPriority;
        if (a.m_iPriority == PriorityFailed)
        {
            if (a.m_iAttempts != b.m_iAttempts) return a.m_iAttempts < b.m_iAttempts;
            if (a.m_iLastFail != b.m_iLastFail) return a.m_iLastFail < b.m_iLastFail;
        }
        if (da != db) return da < db;
        return a.m_iSearchOrder < b.m_iSearchOrder;
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
            return RankBefore(a, b, a.m_vPoint.DistToSqr(origin), b.m_vPoint.DistToSqr(origin));
        });
        return end;
    }

    template<class Point, class Origin>
    void TakeNearest(std::vector<Point>& points, const Origin& origin, int limit)
    {
        points.resize(NextNearestBatch(points, origin, 0, limit));
    }
}
