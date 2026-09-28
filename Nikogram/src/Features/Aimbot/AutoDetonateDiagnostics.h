#pragma once
#include "SelfDamageDiagnostics.h"
#include <array>

namespace AutoDetonateDiagnostics
{
    enum Reason { Gate, Disabled, Empty, NoLauncher, Unarmed, Filtered, Invisible, Range, Visibility,
        SelectiveBusy, Accepted, PredictedMiss, CurrentMiss, SelfBlocked, Requested, Count };
    inline constexpr const char* names[] = {"entry_gate","source_disabled","no_projectiles","no_launcher","unarmed",
        "target_filtered","invisible","out_of_range","visibility_blocked","selective_busy","entity_pass",
        "predicted_miss","current_miss","self_damage_veto","request"};
    struct Capture { int command=0, source=0, projectile=-1, details=0, queries=0; const char* phase="entry"; std::array<int,Count> counts{}; };
    inline thread_local Capture* current=nullptr;
    inline void Event(Reason reason, int entity=-1, float value=0)
    {
        if(!current) return;
        ++current->counts[reason];
        if(current->details++>=16) return;
        SelfDamageDiagnostics::Write("autodet_stage",std::format("cmd={} source={} projectile={} phase={} reason={} entity={} value={}",
            current->command,current->source,current->projectile,current->phase,names[reason],entity,value));
    }
    struct Command
    {
        Capture capture{}; Capture* previous=current;
        Command(const CUserCmd* cmd)
        {
            current=nullptr;
            if(!SelfDamageDiagnostics::Enabled() || !Vars::Aimbot::Projectile::AutoDetonate.Value) return;
            static unsigned long long last=0;
            const auto now=GetTickCount64();
            if(last && now>=last && now-last<500) return;
            last=now; capture.command=cmd->command_number; current=&capture;
            SelfDamageDiagnostics::Write("autodet_begin",std::format("cmd={} enabled={} radius_percent={} target_flags={} ignore_flags={} original_buttons={}",
                capture.command,Vars::Aimbot::Projectile::AutoDetonate.Value,Vars::Aimbot::Projectile::AutodetRadius.Value,
                Vars::Aimbot::General::Target.Value,Vars::Aimbot::General::Ignore.Value,G::OriginalCmd.buttons));
        }
        ~Command()
        {
            if(current==&capture)
            {
                std::string counts;
                for(int i=0;i<Count;++i) counts+=std::format(" {}={}",names[i],capture.counts[i]);
                SelfDamageDiagnostics::Write("autodet_summary",std::format("cmd={} details_omitted={}{}",capture.command,std::max(capture.details-16,0),counts));
            }
            current=previous;
        }
    };
    // Independent current bounds check helps distinguish broadphase enumeration
    // failures from genuine absence of nearby enemies. No traces or state writes.
    inline void Query(CTFPlayer* local, const Vec3& origin, float radius, int returned, int directPlayers)
    {
        if(!current || current->queries++>=4) return;
        float nearest=std::numeric_limits<float>::max(); int index=-1, examined=0;
        for(auto entity : H::Entities.GetGroup(EntityEnum::PlayerAll))
        {
            if(++examined>64) break;
            auto player=entity->As<CTFPlayer>();
            if(player==local || !player->IsAlive() || player->m_iTeamNum()==local->m_iTeamNum()) continue;
            Vec3 point; player->m_Collision()->CalcNearestPoint(origin,&point);
            const float distance=origin.DistTo(point);
            if(distance<nearest) { nearest=distance; index=player->entindex(); }
        }
        SelfDamageDiagnostics::Write("autodet_query",std::format("cmd={} source={} projectile={} phase={} radius={} origin={},{},{} enumerated={} nearest_enemy={} nearest_distance={} player_scan_truncated={} direct_players_checked={}",
            current->command,current->source,current->projectile,current->phase,radius,origin.x,origin.y,origin.z,returned,index,index<0?-1.f:nearest,examined>64,directPlayers));
    }
}
