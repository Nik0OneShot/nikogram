#include "../SDK/SDK.h"

#include "../Features/Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include "../Features/SkinChanger/SkinChanger.h"
#include "../Features/SkinChanger/RenderPolicy.h"

MAKE_SIGNATURE(CParticleProperty_Create_Name, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 48 8B 59 ? 49 8B F1", 0x0);
MAKE_SIGNATURE(CParticleProperty_Create_Point, "client.dll", "44 89 4C 24 ? 44 89 44 24 ? 53", 0x0);
MAKE_SIGNATURE(CParticleProperty_AddControlPoint_Pointer, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 55 41 56 41 57 48 83 EC ? 4C 8B BC 24", 0x0);
MAKE_SIGNATURE(CWeaponMedigun_UpdateEffects_CreateName_Call1, "client.dll", "49 8B CC F3 0F 11 74 24 ? 48 8B D8", 0x0);
MAKE_SIGNATURE(CWeaponMedigun_UpdateEffects_CreateName_Call2, "client.dll", "41 8B 14 24 48 8B D8", 0x0);
MAKE_SIGNATURE(CWeaponMedigun_ManageChargeEffect_CreateName_Call, "client.dll", "48 89 86 ? ? ? ? 48 89 BE ? ? ? ? 48 83 BE", 0x0);
// Verified against the installed x64 client and Valve's econ_entity.cpp. Keep
// schema descriptors opaque: the engine owns their layout and lifetime.
MAKE_SIGNATURE(Skin_GetItemSchema, "client.dll", "48 83 EC 28 E8 ? ? ? ? 48 83 C0 08 48 83 C4 28 C3", 0x0);
// Schema tree offset moved in the October 6 build. Match the lookup's code
// rather than embedding the schema member offset in the signature.
MAKE_SIGNATURE(Skin_GetWeaponParticle, "client.dll", "4C 8B DC 49 89 5B 10 49 89 73 18 57 48 81 EC 80 00 00 00 0F B7 99 ? ? ? ? 33 C0 49 89 43 A0 0F 57 C0", 0x0);
MAKE_SIGNATURE(Skin_UpdateWeaponParticle, "client.dll", "4C 89 44 24 18 88 54 24 10 55 41 54 41 56 48 8D 6C 24 B9 48 81 EC B0 00 00 00 4D 8B E0 4C 8B F1", 0x0);
MAKE_SIGNATURE(Skin_UpdateKillstreakEyes, "client.dll", "44 88 44 24 18 89 54 24 10 55 53 56 57 41 57 48 8D 6C 24 C9 48 81 EC 90 00 00 00 48 8B 91 28 29 00 00", 0x0);

static thread_local bool s_bRestoringKillstreak=false;
MAKE_HOOK(Skin_UpdateKillstreakEyes, S::Skin_UpdateKillstreakEyes(), void, CTFPlayer* player,int count,bool scored)
{
    HookLifetime::Scope hookLifetimeScope;
    auto base=player?player->m_hActiveWeapon().Get():nullptr;
    auto profile=base&&!s_bRestoringKillstreak?SkinChanger::KillstreakFor(base->As<CTFWeaponBase>()):std::nullopt;
    SkinChanger::KillstreakAttributeScope scope(profile);
    // Native code handles attachments, Demoman's single eye, color control
    // points, cloak/disguise rules and removal of its own previous particles.
    CALL_ORIGINAL(player,profile?profile->count:count,scored);
}

void SkinChanger::UpdateKillstreakParticles(bool clear)
{
    struct Record {EHANDLE player;uint32_t weapon=0;int tier=0,sheen=0,effect=0,count=0,team=0;bool firstPerson=false,cloaked=false,disguised=false;
        bool Same(const Record& other)const{return weapon==other.weapon&&tier==other.tier&&sheen==other.sheen&&effect==other.effect&&count==other.count&&team==other.team&&firstPerson==other.firstPerson&&cloaked==other.cloaked&&disguised==other.disguised;}};
    static std::map<uint32_t,Record> active;
    if(!S::Skin_UpdateKillstreakEyes())return;
    std::map<uint32_t,Record> wanted;
    auto local=H::Entities.GetLocal();
    if(!clear&&!G::Unload&&Vars::Misc::SkinChanger::Enabled.Value&&local&&I::EngineClient->IsInGame()&&!SDK::CleanScreenshot())
        for(int index=1;index<=std::min(I::EngineClient->GetMaxClients(),128);++index)
        {
            auto client=I::ClientEntityList->GetClientEntity(index);auto player=client?client->As<CTFPlayer>():nullptr;
            if(!player||!player->IsPlayer()||!player->IsAlive()||player->IsDormant())continue;
            auto base=player->m_hActiveWeapon().Get();auto profile=base?KillstreakFor(base->As<CTFWeaponBase>()):std::nullopt;if(!profile)continue;
            wanted.emplace(uint32_t(player->GetRefEHandle().ToInt()),Record{player->GetRefEHandle(),uint32_t(base->GetRefEHandle().ToInt()),profile->tier,profile->sheen,profile->effect,profile->count,player->m_iTeamNum(),player==local&&!I::Input->CAM_IsThirdPerson(),player->InCond(TF_COND_STEALTHED),player->InCond(TF_COND_DISGUISED)});
        }
    for(auto it=active.begin();it!=active.end();)
    {
        if(wanted.contains(it->first)){++it;continue;}
        auto entity=it->second.player.Get();
        if(entity&&entity->IsPlayer()&&entity->As<CTFPlayer>()->IsAlive()&&!entity->IsDormant())
        {
            int native=0;static const int offset=U::NetVars.GetNetVar("CTFPlayer","m_nStreaks");
            if(offset>0&&offset<65536)std::memcpy(&native,reinterpret_cast<const char*>(entity)+offset,sizeof(native));
            SkinRender::ViewmodelDrawScope restoring(s_bRestoringKillstreak,true);
            S::Skin_UpdateKillstreakEyes.Call<void>(entity->As<CTFPlayer>(),std::clamp(native,0,1000000),false);
        }
        it=active.erase(it);
    }
    for(auto& [key,next]:wanted)
    {
        auto it=active.find(key);if(it!=active.end()&&it->second.Same(next))continue;
        auto entity=next.player.Get();if(!entity||!entity->IsPlayer())continue;
        S::Skin_UpdateKillstreakEyes.Call<void>(entity->As<CTFPlayer>(),next.count,false);
        ObserveCosmeticEffect("killstreak_native_eyes",std::format("tier{}_sheen{}_effect{}_count{}_thirdperson{}",next.tier,next.sheen,next.effect,next.count,!next.firstPerson));
        active[key]=next;
    }
}

void SkinChanger::UpdateUnusualParticles(bool clear)
{
    struct Record {EHANDLE weapon;int effect=0;std::string name;uintptr_t model=0;int modelIndex=0;bool firstPerson=false;};
    static std::map<unsigned long,Record> active;
    if(active.empty()&&(clear||G::Unload||!Vars::Misc::SkinChanger::Enabled.Value))return;
    const auto local=H::Entities.GetLocal();
    const auto schema=S::Skin_GetItemSchema()?S::Skin_GetItemSchema.Call<void*>():nullptr;
    if(!schema||!S::Skin_GetWeaponParticle()||!S::Skin_UpdateWeaponParticle())
    {SkinChanger::ObserveCosmeticEffect("weapon_unusual_backend_unavailable","native_signature_or_schema_missing");return;}
    auto stop=[&](const Record& r,bool restore)
    {
        auto base=r.weapon.Get();if(!local||!base)return;
        auto weapon=base->As<CTFWeaponBase>();auto parent=weapon->m_hOwner().Get();
        // Native econ particle routing expects a player owner. A removed entity
        // cleans its own particles; never call through stale raw effect pointers.
        if(!parent||!parent->IsPlayer())return;
        if(auto system=S::Skin_GetWeaponParticle.Call<void*>(schema,r.effect))
            S::Skin_UpdateWeaponParticle.Call<void>(weapon,false,system);
        if(restore&&parent->As<CTFPlayer>()->IsAlive()&&parent->As<CTFPlayer>()->m_hActiveWeapon().Get()==weapon)
        {
            const float original=SDK::AttribHookValue(0.f,"set_attached_particle",weapon);
            if(std::isfinite(original)&&original>=701.f&&original<=704.f&&std::floor(original)==original)
                if(auto system=S::Skin_GetWeaponParticle.Call<void*>(schema,int(original)))
                    S::Skin_UpdateWeaponParticle.Call<void>(weapon,true,system);
        }
    };
    std::map<unsigned long,Record> wanted;
    if(!clear&&!G::Unload&&Vars::Misc::SkinChanger::Enabled.Value&&local&&I::EngineClient->IsInGame()&&!SDK::CleanScreenshot())
    {
        for(int index=1;index<=std::min(I::EngineClient->GetMaxClients(),128);++index)
        {
            auto client=I::ClientEntityList->GetClientEntity(index);auto player=client?client->As<CTFPlayer>():nullptr;
            if(!player||!player->IsPlayer()||player->IsDormant()||!player->IsAlive())continue;
            auto base=player->m_hActiveWeapon().Get();if(!base)continue;
            auto weapon=base->As<CTFWeaponBase>();auto name=SkinChanger::UnusualFor(weapon);if(name.empty())continue;
            const int effect=name.starts_with("weapon_unusual_hot_")?701:name.starts_with("weapon_unusual_isotope_")?702:name.starts_with("weapon_unusual_cool_")?703:704;
            auto model=weapon->GetAppropriateWorldOrViewModel();if(!model)continue;
            const auto key=static_cast<unsigned long>(weapon->GetRefEHandle().ToInt());
            const bool firstPerson=player==local&&!I::Input->CAM_IsThirdPerson();
            Record next{weapon->GetRefEHandle(),effect,name,reinterpret_cast<uintptr_t>(model),model->m_nModelIndex(),firstPerson};
            wanted.emplace(key,std::move(next));
        }
    }
    // Retire old viewmodel effects BEFORE installing new ones. Two successive
    // weapons may share a suffix and attachment; late cleanup would erase the
    // new weapon's effect from the reused native viewmodel.
    for(auto it=active.begin();it!=active.end();)
        if(!wanted.contains(it->first)){stop(it->second,true);it=active.erase(it);}else ++it;
    for(auto& [key,next]:wanted)
    {
        auto base=next.weapon.Get();if(!base)continue;auto weapon=base->As<CTFWeaponBase>();
        auto it=active.find(key);
        if(it!=active.end()&&it->second.effect==next.effect&&it->second.name==next.name&&it->second.model==next.model
            &&it->second.modelIndex==next.modelIndex&&it->second.firstPerson==next.firstPerson)continue;
        if(it!=active.end()){stop(it->second,false);active.erase(it);}
        auto system=S::Skin_GetWeaponParticle.Call<void*>(schema,next.effect);if(!system)continue;
        S::Skin_UpdateWeaponParticle.Call<void>(weapon,true,system);
        SkinChanger::ObserveCosmeticEffect("weapon_unusual_native_route",next.name);
        active.emplace(key,std::move(next));
    }
}

MAKE_HOOK(Get_ParticleSystemIndex, S::Get_ParticleSystemIndex(), int, const char* name)
{
    DEBUG_RETURN(Get_ParticleSystemIndex,name);
    const int index=CALL_ORIGINAL(name);
    SkinChanger::ObserveParticleLookup(name,index);
    return index;
}

bool SkinChanger::CreateCosmeticTracer(const std::string& name,const Vector& start,const Vector& end,int entity,int attachment)
{
    if(name.empty()||!SkinModel::SafeEffectName(name)||G::Unload||SDK::CleanScreenshot())return false;
    for(int n=0;n<3;++n)if(!std::isfinite(start[n])||!std::isfinite(end[n]))return false;
    auto client=I::ClientEntityList->GetClientEntity(entity);auto shooter=client?client->As<CTFPlayer>():nullptr;
    if(!shooter||!shooter->IsPlayer()||shooter->IsDormant())return false;
    static auto drawTracers=I::CVar->FindVar("r_drawtracers");
    static auto drawFirstPerson=I::CVar->FindVar("r_drawtracers_firstperson");
    if(drawTracers&&!drawTracers->GetBool())return false;
    if(shooter==H::Entities.GetLocal()&&!I::Input->CAM_IsThirdPerson()&&drawFirstPerson&&!drawFirstPerson->GetBool())return false;
    auto weapon=shooter->m_hActiveWeapon().Get();if(!weapon)return false;
    auto property=shooter->m_Particles();if(!property||!S::CParticleProperty_Create_Point()||!S::CParticleProperty_AddControlPoint_Pointer())return false;
    Vector origin=start;
    if(attachment>=0)
    {
        auto model=weapon->As<CTFWeaponBase>()->GetAppropriateWorldOrViewModel();
        if(model&&attachment>0)model->GetAttachment(attachment,origin);
    }
    // WORLDORIGIN avoids batching/restarting a previous beam, and detaches both
    // endpoints from player movement. The native shot endpoint is unchanged.
    return SkinRender::CreateNamedTracer([&]{return S::CParticleProperty_Create_Point.Call<void*>(property,name.c_str(),PATTACH_WORLDORIGIN,0,origin);},
        [&](void* effect,int point){S::CParticleProperty_AddControlPoint_Pointer.Call<void>(property,effect,point,static_cast<CBaseEntity*>(nullptr),PATTACH_WORLDORIGIN,static_cast<const char*>(nullptr),point==0?origin:end);});
}

MAKE_HOOK(CParticleProperty_Create_Name, S::CParticleProperty_Create_Name(), void*,
	void* rcx, const char* pszParticleName, ParticleAttachment_t iAttachType, const char* pszAttachmentName)
{
    DEBUG_RETURN(CParticleProperty_Create_Name, rcx, pszParticleName, iAttachType, pszAttachmentName);
    if(pszParticleName&&std::string_view(pszParticleName).starts_with("weapon_unusual_"))
    {
        auto effect=CALL_ORIGINAL(rcx,pszParticleName,iAttachType,pszAttachmentName);
        SkinChanger::ObserveParticleCreation("weapon_unusual",pszParticleName,pszParticleName,effect!=nullptr);
        return effect;
    }
    const auto cosmeticMuzzle=SkinChanger::MuzzleFor(rcx,pszParticleName);
    if(!cosmeticMuzzle.empty())
    {
        auto effect=CALL_ORIGINAL(rcx,cosmeticMuzzle.c_str(),iAttachType,pszAttachmentName);
        SkinChanger::ObserveParticleCreation("muzzle",pszParticleName,cosmeticMuzzle,effect!=nullptr);
        if(effect)return effect; // Fall back to stock if the requested particle cannot load.
    }

    const auto dwRetAddr = uintptr_t(_ReturnAddress());
    const auto dwUpdateEffects1 = S::CWeaponMedigun_UpdateEffects_CreateName_Call1();
    const auto dwUpdateEffects2 = S::CWeaponMedigun_UpdateEffects_CreateName_Call2();
    const auto dwManageChargeEffect = S::CWeaponMedigun_ManageChargeEffect_CreateName_Call();

    bool bUpdateEffects = dwRetAddr == dwUpdateEffects1 || dwRetAddr == dwUpdateEffects2, bManageChargeEffect = dwRetAddr == dwManageChargeEffect;
    if (bUpdateEffects || bManageChargeEffect)
    {
        auto pLocal = H::Entities.GetLocal();
        if (!pLocal)
            return CALL_ORIGINAL(rcx, pszParticleName, iAttachType, pszAttachmentName);

        /* // probably not needed
        auto pWeapon = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
        if (!pWeapon || pWeapon->GetWeaponID() != TF_WEAPON_MEDIGUN)
            return CALL_ORIGINAL(rcx, pszParticleName, iAttachType, pszAttachmentName);
        */

        auto pModel = pLocal->GetRenderedWeaponModel();
        if (!pModel || rcx != pModel->m_Particles())
            return CALL_ORIGINAL(rcx, pszParticleName, iAttachType, pszAttachmentName);

        bool bBlue = pLocal->m_iTeamNum() == TF_TEAM_BLUE;
        if (bUpdateEffects)
        {
            switch (FNV1A::Hash32(Vars::Visuals::Effects::MedigunBeam.Value.c_str()))
            {
            case FNV1A::Hash32Const("Default"): break;
            case FNV1A::Hash32Const("None"): return nullptr;
            case FNV1A::Hash32Const("Uber"): pszParticleName = bBlue ? "medicgun_beam_blue_invun" : "medicgun_beam_red_invun"; break;
            case FNV1A::Hash32Const("Dispenser"): pszParticleName = bBlue ? "dispenser_heal_blue" : "dispenser_heal_red"; break;
            case FNV1A::Hash32Const("Passtime"): pszParticleName = "passtime_beam"; break;
            case FNV1A::Hash32Const("Bombonomicon"): pszParticleName = "bombonomicon_spell_trail"; break;
            case FNV1A::Hash32Const("White"): pszParticleName = "medicgun_beam_machinery_stage3"; break;
            case FNV1A::Hash32Const("Orange"): pszParticleName = "medicgun_beam_red_trail_stage3"; break;
            default: pszParticleName = Vars::Visuals::Effects::MedigunBeam.Value.c_str();
            }
        }
        else if (bManageChargeEffect)
        {
            switch (FNV1A::Hash32(Vars::Visuals::Effects::MedigunCharge.Value.c_str()))
            {
            case FNV1A::Hash32Const("Default"): break;
            case FNV1A::Hash32Const("None"): return nullptr;
            case FNV1A::Hash32Const("Electrocuted"): pszParticleName = bBlue ? "electrocuted_blue" : "electrocuted_red"; break;
            case FNV1A::Hash32Const("Halloween"): pszParticleName = "ghost_pumpkin"; break;
            case FNV1A::Hash32Const("Fireball"): pszParticleName = bBlue ? "spell_fireball_small_trail_blue" : "spell_fireball_small_trail_red"; break;
            case FNV1A::Hash32Const("Teleport"): pszParticleName = bBlue ? "spell_teleport_blue" : "spell_teleport_red"; break;
            case FNV1A::Hash32Const("Burning"): pszParticleName = "superrare_burning1"; break;
            case FNV1A::Hash32Const("Scorching"): pszParticleName = "superrare_burning2"; break;
            case FNV1A::Hash32Const("Purple energy"): pszParticleName = "superrare_purpleenergy"; break;
            case FNV1A::Hash32Const("Green energy"): pszParticleName = "superrare_greenenergy"; break;
            case FNV1A::Hash32Const("Nebula"): pszParticleName = "unusual_invasion_nebula"; break;
            case FNV1A::Hash32Const("Purple stars"): pszParticleName = "unusual_star_purple_parent"; break;
            case FNV1A::Hash32Const("Green stars"): pszParticleName = "unusual_star_green_parent"; break;
            case FNV1A::Hash32Const("Sunbeams"): pszParticleName = "superrare_beams1"; break;
            case FNV1A::Hash32Const("Spellbound"): pszParticleName = "unusual_spellbook_circle_purple"; break;
            case FNV1A::Hash32Const("Purple sparks"): pszParticleName = "unusual_robot_orbiting_sparks2"; break;
            case FNV1A::Hash32Const("Yellow sparks"): pszParticleName = "unusual_robot_orbiting_sparks"; break;
            case FNV1A::Hash32Const("Green zap"): pszParticleName = "unusual_zap_green"; break;
            case FNV1A::Hash32Const("Yellow zap"): pszParticleName = "unusual_zap_yellow"; break;
            case FNV1A::Hash32Const("Plasma"): pszParticleName = "superrare_plasma1"; break;
            case FNV1A::Hash32Const("Frostbite"): pszParticleName = "unusual_eotl_frostbite"; break;
            case FNV1A::Hash32Const("Time warp"): pszParticleName = bBlue ? "unusual_robot_time_warp2" : "unusual_robot_time_warp"; break;
            case FNV1A::Hash32Const("Purple souls"): pszParticleName = "unusual_souls_purple_parent"; break;
            case FNV1A::Hash32Const("Green souls"): pszParticleName = "unusual_souls_green_parent"; break;
            case FNV1A::Hash32Const("Bubbles"): pszParticleName = "unusual_bubbles"; break;
            case FNV1A::Hash32Const("Hearts"): pszParticleName = "unusual_hearts_bubbling"; break;
            default: pszParticleName = Vars::Visuals::Effects::MedigunCharge.Value.c_str();
            }
        }
    }

	return CALL_ORIGINAL(rcx, pszParticleName, iAttachType, pszAttachmentName);
}

MAKE_HOOK(CParticleProperty_Create_Point, S::CParticleProperty_Create_Point(), void*,
	void* rcx, const char* pszParticleName, ParticleAttachment_t iAttachType, int iAttachmentPoint, Vector vecOriginOffset)
{
    DEBUG_RETURN(CParticleProperty_Create_Point, rcx, pszParticleName, iAttachType, iAttachmentPoint, vecOriginOffset);
    const auto cosmeticMuzzle=SkinChanger::MuzzleFor(rcx,pszParticleName);
    if(!cosmeticMuzzle.empty())
    {
        auto effect=CALL_ORIGINAL(rcx,cosmeticMuzzle.c_str(),iAttachType,iAttachmentPoint,vecOriginOffset);
        SkinChanger::ObserveParticleCreation("muzzle",pszParticleName,cosmeticMuzzle,effect!=nullptr);
        if(effect)return effect;
    }

    if (pszParticleName)
    {
        switch (FNV1A::Hash32(pszParticleName))
        {
        case FNV1A::Hash32Const("kart_impact_sparks"):
            if (I::Prediction->InPrediction() && !I::Prediction->m_bFirstTimePredicted)
                return nullptr;
        }
    }

    if (FNV1A::Hash32(Vars::Visuals::Effects::ProjectileTrail.Value.c_str()) != FNV1A::Hash32Const("Default") && pszParticleName)
    {
        switch (FNV1A::Hash32(pszParticleName))
        {
        // any trails we want to replace
        case FNV1A::Hash32Const("peejar_trail_blu"):
        case FNV1A::Hash32Const("peejar_trail_red"):
        case FNV1A::Hash32Const("peejar_trail_blu_glow"):
        case FNV1A::Hash32Const("peejar_trail_red_glow"):
        case FNV1A::Hash32Const("stunballtrail_blue"):
        case FNV1A::Hash32Const("stunballtrail_red"):
        case FNV1A::Hash32Const("rockettrail"):
        case FNV1A::Hash32Const("rockettrail_airstrike"):
        case FNV1A::Hash32Const("drg_cow_rockettrail_normal_blue"):
        case FNV1A::Hash32Const("drg_cow_rockettrail_normal"):
        case FNV1A::Hash32Const("drg_cow_rockettrail_charged_blue"):
        case FNV1A::Hash32Const("drg_cow_rockettrail_charged"):
        case FNV1A::Hash32Const("rockettrail_RocketJumper"):
        case FNV1A::Hash32Const("rockettrail_underwater"):
        case FNV1A::Hash32Const("halloween_rockettrail"):
        case FNV1A::Hash32Const("eyeboss_projectile"):
        case FNV1A::Hash32Const("drg_bison_projectile"):
        case FNV1A::Hash32Const("flaregun_trail_blue"):
        case FNV1A::Hash32Const("flaregun_trail_red"):
        case FNV1A::Hash32Const("scorchshot_trail_blue"):
        case FNV1A::Hash32Const("scorchshot_trail_red"):
        case FNV1A::Hash32Const("drg_manmelter_projectile"):
        case FNV1A::Hash32Const("pipebombtrail_blue"):
        case FNV1A::Hash32Const("pipebombtrail_red"):
        case FNV1A::Hash32Const("stickybombtrail_blue"):
        case FNV1A::Hash32Const("stickybombtrail_red"):
        case FNV1A::Hash32Const("healshot_trail_blue"):
        case FNV1A::Hash32Const("healshot_trail_red"):
        case FNV1A::Hash32Const("flaming_arrow"):
        case FNV1A::Hash32Const("spell_fireball_small_trail_blue"):
        case FNV1A::Hash32Const("spell_fireball_small_trail_red"):
        {
            auto pLocal = H::Entities.GetLocal();
            if (!pLocal)
                return CALL_ORIGINAL(rcx, pszParticleName, iAttachType, iAttachmentPoint, vecOriginOffset);

            bool bValid = false;
            for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
            {
                auto pOwner = F::ProjSim.GetEntities(pEntity).second;
                if (bValid = pLocal == pOwner && rcx == pEntity->m_Particles())
                    break;
            }
            if (!bValid)
                return CALL_ORIGINAL(rcx, pszParticleName, iAttachType, iAttachmentPoint, vecOriginOffset);

            bool bBlue = pLocal->m_iTeamNum() == TF_TEAM_BLUE;
            switch (FNV1A::Hash32(Vars::Visuals::Effects::ProjectileTrail.Value.c_str()))
            {
            case FNV1A::Hash32Const("None"): return nullptr;
            case FNV1A::Hash32Const("Rocket"): pszParticleName = "rockettrail"; break;
            case FNV1A::Hash32Const("Critical"): pszParticleName = bBlue ? "critical_rocket_blue" : "critical_rocket_red"; break;
            case FNV1A::Hash32Const("Energy"): pszParticleName = bBlue ? "drg_cow_rockettrail_normal_blue" : "drg_cow_rockettrail_normal"; break;
            case FNV1A::Hash32Const("Charged"): pszParticleName = bBlue ? "drg_cow_rockettrail_charged_blue" : "drg_cow_rockettrail_charged"; break;
            case FNV1A::Hash32Const("Ray"): pszParticleName = "drg_manmelter_projectile"; break;
            case FNV1A::Hash32Const("Fireball"): pszParticleName = bBlue ? "spell_fireball_small_trail_blue" : "spell_fireball_small_trail_red"; break;
            case FNV1A::Hash32Const("Teleport"): pszParticleName = bBlue ? "spell_teleport_blue" : "spell_teleport_red"; break;
            case FNV1A::Hash32Const("Fire"): pszParticleName = "flamethrower"; break;
            case FNV1A::Hash32Const("Flame"): pszParticleName = "flying_flaming_arrow"; break;
            case FNV1A::Hash32Const("Sparks"): pszParticleName = bBlue ? "critical_rocket_bluesparks" : "critical_rocket_redsparks"; break;
            case FNV1A::Hash32Const("Flare"): pszParticleName = bBlue ? "flaregun_trail_blue" : "flaregun_trail_red"; break;
            case FNV1A::Hash32Const("Trail"): pszParticleName = bBlue ? "stickybombtrail_blue" : "stickybombtrail_red"; break;
            case FNV1A::Hash32Const("Health"): pszParticleName = bBlue ? "healshot_trail_blue" : "healshot_trail_red"; break;
            case FNV1A::Hash32Const("Smoke"): pszParticleName = "rockettrail_airstrike_line"; break;
            case FNV1A::Hash32Const("Bubbles"): pszParticleName = bBlue ? "pyrovision_scorchshot_trail_blue" : "pyrovision_scorchshot_trail_red"; break;
            case FNV1A::Hash32Const("Halloween"): pszParticleName = "halloween_rockettrail"; break;
            case FNV1A::Hash32Const("Monoculus"): pszParticleName = "eyeboss_projectile"; break;
            case FNV1A::Hash32Const("Sparkles"): pszParticleName = bBlue ? "burningplayer_rainbow_blue" : "burningplayer_rainbow_red"; break;
            case FNV1A::Hash32Const("Rainbow"): pszParticleName = "flamethrower_rainbow"; break;
            default: pszParticleName = Vars::Visuals::Effects::ProjectileTrail.Value.c_str();
            }
            break;
        }
        /*
        // any additional trails
        case FNV1A::Hash32Const("stunballtrail_blue_crit"):
        case FNV1A::Hash32Const("stunballtrail_red_crit"):
        case FNV1A::Hash32Const("critical_rocket_blue"):
        case FNV1A::Hash32Const("critical_rocket_red"):
        case FNV1A::Hash32Const("critical_rocket_bluesparks"):
        case FNV1A::Hash32Const("critical_rocket_redsparks"):
        case FNV1A::Hash32Const("flaregun_trail_crit_blue"):
        case FNV1A::Hash32Const("flaregun_trail_crit_red"):
        case FNV1A::Hash32Const("critical_pipe_blue"):
        case FNV1A::Hash32Const("critical_pipe_red"):
        case FNV1A::Hash32Const("critical_grenade_blue"):
        case FNV1A::Hash32Const("critical_grenade_red"):
        */
        case FNV1A::Hash32Const("rockettrail_airstrike_line"): return nullptr;
        }
    }

	return CALL_ORIGINAL(rcx, pszParticleName, iAttachType, iAttachmentPoint, vecOriginOffset);
}

MAKE_HOOK(CParticleProperty_AddControlPoint_Pointer, S::CParticleProperty_AddControlPoint_Pointer(), void,
    void* rcx, void* pEffect, int iPoint, CBaseEntity* pEntity, ParticleAttachment_t iAttachType, const char* pszAttachmentName, Vector vecOriginOffset)
{
    DEBUG_RETURN(CParticleProperty_AddControlPoint_Pointer, rcx, pEffect, iPoint, pEntity, iAttachType, pszAttachmentName, vecOriginOffset);

    if (!pEffect)
        return; // crash fix

    CALL_ORIGINAL(rcx, pEffect, iPoint, pEntity, iAttachType, pszAttachmentName, vecOriginOffset);
}
