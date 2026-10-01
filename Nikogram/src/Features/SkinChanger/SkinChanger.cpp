#include "SkinChanger.h"
#include "../../Core/Core.h"
#include "Protocol.h"
#include "RenderPolicy.h"
#include "JigglePhysics.h"
#include "PresetUiPolicy.h"
#include <shellapi.h>
#include "../../SDK/Definitions/Main/ITextureCompositor.h"
#include "../Visuals/Chams/Chams.h"
#include "../Visuals/Glow/Glow.h"
#include "../Visuals/Materials/Materials.h"
#include "../Configs/Configs.h"
#include "../ImGui/Menu/Components.h"
#include <boost/property_tree/json_parser.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <atomic>

// Verified against the installed x64 client's WeaponSkin proxy. Definition
// objects and recipe KeyValues remain engine-owned and opaque.
MAKE_SIGNATURE(SkinPaint_ProtoConstruct, "client.dll", "40 53 48 83 EC 20 48 8D 05 ? ? ? ? 48 8B D9 48 89 01 48 83 C1 08 E8 ? ? ? ? 33 C9 33 C0 48 89 4B 14 89 4B 1C 89 43 10", 0x0);
MAKE_SIGNATURE(SkinPaint_ProtoDestroy, "client.dll", "40 53 48 83 EC 20 48 8D 05 ? ? ? ? 48 8B D9 48 89 01 8B 41 28 85 C0 74 37 83 C0 FD", 0x0);
MAKE_SIGNATURE(SkinPaint_ProtoManager, "client.dll", "48 83 EC 28 48 8B 05 ? ? ? ? 48 85 C0 0F 85 ? ? ? ? 48 89 74 24 40 B9 78 02 00 00", 0x0);
MAKE_SIGNATURE(SkinPaint_ProtoLookup, "client.dll", "40 53 48 83 EC 20 48 63 42 1C 4C 8D 04 80 8B 42 18 4A 8D 1C C1", 0x0);
MAKE_SIGNATURE(SkinPaint_WeaponSupport, "client.dll", "48 89 5C 24 08 57 48 83 EC 20 0F B7 FA 48 8B D9 E8 ? ? ? ? 44 8B 83 E0 03 00 00", 0x0);
MAKE_SIGNATURE(SkinPaint_Recipe, "client.dll", "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 56 41 57 48 81 EC 90 00 00 00 0F B7 99 C0 03 00 00", 0x0);
// Installed x64 ReadEncryptedKVFile: filesystem, extensionless path, TF key,
// force-encrypted flag. Reads cosmetics once, never changes a weapon's info.
MAKE_SIGNATURE(Skin_ReadWeaponScript, "client.dll", "48 89 5C 24 10 48 89 6C 24 18 56 57 41 55 41 56 41 57 48 81 EC 30 02 00 00 4D 85 C0", 0x0);

namespace SkinChanger
{
    namespace
    {
        namespace V=Vars::Misc::SkinChanger;
        using namespace SkinModel;
        std::recursive_mutex guard;
        std::atomic<bool> unloadRequested=false,unloadComplete=false;
        Catalog catalog;Preset preset;bool loaded=false,dirty=false,stopped=false;
        std::string activePreset;bool presetModified=false;
        std::map<std::string,std::set<std::string>> soundWaves;
        std::map<std::string,CosmeticEffects> weaponScriptEffects;
        std::string status="Waiting for the item catalog.",renderStatus="No cosmetic draw observed yet.";
        double lastTick=0,lastSave=0,lastProbe=0;
        ISteamNetworkingMessages* transport=nullptr;
        struct TransportObservation {int state=-999,end=-1;double poll=-100,report=-100;};
        std::map<uint64_t,TransportObservation> transportObservations;
        uint64_t server=0;
        double contextSince=0;
        int networkFlags=-1;
        std::string networkStatus="Disabled; no cosmetic network traffic.";
        std::map<uint64_t,SkinProtocol::Peer> peers;
        struct Remote {int weapon=0,cls=0;Selection selection;double time=0;SkinRender::SharedStreak streak;
            SkinRender::WeaponProfiles<Selection> killstreakWeapons;SkinRender::MilestoneSoundGate milestone;};
        std::map<uint64_t,Remote> remote;
        struct SharedPlayerAppearance {int cls=0;bool pipBoy=false,authenticAnimations=true;double time=0;};
        std::map<uint64_t,SharedPlayerAppearance> sharedPlayers;
        void ForgetRemote(uint64_t id){remote.erase(id);sharedPlayers.erase(id);}
        struct Model {int index=-1;const model_t* model=nullptr;bool owned=false;};
        std::map<std::string,Model> models;
        using PaintKey=std::tuple<int,int,int,int,int,int,int>; // kit, recipe weapon, wear, seed, team, width, height
        struct PaintedTexture {ITextureCompositor* compositor=nullptr;double seen=0,created=0;int state=-1;bool failed=false;};
        std::map<PaintKey,PaintedTexture> paintTextures;
        using PaintSurfaceKey=std::tuple<uint32_t,uintptr_t,uintptr_t,int,int,int>;
        struct PaintSurface {SkinRender::PaintRequestGate request;SkinRender::RetainedPaint<ITexture> last;double seen=0;};
        std::map<PaintSurfaceKey,PaintSurface> paintSurfaces;
        std::map<std::string,bool> paintMaterials;
        std::map<std::pair<int,int>,int> paintCompatibility; // kit, chosen definition/class -> recipe definition
        thread_local PaintDrawScope* activePaint=nullptr;
        thread_local const KillstreakProfile* activeKillstreak=nullptr;
        SkinRender::LifeStreaks lifeStreaks;
        struct GoldDeath {uint32_t player=0;double expires=0;bool shared=false;int effects=0;uint64_t peer=0;Vec3 origin;};
        std::map<int,GoldDeath> goldDeaths;
        struct Corpse {GoldDeath death;double seen=0;bool native=false;SkinRender::DeathSoundOnce sound;Vec3 origin;};
        IMaterial* statueGoldMaterial=nullptr;
        std::map<uint32_t,Corpse> corpses;
        std::map<uint32_t,double> ragdollBirths;
        struct CorpsePose {SkinRender::FrozenPose pose;double seen=0;};
        using CorpsePoseKey=std::tuple<uint32_t,uint32_t,uintptr_t>;
        std::map<CorpsePoseKey,CorpsePose> corpsePoses;
        uint32_t localLife=1;
        using JiggleKey=std::tuple<uint32_t,uint32_t,uintptr_t,uintptr_t,bool>;
        struct JiggleInstance {std::array<SkinJiggle::State,MAXSTUDIOBONES> bones{};double seen=0;};
        std::map<JiggleKey,JiggleInstance> jiggleInstances;
        std::set<std::string> logged;
        const int64_t diagnosticRun=GetTickCount64();
        bool diagnosticStarted=false;
        struct NetworkDiagnostics
        {
            std::string state,lastState;
            double report=0;
            uint64_t sent=0,failed=0,received=0,ready=0,styles=0;
            std::map<std::string,uint64_t> counts;
            std::map<uint64_t,unsigned> aliases;
            unsigned nextAlias=0;
        } netLog;
        struct Telemetry
        {
            uint64_t viewmodels=0,verified=0,draws=0,matches=0,skin=0,model=0,skipped=0;
            uint64_t sounds=0,tracers=0,muzzles=0;
            uint64_t accessories=0;
            int lastItem=0;std::string result="none",path="none";
        } telemetry;
        double lastReport=0;
        std::mt19937_64 random{std::random_device{}()};
        double Now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
        uint64_t Nonce(){uint64_t n=0;while(!n)n=random();return n;}
        std::filesystem::path Directory(){return std::filesystem::path(F::Configs.m_sCorePath)/"SkinChanger";}
        void WriteDiagnostic(const std::string& message)
        {
            try{auto dir=std::filesystem::path(F::Configs.m_sCorePath)/"Logs";std::filesystem::create_directories(dir);
                auto file=dir/"SkinChanger-diagnostics.log";auto previous=dir/"SkinChanger-diagnostics.log.1";
                if(std::filesystem::exists(file)&&std::filesystem::file_size(file)>1024*1024)
                {if(std::filesystem::exists(previous))std::filesystem::remove(previous);std::filesystem::rename(file,previous);}
                SYSTEMTIME utc{};GetSystemTime(&utc);
                std::ofstream(file,std::ios::app)<<"[skinchanger-v121] run="<<diagnosticRun<<" utc="
                    <<std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z",utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute,utc.wSecond)
                    <<" ms="<<int64_t(Now()*1000)<<' '<<message<<'\n';}catch(...){}
        }
        void Diagnostic(const std::string& message)
        {if(logged.size()<1024&&logged.insert(message).second)WriteDiagnostic(message);}
        unsigned PeerAlias(uint64_t id)
        {
            auto [it,inserted]=netLog.aliases.try_emplace(id,0);
            if(inserted)it->second=++netLog.nextAlias;
            return it->second;
        }
        void NetworkEvent(const std::string& reason,uint64_t id=0)
        {
            if(netLog.counts.size()>=512&&!netLog.counts.contains(reason))return;
            auto& count=netLog.counts[reason];
            if(count++==0)WriteDiagnostic(std::format("event=network_detail peer={} reason={} first_in_interval=true",id?PeerAlias(id):0,reason));
        }
        void NetworkReport(double now)
        {
            if(netLog.state==netLog.lastState&&now-netLog.report<10)return;
            WriteDiagnostic(std::format("event=network_status reason={} skin_enabled={} networking={} share={} receive={} in_game={} catalog_loaded={} transport_available={} eligible_peers={} ready_peers={} remote_styles={} sent={} send_failed={} received={} handshake_completed={} styles_applied={} interval_seconds={:.1f} counters=interval privacy=no_player_ids_or_server_addresses",
                netLog.state,V::Enabled.Value,V::Networking.Value,V::Share.Value,V::Receive.Value,I::EngineClient->IsInGame(),loaded,transport!=nullptr,peers.size(),std::count_if(peers.begin(),peers.end(),[](const auto& p){return p.second.ready;}),remote.size(),netLog.sent,netLog.failed,netLog.received,netLog.ready,netLog.styles,netLog.report?now-netLog.report:0.));
            for(const auto& [reason,count]:netLog.counts)WriteDiagnostic(std::format("event=network_counter reason={} count={}",reason,count));
            netLog.sent=netLog.failed=netLog.received=netLog.ready=netLog.styles=0;netLog.counts.clear();netLog.lastState=netLog.state;netLog.report=now;
        }
        void ClearPaintTextures()
        {
            for(auto& [key,entry]:paintTextures)if(entry.compositor)entry.compositor->Release();
            paintSurfaces.clear();paintTextures.clear();paintMaterials.clear();paintCompatibility.clear();
        }
        void* PaintDefinition(int kit)
        {
            if(kit<0||!catalog.paints.contains(kit)||!S::SkinPaint_ProtoConstruct()||!S::SkinPaint_ProtoDestroy()
                ||!S::SkinPaint_ProtoManager()||!S::SkinPaint_ProtoLookup())return nullptr;
            // Native constructor/destructor own all protobuf internals. Only the
            // three scalar setters used by the native proxy are mirrored here.
            alignas(16) std::array<std::byte,64> id{};
            S::SkinPaint_ProtoConstruct.Call<void>(id.data());
            const uint32_t fields=3,type=9,index=uint32_t(kit);
            std::memcpy(id.data()+0x10,&fields,4);std::memcpy(id.data()+0x18,&index,4);std::memcpy(id.data()+0x1c,&type,4);
            auto manager=S::SkinPaint_ProtoManager.Call<void*>();
            auto result=manager?S::SkinPaint_ProtoLookup.Call<void*>(manager,id.data()):nullptr;
            S::SkinPaint_ProtoDestroy.Call<void>(id.data());return result;
        }
        int PaintWeapon(const Item& chosen,int cls,int kit)
        {
            const auto key=std::pair{kit,chosen.id*16+cls};
            if(auto it=paintCompatibility.find(key);it!=paintCompatibility.end())return it->second;
            int result=-1;auto candidates=catalog.CosmeticPaintWeapons(chosen,cls);
            if(!candidates.empty()&&S::SkinPaint_WeaponSupport())if(auto definition=PaintDefinition(kit))
                for(int id:candidates)if(S::SkinPaint_WeaponSupport.Call<void*>(definition,uint16_t(id))){result=id;break;}
            const auto authored=catalog.PaintWeapons(chosen,cls);
            if(result>=0&&std::find(authored.begin(),authored.end(),result)==authored.end())
                Diagnostic(std::format("event=paint_layout_fallback item={} kit={} recipe={} native_layout=false",chosen.id,kit,result));
            if(paintCompatibility.size()<4096)paintCompatibility.emplace(key,result);
            return result;
        }
        bool AuthoredPaintMaterial(IMaterial* material)
        {
            if(!material||material->IsErrorMaterial())return false;
            const char* raw=material->GetName();if(!raw)return false;std::string name(raw);
            if(name.size()>512||name.find("..")!=std::string::npos||name.find(':')!=std::string::npos)return false;
            const auto lower=Lower(name);
            if(lower.find("/player/")!=std::string::npos||lower.find("hands")!=std::string::npos||lower.find("arms")!=std::string::npos
                ||lower.find("lens")!=std::string::npos||lower.find("glass")!=std::string::npos)return false;
            if(auto it=paintMaterials.find(name);it!=paintMaterials.end())return it->second;
            auto path="materials/"+name+".vmt";auto file=I::FileSystem->Open(path.c_str(),"rb","GAME");bool supported=false;
            if(file)
            {
                auto size=I::FileSystem->Size(file);std::string data;
                if(size<=128*1024){data.resize(size);if(I::FileSystem->Read(data.data(),int(size),file)!=int(size))data.clear();}
                I::FileSystem->Close(file);
                if(auto parsed=Parser(data).Parse())for(const auto& shader:parsed->children)
                    if(auto proxies=shader.Find("Proxies"))for(const auto& proxy:proxies->children)
                        if(Lower(proxy.name)=="weaponskin")supported=true;
                if(!supported)if(auto parsed=Parser(data).Parse())for(const auto& shader:parsed->children)
                    if(Lower(shader.name)=="vertexlitgeneric"&&!shader.Text("$basetexture").empty())supported=true;
            }
            if(paintMaterials.size()<512)paintMaterials.emplace(name,supported);
            Diagnostic(std::format("event=paint_material name={} accepted_weapon_surface={}",name,supported));return supported;
        }
        ITexture* PaintedSurface(const Item& chosen,int cls,const Selection& selection,int team,ITexture* original,uint32_t handle,const model_t* model)
        {
            const int kit=PaintIndex(selection.finish),weapon=PaintWeapon(chosen,cls,kit);
            if(weapon<0||!original||original->IsError())return nullptr;
            const int width=std::clamp(original->GetActualWidth(),256,1024),height=std::clamp(original->GetActualHeight(),256,1024);
            const PaintKey key{kit,weapon,WearLevel(selection.wear),selection.seed,team,width,height};double now=Now();
            const PaintSurfaceKey surfaceKey{handle,uintptr_t(model),uintptr_t(original),chosen.id,cls,team};
            if(!paintSurfaces.contains(surfaceKey)&&paintSurfaces.size()>=16)
                paintSurfaces.erase(std::min_element(paintSurfaces.begin(),paintSurfaces.end(),[](const auto& a,const auto& b){return a.second.seen<b.second.seen;}));
            auto& surface=paintSurfaces[surfaceKey];surface.seen=now;
            surface.request.Observe(kit,WearLevel(selection.wear),selection.seed,now);
            auto fallback=[&]() -> ITexture*
            {
                if(surface.last.texture)Diagnostic(std::format("event=paint_previous_surface kit={} weapon={} seed={} team={} retained=true",kit,weapon,selection.seed,team));
                return surface.last.texture;
            };
            auto it=paintTextures.find(key);
            if(it==paintTextures.end())
            {
                if(!surface.request.Ready(now))return fallback();
                // Limit native jobs without waiting or driving their updates.
                int pending=0;
                for(auto& [cachedKey,cached]:paintTextures)if(cached.compositor&&!cached.failed)
                {
                    int state=cached.compositor->GetResolveStatus();
                    if(state==ECRS_Error||(state!=ECRS_Complete&&now-cached.created>30))cached.failed=true;
                    else if(state!=ECRS_Complete)++pending;
                }
                if(pending>=4)return fallback();
                // Avoid unbounded GPU resources when a seed slider is dragged.
                if(paintTextures.size()>=16)
                {
                    auto oldest=paintTextures.end();
                    for(auto candidate=paintTextures.begin();candidate!=paintTextures.end();++candidate)
                        if(candidate->second.failed||!candidate->second.compositor||candidate->second.compositor->GetResolveStatus()==ECRS_Complete)
                            if(oldest==paintTextures.end()||candidate->second.seen<oldest->second.seen)oldest=candidate;
                    if(oldest==paintTextures.end())return fallback();
                    if(oldest->second.compositor)oldest->second.compositor->Release();paintTextures.erase(oldest);
                }
                auto definition=PaintDefinition(kit);
                auto recipe=definition&&S::SkinPaint_Recipe()?S::SkinPaint_Recipe.Call<KeyValues*>(definition,uint16_t(weapon),WearLevel(selection.wear)):nullptr;
                const auto name=std::format("nikogram_paint_v87_{}_{}_{}_{}_{}_{}x{}",kit,weapon,WearLevel(selection.wear),selection.seed,team,width,height);
                auto compositor=recipe?I::MaterialSystem->NewTextureCompositor(width,height,name.c_str(),team,uint64_t(selection.seed),recipe):nullptr;
                if(compositor){compositor->AddRef();compositor->ScheduleResolve();}
                it=paintTextures.emplace(key,PaintedTexture{compositor,now,now,-1,!compositor}).first;
                Diagnostic(std::format("event=paint_generate kit={} weapon={} wear_level={} seed={} team={} recipe={} compositor={} dimensions={}x{}",kit,weapon,WearLevel(selection.wear),selection.seed,team,recipe!=nullptr,compositor!=nullptr,width,height));
            }
            auto& entry=it->second;entry.seen=now;if(entry.failed||!entry.compositor)return fallback();
            // Match native WeaponSkin::OnBind: ScheduleResolve once, then poll.
            // The material system owns compositor updates. Running Update from
            // a weapon draw can enter texture loading/render work while model
            // cache/render locks are held and stall the game.
            int state=entry.compositor->GetResolveStatus();
            if(state!=entry.state)
            {entry.state=state;Diagnostic(std::format("event=paint_resolve kit={} weapon={} seed={} team={} state={} elapsed_ms={} update_owner=material_system poll_only=true",kit,weapon,selection.seed,team,state,int((now-entry.created)*1000)));}
            if(state==ECRS_Error||(state!=ECRS_Complete&&now-entry.created>30))
            {entry.failed=true;Diagnostic(std::format("event=paint_failed kit={} weapon={} seed={} state={}",kit,weapon,selection.seed,state));return fallback();}
            auto result=state==ECRS_Complete?entry.compositor->GetResultTexture():nullptr;
            if(result&&!result->IsError()){surface.last.Set(result);return result;}
            return fallback();
        }
        const Item* Appearance(const Item& original,int cls,const Selection& s)
        {
            const Item* chosen=s.reskin?catalog.Find(s.reskin):&original;
            if(!chosen || !catalog.CanReskin(original,*chosen,cls))return nullptr;
            if(s.festive)
            {if(s.reskin)return nullptr;auto variants=catalog.Variants(original.id,cls,true);if(variants.empty())return nullptr;chosen=variants.front();}
            if(s.australium && !chosen->Gold())
            {
                if(s.reskin || s.festive)return nullptr;
                chosen=nullptr;for(auto item:catalog.Variants(original.id,cls))if(item->family==original.family&&item->Gold()){chosen=item;break;}
            }
            return chosen;
        }
        bool Compatible(int weapon,int cls,const Selection& s)
        {
            auto item=catalog.Find(weapon);if(!item||item->Watch()||!s.Valid()||cls<1||cls>9||!item->Supports(cls))return false;
            auto chosen=Appearance(*item,cls,s);
            // A missing/unsupported paint must not disable otherwise working
            // reskins, hand animations or independent weapon Unusual particles.
            return chosen&&(!s.festivized||chosen->CanFestivize());
        }
        uint64_t PlayerID(CTFPlayer* player);
        CTFWeaponBase* EquipmentWeapon(CTFPlayer* player)
        {
            if(!player||!player->IsPlayer()||player->m_iClass()!=8)return nullptr;
            for(auto& handle:player->m_hMyWeapons())if(auto weapon=handle.Get())
                if(auto item=catalog.Find(weapon->m_iItemDefinitionIndex());item&&item->Sapper())return weapon;
            return nullptr;
        }
        bool CosmeticSelection(CTFWeaponBase* weapon,CTFPlayer* owner,Selection& selection)
        {
            if(!weapon||!owner||!owner->IsPlayer())return false;
            const int id=weapon->m_iItemDefinitionIndex(),cls=owner->m_iClass();
            if(owner==H::Entities.GetLocal())selection=Get(preset,catalog.Canonical(id),cls);
            else
            {
                if(!V::Networking.Value||!V::Receive.Value)return false;auto received=remote.find(PlayerID(owner));
                auto peer=peers.find(PlayerID(owner));
                if(received==remote.end()||peer==peers.end()||!peer->second.ready||!(peer->second.flags&1)||Now()-received->second.time>10)return false;
                auto cached=received->second.killstreakWeapons.Find(catalog.Canonical(id),cls);if(!cached)return false;selection=*cached;
            }
            return selection.enabled&&Compatible(id,cls,selection);
        }
        bool Save(const std::filesystem::path& file,const Preset& data=preset)
        {
            try
            {
                boost::property_tree::ptree root,entries;root.put("format","nikogram-skin-presets");root.put("version",1);
                if(Lower(file.filename().string())=="current.json"&&SkinPresetUi::NameAllowed(activePreset))root.put("source_preset",activePreset);
                for(const auto& [key,s]:data)
                {
                    boost::property_tree::ptree row;row.put("weapon",key.first);row.put("class",key.second);
                    row.put("enabled",s.enabled);row.put("reskin",s.reskin);row.put("australium",s.australium);row.put("festive",s.festive);row.put("festivized",s.festivized);
                    row.put("finish",s.finish);row.put("wear",s.wear);row.put("seed",s.seed);row.put("tier",s.tier);row.put("sheen",s.sheen);row.put("effect",s.effect);row.put("preview",s.preview);row.put("preview_count",s.previewCount);
                    row.put("unusual",s.unusual);
                    for(int axis=0;axis<3;++axis){row.put("position_"+std::to_string(axis),s.position[axis]);row.put("rotation_"+std::to_string(axis),s.rotation[axis]);}
                    entries.push_back({"",row});
                }
                root.add_child("weapons",entries);std::filesystem::create_directories(file.parent_path());
                auto temporary=file;temporary+=".tmp";boost::property_tree::write_json(temporary.string(),root);
                if(!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                {std::filesystem::remove(temporary);throw std::runtime_error("atomic replace failed");}
                return true;
            }
            catch(const std::exception& e){status="Could not save preset: "+std::string(e.what());return false;}
        }
        bool Load(const std::filesystem::path& file)
        {
            try
            {
                if(std::filesystem::file_size(file)>256*1024)throw std::runtime_error("file exceeds 256 KB");
                boost::property_tree::ptree root;boost::property_tree::read_json(file.string(),root);
                if(root.get<std::string>("format","")!="nikogram-skin-presets"||root.get<int>("version",0)!=1)throw std::runtime_error("unsupported format/version");
                Preset next;auto entries=root.get_child_optional("weapons");if(!entries)throw std::runtime_error("missing weapon entries");
                if(entries->size()>512)throw std::runtime_error("too many entries");
                for(const auto& [unused,row]:*entries)
                {
                    int weapon=row.get<int>("weapon",-1),cls=row.get<int>("class",-1);auto item=catalog.Find(weapon);
                    if(!item||cls<0||cls>9||catalog.Canonical(weapon)!=weapon)throw std::runtime_error("unknown weapon/class");
                    // Watch cosmetics were retired; keep the remaining entries
                    // in older presets usable without re-enabling that feature.
                    if(item->Watch())continue;
                    Selection s;s.enabled=row.get<bool>("enabled",false);s.reskin=row.get<int>("reskin",0);s.australium=row.get<bool>("australium",false);
                    s.festive=row.get<bool>("festive",false);s.festivized=row.get<bool>("festivized",false);s.finish=row.get<int>("finish",0);s.wear=row.get<float>("wear",0);
                    s.seed=row.get<int>("seed",0);s.tier=row.get<int>("tier",0);s.sheen=row.get<int>("sheen",1);s.effect=row.get<int>("effect",2002);s.preview=row.get<bool>("preview",false);s.previewCount=row.get<int>("preview_count",5);
                    s.unusual=row.get<int>("unusual",0);
                    for(int axis=0;axis<3;++axis){s.position[axis]=row.get<float>("position_"+std::to_string(axis),0);s.rotation[axis]=row.get<float>("rotation_"+std::to_string(axis),0);}
                    if(!s.Valid())throw std::runtime_error("invalid cosmetic values");
                    if(s.reskin){auto variant=catalog.Find(s.reskin);bool valid=false;
                        if(variant)for(int c=1;c<=9;++c)if((!cls||cls==c)&&item->Supports(c)&&catalog.CanReskin(*item,*variant,c))valid=true;
                        if(!valid)throw std::runtime_error("incompatible reskin");}
                    if(!next.emplace(Key{weapon,cls},s).second)throw std::runtime_error("duplicate weapon/class entry");
                }
                preset=std::move(next);dirty=true;presetModified=false;
                activePreset=Lower(file.filename().string())=="current.json"?root.get<std::string>("source_preset",""):file.stem().string();
                if(!SkinPresetUi::NameAllowed(activePreset))activePreset.clear();
                status="Preset loaded.";return true;
            }
            catch(const std::exception& e){status="Preset not loaded: "+std::string(e.what());return false;}
        }
        void CatalogLoad()
        {
            auto file=I::FileSystem->Open("scripts/items/items_game.txt","rb","GAME");if(!file){status="Item catalog unavailable; no appearances will be changed.";return;}
            unsigned size=I::FileSystem->Size(file);std::string data;
            if(size<=16*1024*1024){data.resize(size);if(I::FileSystem->Read(data.data(),int(size),file)!=int(size))data.clear();}
            I::FileSystem->Close(file);
            if(!catalog.Load(data)){status="Item catalog could not be validated; no appearances will be changed.";return;}
            for(auto& [id,item]:catalog.items)if(item.token.starts_with("#"))
            {
                if(auto translated=I::Localize->Find(item.token.c_str()))
                {int count=WideCharToMultiByte(CP_UTF8,0,translated,-1,nullptr,0,nullptr,nullptr);if(count>1&&count<=2048)
                    {std::string name(count,'\0');WideCharToMultiByte(CP_UTF8,0,translated,-1,name.data(),count,nullptr,nullptr);name.pop_back();item.name=std::move(name);}}
            }
            for(auto& [id,paint]:catalog.paints)if(auto translated=I::Localize->Find(paint.token.c_str()))
            {
                int count=WideCharToMultiByte(CP_UTF8,0,translated,-1,nullptr,0,nullptr,nullptr);
                if(count>1&&count<=2048){std::string name(count,'\0');WideCharToMultiByte(CP_UTF8,0,translated,-1,name.data(),count,nullptr,nullptr);name.pop_back();paint.name=std::move(name);}
            }
            loaded=true;status=std::format("{} weapon definitions loaded from the installed catalog.",catalog.items.size());Diagnostic(status);
            // Read installed/custom weapon sound definitions outside audio hooks.
            // No loading, mutation or script execution on the sound thread.
            std::set<std::string> soundFiles={"scripts/game_sounds_weapons.txt","scripts/game_sounds_weapons_mvm.txt","scripts/game_sounds.txt"};
            if(auto manifest=I::FileSystem->Open("scripts/game_sounds_manifest.txt","rb","GAME"))
            {
                const auto bytes=I::FileSystem->Size(manifest);std::string text;
                if(bytes<=128*1024){text.resize(bytes);if(I::FileSystem->Read(text.data(),int(bytes),manifest)!=int(bytes))text.clear();}
                I::FileSystem->Close(manifest);
                if(auto parsed=Parser(text).Parse())if(auto list=parsed->Find("game_sounds_manifest"))
                    for(const auto& entry:list->children)
                        if(soundFiles.size()<64&&entry.value.starts_with("scripts/")&&entry.value.ends_with(".txt")&&entry.value.size()<128&&entry.value.find("..") == std::string::npos)
                            soundFiles.insert(entry.value);
            }
            soundWaves.clear();
            for(const auto& path:soundFiles)
            {
                auto soundFile=I::FileSystem->Open(path.c_str(),"rb","GAME");if(!soundFile)continue;
                const auto bytes=I::FileSystem->Size(soundFile);std::string text;
                if(bytes<=1024*1024){text.resize(bytes);if(I::FileSystem->Read(text.data(),int(bytes),soundFile)!=int(bytes))text.clear();}
                I::FileSystem->Close(soundFile);if(auto root=Parser(text).Parse())ReadSoundWaves(*root,soundWaves);
            }
            Diagnostic(std::format("event=remote_sound_wave_catalog scripts={} installed_files_only=true",soundWaves.size()));
            // Sound scripts differ by weapon class even when item visuals do
            // not override them (Direct Hit is one such case). Retain only
            // cosmetic fields from Valve's parsed installed script.
            weaponScriptEffects.clear();
            for(const auto& [id,item]:catalog.items)
            {
                const auto& type=item.weaponType;
                if(!type.starts_with("tf_weapon_")||!SafeEffectName(type)||weaponScriptEffects.contains(type))continue;
                CosmeticEffects effects;const auto path="scripts/"+type;
                bool bounded=false;
                for(auto ext:{".txt",".ctx"})if(auto file=I::FileSystem->Open((path+ext).c_str(),"rb","MOD"))
                {const auto size=I::FileSystem->Size(file);I::FileSystem->Close(file);if(size>0&&size<=128*1024)bounded=true;else {bounded=false;break;}}
                if(bounded&&S::Skin_ReadWeaponScript())
                {
                    KeyValuesAD data(S::Skin_ReadWeaponScript.Call<KeyValues*>(I::FileSystem,path.c_str(),reinterpret_cast<const unsigned char*>("E2NcUkG2"),false));
                    if(data)
                    {
                        if(auto sounds=data->FindKey("SoundData"))for(size_t n=0;n<effects.sounds.size();++n)
                        {auto name=sounds->GetString(SoundCategories[n]);if(name&&SafeEffectName(name))effects.sounds[n]=name;}
                        for(auto field:{"MuzzleFlashParticleEffect","TracerEffect"})
                        {auto name=data->GetString(field);if(name&&SafeEffectName(name))(std::string_view(field)=="TracerEffect"?effects.tracer:effects.muzzle)=name;}
                    }
                }
                Diagnostic(std::format("event=weapon_script_cosmetics type={} single={} critical={} installed_only=true gameplay_info_unchanged=true",type,effects.Sound(SINGLE),effects.Sound(BURST)));
                weaponScriptEffects.emplace(type,std::move(effects));
            }
            auto current=Directory()/"current.json";if(std::filesystem::exists(current)){bool ok=Load(current);Diagnostic(std::format("event=preset_autoload ok={} entries={} status={}",ok,preset.size(),status));}
        }
        uint64_t PlayerID(CTFPlayer* player)
        {
            if(!player)return 0;player_info_t info{};
            if(!I::EngineClient->GetPlayerInfo(player->entindex(),&info)||info.fakeplayer||!info.friendsID)return 0;
            return CSteamID(info.friendsID,k_EUniversePublic,k_EAccountTypeIndividual).ConvertToUint64();
        }
        bool AcquireGameTransport()
        {
            // Borrow TF2's initialized AppID/user/pipe. The global-user pipe
            // owned by NullInterfaces is not the game's initialized API context.
            // Never initialize/shutdown Steam or drain the game's callbacks.
            auto module=GetModuleHandleA("steam_api64.dll");
            if(!module){netLog.state="game_steam_api_unavailable";return false;}
            auto getUser=reinterpret_cast<HSteamUser(*)()>(GetProcAddress(module,"SteamAPI_GetHSteamUser"));
            auto getPipe=reinterpret_cast<HSteamPipe(*)()>(GetProcAddress(module,"SteamAPI_GetHSteamPipe"));
            if(!getUser||!getPipe){netLog.state="game_steam_context_exports_unavailable";return false;}
            const auto user=getUser();const auto pipe=getPipe();
            if(user<=0||pipe<=0){netLog.state="game_steam_context_not_initialized";return false;}
            auto utils=I::SteamClient->GetISteamUtils(pipe,STEAMUTILS_INTERFACE_VERSION);
            const auto app=utils?utils->GetAppID():0;
            if(!SkinProtocol::NativeContextAllowed(user,pipe,app))
            {netLog.state="game_steam_context_wrong_app";NetworkEvent(std::format("game_context_rejected_app_{}",app));return false;}
            transport=static_cast<ISteamNetworkingMessages*>(I::SteamClient->GetISteamGenericInterface(user,pipe,STEAMNETWORKINGMESSAGES_INTERFACE_VERSION));
            if(!transport){netLog.state="transport_unavailable";return false;}
            NetworkEvent("transport_bound_to_initialized_tf2_context_app_440");return true;
        }
        void ObserveTransport(uint64_t id,double now)
        {
            auto& observation=transportObservations[id];
            if(now-observation.poll<1)return;observation.poll=now;
            SteamNetworkingIdentity identity{};identity.SetSteamID64(id);
            SteamNetConnectionInfo_t info{};SteamNetConnectionRealTimeStatus_t quick{};
            const int state=int(transport->GetSessionConnectionInfo(identity,&info,&quick));
            // Explicitly accept only current-server members while opt-in is
            // active. This does not mark our own cosmetic handshake as ready.
            // Do not consume session-request callbacks belonging to TF2.
            bool attempted=state==k_ESteamNetworkingConnectionState_Connecting||state==k_ESteamNetworkingConnectionState_FindingRoute;
            const bool accepted=attempted&&transport->AcceptSessionWithUser(identity);
            if(observation.state!=state||observation.end!=info.m_eEndReason||now-observation.report>=10)
            {
                WriteDiagnostic(std::format("event=network_transport peer={} state={} state_code={} end_reason={} pending_reliable_bytes={} unacked_reliable_bytes={} ping_ms={} accept_attempted={} accept_result={} app_context=tf2_440 privacy=raw_steam_debug_redacted",
                    PeerAlias(id),SkinProtocol::TransportStateName(state),state,info.m_eEndReason,quick.m_cbPendingReliable,quick.m_cbSentUnackedReliable,quick.m_nPing,attempted,accepted));
                observation.report=now;
            }
            observation.state=state;observation.end=info.m_eEndReason;
        }
        bool Send(uint64_t id,const SkinProtocol::Message& message)
        {
            if(!transport||!server||!SkinProtocol::NetworkAllowed(V::Enabled.Value,V::Networking.Value,V::Share.Value,V::Receive.Value,I::EngineClient->IsInGame(),G::Unload))return false;
            SteamNetworkingIdentity identity{};identity.SetSteamID64(id);auto packet=SkinProtocol::Encode(message);
            if(!SkinProtocol::PacketSizeAllowed(packet.size()))return false;
            const auto result=transport->SendMessageToUser(identity,packet.data(),uint32_t(packet.size()),k_nSteamNetworkingSend_Reliable|k_nSteamNetworkingSend_AutoRestartBrokenSession,SkinProtocol::Channel);
            if(result==k_EResultOK)++netLog.sent;else ++netLog.failed;
            NetworkEvent(std::format("send_type_{}_bytes_{}_result_{}_accepted_not_delivery",message.type,packet.size(),int(result)),id);
            return result==k_EResultOK;
        }
        void Disconnect()
        {
            if(transport)for(const auto& [id,peer]:peers){SteamNetworkingIdentity identity{};identity.SetSteamID64(id);transport->CloseChannelWithUser(identity,SkinProtocol::Channel);}
            if(server||transport)NetworkEvent("context_disconnected");
            peers.clear();remote.clear();sharedPlayers.clear();transportObservations.clear();netLog.aliases.clear();server=0;contextSince=0;networkFlags=-1;lastProbe=-100;transport=nullptr;
        }
        void Networking(double now)
        {
            const bool active=SkinProtocol::NetworkAllowed(V::Enabled.Value,V::Networking.Value,V::Share.Value,V::Receive.Value,I::EngineClient->IsInGame(),G::Unload);
            netLog.state=SkinProtocol::NetworkWaitReason(V::Enabled.Value,V::Networking.Value,V::Share.Value,V::Receive.Value,I::EngineClient->IsInGame(),G::Unload);
            // Consent is checked before acquiring a Steam networking interface,
            // discovery, polling or transmission. A saved main config may opt in.
            if(!active)
            {if(server||transport)Disconnect();remote.clear();sharedPlayers.clear();networkStatus="Disabled or waiting for skin changer/sharing options; no cosmetic network traffic.";return;}
            auto channel=I::EngineClient->GetNetChannelInfo();
            if(!channel||I::EngineClient->IsPlayingDemo()){if(server||transport)Disconnect();netLog.state=channel?"demo_playback":"no_game_channel";networkStatus="Waiting for a live game connection.";return;}
            // PlayerInfo exists even for players whose entity is dormant or not
            // rendered yet. Include self in the shared roster, never in probes.
            std::map<uint64_t,int> members;
            std::vector<std::pair<uint64_t,int>> roster;
            bool localKnown=false;
            for(int n=1;n<=std::clamp(I::EngineClient->GetMaxClients(),0,128);++n)
            {
                player_info_t info{};
                if(!I::EngineClient->GetPlayerInfo(n,&info)||info.fakeplayer||!info.friendsID||info.userID<=0)continue;
                const auto id=CSteamID(info.friendsID,k_EUniversePublic,k_EAccountTypeIndividual).ConvertToUint64();
                roster.emplace_back(id,info.userID);
                if(n==I::EngineClient->GetLocalPlayer()){localKnown=true;continue;}
                members[id]=n;
            }
            if(!localKnown||members.empty())
            {
                if(server||transport)Disconnect();
                netLog.state=localKnown?"no_eligible_remote_players":"local_player_identity_unavailable";
                networkStatus=localKnown?"No remote human players; cosmetic networking is idle.":"Waiting for your player identity.";return;
            }
            const char* level=I::EngineClient->GetLevelName();
            const uint64_t context=SkinProtocol::SessionContext(level?level:"",roster);
            if(!context){if(server||transport)Disconnect();netLog.state="invalid_session_roster";networkStatus="Waiting for a valid player roster.";return;}
            if(context!=server){Disconnect();server=context;contextSince=now;NetworkEvent(channel->IsLoopback()?"hosted_roster_context_changed":"remote_roster_context_changed");}
            if(now-contextSince<1){netLog.state="roster_settling";networkStatus="Waiting briefly for the player roster to settle.";return;}
            if(!transport&&!AcquireGameTransport()){networkStatus="Waiting for TF2's initialized Steam networking context: "+netLog.state;return;}
            const int flags=(V::Share.Value?1:0)|(V::Receive.Value?2:0);
            for(auto it=peers.begin();it!=peers.end();)if(!members.contains(it->first))
            {NetworkEvent("peer_left_server",it->first);SteamNetworkingIdentity identity{};identity.SetSteamID64(it->first);transport->CloseChannelWithUser(identity,SkinProtocol::Channel);ForgetRemote(it->first);transportObservations.erase(it->first);netLog.aliases.erase(it->first);it=peers.erase(it);}else ++it;
            // Prepare every current member before receiving responses. Steam
            // implicitly accepts pending sessions when we send a discovery.
            for(const auto& [id,player]:members)
            {
                auto& peer=peers[id];if(!peer.local)peer.local=Nonce();
                ObserveTransport(id,now); // Observe failures before retrying sends.
                if(peer.Expire(now)){peer.local=Nonce();ForgetRemote(id);NetworkEvent("peer_expired_timeout_30_seconds",id);}
                if(flags!=networkFlags)peer.lastHello=-100;
            }
            networkFlags=flags;
            netLog.state=members.empty()?"no_eligible_remote_players":"discovering";
            // Each peer is pinged every ten seconds, spread across small batches
            // (at most four probes per 200 ms) instead of a server-wide burst.
            if(now-lastProbe>=.2)
            {
                int probes=0;
                for(auto& [id,peer]:peers)
                {
                    if(!peer.DiscoveryDue(now))continue;
                    SkinProtocol::Message hello;hello.server=server;hello.nonce=peer.local;hello.flags=flags;
                    Send(id,hello);peer.lastHello=now;
                    if(++probes>=4)break;
                }
                lastProbe=now;
            }
            SteamNetworkingMessage_t* incoming[16]{};int rawCount=transport->ReceiveMessagesOnChannel(SkinProtocol::Channel,incoming,16);
            if(rawCount<0)NetworkEvent("receive_poll_error");
            int count=std::clamp(rawCount,0,16);netLog.received+=count;
            struct ReceivedBatch
            {
                SteamNetworkingMessage_t** messages;int count;
                ~ReceivedBatch(){for(int n=0;n<count;++n)if(messages[n])messages[n]->Release();}
            } batch{incoming,count};
            for(int n=0;n<count;++n)
            {
                auto packet=incoming[n];if(!packet){NetworkEvent("null_packet");continue;}const uint64_t id=packet->m_identityPeer.GetSteamID64();
                auto peer=peers.find(id);
                if(!members.contains(id)||peer==peers.end()){NetworkEvent("sender_not_current_server_member");continue;}
                if(!packet->m_pData){NetworkEvent("null_payload",id);continue;}
                if(!SkinProtocol::PacketSizeAllowed(size_t(packet->m_cbSize))){NetworkEvent("invalid_packet_size",id);continue;}
                if(members.contains(id)&&peer!=peers.end()&&packet->m_pData&&SkinProtocol::PacketSizeAllowed(size_t(packet->m_cbSize)))
                {
                    auto message=SkinProtocol::Decode({static_cast<uint8_t*>(packet->m_pData),size_t(packet->m_cbSize)});
                    if(message)
                    {
                        NetworkEvent(std::format("receive_type_{}_bytes_{}",message->type,packet->m_cbSize),id);
                        const auto reason=SkinProtocol::RejectReason(peer->second,*message,server,now);
                        if(std::string_view(reason)!="none")NetworkEvent(reason,id);
                        const bool wasReady=peer->second.ready;
                        if(auto reply=peer->second.Accept(*message,server,now,flags))
                            if(now-peer->second.lastReply>=1){Send(id,*reply);peer->second.lastReply=now;}
                        if(wasReady&&!peer->second.ready){ForgetRemote(id);NetworkEvent("peer_nonce_changed_handshake_reset",id);}
                        if(message->type==SkinProtocol::Hello&&message->server==server&&message->nonce==peer->second.remote&&!(message->flags&1))ForgetRemote(id);
                        if(!wasReady&&peer->second.ready){++netLog.ready;WriteDiagnostic(std::format("event=network_handshake_ready peer={} same_server=true nonce_echo_verified=true remote_share={} remote_receive={}",PeerAlias(id),bool(peer->second.flags&1),bool(peer->second.flags&2)));}
                        if(peer->second.CanReceive(*message,server,now))
                        {
                            if(message->type==SkinProtocol::Withdraw){if(!(message->flags&1)){remote.erase(id);sharedPlayers.erase(id);}peer->second.lastSeen=now;}
                            if(message->type==SkinProtocol::PlayerAppearance&&V::Receive.Value)
                            {
                                sharedPlayers[id]={message->cls,message->pipBoy,message->authenticAnimations,now};
                                peer->second.lastSeen=now;
                                NetworkEvent(std::format("player_appearance_class_{}_pipboy_{}_authentic_{}",message->cls,message->pipBoy,message->authenticAnimations),id);
                            }
                            auto equipmentItem=message->type==SkinProtocol::EquipmentStyle?catalog.Find(message->weapon):nullptr;
                            const bool equipmentAllowed=equipmentItem&&message->cls==8&&equipmentItem->Sapper();
                            if(SkinProtocol::HasSelection(message->type) && (message->type==SkinProtocol::Style||equipmentAllowed) && V::Receive.Value && Compatible(message->weapon,message->cls,message->selection))
                            {
                                player_info_t playerInfo{};
                                if(message->streak>=0&&(!I::EngineClient->GetPlayerInfo(members.at(id),&playerInfo)||playerInfo.userID!=message->user))
                                {NetworkEvent("streak_player_connection_mismatch",id);continue;}
                                auto& entry=remote[id];
                                if(entry.cls&&entry.cls!=message->cls)entry.killstreakWeapons={};
                                entry.cls=message->cls;entry.time=now;
                                if(message->type==SkinProtocol::Style){entry.weapon=message->weapon;entry.selection=message->selection;}
                                entry.killstreakWeapons.Remember(catalog.Canonical(message->weapon),message->cls,message->selection);
                                if(message->streak>=0)
                                {
                                    auto client=I::ClientEntityList->GetClientEntity(members.at(id));
                                    const int witnessed=client?lifeStreaks.Get(uint32_t(client->GetRefEHandle().ToInt())):0;
                                    const bool accepted=entry.streak.Sync(message->streak,message->life,witnessed);
                                    NetworkEvent(std::format("streak_sync_count_{}_accepted_{}",message->streak,accepted),id);
                                }
                                else entry.streak={};
                                peer->second.lastSeen=now;++netLog.styles;
                                NetworkEvent(std::format("style_applied_item_{}_class_{}_reskin_{}_unusual_{}_enabled_{}",message->weapon,message->cls,message->selection.reskin,message->selection.unusual,message->selection.enabled),id);}
                            else if(SkinProtocol::HasSelection(message->type))NetworkEvent(V::Receive.Value?"incompatible_cosmetic_style":"receiving_disabled",id);
                            if(message->type==SkinProtocol::Withdraw)NetworkEvent("remote_style_withdrawn",id);
                        }
                    }
                    else NetworkEvent("decode_rejected_header_version_length_or_values",id);
                }
            }
            if(!V::Networking.Value||!V::Receive.Value){remote.clear();sharedPlayers.clear();}
            if(std::any_of(peers.begin(),peers.end(),[](const auto& p){return p.second.ready;}))netLog.state="peers_ready";
            networkStatus=std::format("Automatic discovery: {} peers ready / {} players; {} Steam connections established; {}; 10-second probes; no Nikogram backend.",
                std::count_if(peers.begin(),peers.end(),[](const auto& p){return p.second.ready;}),members.size(),std::count_if(transportObservations.begin(),transportObservations.end(),[](const auto& p){return p.second.state==k_ESteamNetworkingConnectionState_Connected;}),channel->IsLoopback()?"locally hosted game":"remote server");
            auto local=H::Entities.GetLocal();auto weapon=H::Entities.GetWeapon();
            const int item=weapon?weapon->m_iItemDefinitionIndex():0,cls=local?local->m_iClass():0;
            const bool haveStyle=local&&weapon&&catalog.Find(item)&&cls>=1&&cls<=9;
            auto selection=haveStyle?Get(preset,catalog.Canonical(item),cls):Selection{};selection.preview=false;
            player_info_t localInfo{};
            const bool shareCount=haveStyle&&I::EngineClient->GetPlayerInfo(I::EngineClient->GetLocalPlayer(),&localInfo)&&localInfo.userID>0;
            int actualCount=0;
            if(shareCount&&local->IsAlive())
            {
                int native=-1;static const int offset=U::NetVars.GetNetVar("CTFPlayer","m_nStreaks");
                if(offset>0&&offset<65536)std::memcpy(&native,reinterpret_cast<const char*>(local)+offset,sizeof(native));
                actualCount=SkinRender::KillstreakCount(lifeStreaks.Get(uint32_t(local->GetRefEHandle().ToInt())),native,false,0,false);
                actualCount=std::clamp(actualCount,0,1000000);
            }
            for(auto& [id,peer]:peers)if(peer.ready && now-peer.lastSeen<SkinProtocol::PeerTimeoutSeconds && (peer.flags&2))
            {
                if(V::Share.Value&&local&&cls>=1&&cls<=9)
                {
                    SkinProtocol::Message appearance;appearance.type=SkinProtocol::PlayerAppearance;
                    appearance.server=server;appearance.nonce=peer.local;appearance.echo=peer.remote;appearance.flags=flags;appearance.cls=cls;
                    appearance.pipBoy=cls==9&&V::PipBoy.Value;appearance.authenticAnimations=V::ThirdPersonAnimations.Value==0;
                    const auto bytes=SkinProtocol::Encode(appearance);
                    if(peer.AppearanceDue(bytes,now))
                    {if(Send(id,appearance))peer.SentAppearance(bytes,now);else peer.lastAppearanceSend=now;}
                }
                SkinProtocol::Message message;message.type=V::Share.Value&&haveStyle?SkinProtocol::Style:SkinProtocol::Withdraw;
                message.server=server;message.nonce=peer.local;message.echo=peer.remote;message.flags=flags;message.weapon=haveStyle?item:0;message.cls=haveStyle?cls:0;message.selection=selection;
                if(message.type==SkinProtocol::Style&&shareCount){message.streak=actualCount;message.life=localLife;message.user=localInfo.userID;}
                const auto payload=SkinProtocol::Encode(message);
                if((message.type==SkinProtocol::Withdraw||selection.Valid())&&peer.StyleDue(payload,now))
                {
                    if(Send(id,message))peer.SentStyle(payload,now);
                    else peer.lastSend=now; // Back off failed sends without caching an unsent style.
                }
                // Keep sapper profiles fresh while its owner holds a knife/revolver.
                // Separate packets cannot overwrite the peer's held style.
                if(V::Share.Value&&local&&cls==8)
                    for(auto& handle:local->m_hMyWeapons())if(auto equipmentWeapon=handle.Get())
                    {
                        auto itemEntry=catalog.Find(equipmentWeapon->m_iItemDefinitionIndex());
                        if(!itemEntry||!itemEntry->Sapper())continue;
                        SkinProtocol::Message equipment;equipment.type=SkinProtocol::EquipmentStyle;equipment.server=server;
                        equipment.nonce=peer.local;equipment.echo=peer.remote;equipment.flags=flags;equipment.cls=8;equipment.weapon=itemEntry->id;
                        equipment.selection=Get(preset,catalog.Canonical(itemEntry->id),8);
                        auto bytes=SkinProtocol::Encode(equipment);
                        if(equipment.selection.Valid()&&peer.EquipmentDue(itemEntry->id,bytes,now))
                        {Send(id,equipment);peer.SentEquipment(itemEntry->id,bytes,now);}
                    }
            }
            for(auto it=remote.begin();it!=remote.end();)if(now-it->second.time>10){NetworkEvent("remote_style_expired",it->first);it=remote.erase(it);}else ++it;
            std::erase_if(sharedPlayers,[&](const auto& row){return now-row.second.time>10;});
        }
        std::optional<std::string> BoneName(const studiohdr_t* header,int index)
        {
            if(!header||header->length<int(sizeof(studiohdr_t))||header->length>64*1024*1024
                ||header->numbones<1||header->numbones>MAXSTUDIOBONES||index<0||index>=header->numbones
                ||header->boneindex<0||int64_t(header->boneindex)+int64_t(header->numbones)*sizeof(mstudiobone_t)>header->length)return std::nullopt;
            auto bone=header->pBone(index);
            return SkinRender::BoundedModelString(std::span(reinterpret_cast<const char*>(header),size_t(header->length)),
                int64_t(header->boneindex)+int64_t(index)*sizeof(mstudiobone_t)+bone->sznameindex);
        }
        const model_t* LoadModel(const std::string& path,bool readyOnly=false)
        {
            if(path.empty()||path.size()>260||!path.starts_with("models/")||!path.ends_with(".mdl")||path.find("..")!=std::string::npos){Diagnostic("event=model_rejected path="+path);return nullptr;}
            // Extra glow draws may only use models registered by normal draws.
            // Never register or force-load resources from an effect pass.
            if(readyOnly&&!models.contains(path))return nullptr;
            auto& entry=models[path];
            if(readyOnly&&entry.index==-1)return nullptr;
            if(entry.index==-1)
            {
                entry.index=I::ModelInfoClient->GetModelIndex(path.c_str());
                if(entry.index==-1)
                {
                    entry.index=I::ModelInfoClient->RegisterDynamicModel(path.c_str(),true);
                    if(entry.index!=-1){I::ModelInfoClient->AddRefDynamicModel(entry.index);entry.owned=true;}
                }
            }
            if(entry.index==-1){Diagnostic("event=model_register_failed path="+path);return nullptr;}
            if(entry.index<-1&&I::ModelInfoClient->IsDynamicModelLoading(entry.index)){Diagnostic("event=model_loading path="+path);return nullptr;}
            auto cache=static_cast<IMDLCache*>(I::MDLCache);if(!cache)return nullptr;
            // Refresh borrowed pointers after weapon/map transitions.
            entry.model=I::ModelInfoClient->GetModel(entry.index);if(!entry.model)return nullptr;
            auto handle=I::ModelInfoClient->GetCacheHandle(entry.model);if(handle==MDLHANDLE_INVALID)return nullptr;
            if(readyOnly&&!SkinRender::AllowEffectPreparation(true,true,
                cache->IsDataLoaded(handle,MDLCACHE_STUDIOHDR),cache->IsDataLoaded(handle,MDLCACHE_STUDIOHWDATA),true))return nullptr;
            auto header=cache->GetStudioHdr(handle);if(!header)return nullptr;
            auto firstBone=BoneName(header,0);
            if(!firstBone){Diagnostic("event=model_metadata_invalid path="+path);return nullptr;}
            const bool error=cache->IsErrorModel(handle);
            if(!SkinRender::ReadyModelHeader(error,*firstBone,header->numbones))
            {
                Diagnostic(std::format("event=model_filtered_or_unavailable path={} index={} cache_handle={} bones={} first_bone={} error={} possible_custom_asset_override={}",
                    path,entry.index,handle,header->numbones,*firstBone,error,!error&&*firstBone=="dummy_bone"));
                return nullptr;
            }
            // Diagnostics use the owned path, not the optional secondary header's name.
            Diagnostic(std::format("event=model_lookup path={} index={} cache_handle={} bones={} available=true",path,entry.index,handle,header->numbones));
            return entry.model;
        }
        void WarmEffectModels()
        {
            if(!V::Enabled.Value||G::Unload||F::Chams.m_bRendering||F::Glow.m_bRendering||!I::EngineClient->IsInGame())return;
            auto local=H::Entities.GetLocal();auto cache=static_cast<IMDLCache*>(I::MDLCache);if(!local||!cache)return;
            SkinRender::ModelCacheScope<IMDLCache> cacheScope(cache);
            int registrations=0;
            auto warm=[&](const std::string& path)
            {
                if(path.empty())return;
                if(!models.contains(path)){if(registrations>=2)return;++registrations;}
                if(auto model=LoadModel(path))
                {
                    const auto handle=I::ModelInfoClient->GetCacheHandle(model);
                    if(handle!=MDLHANDLE_INVALID)cache->GetHardwareData(handle);
                }
            };
            // FRAME_START, never an effect callback. Chams can suppress the
            // normal draw that previously registered the world replacement.
            for(int index=1;index<=std::clamp(I::EngineClient->GetMaxClients(),0,128);++index)
            {
                auto client=I::ClientEntityList->GetClientEntity(index);auto entity=client?client->As<CBaseEntity>():nullptr;
                if(!entity||!entity->IsPlayer())continue;auto player=entity->As<CTFPlayer>();
                if(!player->IsAlive()||player->IsDormant())continue;
                if(PipBoyFor(player))warm("models/workshop_partner/player/items/engineer/bet_pb/bet_pb.mdl");
                if(player->m_iClass()==8)
                {
                    auto equipment=EquipmentWeapon(player);Selection selected;
                    if(CosmeticSelection(equipment,player,selected))
                    {
                        auto original=catalog.Find(equipment->m_iItemDefinitionIndex());auto chosen=original?Appearance(*original,8,selected):nullptr;
                        if(chosen){warm(chosen->WorldModel(8));warm(chosen->PlacedSapper(false));warm(chosen->PlacedSapper(true));}
                    }
                }
                auto active=player->m_hActiveWeapon().Get();if(!active)continue;auto weapon=active->As<CTFWeaponBase>();
                const int id=weapon->m_iItemDefinitionIndex(),cls=player->m_iClass();auto item=catalog.Find(id);if(!item)continue;
                Selection selection;
                if(player==local)selection=Get(preset,catalog.Canonical(id),cls);
                else
                {
                    if(!V::Networking.Value||!V::Receive.Value)continue;auto received=remote.find(PlayerID(player));
                    if(received==remote.end()||received->second.cls!=cls||catalog.Canonical(received->second.weapon)!=catalog.Canonical(id)||Now()-received->second.time>10)continue;
                    selection=received->second.selection;
                }
                if(!selection.enabled||!Compatible(id,cls,selection))continue;
                auto chosen=Appearance(*item,cls,selection);if(!chosen)continue;
                if(chosen->WorldModel(cls)!=item->WorldModel(cls))warm(chosen->WorldModel(cls));
                const bool nativeLights=selection.festivized&&SDK::AttribHookValue(0.f,"is_festivized",weapon)!=0.f;
                for(const auto& attachment:CosmeticAttachments(*item,*chosen,player->m_iTeamNum(),false,selection.festivized,nativeLights))warm(attachment.model);
                if(player==local)
                {
                    if(chosen->ViewModel(cls)!=item->ViewModel(cls))warm(chosen->ViewModel(cls));
                    for(const auto& attachment:CosmeticAttachments(*item,*chosen,player->m_iTeamNum(),true,selection.festivized,nativeLights))warm(attachment.model);
                }
            }
        }
    }

    PaintDrawScope::PaintDrawScope(const ModelRenderInfo_t& info,bool localViewmodelDraw)
    {
        // Shared model materials must also be restored for unpainted nested draws.
        previous=activePaint;if(previous)previous->Suspend();activePaint=this;
        I::ModelRender->GetMaterialOverride(&oldForced,&oldOverride);
        try
        {
        auto build=[&]()
        {
        std::lock_guard lock(guard);
        if(stopped||!loaded||!V::Enabled.Value||G::Unload)return;
        auto held=H::Entities.GetWeapon();auto local=H::Entities.GetLocal();
        auto requested=held&&local?Get(preset,catalog.Canonical(held->m_iItemDefinitionIndex()),local->m_iClass()):Selection{};
        auto gate=[&](const char* reason)
        {
            if(!requested.enabled||!requested.finish)return;
            const char* name=info.pModel?I::ModelInfoClient->GetModelName(info.pModel):nullptr;
            Diagnostic(std::format("event=paint_setup_exit reason={} item={} finish={} local_scope={} entity={} model={}",reason,
                held?held->m_iItemDefinitionIndex():-1,requested.finish,localViewmodelDraw,info.entity_index,name?name:"<none>"));
        };
        if(!info.pModel){gate("missing_model");return;}
        const char* drawPath=I::ModelInfoClient->GetModelName(info.pModel);
        if(!drawPath||(!std::string_view(drawPath).starts_with("models/weapons/")&&!std::string_view(drawPath).starts_with("models/workshop/weapons/")&&!std::string_view(drawPath).starts_with("models/workshop_partner/weapons/")))return;
        if(F::Chams.m_bRendering||F::Glow.m_bRendering){gate("effect_pass");return;}
        if(!local){gate("missing_local_player");return;}
        CTFWeaponBase* weapon=nullptr;CTFPlayer* owner=nullptr;
        auto client=I::ClientEntityList->GetClientEntity(info.entity_index);auto entity=client?client->As<CBaseEntity>():nullptr;
        if(entity&&entity->IsBaseCombatWeapon())
        {weapon=entity->As<CTFWeaponBase>();auto parent=weapon->m_hOwner().Get();if(parent&&parent->IsPlayer())owner=parent->As<CTFPlayer>();}
        else if(localViewmodelDraw){weapon=H::Entities.GetWeapon();owner=local;}
        if(!weapon||!owner){gate("unresolved_weapon_owner");return;}
        if(!owner->IsAlive()||owner->IsDormant()){gate("inactive_weapon_owner");return;}
        const int id=weapon->m_iItemDefinitionIndex(),cls=owner->m_iClass(),team=owner->m_iTeamNum();if(team!=2&&team!=3){gate("invalid_team");return;}
        auto original=catalog.Find(id);if(!original){gate("uncataloged_weapon");return;}
        Selection selection;
        if(owner==local)selection=Get(preset,catalog.Canonical(id),cls);
        else
        {
            if(!V::Networking.Value||!V::Receive.Value){gate("remote_sharing_disabled");return;}auto it=remote.find(PlayerID(owner));
            if(it==remote.end()||it->second.cls!=cls||catalog.Canonical(it->second.weapon)!=catalog.Canonical(id)||Now()-it->second.time>10){gate("no_current_consented_remote_selection");return;}
            selection=it->second.selection;
        }
        if(!selection.enabled){gate("no_paint_selected_for_draw_owner");return;}
        if(!Compatible(id,cls,selection)){gate("incompatible_cosmetic_selection");return;}
        auto chosen=Appearance(*original,cls,selection);const char* raw=I::ModelInfoClient->GetModelName(info.pModel);
        // Exact weapon model only: hands, muzzle effects and holiday/botkiller
        // addons have separate draws and must keep their own authored textures.
        if(!chosen||!raw||(raw!=chosen->Model(cls)&&raw!=chosen->ViewModel(cls))){gate("not_selected_weapon_surface");return;}
        killstreak=KillstreakFor(weapon);
        if(!chosen->materialOverride.empty())
        {
            auto materialName=chosen->materialOverride;if(materialName.ends_with(".vmt"))materialName.resize(materialName.size()-4);
            auto gold=I::MaterialSystem->FindMaterial(materialName.c_str(),TEXTURE_GROUP_MODEL);
            if(gold&&!gold->IsErrorMaterial())
            {
                forced=gold;I::ModelRender->ForcedMaterialOverride(forced);
                const auto header=I::ModelInfoClient->GetStudiomodel(info.pModel);
                const int capacity=SkinRender::PaintMaterialCapacity(I::ModelInfoClient->GetModelMaterialCount(info.pModel),I::ModelInfoClient->GetModelType(info.pModel),
                    header&&header->length>0&&header->length<=64*1024*1024?header->numtextures:0);
                std::vector<IMaterial*> surfaces(capacity);if(capacity)I::ModelInfoClient->GetModelMaterials(info.pModel,capacity,surfaces.data());
                for(const char* name:{"$basetexture","$bumpmap","$envmapmask"})
                {
                    bool exists=false;auto source=gold->FindVar(name,&exists,false);auto texture=exists&&source?source->GetTextureValue():nullptr;
                    if(!texture||texture->IsError())continue;
                    for(auto surface:surfaces)if(surface&&!surface->IsErrorMaterial())
                    {bool found=false;auto target=surface->FindVar(name,&found,false);if(found&&target&&std::none_of(bindings.begin(),bindings.end(),[&](const Binding& b){return b.var==target;}))bindings.emplace_back(target,texture);}
                }
                Apply();Diagnostic(std::format("event=gold_material item={} chosen={} channels={} render_only=true",id,chosen->id,bindings.size()));
            }
            return;
        }
        if(!selection.finish||selection.australium||chosen->golden)return;
        if(PaintWeapon(*chosen,cls,PaintIndex(selection.finish))<0)
        {Diagnostic(std::format("event=paint_skipped reason=unsupported_recipe kit={} item={} reskin={} class={}",PaintIndex(selection.finish),id,chosen->id,cls));return;}
        const int reported=I::ModelInfoClient->GetModelMaterialCount(info.pModel),modelType=I::ModelInfoClient->GetModelType(info.pModel);
        const auto header=modelType==3?I::ModelInfoClient->GetStudiomodel(info.pModel):nullptr;
        const int studioTextures=header&&header->length>0&&header->length<=64*1024*1024?header->numtextures:0;
        int count=SkinRender::PaintMaterialCapacity(reported,modelType,studioTextures);
        Diagnostic(std::format("event=paint_material_inventory item={} kit={} model_type={} reported_count={} studio_textures={} capacity={} model={}",id,PaintIndex(selection.finish),modelType,reported,studioTextures,count,raw));
        if(!count){gate("invalid_material_capacity");return;}
        int authored=0;
        std::vector<IMaterial*> materials(count);I::ModelInfoClient->GetModelMaterials(info.pModel,count,materials.data());
        for(auto material:materials)if(AuthoredPaintMaterial(material))
        {
            ++authored;
            bool found=false;auto var=material->FindVar("$basetexture",&found,false);if(!found||!var){gate("missing_base_texture_variable");continue;}
            if(std::any_of(bindings.begin(),bindings.end(),[&](const Binding& binding){return binding.var==var;}))continue;
            auto texture=var->GetTextureValue();if(!texture||texture->IsError()){gate("invalid_original_surface_texture");continue;}
            if(!painted)painted=PaintedSurface(*chosen,cls,selection,team,texture,weapon->GetRefEHandle().ToInt(),info.pModel);
            if(!painted)continue;
            bindings.emplace_back(var,painted);
        }
        if(!bindings.empty())
        {
            Apply();renderStatus="War-paint surface ready (render-only).";
            Diagnostic(std::format("event=paint_draw kit={} item={} reskin={} wear_level={} seed={} team={} local_scope={} materials={} model={}",PaintIndex(selection.finish),id,chosen->id,WearLevel(selection.wear),selection.seed,team,localViewmodelDraw,bindings.size(),raw));
        }
        else
        {
            painted=nullptr;renderStatus=authored?"Waiting for war-paint texture; see paint generation diagnostics.":"No authored paint material found; the original texture was retained.";
            if(!authored)Diagnostic(std::format("event=paint_skipped reason=no_authored_material kit={} item={} reskin={} material_count={} model={}",PaintIndex(selection.finish),id,chosen->id,count,raw));
        }
        };
        build();
        }
        catch(...){bindings.clear();painted=nullptr;I::ModelRender->ForcedMaterialOverride(oldForced,oldOverride);activePaint=previous;if(previous)previous->Apply();throw;}
    }
    PaintDrawScope::~PaintDrawScope()
    {
        bindings.clear();I::ModelRender->ForcedMaterialOverride(oldForced,oldOverride);activePaint=previous;if(previous)previous->Apply();
    }
    void PaintDrawScope::Apply(){for(auto& binding:bindings)binding.Apply();if(forced)I::ModelRender->ForcedMaterialOverride(forced);}
    void PaintDrawScope::Suspend(){for(auto& binding:bindings)binding.Suspend();if(forced)I::ModelRender->ForcedMaterialOverride(oldForced,oldOverride);}
    void ApplyPaintDraw(){if(activePaint)activePaint->Apply();}

    std::optional<KillstreakProfile> CurrentDrawKillstreak()
    {return activePaint&&!F::Chams.m_bRendering&&!F::Glow.m_bRendering&&!G::Unload&&!SDK::CleanScreenshot()?activePaint->Killstreak():std::nullopt;}
    KillstreakAttributeScope::KillstreakAttributeScope(std::optional<KillstreakProfile> value):profile(std::move(value)),previous(activeKillstreak)
    {activeKillstreak=profile?&*profile:nullptr;}
    KillstreakAttributeScope::~KillstreakAttributeScope(){activeKillstreak=previous;}
    std::optional<int> KillstreakAttribute(const char* name,void* entity)
    {
        if(!name||!activeKillstreak||entity!=activeKillstreak->weapon||G::Unload||SDK::CleanScreenshot())return {};
        auto value=SkinRender::KillstreakAttribute(name,activeKillstreak->tier,activeKillstreak->sheen,activeKillstreak->effect);
        if(value){std::lock_guard lock(guard);Diagnostic(std::format("event=killstreak_native_attribute name={} value={} scoped=true inventory_unchanged=true",name,*value));}
        return value;
    }
    bool KillstreakKillEligible(IGameEvent* event)
    {
        if(!event||std::string_view(event->GetName())!="player_death"||(event->GetInt("death_flags")&TF_DEATH_FEIGN_DEATH))return false;
        // A positive server weapon streak is authoritative for a real kit.
        if(event->GetInt("kill_streak_wep")>0)return true;
        if(!V::Enabled.Value||G::Unload||I::EngineClient->IsPlayingDemo())return false;
        std::lock_guard lock(guard);if(stopped||!loaded)return false;
        const int index=I::EngineClient->GetPlayerForUserID(event->GetInt("attacker"));
        if(index<1||index>std::min(I::EngineClient->GetMaxClients(),128))return false;
        auto client=I::ClientEntityList->GetClientEntity(index);auto player=client?client->As<CTFPlayer>():nullptr;
        if(!player||!player->IsPlayer())return false;
        // Credit the weapon that caused the death, not whatever was drawn when
        // a projectile/afterburn kill arrived after a weapon switch.
        auto held=player->m_hActiveWeapon().Get();auto weapon=held?held->As<CTFWeaponBase>():nullptr;
        const auto definition=SkinRender::KillWeaponDefinition(event->GetInt("weapon_def_index",-1),
            weapon?weapon->m_iItemDefinitionIndex():-1,event->GetInt("weaponid",-1),weapon?weapon->GetWeaponID():-1);
        if(!definition)return false;const int item=*definition;
        if(!catalog.Find(item))return false;
        const int cls=player->m_iClass();Selection selection;
        if(player==H::Entities.GetLocal())selection=Get(preset,catalog.Canonical(item),cls);
        else
        {
            if(!V::Networking.Value||!V::Receive.Value)return false;
            auto it=remote.find(PlayerID(player));if(it==remote.end()||Now()-it->second.time>10)return false;
            auto cached=it->second.killstreakWeapons.Find(catalog.Canonical(item),cls);if(!cached)return false;
            selection=*cached;
        }
        return selection.enabled&&selection.tier>=1&&selection.tier<=3&&Compatible(item,cls,selection);
    }
    void KillstreakEvent(IGameEvent* event)
    {
        if(!event)return;std::lock_guard lock(guard);if(stopped)return;
        const std::string_view name(event->GetName());
        if(name=="client_beginconnect"||name=="client_disconnect"||name=="game_newmap"||name=="teamplay_round_start"||name=="mvm_reset_stats")
        {lifeStreaks.Clear();goldDeaths.clear();corpses.clear();ragdollBirths.clear();corpsePoses.clear();if(++localLife==0)localLife=1;for(auto& [id,entry]:remote){entry.streak.ResetLife();entry.milestone.Reset();}return;}
        if(I::EngineClient->IsPlayingDemo())return;
        auto handle=[&](int user)->uint32_t
        {
            int index=I::EngineClient->GetPlayerForUserID(user);if(index<1||index>std::min(I::EngineClient->GetMaxClients(),128))return 0;
            auto client=I::ClientEntityList->GetClientEntity(index);auto player=client?client->As<CBaseEntity>():nullptr;
            return player&&player->IsPlayer()?uint32_t(player->GetRefEHandle().ToInt()):0;
        };
        auto resetShared=[&](int user)
        {
            const int index=I::EngineClient->GetPlayerForUserID(user);
            if(index==I::EngineClient->GetLocalPlayer()){if(++localLife==0)localLife=1;}
            auto client=index>0?I::ClientEntityList->GetClientEntity(index):nullptr;
            auto player=client?client->As<CTFPlayer>():nullptr;
            // Death resets remote counts; spawning also advances our outgoing
            // epoch, but must not discard a peer's already-arrived new-life count.
            if(name!="player_spawn"&&player&&player->IsPlayer())if(auto it=remote.find(PlayerID(player));it!=remote.end()){it->second.streak.ResetLife();it->second.milestone.Reset();}
        };
        if(name=="player_spawn"||name=="player_disconnect"){lifeStreaks.Spawn(handle(event->GetInt("userid")));goldDeaths.erase(I::EngineClient->GetPlayerForUserID(event->GetInt("userid")));resetShared(event->GetInt("userid"));}
        if(name=="player_death")
        {
            bool feign=(event->GetInt("death_flags")&TF_DEATH_FEIGN_DEATH)!=0;
            const auto victim=handle(event->GetInt("userid")),attacker=handle(event->GetInt("attacker"));
            const bool credit=KillstreakKillEligible(event);
            if(lifeStreaks.DeathOnce(reinterpret_cast<uintptr_t>(event),I::GlobalVars->framecount,victim,attacker,feign,credit))
            {
                if(!feign)resetShared(event->GetInt("userid"));
                if(!feign)
                {
                    const int victimIndex=I::EngineClient->GetPlayerForUserID(event->GetInt("userid"));goldDeaths.erase(victimIndex);
                    const int killerIndex=I::EngineClient->GetPlayerForUserID(event->GetInt("attacker"));
                    auto client=killerIndex>0?I::ClientEntityList->GetClientEntity(killerIndex):nullptr;
                    auto killer=client?client->As<CTFPlayer>():nullptr;
                    auto held=killer&&killer->IsPlayer()?killer->m_hActiveWeapon().Get():nullptr;
                    auto weapon=held?held->As<CTFWeaponBase>():nullptr;
                    auto definition=SkinRender::KillWeaponDefinition(event->GetInt("weapon_def_index",-1),
                        weapon?weapon->m_iItemDefinitionIndex():-1,event->GetInt("weaponid",-1),weapon?weapon->GetWeaponID():-1);
                    const int item=definition.value_or(-1);auto original=catalog.Find(item);
                    if(V::Enabled.Value&&loaded&&killer&&killer->IsPlayer()&&original&&victimIndex>0&&victimIndex<=128&&attacker!=victim)
                    {
                        Selection selection;const bool shared=killer!=H::Entities.GetLocal();
                        if(!shared)selection=Get(preset,catalog.Canonical(item),killer->m_iClass());
                        else if(V::Networking.Value&&V::Receive.Value)
                            if(auto it=remote.find(PlayerID(killer));it!=remote.end()&&Now()-it->second.time<=10)
                                if(auto cached=it->second.killstreakWeapons.Find(catalog.Canonical(item),killer->m_iClass()))selection=*cached;
                        auto chosen=selection.enabled?Appearance(*original,killer->m_iClass(),selection):nullptr;
                        int effects=chosen?chosen->deathEffects:0;
                        if(chosen&&chosen->id==264&&selection.australium)effects|=DeathGold;
                        // Ice Knife freezes backstabs, not ordinary knife damage.
                        if(event->GetInt("customkill")!=TF_DMG_CUSTOM_BACKSTAB)effects&=~DeathIce;
                        if(chosen&&effects)
                        {
                            auto victimClient=I::ClientEntityList->GetClientEntity(victimIndex);
                            auto victimEntity=victimClient?victimClient->As<CBaseEntity>():nullptr;
                            if(victimEntity&&uint32_t(victimEntity->GetRefEHandle().ToInt())==victim)
                            {
                                goldDeaths[victimIndex]={victim,Now()+3,shared,effects,shared?PlayerID(killer):0,victimEntity->GetAbsOrigin()};
                                WriteDiagnostic(std::format("event=cosmetic_death_mark victim={} source_item={} appearance={} effects={} shared={} client_only=true",victimIndex,item,chosen->id,effects,shared));
                            }
                        }
                    }
                }
                Diagnostic(std::format("event=killstreak_death victim={} attacker={} weapon_def={} credited={} count={} feign={} deduplicated=true",victim,attacker,event->GetInt("weapon_def_index",-1),credit,lifeStreaks.Get(attacker),feign));
            }
        }
    }
    int RagdollEffectsFor(CTFRagdoll* ragdoll)
    {
        if(!ragdoll||!V::Enabled.Value||G::Unload||SDK::CleanScreenshot()||ragdoll->m_bFeignDeath())return 0;
        std::lock_guard lock(guard);if(stopped||!loaded)return 0;
        static const int offset=U::NetVars.GetNetVar("CTFRagdoll","m_hPlayer");
        if(offset<=0||offset>=65536){Diagnostic("event=cosmetic_ragdoll result=player_handle_netvar_unavailable");return 0;}
        auto player=ragdoll->m_hPlayer().Get();
        if(!player||!player->IsPlayer())return 0;
        const int index=player->entindex();const auto playerRef=uint32_t(player->GetRefEHandle().ToInt());
        const auto corpseRef=uint32_t(ragdoll->GetRefEHandle().ToInt());const double now=Now();
        auto allowed=[&](const GoldDeath& death)
        {
            auto peer=remote.find(death.peer);const bool fresh=peer!=remote.end()&&now-peer->second.time<=10;
            return SkinRender::CorpseAllowed(death.player,playerRef,death.shared,V::Networking.Value&&V::Receive.Value,fresh);
        };
        if(auto cached=corpses.find(corpseRef);cached!=corpses.end())
        {if(!allowed(cached->second.death))return 0;cached->second.seen=now;return cached->second.death.effects;}
        auto it=goldDeaths.find(index);
        if(it==goldDeaths.end()||Now()>it->second.expires||(it->second.shared&&(!V::Networking.Value||!V::Receive.Value)))
        {Diagnostic(std::format("event=cosmetic_ragdoll victim={} result=no_fresh_death_mark player_handle=true",index));return 0;}
        if(!allowed(it->second))return 0;
        if(auto birth=ragdollBirths.find(corpseRef);birth!=ragdollBirths.end()&&now-birth->second>3)return 0;
        auto current=player->As<CTFPlayer>()->m_hRagdoll().Get();if(current&&current!=ragdoll)return 0;
        if(corpses.size()>=256)corpses.erase(corpses.begin());
        corpses[corpseRef]={it->second,now,false,{},it->second.origin};
        WriteDiagnostic(std::format("event=cosmetic_ragdoll victim={} effects={} matched_death=true player_handle=true client_only=true",index,it->second.effects));return it->second.effects;
    }
    void RagdollCreated(CTFRagdoll* ragdoll,int effects)
    {
        if(!ragdoll)return;std::lock_guard lock(guard);
        if(ragdollBirths.size()>=256)ragdollBirths.erase(ragdollBirths.begin());
        ragdollBirths[uint32_t(ragdoll->GetRefEHandle().ToInt())]=Now();
        if(!effects)return;
        if(auto it=corpses.find(uint32_t(ragdoll->GetRefEHandle().ToInt()));it!=corpses.end())
        {it->second.native=true;WriteDiagnostic(std::format("event=cosmetic_ragdoll_created effects={} native_physics=true sound_started={}",effects,it->second.sound.started));}
    }
    void ObserveDeathSound(int entity,bool started,int guid,const char* source)
    {
        std::lock_guard lock(guard);if(stopped)return;
        auto client=I::ClientEntityList->GetClientEntity(entity);
        auto ragdoll=client?client->As<CBaseEntity>():nullptr;
        if(!ragdoll||ragdoll->GetClassID()!=ETFClassID::CTFRagdoll)return;
        auto it=corpses.find(uint32_t(ragdoll->GetRefEHandle().ToInt()));if(it==corpses.end())return;
        it->second.sound.Complete(started);
        WriteDiagnostic(std::format("event=cosmetic_death_sound corpse={} effects={} source={} submitted={} started={} guid={} once_per_corpse=true origin=[{:.1f},{:.1f},{:.1f}] render_independent=true",
            entity,it->second.death.effects,source,it->second.sound.submitted,started,guid,it->second.origin.x,it->second.origin.y,it->second.origin.z));
    }
    std::optional<bool> BeginDeathSound(int entity)
    {
        std::lock_guard lock(guard);if(stopped||!V::Enabled.Value)return {};
        auto client=I::ClientEntityList->GetClientEntity(entity);auto body=client?client->As<CBaseEntity>():nullptr;
        if(!body||body->GetClassID()!=ETFClassID::CTFRagdoll)return {};
        if(!RagdollEffectsFor(body->As<CTFRagdoll>()))return {};
        auto it=corpses.find(uint32_t(body->GetRefEHandle().ToInt()));if(it==corpses.end())return {};
        return it->second.sound.Begin(Now(),it->second.death.expires);
    }
    std::optional<Vec3> DeathSoundOrigin(int entity)
    {
        std::lock_guard lock(guard);auto client=I::ClientEntityList->GetClientEntity(entity);
        auto body=client?client->As<CBaseEntity>():nullptr;if(!body||body->GetClassID()!=ETFClassID::CTFRagdoll)return {};
        auto it=corpses.find(uint32_t(body->GetRefEHandle().ToInt()));if(it==corpses.end())return {};
        return it->second.origin;
    }
    IMaterial* GoldStatueMaterial()
    {
        // A dedicated, retained material avoids the player's live burn/cloak
        // proxies and verifies the actual texture, not just IsErrorMaterial.
        // Missing/modded assets get a gold-tinted white fallback, never a
        // checkerboard. The shader keeps the authored gold lighting values.
        if(!statueGoldMaterial)
        {
            auto texture=I::MaterialSystem->FindTexture("models/player/shared/gold_player",TEXTURE_GROUP_MODEL,false);
            const bool authored=texture&&!texture->IsError();
            auto env=I::MaterialSystem->FindTexture("cubemaps/cubemap_gold001",TEXTURE_GROUP_CUBE_MAP,false);
            const bool reflection=env&&!env->IsError();
            auto kv=new KeyValues("VertexLitGeneric");
            kv->SetString("$basetexture",authored?"models/player/shared/gold_player":"vgui/white_additive");
            if(!authored)kv->SetString("$color2","[0.85 0.55 0.08]");
            kv->SetInt("$model",1);kv->SetInt("$phong",1);kv->SetFloat("$phongexponent",10);kv->SetFloat("$phongboost",1);
            kv->SetString("$phongfresnelranges","[.25 1 4]");kv->SetInt("$basemapalphaphongmask",1);
            kv->SetInt("$rimlight",1);kv->SetFloat("$rimlightexponent",4);kv->SetFloat("$rimlightboost",2);
            if(reflection){kv->SetString("$envmap","cubemaps/cubemap_gold001");kv->SetString("$envmaptint","[1.5 1.2 .2]");}
            statueGoldMaterial=F::Materials.Create("nikogram_gold_statue_v102",kv);
            bool found=false;auto base=statueGoldMaterial?statueGoldMaterial->FindVar("$basetexture",&found,false):nullptr;
            auto actual=found&&base?base->GetTextureValue():nullptr;
            const bool valid=statueGoldMaterial&&!statueGoldMaterial->IsErrorMaterial()&&actual&&!actual->IsError();
            WriteDiagnostic(std::format("event=cosmetic_statue_material authored_texture={} reflection={} valid={} retained=true live_player_proxies=false",authored,reflection,valid));
            if(!valid){F::Materials.Remove(statueGoldMaterial);statueGoldMaterial=nullptr;return nullptr;}
        }
        // Material reloads clear the visual-system registry, not our owned ref.
        F::Materials.m_mMatList[statueGoldMaterial];return statueGoldMaterial;
    }
    bool PrepareDeathDraw(const DrawModelState_t& state,const ModelRenderInfo_t& info,matrix3x4* bones,
        std::array<matrix3x4,MAXSTUDIOBONES>& output,matrix3x4*& drawBones,IMaterial*& material,bool effectPass)
    {
        drawBones=bones;material=nullptr;
        if(!V::Enabled.Value||G::Unload||!I::EngineClient->IsInGame()||SDK::CleanScreenshot()||!state.m_pStudioHdr)return false;
        auto client=I::ClientEntityList->GetClientEntity(info.entity_index);auto entity=client?client->As<CBaseEntity>():nullptr;
        if(!entity&&info.pRenderable)if(auto unknown=info.pRenderable->GetIClientUnknown())entity=unknown->GetBaseEntity();
        if(!entity)return false;
        auto root=entity;std::array<CBaseEntity*,8> visited{};int depth=0;
        while(root&&root->GetClassID()!=ETFClassID::CTFRagdoll&&depth<8)
        {if(std::find(visited.begin(),visited.begin()+depth,root)!=visited.begin()+depth)return false;visited[depth++]=root;root=root->GetMoveParent();}
        if(!root||root->GetClassID()!=ETFClassID::CTFRagdoll)return false;
        auto ragdoll=root->As<CTFRagdoll>();const int effects=RagdollEffectsFor(ragdoll);
        if(!(effects&(DeathGold|DeathIce)))return false;
        std::lock_guard lock(guard);const auto corpseRef=uint32_t(ragdoll->GetRefEHandle().ToInt());
        auto corpse=corpses.find(corpseRef);if(corpse==corpses.end())return false;
        if(!effectPass)
        {
            material=effects&DeathGold?GoldStatueMaterial():I::MaterialSystem->FindMaterial("models/player/shared/ice_player.vmt",TEXTURE_GROUP_CLIENT_EFFECTS);
            if(!material||material->IsErrorMaterial()){material=nullptr;Diagnostic("event=cosmetic_death_draw result=authored_material_unavailable");}
        }
        // Never recreate an already-initialized ragdoll. If its death event
        // arrived late, freeze owned render matrices instead of rebuilding or
        // borrowing engine physics objects. Hats use the same corpse identity.
        if(!corpse->second.native&&(effects&DeathGold))
        {
            const int count=state.m_pStudioHdr->numbones;if(count<1||count>MAXSTUDIOBONES)return false;
            std::array<matrix3x4,MAXSTUDIOBONES> scratch{};matrix3x4* source=nullptr;
            const auto acquired=SkinRender::AcquireBones(bones,scratch,count,source,[&](matrix3x4* out,int capacity)
            {auto renderable=state.m_pRenderable?state.m_pRenderable:info.pRenderable;return renderable&&renderable->SetupBones(out,capacity,BONE_USED_BY_ANYTHING,I::GlobalVars->curtime);});
            if(acquired==SkinRender::BoneSource::Unavailable||acquired==SkinRender::BoneSource::InvalidLayout)return false;
            const CorpsePoseKey key{corpseRef,uint32_t(entity->GetRefEHandle().ToInt()),uintptr_t(info.pModel)};
            if(!corpsePoses.contains(key)&&corpsePoses.size()>=512)corpsePoses.erase(corpsePoses.begin());
            auto& pose=corpsePoses[key];const bool first=pose.pose.bones.empty();pose.seen=Now();
            if(!pose.pose.Copy(std::span<const matrix3x4>(source,count),std::span<matrix3x4>(output.data(),count)))return false;
            drawBones=output.data();
            if(first)WriteDiagnostic(std::format("event=cosmetic_death_late_pose effects={} victim={} bones={} wearable={} render_only=true physics_recreated=false",effects,ragdoll->m_hPlayer().Get()->entindex(),count,entity!=root));
        }
        return true;
    }
    std::optional<KillstreakProfile> KillstreakFor(CTFWeaponBase* weapon,bool allowPreview)
    {
        if(!weapon||!V::Enabled.Value||G::Unload||SDK::CleanScreenshot())return {};
        std::lock_guard lock(guard);if(stopped||!loaded)return {};
        auto parent=weapon->m_hOwner().Get();if(!parent||!parent->IsPlayer())return {};
        auto player=parent->As<CTFPlayer>();auto local=H::Entities.GetLocal();
        if(!local||!player->IsAlive()||player->IsDormant()||player->m_hActiveWeapon().Get()!=weapon)return {};
        const int id=weapon->m_iItemDefinitionIndex(),cls=player->m_iClass();Selection selection;
        if(player==local)selection=Get(preset,catalog.Canonical(id),cls);
        else
        {
            if(!V::Networking.Value||!V::Receive.Value)return {};auto it=remote.find(PlayerID(player));
            if(it==remote.end()||it->second.cls!=cls||Now()-it->second.time>10)return {};
            const auto cached=it->second.killstreakWeapons.Find(catalog.Canonical(id),cls);if(!cached)return {};
            selection=*cached;
            if(catalog.Canonical(it->second.weapon)!=catalog.Canonical(id))
                Diagnostic(std::format("event=killstreak_cached_weapon item={} tier={} waiting_for_style=false",id,selection.tier));
        }
        if(!selection.enabled||!selection.tier||!Compatible(id,cls,selection))return {};
        const int tracked=lifeStreaks.Get(uint32_t(player->GetRefEHandle().ToInt()));int native=-1;
        // Read the native streak array, not the SDK's legacy void* accessor
        // (which would interpret the first two counters as a pointer).
        static const int offset=U::NetVars.GetNetVar("CTFPlayer","m_nStreaks");
        if(offset>0&&offset<65536)std::memcpy(&native,reinterpret_cast<const char*>(player)+offset,sizeof(native));
        const bool preview=allowPreview&&player==local&&selection.tier==3&&selection.preview;
        int count=SkinRender::KillstreakCount(tracked,native,preview,selection.previewCount,allowPreview);
        if(player!=local)
            if(auto it=remote.find(PlayerID(player));it!=remote.end())
                if(const int shared=it->second.streak.Value(tracked);shared>=0)count=shared;
        Diagnostic(std::format("event=killstreak_profile item={} tier={} sheen={} effect={} count={} preview={} native_render_only=true",id,selection.tier,selection.sheen,selection.effect,count,preview));
        return KillstreakProfile{weapon,selection.tier,selection.sheen,selection.effect,count,preview};
    }
    std::optional<int> LocalKillstreakDisplay()
    {
        if(!I::EngineClient->IsInGame()||I::EngineClient->IsPlayingDemo())return {};
        auto local=H::Entities.GetLocal();if(!local)return {};
        auto weapon=local->m_hActiveWeapon().Get();if(!weapon)return {};
        if(auto profile=KillstreakFor(weapon->As<CTFWeaponBase>(),false))return profile->count;
        return {};
    }
    std::optional<int> PlayerKillstreakDisplay(int index)
    {
        if(index==I::EngineClient->GetLocalPlayer())return LocalKillstreakDisplay();
        if(index<1||index>std::min(I::EngineClient->GetMaxClients(),128)||!V::Enabled.Value||!V::Networking.Value||!V::Receive.Value||G::Unload||!I::EngineClient->IsInGame()||I::EngineClient->IsPlayingDemo()||SDK::CleanScreenshot())return {};
        std::lock_guard lock(guard);if(stopped||!loaded)return {};
        auto client=I::ClientEntityList->GetClientEntity(index);auto player=client?client->As<CTFPlayer>():nullptr;
        if(!player||!player->IsPlayer())return {};
        auto it=remote.find(PlayerID(player));if(it==remote.end()||Now()-it->second.time>10)return {};
        const auto& entry=it->second;if(entry.cls!=player->m_iClass())return {};
        auto held=player->m_hActiveWeapon().Get();
        if(!held)return {};
        const int item=held->As<CTFWeaponBase>()->m_iItemDefinitionIndex();
        const auto cached=entry.killstreakWeapons.Find(catalog.Canonical(item),entry.cls);
        if(!cached||!cached->enabled||!cached->tier||!Compatible(item,entry.cls,*cached))return {};
        if(!player->IsAlive())return 0;
        const int shared=entry.streak.Value(lifeStreaks.Get(uint32_t(player->GetRefEHandle().ToInt())));
        if(shared>=0)return shared;
        if(auto profile=KillstreakFor(held->As<CTFWeaponBase>(),false))return profile->count;
        return {};
    }
    void ObserveKillstreakUI(const char* surface,int count,bool remotePlayer)
    {
        std::lock_guard lock(guard);
        Diagnostic(std::format("event=killstreak_ui surface={} count={} remote={} preview=false server_unchanged=true",surface,count,remotePlayer));
    }
    void SharedKillstreakMilestone(int index,int count)
    {
        if(index==I::EngineClient->GetLocalPlayer()||!PlayerKillstreakDisplay(index)||count<=0)return;
        std::lock_guard lock(guard);
        auto client=I::ClientEntityList->GetClientEntity(index);auto player=client?client->As<CTFPlayer>():nullptr;
        if(!player||!player->IsPlayer())return;
        auto it=remote.find(PlayerID(player));if(it==remote.end())return;
        const auto rules=I::TFGameRules();const bool mvm=rules&&rules->m_bPlayingMannVsMachine();
        if(!it->second.milestone.Due(count,mvm))return;
        const bool played=PlayKillstreakMilestoneSound();
        Diagnostic(std::format("event=killstreak_milestone_sound count={} remote=true played={} local_playback_only=true",count,played));
    }

    void SessionEvent(IGameEvent* event)
    {
        if(!event)return;
        const std::string_view name=event->GetName();
        if(name!="client_beginconnect"&&name!="client_disconnect"&&name!="game_newmap")return;
        std::lock_guard lock(guard);if(stopped)return;
        // Model indices belong to the engine's current registration table.
        // Preserve selections, but do not carry index/pointer or jiggle state
        // into another map (including rejoining the same local server).
        // Release only registrations still identified as our exact model;
        // game_newmap may run after the previous table has been purged.
        size_t released=0;
        for(const auto& [path,model]:models)if(model.owned&&model.index!=-1)
        {
            const auto current=I::ModelInfoClient->GetModel(model.index);
            const auto currentName=current?I::ModelInfoClient->GetModelName(current):nullptr;
            if(currentName&&path==currentName)
            {I::ModelInfoClient->ReleaseDynamicModel(model.index);++released;}
        }
        const auto registrations=models.size();models.clear();jiggleInstances.clear();
        lastTick=-100;
        Diagnostic(std::format("event=session_cosmetic_models_reset trigger={} registrations={} released={} saved_selections_preserved=true",name,registrations,released));
        // No interface acquisition or discovery here. Rotate all handshake
        // nonces when changing/rejoining a session, including the same map.
        if(server||transport){NetworkEvent("game_connection_reset");Disconnect();}
    }
    void Tick()
    {
        std::lock_guard lock(guard);if(stopped)return;UpdateUnusualParticles();UpdateKillstreakParticles();
        const double corpseNow=Now();
        // Prune before updating seen timestamps. The old ordering compared an
        // earlier frame timestamp with a just-refreshed one, deleted the live
        // corpse and lost BOTH its one-shot sound gate and frozen bone pose.
        std::erase_if(corpses,[&](const auto& value){return SkinRender::CorpseExpired(V::Enabled.Value,corpseNow,value.second.seen);});
        std::erase_if(corpsePoses,[&](const auto& value){return !corpses.contains(std::get<0>(value.first));});
        std::erase_if(ragdollBirths,[&](const auto& value){return SkinRender::CorpseExpired(V::Enabled.Value,corpseNow,value.second);});
        // The death event may arrive after CreateTFRagdoll. Bind through the
        // victim's current full-serial handle even when the body is offscreen.
        if(V::Enabled.Value&&!G::Unload&&I::EngineClient->IsInGame()&&!SDK::CleanScreenshot())
        {
            for(const auto& [index,death]:goldDeaths)
            {
                if(corpseNow>death.expires)continue;
                auto client=I::ClientEntityList->GetClientEntity(index);auto player=client?client->As<CTFPlayer>():nullptr;
                if(!player||!player->IsPlayer()||uint32_t(player->GetRefEHandle().ToInt())!=death.player)continue;
                auto body=player->m_hRagdoll().Get();if(!body||body->GetClassID()!=ETFClassID::CTFRagdoll)continue;
                const int effects=RagdollEffectsFor(body->As<CTFRagdoll>());
                auto corpse=corpses.find(uint32_t(body->GetRefEHandle().ToInt()));
                if((effects&(DeathGold|DeathIce))&&corpse!=corpses.end()&&corpse->second.sound.Begin(corpseNow,death.expires))
                    PlayCosmeticDeathSound(body->entindex(),effects);
            }
        }
        if(!V::Enabled.Value||G::Unload||!I::EngineClient->IsInGame())if(!paintTextures.empty()||!paintCompatibility.empty())ClearPaintTextures();
        double now=Now();if(now-lastTick<.2)return;lastTick=now;
        try
        {
            if(!diagnosticStarted){diagnosticStarted=true;WriteDiagnostic("event=run_start network_diagnostics=5 shared_actual_killstreaks=true preview_counts_local_only=true context=replicated_human_roster transport_context=initialized_tf2 local_host_supported=true defaults_unchanged=true local_logs_only=true");}
            if(!loaded){if(now-lastSave>=5){lastSave=now;CatalogLoad();}}
            if(!loaded){netLog.state="catalog_unavailable";NetworkReport(now);return;}
            WarmEffectModels();
            // Placement speech is tied to the actual sapper's full handle,
            // never rendering frequency or a repeated kill/event delivery.
            static std::map<uint32_t,double> placedVoices;static double lastSapperScan=-100;
            if(now-lastSapperScan>=.2)
            {
                lastSapperScan=now;std::set<uint32_t> live;
                if(V::Enabled.Value&&!G::Unload&&!SDK::CleanScreenshot()&&I::EngineClient->IsInGame())
                    for(int index=1;index<=std::min(I::ClientEntityList->GetHighestEntityIndex(),4096);++index)
                    {
                        auto client=I::ClientEntityList->GetClientEntity(index);auto object=client?client->As<CBaseEntity>():nullptr;
                        if(!object||object->GetClassID()!=ETFClassID::CObjectSapper||object->IsDormant()||object->As<CBaseObject>()->m_bPlacing())continue;
                        auto builder=object->As<CBaseObject>()->m_hBuilder().Get();auto owner=builder&&builder->IsPlayer()?builder->As<CTFPlayer>():nullptr;
                        auto sapper=EquipmentWeapon(owner);Selection selected;if(!CosmeticSelection(sapper,owner,selected))continue;
                        auto original=catalog.Find(sapper->m_iItemDefinitionIndex());auto chosen=original?Appearance(*original,8,selected):nullptr;
                        if(!chosen||chosen->sapperVoice!=1||original->sapperVoice==1)continue;
                        auto handle=uint32_t(object->GetRefEHandle().ToInt());live.insert(handle);
                        const bool first=!placedVoices.contains(handle);
                        if(first||now-placedVoices.at(handle)>=18)
                        {
                            const char* script=first?"Psap.Attached":"PSap.Hacking";const bool played=PlaySapperVoice(index,script);
                            placedVoices[handle]=now;Diagnostic(std::format("event=placed_sapper_voice entity={} script={} submitted={} remote={} client_only=true",index,script,played,owner!=H::Entities.GetLocal()));
                        }
                    }
                std::erase_if(placedVoices,[&](const auto& row){return !live.contains(row.first);});
            }
            auto local=H::Entities.GetLocal();auto held=H::Entities.GetWeapon();
            // AP-Sap voice logic is server-driven for real items. Cosmetic-only
            // selections use installed sound scripts and a local recipient filter.
            // Never broadcast speech or alter the real sapper's attributes.
            static uint32_t voiceWeapon=0;static double nextVoice=0;
            const bool cosmeticVoice=local&&held&&local->IsAlive()&&local->m_iClass()==8&&V::Enabled.Value
                &&!G::Unload&&!SDK::CleanScreenshot()&&I::EngineClient->IsInGame()
                &&CosmeticVoiceAttribute("sapper_voice_pak",held).value_or(0)==1
                &&catalog.Find(held->m_iItemDefinitionIndex())&&catalog.Find(held->m_iItemDefinitionIndex())->sapperVoice!=1;
            if(!cosmeticVoice)
            {
                if(voiceWeapon&&local&&local->IsAlive()&&V::Enabled.Value){const bool played=PlaySapperVoice(local->entindex(),"PSap.Holster");Diagnostic(std::format("event=sapper_voice script=PSap.Holster submitted={} transition=holster",played));}
                voiceWeapon=0;
            }
            else
            {
                const auto handle=uint32_t(held->GetRefEHandle().ToInt());const bool equip=voiceWeapon!=handle;
                if(equip||now>=nextVoice)
                {
                    const char* script=equip?"PSap.Deploy":"Psap.Idle";
                    const bool played=PlaySapperVoice(local->entindex(),script);
                    voiceWeapon=handle;nextVoice=now+18;
                    Diagnostic(std::format("event=sapper_voice script={} submitted={} local_only=true cooldown_seconds=18",script,played));
                }
            }
            // Real AP-Sap speech is server-driven. Render cosmetic peer speech
            // locally at that player's entity, never broadcast it to the server.
            static std::map<uint32_t,std::pair<uint32_t,double>> peerVoices;
            std::set<uint32_t> observedVoices;
            if(local&&V::Enabled.Value&&V::Networking.Value&&V::Receive.Value&&!G::Unload&&!SDK::CleanScreenshot())
                for(int index=1;index<=std::clamp(I::EngineClient->GetMaxClients(),0,128);++index)
                {
                    auto client=I::ClientEntityList->GetClientEntity(index);auto player=client?client->As<CTFPlayer>():nullptr;
                    if(!player||!player->IsPlayer()||player==local||!player->IsAlive()||player->IsDormant()||player->m_iClass()!=8)continue;
                    const auto id=PlayerID(player);auto consent=peers.find(id);auto style=remote.find(id);
                    if(consent==peers.end()||!consent->second.ready||!(consent->second.flags&1)||style==remote.end()||style->second.cls!=8||now-style->second.time>10)continue;
                    const auto ownerHandle=uint32_t(player->GetRefEHandle().ToInt());observedVoices.insert(ownerHandle);
                    auto base=player->m_hActiveWeapon().Get();auto weapon=base?base->As<CTFWeaponBase>():nullptr;
                    auto item=weapon?catalog.Find(weapon->m_iItemDefinitionIndex()):nullptr;
                    auto selection=item?style->second.killstreakWeapons.Find(catalog.Canonical(item->id),8):nullptr;
                    auto chosen=item&&selection&&selection->enabled&&Compatible(item->id,8,*selection)?Appearance(*item,8,*selection):nullptr;
                    const bool voice=chosen&&chosen->sapperVoice==1&&item->sapperVoice!=1;
                    auto previous=peerVoices.find(ownerHandle);
                    if(!voice)
                    {
                        if(previous!=peerVoices.end())
                        {PlaySapperVoice(index,"PSap.Holster");peerVoices.erase(previous);}
                        continue;
                    }
                    const auto handle=uint32_t(weapon->GetRefEHandle().ToInt());
                    const bool equip=previous==peerVoices.end()||previous->second.first!=handle;
                    if(equip||now>=previous->second.second)
                    {
                        const char* script=equip?"PSap.Deploy":"Psap.Idle";const bool played=PlaySapperVoice(index,script);
                        peerVoices[ownerHandle]={handle,now+18};
                        Diagnostic(std::format("event=sapper_voice script={} submitted={} remote=true playback_local_only=true cooldown_seconds=18",script,played));
                    }
                }
            std::erase_if(peerVoices,[&](const auto& row){return !observedVoices.contains(row.first);});
            if(local&&held)
            {
                int id=held->m_iItemDefinitionIndex(),cls=local->m_iClass();auto item=catalog.Find(id);auto s=Get(preset,catalog.Canonical(id),cls);
                Diagnostic(std::format("event=active_selection enabled={} item={} canonical={} class={} apply={} reskin={} australium={} festive={} compatible={} expected_model={}",
                    V::Enabled.Value,id,catalog.Canonical(id),cls,s.enabled,s.reskin,s.australium,s.festive,Compatible(id,cls,s),item?item->Model(cls):"<uncataloged>"));
                if(V::Enabled.Value&&now-lastReport>=5)
                {
                    WriteDiagnostic(std::format("event=summary interval_seconds={:.1f} item={} class={} apply={} reskin={} australium={} compatible={} viewmodel_calls={} verified_viewmodels={} matching_model_draws={} selection_matches={} skin_overrides={} model_overrides={} skipped={} sound_overrides={} tracer_overrides={} muzzle_overrides={} accessory_overrides={} last_draw_item={} last_result={} last_model={}",
                        lastReport?now-lastReport:0.,id,cls,s.enabled,s.reskin,s.australium,Compatible(id,cls,s),telemetry.viewmodels,telemetry.verified,telemetry.draws,telemetry.matches,telemetry.skin,telemetry.model,telemetry.skipped,telemetry.sounds,telemetry.tracers,telemetry.muzzles,telemetry.accessories,telemetry.lastItem,telemetry.result,telemetry.path));
                    auto result=telemetry.result,path=telemetry.path;int lastItem=telemetry.lastItem;telemetry={};telemetry.result=result;telemetry.path=path;telemetry.lastItem=lastItem;lastReport=now;
                }
            }
            if(dirty && now-lastSave>=2){if(Save(Directory()/"current.json"))dirty=false;lastSave=now;}
            Networking(now);
            NetworkReport(now);
        }
        catch(const std::exception& e){status="Skin changer paused: "+std::string(e.what());Diagnostic(status);netLog.state="tick_exception";NetworkReport(now);}
    }
    std::optional<HandAnimationProfile> HandAnimationsFor(CTFWeaponBase* weapon)
    {
        std::lock_guard lock(guard);
        if(stopped||!loaded||!V::Enabled.Value||!weapon||G::Unload||SDK::CleanScreenshot())return std::nullopt;
        auto local=H::Entities.GetLocal();
        if(!local||!local->IsAlive()||local->m_hActiveWeapon().Get()!=weapon||weapon->m_hOwner().Get()!=local)return std::nullopt;
        int id=weapon->m_iItemDefinitionIndex(),cls=local->m_iClass(),team=local->m_iTeamNum();
        auto item=catalog.Find(id);if(!item)return std::nullopt;
        auto selection=Get(preset,catalog.Canonical(id),cls);
        if(!selection.enabled||!Compatible(id,cls,selection))return std::nullopt;
        auto chosen=Appearance(*item,cls,selection);
        if(!chosen)return std::nullopt;
        auto animations=chosen->Animations(team);
        if(cls==8&&(chosen->animSlot=="MELEE_ALLCLASS"||chosen->id==154))
            SkinRender::SpyAllClassHands(animations);
        if(animations==item->Animations(team))return std::nullopt;
        auto mappings=SkinRender::HandActivityMap(item->animSlot,chosen->animSlot,item->Animations(team),animations);
        if(mappings.empty())return std::nullopt;
        return HandAnimationProfile{id,chosen->id,std::move(mappings)};
    }
    bool BuildingMenuPipBoy(void* entity)
    {
        auto local=H::Entities.GetLocal();return V::Enabled.Value&&V::PipBoy.Value&&!G::Unload&&!SDK::CleanScreenshot()
            &&local&&entity==local&&local->m_iClass()==9&&I::EngineClient->IsInGame();
    }
    bool PipBoyFor(CTFPlayer* player)
    {
        if(!player||!player->IsPlayer()||player->m_iClass()!=9||!V::Enabled.Value||G::Unload||SDK::CleanScreenshot()||!I::EngineClient->IsInGame())return false;
        if(player==H::Entities.GetLocal())return V::PipBoy.Value;
        if(!V::Networking.Value||!V::Receive.Value)return false;
        std::lock_guard lock(guard);if(stopped||!loaded)return false;
        const auto id=PlayerID(player);auto peer=peers.find(id);auto appearance=sharedPlayers.find(id);
        return peer!=peers.end()&&appearance!=sharedPlayers.end()
            &&SkinProtocol::PlayerAppearanceAllowed(V::Networking.Value,V::Receive.Value,peer->second.ready,peer->second.flags&1,
                appearance->second.cls,player->m_iClass(),Now(),appearance->second.time)&&appearance->second.pipBoy;
    }
    bool AuthenticAnimationsFor(CTFPlayer* player)
    {
        if(!player||!V::Enabled.Value||G::Unload||SDK::CleanScreenshot())return false;
        if(player==H::Entities.GetLocal())return V::ThirdPersonAnimations.Value==0;
        if(!V::Networking.Value||!V::Receive.Value)return false;
        std::lock_guard lock(guard);if(stopped||!loaded)return false;
        const auto id=PlayerID(player);auto peer=peers.find(id);if(peer==peers.end()||!peer->second.ready||!(peer->second.flags&1))return false;
        auto appearance=sharedPlayers.find(id);
        if(appearance!=sharedPlayers.end()&&SkinProtocol::PlayerAppearanceAllowed(V::Networking.Value,V::Receive.Value,peer->second.ready,peer->second.flags&1,
            appearance->second.cls,player->m_iClass(),Now(),appearance->second.time))
            return appearance->second.authenticAnimations;
        return V::ThirdPersonAnimations.Value==0; // retain older peers' existing animation behavior
    }
    std::optional<float> CosmeticVoiceAttribute(const char* name,void* entity)
    {
        if(!name||!entity||(!std::string_view(name).starts_with("sapper_voice_pak"))||!V::Enabled.Value||G::Unload||SDK::CleanScreenshot())return {};
        const std::string_view attribute=name;if(attribute!="sapper_voice_pak"&&attribute!="sapper_voice_pak_idle_wait")return {};
        // Locate by actual entity identity; never cast an arbitrary econ pointer.
        auto local=H::Entities.GetLocal();if(!local)return {};
        CTFWeaponBase* weapon=nullptr;CTFPlayer* owner=nullptr;
        for(int index=1;index<=std::clamp(I::EngineClient->GetMaxClients(),0,128)&&!weapon;++index)
        {
            auto client=I::ClientEntityList->GetClientEntity(index);auto player=client?client->As<CTFPlayer>():nullptr;
            if(!player||!player->IsPlayer()||player->m_iClass()!=8)continue;
            if(player!=local&&(!V::Networking.Value||!V::Receive.Value))continue;
            for(auto handle:player->m_hMyWeapons())if(handle.Get()==entity){weapon=handle.Get();owner=player;break;}
        }
        if(!weapon)return {};std::lock_guard lock(guard);if(stopped||!loaded)return {};
        const int id=weapon->m_iItemDefinitionIndex();auto original=catalog.Find(id);
        if(!original||original->slot!="building"||(original->weaponType!="tf_weapon_sapper"&&original->weaponType!="tf_weapon_builder"))return {};
        Selection selection;
        if(owner==local)selection=Get(preset,catalog.Canonical(id),8);
        else
        {
            auto peer=remote.find(PlayerID(owner));if(peer==remote.end()||Now()-peer->second.time>10)return {};
            auto cached=peer->second.killstreakWeapons.Find(catalog.Canonical(id),8);if(!cached)return {};selection=*cached;
        }
        if(!selection.enabled||!Compatible(id,8,selection))return {};
        auto chosen=Appearance(*original,8,selection);if(!chosen||chosen->slot!="building")return {};
        return attribute=="sapper_voice_pak"?float(chosen->sapperVoice):chosen->sapperVoiceWait;
    }
    bool PreparePipBoyDraw(const DrawModelState_t& state,const ModelRenderInfo_t& info,matrix3x4* bones,
        DrawModelState_t& outputState,ModelRenderInfo_t& outputInfo,std::array<matrix3x4,MAXSTUDIOBONES>& output,bool localViewmodelDraw)
    {
        auto local=H::Entities.GetLocal();if(!local||!info.pModel||!state.m_pStudioHdr)return false;
        const char* path=I::ModelInfoClient->GetModelName(info.pModel);if(!path)return false;
        auto client=I::ClientEntityList->GetClientEntity(info.entity_index);auto entity=client?client->As<CBaseEntity>():nullptr;
        if(!entity&&info.pRenderable)if(auto unknown=info.pRenderable->GetIClientUnknown())entity=unknown->GetBaseEntity();
        const bool world=entity&&entity->IsPlayer()&&entity->As<CTFPlayer>()->IsAlive()&&PipBoyFor(entity->As<CTFPlayer>());
        const bool hands=local->IsAlive()&&PipBoyFor(local)&&std::string_view(path)=="models/weapons/c_models/c_engineer_arms.mdl"&&localViewmodelDraw;
        if(!world&&!hands)return false;
        constexpr const char* pipPath="models/workshop_partner/player/items/engineer/bet_pb/bet_pb.mdl";
        // Do not double-draw an inventory Pip-Boy already attached by TF2.
        std::set<CBaseEntity*> visited;
        auto parent=world?entity:local->m_hViewModel().Get();if(!parent)return false;
        for(auto child=parent->FirstMoveChild();child&&visited.size()<64&&visited.insert(child).second;child=child->NextMovePeer())
        {auto model=child->GetModel();auto name=model?I::ModelInfoClient->GetModelName(model):nullptr;if(name&&std::string_view(name)==pipPath)return false;}
        std::lock_guard lock(guard);if(stopped||!loaded)return false;
        auto model=LoadModel(pipPath,F::Chams.m_bRendering||F::Glow.m_bRendering);if(!model)return false;
        auto target=I::ModelInfoClient->GetStudiomodel(model);auto source=state.m_pStudioHdr;
        if(!target||target->numbones<1||target->numbones>MAXSTUDIOBONES||source->numbones<1||source->numbones>MAXSTUDIOBONES||target->numflexdesc)return false;
        std::array<matrix3x4,MAXSTUDIOBONES> scratch;matrix3x4* sourceBones=nullptr;
        const auto acquired=SkinRender::AcquireBones(bones,scratch,source->numbones,sourceBones,[&](matrix3x4* out,int capacity)
        {auto renderable=state.m_pRenderable?state.m_pRenderable:info.pRenderable;return renderable&&renderable->SetupBones(out,capacity,BONE_USED_BY_ANYTHING,I::GlobalVars->curtime);});
        if(acquired==SkinRender::BoneSource::Unavailable||acquired==SkinRender::BoneSource::InvalidLayout)return false;
        std::vector<std::string> sourceStorage,targetStorage;std::vector<std::string_view> sourceNames,targetNames;
        std::vector<int> parents,procedures,mapping;
        for(int n=0;n<source->numbones;++n){auto name=BoneName(source,n);if(!name)return false;sourceStorage.push_back(*name);}
        for(int n=0;n<target->numbones;++n){auto name=BoneName(target,n);if(!name)return false;targetStorage.push_back(*name);parents.push_back(target->pBone(n)->parent);procedures.push_back(target->pBone(n)->proctype);}
        for(const auto& name:sourceStorage)sourceNames.push_back(name);for(const auto& name:targetStorage)targetNames.push_back(name);
        if(!SkinRender::AnchoredBoneMap(sourceNames,targetNames,parents,procedures,mapping))
        {Diagnostic(std::format("event=pipboy_draw result=unsupported_skeleton first_person={}",hands));return false;}
        std::array<bool,MAXSTUDIOBONES> resolved{};int remaining=target->numbones;
        for(int n=0;n<target->numbones;++n)if(mapping[n]>=0)
        {
            if(hands){if(!SkinRender::RestTransform(sourceBones[mapping[n]],source->pBone(mapping[n])->poseToBone,target->pBone(n)->poseToBone,output[n]))return false;}
            else std::memcpy(output[n],sourceBones[mapping[n]],sizeof(matrix3x4));
            resolved[n]=true;--remaining;
        }
        for(int pass=0;remaining&&pass<target->numbones;++pass)for(int n=0;n<target->numbones;++n)if(!resolved[n])
        {const int parent=parents[n];if(parent>=0&&parent<target->numbones&&resolved[parent])
        {if(!SkinRender::RestTransform(output[parent],target->pBone(parent)->poseToBone,target->pBone(n)->poseToBone,output[n]))return false;resolved[n]=true;--remaining;}
        else for(int child=0;child<target->numbones;++child)if(parents[child]==n&&resolved[child])
        {if(!SkinRender::RestTransform(output[child],target->pBone(child)->poseToBone,target->pBone(n)->poseToBone,output[n]))return false;resolved[n]=true;--remaining;break;}}
        if(remaining)return false;
        auto cache=static_cast<IMDLCache*>(I::MDLCache);if(!cache)return false;const auto handle=I::ModelInfoClient->GetCacheHandle(model);if(handle==MDLHANDLE_INVALID)return false;
        auto hardware=cache->GetHardwareData(handle);if(!hardware||!hardware->m_pLODs||hardware->m_NumStudioMeshes<1)return false;
        const int lod=SkinRender::ReplacementLOD(hardware->m_RootLOD,hardware->m_NumLODs,hardware->m_RootLOD,false);if(lod<0)return false;
        auto& level=hardware->m_pLODs[lod];if(!level.m_pMeshData||level.numMaterials<1||!level.ppMaterials)return false;
        outputState=state;outputState.m_pStudioHdr=target;outputState.m_pStudioHWData=hardware;outputState.m_decals=STUDIORENDER_DECAL_INVALID;outputState.m_lod=lod;
        outputInfo=info;outputInfo.pModel=model;outputInfo.skin=0;outputInfo.body=0;outputInfo.hitboxset=0;outputInfo.instance=MODEL_INSTANCE_INVALID;
        Diagnostic(std::format("event=pipboy_draw result=prepared first_person={} remote={} render_only=true",hands,world&&entity!=local));return true;
    }
    const model_t* HudPaintModel(const HudAppearance& appearance)
    {
        std::lock_guard lock(guard);if(stopped||!loaded||G::Unload)return nullptr;
        // Draw-time lookup is ready-only; model loading is warmed by Tick or
        // the native HUD callback, never synchronously repeated in this draw.
        // Stock meshes may already be loaded without an entry in our warm cache.
        const int index=I::ModelInfoClient->GetModelIndex(appearance.model.c_str());
        if(index!=-1)if(auto model=I::ModelInfoClient->GetModel(index))return model;
        return LoadModel(appearance.model,true);
    }
    std::optional<HudAppearance> HudAppearanceFor()
    {
        auto local=H::Entities.GetLocal();auto held=local?local->m_hActiveWeapon().Get():nullptr;
        auto weapon=held?held->As<CTFWeaponBase>():nullptr;
        if(!local||!weapon||!local->IsAlive()||local->InCond(TF_COND_DISGUISED)||!V::Enabled.Value||G::Unload||SDK::CleanScreenshot())return {};
        std::lock_guard lock(guard);if(stopped||!loaded)return {};
        const int id=weapon->m_iItemDefinitionIndex(),cls=local->m_iClass(),team=local->m_iTeamNum();
        auto original=catalog.Find(id);if(!original||(team!=2&&team!=3))return {};
        auto selection=Get(preset,catalog.Canonical(id),cls);if(!selection.enabled||!Compatible(id,cls,selection))return {};
        auto chosen=Appearance(*original,cls,selection);if(!chosen||chosen->Model(cls).empty())return {};
        HudAppearance result{id,chosen->id,cls,team,
            SkinRender::HudPreviewSkin(*chosen,team,selection.australium),
            uint32_t(local->m_hActiveWeapon().ToInt()),selection,chosen->Model(cls),original->ExtraModel(false),{}};
        auto extra=chosen->ExtraModel(false);if(!extra.empty())result.extras.push_back({extra,chosen->ExtraSkin(team)});
        const bool nativeLights=SDK::AttribHookValue(0.f,"is_festivized",weapon)!=0.f;
        if(selection.festivized&&!chosen->festive&&!nativeLights)for(const auto& attachment:chosen->FestivizerAttachments(team))
            if(std::find(result.extras.begin(),result.extras.end(),attachment)==result.extras.end())result.extras.push_back(attachment);
        return result;
    }
    std::optional<HandAnimationProfile> PlayerAnimationsFor(CTFPlayer* player)
    {
        if(!player||!player->IsPlayer()||!player->IsAlive()||player->IsDormant()||player->IsTaunting()||!V::Enabled.Value
            ||!AuthenticAnimationsFor(player)||G::Unload||SDK::CleanScreenshot())return {};
        std::lock_guard lock(guard);if(stopped||!loaded)return {};
        auto held=player->m_hActiveWeapon().Get();auto weapon=held?held->As<CTFWeaponBase>():nullptr;if(!weapon)return {};
        const int id=weapon->m_iItemDefinitionIndex(),cls=player->m_iClass();auto original=catalog.Find(id);if(!original)return {};
        Selection selection;
        if(player==H::Entities.GetLocal())selection=Get(preset,catalog.Canonical(id),cls);
        else
        {
            if(!V::Networking.Value||!V::Receive.Value)return {};
            auto peer=remote.find(PlayerID(player));if(peer==remote.end()||peer->second.cls!=cls||Now()-peer->second.time>10)return {};
            auto cached=peer->second.killstreakWeapons.Find(catalog.Canonical(id),cls);if(!cached)return {};selection=*cached;
        }
        if(!selection.enabled||!Compatible(id,cls,selection))return {};
        auto chosen=Appearance(*original,cls,selection);if(!chosen)return {};
        const auto slot=cls==8&&chosen->id==154?std::string("MELEE_ALLCLASS"):chosen->animSlot;
        auto replacements=SkinRender::PlayerActivityMap(original->animSlot,slot,original->Animations(player->m_iTeamNum()),chosen->Animations(player->m_iTeamNum()));
        if(replacements.empty())return {};return HandAnimationProfile{id,chosen->id,std::move(replacements)};
    }
    std::string KillIconFor(IGameEvent* event)
    {
        // A feigned death still has a death-notice icon. Only streak credit and
        // corpse effects exclude it; the private HUD appearance does not.
        if(!event||!V::Enabled.Value||G::Unload||SDK::CleanScreenshot()||event->GetInt("attacker")==event->GetInt("userid"))return {};
        std::lock_guard lock(guard);if(stopped||!loaded)return {};
        auto client=I::ClientEntityList->GetClientEntity(I::EngineClient->GetPlayerForUserID(event->GetInt("attacker")));
        auto player=client?client->As<CTFPlayer>():nullptr;if(!player||!player->IsPlayer())return {};
        auto held=player->m_hActiveWeapon().Get();auto weapon=held?held->As<CTFWeaponBase>():nullptr;
        auto definition=SkinRender::KillWeaponDefinition(event->GetInt("weapon_def_index",-1),weapon?weapon->m_iItemDefinitionIndex():-1,event->GetInt("weaponid",-1),weapon?weapon->GetWeaponID():-1);
        auto original=definition?catalog.Find(*definition):nullptr;if(!original)return {};
        Selection selection;const int cls=player->m_iClass();
        if(player==H::Entities.GetLocal())selection=Get(preset,catalog.Canonical(original->id),cls);
        else
        {
            if(!V::Networking.Value||!V::Receive.Value)return {};
            auto peer=remote.find(PlayerID(player));if(peer==remote.end()||Now()-peer->second.time>10)return {};
            auto cached=peer->second.killstreakWeapons.Find(catalog.Canonical(original->id),cls);if(!cached)return {};selection=*cached;
        }
        if(!selection.enabled||!Compatible(original->id,cls,selection))return {};
        auto chosen=Appearance(*original,cls,selection);if(!chosen||chosen->id==original->id||chosen->killIcon.empty())return {};
        Diagnostic(std::format("event=cosmetic_kill_icon source={} appearance={} icon={} private_hud_event=true gameplay_unchanged=true",original->id,chosen->id,chosen->killIcon));
        return chosen->killIcon;
    }
    void ObserveHandActivity(int item,int reskin,int input,int output,const char* result,int sourceSequence,int targetSequence,int sequenceCount)
    {
        std::lock_guard lock(guard);if(stopped)return;
        Diagnostic(std::format("event=viewmodel_activity item={} reskin={} input={} output={} result={} source_sequence={} target_sequence={} sequence_count={} native_sequence_selection=true latched=true cosmetic_scopes=render_and_events gameplay_sequence_unchanged=true",item,reskin,input,output,result,sourceSequence,targetSequence,sequenceCount));
    }
    void UnloadDiagnostic(const char* stage)
    {WriteDiagnostic(std::string("event=unload_stage stage=")+stage);}
    bool PrepareUnload()
    {
        WriteDiagnostic("event=unload_stage stage=requested config_enabled_preserved=true");
        unloadRequested.store(true,std::memory_order_release);
        const auto started=GetTickCount64();
        while(!unloadComplete.load(std::memory_order_acquire))
        {
            if(GetTickCount64()-started>=5000)
            {
                WriteDiagnostic("event=unload_stage stage=frame_cleanup_timeout dll_retained=true");
                return false;
            }
            Sleep(10);
        }
        return true;
    }
    void ServiceUnload()
    {
        if(!unloadRequested.load(std::memory_order_acquire)||unloadComplete.load(std::memory_order_acquire))return;
        // This runs at FRAME_START, after the previous frame's cosmetic scopes
        // have restored native state. Never free live render resources on the
        // DLL's worker thread while the game is drawing a replacement model.
        G::Unload=true;
        // Only this frame callback may be active when resources are released.
        // If a render/other hook is still using them, stop future cosmetics and
        // retry on the next frame instead of releasing out from underneath it.
        if(HookLifetime::active.load(std::memory_order_acquire)>1)return;
        WriteDiagnostic("event=unload_stage stage=frame_boundary_overrides_disabled");
        RestoreHudPreview();
        WriteDiagnostic("event=unload_stage stage=hud_original_restored");
        Shutdown();
        WriteDiagnostic("event=unload_stage stage=cosmetic_resources_released");
        U::Core.CleanupGameResources();
        WriteDiagnostic("event=unload_stage stage=all_game_resources_released_on_frame");
        unloadComplete.store(true,std::memory_order_release);
    }
    void Shutdown()
    {
        std::lock_guard lock(guard);UpdateUnusualParticles(true);UpdateKillstreakParticles(true);ClearPaintTextures();lifeStreaks.Clear();corpses.clear();ragdollBirths.clear();corpsePoses.clear();goldDeaths.clear();F::Materials.Remove(statueGoldMaterial);statueGoldMaterial=nullptr;stopped=true;Disconnect();if(loaded&&dirty)Save(Directory()/"current.json");
        // Dynamic references are engine resources, not changes to weapon entities.
        for(const auto& [path,model]:models)if(model.owned)I::ModelInfoClient->ReleaseDynamicModel(model.index);
        models.clear();
        jiggleInstances.clear();
    }
    std::string UnusualFor(CBaseCombatWeapon* base)
    {
        if(!base||!V::Enabled.Value||G::Unload||SDK::CleanScreenshot())return {};
        std::lock_guard lock(guard);if(stopped||!loaded)return {};
        auto weapon=base->As<CTFWeaponBase>();auto parent=weapon->m_hOwner().Get();
        if(!parent||!parent->IsPlayer())return {};auto owner=parent->As<CTFPlayer>();
        if(owner->IsDormant()||!owner->IsAlive()||owner->m_hActiveWeapon().Get()!=base)return {};
        int id=weapon->m_iItemDefinitionIndex(),cls=owner->m_iClass();auto item=catalog.Find(id);
        if(!item)return {};Selection selection;
        if(owner==H::Entities.GetLocal())selection=Get(preset,catalog.Canonical(id),cls);
        else
        {
            if(!V::Networking.Value||!V::Receive.Value)return {};auto it=remote.find(PlayerID(owner));
            if(it==remote.end()||it->second.cls!=cls||Now()-it->second.time>10)return {};
            auto cached=it->second.killstreakWeapons.Find(catalog.Canonical(id),cls);if(!cached)return {};
            selection=*cached;
        }
        if(!selection.enabled||!Compatible(id,cls,selection)||!selection.unusual)return {};
        auto chosen=Appearance(*item,cls,selection);if(!chosen)return {};
        auto name=WeaponUnusualName(selection.unusual,chosen->particleSuffix);
        // Decorated definitions use a generic tool suffix. Resolve the authored
        // weapon suffix from an exact-model, same-stat-family stock definition.
        if(name.empty())for(auto candidate:catalog.Variants(id,cls))
            if(candidate->Model(cls)==chosen->Model(cls)&&!(name=WeaponUnusualName(selection.unusual,candidate->particleSuffix)).empty())break;
        Diagnostic(std::format("event=unusual_profile item={} reskin={} unusual={} name={} independent_of_paint=true",id,chosen->id,selection.unusual,name.empty()?"unsupported_suffix":name));
        return name;
    }
    SkinModel::CosmeticEffects EffectsFor(CBaseCombatWeapon* base)
    {
        if(!base||!V::Enabled.Value||G::Unload)return {};
        std::lock_guard lock(guard);if(stopped||!loaded)return {};
        auto weapon=base->As<CTFWeaponBase>();auto parent=weapon->m_hOwner().Get();
        if(!parent||!parent->IsPlayer())return {};auto owner=parent->As<CTFPlayer>();
        if(owner->IsDormant())return {};
        int id=weapon->m_iItemDefinitionIndex(),cls=owner->m_iClass();auto item=catalog.Find(id);
        if(!item)return {};Selection selection;
        if(owner==H::Entities.GetLocal())selection=Get(preset,catalog.Canonical(id),cls);
        else
        {
            if(!V::Networking.Value||!V::Receive.Value)return {};auto it=remote.find(PlayerID(owner));
            if(it==remote.end()||it->second.cls!=cls||Now()-it->second.time>10)return {};
            auto cached=it->second.killstreakWeapons.Find(catalog.Canonical(id),cls);if(!cached)return {};
            selection=*cached;
        }
        if(!selection.enabled||!Compatible(id,cls,selection))return {};
        auto chosen=Appearance(*item,cls,selection);if(!chosen||chosen->id==id)return {};
        auto result=chosen->Effects(owner->m_iTeamNum());result.active=true;
        if(auto selectedScript=weaponScriptEffects.find(chosen->weaponType);selectedScript!=weaponScriptEffects.end())result.Fallback(selectedScript->second);
        result.sourceSounds=item->Effects(owner->m_iTeamNum()).sounds;
        result.sourceMuzzle=item->Effects(owner->m_iTeamNum()).muzzle;
        // Native item sounds/particles fall back to the weapon script. Do the same
        // when a special reskin is changed back to a plain stock-looking variant.
        if(auto info=weapon->m_pWeaponInfo())
        {
            auto script=[](const auto& field){std::string value(field,strnlen_s(field,sizeof(field)));return SafeEffectName(value)?value:std::string{};};
            CosmeticEffects fallback;
            for(size_t n=0;n<fallback.sounds.size();++n)fallback.sounds[n]=script(info->aShootSounds[n]);
            fallback.muzzle=script(info->m_szMuzzleFlashParticleEffect);fallback.tracer=script(info->m_szTracerEffect);
            result.Fallback(fallback);
            for(size_t n=0;n<result.sourceSounds.size();++n)if(result.sourceSounds[n].empty())result.sourceSounds[n]=fallback.sounds[n];
            if(result.sourceMuzzle.empty())result.sourceMuzzle=script(info->m_szMuzzleFlashParticleEffect);
        }
        Diagnostic(std::format("event=cosmetic_profile remote={} item={} reskin={} team={} single={} critical={} melee_miss={} melee_hit={} melee_world={} source_hit={} source_world={} muzzle={} tracer={} source_muzzle={}",owner!=H::Entities.GetLocal(),id,chosen->id,owner->m_iTeamNum(),result.Sound(SINGLE),result.Sound(BURST),result.Sound(MELEE_MISS),result.Sound(MELEE_HIT),result.Sound(MELEE_HIT_WORLD),result.sourceSounds[MELEE_HIT],result.sourceSounds[MELEE_HIT_WORLD],result.muzzle,result.tracer,result.sourceMuzzle));
        return result;
    }
    void ObserveCosmeticEffect(const char* kind,const std::string& name)
    {
        if(name.empty()||!V::Enabled.Value)return;std::lock_guard lock(guard);if(stopped)return;
        if(std::string_view(kind)=="sound")++telemetry.sounds;
        else if(std::string_view(kind)=="tracer")++telemetry.tracers;
        else if(std::string_view(kind)=="muzzle")++telemetry.muzzles;
        Diagnostic(std::format("event=cosmetic_effect kind={} name={}",kind,name));
    }
    std::string SoundFor(CBaseCombatWeapon* weapon,int category)
    {
        if(category<0||category>=NUM_SHOOT_SOUND_TYPES)return {};
        auto effects=EffectsFor(weapon);auto sound=effects.active?effects.Sound(category):std::string{};
        if(!sound.empty())ObserveCosmeticEffect("sound",sound);return sound;
    }
    std::string RemoteSoundFor(int entity,const char* original,bool wave)
    {
        if(!original||!V::Enabled.Value||G::Unload||SDK::CleanScreenshot())return {};
        std::lock_guard lock(guard);if(stopped||!loaded)return {};
        auto client=I::ClientEntityList->GetClientEntity(entity);if(!client)return {};
        auto base=client->As<CBaseEntity>();CTFWeaponBase* weapon=nullptr;
        if(base->GetClassID()==ETFClassID::CObjectSapper)
        {
            auto builder=base->As<CBaseObject>()->m_hBuilder().Get();auto owner=builder&&builder->IsPlayer()?builder->As<CTFPlayer>():nullptr;
            auto sapper=EquipmentWeapon(owner);Selection selection;
            if(!CosmeticSelection(sapper,owner,selection))return {};
            auto item=catalog.Find(sapper->m_iItemDefinitionIndex());auto chosen=item?Appearance(*item,8,selection):nullptr;
            if(!chosen)return {};const auto target=chosen->SapperTimer();if(target.empty())return {};
            const std::array<std::string,2> sources={item->SapperTimer(),"Weapon_Sapper.Timer"};
            for(const auto& source:sources)
            {
                if(source.empty()||Lower(source)==Lower(target))continue;
                bool matches=!wave&&Lower(original)==Lower(source);
                if(wave){auto waves=soundWaves.find(Lower(source));matches=waves!=soundWaves.end()&&waves->second.contains(WaveKey(original));}
                if(matches){Diagnostic(std::format("event=placed_sapper_sound entity={} source={} target={} remote={} timer=true client_only=true",entity,source,target,owner!=H::Entities.GetLocal()));return target;}
            }
            return {};
        }
        if(!V::Networking.Value||!V::Receive.Value)return {};
        if(base->IsPlayer())
        {
            auto player=base->As<CTFPlayer>();if(player==H::Entities.GetLocal()||player->IsDormant())return {};
            auto held=player->m_hActiveWeapon().Get();weapon=held?held->As<CTFWeaponBase>():nullptr;
        }
        else if(base->IsBaseCombatWeapon())weapon=base->As<CTFWeaponBase>();
        if(!weapon)return {};auto parent=weapon->m_hOwner().Get();if(!parent||parent==H::Entities.GetLocal())return {};
        auto effects=EffectsFor(weapon);if(!effects.active)return {};
        std::string replacement;
        if(!wave)replacement=effects.ReplaceSound(original);
        else
        {
            const auto key=WaveKey(original);if(key.empty())return {};
            for(size_t n=0;n<effects.sounds.size();++n)
            {
                if(effects.sounds[n].empty()||effects.sounds[n]==effects.sourceSounds[n])continue;
                auto it=soundWaves.find(Lower(effects.sourceSounds[n]));
                if(it!=soundWaves.end()&&it->second.contains(key))
                {
                    if(!replacement.empty()&&replacement!=effects.sounds[n])
                    {Diagnostic(std::format("event=remote_sound_unmatched entity={} wave={} reason=ambiguous_categories native_preserved=true",entity,key));return {};}
                    replacement=effects.sounds[n];
                }
            }
        }
        if(!replacement.empty()){Diagnostic(std::format("event=remote_reskin_sound route={} replacement={}",wave?"network_wave":"script",replacement));ObserveCosmeticEffect("sound",replacement);}
        else if(wave){auto key=WaveKey(original);if(key.starts_with("weapons/"))Diagnostic(std::format("event=remote_sound_unmatched entity={} wave={} reason=no_authored_source_match native_preserved=true",entity,key));}
        return replacement;
    }
    void ObserveParticleCreation(const char* kind,const char* original,const std::string& requested,bool created)
    {
        if(!V::Enabled.Value||requested.empty())return;std::lock_guard lock(guard);if(stopped)return;
        Diagnostic(std::format("event=particle_creation kind={} original={} requested={} created={} route=local_named_particle",kind,original?original:"none",requested,created));
        if(created)ObserveCosmeticEffect(kind,requested);
    }
    void ObserveParticleLookup(const char* name,int index)
    {
        if(!name||!V::Enabled.Value)return;
        std::string_view effect(name);
        if(effect.find("muzzle")==std::string_view::npos&&!effect.starts_with("bullet_tracer_raygun"))return;
        std::lock_guard lock(guard);if(stopped||!loaded)return;
        Diagnostic(std::format("event=particle_lookup name={} network_index={}",name,index));
    }
    std::string MuzzleFor(const void* particleProperty,const char* original)
    {
        if(!particleProperty||!original||!V::Enabled.Value||SDK::CleanScreenshot())return {};
        if(std::string_view(original).find("muzzle")==std::string_view::npos)return {};
        std::lock_guard lock(guard);if(stopped||!loaded)return {};
        auto local=H::Entities.GetLocal();
        for(int index=1;index<=std::clamp(I::EngineClient->GetMaxClients(),0,128);++index)
        {
            auto client=I::ClientEntityList->GetClientEntity(index);auto player=client?client->As<CTFPlayer>():nullptr;
            if(!player||!player->IsPlayer()||!player->IsAlive()||player->IsDormant())continue;
            if(player!=local&&(!V::Networking.Value||!V::Receive.Value||!remote.contains(PlayerID(player))))continue;
            auto held=player->m_hActiveWeapon().Get();auto weapon=held?held->As<CTFWeaponBase>():nullptr;if(!weapon)continue;
            auto model=player->GetRenderedWeaponModel();auto viewmodel=player==local?player->m_hViewModel().Get():nullptr;
            auto attachment=weapon->GetAppropriateWorldOrViewModel();
            const bool owned=particleProperty==weapon->m_Particles()||(model&&particleProperty==model->m_Particles())||(viewmodel&&particleProperty==viewmodel->m_Particles())||(attachment&&particleProperty==attachment->m_Particles());
            if(!owned)continue;
            auto replacement=EffectsFor(weapon).Muzzle(original);
            if(!replacement.empty())Diagnostic(std::format("event=reskin_muzzle_route remote={} original={} replacement={}",player!=local,original,replacement));
            return replacement;
        }
        return {};
    }
    void ObserveViewmodelDraw(const void* drawing,bool verified)
    {
        if(!V::Enabled.Value)return;std::lock_guard lock(guard);if(stopped||!loaded)return;
        auto local=H::Entities.GetLocal();auto held=H::Entities.GetWeapon();if(!local||!held)return;
        auto id=held->m_iItemDefinitionIndex();++telemetry.viewmodels;if(verified)++telemetry.verified;
        auto entity=local->m_hViewModel().Get();auto renderable=entity?static_cast<IClientRenderable*>(entity):nullptr;
        auto kind=SkinRender::MatchViewmodel(drawing,entity,renderable);
        Diagnostic(std::format("event=viewmodel_context verified={} match={} item={} drawing={:#x} expected_entity={:#x} expected_renderable={:#x}",verified,
            kind==SkinRender::ViewmodelPointerKind::Renderable?"renderable":kind==SkinRender::ViewmodelPointerKind::Entity?"entity":"none",id,
            reinterpret_cast<uintptr_t>(drawing),reinterpret_cast<uintptr_t>(entity),reinterpret_cast<uintptr_t>(renderable)));
    }
    void ObserveDraw(const ModelRenderInfo_t& info,matrix3x4* bones,const char* route)
    {
        if(!V::Enabled.Value||!info.pModel)return;std::lock_guard lock(guard);if(stopped||!loaded)return;
        auto local=H::Entities.GetLocal();auto held=H::Entities.GetWeapon();if(!local||!held)return;
        auto id=held->m_iItemDefinitionIndex();auto s=Get(preset,catalog.Canonical(id),local->m_iClass());
        const char* path=I::ModelInfoClient->GetModelName(info.pModel);if(!path)return;
        auto item=catalog.Find(id);if(!item||(item->Model(local->m_iClass())!=path&&item->ViewModel(local->m_iClass())!=path))return;
        ++telemetry.draws;
        Diagnostic(std::format("event=draw_route route={} item={} reskin={} australium={} entity={} custom_bones={} model={}",route,id,s.reskin,s.australium,info.entity_index,bones!=nullptr,path));
    }
    bool Prepare(const DrawModelState_t& state,const ModelRenderInfo_t& info,matrix3x4* bones,
        DrawModelState_t& outputState,ModelRenderInfo_t& outputInfo,std::array<matrix3x4,MAXSTUDIOBONES>& outputBones,matrix3x4*& drawBones,bool localViewmodelDraw,bool accessory,bool effectPass,int accessoryIndex,int* accessoryCount)
    {
        std::lock_guard lock(guard);
        drawBones=bones;
        if(accessoryCount)*accessoryCount=0;
        static thread_local bool preparing=false;if(preparing)return false;
        struct PreparingGuard {bool& flag;PreparingGuard(bool& f):flag(f){flag=true;}~PreparingGuard(){flag=false;}} preparingGuard(preparing);
        if(stopped||!loaded||!V::Enabled.Value||!info.pModel||(!effectPass&&(F::Chams.m_bRendering||F::Glow.m_bRendering)))return false;
        const char* path=I::ModelInfoClient->GetModelName(info.pModel);if(!path||(!std::string_view(path).starts_with("models/weapons/")&&!std::string_view(path).starts_with("models/workshop/weapons/")&&!std::string_view(path).starts_with("models/workshop_partner/weapons/")&&!std::string_view(path).starts_with("models/buildables/")))return false;
        auto reject=[&](const std::string& reason){renderStatus="Skipped: "+reason;if(info.entity_index==-1||localViewmodelDraw){++telemetry.skipped;telemetry.result=reason;telemetry.path=path;}
            Diagnostic(std::format("event=draw_rejected reason={} local_scope={} entity={} model={}",reason,localViewmodelDraw,info.entity_index,path));return false;};
        if(!state.m_pStudioHdr||state.m_pStudioHdr->numbones<1||state.m_pStudioHdr->numbones>MAXSTUDIOBONES)return reject("invalid_source_studio_layout");
        auto local=H::Entities.GetLocal();auto client=I::ClientEntityList->GetClientEntity(info.entity_index);
        auto entity=client?client->As<CBaseEntity>():nullptr;
        // Weapon attachments may be client-only and have no network entity index.
        if(!entity && info.pRenderable)if(auto unknown=info.pRenderable->GetIClientUnknown())entity=unknown->GetBaseEntity();
        if(!local||!entity)return reject("no_local_or_render_entity");
        if(effectPass&&!bones)
        {
            // Some glow/chams draws omit custom bones. Reuse the verified
            // attachment's native cache; never recurse into SetupBones here.
            auto& cached=entity->As<CBaseAnimating>()->m_CachedBoneData();
            if(cached.Count()!=state.m_pStudioHdr->numbones)return reject("effect_pass_cached_bones_unavailable");
            bones=cached.Base();drawBones=bones;
        }
        CTFWeaponBase* weapon=nullptr;CTFPlayer* owner=nullptr;
        const bool planted=entity->GetClassID()==ETFClassID::CObjectSapper;
        if(planted)
        {
            auto builder=entity->As<CBaseObject>()->m_hBuilder().Get();owner=builder&&builder->IsPlayer()?builder->As<CTFPlayer>():nullptr;
            weapon=EquipmentWeapon(owner);
            if(accessory)return false;
        }
        else if(entity->IsBaseCombatWeapon()){weapon=entity->As<CTFWeaponBase>();auto parent=weapon->m_hOwner().Get();if(parent&&parent->IsPlayer())owner=parent->As<CTFPlayer>();}
        else
        {
            // Bone-merged world weapons may be wearables rather than weapons.
            // Resolve their actual move-parent; never borrow our own active gun
            // for another player's sapper attachment.
            std::set<CBaseEntity*> ancestors;
            for(auto parent=entity->GetMoveParent();parent&&ancestors.size()<8&&ancestors.insert(parent).second;parent=parent->GetMoveParent())
                if(parent->IsPlayer())
                {
                    auto player=parent->As<CTFPlayer>();auto held=player->m_hActiveWeapon().Get();
                    auto item=held?catalog.Find(held->As<CTFWeaponBase>()->m_iItemDefinitionIndex()):nullptr;
                    if(item&&item->MatchesModel(path,player->m_iClass())){weapon=held->As<CTFWeaponBase>();owner=player;}break;
                }
            if(!weapon)
            {
            weapon=H::Entities.GetWeapon();owner=local;
            // Hands, hats and unrelated attachments must never be replaced.
            auto item=weapon?catalog.Find(weapon->m_iItemDefinitionIndex()):nullptr;
            const char* modelName=I::ModelInfoClient->GetModelName(info.pModel);
            // Hands, cosmetic extras and projectiles are expected mismatches, not failures.
            if(!item||!modelName||!item->MatchesModel(modelName,local->m_iClass()))return false;
            telemetry.lastItem=item->id;
            auto candidate=Get(preset,catalog.Canonical(item->id),local->m_iClass());
            if(!candidate.enabled){++telemetry.skipped;telemetry.lastItem=item->id;telemetry.result="weapon_selection_disabled";telemetry.path=path;
                Diagnostic(std::format("event=draw_skipped reason=weapon_selection_disabled item={} class={} model={}",item->id,local->m_iClass(),path));return false;}
            bool attached=false;std::set<CBaseEntity*> visited;
            auto localViewmodel=local->m_hViewModel().Get();
            for(auto parent=entity;parent && visited.size()<8 && visited.insert(parent).second;parent=parent->GetMoveParent())
                if(parent==localViewmodel){attached=true;break;}
            if(!SkinRender::LocalAttachment(true,attached,localViewmodelDraw,info.entity_index))
                return reject(std::format("unverified_local_attachment local_scope={} entity={}",localViewmodelDraw,info.entity_index));
            Diagnostic(std::format("event=attachment_identity source={} item={} entity={} model={}",attached?"local_parent":"local_viewmodel_draw_scope",weapon->m_iItemDefinitionIndex(),info.entity_index,path));
            }
        }
        if(!weapon||!owner||!owner->IsPlayer()||owner->IsDormant())return reject("no_valid_weapon_owner");
        int id=weapon->m_iItemDefinitionIndex(),cls=owner->m_iClass();auto item=catalog.Find(id);if(!item)return reject("uncataloged_weapon");
        // Engine-drawn pilot lights / holiday addons are not the base weapon.
        // Leave them alone rather than replacing an addon with an entire gun.
        if(path!=item->Model(cls)&&path!=item->ViewModel(cls))
        {
            for(const auto& a:item->Attachments(owner->m_iTeamNum()))if(a.model==path)return false;
            for(const auto& a:item->FestivizerAttachments(owner->m_iTeamNum()))if(a.model==path)return false;
        }
        Selection selection;
        if(owner==local)selection=Get(preset,catalog.Canonical(id),cls);
        else
        {
            if(!V::Networking.Value||!V::Receive.Value)return false;auto it=remote.find(PlayerID(owner));
            if(it==remote.end()||it->second.cls!=cls||Now()-it->second.time>10)return false;
            auto cached=it->second.killstreakWeapons.Find(catalog.Canonical(id),cls);if(!cached)return false;
            selection=*cached;
        }
        if(!selection.enabled)return false;
        ++telemetry.matches;telemetry.lastItem=id;telemetry.path=path;
        if(!Compatible(id,cls,selection))return reject("incompatible_selection");
        Diagnostic(std::format("event=selection_matched item={} class={} reskin={} australium={} festive={} custom_bones={} model={}",id,cls,selection.reskin,selection.australium,selection.festive,bones!=nullptr,path));
        auto chosen=Appearance(*item,cls,selection);if(!chosen)return reject("appearance_unresolved");
        const bool nativeLights=accessory&&selection.festivized&&SDK::AttribHookValue(0.f,"is_festivized",weapon)!=0.f;
        auto extras=accessory?CosmeticAttachments(*item,*chosen,owner->m_iTeamNum(),localViewmodelDraw,selection.festivized,nativeLights):std::vector<Attachment>{};
        if(accessoryCount)*accessoryCount=int(extras.size());
        if(accessory)Diagnostic(std::format("event=holiday_attachment_profile item={} reskin={} festive={} festivized={} native_festivized={} count={} team={} local_scope={}",id,chosen->id,chosen->festive,selection.festivized,nativeLights,extras.size(),owner->m_iTeamNum(),localViewmodelDraw));
        if(accessory&&(accessoryIndex<0||accessoryIndex>=int(extras.size())))return false;
        const auto targetPath=planted?chosen->PlacedSapper(entity->As<CBaseObject>()->m_bPlacing()):accessory?extras[accessoryIndex].model:localViewmodelDraw?chosen->ViewModel(cls):chosen->WorldModel(cls);
        if(planted)Diagnostic(std::format("event=placed_sapper_profile source={} reskin={} builder={} remote={} target={} gameplay_unchanged=true",id,chosen->id,owner->entindex(),owner!=local,targetPath));
        const bool alignment=localViewmodelDraw&&owner==local&&selection.Adjusted()&&selection.ValidAlignment();
        if(!accessory&&!selection.australium&&!chosen->golden&&!alignment && targetPath==path){++telemetry.skipped;telemetry.result="original_appearance_requested";return false;}
        outputInfo=info;outputState=state;
        if((selection.australium||chosen->golden)&&!accessory)
        {
            const int skin=selection.australium?(owner->m_iTeamNum()==3?chosen->goldBlue:chosen->goldRed):(owner->m_iTeamNum()==3?chosen->defaultBlue:chosen->defaultRed);
            auto target=state.m_pStudioHdr;
            if(!alignment && targetPath==path && target && SkinRender::ValidSkin(skin,target->numskinfamilies))
            {outputInfo.skin=skin;drawBones=SkinRender::SkinOnlyBones(bones);++telemetry.skin;telemetry.result="skin_override_prepared";
                renderStatus="Australium draw prepared (render-only).";Diagnostic(std::format("event=applied item={} class={} source_skin={} gold_skin={} skin_families={} custom_bones={} local_scope={} model={}",id,cls,info.skin,skin,target->numskinfamilies,bones!=nullptr,localViewmodelDraw,path));return true;}
        }
        const auto model=LoadModel(targetPath,effectPass);if(!model){++telemetry.skipped;telemetry.result="replacement_model_pending_or_unavailable";renderStatus="Waiting for replacement model; see model loading diagnostics.";return false;}
        auto target=I::ModelInfoClient->GetStudiomodel(model);auto source=state.m_pStudioHdr;
        if(!target || target->numbones<1 || target->numbones>MAXSTUDIOBONES || source->numbones<1 || source->numbones>MAXSTUDIOBONES || target->numflexdesc)
        {return reject(std::format("unsupported_target_bone_flex_layout item={} reskin={}",id,chosen->id));}
        std::array<matrix3x4,MAXSTUDIOBONES> sourceScratch{};matrix3x4* sourceBones=nullptr;
        const auto boneSource=SkinRender::AcquireBones(bones,sourceScratch,source->numbones,sourceBones,[&](matrix3x4* out,int capacity)
        {
            auto renderable=state.m_pRenderable?state.m_pRenderable:info.pRenderable;if(!renderable||!I::GlobalVars)return false;
            // Bypass the player-cache optimization: this is the weapon attachment's skeleton, not its owner's.
            auto original=U::Hooks.m_mHooks.find("CBaseAnimating_SetupBones");
            if(original!=U::Hooks.m_mHooks.end()&&original->second)
                return original->second->Call<bool>(renderable,out,capacity,BONE_USED_BY_ANYTHING,I::GlobalVars->curtime);
            return renderable->SetupBones(out,capacity,BONE_USED_BY_ANYTHING,I::GlobalVars->curtime);
        });
        if(boneSource==SkinRender::BoneSource::Unavailable||boneSource==SkinRender::BoneSource::InvalidLayout)return reject("source_bone_setup_failed");
        // Bone-merged stock attachments retain their own cached pose. Read the
        // verified local arms directly while the selected hand sequence is scoped
        // in, so animated bread/weapon bones follow that same authored animation.
        std::array<matrix3x4,MAXSTUDIOBONES> cosmeticSource{};
        if(localViewmodelDraw&&owner==local&&!accessory&&HandAnimationsFor(weapon))
        {
            auto vm=local->m_hViewModel().Get();auto vmModel=vm?vm->GetModel():nullptr;
            auto vmHeader=vmModel?I::ModelInfoClient->GetStudiomodel(vmModel):nullptr;
            auto setup=U::Hooks.m_mHooks.find("CBaseAnimating_SetupBones");
            if(vmHeader&&vmHeader->numbones>0&&vmHeader->numbones<=MAXSTUDIOBONES&&setup!=U::Hooks.m_mHooks.end()&&setup->second
                &&setup->second->Call<bool>(static_cast<IClientRenderable*>(vm),cosmeticSource.data(),MAXSTUDIOBONES,BONE_USED_BY_ANYTHING,I::GlobalVars->curtime))
            {source=vmHeader;sourceBones=cosmeticSource.data();Diagnostic(std::format("event=cosmetic_attachment_pose item={} reskin={} source=selected_hand_animation",id,chosen->id));}
        }
        else if(!planted&&!localViewmodelDraw&&!accessory&&AuthenticAnimationsFor(owner->As<CTFPlayer>()))
        {
            auto playerModel=owner->GetModel();auto playerHeader=playerModel?I::ModelInfoClient->GetStudiomodel(playerModel):nullptr;
            auto& originalCache=owner->m_CachedBoneData();
            int anchor=-1;if(playerHeader&&originalCache.Count()==playerHeader->numbones)
                for(int n=0;n<playerHeader->numbones;++n)if(auto name=BoneName(playerHeader,n);name&&*name=="weapon_bone"){anchor=n;break;}
            if(anchor>=0)
            {
                matrix3x4 oldAnchor;std::memcpy(oldAnchor,originalCache[anchor],sizeof(matrix3x4));
                std::array<matrix3x4,MAXSTUDIOBONES> playerPose;
                if(PreparePlayerBones(owner,playerPose))
                {
                    for(int n=0;n<source->numbones;++n)if(!SkinRender::RebaseAttachment(oldAnchor,playerPose[anchor],sourceBones[n],cosmeticSource[n]))return reject("invalid_third_person_attachment_pose");
                    sourceBones=cosmeticSource.data();Diagnostic(std::format("event=cosmetic_attachment_pose item={} reskin={} source=authentic_player_hand gameplay_bones_unchanged=true",id,chosen->id));
                }
            }
        }
        Diagnostic(std::format("event=source_bones item={} source={} count={} model={}",id,boneSource==SkinRender::BoneSource::Custom?"custom":"engine_setup",source->numbones,path));
        // Named animated bones come from the original weapon. Additional non-procedural
        // child bones use the target's authored bind pose, rooted in a mapped parent.
        std::vector<std::string> sourceStorage,targetStorage;
        sourceStorage.reserve(source->numbones);targetStorage.reserve(target->numbones);
        std::vector<std::string_view> sourceNames,targetNames;
        std::vector<int> parents,procedural;
        for(int n=0;n<source->numbones;++n){auto name=BoneName(source,n);if(!name)return reject("invalid_source_bone_name");sourceStorage.push_back(std::move(*name));}
        for(int n=0;n<target->numbones;++n){auto name=BoneName(target,n);if(!name)return reject("invalid_target_bone_name");targetStorage.push_back(std::move(*name));auto bone=target->pBone(n);parents.push_back(bone->parent);procedural.push_back(bone->proctype);}
        for(const auto& name:sourceStorage)sourceNames.emplace_back(name);
        for(const auto& name:targetStorage)targetNames.emplace_back(name);
        std::vector<int> mapping;std::string_view missing;
        bool anchored=false;
        if(!SkinRender::RestBoneMap(sourceNames,targetNames,parents,procedural,mapping,missing,true))
        {
            anchored=item->Sapper()&&SkinRender::AnchoredBoneMap(sourceNames,targetNames,parents,procedural,mapping);
            if(!anchored)return reject(std::format("incompatible_skeleton item={} reskin={} accessory={} missing_bone={}",id,chosen->id,accessory,missing));
        }
        JiggleInstance* jiggle=nullptr;int simulated=0;
        if(I::GlobalVars&&target->length>0&&target->length<=64*1024*1024)
        {
            const double now=I::GlobalVars->realtime;
            // Each weapon/renderable/model and world/viewmodel context owns its
            // own simulation. No retained entity or engine-owned physics pointers.
            const JiggleKey key{uint32_t(weapon->GetRefEHandle().ToInt()),uint32_t(owner->GetRefEHandle().ToInt()),
                uintptr_t(model),uintptr_t(info.pRenderable),localViewmodelDraw};
            for(auto it=jiggleInstances.begin();it!=jiggleInstances.end();)
                if(now<it->second.seen||now-it->second.seen>2)it=jiggleInstances.erase(it);else ++it;
            if(!jiggleInstances.contains(key)&&jiggleInstances.size()>=128)
            {
                auto oldest=std::min_element(jiggleInstances.begin(),jiggleInstances.end(),[](const auto& a,const auto& b){return a.second.seen<b.second.seen;});
                jiggleInstances.erase(oldest);
            }
            jiggle=&jiggleInstances[key];jiggle->seen=now;
        }
        std::array<bool,MAXSTUDIOBONES> resolved{};int remaining=0,restCount=0;
        for(int n=0;n<target->numbones;++n)
        {
            if(mapping[n]>=0){std::memcpy(outputBones[n],sourceBones[mapping[n]],sizeof(matrix3x4));resolved[n]=true;}else ++remaining;
        }
        for(int pass=0;remaining&&pass<target->numbones;++pass)for(int n=0;n<target->numbones;++n)if(!resolved[n])
        {int parent=parents[n];if(parent>=0&&parent<target->numbones&&resolved[parent])
            {if(!SkinRender::RestTransform(outputBones[parent],target->pBone(parent)->poseToBone,target->pBone(n)->poseToBone,outputBones[n]))return reject("invalid_target_bind_pose");
                if(jiggle&&procedural[n]==5)
                {
                    SkinJiggle::Params params{};
                    const int64_t offset=int64_t(target->boneindex)+int64_t(n)*sizeof(mstudiobone_t);
                    if(SkinJiggle::Read({reinterpret_cast<const char*>(target),size_t(target->length)},offset,target->pBone(n)->procindex,params))
                    {
                        if(jiggle->bones[n].Build(params,outputBones[n],outputBones[n],I::GlobalVars->realtime,I::GlobalVars->framecount))++simulated;
                        else Diagnostic(std::format("event=weapon_jiggle_fallback item={} reskin={} accessory={} bone={} reason=invalid_transform_or_state resting_pose=true",id,chosen->id,accessory,n));
                    }
                    else Diagnostic(std::format("event=weapon_jiggle_fallback item={} reskin={} accessory={} bone={} reason=invalid_or_unsupported_parameters resting_pose=true",id,chosen->id,accessory,n));
                }
                resolved[n]=true;--remaining;++restCount;}
            else if(anchored)for(int child=0;child<target->numbones;++child)if(parents[child]==n&&resolved[child])
            {if(!SkinRender::RestTransform(outputBones[child],target->pBone(child)->poseToBone,target->pBone(n)->poseToBone,outputBones[n]))return reject("invalid_target_ancestor_bind_pose");resolved[n]=true;--remaining;++restCount;break;}}
        if(remaining)return reject("unresolved_target_bone_parents");
        if(restCount)Diagnostic(std::format("event=bind_pose_children item={} reskin={} count={} accessory={} model={}",id,chosen->id,restCount,accessory,targetPath));
        if(simulated)Diagnostic(std::format("event=weapon_jiggle item={} reskin={} accessory={} bones={} local_scope={} effect_pass={} authored_parameters=true frame_cached=true model={}",id,chosen->id,accessory,simulated,localViewmodelDraw,effectPass,targetPath));
        if(alignment)
        {
            matrix3x4 basis{},rotation{};
            Math::AngleMatrix(I::EngineClient->GetViewAngles(),basis);
            Math::AngleMatrix(Vec3{selection.rotation[0],selection.rotation[1],selection.rotation[2]},rotation);
            const std::array<float,3> pivot={sourceBones[0][0][3],sourceBones[0][1][3],sourceBones[0][2][3]};
            if(!SkinRender::AlignBones(std::span<matrix3x4>(outputBones.data(),target->numbones),basis,rotation,pivot,selection.position))return reject("invalid_alignment_transform");
            Diagnostic(std::format("event=viewmodel_alignment item={} class={} position={},{},{} rotation={},{},{} effect_pass={} gameplay_unchanged=true",id,cls,selection.position[0],selection.position[1],selection.position[2],selection.rotation[0],selection.rotation[1],selection.rotation[2],effectPass));
        }
        outputInfo.pModel=model;outputInfo.skin=accessory?(extras[accessoryIndex].skin>=0?extras[accessoryIndex].skin:(owner->m_iTeamNum()==3?1:0)):selection.australium?(owner->m_iTeamNum()==3?chosen->goldBlue:chosen->goldRed):(owner->m_iTeamNum()==3?chosen->defaultBlue:chosen->defaultRed);
        if(!SkinRender::ValidSkin(outputInfo.skin,target->numskinfamilies))
        {if(selection.australium&&!accessory||accessory&&extras[accessoryIndex].skin>=0)return reject(std::format("selected_skin_out_of_range skin={} families={}",outputInfo.skin,target->numskinfamilies));outputInfo.skin=0;}
        outputInfo.instance=MODEL_INSTANCE_INVALID;
        outputInfo.body=0;
        outputInfo.hitboxset=0;
        // DrawModelSetup still sees the original renderable/model instance and rejected
        // the client-only AWPer attachment in v70. Supply matching target header +
        // hardware data directly to DrawModelExecute, with our remapped bone matrices.
        // Never reuse the stock mesh/LOD/decal data for a different model.
        auto cache=static_cast<IMDLCache*>(I::MDLCache);
        if(!cache)return reject("replacement_model_cache_unavailable");
        const auto handle=I::ModelInfoClient->GetCacheHandle(model);
        if(handle==MDLHANDLE_INVALID)return reject("replacement_cache_handle_invalid");
        auto cachedHeader=cache->GetStudioHdr(handle);
        auto hardware=cache->GetHardwareData(handle);
        if(!cachedHeader||cachedHeader!=target)return reject("replacement_cache_header_mismatch");
        if(!hardware||!hardware->m_pLODs||hardware->m_NumStudioMeshes<1)return reject("replacement_hardware_pending_or_empty");
        const bool shadowLast=hardware->m_NumLODs>0&&hardware->m_NumLODs<=8&&hardware->m_pLODs[hardware->m_NumLODs-1].m_SwitchPoint<0;
        const int lod=SkinRender::ReplacementLOD(hardware->m_RootLOD,hardware->m_NumLODs,localViewmodelDraw?hardware->m_RootLOD:state.m_lod,shadowLast);
        if(lod<0)return reject("replacement_hardware_lod_invalid");
        auto& level=hardware->m_pLODs[lod];
        if(!level.m_pMeshData||level.numMaterials<1||!level.ppMaterials)return reject("replacement_mesh_materials_pending");
        outputState.m_pStudioHdr=target;
        outputState.m_pStudioHWData=hardware;
        outputState.m_decals=STUDIORENDER_DECAL_INVALID;
        outputState.m_lod=lod;
        Diagnostic(std::format("event=replacement_render_state item={} reskin={} accessory={} cache_handle={} source_lod={} target_lod={} root_lod={} lod_count={} meshes={} materials={} instance_detached=true decals_detached=true model={}",id,chosen->id,accessory,handle,state.m_lod,lod,hardware->m_RootLOD,hardware->m_NumLODs,hardware->m_NumStudioMeshes,level.numMaterials,targetPath));
        drawBones=outputBones.data();
        if(accessory)++telemetry.accessories;else ++telemetry.model;telemetry.result=accessory?"cosmetic_attachment_prepared":"model_override_prepared";telemetry.path=targetPath;
        renderStatus=accessory?(simulated?"Cosmetic attachment prepared (jiggle physics active).":"Cosmetic attachment prepared.") :"Reskin draw prepared (render-only).";Diagnostic(std::format("event=applied item={} class={} reskin={} accessory={} accessory_index={} festive={} festivized={} source_model={} model={} source_bones={} target_bones={} source_skin={} target_skin={} local_scope={} effect_pass={} render_path=target_cached_meshes",id,cls,chosen->id,accessory,accessoryIndex,chosen->festive,selection.festivized,path,targetPath,source->numbones,target->numbones,info.skin,outputInfo.skin,localViewmodelDraw,effectPass));return true;
    }

    void DrawEffectGeometry(const DrawModelState_t& state,const ModelRenderInfo_t& info,matrix3x4* bones,bool localViewmodel,const char* route)
    {
        static auto original=U::Hooks.m_mHooks["IVModelRender_DrawModelExecute"];
        auto draw=[&](const DrawModelState_t& s,const ModelRenderInfo_t& i,matrix3x4* b)
        {original->Call<void>(I::ModelRender,s,i,b);};
        const char* path=info.pModel?I::ModelInfoClient->GetModelName(info.pModel):nullptr;
        if(!V::Enabled.Value||G::Unload||SDK::CleanScreenshot()||!bones||!path
            ||(!std::string_view(path).starts_with("models/weapons/")&&!std::string_view(path).starts_with("models/workshop/weapons/")&&!std::string_view(path).starts_with("models/workshop_partner/weapons/")&&!std::string_view(path).starts_with("models/buildables/")))
        {draw(state,info,bones);return;}
        // Preserve the caller's material, tint, depth and stencil state. Only
        // replace weapon geometry; player/wearable and historical poses bypass
        // this route. Keep cached replacement meshes resident across the draw.
        SkinRender::ModelCacheScope<IMDLCache> cache(static_cast<IMDLCache*>(I::MDLCache));
        DrawModelState_t replacementState{};ModelRenderInfo_t replacementInfo{};
        std::array<matrix3x4,MAXSTUDIOBONES> replacementBones;
        matrix3x4* drawBones=bones;
        const bool replaced=Prepare(state,info,bones,replacementState,replacementInfo,replacementBones,drawBones,localViewmodel,false,true);
        ObserveDraw(info,bones,route);
        if(replaced)draw(replacementState,replacementInfo,drawBones);
        else draw(state,info,bones);
        int accessoryCount=1;
        for(int index=0;index<accessoryCount&&index<SkinModel::MaxCosmeticAttachments;++index)
            if(Prepare(state,info,bones,replacementState,replacementInfo,replacementBones,drawBones,localViewmodel,true,true,index,&accessoryCount))
                draw(replacementState,replacementInfo,drawBones);
        if(PreparePipBoyDraw(state,info,bones,replacementState,replacementInfo,replacementBones,localViewmodel))
            draw(replacementState,replacementInfo,replacementBones.data());
    }

    namespace
    {
        bool BeginSkinConfirmation(const char* label)
        {
            using namespace ImGui;
            const float width=std::min(H::Draw.Scale(420),std::max(100.f,GetIO().DisplaySize.x-H::Draw.Scale(40)));
            // Auto-size height only: window-relative buttons must not feed
            // their own width back into the next frame's popup auto-sizing.
            SetNextWindowSizeConstraints({width,0},{width,GetIO().DisplaySize.y});
            SetNextWindowPos(GetMainViewport()->GetCenter(),ImGuiCond_Appearing,{.5f,.5f});
            PushStyleVar(ImGuiStyleVar_WindowPadding,{H::Draw.Scale(12),H::Draw.Scale(10)});
            PushStyleVar(ImGuiStyleVar_ItemSpacing,{H::Draw.Scale(8),H::Draw.Scale(6)});
            if(!FBeginPopupModal(label)){PopStyleVar(2);return false;}
            const ImVec2 pos=GetWindowPos(),size=GetWindowSize();
            GetWindowDrawList()->AddRect(pos+ImVec2(1,1),pos+size-ImVec2(1,1),GetColorU32(ImGuiCol_Border));
            return true;
        }
        void EndSkinConfirmation(){ImGui::EndPopup();ImGui::PopStyleVar(2);}
        float SkinConfirmationButtonWidth(int count)
        {
            return std::max(1.f,(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x*(count-1))/count)/H::Draw.Scale(1);
        }
        struct PresetBrowser
        {
            std::string createName,pendingName;
            SkinPresetUi::Action pending=SkinPresetUi::Action::None;
            std::vector<std::string> names;
            double scanned=-100;
        } presetBrowser;

        SkinPresetUi::Request DrawPresetBrowser()
        {
            using namespace ImGui;
            SkinPresetUi::Request request;bool openConfirmation=false;
            auto& browser=presetBrowser;
            const double now=Now();
            if(now-browser.scanned>=1)
            {
                browser.scanned=now;browser.names.clear();
                std::error_code error;std::filesystem::create_directories(Directory(),error);
                const auto defaultFile=Directory()/"default.json";
                if(!error&&!std::filesystem::exists(defaultFile,error)&&!error)
                    Save(defaultFile,Preset{});
                for(std::filesystem::directory_iterator it(Directory(),error),end;!error&&it!=end;it.increment(error))
                {
                    const auto& entry=*it;
                    if(entry.is_symlink(error)||error||!entry.is_regular_file(error)||error)continue;
                    if(Lower(entry.path().extension().string())!=".json")continue;
                    const auto name=entry.path().stem().string();
                    if(SkinPresetUi::NameAllowed(name))browser.names.push_back(name);
                    if(browser.names.size()>=512)break;
                }
                std::sort(browser.names.begin(),browser.names.end(),SkinPresetUi::Less);
                if(error)status="Could not read skin presets.";
            }
            FSDropdown("Name",&browser.createName,{},FSDropdownEnum::AutoUpdate,
                -H::Draw.Unscale(FCalcTextSize("CREATE").x+H::Draw.Scale(32))-36);
            PushDisabled(!SkinPresetUi::NameAllowed(browser.createName));
            if(FButton("Create",FButtonEnum::Fit|FButtonEnum::SameLine|FButtonEnum::Compact,{0,32}))
            {request={SkinPresetUi::Action::Create,browser.createName};browser.createName.clear();}
            PopDisabled();
            if(FButton(ICON_MD_FOLDER,FButtonEnum::Fit|FButtonEnum::SameLine|FButtonEnum::Compact,{32,32},0,F::Render.IconFont))
                ShellExecuteW(nullptr,L"open",Directory().c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            DebugDummy({0,H::Draw.Scale(6)});
            for(const auto& name:browser.names)
            {
                PushID(name.c_str());
                const bool current=Lower(name)==Lower(activePreset);
                const ImVec2 origin=GetCursorPos();
                const float width=GetContentRegionAvail().x;
                if(current)GetWindowDrawList()->AddRectFilled(GetCursorScreenPos(),
                    GetCursorScreenPos()+ImVec2(width,H::Draw.Scale(22)),GetColorU32(F::Render.Background2.Value));
                SetCursorPos(origin+ImVec2(H::Draw.Scale(2),H::Draw.Scale(1)));
                const bool load=CompactManagerIcon(current?ICON_MD_REFRESH:ICON_MD_DOWNLOAD);
                FTooltip(current?"Reload this skin preset":"Load this skin preset");
                SetCursorPos(origin+ImVec2(H::Draw.Scale(29),H::Draw.Scale(4)));
                const std::string label=name+(current&&presetModified?" *":"");
                TextColored(current?F::Render.Active.Value:F::Render.Inactive.Value,"%s",
                    TruncateText(label,std::max(1.f,width-H::Draw.Scale(84))).c_str());
                FTooltip(label.c_str());
                SetCursorPos(origin+ImVec2(std::max(H::Draw.Scale(52),width-H::Draw.Scale(49)),H::Draw.Scale(1)));
                const bool save=CompactManagerIcon(ICON_MD_SAVE);FTooltip("Save current cosmetics to this preset");
                SetCursorPos(origin+ImVec2(std::max(H::Draw.Scale(77),width-H::Draw.Scale(24)),H::Draw.Scale(1)));
                const bool remove=CompactManagerIcon(ICON_MD_DELETE);
                FTooltip(SkinPresetUi::Default(name)?"Reset default preset":"Delete or reset this skin preset");
                if(load||remove||(save&&!current))
                {
                    browser.pendingName=name;
                    browser.pending=load?SkinPresetUi::Action::Load:remove?SkinPresetUi::Action::Delete:SkinPresetUi::Action::Save;
                    openConfirmation=true;
                }
                else if(save)request={SkinPresetUi::Action::Save,name};
                SetCursorPos(origin);DebugDummy({0,H::Draw.Scale(22)});
                PopID();
            }
            // OpenPopup must use the same ID scope as BeginPopupModal.
            // Row controls only request a dialog; use a stable browser-level ID.
            if(openConfirmation)
                OpenPopup("Confirm skin preset");
            if(BeginSkinConfirmation("Confirm skin preset"))
            {
                const bool loading=browser.pending==SkinPresetUi::Action::Load;
                const bool saving=browser.pending==SkinPresetUi::Action::Save;
                TextWrapped(loading?"Replace current cosmetics with '%s'?":saving?"Overwrite '%s' with current cosmetics?":"Delete or reset '%s'?",
                    browser.pendingName.c_str());
                if(loading&&presetModified)TextWrapped("Unsaved preset changes will be replaced.");
                const float actionWidth=SkinConfirmationButtonWidth(loading||saving?2:3);
                if(loading||saving)
                {
                    if(FButton(loading?"Load":"Yes, overwrite",FButtonEnum::Compact,{actionWidth,22}))
                    {request={browser.pending,browser.pendingName};browser.pending=SkinPresetUi::Action::None;CloseCurrentPopup();}
                    if(FButton("Cancel",FButtonEnum::Compact|FButtonEnum::SameLine,{actionWidth,22}))
                    {browser.pending=SkinPresetUi::Action::None;CloseCurrentPopup();}
                }
                else
                {
                    PushDisabled(SkinPresetUi::Default(browser.pendingName));
                    if(FButton("Yes, delete",FButtonEnum::Compact,{actionWidth,22}))
                    {request={SkinPresetUi::Action::Delete,browser.pendingName};browser.pending=SkinPresetUi::Action::None;CloseCurrentPopup();}
                    PopDisabled();
                    if(FButton("Yes, reset",FButtonEnum::Compact|FButtonEnum::SameLine,{actionWidth,22}))
                    {request={SkinPresetUi::Action::Reset,browser.pendingName};browser.pending=SkinPresetUi::Action::None;CloseCurrentPopup();}
                    if(FButton("Cancel",FButtonEnum::Compact|FButtonEnum::SameLine,{actionWidth,22}))
                    {browser.pending=SkinPresetUi::Action::None;CloseCurrentPopup();}
                }
                EndSkinConfirmation();
            }
            if(browser.pending!=SkinPresetUi::Action::None&&!IsPopupOpen("Confirm skin preset"))
                browser.pending=SkinPresetUi::Action::None;
            if(!status.empty())
            {
                TextUnformatted(TruncateText(status,GetContentRegionAvail().x).c_str());
                FTooltip(status.c_str());
            }
            return request;
        }

        void ApplyPresetRequest(const SkinPresetUi::Request& request)
        {
            using Action=SkinPresetUi::Action;
            if(request.action==Action::None)return;
            if(!SkinPresetUi::NameAllowed(request.name)){status="Invalid preset name.";return;}
            const auto file=Directory()/(request.name+".json");
            std::error_code error;
            const bool exists=std::filesystem::exists(file,error);
            if(error){status="Could not access skin preset.";return;}
            if(std::filesystem::is_symlink(file,error)){status="Linked preset files are not supported.";return;}
            error.clear();
            if(request.action==Action::Load)
            {
                if(Load(file))status="Loaded: "+request.name;
            }
            else if(request.action==Action::Create||request.action==Action::Save)
            {
                if(request.action==Action::Create&&exists){status="Preset already exists. Use its save button.";return;}
                if(Save(file)){activePreset=request.name;presetModified=false;dirty=true;status="Saved: "+request.name;}
            }
            else if(request.action==Action::Reset)
            {
                if(Save(file,Preset{}))
                {
                    if(Lower(activePreset)==Lower(request.name)){preset.clear();dirty=true;presetModified=false;}
                    status="Reset: "+request.name;
                }
            }
            else if(request.action==Action::Delete)
            {
                if(SkinPresetUi::Default(request.name)){status="The default preset can only be reset.";return;}
                if(!exists){status="Preset no longer exists.";return;}
                // The confirmation has already been accepted. Prefer recoverable
                // deletion through Windows' Recycle Bin over permanent removal.
                std::wstring source=file.wstring();source.push_back(L'\0');
                SHFILEOPSTRUCTW operation{};operation.wFunc=FO_DELETE;operation.pFrom=source.c_str();
                operation.fFlags=FOF_ALLOWUNDO|FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_SILENT;
                if(SHFileOperationW(&operation)==0&&!operation.fAnyOperationsAborted)
                {
                    if(Lower(activePreset)==Lower(request.name)){activePreset.clear();dirty=true;}
                    status="Deleted: "+request.name;
                }
                else status="Preset was not deleted.";
            }
            presetBrowser.scanned=-100;
            Diagnostic(std::format("event=preset_manager action={} success_status={} current_name={} entries={} network_preferences_unchanged=true",
                int(request.action),status,activePreset,preset.size()));
        }
    }

    void Menu()
    {
        using namespace ImGui;std::lock_guard lock(guard);
        const auto tip=[](const char* message){FTooltip(message,IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled));};
        FDropdown(V::ThirdPersonAnimations,FDropdownEnum::InlineTitle);
        tip("Authentic uses the selected weapon's animation family. Legacy keeps the original animations. Gameplay hitboxes do not change.");
        FToggle(V::Enabled);FToggle(V::Follow,FToggleEnum::Left);FToggle(V::PerClass,FToggleEnum::Right);
        if(!loaded){TextUnformatted("Waiting for the item catalog.");return;}
        static int weaponID=14,editClass=2;
        auto local=H::Entities.GetLocal();auto held=H::Entities.GetWeapon();
        if(V::Follow.Value&&local&&held&&!IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId))
        {weaponID=held->m_iItemDefinitionIndex();editClass=local->m_iClass();}
        if(!V::Follow.Value)
        {
            FDropdown("Edit class",&editClass,{"Scout","Sniper","Soldier","Demoman","Medic","Heavy","Pyro","Spy","Engineer"},{1,2,3,4,5,6,7,8,9},FDropdownEnum::InlineTitle);
            std::vector<const char*> names;std::vector<int> ids;std::set<int> seen;
            for(const auto& [id,item]:catalog.items)if(!item.Watch()&&item.Supports(editClass)&&seen.insert(catalog.Canonical(id)).second)
            {names.push_back(item.name.c_str());ids.push_back(id);}
            FDropdown("Weapon",&weaponID,names,ids,FDropdownEnum::InlineTitle);
        }
        const Item* original=catalog.Find(weaponID);
        const bool editable=original&&!original->Watch()&&original->Supports(editClass);
        Dummy({0,H::Draw.Scale(5)});
        const std::string editingLabel=editable?std::format("Editing: {} / {}",Classes[std::clamp(editClass,1,9)],original->name):"Select a supported weapon to edit.";
        TextUnformatted(TruncateText(editingLabel,GetContentRegionAvail().x).c_str());
        tip(editingLabel.c_str());
        Dummy({0,H::Draw.Scale(7)});
        const int canonical=editable?catalog.Canonical(weaponID):0,scope=V::PerClass.Value?editClass:0;
        Selection value=editable?Get(preset,canonical,scope):Selection{};
        bool changed=false;SkinPresetUi::Request request;
        const int columns=SkinPresetUi::Columns(GetContentRegionAvail().x,H::Draw.Scale(1));
        if(BeginTable("SkinChangerColumns",columns,ImGuiTableFlags_SizingStretchSame))
        {
            TableNextColumn();
            // These widgets measure their current window, not the table cell.
            // Give each column its own padded window to keep all controls and
            // their custom borders inside the column's clipping rectangle.
            Section("##SkinAppearanceColumn",0.f);
            if(editable)
            {
                if(Section("Weapon appearance"))
                {
                    changed|=FToggle("Apply this weapon selection",&value.enabled);
                    tip("Enable the saved cosmetic selection for this weapon and class.");
                    auto variants=catalog.Variants(weaponID,editClass);
                    std::vector<const char*> names={"Original appearance"};std::vector<int> ids={0};
                    for(auto item:variants){names.push_back(item->name.c_str());ids.push_back(item->id);}
                    const bool reskinChanged=FDropdown("Reskin",&value.reskin,names,ids,FDropdownEnum::InlineTitle);changed|=reskinChanged;
                    if(reskinChanged)
                    {
                        auto reskin=value.reskin?catalog.Find(value.reskin):nullptr;
                        if(NormalizeReskinChange(value,reskin&&reskin->Gold()))status="Cleared incompatible gold/Festive options.";
                    }
                    auto chosen=Appearance(*original,editClass,Selection{true,false,false,false,false,value.reskin});
                    bool gold=chosen&&chosen->Gold();
                    if(!value.reskin)for(auto item:variants)if(item->family==original->family)gold|=item->Gold();
                    BeginDisabled((!gold||value.festive||value.finish)&&!value.australium);
                    changed|=FToggle("Australium",&value.australium);EndDisabled();
                    tip(!gold?"This appearance has no gold style.":value.finish?"Choose Original finish before enabling Australium.":"Gold style is incompatible with classic Festive variants.");
                    const bool festive=!value.reskin&&!value.australium&&!catalog.Variants(weaponID,editClass,true).empty();
                    BeginDisabled(!festive&&!value.festive);changed|=FToggle("Festive variant",&value.festive);EndDisabled();
                    tip(festive?"Use the authored classic Festive model.":"No compatible classic Festive variant, or another model/gold style is selected.");
                    auto appearance=Appearance(*original,editClass,value);
                    const bool lights=appearance&&appearance->CanFestivize();
                    if(value.festivized&&!lights){value.festivized=false;changed=true;status="Cleared incompatible Festivizer lights.";}
                    BeginDisabled(!lights);changed|=FToggle("Festivized lights",&value.festivized);EndDisabled();
                    tip(lights?"Add compatible Festivizer light attachments.":"This appearance has no compatible Festivizer attachments.");
                    const bool unusual=appearance&&!WeaponUnusualName(701,appearance->particleSuffix).empty();
                    BeginDisabled(!unusual&&!value.unusual);
                    changed|=FDropdown("Weapon Unusual effect",&value.unusual,{"None","Hot","Isotope","Cool","Energy Orb"},{0,701,702,703,704},FDropdownEnum::InlineTitle);
                    EndDisabled();tip(unusual?"Weapon Unusuals also work without a war paint.":"This appearance has no authored weapon-particle suffix.");
                }EndSection();
                if(Section("Finish"))
                {
                    auto appearance=Appearance(*original,editClass,value);
                    std::vector<const Paint*> paints;
                    if(appearance&&!value.australium&&!appearance->golden)for(const auto& [id,paint]:catalog.paints)
                        if(PaintWeapon(*appearance,editClass,id)>=0)paints.push_back(&paint);
                    std::sort(paints.begin(),paints.end(),[](auto a,auto b){return a->name<b->name;});
                    std::vector<const char*> names={"Original finish"};std::vector<int> ids={0};
                    for(auto paint:paints){names.push_back(paint->name.c_str());ids.push_back(paint->id+1);}
                    if(value.finish&&std::find(ids.begin(),ids.end(),value.finish)==ids.end())
                    {value.finish=0;changed=true;status="Cleared incompatible war paint.";}
                    BeginDisabled(paints.empty());changed|=FDropdown("Finish / war paint",&value.finish,names,ids,FDropdownEnum::InlineTitle);EndDisabled();
                    tip(paints.empty()?"No usable paint recipe is available, or a gold finish is selected.":"Original finish restores authored textures. Atlas fallbacks can have different pattern placement.");
                    BeginDisabled(!value.finish);
                    changed|=FSlider("Wear",&value.wear,0.f,1.f,.01f,"%.2f");tip(WearName(value.wear));
                    changed|=FSlider("Pattern seed",&value.seed,0,1000000,1,"%i");
                    if(FButton("Randomize pattern seed")){value.seed=int(random()%1000001);changed=true;}
                    EndDisabled();
                }EndSection();
                if(CollapsingHeader("Viewmodel alignment"))
                {
                    Dummy({0,H::Draw.Scale(5)});
                    changed|=FSlider("Position X (depth)",&value.position[0],-50.f,50.f,.1f,"%.1f");
                    changed|=FSlider("Position Y (side)",&value.position[1],-50.f,50.f,.1f,"%.1f");
                    changed|=FSlider("Position Z (height)",&value.position[2],-50.f,50.f,.1f,"%.1f");
                    changed|=FSlider("Rotation X (pitch)",&value.rotation[0],-180.f,180.f,1.f,"%.0f");
                    changed|=FSlider("Rotation Y (yaw)",&value.rotation[1],-180.f,180.f,1.f,"%.0f");
                    changed|=FSlider("Rotation Z (roll)",&value.rotation[2],-180.f,180.f,1.f,"%.0f");
                    if(FButton("Reset viewmodel alignment",FButtonEnum::Compact,{0,22})){value.position={};value.rotation={};changed=true;}
                    tip("Zero restores authored placement. Alignment changes only your first-person cosmetic, not hands, hitboxes or shot origin.");
                }
                Dummy({0,H::Draw.Scale(3)});
                if(V::PerClass.Value&&FButton("Use shared selection for this class",FButtonEnum::Compact,{0,22}))
                {preset.erase({canonical,scope});dirty=presetModified=true;value=Get(preset,canonical,scope);changed=false;}
                // Explicit equal widths and SameLine share one baseline; the
                // legacy Right flag only shifts X and starts a separate row.
                const float resetWidth=std::max(1.f,(GetContentRegionAvail().x-GetStyle().ItemSpacing.x)*.5f)/H::Draw.Scale(1);
                if(FButton("Reset weapon",FButtonEnum::Compact,{resetWidth,22}))OpenPopup("Reset skin weapon");
                if(FButton("Reset all",FButtonEnum::Compact|FButtonEnum::SameLine,{resetWidth,22}))OpenPopup("Reset all skins");
                if(BeginSkinConfirmation("Reset skin weapon"))
                {
                    TextWrapped("Reset this weapon in the selected class/shared scope?");
                    const float actionWidth=SkinConfirmationButtonWidth(2);
                    if(FButton("Reset",FButtonEnum::Compact,{actionWidth,22}))
                    {preset.erase({canonical,scope});dirty=presetModified=true;value=Get(preset,canonical,scope);changed=false;CloseCurrentPopup();}
                    if(FButton("Cancel",FButtonEnum::Compact|FButtonEnum::SameLine,{actionWidth,22}))CloseCurrentPopup();EndSkinConfirmation();
                }
                if(BeginSkinConfirmation("Reset all skins"))
                {
                    TextWrapped("Reset every weapon and class cosmetic selection?");
                    const float actionWidth=SkinConfirmationButtonWidth(2);
                    if(FButton("Reset all",FButtonEnum::Compact,{actionWidth,22}))
                    {preset.clear();dirty=presetModified=true;value={};changed=false;CloseCurrentPopup();}
                    if(FButton("Cancel",FButtonEnum::Compact|FButtonEnum::SameLine,{actionWidth,22}))CloseCurrentPopup();EndSkinConfirmation();
                }
            }
            EndSection();
            TableNextColumn();
            Section("##SkinPresetColumn",0.f);
            if(Section("Skin presets"))request=DrawPresetBrowser();EndSection();
            if(editable)
            {
                if(Section("Killstreaks"))
                {
                    changed|=FDropdown("Killstreak tier",&value.tier,{"None / original","Basic","Specialized","Professional"},{},FDropdownEnum::InlineTitle);
                    BeginDisabled(value.tier<2);
                    changed|=FDropdown("Killstreak sheen",&value.sheen,{"Team Shine","Deadly Daffodil","Manndarin","Mean Green","Agonizing Emerald","Villainous Violet","Hot Rod"},{1,2,3,4,5,6,7},FDropdownEnum::InlineTitle);
                    EndDisabled();tip("Sheen requires Specialized or Professional tier.");
                    BeginDisabled(value.tier!=3);
                    changed|=FDropdown("Professional effect",&value.effect,{"Fire Horns","Cerebral Discharge","Tornado","Flames","Singularity","Incinerator","Hypno-Beam"},{2002,2003,2004,2005,2006,2007,2008},FDropdownEnum::InlineTitle);
                    tip("Eye effects appear at 5 kills and strengthen at 10. Professional tier is required.");
                    changed|=FToggle("Local effect preview",&value.preview);
                    tip("Preview changes only local effects, not displayed kill counts or milestones.");
                    BeginDisabled(!value.preview);changed|=FSlider("Preview streak count",&value.previewCount,0,100,1,"%i");EndDisabled();EndDisabled();
                }EndSection();
            }
            const size_t ready=std::count_if(peers.begin(),peers.end(),[](const auto& p){return p.second.ready;});
            const std::string networkTitle=std::format("Networking / {}###SkinNetworking",
                !V::Networking.Value?std::string("Off"):std::format("{} peers ready",ready));
            if(CollapsingHeader(networkTitle.c_str()))
            {
                Dummy({0,H::Draw.Scale(5)});
                FToggle(V::Networking);tip("Off by default. No cosmetic traffic while disabled. Save your main config to remember your opt-in.");
                BeginDisabled(!V::Networking.Value);FToggle(V::Share);FToggle(V::Receive);EndDisabled();
                tip("Only ready, consenting Nikogram peers exchange cosmetics. Skin presets cannot change these preferences.");
                Dummy({0,H::Draw.Scale(5)});
                const std::string summary=!V::Networking.Value?"Networking disabled.":std::format("{} peers ready.",ready);
                TextUnformatted(summary.c_str());tip(networkStatus.c_str());
                Dummy({0,H::Draw.Scale(5)});
            }
            if(editClass==9&&CollapsingHeader("Engineer Pip-Boy"))FToggle(V::PipBoy);
            EndSection();
            EndTable();
        }
        // Commit this frame's controls BEFORE a requested save/load/reset. A
        // loaded preset must never be overwritten by a stale pre-load value.
        if(changed&&editable&&value.Valid())
        {
            preset[{canonical,scope}]=value;dirty=presetModified=true;
            Diagnostic(std::format("event=selection_changed weapon={} canonical={} class_scope={} apply={} reskin={} australium={} festive={} finish={} paint_kit={} wear={} wear_level={} seed={} unusual={}",
                weaponID,canonical,scope,value.enabled,value.reskin,value.australium,value.festive,value.finish,PaintIndex(value.finish),value.wear,WearLevel(value.wear),value.seed,value.unusual));
        }
        ApplyPresetRequest(request);
    }
}
