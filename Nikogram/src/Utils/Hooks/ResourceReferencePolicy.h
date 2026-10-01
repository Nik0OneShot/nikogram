#pragma once
#include <map>

namespace ResourceReferencePolicy
{
    // Each successful CreateMaterial supplies one owned reference. Keep that
    // ownership separate from the list of materials used for render overrides.
    template<class Pointer> class References
    {
        std::map<Pointer,unsigned> owned;
    public:
        void Acquire(Pointer pointer){if(pointer)++owned[pointer];}
        unsigned Count(Pointer pointer)const
        {auto it=owned.find(pointer);return it==owned.end()?0:it->second;}
        bool Take(Pointer pointer)
        {
            auto it=owned.find(pointer);if(it==owned.end())return false;
            if(--it->second==0)owned.erase(it);return true;
        }
    };
}
