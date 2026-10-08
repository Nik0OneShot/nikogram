#pragma once
#include "SplashWorker.h"
#include "SplashSearchPolicy.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <random>

namespace TracePreparation
{
    // Relative offsets only: no entity, view, world, or collision state.
    struct Key
    {
        uint64_t generation=0;
        int samples=0;
        float radius=0, rotateX=0, rotateY=0;
        static float Rotation(float value)
        { return value<0.f ? -1.f : !std::isfinite(value) ? 0.f : value; }
        bool operator==(const Key&) const = default;
        bool Valid() const
        { return samples>0 && samples<=512 && std::isfinite(radius) && radius>0.f
            && std::isfinite(rotateX) && std::isfinite(rotateY); }
        bool Random() const { return rotateX<0.f || rotateY<0.f; }
    };
    template<class Point> struct Result
    {
        Key key;
        std::array<std::vector<Point>,4> patterns;
        std::array<std::array<float,2>,4> rotations{};
        size_t filled=0;
    };
    template<class Point, class Rotate> struct Job
    {
        Key key;
        uint32_t seed=0;
        Result<Point> Run(const std::atomic<bool>& stopping)
        {
            Result<Point> result; result.key=key;
            if(!key.Valid()) return result;
            std::mt19937 random(seed);
            std::uniform_real_distribution<float> rotation(0.f,360.f);
            static thread_local SplashSearchPolicy::SphereDirectionCache<Point> cache;
            auto* directions=cache.Find(key.samples);
            constexpr float pi=3.14159265358979323846f;
            const float angle=pi*(3.f-sqrtf(5.f));
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(2);
            const size_t count=key.Random() ? result.patterns.size() : 1;
            for(size_t p=0;p<count;++p)
            {
                const float rx=key.rotateX<0.f ? rotation(random) : key.rotateX;
                const float ry=key.rotateY<0.f ? rotation(random) : key.rotateY;
                auto& points=result.patterns[p];
                points.reserve(size_t(key.samples)+1);
                points.emplace_back(Point(0.f,0.f,-1.f)*key.radius);
                for(int n=0;n<key.samples;++n)
                {
                    if(stopping.load(std::memory_order_relaxed) || std::chrono::steady_clock::now()>=deadline)
                    { points.clear(); return result; } // Never publish a partial sphere.
                    const Point unit=directions->Get(n,[&]
                    {
                        const float t=angle*n, y=SplashSearchPolicy::SphereY(n,key.samples);
                        const float r=sqrtf(std::max(0.f,1-y*y));
                        return Point(cosf(t)*r,y,sinf(t)*r);
                    });
                    points.push_back(Rotate{}(unit*key.radius,rx,ry));
                }
                result.rotations[p]={rx,ry};
                ++result.filled;
            }
            return result;
        }
    };

    template<class Point, class Rotate> class Pipeline
    {
        using Output=Result<Point>;
        using Work=Job<Point,Rotate>;
        SplashWorker::Mailbox<Work,Output> worker;
        std::array<std::unique_ptr<Output>,4> cache;
        size_t next=0;
        int lastCommand=0;
        bool submittedOnce=false;
        std::atomic<uint64_t> generation{0};
    public:
        bool Start() { return worker.Start(); }
        void Stop() { worker.Stop(); Invalidate(); }
        void Invalidate() { generation.fetch_add(1,std::memory_order_relaxed); }

        // Called exclusively by the aiming thread. One background submission
        // per command at most, not one worker or queued job per target.
        template<class Seed>
        std::vector<Point> Get(Key key,int command,Seed seed,bool& submitted)
        {
            submitted=false;
            key.generation=generation.load(std::memory_order_relaxed);
            if(!key.Valid()) return {};
            if(auto ready=worker.Take([](const Output&){return true;}))
            {
                if(ready->filled && ready->key.generation==key.generation)
                {
                    size_t slot=next;
                    for(size_t i=0;i<cache.size();++i)
                        if(cache[i] && cache[i]->key==ready->key) {slot=i;break;}
                    cache[slot]=std::move(ready);
                    next=(slot+1)%cache.size();
                }
            }
            Output* match=nullptr;
            for(auto& entry:cache) if(entry && entry->key==key) {match=entry.get();break;}
            std::vector<Point> points;
            if(match && match->filled)
            {
                if(key.Random()) points=std::move(match->patterns[--match->filled]);
                else points=match->patterns[0]; // Fixed rotations are reusable across all targets.
            }
            const bool replenish=!match || (key.Random() && match->filled<=1);
            if(replenish && (!submittedOnce || command!=lastCommand) && worker.Available())
            {
                auto job=std::make_unique<Work>(); job->key=key; job->seed=seed();
                submitted=worker.Submit(std::move(job));
                if(submitted) {lastCommand=command;submittedOnce=true;}
            }
            return points;
        }
    };
}
