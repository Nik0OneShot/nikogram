#include "../SDK/SDK.h"
#include "../Features/SkinChanger/SkinChanger.h"
#include "../Features/SkinChanger/RenderPolicy.h"

// Verified native x64 functions. CStudioHdr stays opaque: the game resolves
// included models, virtual sequences and activities. No seqdesc casts.
MAKE_SIGNATURE(ActivityList_IndexForName, "client.dll", "48 83 EC 28 48 8B D1 48 8D 0D ? ? ? ? E8 ? ? ? ? 85 C0 78 19 8B C8 48 8B 05 ? ? ? ? 48 8D 04 C8 48 85 C0 74 07 8B 00 48 83 C4 28 C3 B8 FF FF FF FF", 0x0);
MAKE_SIGNATURE(SkinNative_GetModelPtr, "client.dll", "48 89 7C 24 20 55 48 8B EC 48 83 EC 70 80 B9 ? ? ? ? 00 48 8B F9 74 10 33 C0", 0x0);
MAKE_SIGNATURE(SkinNative_SequenceCount, "client.dll", "48 8B 41 08 48 85 C0 75 0A 48 8B 01 8B 80 BC 00 00 00 C3 8B 40 20 C3", 0x0);
MAKE_SIGNATURE(SkinNative_SequenceActivity, "client.dll", "48 89 5C 24 10 48 89 6C 24 18 57 48 83 EC 20 49 8B D8 8B EA 48 8B F9 48 85 C9 74 50 E8 ? ? ? ? 84 C0 74 47 8B D5", 0x0);
MAKE_SIGNATURE(SkinNative_SelectSequence, "client.dll", "48 89 5C 24 10 48 89 6C 24 18 56 48 83 EC 20 41 8B F0 8B EA 48 8B D9 48 85 C9 74 57 E8 ? ? ? ? 84 C0 74 4E", 0x0);
MAKE_SIGNATURE(SkinNative_DoAnimationEvents, "client.dll", "48 85 D2 0F 84 ? ? ? ? 53 41 56 48 83 EC 68 83 B9 ? ? ? ? FF 48 8B DA 4C 8B F1", 0x0);

namespace {
    thread_local SkinRender::HandSequenceLatch s_handSequence;
    struct SapperPoseClock {uint32_t weapon=0;int sequence=-1,parity=-1,resetParity=-1;double start=0;float duration=0;bool draw=false;};
    thread_local SapperPoseClock s_sapperClock;
}

bool SkinChanger::PreparePlayerBones(CTFPlayer* player,std::array<matrix3x4,MAXSTUDIOBONES>& output)
{
    auto profile=PlayerAnimationsFor(player);if(!profile)return false;
    using LayerStamp=std::tuple<int,int,float,float,float,float>;
    using PoseStamp=std::tuple<int,int,int,int,int,float,float,std::array<float,24>,std::vector<LayerStamp>>;
    struct CachedPose {int frame=-1,item=0,reskin=0;uint32_t weapon=0;PoseStamp stamp;bool ready=false;std::array<matrix3x4,MAXSTUDIOBONES> bones;};
    auto anim=player->m_PlayerAnimState();const int slots=anim?anim->m_aGestureSlots.Count():0;
    if(slots<0||slots>16)return false;
    std::vector<LayerStamp> layerStamp;layerStamp.reserve(slots);
    for(int n=0;n<slots;++n)if(auto layer=anim->m_aGestureSlots[n].m_pAnimLayer)
        layerStamp.emplace_back(n,int(layer->m_nSequence),float(layer->m_flCycle),float(layer->m_flWeight),float(layer->m_flPlaybackRate),float(layer->m_flPrevCycle));
    const PoseStamp stamp{player->m_nSequence(),player->m_nNewSequenceParity(),player->m_nResetEventsParity(),player->m_iClass(),player->m_fFlags(),
        player->m_flCycle(),player->m_flPlaybackRate(),player->m_flPoseParameter(),std::move(layerStamp)};
    static thread_local std::map<uint32_t,CachedPose> poses;
    const auto handle=uint32_t(player->GetRefEHandle().ToInt());
    if(!poses.contains(handle)&&poses.size()>=128)poses.clear();
    auto& memo=poses[handle];const auto weapon=uint32_t(player->m_hActiveWeapon().ToInt());
    if(memo.ready&&memo.frame==I::GlobalVars->framecount&&memo.item==profile->item&&memo.reskin==profile->reskin&&memo.weapon==weapon&&memo.stamp==stamp)
    {output=memo.bones;return true;}
    memo.frame=I::GlobalVars->framecount;memo.item=profile->item;memo.reskin=profile->reskin;memo.weapon=weapon;memo.stamp=stamp;memo.ready=false;
    SkinRender::ModelCacheScope<IMDLCache> modelCache(static_cast<IMDLCache*>(I::MDLCache));
    auto header=S::SkinNative_GetModelPtr.Call<void*>(player);if(!header)return false;
    const int count=S::SkinNative_SequenceCount.Call<int>(header);if(count<1||count>8192)return false;
    auto native=U::Hooks.m_mHooks.find("CBaseAnimating_SetupBones");if(native==U::Hooks.m_mHooks.end()||!native->second)return false;
    auto& cache=player->m_CachedBoneData();if(cache.Count()<1||cache.Count()>MAXSTUDIOBONES)return false;
    std::array<matrix3x4,MAXSTUDIOBONES> originalBones;const int originalCount=cache.Count();
    if(originalCount)std::memcpy(originalBones.data(),cache.Base(),originalCount*sizeof(matrix3x4));
    const int originalSequence=player->m_nSequence();
    std::vector<std::pair<CAnimationLayer*,CAnimationLayer>> layers;
    for(int n=0;n<slots;++n)if(auto layer=anim->m_aGestureSlots[n].m_pAnimLayer)layers.emplace_back(layer,*layer);
    struct Restore
    {
        CTFPlayer* player;int sequence,count;std::array<matrix3x4,MAXSTUDIOBONES>& bones;
        std::vector<std::pair<CAnimationLayer*,CAnimationLayer>>& layers;
        ~Restore()
        {
            player->m_nSequence()=sequence;for(auto& [layer,saved]:layers)*layer=saved;
            auto& cache=player->m_CachedBoneData();SkinRender::RestoreGameplayBones(std::span<const matrix3x4>(bones.data(),count),cache);
            // Subsequent simulation/aim queries rebuild against the original
            // sequence. Cosmetic bones exist ONLY in the output draw buffer.
            player->InvalidateBoneCache();
        }
    } restore{player,originalSequence,originalCount,originalBones,layers};
    using ProfileKey=std::tuple<int,int,int,int>;
    static thread_local std::map<ProfileKey,std::map<int,int>> activityMaps;
    const ProfileKey profileKey{profile->item,profile->reskin,player->m_iClass(),player->m_iTeamNum()};
    if(!activityMaps.contains(profileKey))
    {
        if(activityMaps.size()>=128)activityMaps.clear();
        auto& map=activityMaps[profileKey];for(const auto& [from,to]:profile->replacements)
        {const int a=S::ActivityList_IndexForName.Call<int>(from.c_str()),b=S::ActivityList_IndexForName.Call<int>(to.c_str());if(a>0&&b>0)map[a]=b;}
    }
    const auto& activities=activityMaps.at(profileKey);
    static thread_local std::map<std::pair<uint32_t,int>,SkinRender::HandSequenceLatch> latches;
    if(latches.size()>=512)latches.clear();
    auto replace=[&](int sequence,int layerIndex,float cycle)
    {
        if(sequence<0||sequence>=count)return sequence;
        int weight=0;const int activity=S::SkinNative_SequenceActivity.Call<int>(header,sequence,&weight);
        auto desired=activities.find(activity);if(desired==activities.end()||desired->second==activity)return sequence;
        SkinRender::HandSequenceKey key{uintptr_t(player),uintptr_t(header),uint32_t(player->m_hActiveWeapon().ToInt()),
            profile->item,profile->reskin,player->m_iTeamNum(),player->m_iClass(),sequence,desired->second,count,player->m_nNewSequenceParity(),player->m_nResetEventsParity()};
        auto& latch=latches[{uint32_t(player->GetRefEHandle().ToInt()),layerIndex}];
        const int target=latch.Resolve(key,cycle,[&]{return S::SkinNative_SelectSequence.Call<int>(header,desired->second,sequence);});
        return target>=0&&target<count?target:sequence;
    };
    player->m_nSequence()=replace(originalSequence,-1,player->m_flCycle());bool changed=player->m_nSequence()!=originalSequence;
    int layerIndex=0;for(auto& [layer,saved]:layers)
    {const int seq=replace(saved.m_nSequence,layerIndex++,saved.m_flCycle);layer->m_nSequence=seq;changed|=seq!=int(saved.m_nSequence);}
    if(!changed)return false;
    player->InvalidateBoneCache();
    SkinRender::CosmeticPoseScope cosmeticPose(player);
    const bool built=native->second->Call<bool>(static_cast<IClientRenderable*>(player),output.data(),MAXSTUDIOBONES,BONE_USED_BY_ANYTHING,I::GlobalVars->curtime);
    if(built){memo.bones=output;memo.ready=true;ObserveCosmeticEffect("third_person_animation","frame_pose_stamp_native_cosmetic_blending_original_gameplay_bones_restored");}return built;
}

std::optional<int> SkinChanger::RenderHandSequence(CBaseAnimating* viewmodel)
{
    auto local=H::Entities.GetLocal();
    if(!local||!viewmodel||local->m_hViewModel().Get()!=viewmodel)return std::nullopt;
    auto active=local->m_hActiveWeapon().Get();
    auto profile=HandAnimationsFor(active?active->As<CTFWeaponBase>():nullptr);if(!profile){s_handSequence.Clear();return std::nullopt;}
    auto header=S::SkinNative_GetModelPtr.Call<void*>(viewmodel);
    if(!header){ObserveHandActivity(profile->item,profile->reskin,-1,-1,"render_hand_header_unavailable");return std::nullopt;}
    const int count=S::SkinNative_SequenceCount.Call<int>(header),sequence=viewmodel->m_nSequence();
    if(count<1||count>8192||sequence<0||sequence>=count)
    {ObserveHandActivity(profile->item,profile->reskin,-1,-1,"render_sequence_bounds_rejected",sequence,-1,count);return std::nullopt;}
    int weight=0;const int activity=S::SkinNative_SequenceActivity.Call<int>(header,sequence,&weight);
    auto lookup=[](const std::string& name){return S::ActivityList_IndexForName.Call<int>(name.c_str());};
    auto desired=SkinRender::RenderHandActivity(profile->replacements,activity,lookup);
    if(!desired||*desired==activity)
    {s_handSequence.Clear();ObserveHandActivity(profile->item,profile->reskin,activity,activity,desired?"render_activity_unchanged":"render_activity_unknown",sequence,sequence,count);return std::nullopt;}
    SkinRender::HandSequenceKey key{uintptr_t(viewmodel),uintptr_t(header),uint32_t(local->m_hActiveWeapon().ToInt()),
        profile->item,profile->reskin,local->m_iTeamNum(),local->m_iClass(),sequence,*desired,count,
        viewmodel->m_nNewSequenceParity(),viewmodel->m_nResetEventsParity()};
    int replacement=s_handSequence.Resolve(key,viewmodel->m_flCycle(),[&]{return S::SkinNative_SelectSequence.Call<int>(header,*desired,sequence);});
    if(replacement<0&&*desired==lookup("ACT_MELEE_ALLCLASS_VM_SWINGHARD"))
        replacement=S::SkinNative_SelectSequence.Call<int>(header,lookup("ACT_MELEE_ALLCLASS_VM_HITCENTER"),sequence);
    if(profile->reskin==1102&&replacement>=0&&replacement<count)
    {
        const double now=I::GlobalVars->realtime;const auto weapon=uint32_t(local->m_hActiveWeapon().ToInt());
        const bool draw=*desired==lookup("ACT_BREADSAPPER_VM_DRAW");
        // A stock draw completes sooner than the bread draw. Keep the cosmetic
        // draw until its authored end; otherwise the arm snaps back mid-animation.
        if(!draw&&s_sapperClock.weapon==weapon&&s_sapperClock.draw&&now>=s_sapperClock.start
            &&now-s_sapperClock.start<s_sapperClock.duration)return s_sapperClock.sequence;
        if(s_sapperClock.weapon!=weapon||s_sapperClock.sequence!=replacement||now<s_sapperClock.start
            ||(draw&&s_sapperClock.parity!=viewmodel->m_nNewSequenceParity()))
        {
            s_sapperClock={weapon,replacement,viewmodel->m_nNewSequenceParity(),viewmodel->m_nResetEventsParity(),now,viewmodel->SequenceDuration(replacement),draw};
            ObserveCosmeticEffect("sapper_animation_timing",std::format("target_sequence={} authored_duration={} render_only=true",replacement,s_sapperClock.duration));
        }
    }
    ObserveHandActivity(profile->item,profile->reskin,activity,*desired,
        replacement>=0&&replacement<count?"render_sequence_resolved":"render_sequence_unavailable",sequence,replacement,count);
    return replacement>=0&&replacement<count?std::optional<int>(replacement):std::nullopt;
}

std::optional<float> SkinChanger::RenderHandCycle(CBaseAnimating* vm,int sequence)
{
    auto local=H::Entities.GetLocal();auto held=local?local->m_hActiveWeapon().Get():nullptr;
    auto profile=HandAnimationsFor(held?held->As<CTFWeaponBase>():nullptr);
    if(!vm||!profile||profile->reskin!=1102||sequence<0)return {};
    if(s_sapperClock.weapon!=uint32_t(local->m_hActiveWeapon().ToInt())||s_sapperClock.sequence!=sequence)return {};
    return SkinRender::AuthoredCycle(I::GlobalVars->realtime-s_sapperClock.start,s_sapperClock.duration,vm->m_flPlaybackRate(),!s_sapperClock.draw);
}

// Native animation events run during simulation, outside the draw scope. Let
// the engine dispatch the selected item's own events (including draw sounds),
// retaining its cycle/parity deduplication. Never replay events from rendering.
MAKE_HOOK(SkinNative_DoAnimationEvents, S::SkinNative_DoAnimationEvents(), void,
    CBaseAnimating* entity,void* header)
{
    DEBUG_RETURN(SkinNative_DoAnimationEvents, entity, header);
    auto local=H::Entities.GetLocal();
    if(!header||!local||!local->IsAlive()||local->m_hViewModel().Get()!=entity||G::Unload||SDK::CleanScreenshot())
        return CALL_ORIGINAL(entity,header);
    SkinRender::ModelCacheScope<IMDLCache> cache(static_cast<IMDLCache*>(I::MDLCache));
    if(header!=S::SkinNative_GetModelPtr.Call<void*>(entity))return CALL_ORIGINAL(entity,header);
    auto sequence=SkinChanger::RenderHandSequence(entity);
    auto cycle=sequence?SkinChanger::RenderHandCycle(entity,*sequence):std::nullopt;
    struct EventState
    {
        CBaseAnimating* entity;int parity,reset;bool loops,changed;
        EventState(CBaseAnimating* value,bool custom):entity(value),parity(value->m_nNewSequenceParity()),reset(value->m_nResetEventsParity()),loops(value->m_bSequenceLoops()),changed(custom)
        {if(changed){value->m_nNewSequenceParity()=s_sapperClock.parity;value->m_nResetEventsParity()=s_sapperClock.resetParity;value->m_bSequenceLoops()=!s_sapperClock.draw;}}
        ~EventState(){if(changed){entity->m_nNewSequenceParity()=parity;entity->m_nResetEventsParity()=reset;entity->m_bSequenceLoops()=loops;}}
    } eventState(entity,cycle.has_value());
    SkinRender::TimedSequenceScope scope(entity,sequence.value_or(-1),cycle,[](CBaseAnimating* value){value->InvalidateBoneCache();});
    if(sequence)SkinChanger::ObserveCosmeticEffect("hand_animation_events","native_replacement_sequence");
    CALL_ORIGINAL(entity,header);
}
