#pragma once
#include "../../../SDK/SDK.h"
#include "RadarPolicy.h"
class CRadar
{
public:
    void Draw(CTFPlayer* local);
    bool SuppressCrit() const {return RadarPolicy::Suppress(Vars::Visuals::Radar::Enabled.Value,Vars::Visuals::Radar::Mode.Value,Vars::Visuals::Radar::HideStandalone.Value,Vars::Visuals::Radar::Crit.Value);}
    bool SuppressTicks() const {return RadarPolicy::Suppress(Vars::Visuals::Radar::Enabled.Value,Vars::Visuals::Radar::Mode.Value,Vars::Visuals::Radar::HideStandalone.Value,Vars::Visuals::Radar::Ticks.Value);}
    bool SuppressBinds() const {return RadarPolicy::Suppress(Vars::Visuals::Radar::Enabled.Value,Vars::Visuals::Radar::Mode.Value,Vars::Visuals::Radar::HideStandalone.Value,Vars::Visuals::Radar::Binds.Value);}
};
ADD_FEATURE(CRadar,Radar);
