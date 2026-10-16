#pragma once
#include "../../SDK/SDK.h"
#include "../Aimbot/SelfDamageDiagnostics.h"
#include "NamedPlayerTracePolicy.h"
#include <atomic>
#include <cstddef>
#include <mutex>

// Diagnostic-only state. All engine reads occur at existing command or actual
// model-submission sites. No getter which rebuilds transforms/bounds/bones is used.
namespace NamedPlayerTraceDiagnostics
{
    // These diagnostic copies rely on the installed x64 collision layout.
    // Runtime netvar checks below separately validate the engine's embedding.
    static_assert(sizeof(Vec3)==12 && sizeof(CCollisionProperty)==0x98);
    static_assert(offsetof(CCollisionProperty,m_pOuter)==0x08);
    static_assert(offsetof(CCollisionProperty,m_vecMins)==0x28 && offsetof(CCollisionProperty,m_vecMaxs)==0x34);
    static_assert(offsetof(CCollisionProperty,m_flRadius)==0x40);
    static_assert(offsetof(CCollisionProperty,m_usSolidFlags)==0x44 && sizeof(CCollisionProperty::m_usSolidFlags)==2);
    static_assert(offsetof(CCollisionProperty,m_Partition)==0x46 && sizeof(CCollisionProperty::m_Partition)==2);
    static_assert(offsetof(CCollisionProperty,m_nSurroundType)==0x48 && sizeof(CCollisionProperty::m_nSurroundType)==1);
    static_assert(offsetof(CCollisionProperty,m_nSolidType)==0x49 && sizeof(CCollisionProperty::m_nSolidType)==1);
    static_assert(offsetof(CCollisionProperty,m_vecSpecifiedSurroundingMins)==0x64 && offsetof(CCollisionProperty,m_vecSpecifiedSurroundingMaxs)==0x70);
    static_assert(offsetof(CCollisionProperty,m_vecSurroundingMins)==0x7C && offsetof(CCollisionProperty,m_vecSurroundingMaxs)==0x88);
    namespace P=NamedPlayerTracePolicy;
    inline constexpr const char* Names[]={"Bot09","Bot03","Bot07"};
    inline constexpr const char* RouteNames[]={"current","chams","glow","historical_chams","historical_glow"};
    struct Target
    {
        int index=-1,matches=0,userID=-1,spawn=-1,modelIndex=-1;
        unsigned handle=0; const model_t* model=nullptr;
        std::array<std::array<P::Pose,P::PosesPerRoute>,P::Routes> poses{};
        std::array<std::uint64_t,P::Routes> window{};
        std::array<unsigned,P::Routes> captured{},omitted{};
    };
    inline std::mutex mutex;
    inline std::array<Target,3> targets{};
    inline std::array<std::atomic<int>,3> watchedIndex{{-1,-1,-1}};
    inline std::atomic<bool> enabled=false;
    inline std::atomic<unsigned> lockDrops=0;
    inline unsigned owner=0;inline int server=-1,localSpawn=-1;
    inline std::uint64_t lastRoster=0;
    inline std::atomic<unsigned> mainThread=0;
    inline int lastCommand=0;
    inline thread_local int drawRoute=0;
    inline thread_local bool drawOccluded=false;
    struct RenderScope
    {
        int previous;bool oldOccluded;
        RenderScope(int route,bool occluded=false):previous(drawRoute),oldOccluded(drawOccluded)
        {drawRoute=route;drawOccluded=occluded;}
        ~RenderScope(){drawRoute=previous;drawOccluded=oldOccluded;}
    };
    inline P::Point Point(const Vec3& v){return {v.x,v.y,v.z};}
    inline std::string VectorText(const P::Point& p){return std::format("{},{},{}",p[0],p[1],p[2]);}
    inline std::string VectorText(const Vec3& p){return std::format("{},{},{}",p.x,p.y,p.z);}
    inline bool Named(const char* value,const char* expected)
    {return value&&strnlen(value,48)<48&&!strcmp(value,expected);}
    inline void Tick(CTFPlayer* local,CUserCmd* command) noexcept
    {
        const bool active=SelfDamageDiagnostics::TriggerEnabled()&&local&&command&&I::ClientState&&I::GlobalVars&&!G::Unload;
        enabled.store(active,std::memory_order_relaxed);if(!active)return;
        const unsigned thread=GetCurrentThreadId();unsigned expected=0;mainThread.compare_exchange_strong(expected,thread);if(mainThread.load()!=thread)return;lastCommand=command->command_number;
        try
        {
            const auto now=GetTickCount64();const auto h=unsigned(local->GetRefEHandle().ToInt());
            const int s=I::ClientState->m_nServerCount,spawn=local->m_iSpawnCounter();
            const bool same=owner==h&&server==s&&localSpawn==spawn;
            if(same&&lastRoster&&now>=lastRoster&&now-lastRoster<500)return;
            std::array<Target,3> found{};
            const int maxClients=std::clamp(I::GlobalVars->maxClients,0,P::MaxPlayers);
            for(int index=1;index<=maxClients;++index)
            {
                player_info_t info{};if(!I::EngineClient->GetPlayerInfo(index,&info))continue;
                for(int n=0;n<3;++n)if(Named(info.name,Names[n]))
                {
                    auto& target=found[n];++target.matches;if(target.matches!=1)continue;
                    target.index=index;target.userID=info.userID;
                    auto entity=I::ClientEntityList->GetClientEntity(index);if(!entity)continue;
                    auto cls=entity->GetClientClass();if(!cls||!Named(cls->m_pNetworkName,"CTFPlayer"))continue;
                    auto player=entity->As<CTFPlayer>();target.handle=player->GetRefEHandle().ToInt();
                    target.spawn=player->m_iSpawnCounter();target.modelIndex=player->m_nModelIndex();target.model=player->GetModel();
                }
            }
            std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock()){++lockDrops;return;}
            for(int n=0;n<3;++n)
            {
                auto& old=targets[n];auto& next=found[n];
                if(same&&next.matches==1&&old.matches==1&&next.index==old.index&&next.handle==old.handle
                    &&next.spawn==old.spawn&&next.modelIndex==old.modelIndex&&next.model==old.model)continue;
                old=next;
                watchedIndex[n].store(next.matches==1&&next.handle&&next.model?next.index:-1,std::memory_order_relaxed);
            }
            owner=h;server=s;localSpawn=spawn;lastRoster=now;lock.unlock();
            SelfDamageDiagnostics::WriteTrigger("trigger_named_watch",std::format(
                "schema=1 cmd={} server={} owner={} roster_limit={} roster_truncated={} Bot09_index={} Bot09_matches={} Bot03_index={} Bot03_matches={} Bot07_index={} Bot07_matches={} thread={} render_lock_drops={}",
                command->command_number,s,h,maxClients,I::GlobalVars->maxClients>P::MaxPlayers,
                found[0].index,found[0].matches,found[1].index,found[1].matches,found[2].index,found[2].matches,thread,lockDrops.load()));
        }
        catch(...){}
    }
    // Called immediately before a real original DrawModelExecute submission.
    // No engine names, traces, logging or bone/model cache requests here.
    inline void ObserveDraw(const DrawModelState_t& state,const ModelRenderInfo_t& info,const matrix3x4* bones) noexcept
    {
        if(!enabled.load(std::memory_order_relaxed))return;
        int watch=-1;for(int n=0;n<3;++n)if(watchedIndex[n].load(std::memory_order_relaxed)==info.entity_index){watch=n;break;}
        if(watch<0||!info.pRenderable||drawRoute<0||drawRoute>=P::Routes)return;
        std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock()){++lockDrops;return;}
        auto& t=targets[watch];if(t.matches!=1||t.index!=info.entity_index||info.pModel!=t.model)return;
        auto unknown=info.pRenderable->GetIClientUnknown();if(!unknown||unsigned(unknown->GetRefEHandle().ToInt())!=t.handle)return;
        const auto now=GetTickCount64(),window=now/250;
        const int route=drawRoute;if(t.window[route]!=window){t.window[route]=window;t.captured[route]=0;t.omitted[route]=0;}
        if(t.captured[route]>=P::PosesPerRoute){++t.omitted[route];return;}
        auto& pose=t.poses[route][t.captured[route]++];pose={};pose.observed=true;pose.captured=now;
        pose.handle=t.handle;pose.thread=GetCurrentThreadId();pose.modelIndex=t.modelIndex;pose.hitboxSet=info.hitboxset;
        pose.origin=Point(info.origin);pose.angles=Point(info.angles);pose.occluded=drawOccluded;pose.drawFlags=state.m_drawFlags;pose.infoFlags=info.flags;
        const auto hdr=state.m_pStudioHdr;
        if(!bones||!hdr||hdr->numbones<1||hdr->numbones>MAXSTUDIOBONES||hdr->numhitboxsets<1
            ||hdr->numhitboxsets>64||info.hitboxset<0||info.hitboxset>=hdr->numhitboxsets
            ||!P::Span(hdr->length,hdr->hitboxsetindex,hdr->numhitboxsets,sizeof(mstudiohitboxset_t)))return;
        auto set=hdr->pHitboxSet(info.hitboxset);if(!set||set->numhitboxes<1||set->numhitboxes>256)return;
        const auto setOffset=reinterpret_cast<const char*>(set)-reinterpret_cast<const char*>(hdr);
        if(set->hitboxindex<0||setOffset<0||setOffset>hdr->length
            ||!P::Span(hdr->length-int(setOffset),set->hitboxindex,set->numhitboxes,sizeof(mstudiobbox_t)))return;
        pose.total=set->numhitboxes;pose.complete=pose.total<=P::MaxBoxes;
        for(int n=0;n<std::min(set->numhitboxes,P::MaxBoxes);++n)
        {
            const auto box=set->pHitbox(n);if(!box||box->bone<0||box->bone>=hdr->numbones){pose.complete=false;continue;}
            auto& out=pose.boxes[pose.count++];out.index=n;out.bone=box->bone;out.group=box->group;
            out.mins=Point(box->bbmin);out.maxs=Point(box->bbmax);
            for(int r=0;r<3;++r)for(int c=0;c<4;++c)out.matrix[r][c]=bones[box->bone][r][c];
        }
    }
    struct RawFields
    {
        int flags=0;Vec3 absolute{},angles{},network{},networkAngles{};
        bool flagsKnown=false,absoluteKnown=false,anglesKnown=false,networkKnown=false,networkAnglesKnown=false;
    };
    inline RawFields ReadRaw(CTFPlayer* player)
    {
        RawFields raw;const auto root=player->GetPredDescMap();
        struct Offsets { datamap_t* map=nullptr;std::array<int,5> field{};bool initialized=false; };
        static Offsets cache;
        if(!cache.initialized||cache.map!=root)
        {
            cache={};cache.map=root;cache.initialized=true;auto map=root;int visited=0;
            constexpr const char* names[]={"m_iEFlags","m_vecAbsOrigin","m_angAbsRotation","m_vecNetworkOrigin","m_angNetworkAngles"};
            for(int depth=0;map&&depth<32&&visited<4096;++depth,map=map->baseMap)
            {
                if(map->dataNumFields<0||map->dataNumFields>2048||(map->dataNumFields&&!map->dataDesc))break;
                for(int n=0;n<map->dataNumFields&&visited<4096;++n,++visited)
                {
                    const auto& f=map->dataDesc[n];const int off=f.fieldOffset[TD_OFFSET_NORMAL];
                    if(!f.fieldName||f.fieldSize!=1||off<1||off>65524)continue;
                    for(int k=0;k<5;++k)if(!cache.field[k]&&!strcmp(f.fieldName,names[k])
                        &&(k==0?f.fieldType==FIELD_INTEGER:f.fieldType==FIELD_VECTOR||f.fieldType==FIELD_POSITION_VECTOR))cache.field[k]=off;
                }
            }
        }
        void* values[]={&raw.flags,&raw.absolute,&raw.angles,&raw.network,&raw.networkAngles};
        bool* known[]={&raw.flagsKnown,&raw.absoluteKnown,&raw.anglesKnown,&raw.networkKnown,&raw.networkAnglesKnown};
        for(int k=0;k<5;++k)if(cache.field[k]){memcpy(values[k],reinterpret_cast<const char*>(player)+cache.field[k],k?12:4);*known[k]=true;}
        return raw;
    }
    inline bool CollisionLayout()
    {
        static const bool valid=U::NetVars.GetNetVar("CBaseEntity","m_Collision")==0x250
            &&U::NetVars.GetNetVar("CBaseEntity","m_vecMins")==0x278&&U::NetVars.GetNetVar("CBaseEntity","m_vecMaxs")==0x284
            &&U::NetVars.GetNetVar("CBaseEntity","m_usSolidFlags")==0x294&&U::NetVars.GetNetVar("CBaseEntity","m_nSolidType")==0x299;
        return valid;
    }
    inline P::Box WorldBox(const Vec3& mins,const Vec3& maxs,const Vec3& origin)
    {
        P::Box box;box.mins=Point(mins);box.maxs=Point(maxs);
        for(int n=0;n<3;++n){box.matrix[n][n]=1;box.matrix[n][3]=origin[n];}return box;
    }
    inline std::string Geometry(const char* label,const P::Result& hit,double world,bool worldHit)
    {return std::format(" {}_status={} {}_enter={} {}_leave={} {}_box={} {}_relation={}",label,P::Name(hit.status),label,hit.enter,label,hit.leave,label,hit.box,label,P::Relation(hit,world,worldHit));}
    inline void Write(CTFPlayer* local,CUserCmd* command,const Vec3& eye,const Vec3& end,float range,const CGameTrace& trace) noexcept
    {
        if(!enabled.load(std::memory_order_relaxed)||!local||!command||GetCurrentThreadId()!=mainThread)return;
        try
        {
            const auto now=GetTickCount64();const auto direction=(end-eye)/range;
            const auto origin=Point(eye),ray=Point(direction);const double world=range*trace.fraction;
            const bool worldHit=trace.fraction<1&&!trace.startsolid&&!trace.allsolid&&trace.m_pEnt&&trace.m_pEnt==I::ClientEntityList->GetClientEntity(0);
            for(int named=0;named<3;++named)
            {
                Target target;
                {std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock()){++lockDrops;continue;}target=targets[named];}
                const bool identity=target.matches==1&&target.handle&&server==I::ClientState->m_nServerCount;
                auto base=identity?I::ClientEntityList->GetClientEntityFromHandle(CBaseHandle(target.handle)):nullptr;
                auto cls=base?base->GetClientClass():nullptr;
                const bool playerClass=cls&&Named(cls->m_pNetworkName,"CTFPlayer");
                std::string text=std::format("schema=1 cmd={} name={} matches={} index={} handle={} entry={} serial={} user_id={} resolved={} network_player={} class_id={} enum_player={} world_distance={} trace_fraction={} eye={} direction={} thread={} render_lock_drops={}",
                    command->command_number,Names[named],target.matches,target.index,target.handle,CBaseHandle(target.handle).GetEntryIndex(),CBaseHandle(target.handle).GetSerialNumber(),target.userID,
                    base!=nullptr,playerClass,cls?cls->m_ClassID:-1,int(ETFClassID::CTFPlayer),world,trace.fraction,VectorText(eye),VectorText(direction),mainThread.load(),lockDrops.load());
                if(!playerClass){SelfDamageDiagnostics::WriteTrigger("trigger_named",text);continue;}
                auto player=base->As<CTFPlayer>();const auto raw=ReadRaw(player);
                const bool same=target.spawn==player->m_iSpawnCounter()&&target.modelIndex==player->m_nModelIndex()&&target.model==player->GetModel();
                text+=std::format(" spawn={} identity_current={} team={} role={} health={} life={} dormant={} flags={} effects={} simtime={} old_simtime={} animtime={} model_index={} model_scale={} netvar_origin={} netvar_angles={} abs_known={} abs_cached={} abs_angles_known={} abs_angles_cached={} network_known={} network_origin={} network_angles_known={} network_angles={} eflags_known={} eflags={} dirty_abs={} dirty_surround={} cached_bones_abi_known=0",
                    player->m_iSpawnCounter(),same,player->m_iTeamNum(),player->m_iClass(),player->m_iHealth(),int(player->m_lifeState()),player->IsDormant(),player->m_fFlags(),player->m_fEffects(),
                    player->m_flSimulationTime(),player->m_flOldSimulationTime(),player->m_flAnimTime(),player->m_nModelIndex(),player->m_flModelScale(),VectorText(player->m_vecOrigin()),VectorText(player->m_angRotation()),
                    raw.absoluteKnown,VectorText(raw.absolute),raw.anglesKnown,VectorText(raw.angles),raw.networkKnown,VectorText(raw.network),raw.networkAnglesKnown,VectorText(raw.networkAngles),
                    raw.flagsKnown,raw.flags,!!(raw.flags&0x800),!!(raw.flags&0x4000));
                auto collision=CollisionLayout()?player->m_Collision():nullptr;
                const bool collisionKnown=collision&&collision->m_pOuter==player;text+=std::format(" collision_layout_known={}",collisionKnown);
                if(collisionKnown)
                {
                    text+=std::format(" solid_type={} solid_flags={} solid_fields_allow={} collision_group={} partition_handle={} surround_type={} radius={} obb_mins={} obb_maxs={} surround_relative_mins={} surround_relative_maxs={} specified_mins={} specified_maxs={}",
                        int(collision->m_nSolidType),collision->m_usSolidFlags,collision->m_nSolidType!=SOLID_NONE&&!(collision->m_usSolidFlags&FSOLID_NOT_SOLID),player->m_CollisionGroup(),
                        collision->m_Partition,int(collision->m_nSurroundType),collision->m_flRadius,VectorText(collision->m_vecMins),VectorText(collision->m_vecMaxs),
                        VectorText(collision->m_vecSurroundingMins),VectorText(collision->m_vecSurroundingMaxs),VectorText(collision->m_vecSpecifiedSurroundingMins),VectorText(collision->m_vecSpecifiedSurroundingMaxs));
                    const bool clean=raw.absoluteKnown&&raw.flagsKnown&&!(raw.flags&0x800);
                    // TF player SOLID_BBOX is axis-aligned. Other solid types are not guessed.
                    auto box=clean&&collision->m_nSolidType==SOLID_BBOX?P::Intersect(origin,ray,range,WorldBox(collision->m_vecMins,collision->m_vecMaxs,raw.absolute)):P::Result{};
                    auto surround=clean&&!(raw.flags&0x4000)?P::Intersect(origin,ray,range,WorldBox(collision->m_vecSurroundingMins,collision->m_vecSurroundingMaxs,raw.absolute)):P::Result{};
                    text+=Geometry("collision_obb",box,world,worldHit)+Geometry("cached_surround",surround,world,worldHit);
                }
                for(int route=0;route<P::Routes;++route)text+=std::format(" {}_observed={} {}_captured={} {}_omitted={}",RouteNames[route],target.poses[route][0].observed,RouteNames[route],target.captured[route],RouteNames[route],target.omitted[route]);
                SelfDamageDiagnostics::WriteTrigger("trigger_named",text);
                for(int route=0;route<P::Routes;++route)for(int slot=0;slot<P::PosesPerRoute;++slot)
                {
                    const auto& pose=target.poses[route][slot];if(!pose.observed)continue;
                    const bool fresh=same&&pose.handle==target.handle&&pose.modelIndex==target.modelIndex&&P::Fresh(now,pose.captured);
                    const auto rendered=fresh?P::Evaluate(pose,origin,ray,range):P::Result{};
                    SelfDamageDiagnostics::WriteTrigger("trigger_named_pose",std::format(
                        "schema=1 cmd={} name={} handle={} route={} slot={} observed_ms={} age_ms={} identity_fresh={} complete={} boxes={} total_boxes={} hitbox_set={} draw_origin={} draw_angles={} occluded_pass={} draw_thread={} captured_in_window={} omitted_draw_calls={} cached_bones_abi_known=0 selected_hitbox_mask={} draw_flags={} info_flags={}",
                        command->command_number,Names[named],target.handle,RouteNames[route],slot,pose.captured,now>=pose.captured?now-pose.captured:0,fresh,pose.complete,pose.count,pose.total,pose.hitboxSet,
                        VectorText(pose.origin),VectorText(pose.angles),pose.occluded,pose.thread,target.captured[route],target.omitted[route],Vars::Triggerbot::Hitboxes.Value,pose.drawFlags,pose.infoFlags)
                        +Geometry("submitted_hitboxes",rendered,world,worldHit));
                }
            }
        }
        catch(...){}
    }
}
