#include "../../Nikogram/src/Features/SkinChanger/Model.h"
#include "../../Nikogram/src/Features/SkinChanger/PresetUiPolicy.h"
#include "../../Nikogram/src/Features/SkinChanger/Protocol.h"
#include "../../Nikogram/src/Features/SkinChanger/RenderPolicy.h"
#include "../../Nikogram/src/Utils/Hooks/StartupPolicy.h"
#include "../../Nikogram/src/Utils/Hooks/ResourceReferencePolicy.h"
#include "../../Nikogram/src/Features/SkinChanger/JigglePhysics.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <source_location>
#include "../../Nikogram/src/Utils/Hooks/Lifetime.h"
#include "../../Nikogram/src/Hooks/Direct3DDevice9.h"
#include "../../Nikogram/src/Utils/ExceptionHandler/CrashLog.h"

int main(int argc,char** argv)
{
    int checks=0;auto check=[&](bool ok,const std::source_location where=std::source_location::current())
        {++checks;if(!ok){std::cerr<<"FAIL "<<checks<<" line "<<where.line()<<'\n';std::exit(1);}};
    {
        for(auto name:{"default","my skins","gold collection","a-b_c"})check(SkinPresetUi::NameAllowed(name));
        for(auto name:{"current","CURRENT","../skins","","con","my skins.json","name ","a/b"})check(!SkinPresetUi::NameAllowed(name));
        check(SkinPresetUi::Default("DEFAULT"));check(!SkinPresetUi::Default("my skins"));
        std::vector<std::string> names={"zebra","my skins","default","Alpha"};
        std::sort(names.begin(),names.end(),SkinPresetUi::Less);
        check(names==std::vector<std::string>({"default","Alpha","my skins","zebra"}));
        for(const auto& a:names)for(const auto& b:names)
        {check(!SkinPresetUi::Less(a,a));if(SkinPresetUi::Less(a,b))check(!SkinPresetUi::Less(b,a));}
        check(SkinPresetUi::Columns(619,1)==1);check(SkinPresetUi::Columns(620,1)==2);
        check(SkinPresetUi::Columns(1000,2)==1);check(SkinPresetUi::Columns(1240,2)==2);
        check(SkinPresetUi::Columns(1000,0)==1);check(SkinPresetUi::Columns(std::numeric_limits<float>::infinity(),1)==1);
    }
    {
        StartupPolicy::Gate gate;
        check(!gate.Ready());check(!gate.Begin(0));check(!gate.Begin(2));
        check(gate.Begin(1));check(!gate.Ready());check(!gate.Begin(1));
        check(!gate.CancelQueued());check(gate.Complete(true));check(gate.Ready());check(!gate.Complete(false));
        StartupPolicy::Gate failed;check(failed.Begin(1));check(failed.Complete(false));check(!failed.Ready());check(!failed.Begin(1));
        StartupPolicy::Gate canceled;check(canceled.CancelQueued());check(!canceled.Begin(1));check(!canceled.Ready());
        ResourceReferencePolicy::References<int*> refs;int a=0,b=0;
        refs.Acquire(nullptr);check(!refs.Take(nullptr));check(!refs.Take(&a));
        refs.Acquire(&a);refs.Acquire(&a);refs.Acquire(&b);
        check(refs.Count(&a)==2&&refs.Count(&b)==1);
        check(refs.Take(&a)&&refs.Count(&a)==1);check(refs.Take(&a)&&refs.Count(&a)==0);
        check(!refs.Take(&a));check(refs.Take(&b));check(!refs.Take(&b));
    }
    {
        char directory[MAX_PATH]{},path[MAX_PATH]{};
        check(GetTempPathA(MAX_PATH,directory)>0);check(GetTempFileNameA(directory,"ncr",0,path)!=0);
        CrashLog::File=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(CrashLog::File!=INVALID_HANDLE_VALUE);
        CrashLog::Stage("regression_heap_record");
        EXCEPTION_RECORD exception{};CONTEXT context{};unsigned long long stack[32]{};
        stack[0]=reinterpret_cast<unsigned long long>(&main);
        exception.ExceptionCode=STATUS_HEAP_CORRUPTION;exception.NumberParameters=1;exception.ExceptionInformation[0]=0x1234;
        context.Rsp=reinterpret_cast<unsigned long long>(stack);
        EXCEPTION_POINTERS pointers{&exception,&context};
        check(CrashLog::Basic(&pointers,nullptr,"test-only"));CrashLog::Close();
        std::ifstream input(path);std::string contents((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());input.close();
        check(contents.find("Code: 0xC0000374")!=std::string::npos);
        check(contents.find("Lifecycle stage: regression_heap_record")!=std::string::npos);
        check(contents.find("Exception parameter[0]: 0x1234")!=std::string::npos);
        check(contents.find("Raw stack[0]")!=std::string::npos);
        check(DeleteFileA(path)!=0);
    }
    {
        using Map=std::map<std::string,std::string>;
        Map stock,black={{"ACT_RELOAD_START","ACT_PRIMARY_RELOAD_START_2"},{"ACT_VM_RELOAD","ACT_PRIMARY_VM_RELOAD_2"},{"ACT_RELOAD_FINISH","ACT_PRIMARY_RELOAD_FINISH_2"}};
        const auto forward=SkinRender::HandActivityMap("PRIMARY","PRIMARY",stock,black);
        check(forward.at("ACT_RELOAD_START")=="ACT_PRIMARY_RELOAD_START_2");
        check(forward.at("ACT_VM_RELOAD")=="ACT_PRIMARY_VM_RELOAD_2");
        const auto reverse=SkinRender::HandActivityMap("PRIMARY","PRIMARY",black,stock);
        check(reverse.at("ACT_PRIMARY_RELOAD_START_2")=="ACT_PRIMARY_RELOAD_START");
        check(reverse.at("ACT_PRIMARY_VM_RELOAD_2")=="ACT_PRIMARY_VM_RELOAD");
        check(reverse.at("ACT_PRIMARY_RELOAD_FINISH_2")=="ACT_PRIMARY_RELOAD_FINISH");
        check(SkinRender::HandActivityMap("PRIMARY","PRIMARY",stock,stock).empty());
        check(SkinRender::NativeHandActivity("ACT_VM_IDLE_QRL","PRIMARY")=="ACT_VM_IDLE_QRL");
        check(SkinRender::NativeHandActivity("ACT_VM_RELOAD_START_QRL","PRIMARY")=="ACT_VM_RELOAD_START_QRL");
        auto lookup=[](const std::string& name){return name=="ACT_PRIMARY_RELOAD_START"?1:name=="ACT_PRIMARY_RELOAD_START_2"?2:0;};
        check(SkinRender::RenderHandActivity(forward,1,lookup)==2);
        check(SkinRender::RenderHandActivity(reverse,2,lookup)==1);
    }
    {
        SkinProtocol::Message equipment;equipment.type=SkinProtocol::EquipmentStyle;equipment.flags=3;equipment.cls=8;equipment.weapon=735;
        equipment.server=11;equipment.nonce=12;equipment.echo=13;equipment.selection.enabled=true;equipment.selection.reskin=1102;
        auto bytes=SkinProtocol::Encode(equipment);auto decoded=SkinProtocol::Decode(bytes);
        check(bytes.size()==53&&decoded&&decoded->type==SkinProtocol::EquipmentStyle&&decoded->selection.reskin==1102);
        equipment.selection.unusual=701;bytes=SkinProtocol::Encode(equipment);decoded=SkinProtocol::Decode(bytes);
        check(bytes.size()==55&&decoded&&decoded->selection.unusual==701);
        auto corrupt=bytes;corrupt.pop_back();check(!SkinProtocol::Decode(corrupt));
        corrupt=bytes;corrupt.push_back(0);check(!SkinProtocol::Decode(corrupt));
        corrupt=bytes;corrupt[8]=3;check(!SkinProtocol::Decode(corrupt));
        SkinProtocol::Peer peer;peer.ready=true;peer.local=13;peer.remote=12;peer.flags=3;peer.lastSeen=1;
        check(peer.CanReceive(equipment,11,2));peer.flags=2;check(!peer.CanReceive(equipment,11,2));peer.flags=3;
        check(!peer.CanReceive(equipment,99,2));check(!peer.CanReceive(equipment,11,32));
        check(peer.EquipmentDue(735,bytes,2));peer.SentEquipment(735,bytes,2);check(!peer.EquipmentDue(735,bytes,2.1));
        check(!peer.EquipmentDue(735,bytes,3));check(peer.EquipmentDue(735,bytes,5));
        check(peer.EquipmentDue(30,bytes,2.1));check(peer.lastPayload.empty());
        peer.Expire(32);check(peer.equipment.empty());
    }
    {
        static std::array<uintptr_t,5> received{};
        Direct3DContract::Present original=+[](IDirect3DDevice9* device,const RECT* source,const RECT* destination,HWND window,const RGNDATA* dirty)->HRESULT
        {received={uintptr_t(device),uintptr_t(source),uintptr_t(destination),uintptr_t(window),uintptr_t(dirty)};return D3DERR_DEVICELOST;};
        RECT source{},destination{};RGNDATA dirty{};
        auto device=reinterpret_cast<IDirect3DDevice9*>(uintptr_t(0x1234));
        auto window=reinterpret_cast<HWND>(uintptr_t(0x5678));
        check(Direct3DContract::ForwardPresent(original,device,&source,&destination,window,&dirty)==D3DERR_DEVICELOST);
        check(received==std::array<uintptr_t,5>{uintptr_t(device),uintptr_t(&source),uintptr_t(&destination),uintptr_t(window),uintptr_t(&dirty)});
        check(Direct3DContract::ForwardPresent(original,device,nullptr,nullptr,nullptr,nullptr)==D3DERR_DEVICELOST);
        check(received==std::array<uintptr_t,5>{uintptr_t(device),0,0,0,0});
    }
    {
        check(HookLifetime::active.load()==0);
        {HookLifetime::Scope frame;check(HookLifetime::active.load()==1);
            {HookLifetime::Scope draw;check(HookLifetime::active.load()==2);}
            check(HookLifetime::active.load()==1);}
        check(HookLifetime::active.load()==0);
        try{HookLifetime::Scope hook;throw 1;}catch(int){}
        check(HookLifetime::active.load()==0);
    }
    {
        check(SkinRender::HudCopyEligible(true,true,264,264));
        check(SkinRender::HudImmediateThink(true,false,true,1,2));
        check(SkinRender::HudImmediateThink(true,true,true,2,1));
        check(!SkinRender::HudImmediateThink(true,true,true,1,1));
        check(!SkinRender::HudImmediateThink(false,false,true,1,2));
        check(SkinRender::HudImmediateThink(false,true,true,1,2));
        check(SkinRender::HudImmediateThink(true,false,false,1,1));
        // Model the native 0.5s poll gate: a swap must update the held slot
        // BEFORE preview refresh, not rebuild the previous slot while gated.
        float now=10.f,nextThink=10.5f;uint32_t displayed=1,active=2;
        if(SkinRender::HudImmediateThink(true,true,true,displayed,active))nextThink=now;
        if(nextThink<=now){displayed=active;nextThink=now+.5f;}
        check(displayed==active);check(nextThink==10.5f);
        active=1;now=10.01f;
        if(SkinRender::HudImmediateThink(true,false,true,displayed,active))nextThink=now;
        if(nextThink<=now)displayed=active;
        check(displayed==active);
        check(SkinRender::HudCopyEligible(true,true,0,0));
        check(!SkinRender::HudCopyEligible(false,true,264,264));
        check(!SkinRender::HudCopyEligible(true,false,264,264));
        check(!SkinRender::HudCopyEligible(true,true,264,225));
        check(!SkinRender::HudCopyEligible(true,true,-1,-1));
        SkinModel::Item appearance;appearance.defaultRed=0;appearance.defaultBlue=1;appearance.goldRed=2;appearance.goldBlue=3;
        check(SkinRender::HudPreviewSkin(appearance,2,false)==0);check(SkinRender::HudPreviewSkin(appearance,3,false)==1);
        check(SkinRender::HudPreviewSkin(appearance,2,true)==2);check(SkinRender::HudPreviewSkin(appearance,3,true)==3);
        check(SkinRender::HudPreviewSkin(appearance,0,true)==-1);
        appearance.defaultRed=2;appearance.defaultBlue=3;
        check(SkinRender::HudPreviewSkin(appearance,2,false)==2);check(SkinRender::HudPreviewSkin(appearance,3,false)==3);
    }
    {
        // Think may equip directly; a nested full refresh must preserve the
        // outer panel identity, and neither context may leak into other UI.
        int panel=0,other=0;void* context=nullptr;
        auto directEquip=[&](void* self){return SkinRender::HudCopyEligible(context==self,true,0,0);};
        check(!directEquip(&panel));
        {SkinRender::DrawStateScope think(context,static_cast<void*>(&panel));
            check(directEquip(&panel));check(!directEquip(&other));
            {SkinRender::DrawStateScope refresh(context,static_cast<void*>(&panel));check(directEquip(&panel));}
            check(directEquip(&panel));}
        check(context==nullptr);check(!directEquip(&panel));
    }
    {
        struct Queue{int* memory;int count,capacity;};int nativeData=1,cosmeticData=2;
        Queue queue{&nativeData,3,8};int parity=7;
        {SkinRender::DrawStateScope scope(queue,Queue{&cosmeticData,1,64});SkinRender::DrawStateScope parityScope(parity,9);
            check(queue.memory==&cosmeticData&&queue.count==1&&parity==9);queue.count=4;parity=10;}
        check(queue.memory==&nativeData&&queue.count==3&&queue.capacity==8&&parity==7);
        try{SkinRender::DrawStateScope scope(queue,Queue{&cosmeticData,0,64});throw 1;}catch(int){}
        check(queue.memory==&nativeData&&queue.count==3);
        check(SkinRender::BlendHistoryFresh(1.1,1,true));check(!SkinRender::BlendHistoryFresh(1.1,1,false));
        check(!SkinRender::BlendHistoryFresh(.9,1,true));check(!SkinRender::BlendHistoryFresh(2,1,true));
        check(!SkinRender::BlendHistoryFresh(std::numeric_limits<double>::quiet_NaN(),1,true));
    }
    {
        int a=0,b=0;check(!SkinRender::CosmeticPoseActive(&a));
        {SkinRender::CosmeticPoseScope scope(&a);check(SkinRender::CosmeticPoseActive(&a));check(!SkinRender::CosmeticPoseActive(&b));
            {SkinRender::CosmeticPoseScope nested(&b);check(SkinRender::CosmeticPoseActive(&b));check(!SkinRender::CosmeticPoseActive(&a));}
            check(SkinRender::CosmeticPoseActive(&a));}
        check(!SkinRender::CosmeticPoseActive(&a));check(!SkinRender::CosmeticPoseActive(nullptr));
    }
    {
        std::map<std::string,std::string> animations;SkinRender::SpyAllClassHands(animations);
        for(auto suffix:{"UP","DOWN","IDLE"})check(animations.at(std::string("ACT_BACKSTAB_VM_")+suffix)=="ACT_MELEE_ALLCLASS_VM_IDLE");
        check(animations.at("ACT_VM_HITCENTER")=="ACT_MELEE_ALLCLASS_VM_HITCENTER");
        check(animations.at("ACT_MELEE_VM_STUN")=="ACT_MELEE_ALLCLASS_VM_IDLE");
    }
    {
        check(std::abs(SkinRender::AuthoredCycle(.5,2,1,false)-.25f)<.001f);
        check(SkinRender::AuthoredCycle(5,2,1,false)==1);
        check(std::abs(SkinRender::AuthoredCycle(5,2,1,true)-.5f)<.001f);
        check(SkinRender::AuthoredCycle(1,0,1,true)==0);
        struct Animation{int sequence=4;float cycle=.9f;int& m_nSequence(){return sequence;}float& m_flCycle(){return cycle;}} animation;
        int invalidations=0;
        {SkinRender::TimedSequenceScope scope(&animation,8,std::optional<float>(.2f),[&](auto*){++invalidations;});check(animation.sequence==8&&animation.cycle==.2f);}
        check(animation.sequence==4&&animation.cycle==.9f&&invalidations==2);
        std::vector<std::string_view> source={"bip_upperArm_L","bip_lowerArm_L"},target={"bip_pelvis","bip_spine_0","bip_upperArm_L","bip_lowerArm_L"};
        std::vector<int> parents={-1,0,1,2},procedures(4,0),mapping;
        check(SkinRender::AnchoredBoneMap(source,target,parents,procedures,mapping));check(mapping[0]==-1&&mapping[2]==0&&mapping[3]==1);
        source={"unrelated"};check(!SkinRender::AnchoredBoneMap(source,target,parents,procedures,mapping));
        using Matrix=SkinRender::FrozenPose::Matrix;Matrix old{},next{},child{},out{};
        for(int n=0;n<3;++n){old[n][n]=next[n][n]=child[n][n]=1;}
        old[0][3]=10;next[0][3]=30;child[0][3]=12;child[1][3]=3;
        check(SkinRender::RebaseAttachment(old,next,child,out));check(out[0][3]==32&&out[1][3]==3);
    }
    {
        using Matrix=SkinRender::FrozenPose::Matrix;
        struct Cache {std::vector<Matrix> values;int Count()const{return int(values.size());}Matrix* Base(){return values.data();}void RemoveAll(){values.clear();}};
        std::array<Matrix,2> gameplay{};gameplay[0][0][3]=42;gameplay[1][1][3]=84;Cache cache{{Matrix{},Matrix{}}};
        cache.values[0][0][3]=999;check(SkinRender::RestoreGameplayBones(std::span<const Matrix>(gameplay),cache));check(cache.values[0][0][3]==42&&cache.values[1][1][3]==84);
        cache.values.push_back(Matrix{});check(!SkinRender::RestoreGameplayBones(std::span<const Matrix>(gameplay),cache));check(cache.Count()==0);
        for(bool menu:{false,true})for(bool enabled:{false,true})
            check(SkinRender::BypassMenuCosmetics(menu,enabled)==(menu&&!enabled));
        // Menu transitions must not disable an enabled skin changer.
        for(int frame=0;frame<100;++frame)
            check(!SkinRender::BypassMenuCosmetics(frame%2==0,true));
        SkinRender::DeathSoundOnce once;
        check(once.Begin(1,4));once.Complete(false); // GUID capture can miss a sound that actually played.
        check(!once.Begin(1.1,4));check(!once.Begin(1.25,4));
        once.Complete(true);check(!once.Begin(2,4));once.Complete(false);check(once.started);
        once={};check(once.Begin(1,4));check(!once.Begin(2,4));check(!once.Begin(3,4));check(!once.Begin(4,4));
        once={};check(!once.Begin(5,4));check(!once.Begin(std::numeric_limits<double>::quiet_NaN(),4));
        check(!once.Begin(1,std::numeric_limits<double>::quiet_NaN()));
        once.Complete(true);check(!once.Begin(1,4)); // Native playback prevents late-event duplicate.
        check(!SkinRender::CorpseExpired(true,100,100.0001)); // Reproduce v102 refreshed-seen timestamp ordering.
        check(!SkinRender::CorpseExpired(true,220,100));check(SkinRender::CorpseExpired(true,220.001,100));
        check(SkinRender::CorpseExpired(false,100,100));
        // Repeated tick/draw updates must preserve the same sound gate and pose.
        for(int frame=0;frame<1000;++frame)
        {
            const double now=100+frame*.0025;
            check(!SkinRender::CorpseExpired(true,now,now+.000001));
            check(!once.Begin(now,200));
        }
        for(bool shared:{false,true})for(bool receive:{false,true})for(bool fresh:{false,true})
        {
            check(SkinRender::CorpseAllowed(1001,1001,shared,receive,fresh)==(!shared||(receive&&fresh)));
            check(!SkinRender::CorpseAllowed(1001,2001,shared,receive,fresh)); // Reused player index, different entity serial.
            check(!SkinRender::CorpseAllowed(0,0,shared,receive,fresh));
        }
        using Matrix=SkinRender::FrozenPose::Matrix;Matrix identity={{{1,0,0,10},{0,1,0,20},{0,0,1,30}}};
        std::array<Matrix,2> source={identity,identity},output{};SkinRender::FrozenPose pose;
        check(pose.Copy(std::span<const Matrix>(source),std::span<Matrix>(output))&&source==output);
        source[0][0][3]=1234;source[1][2][3]=5678;
        check(pose.Copy(std::span<const Matrix>(source),std::span<Matrix>(output))&&output[0][0][3]==10&&output[1][2][3]==30);
        check(!pose.Copy(std::span<const Matrix>(source.data(),1),std::span<Matrix>(output.data(),1)));
        pose.bones.clear();source[0][0][3]=std::numeric_limits<float>::quiet_NaN();check(!pose.Copy(std::span<const Matrix>(source),std::span<Matrix>(output))&&pose.bones.empty());
        check(!pose.Copy(std::span<const Matrix>{},std::span<Matrix>{}));
        std::vector<std::string_view> names={"weapon_bone","jiggle01","jiggle02"};std::vector<int> parents={-1,0,1},procedures={0,5,5},mapping;std::string_view missing;
        check(SkinRender::RestBoneMap(names,names,parents,procedures,mapping,missing,true)&&mapping==std::vector<int>({0,-1,-1}));
        check(SkinRender::RestBoneMap(names,names,parents,procedures,mapping,missing,false)&&mapping==std::vector<int>({0,1,2}));
    }
    {
        SkinModel::CosmeticEffects effects;effects.active=true;
        for(size_t category=0;category<effects.sounds.size();++category)
        {effects.sourceSounds[category]="Native.Sound"+std::to_string(category);effects.sounds[category]="Reskin.Sound"+std::to_string(category);}
        for(size_t category=0;category<effects.sounds.size();++category)
        {check(effects.Sound(int(category))==effects.sounds[category]);check(effects.ReplaceSound(effects.sourceSounds[category])==effects.sounds[category]);}
        check(effects.ReplaceSound("Player.Footstep").empty());
        effects.active=false;check(effects.ReplaceSound("Native.Sound9").empty());
    }
    {
        SkinModel::Selection alignment;check(alignment.Valid()&&!alignment.Adjusted());
        for(int axis=0;axis<3;++axis)
        {
            alignment={};alignment.position[axis]=50;alignment.rotation[axis]=-180;check(alignment.Valid()&&alignment.Adjusted());
            alignment.position[axis]=50.1f;check(!alignment.Valid());alignment.position[axis]=0;
            alignment.rotation[axis]=181;check(!alignment.Valid());alignment.rotation[axis]=std::numeric_limits<float>::quiet_NaN();check(!alignment.Valid());
        }
        using Matrix=std::array<std::array<float,4>,3>;
        Matrix identity={{{1,0,0,0},{0,1,0,0},{0,0,1,0}}};
        std::array<Matrix,2> bones={identity,identity};bones[0][0][3]=10;bones[1][0][3]=20;
        const auto original=bones;
        check(SkinRender::AlignBones(std::span<Matrix>(bones),identity,identity,{10,0,0},{0,0,0})&&bones==original);
        check(SkinRender::AlignBones(std::span<Matrix>(bones),identity,identity,{10,0,0},{1,2,3}));
        check(bones[0][0][3]==11&&bones[1][0][3]==21&&bones[1][1][3]==2&&bones[1][2][3]==3);
        bones=original;Matrix quarter={{{0,-1,0,0},{1,0,0,0},{0,0,1,0}}};
        check(SkinRender::AlignBones(std::span<Matrix>(bones),identity,quarter,{10,0,0},{0,0,0}));
        check(bones[0][0][3]==10&&bones[1][0][3]==10&&bones[1][1][3]==10);
        check(SkinModel::AuthoredDeathEffect("turn to gold","1")==SkinModel::DeathGold);
        check(SkinModel::AuthoredDeathEffect("turn to gold","0")==0);
        check(SkinModel::AuthoredDeathEffect("ragdolls become ash","1")==SkinModel::DeathAsh);
        check(SkinModel::AuthoredDeathEffect("ragdolls plasma effect","1")==SkinModel::DeathPlasma);
        check(SkinModel::AuthoredDeathEffect("freeze backstab victim","1")==SkinModel::DeathIce);
        check(SkinModel::AuthoredDeathEffect("damage bonus","1")==0);
    }
    {
        check(SkinRender::KillWeaponDefinition(18,6,22,1)==18); // Rocket killed after drawing a shovel.
        check(SkinRender::KillWeaponDefinition(6,18,1,22)==6); // A shovel kill cannot borrow the rocket kit.
        check(SkinRender::KillWeaponDefinition(-1,18,22,22)==18);
        check(SkinRender::KillWeaponDefinition(65535,18,22,22)==18);
        check(!SkinRender::KillWeaponDefinition(-1,18,1,22));
        check(!SkinRender::KillWeaponDefinition(65535,18,1,22));
        for(int bad:{-2,65536,1000000})check(!SkinRender::KillWeaponDefinition(bad,18,22,22));
        check(!SkinRender::KillWeaponDefinition(-1,18,-1,22));
        check(SkinRender::KillWeaponDefinition(0,-1,-1,-1)==0); // Stock bat definition zero is valid.
        SkinRender::LifeStreaks streaks;
        for(int kill=0;kill<4;++kill)check(streaks.DeathOnce(kill,kill,1,2,false,true));
        check(streaks.Get(2)==4);
        streaks.Death(3,1,false,true);check(streaks.Get(1)==1);
        check(streaks.DeathOnce(5,5,1,2,false,false));
        check(streaks.Get(2)==4&&streaks.Get(1)==0); // Non-kit kill: victim resets, attacker does not increase.
        check(!streaks.DeathOnce(5,5,1,2,false,true)&&streaks.Get(2)==4);
        check(streaks.DeathOnce(6,6,1,2,false,true)&&streaks.Get(2)==5);
        SkinRender::MilestoneSoundGate sounds;check(sounds.Due(5,false));
        check(streaks.DeathOnce(7,7,1,2,false,false)&&streaks.Get(2)==5);
        check(!sounds.Due(streaks.Get(2),false)); // A non-kit kill cannot repeat the five-kill sound.
        SkinRender::SharedStreak shared;check(shared.Sync(4,1,4));
        check(shared.Value(4)==4&&shared.Value(5)==5);
        streaks.Death(2,3,false,false);check(streaks.Get(2)==0&&streaks.Get(3)==0);
        streaks.Death(1,3,true,true);check(streaks.Get(3)==0);
    }
    {
        SkinRender::WeaponProfiles<SkinModel::Selection> weapons;
        SkinModel::Selection professional;professional.enabled=true;professional.tier=3;
        weapons.Remember(14,2,professional);check(weapons.Find(14,2)&&weapons.Find(14,2)->tier==3);
        weapons.Remember(16,2,SkinModel::Selection{});
        check(weapons.Find(14,2)&&weapons.Find(14,2)->tier==3); // Swap away, kill, return.
        check(weapons.Find(16,2)&&!weapons.Find(16,2)->enabled);
        check(!weapons.Find(14,3)&&!weapons.Find(9999,2));
        weapons.Remember(14,2,SkinModel::Selection{});check(!weapons.Find(14,2)->enabled);
        weapons.Remember(18,3,professional);check(!weapons.Find(14,2)&&weapons.Find(18,3));
        for(int n=0;n<100;++n){weapons.Remember(n,3,professional);check(weapons.values.size()<=32);}
        SkinRender::MilestoneSoundGate gate;
        for(int n=1;n<=100;++n){check(gate.Due(n,false)==(n%5==0));check(!gate.Due(n,false));}
        gate.Reset();check(gate.Due(5,false));check(!gate.Due(5,false));
        gate.Reset();for(int n=1;n<=100;++n)check(gate.Due(n,true)==(n%20==0));
        gate.Reset();check(!gate.Due(0,false)&&!gate.Due(-1,false)&&!gate.Due(1000005,false));
        check(SkinRender::KillstreakCount(6,0,false,100,true)==6); // Local native count lag cannot erase tracked kills.
        SkinRender::SharedStreak count;check(count.Sync(4,1,4));
        check(count.Value(5)==5);weapons.Remember(18,3,professional);check(count.Value(6)==6);
    }
    {
        SkinProtocol::Message m;m.type=SkinProtocol::Style;m.server=123;m.nonce=456;m.echo=789;m.weapon=14;m.cls=2;
        m.selection.enabled=true;m.selection.tier=3;m.selection.preview=true;m.selection.previewCount=100;
        check(SkinProtocol::Decode(SkinProtocol::Encode(m))->streak==-1);
        m.streak=12;m.life=4;m.user=101;
        auto bytes=SkinProtocol::Encode(m);check(bytes.size()==67&&bytes[8]==3);
        auto result=SkinProtocol::Decode(bytes);
        check(result&&result->streak==12&&result->life==4&&result->user==101);
        check(result&&!result->selection.preview&&result->selection.previewCount==5);
        for(size_t n=0;n<bytes.size();++n)check(!SkinProtocol::Decode(std::span(bytes).first(n)));
        auto bad=bytes;bad.push_back(0);check(!SkinProtocol::Decode(bad));
        for(int field:{55,59,63})
        {
            bad=bytes;for(int n=0;n<4;++n)bad[field+n]=255;check(!SkinProtocol::Decode(bad)||field==59);
            if(field!=55){bad=bytes;for(int n=0;n<4;++n)bad[field+n]=0;check(!SkinProtocol::Decode(bad));}
        }
        for(int count:{0,1,5,10,1000000}){m.streak=count;check(SkinProtocol::Decode(SkinProtocol::Encode(m))->streak==count);}
        m.streak=1000001;check(!SkinProtocol::Decode(SkinProtocol::Encode(m)));
        m.streak=5;m.life=0;check(!SkinProtocol::Decode(SkinProtocol::Encode(m)));
        m.life=1;m.user=-1;check(!SkinProtocol::Decode(SkinProtocol::Encode(m)));
        SkinRender::SharedStreak s;check(s.Value(0)==-1);
        check(!s.Sync(-1,1,0)&&!s.Sync(1000001,1,0)&&!s.Sync(1,0,0));
        check(s.Sync(10,4,2)&&s.Value(2)==10); // Joining mid-streak.
        check(s.Value(3)==11);check(s.Sync(11,4,3)&&s.Value(3)==11); // Event before snapshot.
        check(s.Sync(12,4,3)&&s.Value(3)==12);check(s.Value(4)==12); // Snapshot before event.
        s.ResetLife();check(s.Value(0)==0);check(!s.Sync(12,4,0));
        check(s.Sync(0,5,0)&&s.Value(0)==0);s.ResetLife();check(s.Sync(0,5,0));
        check(s.Sync(1,5,1)&&s.Value(1)==1);check(!s.Sync(2,4,1));
        check(s.Sync(0,6,0)&&s.Value(0)==0);
        check(s.Sync(1000000,6,0)&&s.Value(10)==1000000);
    }
    {
        SkinModel::CosmeticEffects fx;fx.active=true;fx.sourceSounds[1]="Weapon_Pistol.Single";fx.sounds[1]="Weapon_Capper.Single";
        check(fx.ReplaceSound("Weapon_Pistol.Single")=="Weapon_Capper.Single");
        check(fx.ReplaceSound("weapon_pistol.single")=="Weapon_Capper.Single");
        check(fx.ReplaceSound("Player.Footstep").empty());check(fx.ReplaceSound("Weapon_Pistol.Reload").empty());
        fx.active=false;check(fx.ReplaceSound("Weapon_Pistol.Single").empty());fx.active=true;
        fx.sounds[1]=fx.sourceSounds[1];check(fx.ReplaceSound("Weapon_Pistol.Single").empty());
        auto script=SkinModel::Parser(R"("Weapon_Pistol.Single" { "wave" "#weapons/pistol_shoot.wav" } "Weapon_Test.Single" { "rndwave" { "wave" "weapons/test1.wav" "wave" "weapons/test2.wav" } } )").Parse();
        check(script.has_value());std::map<std::string,std::set<std::string>> waves;SkinModel::ReadSoundWaves(*script,waves);
        check(waves["weapon_pistol.single"].contains("weapons/pistol_shoot.wav"));check(waves["weapon_test.single"].size()==2);
        check(SkinModel::WaveKey("*WEAPONS\\PISTOL_SHOOT.WAV")=="weapons/pistol_shoot.wav");
        check(SkinModel::WaveKey("../unsafe.wav").empty());check(SkinModel::WaveKey("C:/unsafe.wav").empty());
        fx.muzzle="muzzle_raygun_red";fx.sourceMuzzle="muzzle_pistol";
        check(fx.Muzzle("muzzle_pistol")=="muzzle_raygun_red");check(fx.Muzzle("muzzle_rocket").empty());
        check(SkinRender::AllowCosmeticTracer(true,false,false,"None"));
    }
    {
        for(int tier=1;tier<=3;++tier)for(int sheen=1;sheen<=7;++sheen)for(int effect=2002;effect<=2008;++effect)
        {
            check(SkinRender::KillstreakAttribute("killstreak_tier",tier,sheen,effect)==tier);
            check(SkinRender::KillstreakAttribute("killstreak_idleeffect",tier,sheen,effect)==(tier>=2?sheen:0));
            check(SkinRender::KillstreakAttribute("killstreak_effect",tier,sheen,effect)==(tier==3?effect:0));
            for(auto stat:{"mult_dmg","mult_postfiredelay","is_festivized","set_attached_particle","set_weapon_mode"})check(!SkinRender::KillstreakAttribute(stat,tier,sheen,effect));
        }
        for(int tier:{-1,0,4,1000})check(!SkinRender::KillstreakAttribute("killstreak_effect",tier,1,2002));
        check(!SkinRender::KillstreakAttribute("killstreak_effect",3,0,2002));
        check(!SkinRender::KillstreakAttribute("killstreak_effect",3,8,2002));
        check(!SkinRender::KillstreakAttribute("killstreak_effect",3,1,2001));
        check(!SkinRender::KillstreakAttribute("killstreak_effect",3,1,2009));
        SkinRender::LifeStreaks streaks;
        streaks.Death(1,2,false);check(streaks.Get(2)==1&&streaks.Get(1)==0);
        streaks.Death(1,2,true);check(streaks.Get(2)==1);
        streaks.Death(3,2,false);check(streaks.Get(2)==2);
        streaks.Death(2,2,false);check(streaks.Get(2)==0);
        streaks.Death(2,0,false);check(streaks.Get(0)==0);
        streaks.Death(3,2,false);streaks.Reset(2);check(streaks.Get(2)==0);
        check(streaks.DeathOnce(100,1,1,2,false));check(streaks.Get(2)==1);
        check(!streaks.DeathOnce(100,1,1,2,false));check(streaks.Get(2)==1);
        check(streaks.DeathOnce(100,1,3,2,false));check(streaks.Get(2)==2);
        check(streaks.DeathOnce(100,2,1,2,false));check(streaks.Get(2)==3);
        check(streaks.DeathOnce(101,2,2,3,true));check(streaks.Get(2)==3&&streaks.Get(3)==0);
        check(!streaks.DeathOnce(101,2,2,3,true));check(streaks.Get(2)==3);
        check(streaks.DeathOnce(102,2,2,2,false));check(streaks.Get(2)==0);
        check(!streaks.DeathOnce(102,2,2,2,false));
        streaks.Spawn(2);check(streaks.DeathOnce(102,2,2,3,false));check(streaks.Get(3)==1);
        streaks.Spawn(2);check(streaks.DeathOnce(102,2,2,3,false));check(streaks.Get(3)==2);
        streaks.Death(0,3,false);check(streaks.Get(3)==2);
        streaks.Clear();check(streaks.counts.empty()&&streaks.seen.empty());
        check(streaks.DeathOnce(100,2,1,2,false));check(streaks.Get(2)==1);
        for(int native:{-1,0,3,5,1000001})for(int tracked:{0,1,4,10})
        {
            const int actual=native>=0&&native<=1000000?std::max(native,tracked):tracked;
            check(SkinRender::KillstreakCount(tracked,native,true,100,false)==actual);
            check(SkinRender::KillstreakCount(tracked,native,true,100,true)==100);
            check(SkinRender::KillstreakCount(tracked,native,false,100,true)==actual);
        }
        streaks.Clear();for(int event=0;event<256;++event)check(streaks.DeathOnce(event,3,1,2,false));
        check(!streaks.DeathOnce(257,3,1,2,false));check(streaks.Get(2)==256);
        check(streaks.DeathOnce(257,4,1,2,false));check(streaks.Get(2)==257);
        // Scoped native attribute answers must unwind correctly, including an
        // empty nested scope and exception paths.
        const int* context=nullptr;int a=1,b=2;
        {
            SkinRender::ScopedPointer<int> outer(context,&a);check(context==&a);
            {SkinRender::ScopedPointer<int> inner(context,&b);check(context==&b);}check(context==&a);
            {SkinRender::ScopedPointer<int> empty(context,nullptr);check(context==nullptr);}check(context==&a);
            try{SkinRender::ScopedPointer<int> inner(context,&b);throw 1;}catch(int){}check(context==&a);
        }
        check(context==nullptr);
        SkinModel::Selection settings;settings.enabled=true;settings.tier=3;settings.sheen=7;settings.effect=2008;settings.preview=true;settings.previewCount=100;
        SkinProtocol::Message message;message.type=SkinProtocol::Style;message.server=123;message.nonce=111;message.cls=2;message.weapon=14;message.selection=settings;
        auto decoded=SkinProtocol::Decode(SkinProtocol::Encode(message));
        check(decoded&&decoded->selection.tier==3&&decoded->selection.sheen==7&&decoded->selection.effect==2008);
        check(decoded&&!decoded->selection.preview&&decoded->selection.previewCount==5);
    }
    {
        SkinRender::PaintRequestGate gate;
        gate.Observe(390,1,0,0);check(gate.Ready(0));
        gate.Observe(390,1,1,.01);check(!gate.Ready(.01));
        for(int seed=2;seed<=1000;++seed)
        {double now=seed*.01;gate.Observe(390,1,seed,now);check(!gate.Ready(now));check(!gate.Ready(now+.1));}
        check(gate.seed==1000);check(gate.Ready(10.121));
        gate.Observe(414,1,1000,11);check(gate.Ready(11));
        gate.Observe(414,2,1000,11.01);check(gate.Ready(11.01));
        gate.Observe(414,2,1000,11.02);check(gate.changed==11.01);
        struct Texture {int refs=1;void AddRef(){++refs;}void Release(){--refs;}} a,b;
        {
            SkinRender::RetainedPaint<Texture> previous;
            previous.Set(&a);check(a.refs==2);previous.Set(&a);check(a.refs==2);
            previous.Set(&b);check(a.refs==1&&b.refs==2);
            previous.Set(nullptr);check(b.refs==1);
            previous.Set(&a);
        }
        check(a.refs==1&&b.refs==1);
    }
    {
        // The installed engine returns zero for mod_studio's material count,
        // but GetModelMaterials accepts the studio header's numtextures.
        check(SkinRender::PaintMaterialCapacity(0,3,12)==12);
        for(int count=1;count<=128;++count)
        {check(SkinRender::PaintMaterialCapacity(0,3,count)==count);check(SkinRender::PaintMaterialCapacity(count,1,0)==count);}
        for(int invalid:{-1,0,129,1000000})check(SkinRender::PaintMaterialCapacity(0,3,invalid)==0);
        for(int model:{0,1,2,4})check(SkinRender::PaintMaterialCapacity(0,model,12)==0);
        check(SkinRender::PaintMaterialCapacity(-1,3,12)==0);
        check(SkinRender::PaintMaterialCapacity(129,3,12)==0);
    }
    {
        struct Texture {int refs=1;void AddRef(){++refs;}void Release(){--refs;}} stock,paintA,paintB;
        struct Var {Texture* current;Texture* GetTextureValue(){return current;}
            void SetTextureValue(Texture* value){value->AddRef();current->Release();current=value;}} var{&stock};
        using Binding=SkinRender::SurfaceTextureBinding<Var,Texture>;
        for(int i=0;i<1000;++i)
        {
            {
                std::vector<Binding> bindings;bindings.emplace_back(&var,&paintA);
                check(var.current==&paintA&&stock.refs==1&&paintA.refs==3);
                var.SetTextureValue(&stock);bindings[0].Apply();check(var.current==&paintA);
                bindings[0].Suspend();check(var.current==&stock);
                {Binding nested(&var,&paintB);check(var.current==&paintB);}
                check(var.current==&stock&&paintB.refs==1);
                bindings[0].Apply();check(var.current==&paintA);
                // Exercise vector relocation without double restoration/release.
                bindings.reserve(16);check(var.current==&paintA&&stock.refs==1&&paintA.refs==3);
            }
            check(var.current==&stock&&stock.refs==1&&paintA.refs==1&&paintB.refs==1);
        }
    }
    {
        using namespace SkinModel;
        check(PaintIndex(0)==-1);check(PaintIndex(1)==0);check(PaintIndex(391)==390);
        check(PaintIndex(-1)==-1);check(PaintIndex(65536)==-1);
        for(int n=0;n<=100000;++n)
        {
            float wear=n/100000.f;
            check(WearLevel(wear)==(wear<=.2f?1:wear<=.4f?2:wear<=.6f?3:wear<=.8f?4:5));
        }
        check(WearLevel(.2f)==1);check(WearLevel(std::nextafter(.2f,1.f))==2);
        check(WearLevel(.4f)==2);check(WearLevel(std::nextafter(.4f,1.f))==3);
        check(WearLevel(.6f)==3);check(WearLevel(std::nextafter(.6f,1.f))==4);
        check(WearLevel(.8f)==4);check(WearLevel(std::nextafter(.8f,1.f))==5);
        check(std::string_view(WearName(0))=="Factory New");check(std::string_view(WearName(1))=="Battle Scarred");
        for(int kit:{0,1,14,114,390,65534})for(int seed:{0,1,999999,1000000})
        {
            SkinProtocol::Message message;message.type=SkinProtocol::Style;message.server=123;message.nonce=456;message.echo=789;message.weapon=18;message.cls=3;
            message.selection.enabled=true;message.selection.finish=kit+1;message.selection.wear=.73f;message.selection.seed=seed;message.selection.unusual=701;
            auto decoded=SkinProtocol::Decode(SkinProtocol::Encode(message));
            check(decoded&&PaintIndex(decoded->selection.finish)==kit&&decoded->selection.seed==seed);
            check(decoded&&std::abs(decoded->selection.wear-.73f)<.00011f&&decoded->selection.unusual==701);
        }
    }
    {
        using namespace SkinModel;
        Selection selection;check(selection.unusual==0);check(selection.finish==0);
        for(int id:{701,702,703,704})
        {
            selection.unusual=id;check(selection.Valid());
            check(!WeaponUnusualName(id,"rocketlauncher").empty());
            SkinProtocol::Message message;message.type=SkinProtocol::Style;message.server=123;message.nonce=456;message.echo=789;
            message.weapon=18;message.cls=3;message.selection=selection;
            auto packet=SkinProtocol::Encode(message);check(packet.size()==55&&packet[8]==2);
            auto decoded=SkinProtocol::Decode(packet);check(decoded&&decoded->selection.unusual==id&&decoded->selection.finish==0);
            packet.pop_back();check(!SkinProtocol::Decode(packet));
        }
        check(WeaponUnusualName(701,"sniperrifle")=="weapon_unusual_hot_sniperrifle");
        check(WeaponUnusualName(702,"sniperrifle")=="weapon_unusual_isotope_sniperrifle");
        check(WeaponUnusualName(703,"sniperrifle")=="weapon_unusual_cool_sniperrifle");
        check(WeaponUnusualName(704,"sniperrifle")=="weapon_unusual_energyorb_sniperrifle");
        for(int invalid:{-1,1,700,705,2002,65535}){selection.unusual=invalid;check(!selection.Valid());check(WeaponUnusualName(invalid,"sniperrifle").empty());}
        check(WeaponUnusualName(701,"").empty());check(WeaponUnusualName(701,"paintkit").empty());check(WeaponUnusualName(701,"../bad").empty());
    }
    {
        using namespace SkinModel;
        Item stock,classic,modern,bot;
        stock.attachments[1]={{"models/pilot.mdl",-1}};
        classic.festive=true;classic.attachments[1]={{"models/lights.mdl",-1},{"models/pilot.mdl",-1}};
        auto extras=CosmeticAttachments(stock,classic,2,true,false);
        check(extras.size()==1&&extras[0].model=="models/lights.mdl");
        check(CosmeticAttachments(classic,classic,2,true,false).empty());
        modern.festivized=true;modern.festiveAttachments[1]={{"models/modern.mdl",-1}};
        modern.festiveAttachments[2]={{"models/blue.mdl",1}};
        check(modern.CanFestivize());check(!stock.CanFestivize());check(!classic.CanFestivize());
        check(CosmeticAttachments(stock,modern,2,true,false).empty());
        check(CosmeticAttachments(stock,modern,2,true,true)[0].model=="models/modern.mdl");
        check(CosmeticAttachments(stock,modern,3,false,true)[0].model=="models/blue.mdl");
        check(CosmeticAttachments(modern,modern,2,true,true,true).empty());
        check(CosmeticAttachments(modern,modern,2,true,true,false).size()==1);
        bot=modern;bot.botkiller=true;bot.extraView="models/bot_view.mdl";bot.extraWorld="models/bot_world.mdl";
        check(!bot.CanFestivize());check(CosmeticAttachments(stock,bot,2,true,true)[0].model==bot.extraView);
        check(CosmeticAttachments(stock,bot,3,false,true)[0].skin==1);
        auto parsed=Parser(R"("visuals" { "attached_models_festive" { "0" { "model" "models/valid.mdl" } "1" { "model" "../bad.mdl" } "2" { "model" "models/valid.mdl" } "3" { "model" "models/bad.txt" } "4" { "model" "models/negative.mdl" "skin" "-5" } } })").Parse();
        check(parsed.has_value());check(ReadAttachments(parsed->Find("visuals"),"attached_models_festive").size()==1);
        check(ReadAttachments(nullptr,"attached_models").empty());
        classic.attachments[1].clear();for(int n=0;n<30;++n)classic.attachments[1].push_back({"models/"+std::to_string(n)+".mdl",-1});
        check(CosmeticAttachments(stock,classic,2,true,false).size()==MaxCosmeticAttachments);
    }
    {
        using namespace SkinJiggle;
        Matrix goal{{{1,0,0,0},{0,1,0,0},{0,0,1,0}}},output{};
        Params p{};p.flags=49;p.length=20;p.tipMass=550;p.yawDamping=2;p.pitchStiffness=1000;p.pitchDamping=10;p.angleLimit=.7853982f;
        check(Valid(p));State state;check(state.Build(p,goal,output,0,0));
        goal[0][3]=5;check(state.Build(p,goal,output,1./60,1));
        check(std::abs(output[0][2])>.01f);check(std::abs((state.tip-Column(goal,3)).Length()-20)<.001f);
        auto cached=output;auto savedTip=state.tip;auto savedVelocity=state.tipVelocity;
        for(int pass=0;pass<100;++pass){check(state.Build(p,goal,output,1./60,1));check(output==cached);}
        check((state.tip-savedTip).Length()==0);check((state.tipVelocity-savedVelocity).Length()==0);
        for(int frame=2;frame<=1800;++frame)
        {
            goal[0][3]=5*std::sin(frame*.04f);goal[1][3]=4*std::cos(frame*.02f);
            check(state.Build(p,goal,output,frame/60.,frame));
            auto direction=Column(output,2);
            check(direction.Finite());check(std::abs(direction.Length()-1)<.0001f);
            check(direction.z>=std::cos(p.angleLimit)-.0001f);
            check(std::abs((state.tip-Column(goal,3)).Length()-p.length)<.001f);
            check(std::abs(Column(output,0).Dot(Column(output,1)))<.0001f);
        }
        check(state.Build(p,goal,output,40,2000));check(output==goal); // long absence
        goal[0][3]=1000;check(state.Build(p,goal,output,40.01,2001));check(output==goal); // teleport
        check(state.Build(p,goal,output,0,0));check(output==goal); // clock/map reset
        State independent;Matrix origin{{{1,0,0,0},{0,1,0,0},{0,0,1,0}}};
        check(independent.Build(p,origin,output,0,0));check(output==origin);
        Params jaw{};jaw.flags=64;jaw.length=10;jaw.baseStiffness=250;jaw.baseDamping=1;jaw.baseMinUp=-.4f;jaw.baseMaxUp=.1f;
        check(Valid(jaw));State jawState;
        for(int frame=0;frame<120;++frame)
        {origin[1][3]=std::sin(frame*.1f);check(jawState.Build(jaw,origin,output,frame/60.,frame));
            check(std::abs(output[0][3]-origin[0][3])<.0001f);check(std::abs(output[2][3]-origin[2][3])<.0001f);
            check(output[1][3]-origin[1][3]>=-.4001f&&output[1][3]-origin[1][3]<=.1001f);}
        Params pitch=p;pitch.flags=41;pitch.minPitch=-.1396263f;pitch.maxPitch=.6108652f;State pitchState;
        origin[1][3]=0;pitchState.Build(pitch,origin,output,0,0);
        for(int frame=1;frame<500;++frame)
        {origin[1][3]=20*std::sin(frame*.07f);check(pitchState.Build(pitch,origin,output,frame/60.,frame));
            float angle=std::atan2(output[1][2],output[2][2]);check(angle>=pitch.minPitch-.0001f&&angle<=pitch.maxPitch+.0001f);}
        Params yaw=p;yaw.flags=37;yaw.minYaw=-.2f;yaw.maxYaw=.3f;State yawState;
        yawState.Build(yaw,origin,output,0,0);
        for(int frame=1;frame<500;++frame)
        {origin[0][3]=20*std::sin(frame*.07f);check(yawState.Build(yaw,origin,output,frame/60.,frame));
            float angle=std::atan2(output[0][2],output[2][2]);check(angle>=yaw.minYaw-.0001f&&angle<=yaw.maxYaw+.0001f);}
        Matrix flipped{{{-1,0,0,0},{0,1,0,0},{0,0,1,0}}};State mirror;
        mirror.Build(p,flipped,output,0,0);check(output==flipped);
        flipped[0][3]=2;check(mirror.Build(p,flipped,output,1./60,1));
        check(Column(output,0).Cross(Column(output,1)).Dot(Column(output,2))<-.99f);
        Params bad=p;bad.tipMass=std::numeric_limits<float>::quiet_NaN();check(!Valid(bad));
        bad=p;bad.flags=128;check(!Valid(bad));bad=p;bad.length=0;check(!Valid(bad));
        bad=jaw;bad.baseMinUp=2;check(!Valid(bad));
        std::array<char,160> bytes{};std::memcpy(bytes.data()+20,&p,sizeof(p));Params read{};
        check(Read(bytes,10,10,read));check(read.length==20);
        check(!Read(bytes,10,11,read));check(!Read(bytes,-100,1,read));check(!Read(bytes,10,0,read));
        auto invalid=origin;invalid[0][0]=std::numeric_limits<float>::infinity();check(!state.Build(p,invalid,output,1,1));
        check(!state.Build(p,origin,output,std::numeric_limits<double>::quiet_NaN(),1));
        invalid=origin;invalid[0][0]=0;check(!state.Build(p,invalid,output,1,1));
        for(int fps:{20,30,60,144,360})
        {
            State soak;Matrix fixed{{{1,0,0,0},{0,1,0,0},{0,0,1,0}}};
            for(int frame=0;frame<fps*10;++frame)check(soak.Build(p,fixed,output,double(frame)/fps,frame));
        }
    }
    {
        SkinRender::HandSequenceLatch latch;
        SkinRender::HandSequenceKey key{1,2,3,4,638,2,8,35,1642,98,1,1};
        int selections=0;auto select=[&]{return 25+(selections++%3);};
        for(int frame=0;frame<1000;++frame)check(latch.Resolve(key,float(frame)/1000,select)==25);
        check(selections==1); // simulation + glow + normal draws cannot reroll
        check(latch.Resolve(key,.9f,select)==25);check(selections==1); // small correction
        check(latch.Resolve(key,0,select)==26);check(selections==2); // full cycle wrap
        ++key.sequenceParity;check(latch.Resolve(key,0,select)==27);check(selections==3);
        ++key.eventParity;latch.Resolve(key,0,select);check(selections==4);
        ++key.weapon;latch.Resolve(key,0,select);check(selections==5);
        ++key.reskin;latch.Resolve(key,0,select);check(selections==6);
        ++key.header;latch.Resolve(key,0,select);check(selections==7);
        ++key.source;latch.Resolve(key,0,select);check(selections==8);
        ++key.team;latch.Resolve(key,0,select);check(selections==9);
        ++key.playerClass;latch.Resolve(key,0,select);check(selections==10);
        latch.Clear();latch.Resolve(key,0,select);check(selections==11);
        check(latch.Resolve(key,std::numeric_limits<float>::quiet_NaN(),select)==-1);check(!latch.valid);
        latch.Resolve(key,0,select);check(selections==12);
        latch.Clear();check(latch.Resolve(key,0,[]{return -1;})==-1);
        check(latch.Resolve(key,.1f,select)==-1);check(selections==12); // failed lookup stays stable too
    }
    {
        using Map=std::map<std::string,std::string>;
        Map replacements={{"ACT_VM_IDLE","ACT_ITEM1_VM_IDLE"}};
        std::map<std::string,int> registry={{"ACT_VM_IDLE",101},{"ACT_MELEE_VM_IDLE",201},{"ACT_PRIMARY_VM_IDLE",301},{"ACT_SECONDARY_VM_IDLE",401},{"ACT_PDA_VM_IDLE",601},{"ACT_ITEM1_VM_IDLE",501}};
        auto lookup=[&](const std::string& name){auto it=registry.find(name);return it==registry.end()?-1:it->second;};
        for(int activity:{101,201,301,401,501,601})check(SkinRender::RenderHandActivity(replacements,activity,lookup)==501);
        check(SkinRender::RenderHandActivity(replacements,999,lookup)==999);
        check(!SkinRender::RenderHandActivity(replacements,-1,lookup));
        check(!SkinRender::RenderHandActivity(Map{{"ACT_VM_IDLE","ACT_UNKNOWN"}},201,lookup));
        for(bool effect:{false,true})for(bool registered:{false,true})for(bool header:{false,true})for(bool hardware:{false,true})for(bool bones:{false,true})
            check(SkinRender::AllowEffectPreparation(effect,registered,header,hardware,bones)==(!effect||(registered&&header&&hardware&&bones)));
        struct Entity {int sequence=3;int& m_nSequence(){return sequence;}} entity;
        int invalidations=0;auto invalidate=[&](Entity*){++invalidations;};
        {SkinRender::RenderSequenceScope scope(&entity,8,invalidate);check(entity.sequence==8);check(invalidations==1);}
        check(entity.sequence==3);check(invalidations==2);
        {SkinRender::RenderSequenceScope scope(&entity,-1,invalidate);check(entity.sequence==3);}check(invalidations==2);
        {SkinRender::RenderSequenceScope scope(&entity,3,invalidate);check(entity.sequence==3);}check(invalidations==2);
        try {SkinRender::RenderSequenceScope scope(&entity,8,invalidate);throw 1;}catch(int){}
        check(entity.sequence==3&&invalidations==4);
        {SkinRender::RenderSequenceScope<Entity,decltype(invalidate)> scope(nullptr,8,invalidate);}check(invalidations==4);
    }
    {
        using Map=std::map<std::string,std::string>;
        Map sharp={{"ACT_VM_DRAW","ACT_ITEM1_VM_DRAW"},{"ACT_VM_IDLE","ACT_ITEM1_VM_IDLE"}};
        std::map<std::string,int> registry={{"ACT_VM_DRAW",101},{"ACT_VM_IDLE",102},{"ACT_ITEM1_VM_DRAW",501},{"ACT_ITEM1_VM_IDLE",502}};
        auto lookup=[&](const std::string& name){auto it=registry.find(name);return it==registry.end()?-1:it->second;};
        check(SkinRender::CosmeticHandActivity(sharp,101,lookup)==501);
        check(SkinRender::CosmeticHandActivity(sharp,102,lookup)==502);
        check(SkinRender::CosmeticHandActivity(sharp,103,lookup)==103);
        check(SkinRender::CosmeticHandActivity(Map{},101,lookup)==101); // stock restores class translation
        check(!SkinRender::CosmeticHandActivity(Map{{"ACT_VM_DRAW","ACT_MISSING"}},101,lookup));
        check(!SkinRender::CosmeticHandActivity(sharp,-1,lookup));
        check(!SkinRender::CosmeticHandActivity(sharp,0,lookup));
        const Map* context=nullptr;
        {SkinRender::ScopedPointer outer(context,&sharp);check(context==&sharp);
            {SkinRender::ScopedPointer<Map> nested(context,nullptr);check(!context);}check(context==&sharp);}
        check(!context);
        try {SkinRender::ScopedPointer outer(context,&sharp);throw 1;}catch(int){}check(!context);
        SkinModel::Catalog catalog;check(catalog.Load(R"(items_game {items {4 {name Knife item_class tf_weapon_knife item_slot melee model_player models/stock.mdl used_by_classes {spy 1}} 638 {name "Sharp Dresser" item_class tf_weapon_knife item_slot melee model_player models/sharp.mdl used_by_classes {spy 1} visuals {animation_replacement {ACT_VM_DRAW ACT_ITEM1_VM_DRAW ACT_VM_IDLE ACT_ITEM1_VM_IDLE}} visuals_red {animation_replacement {ACT_VM_DRAW ACT_ITEM2_VM_DRAW}}}}})"));
        check(catalog.Find(638)->Animations(2).at("ACT_VM_DRAW")=="ACT_ITEM2_VM_DRAW");
        check(catalog.Find(638)->Animations(3).at("ACT_VM_DRAW")=="ACT_ITEM1_VM_DRAW");
        check(catalog.Find(638)->Animations(2).at("ACT_VM_IDLE")=="ACT_ITEM1_VM_IDLE");
        check(catalog.Find(4)->Animations(2).empty());
    }
    {
        std::array<char,5> bytes={'x','a','b',0,'z'};
        check(SkinRender::BoundedModelString(bytes,1)=="ab");
        auto owned=SkinRender::BoundedModelString(bytes,1);bytes[1]='c';check(*owned=="ab");
        check(SkinRender::BoundedModelString(bytes,3)=="");
        check(!SkinRender::BoundedModelString(bytes,-1));
        check(!SkinRender::BoundedModelString(bytes,5));
        check(!SkinRender::BoundedModelString(bytes,4));
        check(!SkinRender::BoundedModelString(bytes,std::numeric_limits<int64_t>::max()));
        check(!SkinRender::BoundedModelString({},0));
        std::array<char,129> longName;longName.fill('a');longName[128]=0;
        check(!SkinRender::BoundedModelString(longName,0));longName[127]=0;
        check(SkinRender::BoundedModelString(longName,0)->size()==127);
        for(bool chams:{false,true})for(bool glow:{false,true})for(bool effect:{false,true})
            check(SkinRender::AllowCosmeticRender(chams,glow,effect)==(!chams&&!glow&&!effect));
        for(bool chams:{false,true})for(bool glow:{false,true})for(bool effect:{false,true})for(bool bones:{false,true})
            check(SkinRender::AllowCosmeticPreparation(chams,glow,effect,bones)==(effect?bones:(!chams&&!glow)));
    }
    {
        for(bool error:{false,true})for(int count:{-1,0,1,6,128,129})for(auto bone:{"weapon_bone","dummy_bone"})
            check(SkinRender::ReadyModelHeader(error,bone,count)==(!error&&count>0&&count<=128&&std::string_view(bone)!="dummy_bone"));
        using Map=std::map<std::string,std::string>;
        Map stock,sharp={{"ACT_VM_DRAW","ACT_ITEM1_VM_DRAW"},{"ACT_VM_IDLE","ACT_ITEM1_VM_IDLE"},{"ACT_VM_HITCENTER","ACT_ITEM1_VM_HITCENTER"},{"ACT_BACKSTAB_VM_UP","ACT_ITEM1_BACKSTAB_VM_UP"},{"ACT_MELEE_VM_INSPECT_START","ACT_ITEM1_VM_INSPECT_START"}};
        check(SkinRender::ReplacementHandActivity("ACT_MELEE_VM_DRAW",stock,sharp)=="ACT_ITEM1_VM_DRAW");
        check(SkinRender::ReplacementHandActivity("ACT_MELEE_VM_IDLE",stock,sharp)=="ACT_ITEM1_VM_IDLE");
        check(SkinRender::ReplacementHandActivity("ACT_MELEE_VM_HITCENTER",stock,sharp)=="ACT_ITEM1_VM_HITCENTER");
        check(SkinRender::ReplacementHandActivity("ACT_BACKSTAB_VM_UP",stock,sharp)=="ACT_ITEM1_BACKSTAB_VM_UP");
        check(SkinRender::ReplacementHandActivity("ACT_MELEE_VM_INSPECT_START",stock,sharp)=="ACT_ITEM1_VM_INSPECT_START");
        check(SkinRender::ReplacementHandActivity("ACT_ITEM1_VM_DRAW",sharp,stock)=="ACT_VM_DRAW");
        check(SkinRender::ReplacementHandActivity("ACT_MELEE_VM_IDLE",stock,stock)=="ACT_MELEE_VM_IDLE");
        check(SkinRender::ReplacementHandActivity("ACT_VM_UNKNOWN",stock,sharp)=="ACT_VM_UNKNOWN");
        check(SkinRender::HandActivityKey("ACT_PRIMARY_VM_DRAW")=="ACT_VM_DRAW");
        check(SkinRender::HandActivityKey("ACT_SECONDARY_VM_IDLE")=="ACT_VM_IDLE");
        SkinModel::Catalog catalog;check(catalog.Load(R"(items_game {items {4 {name Knife item_class tf_weapon_knife item_slot melee model_player models/stock.mdl used_by_classes {spy 1}} 638 {name "Sharp Dresser" item_class tf_weapon_knife item_slot melee model_player models/sharp_world.mdl model_viewmodel models/sharp_view.mdl used_by_classes {spy 1} visuals {animation_replacement {ACT_VM_DRAW ACT_ITEM1_VM_DRAW}}}}})"));
        check(catalog.Find(638)->Model(8)=="models/sharp_world.mdl");check(catalog.Find(638)->ViewModel(8)=="models/sharp_view.mdl");check(catalog.Find(4)->ViewModel(8)=="models/stock.mdl");
        check(catalog.Find(638)->animations.at("ACT_VM_DRAW")=="ACT_ITEM1_VM_DRAW");check(catalog.Canonical(638)==4);
    }
    {
        using namespace SkinModel;
        const auto schema=R"(items_game { prefabs { weapon { item_class tf_weapon_wrench item_slot melee model_player models/wrench.mdl used_by_classes {engineer 1} } } items {
          7 {prefab weapon name Wrench} 795 {prefab weapon name "Silver Botkiller Wrench Mk.I" extra_wearable models/silver.mdl extra_wearable_vm models/silver_vm.mdl attributes { "disable fancy class select anim" { value 1 } } }
          804 {prefab weapon name "Gold Botkiller Wrench Mk.I" extra_wearable models/silver.mdl visuals_red {skin 2} visuals_blu {skin 3} }
          999 {prefab weapon name "Gameplay variant" attributes {damage_bonus {value 2}}} } })";
        Catalog catalog;check(catalog.Load(schema));check(catalog.Canonical(795)==7);check(catalog.Canonical(804)==7);check(catalog.Canonical(999)==999);
        check(catalog.Variants(7,9).size()==4);check(catalog.Variants(7,1).empty());
        check(catalog.Canonical(999)!=catalog.Canonical(7)); // Cosmetic choices never merge stat families.
        auto silver=catalog.Find(795),gold=catalog.Find(804);check(silver->botkiller&&gold->botkiller&&!catalog.Find(7)->botkiller);
        check(silver->ExtraModel(true)=="models/silver_vm.mdl");check(silver->ExtraModel(false)=="models/silver.mdl");
        check(silver->ExtraSkin(2)==0&&silver->ExtraSkin(3)==1);check(gold->ExtraSkin(2)==2&&gold->ExtraSkin(3)==3);check(gold->ExtraModel(true)=="models/silver.mdl");
        std::vector<std::string_view> source={"weapon_bone"},target={"weapon_bone","chain","head"};std::vector<int> parents={-1,0,1},procedural={0,5,5},mapping;std::string_view missing;
        check(!SkinRender::RestBoneMap(source,target,parents,procedural,mapping,missing));
        check(SkinRender::RestBoneMap(source,target,parents,procedural,mapping,missing,true));check(mapping==std::vector<int>({0,-1,-1}));
        procedural[2]=1;check(!SkinRender::RestBoneMap(source,target,parents,procedural,mapping,missing,true));procedural[2]=5;parents[1]=-1;check(!SkinRender::RestBoneMap(source,target,parents,procedural,mapping,missing,true));
        parents[1]=2;check(!SkinRender::RestBoneMap(source,target,parents,procedural,mapping,missing,true));
    }
    {
        int creates=0,points=0,particle=1;
        check(!SkinRender::CreateNamedTracer([&]()->int*{++creates;return nullptr;},[&](int*,int){++points;}));check(creates==1&&points==0);
        check(SkinRender::CreateNamedTracer([&]{++creates;return &particle;},[&](int* effect,int point){check(effect==&particle);check(point==points);++points;}));check(creates==2&&points==2);
    }
    {
        using namespace SkinModel;
        auto node=Parser(R"(visuals { sound_single_shot "Weapon_AWP.Single" sound_burst "Weapon_AWP.SingleCrit" sound_reload "Weapon.TestReload" muzzle_flash "muzzle_stock" tracer_effect "bullet_tracer01" sound_special1 "../bad" sound_deploy "bad name" })").Parse();
        check(node.has_value());CosmeticEffects effects;effects.Read(node->Find("visuals"));
        check(effects.Sound(1)=="Weapon_AWP.Single");check(effects.Sound(5)=="Weapon_AWP.SingleCrit");check(effects.Sound(6)=="Weapon.TestReload");
        check(effects.Sound(-1).empty());check(effects.Sound(16).empty());check(effects.Sound(999).empty());check(effects.Sound(11).empty());check(effects.Sound(15).empty());
        check(effects.Tracer(2,false)=="bullet_tracer01_red");check(effects.Tracer(3,true)=="bullet_tracer01_blue_crit");check(effects.Tracer(0,true).empty());
        CosmeticEffects fallback;fallback.sounds[1]="Stock.Single";fallback.sounds[8]="Stock.Swing";fallback.muzzle="fallback_muzzle";fallback.tracer="fallback_tracer";
        effects.Fallback(fallback);check(effects.sounds[1]=="Weapon_AWP.Single"&&effects.sounds[8]=="Stock.Swing"&&effects.muzzle=="muzzle_stock"&&effects.tracer=="bullet_tracer01");
        CosmeticEffects plain;plain.Fallback(fallback);check(plain.sounds[1]=="Stock.Single"&&plain.muzzle=="fallback_muzzle"&&plain.tracer=="fallback_tracer");
        effects.sourceMuzzle="original_muzzle";check(effects.Muzzle("original_muzzle").empty());effects.active=true;
        check(effects.Muzzle("original_muzzle")=="muzzle_stock");check(effects.Muzzle("health_particle").empty());check(effects.Muzzle("muzzle_stock").empty());effects.sourceMuzzle.clear();check(effects.Muzzle("").empty());
        for(auto value:{"Weapon_AWP.Single","bullet_tracer_raygun","muzzle_raygun_blue","weapons/test.wav"})check(SafeEffectName(value));
        for(auto value:{"","bad;exec","bad name","../file","name\n","name\\file"})check(!SafeEffectName(value));check(!SafeEffectName(std::string(128,'a')));
        for(bool doEffects:{false,true})for(bool clean:{false,true})for(bool local:{false,true})for(auto configured:{"Default","None","Machina"})
            check(SkinRender::AllowCosmeticTracer(doEffects,clean,local,configured)==(doEffects&&!clean&&(!local||std::string_view(configured)=="Default")));
        int a=1,b=2;const int* pointer=&a;
        {SkinRender::ScopedPointer<int> outer(pointer,&b);check(pointer==&b);{SkinRender::ScopedPointer<int> nested(pointer,nullptr);check(pointer==nullptr);}check(pointer==&b);}check(pointer==&a);
        try {SkinRender::ScopedPointer<int> scope(pointer,nullptr);throw 1;}catch(int){}check(pointer==&a);
        auto schema=R"(items_game {prefabs { base { item_class tf_weapon_pistol item_slot secondary model_player models/pistol.mdl used_by_classes {scout 1} visuals {sound_single_shot "Base.Single" sound_burst "Base.Crit" tracer_effect base_tracer} }} items { 22 {prefab base name Stock} 100 {prefab base name Reskin visuals_red {muzzle_flash red_muzzle sound_single_shot Red.Single} visuals_blu {muzzle_flash blue_muzzle tracer_effect blue_tracer} } } })";
        Catalog catalog;check(catalog.Load(schema));auto item=catalog.Find(100);check(item!=nullptr);
        check(item->Effects(2).Sound(1)=="Red.Single");check(item->Effects(2).Sound(5)=="Base.Crit");check(item->Effects(3).Sound(1)=="Base.Single");
        check(item->Effects(2).muzzle=="red_muzzle"&&item->Effects(3).muzzle=="blue_muzzle");check(item->Effects(3).tracer=="blue_tracer"&&item->Effects(2).tracer=="base_tracer");
        check(item->Effects(0).muzzle.empty());check(catalog.Canonical(100)==22);
    }
    {
        check(SkinRender::ReplacementLOD(0,1,3,false)==0);
        check(SkinRender::ReplacementLOD(1,4,0,false)==1);
        check(SkinRender::ReplacementLOD(0,4,99,true)==2);
        check(SkinRender::ReplacementLOD(0,1,0,true)==-1);
        for(int count:{-1,0,9})check(SkinRender::ReplacementLOD(0,count,0,false)==-1);
        check(SkinRender::ReplacementLOD(-1,3,0,false)==-1);
        check(SkinRender::ReplacementLOD(3,3,0,false)==-1);
        for(int root=0;root<8;++root)for(int count=1;count<=8;++count)for(int original=-1;original<10;++original)for(bool shadow:{false,true})
        {int lod=SkinRender::ReplacementLOD(root,count,original,shadow);int visible=count-int(shadow);
            check(root>=visible?lod==-1:lod>=root&&lod<visible);}
        struct Cache {int locks=0;void BeginLock(){++locks;}void EndLock(){--locks;}} cache;
        {SkinRender::ModelCacheScope<Cache> scope(&cache);check(cache.locks==1);{SkinRender::ModelCacheScope<Cache> nested(&cache);check(cache.locks==2);}check(cache.locks==1);}check(cache.locks==0);
        try {SkinRender::ModelCacheScope<Cache> scope(&cache);throw 1;}catch(int){}check(cache.locks==0);
        {SkinRender::ModelCacheScope<Cache> scope(nullptr);check(cache.locks==0);}
    }
    {
        // The real v69 log passed the IClientRenderable subobject (+8), not the entity base.
        struct UnknownBase {virtual ~UnknownBase()=default;};
        struct RenderableBase {virtual ~RenderableBase()=default;};
        struct FakeViewmodel:UnknownBase,RenderableBase {} viewmodel;
        const void* entity=&viewmodel;const void* renderable=static_cast<RenderableBase*>(&viewmodel);
        check(entity!=renderable);
        check(SkinRender::MatchViewmodel(renderable,entity,renderable)==SkinRender::ViewmodelPointerKind::Renderable);
        check(SkinRender::MatchViewmodel(entity,entity,renderable)==SkinRender::ViewmodelPointerKind::Entity);
        FakeViewmodel other;check(SkinRender::MatchViewmodel(&other,entity,renderable)==SkinRender::ViewmodelPointerKind::None);
        check(SkinRender::MatchViewmodel(nullptr,entity,renderable)==SkinRender::ViewmodelPointerKind::None);
        check(SkinRender::MatchViewmodel(renderable,nullptr,renderable)==SkinRender::ViewmodelPointerKind::None);
        check(SkinRender::MatchViewmodel(renderable,entity,nullptr)==SkinRender::ViewmodelPointerKind::None);
        check(SkinRender::MatchViewmodel(reinterpret_cast<const void*>(uintptr_t(0x12651377f18)),reinterpret_cast<const void*>(uintptr_t(0x12651377f10)),reinterpret_cast<const void*>(uintptr_t(0x12651377f18)))==SkinRender::ViewmodelPointerKind::Renderable);
        check(SkinRender::MatchViewmodel(reinterpret_cast<const void*>(uintptr_t(0x12651377f20)),reinterpret_cast<const void*>(uintptr_t(0x12651377f10)),reinterpret_cast<const void*>(uintptr_t(0x12651377f18)))==SkinRender::ViewmodelPointerKind::None);
        // Replay the v68 log's failure: exact stock model, client-only entity=-1, no EHANDLE parent.
        check(SkinRender::LocalAttachment(true,false,true,-1));
        check(!SkinRender::LocalAttachment(true,false,false,-1));
        check(!SkinRender::LocalAttachment(true,false,true,165));
        check(!SkinRender::LocalAttachment(false,false,true,-1));
        check(SkinRender::LocalAttachment(true,true,false,53));
        check(!SkinRender::LocalAttachment(false,true,true,53));
        for(bool modelMatch:{false,true})for(bool parent:{false,true})for(bool draw:{false,true})for(int index:{-1,0,53,165})
            check(SkinRender::LocalAttachment(modelMatch,parent,draw,index)==(modelMatch&&(parent||(draw&&index==-1))));
        bool localScope=false;
        {SkinRender::ViewmodelDrawScope outer(localScope,true);check(localScope);
            {SkinRender::ViewmodelDrawScope nested(localScope,false);check(!localScope);}check(localScope);}
        check(!localScope);
        try {SkinRender::ViewmodelDrawScope scope(localScope,true);throw 1;}catch(int){}check(!localScope);
        using Matrix=std::array<float,12>;std::array<Matrix,128> scratch{},original{};Matrix* result=nullptr;int setupCalls=0;
        auto setup=[&](Matrix* destination,int capacity){++setupCalls;check(capacity==128);destination[0][0]=42;return true;};
        check(SkinRender::SkinOnlyBones<Matrix>(nullptr)==nullptr);check(SkinRender::SkinOnlyBones(original.data())==original.data());
        check(SkinRender::AcquireBones(original.data(),scratch,3,result,setup)==SkinRender::BoneSource::Custom);check(result==original.data()&&setupCalls==0);
        check(SkinRender::AcquireBones<Matrix>(nullptr,scratch,3,result,setup)==SkinRender::BoneSource::Setup);check(result==scratch.data()&&result[0][0]==42&&setupCalls==1);
        for(int size:{-1,0,129,10000}){check(SkinRender::AcquireBones<Matrix>(nullptr,scratch,size,result,setup)==SkinRender::BoneSource::InvalidLayout);check(result==nullptr&&setupCalls==1);}
        check(SkinRender::AcquireBones<Matrix>(nullptr,scratch,128,result,setup)==SkinRender::BoneSource::Setup);check(result!=nullptr&&setupCalls==2);
        check(SkinRender::AcquireBones<Matrix>(nullptr,scratch,3,result,[](Matrix*,int){return false;})==SkinRender::BoneSource::Unavailable);check(result==nullptr);
        std::vector<std::string_view> source={"root","weapon","bolt"},target={"bolt","root","weapon"};std::vector<int> mapping;std::string_view missing;
        check(SkinRender::BoneMap(source,target,mapping,missing));check(mapping==std::vector<int>({2,0,1}));
        target.push_back("missing");check(!SkinRender::BoneMap(source,target,mapping,missing));check(mapping.empty()&&missing=="missing");
        check(!SkinRender::BoneMap({},source,mapping,missing));target={""};check(!SkinRender::BoneMap(source,target,mapping,missing));
        source.resize(129,"root");check(!SkinRender::BoneMap(source,target,mapping,missing));
        source={"weapon_bone","vm_weapon_bone"};target={"weapon_bone","bolt"};std::vector<int> parents={-1,0},procedural={0,0};
        check(SkinRender::RestBoneMap(source,target,parents,procedural,mapping,missing));check(mapping==std::vector<int>({0,-1}));
        parents[1]=-1;check(!SkinRender::RestBoneMap(source,target,parents,procedural,mapping,missing));
        parents[1]=1;check(!SkinRender::RestBoneMap(source,target,parents,procedural,mapping,missing));
        parents[1]=99;check(!SkinRender::RestBoneMap(source,target,parents,procedural,mapping,missing));
        parents[1]=0;procedural[1]=1;check(!SkinRender::RestBoneMap(source,target,parents,procedural,mapping,missing));procedural[1]=0;
        target.push_back("child");parents.push_back(1);procedural.push_back(0);check(SkinRender::RestBoneMap(source,target,parents,procedural,mapping,missing));
        parents[1]=2;check(!SkinRender::RestBoneMap(source,target,parents,procedural,mapping,missing));
        check(!SkinRender::RestBoneMap(source,target,{},procedural,mapping,missing));
        using Transform=std::array<std::array<float,4>,3>;
        Transform identity={{{1,0,0,0},{0,1,0,0},{0,0,1,0}}},world=identity,parentPose=identity,childPose=identity,rest{};
        world[0][3]=100;parentPose[0][3]=-10;childPose[0][3]=-12;
        check(SkinRender::RestTransform(world,parentPose,childPose,rest));check(rest[0][3]==102&&rest[1][3]==0&&rest[2][3]==0);
        world={{{0,-1,0,100},{1,0,0,50},{0,0,1,0}}};
        check(SkinRender::RestTransform(world,parentPose,childPose,rest));check(rest[0][3]==100&&rest[1][3]==52);
        childPose[0][0]=2;check(!SkinRender::RestTransform(world,parentPose,childPose,rest));childPose=identity;
        world[0][0]=std::numeric_limits<float>::quiet_NaN();check(!SkinRender::RestTransform(world,parentPose,childPose,rest));
        // Bind matrices read from the installed AWPer Hand model (weapon_bone -> bolt).
        parentPose=identity;parentPose[1][3]=1.772578f;parentPose[2][3]=15.12125f;
        childPose=identity;childPose[0][3]=.6445276f;childPose[1][3]=-3.042394f;childPose[2][3]=15.17678f;
        check(SkinRender::RestTransform(identity,parentPose,childPose,rest));
        check(std::abs(rest[0][3]+.6445276f)<.0001f&&std::abs(rest[1][3]-4.814972f)<.0001f&&std::abs(rest[2][3]+.05553f)<.0001f);
        // Installed Eyelander has four skin families; stock rifle has ten; AWPer has one.
        check(SkinRender::ValidSkin(2,4)&&SkinRender::ValidSkin(3,4));
        check(SkinRender::ValidSkin(8,10)&&SkinRender::ValidSkin(9,10));
        check(!SkinRender::ValidSkin(2,1)&&!SkinRender::ValidSkin(-1,4)&&!SkinRender::ValidSkin(4,4)&&!SkinRender::ValidSkin(0,0));
        // Exercise every legal count with both custom matrices and engine setup.
        for(int n=1;n<=128;++n)
        {check(SkinRender::AcquireBones(original.data(),scratch,n,result,setup)==SkinRender::BoneSource::Custom);
            check(SkinRender::AcquireBones<Matrix>(nullptr,scratch,n,result,setup)==SkinRender::BoneSource::Setup);}
    }
    using namespace SkinModel;
    check(!Parser("\"broken").Parse());check(!Parser("a {").Parse());check(!Parser("}").Parse());check(!Parser("a { b }").Parse());
    auto parsed=Parser("//comment\na { b \"hello \\\"there\\\"\" c 5 }").Parse();check(parsed.has_value());check(parsed->Find("a")->Text("b")=="hello \"there\"");
    check(!Parser(std::string(17*1024*1024,' ')).Parse());
    check(Number("123")==123 && Number("123bad",-1)==-1);
    for(auto name:{"my skins","default","Friend_1","preset-5"})check(SafeName(name));
    for(auto name:{"","..","../outside","CON","LPT1","bad.json","name.","name ","A:B","a/b","a\\b"})check(!SafeName(name));
    Selection selection;check(selection.Valid());selection.wear=std::numeric_limits<float>::quiet_NaN();check(!selection.Valid());selection.wear=0;
    Selection awper;awper.enabled=true;awper.reskin=851;awper.australium=true;awper.festive=true;awper.seed=123;
    check(NormalizeReskinChange(awper,false));check(!awper.australium&&!awper.festive&&awper.reskin==851&&awper.enabled&&awper.seed==123);
    check(!NormalizeReskinChange(awper,false));awper.australium=true;check(!NormalizeReskinChange(awper,true));check(awper.australium);
    awper.reskin=0;check(!NormalizeReskinChange(awper,false));check(awper.australium);
    selection.seed=-1;check(!selection.Valid());selection.seed=1000001;check(!selection.Valid());selection.seed=0;
    Preset preset;preset[{14,0}].reskin=851;preset[{14,2}].reskin=201;
    check(Get(preset,14,2).reskin==201);check(Get(preset,14,1).reskin==851);preset.erase({14,2});check(Get(preset,14,2).reskin==851);check(!Get(preset,99,1).enabled);
    const char* fixture=R"("items_game" {
      "prefabs" { "weapon" { "item_class" "tf_weapon_sniperrifle" "item_slot" "primary" "model_player" "models/sniper.mdl" "used_by_classes" { "sniper" "1" } } }
      "items" {
        "14" { "prefab" "weapon" "name" "Sniper" }
        "201" { "prefab" "weapon" "name" "Upgradeable Sniper" "visuals" { "styles" { "1" { "image_inventory" "backpack/sniper_gold" "skin_red" "8" "skin_blu" "9" } } } }
        "851" { "prefab" "weapon" "name" "AWPer Hand" "model_player" "models/awp.mdl" }
        "999" { "prefab" "weapon" "name" "Stat variant" "attributes" { "mult_dmg" { "value" "2" } } }
      }
    })";
    Catalog catalog;check(catalog.Load(fixture));check(catalog.items.size()==4);check(catalog.Find(201)->Gold());check(!catalog.Find(851)->Gold());
    check(catalog.Canonical(851)==14);check(catalog.Canonical(999)==999);check(catalog.Variants(14,2).size()==4);check(catalog.Variants(14,1).empty());
    check(!catalog.Load("broken"));check(catalog.items.size()==4);
    using namespace SkinProtocol;
    {
        check(NativeContextAllowed(1,1,440));check(NativeContextAllowed(5,8,440));
        check(!NativeContextAllowed(0,1,440));check(!NativeContextAllowed(1,0,440));
        check(!NativeContextAllowed(-1,1,440));check(!NativeContextAllowed(1,-1,440));
        check(!NativeContextAllowed(1,1,0));check(!NativeContextAllowed(1,1,480));
        const std::array<std::pair<int,std::string_view>,9> states{{{0,"none"},{1,"connecting"},{2,"finding_route"},{3,"connected"},{4,"closed_by_peer"},{5,"problem_detected"},{-1,"fin_wait"},{-2,"linger"},{-3,"dead"}}};
        for(const auto& [state,name]:states)check(TransportStateName(state)==name);
        check(std::string_view(TransportStateName(999))=="unknown");
    }
    {
        const std::vector<std::pair<uint64_t,int>> hostRoster{{111,1},{222,2},{333,3}};
        auto guestRoster=hostRoster;std::reverse(guestRoster.begin(),guestRoster.end());
        const auto host=SessionContext("maps/itemtest.bsp",hostRoster);
        check(host!=0);check(host==SessionContext("maps/itemtest.bsp",guestRoster));
        check(host!=SessionContext("maps/ctf_2fort.bsp",hostRoster));
        guestRoster.pop_back();check(host!=SessionContext("maps/itemtest.bsp",guestRoster));
        guestRoster=hostRoster;guestRoster[1].second=5;check(host!=SessionContext("maps/itemtest.bsp",guestRoster));
        guestRoster=hostRoster;guestRoster[1].first=444;check(host!=SessionContext("maps/itemtest.bsp",guestRoster));
        check(!SessionContext("",hostRoster));check(!SessionContext("maps/itemtest.bsp",{}));
        check(!SessionContext("maps/itemtest.bsp",{{111,1},{111,2}}));
        check(!SessionContext("maps/itemtest.bsp",{{0,1}}));check(!SessionContext("maps/itemtest.bsp",{{111,0}}));
        check(!SessionContext(std::string(513,'a'),hostRoster));
        Peer hostPeer,guestPeer;hostPeer.local=11;guestPeer.local=22;
        Message hello;hello.server=host;hello.nonce=11;hello.flags=3;
        auto ack=guestPeer.Accept(*Decode(Encode(hello)),SessionContext("maps/itemtest.bsp",std::vector<std::pair<uint64_t,int>>{{333,3},{111,1},{222,2}}),1,3);
        check(ack.has_value());auto reply=hostPeer.Accept(*ack,host,2,3);check(reply&&hostPeer.ready);
        check(guestPeer.Accept(*reply,host,3,3).has_value()&&guestPeer.ready);
        Message style;style.type=Style;style.server=host;style.nonce=11;style.echo=22;
        check(guestPeer.CanReceive(style,host,4));
        check(!guestPeer.CanReceive(style,SessionContext("maps/itemtest.bsp",{{111,1},{222,5},{333,3}}),4));
        Peer rejoined;rejoined.local=99;check(!rejoined.Accept(*ack,host,4,3));check(!rejoined.CanReceive(style,host,4));
    }
    for(int bits=0;bits<64;++bits)
    {
        const bool cosmetics=bits&1,networking=bits&2,share=bits&4,receive=bits&8,inGame=bits&16,unloading=bits&32;
        check(NetworkAllowed(cosmetics,networking,share,receive,inGame,unloading)==(cosmetics&&networking&&(share||receive)&&inGame&&!unloading));
        check((std::string_view(NetworkWaitReason(cosmetics,networking,share,receive,inGame,unloading))=="active")==NetworkAllowed(cosmetics,networking,share,receive,inGame,unloading));
    }
    {
        Peer p;p.local=22;p.remote=11;p.flags=3;p.ready=true;p.lastSeen=1;
        Message m;m.type=Style;m.server=123;m.nonce=11;m.echo=22;
        auto reason=[&](const char* expected,double now=2){check(std::string_view(RejectReason(p,m,123,now))==expected);};
        reason("none");reason("peer_timed_out",31);reason("invalid_time",0);
        m.server=321;reason("server_context_mismatch");m.server=123;
        p.ready=false;reason("handshake_not_ready");p.ready=true;
        m.nonce=33;reason("remote_nonce_mismatch");m.nonce=11;
        m.echo=33;reason("nonce_echo_mismatch");m.echo=22;
        p.flags=2;reason("sender_sharing_disabled");m.type=Withdraw;reason("none");
        m.type=Ack;reason("none");m.echo=0;reason("nonce_echo_mismatch");
        m.type=Hello;reason("none");p.local=0;reason("missing_nonce");
    }
    check(!PacketSizeAllowed(0));check(!PacketSizeAllowed(37));check(PacketSizeAllowed(38));check(PacketSizeAllowed(53));check(PacketSizeAllowed(55));
    {
        Message appearance;appearance.type=PlayerAppearance;appearance.server=123;appearance.nonce=11;appearance.echo=22;appearance.flags=3;
        for(int cls=1;cls<=9;++cls)for(bool authentic:{false,true})for(bool pip:{false,true})
        {
            appearance.cls=cls;appearance.authenticAnimations=authentic;appearance.pipBoy=pip;
            auto bytes=Encode(appearance);check(bytes.size()==39&&bytes[8]==1);
            auto decoded=Decode(bytes);check(bool(decoded)==(!pip||cls==9));
            if(decoded){check(decoded->pipBoy==pip);check(decoded->authenticAnimations==authentic);}
        }
        appearance.cls=9;appearance.pipBoy=true;
        auto bytes=Encode(appearance);auto bad=bytes;bad[38]=4;check(!Decode(bad));
        bad=bytes;bad.pop_back();check(!Decode(bad));bad=bytes;bad.push_back(0);check(!Decode(bad));
        bad=bytes;bad[37]=0;check(!Decode(bad));bad=bytes;bad[37]=10;check(!Decode(bad));
        bad=bytes;bad[35]=1;check(!Decode(bad));bad=bytes;bad[8]=2;check(!Decode(bad));
        Peer receiver;receiver.local=22;receiver.remote=11;receiver.ready=true;receiver.flags=1;receiver.lastSeen=1;
        check(receiver.CanReceive(appearance,123,2));check(!receiver.CanReceive(appearance,999,2));
        appearance.echo=23;check(!receiver.CanReceive(appearance,123,2));appearance.echo=22;
        receiver.ready=false;check(!receiver.CanReceive(appearance,123,2));receiver.ready=true;
        receiver.flags=0;check(!receiver.CanReceive(appearance,123,2));check(std::string_view(RejectReason(receiver,appearance,123,2))=="sender_sharing_disabled");receiver.flags=1;
        check(!receiver.CanReceive(appearance,123,32));
        check(receiver.AppearanceDue(bytes,2));receiver.SentAppearance(bytes,2);
        check(!receiver.AppearanceDue(bytes,2.1));check(!receiver.AppearanceDue(bytes,4.99));check(receiver.AppearanceDue(bytes,5));
        auto style=bytes;style[38]^=2;check(receiver.AppearanceDue(style,2.21));
        receiver.SentStyle(bytes,2);check(receiver.lastAppearance==bytes);check(receiver.lastAppearanceSend==2);
        check(receiver.Expire(31));check(receiver.lastAppearance.empty());
        for(int bits=0;bits<16;++bits)for(int cls=0;cls<=10;++cls)
        {
            check(PlayerAppearanceAllowed(bits&1,bits&2,bits&4,bits&8,9,cls,2,1)==(bits==15&&cls==9));
        }
        check(!PlayerAppearanceAllowed(true,true,true,true,9,9,.5,1));
        check(!PlayerAppearanceAllowed(true,true,true,true,9,9,11.01,1));
        check(PlayerAppearanceAllowed(true,true,true,true,9,9,11,1));
        check(!PlayerAppearanceAllowed(true,true,true,true,9,9,std::numeric_limits<double>::quiet_NaN(),1));
    }
    check(PacketSizeAllowed(4096));check(!PacketSizeAllowed(4097));check(!PacketSizeAllowed(size_t(-1)));
    {
        std::vector<uint8_t> large(4097);check(!Decode(large));
        Message unusual;unusual.type=Style;unusual.flags=3;unusual.server=123;unusual.nonce=111;unusual.echo=222;
        unusual.weapon=18;unusual.cls=3;unusual.selection.enabled=true;unusual.selection.unusual=701;
        auto payload=Encode(unusual);check(payload.size()==55&&PacketSizeAllowed(payload.size()));
        check(Decode(payload)&&Decode(payload)->selection.unusual==701);
        for(size_t size=0;size<payload.size();++size)check(!Decode({payload.data(),size}));
        payload.push_back(0);check(!Decode(payload));
    }
    for(int type=Hello;type<=Withdraw;++type)
    {
        Message message;message.type=type;message.flags=3;message.server=123;message.nonce=111;message.echo=222;message.weapon=14;message.cls=2;message.selection.enabled=true;message.selection.reskin=851;
        auto bytes=Encode(message);check(bytes.size()==(type==Style?53:38));auto read=Decode(bytes);check(read && read->type==type && read->weapon==14);
        for(size_t length=0;length<bytes.size();++length)check(!Decode({bytes.data(),length}));
        for(size_t n=0;n<9;++n){auto bad=bytes;bad[n]^=0xff;check(!Decode(bad));}
        auto bad=bytes;bad.push_back(0);check(!Decode(bad));bad=bytes;bad[10]=255;check(!Decode(bad));
    }
    Message invalid;invalid.type=Style;invalid.server=123;invalid.nonce=111;invalid.cls=2;invalid.selection.tier=9;check(!Decode(Encode(invalid)));
    Peer alice,bob;alice.local=111;bob.local=222;
    Message hello;hello.server=123;hello.nonce=111;hello.flags=3;
    auto reply=bob.Accept(hello,123,1,3);check(reply && reply->type==Ack && reply->echo==111);
    auto confirm=alice.Accept(*reply,123,2,3);check(confirm && alice.ready);
    auto last=bob.Accept(*confirm,123,3,3);check(last && bob.ready);check(!alice.Accept(*last,123,4,3));
    Message style;style.type=Style;style.server=123;style.nonce=111;style.echo=222;check(bob.CanReceive(style,123,4));
    check(!bob.CanReceive(style,999,4));style.nonce=999;check(!bob.CanReceive(style,123,4));style.nonce=111;style.echo=999;check(!bob.CanReceive(style,123,4));style.echo=222;
    check(!bob.CanReceive(style,123,100));check(!bob.CanReceive(style,123,std::numeric_limits<double>::quiet_NaN()));
    Peer unknown;unknown.local=333;check(!unknown.CanReceive(style,123,4));check(!unknown.Accept(hello,999,1,3));
    Message wrongAck=*reply;wrongAck.echo=444;check(!unknown.Accept(wrongAck,123,1,3));
    hello.flags=0;check(bob.Accept(hello,123,5,3).has_value());check(!bob.CanReceive(style,123,6));style.type=Withdraw;check(bob.CanReceive(style,123,6));
    {
        Peer quiet;quiet.local=555;
        check(quiet.DiscoveryDue(0));quiet.lastHello=0;
        check(!quiet.DiscoveryDue(9.99));check(quiet.DiscoveryDue(10));
        check(!quiet.DiscoveryDue(std::numeric_limits<double>::quiet_NaN()));
        Message update;update.type=Style;update.flags=3;update.server=123;update.nonce=555;update.echo=777;update.cls=3;update.weapon=18;update.selection.enabled=true;
        auto payload=Encode(update);check(!quiet.StyleDue(payload,0));
        check(!quiet.CanReceive(update,123,1)); // No reply: no styles accepted or sent.
        quiet.remote=777;quiet.flags=3;quiet.ready=true;quiet.lastSeen=0;
        check(quiet.StyleDue(payload,0));quiet.SentStyle(payload,0);
        check(!quiet.StyleDue(payload,.1));check(!quiet.StyleDue(payload,2.99));check(quiet.StyleDue(payload,3));
        update.selection.unusual=701;payload=Encode(update);check(payload.size()==55);
        check(!quiet.StyleDue(payload,.1));check(quiet.StyleDue(payload,.21));quiet.SentStyle(payload,.21);
        check(!quiet.StyleDue(payload,.5));check(quiet.StyleDue(payload,3.22));
        update.selection.reskin=205;payload=Encode(update);check(quiet.StyleDue(payload,.5));quiet.SentStyle(payload,.5);
        update.type=Withdraw;payload=Encode(update);check(quiet.StyleDue(payload,.71));quiet.SentStyle(payload,.71);
        check(!quiet.StyleDue(payload,1));
        check(!quiet.Expire(-1));check(!quiet.Expire(std::numeric_limits<double>::quiet_NaN()));check(!quiet.Expire(29.99));check(quiet.Expire(30));
        check(!quiet.ready&&quiet.remote==0&&quiet.flags==0&&quiet.lastPayload.empty());check(!quiet.StyleDue(payload,30));
        quiet.local=888;
        Message staleAck;staleAck.type=Ack;staleAck.server=123;staleAck.nonce=777;staleAck.echo=555;staleAck.flags=3;
        check(!quiet.Accept(staleAck,123,31,3));check(!quiet.ready);
    }
    {
        // Exchange actual serialized discovery packets, not just hand-built
        // structs, then transmit Unusual + all existing cosmetic selections.
        Peer sender,receiver;sender.local=1001;receiver.local=1002;
        Message discovery;discovery.flags=3;discovery.server=2222;discovery.nonce=sender.local;
        auto response=receiver.Accept(*Decode(Encode(discovery)),2222,0,3);check(response.has_value());
        auto confirmation=sender.Accept(*Decode(Encode(*response)),2222,.1,3);check(confirmation&&sender.ready);
        auto finalAck=receiver.Accept(*Decode(Encode(*confirmation)),2222,.2,3);check(finalAck&&receiver.ready);
        Message appearance;appearance.type=Style;appearance.flags=3;appearance.server=2222;appearance.nonce=1001;appearance.echo=1002;
        appearance.cls=2;appearance.weapon=14;appearance.selection.enabled=true;appearance.selection.reskin=851;
        appearance.selection.tier=3;appearance.selection.sheen=7;appearance.selection.effect=2008;appearance.selection.unusual=701;
        appearance.selection.preview=true;appearance.selection.previewCount=99;
        auto received=Decode(Encode(appearance));check(received&&receiver.CanReceive(*received,2222,.3));
        check(received->selection.reskin==851&&received->selection.unusual==701&&received->selection.tier==3);
        check(!received->selection.preview&&received->selection.previewCount==5);
        auto otherServer=*received;otherServer.server=3333;check(!receiver.CanReceive(otherServer,2222,.3));
        auto wrongSession=*received;wrongSession.echo=3333;check(!receiver.CanReceive(wrongSession,2222,.3));
        Message newHello=discovery;newHello.nonce=1003;
        check(receiver.Accept(newHello,2222,1,3).has_value());check(!receiver.ready);check(!receiver.CanReceive(*received,2222,1));
    }
    if(argc>1)
    {
        std::ifstream file(argv[1],std::ios::binary);std::string schema((std::istreambuf_iterator<char>(file)),{});Catalog actual;
        check(file.good()||file.eof());check(actual.Load(schema));check(actual.items.size()>100);
        check(actual.paints.size()>100);check(actual.paints.contains(0));check(actual.paints.contains(390));
        check(actual.paints.at(390).token=="#9_390_field { field_number: 2 }");
        check(!actual.PaintWeapons(*actual.Find(18),3).empty());
        check(!actual.PaintWeapons(*actual.Find(14),2).empty());
        check(actual.PaintWeapons(*actual.Find(851),2).empty());
        check(actual.PaintWeapons(*actual.Find(30666),1).empty());
        for(const auto& [id,item]:actual.items)for(int cls=1;cls<=9;++cls)
            for(int recipe:actual.PaintWeapons(item,cls))
            {
                auto candidate=actual.Find(recipe);check(candidate&&candidate->family==item.family);
                check(candidate&&candidate->Supports(cls)&&candidate->Model(cls)==item.Model(cls)&&candidate->ViewModel(cls)==item.ViewModel(cls));
            }
        std::cout<<"Paint catalog: "<<actual.paints.size()<<" native kit IDs; exact-model/class compatibility exercised.\n";
        check(actual.Find(14)&&actual.Find(851)&&actual.Find(201));
        check(actual.CanReskin(*actual.Find(13),*actual.Find(772),1)); // Baby Face on real stock Scattergun.
        check(actual.CanReskin(*actual.Find(14),*actual.Find(526),2)); // Machina on real stock Sniper Rifle.
        const auto knife=actual.Find(4),sapper=actual.Find(735);check(knife&&sapper);
        for(const auto& [id,item]:actual.items)
        {
            if(item.Supports(8)&&item.slot=="melee")check(actual.CanReskin(*knife,item,8));
            if(item.MultiClass()&&item.slot=="melee"&&id!=357)check(item.Supports(8)&&!item.Model(8).empty());
            if(item.weaponType=="tf_weapon_knife")check(actual.CanReskin(*knife,item,8));
            if(item.weaponType=="tf_weapon_sapper")check(actual.CanReskin(*sapper,item,8));
            check(item.killIcon.empty()||SkinModel::SafeEffectName(item.killIcon));
            for(int cls=1;cls<=9;++cls)if(item.Supports(cls))for(const auto& [baseID,base]:actual.items)
                if(base.Supports(cls)&&base.slot==item.slot)check(actual.CanReskin(base,item,cls)==(!base.Watch()&&!item.Watch()));
        }
        for(int id:{810,933,1080,1102})
        {
            auto item=actual.Find(id);check(item&&actual.CanReskin(*sapper,*item,8));
            const auto variants=actual.Variants(735,8);check(std::any_of(variants.begin(),variants.end(),[&](auto value){return value->id==id;}));
        }
        check(actual.Find(933)->sapperVoice==1&&actual.Find(933)->sapperVoiceWait==3);
        check(actual.Find(1102)->WorldModel(8)=="models/weapons/c_models/c_breadmonster_sapper/c_breadmonster_sapper.mdl");
        check(actual.Find(1102)->PlacedSapper(false)=="models/buildables/breadmonster_sapper_placed.mdl");
        check(actual.Find(1102)->PlacedSapper(true)=="models/buildables/breadmonster_sapper_placement.mdl");
        check(actual.Find(1102)->SapperTimer()=="Weapon_breadmonster_sapper.Timer");
        check(actual.Find(735)->MatchesModel("models/weapons/w_models/w_sapper.mdl",8));
        check(actual.Find(1080)->PlacedSapper(false)=="models/buildables/sapper_xmas_placed.mdl");
        check(actual.Find(933)->PlacedSapper(false)=="models/buildables/p2rec_placed.mdl");
        check(actual.Find(810)->PlacedSapper(false)=="models/buildables/sd_sapper_placed.mdl");
        auto watch=actual.Find(30);check(watch&&watch->Watch());
        for(const auto& [id,item]:actual.items)if(item.Watch())check(!actual.CanReskin(*watch,item,8));
        check(actual.Find(59)&&!actual.CanReskin(*watch,*actual.Find(59),8));
        check(actual.Variants(30,8).empty());
        check(actual.Find(441)->animations.at("ACT_VM_RELOAD")=="ACT_PRIMARY_VM_RELOAD_3");
        auto originalMap=SkinRender::HandActivityMap("PRIMARY","PRIMARY",actual.Find(18)->Animations(2),actual.Find(513)->Animations(2));
        check(originalMap.at("ACT_RELOAD_START")=="ACT_VM_RELOAD_START_QRL");
        check(originalMap.at("ACT_RELOAD_FINISH")=="ACT_VM_RELOAD_FINISH_QRL");
        check(!actual.Find(357)->Supports(8));check(!actual.CanReskin(*knife,*actual.Find(357),8));
        check(actual.Find(1102)->animations.at("ACT_VM_DRAW")=="ACT_BREADSAPPER_VM_DRAW");
        check(actual.Find(1102)->killIcon=="snack_attack");check(actual.Find(526)->killIcon=="machina");
        {
            const auto map=SkinRender::PlayerActivityMap("MELEE","MELEE_ALLCLASS",knife->Animations(2),actual.Find(264)->Animations(2));
            check(map.at("ACT_MP_STAND_MELEE")=="ACT_MP_STAND_MELEE_ALLCLASS");
            check(map.at("ACT_MP_ATTACK_STAND_MELEE")=="ACT_MP_ATTACK_STAND_MELEE_ALLCLASS");
            const auto same=SkinRender::PlayerActivityMap("PRIMARY","PRIMARY",std::map<std::string,std::string>{},std::map<std::string,std::string>{});check(same.empty());
        }
        check(actual.Find(18)->CanFestivize());check(actual.Find(14)->CanFestivize());
        check(actual.Find(658)->festive);check(actual.Find(664)->festive);check(actual.Find(665)->festive);
        check(!actual.Find(658)->CanFestivize());check(!actual.Find(795)->CanFestivize());
        check(CosmeticAttachments(*actual.Find(18),*actual.Find(658),2,true,false)[0].model=="models/weapons/c_models/c_rocketlauncher/c_rocketlauncher_xmas.mdl");
        check(CosmeticAttachments(*actual.Find(14),*actual.Find(664),3,true,false)[0].model=="models/weapons/c_models/c_sniperrifle/c_sniperrifle_xmas.mdl");
        check(CosmeticAttachments(*actual.Find(18),*actual.Find(18),2,true,true)[0].model=="models/weapons/c_models/c_rocketlauncher/c_rocketlauncher_festivizer.mdl");
        int classicCount=0,festivizerCount=0;
        for(const auto& [id,item]:actual.items)
        {
            if(item.festive)++classicCount;if(item.CanFestivize())++festivizerCount;
            for(int team:{0,2,3})for(bool view:{false,true})
            {
                auto attachments=CosmeticAttachments(*actual.Find(actual.Canonical(id)),item,team,view,true);
                check(attachments.size()<=MaxCosmeticAttachments);
                for(const auto& a:attachments){check(a.model.starts_with("models/")&&a.model.ends_with(".mdl")&&a.model.find("..") == std::string::npos);check(a.skin>=-1&&a.skin<=255);}
            }
        }
        std::cout<<"Holiday catalog: "<<classicCount<<" classic Festive definitions, "<<festivizerCount<<" compatible Festivizer definitions.\n";
        check(actual.Canonical(851)==actual.Canonical(14));check(actual.Find(201)->Gold());check(!actual.Find(851)->Gold());
        check(actual.Find(22)&&actual.Find(23)&&actual.Find(160));
        const auto pan=actual.Find(264),goldPan=actual.Find(1071),goldWrench=actual.Find(169),direct=actual.Find(127);
        check(pan&&goldPan&&goldWrench&&direct);
        check(pan->Gold()&&goldPan->golden&&goldPan->defaultRed==2&&goldPan->defaultBlue==3);
        check(goldWrench->golden&&goldWrench->materialOverride.empty()&&goldWrench->Gold());
        check(goldWrench->defaultRed==goldWrench->goldRed&&goldWrench->defaultBlue==goldWrench->goldBlue);
        bool nativeWrenchMatch=false;for(const auto& [id,item]:actual.items)if(id!=169&&item.family==goldWrench->family&&item.Gold())
            nativeWrenchMatch|=item.Model(9)==goldWrench->Model(9)&&item.ViewModel(9)==goldWrench->ViewModel(9)&&item.goldRed==goldWrench->goldRed&&item.goldBlue==goldWrench->goldBlue;
        check(nativeWrenchMatch);
        const auto saxxy=actual.Find(423);check(saxxy&&saxxy->deathEffects==SkinModel::DeathGold);
        check(goldPan->deathEffects==SkinModel::DeathGold&&goldWrench->deathEffects==SkinModel::DeathGold);
        check(pan->Animations(2).at("ACT_VM_DRAW")=="ACT_MELEE_ALLCLASS_VM_DRAW");
        check(actual.Canonical(169)==actual.Canonical(7));
        check(actual.Canonical(264)==actual.Canonical(1071));
        for(int cls=1;cls<=9;++cls)
        {
            check(pan->Supports(cls)&&goldPan->Supports(cls));
            // Stock Heavy fists have no separate model in TF2's schema;
            // exercise his separately modeled melee weapon instead.
            int melee=cls==1?0:cls==2?3:cls==3?6:cls==4?1:cls==5?8:cls==6?239:cls==7?2:cls==8?4:7;
            auto base=actual.Find(melee);
            check(base&&actual.CanReskin(*base,*pan,cls)&&actual.CanReskin(*base,*goldPan,cls));
            const auto choices=actual.Variants(melee,cls);
            check(std::any_of(choices.begin(),choices.end(),[](const Item* item){return item->id==264;}));
        }
        check(!actual.CanReskin(*actual.Find(14),*pan,2));
        check(!actual.CanReskin(*actual.Find(4),*actual.Find(18),8));
        auto fallback=actual.CosmeticPaintWeapons(*direct,3);
        check(!fallback.empty()&&fallback.front()==127);
        check(std::find(fallback.begin(),fallback.end(),18)!=fallback.end());
        for(const auto& [id,item]:actual.items)for(int cls=1;cls<=9;++cls)if(item.Supports(cls))
        {
            const auto candidates=actual.CosmeticPaintWeapons(item,cls);
            check(!candidates.empty()&&std::find(candidates.begin(),candidates.end(),id)!=candidates.end());
            check(std::set<int>(candidates.begin(),candidates.end()).size()==candidates.size());
        }
        check(actual.Canonical(22)==actual.Canonical(23));check(actual.Canonical(160)==actual.Canonical(22));
        check(actual.Find(851)->Effects(2).Sound(1)=="Weapon_AWP.Single");check(actual.Find(851)->Effects(3).Sound(5)=="Weapon_AWP.SingleCrit");
        auto capper=actual.Find(30666);check(capper!=nullptr);check(actual.Canonical(30666)==actual.Canonical(22));
        check(capper->Effects(2).Sound(1)=="Weapon_Capper.Single");check(capper->Effects(3).Sound(5)=="Weapon_Capper.SingleCrit");
        check(capper->Effects(2).muzzle=="muzzle_raygun_red");check(capper->Effects(3).muzzle=="muzzle_raygun_blue");
        check(capper->Effects(2).Tracer(2,false)=="bullet_tracer_raygun_red");check(capper->Effects(3).Tracer(3,true)=="bullet_tracer_raygun_blue_crit");
        int botkillers=0;std::map<int,int> botFamilies;
        for(const auto& [id,item]:actual.items)if(item.botkiller)
        {++botkillers;check(!item.ExtraModel(true).empty()&&!item.ExtraModel(false).empty());check(item.ExtraSkin(2)>=0&&item.ExtraSkin(2)<8&&item.ExtraSkin(3)>=0&&item.ExtraSkin(3)<8);++botFamilies[actual.Canonical(id)];}
        check(botkillers==72);check(botFamilies.size()==9);
        for(int stock:{4,7,13,14,15,18,20,21,29})check(botFamilies[actual.Canonical(stock)]==8);
        check(actual.Find(795)->ExtraSkin(2)==0&&actual.Find(795)->ExtraSkin(3)==1);check(actual.Find(804)->ExtraSkin(2)==2&&actual.Find(804)->ExtraSkin(3)==3);
        auto sharp=actual.Find(638);check(sharp!=nullptr);check(actual.Canonical(638)==actual.Canonical(4));
        check(sharp->animations.size()==12);check(sharp->animations.at("ACT_VM_DRAW")=="ACT_ITEM1_VM_DRAW");
        check(sharp->animations.at("ACT_BACKSTAB_VM_UP")=="ACT_ITEM1_BACKSTAB_VM_UP");
        check(sharp->animations.at("ACT_MELEE_VM_INSPECT_START")=="ACT_ITEM1_VM_INSPECT_START");
        check(sharp->ViewModel(8)=="models/workshop/weapons/c_models/c_acr_hookblade/c_acr_hookblade.mdl");
        int animationItems=0;
        for(const auto& [id,item]:actual.items)
        {
            check(item.classes!=0);check(!item.family.empty());check(actual.Find(actual.Canonical(id))!=nullptr);
            if(!item.animations.empty())++animationItems;
            for(int team:{0,2,3})for(const auto& [from,to]:item.Animations(team))
            {
                check(from.starts_with("ACT_")&&to.starts_with("ACT_")&&from.size()<128&&to.size()<128);
                auto lookup=[&](const std::string& name){return name==from?100:200;};
                check(SkinRender::CosmeticHandActivity(item.Animations(team),100,lookup)==(from==to?100:200));
            }
        }
        std::cout<<"Animation profiles: "<<animationItems<<" catalog items; all RED/BLU/base replacement rules exercised.\n";
        std::cout<<"Installed catalog: "<<actual.items.size()<<" weapon definitions; stock/AWPer/Australium compatibility verified.\n";
    }
    std::cout<<"PASS: "<<checks<<" skin catalog, class selection, preset name, bounds, handshake and render bone-policy checks\n";
}
