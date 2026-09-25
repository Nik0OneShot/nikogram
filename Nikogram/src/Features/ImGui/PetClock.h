#pragma once
#include <chrono>
#include <atomic>
namespace PetClock
{
    inline double Now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    inline std::atomic<double> LoadedAt{0};
    inline void Start(){LoadedAt.store(Now());}
    inline double Uptime(){const double t=LoadedAt.load();return t>0?Now()-t:0;}
}
