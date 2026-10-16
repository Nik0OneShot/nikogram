#pragma once
#include "../../SDK/SDK.h"
#include "TriggerPolicy.h"
#include "TriggerAutomaticPolicy.h"

namespace TriggerAutomaticPolicy { bool ObserverReady(); }

class CTriggerbot
{
    TriggerPolicy::Timer m_timer;
    TriggerAutomaticPolicy::State m_automatic;
    bool m_shotAutomatic=false;
    int m_automaticInput=0;
    bool RunAutomatic(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd);
public:
    struct NativeObservation
    {
        TriggerAutomaticPolicy::Context context;
        CTFPlayer* local=nullptr;
        CUserCmd* command=nullptr;
        int sequence=0,outgoing=0;
        bool valid=false;
    };
    NativeObservation BeginNative(CTFPlayer* local,int sequence,CUserCmd* cmd);
    void EndNative(const NativeObservation& previous);
    void ObserveNativeShot(CTFWeaponBase* weapon,int player,int mode);
    void CommitAutomatic(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd,int inputSequence);
private:
    NativeObservation m_native;

    int m_lastCommand=-1,m_lastShot=-1,m_hitboxes=-1,m_history=-1;
    float m_fixed=-1,m_lower=-1,m_upper=-1;
    bool m_dynamic=false,m_disableDelay=false,m_triggerAllowed=false,m_priorCompatibilityTrigger=false,m_shotCommitted=false;
    uint32_t m_owner=0;
    int m_spawn=-1,m_server=-1;
    int m_shotCommand=-1,m_shotTarget=-1,m_shotWeapon=-1,m_beforeShotTick=0,m_shotTick=0;
    unsigned long long m_diagnosticAt[2]{};
    void Diagnostic(const char* reason,CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd,
        bool finalStage=false,const Vec3* ray=nullptr,int target=-1,int hitbox=-1,bool historical=false);
public:
    void Run(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd);
    void Finalize(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd,const Vec3& shotAngles,bool compatibilityAddedAttack,bool attackBeforeCompatibility);
    void Reset() {m_automatic.Cancel();m_shotAutomatic=false;m_native={};m_timer.Reset();m_lastCommand=m_lastShot=m_shotCommand=-1;m_triggerAllowed=m_priorCompatibilityTrigger=false;}
};
ADD_FEATURE(CTriggerbot,Triggerbot);
