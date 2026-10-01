#include "../SDK/SDK.h"
#include "../Features/SkinChanger/SkinChanger.h"

// Verified against the installed x64 client. These are UI readers only:
// C_TFPlayerResource::GetStreak, killstreak meter GetCount/IsEnabled and
// CTFHudDeathNotice::OnGameEvent(event, deathNoticeIndex).
MAKE_SIGNATURE(Skin_PlayerResourceStreak, "client.dll", "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F9 41 8B F0 48 81 C1 C0 07 00 00 8B DA", 0x0);
MAKE_SIGNATURE(Skin_KillstreakHUDCount, "client.dll", "48 83 EC 28 E8 ? ? ? ? 48 85 C0 74 0B 8B 80 6C 22 00 00", 0x0);
// Include the verified killstreak_tier reference: another item meter has the
// same instruction skeleton but queries a different player/attribute pair.
MAKE_SIGNATURE(Skin_KillstreakHUDEnabled, "client.dll", "48 83 EC 38 E8 ? ? ? ? 48 85 C0 74 23 45 33 C9 C6 44 24 20 01 4C 8B C0 48 8D 15 78 34 73 00 33 C9 E8 ? ? ? ? 85 C0 0F 95 C0", 0x0);
MAKE_SIGNATURE(Skin_KillstreakDeathNotice, "client.dll", "48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D0 48 81 EC 30 01 00 00 48 8B F2 45 8B E8", 0x0);
// CHudBaseDeathNotice::FireGameEvent builds the initial icon BEFORE OnGameEvent.
// Verified native entry point: scope the cosmetic event to this HUD reader only.
static thread_local IGameEvent* s_originalHudEvent=nullptr;
MAKE_SIGNATURE(Skin_DeathNoticeEvent, "client.dll", "4C 8B DC 49 89 53 10 49 89 4B 08 41 55 41 56 48 81 EC 18 03 00 00 48 83 3D ? ? ? ? 00 4C 8B F2 4C 8B E9",0x0);
MAKE_HOOK(Skin_DeathNoticeEvent,S::Skin_DeathNoticeEvent(),void,void* hud,IGameEvent* event)
{
    HookLifetime::Scope hookLifetimeScope;
    if(!event||std::string_view(event->GetName())!="player_death")return CALL_ORIGINAL(hud,event);
    SkinChanger::KillstreakEvent(event);const auto icon=SkinChanger::KillIconFor(event);
    if(icon.empty()||!I::GameEventManager)return CALL_ORIGINAL(hud,event);
    auto copy=I::GameEventManager->DuplicateEvent(event);if(!copy)return CALL_ORIGINAL(hud,event);
    struct Free{IGameEvent* event;~Free(){I::GameEventManager->FreeEvent(event);}} free{copy};
    copy->SetString("weapon",icon.c_str());
    // Native custom-damage icons (e.g. stock knife backstab) must not overwrite
    // the selected item's kill icon. Only this private HUD copy is changed.
    copy->SetInt("customkill",0);
    SkinChanger::ObserveCosmeticEffect("kill_icon_hud",icon+" early_private_event=true");
    struct EventScope {IGameEvent* previous;EventScope(IGameEvent* event):previous(s_originalHudEvent){s_originalHudEvent=event;}~EventScope(){s_originalHudEvent=previous;}} scope(event);
    CALL_ORIGINAL(hud,copy);
}

MAKE_HOOK(Skin_PlayerResourceStreak, S::Skin_PlayerResourceStreak(), int,
    void* resource,unsigned int playerIndex,int streakType)
{
    HookLifetime::Scope hookLifetimeScope;
    // Type zero is kills. Only local/verified shared players are overridden;
    // duck streaks and non-participating players remain native.
    if(streakType==0&&playerIndex>=1&&playerIndex<=128)
        if(auto count=SkinChanger::PlayerKillstreakDisplay(int(playerIndex)))
        {SkinChanger::ObserveKillstreakUI("scoreboard",*count,playerIndex!=static_cast<unsigned int>(I::EngineClient->GetLocalPlayer()));return *count;}
    return CALL_ORIGINAL(resource,playerIndex,streakType);
}

MAKE_HOOK(Skin_KillstreakHUDCount, S::Skin_KillstreakHUDCount(), int,void* meter)
{
    HookLifetime::Scope hookLifetimeScope;
    if(auto count=SkinChanger::LocalKillstreakDisplay())
    {SkinChanger::ObserveKillstreakUI("hud_count",*count);return *count;}
    return CALL_ORIGINAL(meter);
}

MAKE_HOOK(Skin_KillstreakHUDEnabled, S::Skin_KillstreakHUDEnabled(), bool,void* meter)
{
    HookLifetime::Scope hookLifetimeScope;
    if(SkinChanger::LocalKillstreakDisplay())return true;
    return CALL_ORIGINAL(meter);
}

MAKE_HOOK(Skin_KillstreakDeathNotice, S::Skin_KillstreakDeathNotice(), void,
    void* hud,IGameEvent* event,int deathNoticeIndex)
{
    HookLifetime::Scope hookLifetimeScope;
    if(!event||std::string_view(event->GetName())!="player_death")
        return CALL_ORIGINAL(hud,event,deathNoticeIndex);
    // Listener ordering is unspecified. Credit the original event now; the
    // common tracker ignores a second delivery by our general event listener.
    SkinChanger::KillstreakEvent(s_originalHudEvent?s_originalHudEvent:event);
    const int attacker=event->GetInt("attacker"),victim=event->GetInt("userid");
    if(!attacker||attacker==victim||(event->GetInt("death_flags")&TF_DEATH_FEIGN_DEATH))
        return CALL_ORIGINAL(hud,event,deathNoticeIndex);
    const int attackerIndex=I::EngineClient->GetPlayerForUserID(attacker);
    auto count=SkinChanger::KillstreakKillEligible(event)?SkinChanger::PlayerKillstreakDisplay(attackerIndex):std::optional<int>{};
    const auto icon=SkinChanger::KillIconFor(event);
    if((!count||*count<=0)&&icon.empty())return CALL_ORIGINAL(hud,event,deathNoticeIndex);
    auto manager=I::GameEventManager;
    auto copy=manager?manager->DuplicateEvent(event):nullptr;
    if(!copy)return CALL_ORIGINAL(hud,event,deathNoticeIndex);
    struct OwnedEvent
    {
        IGameEventManager2* manager;IGameEvent* event;
        ~OwnedEvent(){manager->FreeEvent(event);}
    } owned{manager,copy};
    // Private HUD input, never FireEvent/FireEventClientSide. The original
    // event, server counts and other listeners are untouched. Nothing is
    // broadcast to clients that are not participating in cosmetic sharing.
    if(!icon.empty())copy->SetString("weapon",icon.c_str());
    if(count&&*count>0)
    {
        copy->SetInt("kill_streak_total",*count);copy->SetInt("kill_streak_wep",*count);
        SkinChanger::ObserveKillstreakUI("death_notice",*count,attackerIndex!=I::EngineClient->GetLocalPlayer());
    }
    CALL_ORIGINAL(hud,copy,deathNoticeIndex);
    if(count&&*count>0)SkinChanger::SharedKillstreakMilestone(attackerIndex,*count);
}
