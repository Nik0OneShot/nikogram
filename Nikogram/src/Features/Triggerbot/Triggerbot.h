#pragma once
#include "../../SDK/SDK.h"
#include "TriggerPolicy.h"

class CTriggerbot
{
    TriggerPolicy::Timer m_timer;
    int m_lastCommand=-1,m_lastShot=-1,m_hitboxes=-1,m_history=-1;
    float m_fixed=-1,m_lower=-1,m_upper=-1;
    bool m_dynamic=false;
public:
    void Run(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd);
    void Reset() {m_timer.Reset();m_lastCommand=m_lastShot=-1;}
};
ADD_FEATURE(CTriggerbot,Triggerbot);
