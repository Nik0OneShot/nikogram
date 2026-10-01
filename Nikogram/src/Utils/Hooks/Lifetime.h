#pragma once
#include <atomic>

namespace HookLifetime
{
    inline std::atomic<unsigned> active=0;
    class Scope
    {
    public:
        Scope(){active.fetch_add(1,std::memory_order_acq_rel);}
        ~Scope(){active.fetch_sub(1,std::memory_order_acq_rel);}
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
    };
}
