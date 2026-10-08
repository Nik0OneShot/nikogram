#include "ConditionSensor.h"
#include "../Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include "../Aimbot/AimbotProjectile/AimbotProjectile.h"
#include <unordered_map>

namespace
{
    bool Valid(CTFPlayer* player)
    {return player&&player->IsAlive()&&!player->IsDormant()&&!player->IsAGhost();}
    bool Finite(const Vec3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
    std::array<float,3> XYZ(const Vec3& v){return {v.x,v.y,v.z};}
    bool Exposed(CBaseEntity* source,CTFPlayer* target,const Vec3& from,const Vec3& point)
    {return SDK::VisPosWorld(source,target,from,point,MASK_SHOT);}
    bool Aimed(CTFPlayer* enemy,const Vec3& from,const Vec3& point,float tolerance,float extent)
    {
        const float distance=from.DistTo(point);
        const float extra=57.2957795f*std::atan2(extent,std::max(distance,1.f));
        return Math::CalcFov(H::Entities.GetEyeAngles(enemy->entindex()),Math::CalcAngle(from,point))<=tolerance+extra;
    }
    float ProtectedDamage(CTFPlayer* local,float base,float mult,int kind)
    {
        if(local->IsInvulnerable())return 0;
        ETFCond immune=TF_COND_BULLET_IMMUNE,uber=TF_COND_MEDIGUN_UBER_BULLET_RESIST,resistSmall=TF_COND_MEDIGUN_SMALL_BULLET_RESIST;
        const char* attribute="mult_dmgtaken_from_bullets";
        if(kind==1){immune=TF_COND_BLAST_IMMUNE;uber=TF_COND_MEDIGUN_UBER_BLAST_RESIST;resistSmall=TF_COND_MEDIGUN_SMALL_BLAST_RESIST;attribute="mult_dmgtaken_from_explosions";}
        if(kind==2){immune=TF_COND_FIRE_IMMUNE;uber=TF_COND_MEDIGUN_UBER_FIRE_RESIST;resistSmall=TF_COND_MEDIGUN_SMALL_FIRE_RESIST;attribute="mult_dmgtaken_from_fire";}
        float vaccine=1.f; bool blockCrit=false;
        if(kind<3)
        {
            if(local->InCond(immune))return 0;
            if(local->InCond(uber)){vaccine=.25f;blockCrit=true;}
            else if(local->InCond(resistSmall))vaccine=.9f;
        }
        float general=SDK::AttribHookValue(1.f,"mult_dmgtaken",local);
        const float type=SDK::AttribHookValue(1.f,kind<3?attribute:"mult_dmgtaken_from_melee",local);
        const float crit=SDK::AttribHookValue(1.f,"mult_dmgtaken_from_crit",local);
        if(local->InCond(TF_COND_DEFENSEBUFF)){general*=.65f;blockCrit=true;}
        if(local->InCond(TF_COND_DEFENSEBUFF_NO_CRIT_BLOCK))general*=.65f;
        if(local->InCond(TF_COND_DEFENSEBUFF_HIGH))general*=.25f;
        return ConditionPolicy::Damage(base,mult,general,type,crit,vaccine,blockCrit);
    }
    float Boost(CTFPlayer* enemy,CTFPlayer* local)
    {return enemy->IsCritBoosted()?3.f:enemy->IsMiniCritBoosted()||local->IsMarked()?1.35f:1.f;}
    bool Ready(CTFPlayer* enemy,CTFWeaponBase* weapon)
    {
        if(!weapon||!enemy->CanAttack(true,false))return false;
        // Ammo, clip and next-primary-attack are local weapon data, not reliable
        // remote readiness. Missing reserve ammo must not hide every Sniper.
        // The broadcast last-fire time provides a best-effort cooldown check.
        return weapon->m_flLastFireTime()+weapon->GetFireRate()<=I::GlobalVars->curtime+TICK_INTERVAL;
    }
    float SniperBase(CTFPlayer* enemy,CTFWeaponBase* weapon)
    {
        float charge=50.f;
        // Remote rifle charge is not always sent. The owned sniper dot carries
        // charge start time; never assume every scoped rifle is fully charged.
        for(auto entity:H::Entities.GetGroup(EntityEnum::SniperDots))
        {
            auto dot=entity->As<CSniperDot>();
            if(dot->m_hOwnerEntity().Get()!=enemy)continue;
            const float rate=SDK::AttribHookValue(1.f,"mult_sniper_charge_per_sec",weapon);
            charge=ConditionPolicy::SniperDamage(I::GlobalVars->curtime-dot->m_flChargeStartTime(),50.f*rate);
            break;
        }
        return charge;
    }
    bool EnemyShot(CTFPlayer* enemy,CTFPlayer* local,const ConditionPolicy::Options& options)
    {
        auto entityWeapon=enemy->m_hActiveWeapon().Get();
        auto weapon=entityWeapon?entityWeapon->As<CTFWeaponBase>():nullptr;
        if(!Ready(enemy,weapon))return false;
        const auto type=SDK::GetWeaponType(weapon);const int id=weapon->GetWeaponID();
        const Vec3 eye=enemy->GetShootPos(),head=local->GetShootPos(),body=local->GetCenter();
        if(!Finite(eye)||!Finite(head)||!Finite(body))return false;
        const bool sniper=id==TF_WEAPON_SNIPERRIFLE||id==TF_WEAPON_SNIPERRIFLE_DECAP||id==TF_WEAPON_SNIPERRIFLE_CLASSIC;
        const bool requireAim=ConditionPolicy::RequiresAim(sniper,options);
        if(requireAim&&!Aimed(enemy,eye,body,options.aimTolerance,80.f))return false;
        float base=weapon->GetDamage(),mult=Boost(enemy,local);
        if(sniper)
        {
            const bool classic=id==TF_WEAPON_SNIPERRIFLE_CLASSIC,zoomed=enemy->InCond(TF_COND_ZOOMED);
            if(!classic&&SDK::AttribHookValue(0,"sniper_only_fire_zoomed",weapon)&&!zoomed)return false;
            const float charge=SniperBase(enemy,weapon);
            base=SDK::AttribHookValue(charge,"mult_dmg",weapon);
            if(charge>=150.f)base=SDK::AttribHookValue(base,"sniper_full_charge_damage_bonus",weapon);
            const bool noScope=SDK::AttribHookValue(0,"sniper_crit_no_scope",weapon)!=0;
            const bool needsCharge=classic||SDK::AttribHookValue(0,"sniper_no_headshot_without_full_charge",weapon)!=0;
            const bool canHeadshot=(!needsCharge||charge>=150.f)&&(noScope||(zoomed&&I::GlobalVars->curtime-enemy->m_flFOVTime()>=.2f))
                &&weapon->As<CTFSniperRifle>()->GetRifleType()!=RIFLE_JARATE;
            if(canHeadshot&&(!requireAim||Aimed(enemy,eye,head,options.aimTolerance,5.f))&&Exposed(enemy,local,eye,head)
                &&ConditionPolicy::Lethal(ProtectedDamage(local,base*SDK::AttribHookValue(1.f,"mult_dmg_vs_head",weapon),std::max(mult,3.f),0),float(local->m_iHealth())))return true;
            base=SDK::AttribHookValue(base,"bodyshot_damage_modify",weapon);
        }
        else if(type==EWeaponType::PROJECTILE||type==EWeaponType::UNKNOWN)return false; // Existing projectiles below.
        if(type==EWeaponType::MELEE)
        {
            if(eye.DistTo(body)>weapon->GetSwingRange()*enemy->m_flModelScale()+32.f)return false;
            if(id==TF_WEAPON_KNIFE)
            {
                Vec3 toward=local->GetCenter()-enemy->GetCenter(),victimForward,attackerForward;
                toward.z=0;toward.Normalize();
                // The remote-eye-angle cache explicitly excludes the local
                // player; reading it would pretend the victim always faces east.
                Vec3 localView;I::EngineClient->GetViewAngles(localView);
                Math::AngleVectors(localView,&victimForward);
                Math::AngleVectors(H::Entities.GetEyeAngles(enemy->entindex()),&attackerForward);
                victimForward.z=attackerForward.z=0;victimForward.Normalize();attackerForward.Normalize();
                if(toward.Dot(victimForward)>.0f&&toward.Dot(attackerForward)>.5f&&victimForward.Dot(attackerForward)>-.3f)
                    base=float(local->m_iHealth())*6.f;
            }
        }
        if(type==EWeaponType::HITSCAN&&!sniper)
        {
            if(id==TF_WEAPON_MINIGUN&&!enemy->InCond(TF_COND_AIMING))return false;
            // One attack, not a damage-per-second score. Spread reduces expected
            // pellet contact at distance; do not invent future random crits.
            const float spread=std::max(weapon->GetWeaponSpread(),.001f),distance=eye.DistTo(body);
            const float pellets=Math::RemapVal(distance,20.f/spread,100.f/spread,float(weapon->GetBulletsPerShot()),1.f);
            base*=pellets;
            if(mult<3.f)base*=Math::RemapVal(distance,0.f,1024.f,1.5f,.5f);
        }
        const int kind=type==EWeaponType::MELEE?3:0;
        return ConditionPolicy::Lethal(ProtectedDamage(local,base,mult,kind),float(local->m_iHealth()))
            &&(!requireAim||Aimed(enemy,eye,body,options.aimTolerance,22.f))&&Exposed(enemy,local,eye,body);
    }
    Vec3 Closest(const Vec3& point,const Vec3& low,const Vec3& high)
    {return {std::clamp(point.x,low.x,high.x),std::clamp(point.y,low.y,high.y),std::clamp(point.z,low.z,high.z)};}
    float Explosion(CBaseEntity* source,CTFPlayer* local,Vec3 point,float time,float base,float mult,float radius,int kind)
    {
        const Vec3 origin=local->m_vecOrigin()+local->m_vecVelocity()*time;
        const Vec3 closest=Closest(point,origin+local->m_vecMins(),origin+local->m_vecMaxs());
        const float damage=ConditionPolicy::Splash(base,point.DistTo(closest),radius);
        return damage>0&&Exposed(source,local,point,closest)?ProtectedDamage(local,damage,mult,kind):0.f;
    }
    constexpr int trapSamples=9;
    using TrapDamage=std::array<float,trapSamples>;
    float Projectile(CBaseEntity* entity,CTFPlayer* local,float window,CTFPlayer*& stickyOwner,TrapDamage& trapDamage)
    {
        stickyOwner=nullptr;if(!entity||entity->IsDormant()||entity->m_iTeamNum()==local->m_iTeamNum())return 0;
        const auto [weapon,owner]=F::ProjSim.GetEntities(entity);
        const auto cls=entity->GetClassID();
        if(!owner||(!weapon&&cls!=ETFClassID::CTFProjectile_SentryRocket))return 0;
        const bool pipe=cls==ETFClassID::CTFGrenadePipebombProjectile;
        const bool rocket=cls==ETFClassID::CTFProjectile_Rocket||cls==ETFClassID::CTFProjectile_SentryRocket;
        const bool arrow=cls==ETFClassID::CTFProjectile_Arrow||cls==ETFClassID::CTFProjectile_HealingBolt;
        const bool flare=cls==ETFClassID::CTFProjectile_Flare;
        const bool energy=cls==ETFClassID::CTFProjectile_EnergyBall||cls==ETFClassID::CTFProjectile_EnergyRing;
        if(!pipe&&!rocket&&!arrow&&!flare&&!energy)return 0;
        float base=weapon?weapon->GetDamage():100.f,mult=local->IsMarked()?1.35f:1.f;int kind=arrow||cls==ETFClassID::CTFProjectile_EnergyRing?0:flare?2:1;
        bool sticky=false,touched=false,crit=false;float radius=0,readyTime=0;
        if(pipe)
        {
            auto grenade=entity->As<CTFGrenadePipebombProjectile>();
            crit=grenade->m_bCritical();touched=grenade->m_bTouched();sticky=grenade->m_iType()==TF_GL_MODE_REMOTE_DETONATE;
            if(grenade->m_iType()!=TF_GL_MODE_REGULAR&&!sticky)return 0;
            if(sticky)
            {
                if(!Valid(owner)||!owner->CanAttack(true,false)||weapon->As<CTFPipebombLauncher>()->GetDetonateType()!=0)return 0;
                const float arm=SDK::AttribHookValue(.8f,"sticky_arm_time",weapon);
                if(grenade->m_flCreationTime()<=0||I::GlobalVars->curtime+window<grenade->m_flCreationTime()+arm)return 0;
                readyTime=std::max(0.f,grenade->m_flCreationTime()+arm-I::GlobalVars->curtime);
            }
        }
        else if(cls==ETFClassID::CTFProjectile_Rocket)crit=entity->As<CTFProjectile_Rocket>()->m_bCritical();
        else if(arrow)
        {
            crit=entity->As<CTFProjectile_Arrow>()->m_bCritical();
            base=cls==ETFClassID::CTFProjectile_HealingBolt?Math::RemapVal(owner->GetCenter().DistTo(entity->GetCenter()),200.f,1600.f,38.f,75.f)
                :Math::RemapVal(entity->As<CTFProjectile_Arrow>()->m_vInitialVelocity().Length(),1800.f,2600.f,50.f,120.f);
            base=SDK::AttribHookValue(base,"mult_dmg",weapon);
        }
        else if(flare)
        {
            crit=entity->As<CTFProjectile_Flare>()->m_bCritical();
            if(local->InCond(TF_COND_BURNING))mult=weapon->As<CTFFlareGun>()->GetFlareGunType()==FLAREGUN_NORMAL?3.f:1.35f;
        }
        else if(cls==ETFClassID::CTFProjectile_EnergyBall&&entity->As<CTFProjectile_EnergyBall>()->m_bChargedShot())base=SDK::AttribHookValue(90.f,"mult_dmg",weapon);
        if(crit)mult=3.f;
        if(rocket||pipe)radius=F::AimbotProjectile.GetSplashRadius(entity,weapon,owner);
        Vec3 origin=entity->m_vecOrigin(),velocity=F::ProjSim.GetVelocity(entity);
        const float gravity=F::ProjSim.GetGravity(entity,weapon);
        if(!Finite(origin)||!Finite(velocity)||!std::isfinite(gravity)||!std::isfinite(base)||base<=0)return 0;
        // Skip projectiles unable even to reach the local hull/blast radius.
        const float reach=radius+local->m_vecMaxs().Length()+velocity.Length()*window+local->m_vecVelocity().Length()*window+std::abs(gravity)*.5f*window*window+4.f;
        if(origin.DistTo(local->m_vecOrigin())>reach)return 0;
        float danger=0,time=0;
        CTraceFilterWorldAndPropsOnly filter;
        if(sticky)
        {
            stickyOwner=owner;
            // All bombs of one owner are added at matching future times; do
            // not sum unrelated peak exposures from different instants.
            for(int i=0;i<trapSamples;++i)
            {
                const float future=window*i/(trapSamples-1),dt=future-time;
                if(!touched&&dt>0)
                {
                    Vec3 next=origin+velocity*dt;next.z-=gravity*.5f*dt*dt;
                    CGameTrace trace={};SDK::Trace(origin,next,MASK_SHOT,&filter,&trace);
                    if(trace.startsolid||trace.allsolid)break;
                    if(trace.fraction<1.f){origin=trace.endpos+trace.plane.normal;touched=true;}
                    else{origin=next;velocity.z-=gravity*dt;}
                }
                time=future;
                if(future>=readyTime)trapDamage[i]=Explosion(entity,local,origin,future,base,mult,radius,kind);
                danger=std::max(danger,trapDamage[i]);
            }
            return danger;
        }
        while(time<window)
        {
            const float dt=std::min(std::max(TICK_INTERVAL,.001f),window-time);
            Vec3 next=origin+velocity*dt;next.z-=gravity*.5f*dt*dt;
            CGameTrace trace={};SDK::Trace(origin,next,MASK_SHOT,&filter,&trace);
            if(trace.startsolid||trace.allsolid)break;
            const Vec3 startRelative=origin-(local->m_vecOrigin()+local->m_vecVelocity()*time);
            const Vec3 endRelative=next-(local->m_vecOrigin()+local->m_vecVelocity()*(time+dt));
            const float hit=ConditionPolicy::Contact(XYZ(startRelative),XYZ(endRelative),XYZ(local->m_vecMins()-2.f),XYZ(local->m_vecMaxs()+2.f));
            if(!sticky&&(!pipe||!touched)&&hit<=trace.fraction)
            {
                const Vec3 point=origin+(next-origin)*hit;
                if(Exposed(entity,local,origin,point))
                {
                    float hitMult=mult;
                    if(cls==ETFClassID::CTFProjectile_Arrow&&entity->As<CTFProjectile_Arrow>()->CanHeadshot())
                    {
                        const Vec3 relativeHead=local->GetShootPos()-local->m_vecOrigin();
                        if(point.z-(local->m_vecOrigin().z+local->m_vecVelocity().z*(time+dt*hit))>=relativeHead.z-8.f)hitMult=std::max(hitMult,3.f);
                    }
                    danger=std::max(danger,ProtectedDamage(local,base,hitMult,kind));
                }
                break;
            }
            if(trace.fraction<1.f)
            {
                if(rocket&&!(trace.surface.flags&SURF_SKY))danger=std::max(danger,Explosion(entity,local,trace.endpos+trace.plane.normal*1.f,time+dt*trace.fraction,base,mult,radius,kind));
                // Grenade bounce/fuse timing is uncertain; do not claim an
                // explosion merely because a pill touches a wall.
                break;
            }
            time+=dt;origin=next;velocity.z-=gravity*dt;
        }
        return danger;
    }
}

bool CConditionSensor::Behind(const ConditionPolicy::Options& options)
{
    const Vec3 origin=local->GetCenter();Vec3 view;I::EngineClient->GetViewAngles(view);
    for(auto entity:H::Entities.GetGroup(EntityEnum::PlayerEnemy))
    {
        auto enemy=entity->As<CTFPlayer>();if(!Valid(enemy)||enemy->m_iTeamNum()==local->m_iTeamNum())continue;
        const Vec3 delta=enemy->GetCenter()-origin;
        if(ConditionPolicy::Behind(delta.x,delta.y,delta.z,view.y,enemy->m_iClass(),options)
            &&(!options.behindVisible||Exposed(local,enemy,local->GetShootPos(),enemy->GetCenter())))return true;
    }
    return false;
}
bool CConditionSensor::Threat(const ConditionPolicy::Options& options)
{
    if(local->m_iHealth()<=0)return false;
    // The AA activation option treats any visible enemy Sniper as a threat,
    // independently of their weapon, scope, aim, readiness or estimated damage.
    // Keep this before the lethal-damage guards, including local invulnerability.
    if(options.sniperAnyAim)
    {
        const Vec3 eye=local->GetShootPos();
        if(Finite(eye))for(auto entity:H::Entities.GetGroup(EntityEnum::PlayerEnemy))
        {
            auto enemy=entity->As<CTFPlayer>();
            if(!Valid(enemy)||enemy->m_iTeamNum()==local->m_iTeamNum()||enemy->m_iClass()!=TF_CLASS_SNIPER)continue;
            const Vec3 head=enemy->GetShootPos(),body=enemy->GetCenter();
            if((Finite(head)&&Exposed(local,enemy,eye,head))
                ||(Finite(body)&&Exposed(local,enemy,eye,body)))return true;
        }
    }
    if(local->IsInvulnerable())return false;
    for(auto entity:H::Entities.GetGroup(EntityEnum::PlayerEnemy))
    {
        auto enemy=entity->As<CTFPlayer>();if(!Valid(enemy)||enemy->m_iTeamNum()==local->m_iTeamNum())continue;
        if(EnemyShot(enemy,local,options))return true;
    }
    std::unordered_map<CTFPlayer*,TrapDamage> traps;
    for(auto projectile:H::Entities.GetGroup(EntityEnum::WorldProjectile))
    {
        CTFPlayer* owner=nullptr;TrapDamage samples{};
        const float damage=Projectile(projectile,local,options.projectileWindow,owner,samples);
        if(ConditionPolicy::Lethal(damage,float(local->m_iHealth())))return true;
        if(owner)
            for(int i=0;i<trapSamples;++i)
                if(ConditionPolicy::Lethal(traps[owner][i]+=samples[i],float(local->m_iHealth())))return true;
    }
    return false;
}
bool CConditionSensor::Evaluate(int type,const ConditionPolicy::Options& raw)
{
    if(!Valid(local)||!I::EngineClient->IsInGame())return false;
    const auto options=ConditionPolicy::Normalize(raw);
    for(const auto& result:results)if(result.type==type&&result.options==options)return result.value;
    const bool value=type==BindEnum::Behind?Behind(options):type==BindEnum::Threat?Threat(options):false;
    results.push_back({type,options,value});return value;
}
