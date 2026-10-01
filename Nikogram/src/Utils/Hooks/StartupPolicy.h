#pragma once
#include <atomic>

namespace StartupPolicy
{
    enum class Phase { Queued, Initializing, Ready, Failed };
    class Gate
    {
        std::atomic<Phase> phase{Phase::Queued};
    public:
        Phase State() const { return phase.load(std::memory_order_acquire); }
        bool Ready() const { return State()==Phase::Ready; }
        bool Begin(unsigned active)
        {
            if(active!=1)return false;
            auto expected=Phase::Queued;
            return phase.compare_exchange_strong(expected,Phase::Initializing,std::memory_order_acq_rel);
        }
        bool Complete(bool success)
        {
            auto expected=Phase::Initializing;
            return phase.compare_exchange_strong(expected,success?Phase::Ready:Phase::Failed,std::memory_order_acq_rel);
        }
        bool CancelQueued()
        {
            auto expected=Phase::Queued;
            return phase.compare_exchange_strong(expected,Phase::Failed,std::memory_order_acq_rel);
        }
    };
    inline Gate gate;
}
