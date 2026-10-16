#pragma once
#include <cstdint>

namespace AutoFirePolicy
{
    struct Context
    {
        std::uint32_t owner=0,weapon=0;
        int spawn=0,server=0,mode=0,style=0;
        bool operator==(const Context&) const = default;
    };
    struct Latch
    {
        Context context{};
        int command=-1,ownedCommand=-1;
        bool active=false,armed=false,paced=false,priorOwned=false;
        void Begin(const Context& next,int number,bool allowed)
        {
            if(!allowed || !(next==context) || number<=0 || std::int64_t(number)!=std::int64_t(command)+1)
                armed=paced=false;
            context=next;command=number;active=allowed&&number>0;ownedCommand=-1;
        }
        bool Continue(bool ready,bool manual,bool requested)
        {
            if(!active || !armed || !ready || manual || requested)return false;
            ownedCommand=command;return true;
        }
        void Acquire(bool selectedAutoShot,bool ready,bool manual,bool outgoing,bool shotPaced)
        {
            if(!active || !selectedAutoShot || !ready || manual || !outgoing)return;
            armed=true;paced=shotPaced;ownedCommand=command;
        }
        bool Finalize(bool compatibilityAdded,bool beforeCompatibility,bool manual)
        {
            const bool reject=compatibilityAdded&&priorOwned&&!manual;
            priorOwned=ownedCommand==command&&beforeCompatibility&&!manual;
            return reject;
        }
    };
}
