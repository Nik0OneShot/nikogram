#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace TriggerAutomaticPolicy
{
    struct Context
    {
        uint32_t owner=0,weapon=0;
        int spawn=0,server=0;
        bool operator==(const Context&) const = default;
    };
    enum class Phase { Idle, Held, Tail };
    struct State
    {
        struct Receipt
        {
            int sequence=0,outgoing=0;
            uint64_t epoch=0;
            bool tail=false,observed=false,settled=false;
        };
        Context context;
        Phase phase=Phase::Idle;
        uint64_t epoch=1;
        int lastInput=0,remaining=0,selected=0;
        double deadline=0;
        std::array<Receipt,128> receipts{};

        void Cancel()
        {
            phase=Phase::Idle;remaining=selected=0;deadline=0;
            ++epoch;if(!epoch)++epoch;
            receipts={};
        }
        void Begin(Context next,int input,bool active,bool allowed,double now,int releaseCount)
        {
            if(next!=context || input<=0 || (lastInput && int64_t(input)!=int64_t(lastInput)+1)) Cancel();
            context=next;lastInput=input;
            if(!allowed || !std::isfinite(now)){Cancel();return;}
            if(active)
            {
                if(phase!=Phase::Held){Cancel();phase=Phase::Held;}
            }
            else if(phase==Phase::Held)
            {
                Cancel();phase=Phase::Tail;
                remaining=selected=std::clamp(releaseCount,3,6);deadline=now+5.;
            }
            if(phase==Phase::Tail && now>=deadline)Cancel();
        }
        int Pending() const
        {
            int count=0;
            for(const auto& r:receipts)if(r.epoch==epoch && r.tail && !r.observed && !r.settled)++count;
            return count;
        }
        bool CanRequest() const
        {return phase==Phase::Held || (phase==Phase::Tail && remaining>Pending());}
        Receipt* Find(int sequence,int outgoing)
        {
            if(sequence<=0)return nullptr;
            auto& r=receipts[size_t(sequence)%receipts.size()];
            return r.epoch==epoch && r.sequence==sequence && r.outgoing==outgoing?&r:nullptr;
        }
        bool Authorize(int sequence,int outgoing)
        {
            if(sequence<=0 || outgoing<=0)return false;
            if(Find(sequence,outgoing))return true;
            if(!CanRequest())return false;
            auto& r=receipts[size_t(sequence)%receipts.size()];
            // Never overwrite an unsettled tail reservation, even on ring wrap.
            if(r.epoch==epoch && r.tail && !r.observed && !r.settled)return false;
            r={sequence,outgoing,epoch,phase==Phase::Tail,false,false};
            return true;
        }
        bool Observe(Context current,int sequence,int outgoing)
        {
            auto* r=Find(sequence,outgoing);
            if(current!=context || !r || r->observed || r->settled)return false;
            r->observed=true;
            if(r->tail && phase==Phase::Tail && remaining>0)
            {
                --remaining;
                if(!remaining)phase=Phase::Idle;
            }
            return true;
        }
        void Settle(Context current,int sequence,int outgoing)
        {
            if(current!=context)return;
            if(auto* r=Find(sequence,outgoing))r->settled=true;
        }
    };
}
