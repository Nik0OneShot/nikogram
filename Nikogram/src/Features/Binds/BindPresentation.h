#pragma once
#include "../Aimbot/AimModes.h"

// Display state is not the condition state: a satisfied bind may DISABLE its
// setting. Never feed this back into bind evaluation or parent traversal.
namespace BindPresentation
{
    struct State { bool active; bool feature; };
    template<class Bind,class Binds> bool ParentsActive(const Bind& bind,const Binds& binds)
    {
        int parent=bind.m_iParent;
        for(size_t depth=0;parent!=DEFAULT_BIND;++depth)
        {
            if(parent<0||parent>=int(binds.size())||depth>=binds.size())return false;
            const auto& p=binds[parent];if(!p.m_bEnabled||!p.m_bActive)return false;
            parent=p.m_iParent;
        }
        return true;
    }
    template<class Bind> State Get(const Bind& bind, int id, bool parentActive, bool nikogram)
    {
        const bool eligible=parentActive&&bind.m_bEnabled;
        const State condition{eligible&&bind.m_bActive,false};
        if(bind.m_iType!=0||bind.m_vVars.size()!=1)return condition;
        auto* base=bind.m_vVars.front();
        auto* var=base->template As<bool>();
        if(!var||!var->Map.contains(DEFAULT_BIND)||!var->Map.contains(id)||var->Map.at(DEFAULT_BIND)==var->Map.at(id))return condition;
        bool active=var->Value;
        if(var->m_iFlags&TOGGLE_INVERT)active=!active;
        if(base==&Vars::AimModes::RageActivation)
            active=nikogram&&AimModes::Active==AimModes::Rage;
        else if(base==&Vars::AimModes::LegitActivation)
            active=nikogram&&AimModes::Active==AimModes::Legit;
        else if(base==&Vars::Triggerbot::Activation)
            active=active&&Vars::Triggerbot::Enabled.Value&&Vars::Triggerbot::Hitboxes.Value!=0;
        else if(base==&Vars::ESP::MoonlitEnabled)
            active=active&&Vars::ESP::MoonlitMaster.Value;
        else
        {
            // A bind on a per-mode setting cannot advertise an inactive bank.
            for(const auto& entry:AimModes::Entries())for(int mode=0;mode<2;++mode)
                if(entry.mode[mode].get()==base)active=active&&nikogram&&AimModes::Active==mode;
        }
        return {eligible&&active,true};
    }
}
