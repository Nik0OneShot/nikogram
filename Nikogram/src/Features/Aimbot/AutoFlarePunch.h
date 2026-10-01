#pragma once
#include "../../SDK/SDK.h"
#include "FlareComboPolicy.h"

class CAutoFlarePunch
{
    FlareComboPolicy::State m_state;
    int m_targetIndex=-1,m_originalWeapon=-1,m_flareWeapon=-1;
    double m_lastAttempt=-10;
    double m_nextFlareAttack=0;
    int m_validatedCommand=-1,m_validatedTarget=-1;
    double m_lastDiagnostic=-10;
    double m_lastFrameDiagnostic=-10;
    std::string m_lastDiagnosticReason;
    bool Return(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd);
    void Abort(const char* reason,double now);
public:
    void Maintain(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd);
    void Diagnostic(const char* reason,CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd);
    void Event(IGameEvent* event,CTFPlayer* local);
    bool Run(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd);
    void Shot(CTFWeaponBase* weapon,CUserCmd* cmd);
    void Validated(CTFWeaponBase* weapon,CUserCmd* cmd,int target);
    int Target() const {return m_state.active && m_state.switched?m_targetIndex:-1;}
    void Reset(const char* reason="reset");
};
ADD_FEATURE(CAutoFlarePunch,AutoFlarePunch);
