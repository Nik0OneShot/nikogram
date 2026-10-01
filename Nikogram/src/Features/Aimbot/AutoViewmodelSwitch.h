#pragma once
#include "../../SDK/SDK.h"
#include "../Backtrack/Backtrack.h"
#include "ProjectileMuzzlePolicy.h"
#include "SelfDamageDiagnostics.h"

namespace AutoViewmodelSwitch
{
    inline bool owned=false,original=false,last=false;
    inline int requiredSequence=0;
    inline float earliest=0;
    inline bool Enabled()
    {return Vars::Aimbot::Projectile::Modifiers.Value&Vars::Aimbot::Projectile::ModifiersEnum::AutomaticViewmodelSwitch;}
    inline CNetChannel* Channel()
    {return reinterpret_cast<CNetChannel*>(I::EngineClient->GetNetChannelInfo());}
    inline bool Immediate(CTFWeaponBase* weapon)
    {
        if(!weapon || Vars::Visuals::Trajectory::Override.Value) return false;
        if(weapon->m_iItemDefinitionIndex()==Soldier_m_TheBeggarsBazooka) return false;
        switch(weapon->GetWeaponID())
        {
        case TF_WEAPON_ROCKETLAUNCHER: case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
        case TF_WEAPON_GRENADELAUNCHER: case TF_WEAPON_FLAREGUN: case TF_WEAPON_FLAREGUN_REVENGE:
        case TF_WEAPON_CROSSBOW: case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
        case TF_WEAPON_RAYGUN: case TF_WEAPON_DRG_POMSON: case TF_WEAPON_PARTICLE_CANNON:
        case TF_WEAPON_SYRINGEGUN_MEDIC: return true;
        }
        return false; // Charged/delayed releases are deliberately not switched.
    }
    inline bool Supported(CTFWeaponBase* weapon)
    {
        if(!weapon || Vars::Visuals::Trajectory::Override.Value) return false;
        const bool charged=weapon->GetWeaponID()==TF_WEAPON_PIPEBOMBLAUNCHER || weapon->GetWeaponID()==TF_WEAPON_COMPOUND_BOW;
        if(!ProjectileMuzzlePolicy::ChargedSwitchSafe(charged,charged?weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime():0.f)) return false;
        if(charged)
        {
            const float latency=F::Backtrack.GetReal(MAX_FLOWS,false);
            if(!std::isfinite(latency) || latency<0.f || latency>.15f) return false;
        }
        return (Immediate(weapon) || charged) && weapon->GetWeaponID()!=TF_WEAPON_PARTICLE_CANNON
            && !SDK::AttribHookValue(0,"centerfire_projectile",weapon);
    }
    inline bool DelayedBomb(CTFWeaponBase* weapon)
    {return weapon && (weapon->GetWeaponID()==TF_WEAPON_GRENADELAUNCHER || weapon->GetWeaponID()==TF_WEAPON_PIPEBOMBLAUNCHER || weapon->GetWeaponID()==TF_WEAPON_CANNON);}
    inline bool Set(bool flip,bool unloading=false)
    {
        auto* convar=H::ConVars.FindVar("cl_flipviewmodels"); auto* channel=Channel();
        const float latency=F::Backtrack.GetReal(MAX_FLOWS,false);
        if(!convar || !channel || !I::EngineClient->IsConnected() || (!unloading && I::ClientState->chokedcommands)
            || !std::isfinite(latency) || !std::isfinite(I::GlobalVars->curtime)) return false;
        NET_SetConVar message("cl_flipviewmodels",flip?"1":"0");
        if(!channel->SendNetMsg(message,true)) return false;
        requiredSequence=channel->m_nOutSequenceNr+1;
        earliest=I::GlobalVars->curtime+std::max(latency,2*TICK_INTERVAL);
        convar->SetValue(flip?1:0); last=flip;
        return true;
    }
    inline bool Commit(bool flip,int command)
    {
        auto* convar=H::ConVars.FindVar("cl_flipviewmodels");
        if(!convar) return false;
        const bool before=convar->GetBool();
        if(!Set(flip)) return false;
        if(!owned) original=before;
        owned=true;
        if(SelfDamageDiagnostics::Enabled()) SelfDamageDiagnostics::Write("projectile_viewmodel_switch",std::format("cmd={} previous={} selected={} sequence={} earliest={} shot_withheld=1",command,before,flip,requiredSequence,earliest));
        return true;
    }
    inline void Restore(bool unloading=false)
    {
        auto* convar=H::ConVars.FindVar("cl_flipviewmodels");
        if(!owned || !convar) return;
        if(convar->GetBool()!=last) {owned=false;requiredSequence=0;return;} // Preserve manual changes.
        if(!I::EngineClient->IsConnected()) {convar->SetValue(original?1:0);owned=false;requiredSequence=0;return;}
        if(Set(original,unloading)) owned=false;
    }
    inline void Frame()
    {
        auto* convar=H::ConVars.FindVar("cl_flipviewmodels");
        if(owned && convar && convar->GetBool()!=last) {owned=false;requiredSequence=0;}
        if(!Enabled() || !I::EngineClient->IsConnected()) Restore();
    }
    inline void Reset()
    {
        Restore(true);
        requiredSequence=0;earliest=0; // Packet numbers must not cross connections/maps.
    }
    inline bool Pending()
    {
        if(!requiredSequence) return false;
        auto* channel=Channel();
        if(!channel) return true;
        if(ProjectileMuzzlePolicy::Pending(channel->m_nOutSequenceNrAck,requiredSequence,I::GlobalVars->curtime,earliest)) return true;
        requiredSequence=0;return false;
    }
}
