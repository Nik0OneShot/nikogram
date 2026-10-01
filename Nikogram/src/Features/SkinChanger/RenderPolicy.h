#pragma once
#include <array>
#include <span>
#include <string_view>
#include <string>
#include <vector>
#include <cmath>
#include <optional>
#include <cstdint>
#include <utility>
#include <map>
#include <tuple>
#include <set>
#include <algorithm>
#include <cstring>
#include "Model.h"

namespace SkinRender
{
    inline bool NeedsFrozenStatuePose(int effects,bool native)
    {return !native&&(effects&(SkinModel::DeathGold|SkinModel::DeathIce));}
    // A temporary cosmetic sequence must not enter the native gameplay blend
    // history. Scope by entity so nested attachment/other-player draws retain
    // their normal transitions, and always restore the previous context.
    inline thread_local const void* cosmeticPoseEntity=nullptr;
    struct CosmeticPoseScope
    {
        const void* previous;
        explicit CosmeticPoseScope(const void* entity):previous(cosmeticPoseEntity){cosmeticPoseEntity=entity;}
        ~CosmeticPoseScope(){cosmeticPoseEntity=previous;}
        CosmeticPoseScope(const CosmeticPoseScope&)=delete;
        CosmeticPoseScope& operator=(const CosmeticPoseScope&)=delete;
    };
    inline bool CosmeticPoseActive(const void* entity){return entity&&entity==cosmeticPoseEntity;}
    // Exchange a draw-only native state descriptor, not its allocation. Both
    // normal return and exceptions restore the exact gameplay descriptor.
    template<class T> struct DrawStateScope
    {
        T& value;T original;
        DrawStateScope(T& target,const T& replacement):value(target),original(target){value=replacement;}
        ~DrawStateScope(){value=original;}
        DrawStateScope(const DrawStateScope&)=delete;
        DrawStateScope& operator=(const DrawStateScope&)=delete;
    };
    inline bool BlendHistoryFresh(double now,double last,bool sameIdentity)
    {return sameIdentity&&std::isfinite(now)&&std::isfinite(last)&&now>=last&&now-last<=.25;}
    inline bool HudCopyEligible(bool hudPanel,bool ownedCopy,int definition,int heldDefinition)
    {return hudPanel&&ownedCopy&&definition>=0&&definition==heldDefinition;}
    inline bool HudImmediateThink(bool enabled,bool previousCosmetic,bool sameHud,uint32_t previousWeapon,uint32_t weapon)
    {return (enabled||previousCosmetic)&&(!sameHud||previousWeapon!=weapon);}
    template<class Item> int HudPreviewSkin(const Item& item,int team,bool australium)
    {return team!=2&&team!=3?-1:australium?(team==3?item.goldBlue:item.goldRed):(team==3?item.defaultBlue:item.defaultRed);}
    template<class M,class Cache> bool RestoreGameplayBones(std::span<const M> original,Cache& cache)
    {
        if(original.empty()||original.size()>128||cache.Count()!=int(original.size())){cache.RemoveAll();return false;}
        std::memcpy(cache.Base(),original.data(),original.size_bytes());return true;
    }
    // Submission and engine confirmation are separate. A missing GUID is not
    // proof that the sound was inaudible, so never resubmit the same death.
    struct DeathSoundOnce
    {
        bool submitted=false,started=false;
        bool Begin(double now,double expires)
        {
            if(submitted||!std::isfinite(now)||!std::isfinite(expires)||now>expires)return false;
            submitted=true;return true;
        }
        void Complete(bool success){submitted=true;started|=success;}
    };
    inline bool CorpseExpired(bool enabled,double now,double seen)
    {return !enabled||!std::isfinite(now)||!std::isfinite(seen)||now-seen>120;}
    inline bool CorpseAllowed(uint32_t expected,uint32_t actual,bool shared,bool receiving,bool peerFresh)
    {return expected&&expected==actual&&(!shared||(receiving&&peerFresh));}
    struct FrozenPose
    {
        using Matrix=std::array<std::array<float,4>,3>;
        std::vector<Matrix> bones;
        template<class M> bool Copy(std::span<const M> source,std::span<M> output)
        {
            if(source.empty()||source.size()>128||source.size()!=output.size())return false;
            if(bones.empty())
            {
                std::vector<Matrix> next(source.size());
                for(size_t n=0;n<source.size();++n)for(int r=0;r<3;++r)for(int c=0;c<4;++c)
                {if(!std::isfinite(source[n][r][c]))return false;next[n][r][c]=source[n][r][c];}
                bones=std::move(next);
            }
            if(bones.size()!=output.size())return false;
            for(size_t n=0;n<bones.size();++n)for(int r=0;r<3;++r)for(int c=0;c<4;++c)output[n][r][c]=bones[n][r][c];
            return true;
        }
    };
    // Camera-oriented, render-only alignment around the original weapon root.
    // Source matrices are recopied each draw, so this never accumulates offsets.
    template<class Matrix> bool AlignBones(std::span<Matrix> bones,const Matrix& basis,const Matrix& rotation,
        const std::array<float,3>& pivot,const std::array<float,3>& position)
    {
        float world[3][3]{};float shift[3]{};
        for(int r=0;r<3;++r)
        {
            if(!std::isfinite(pivot[r])||!std::isfinite(position[r]))return false;
            for(int c=0;c<3;++c)
            {
                if(!std::isfinite(basis[r][c])||!std::isfinite(rotation[r][c]))return false;
                shift[r]+=basis[r][c]*position[c];
                for(int a=0;a<3;++a)for(int b=0;b<3;++b)world[r][c]+=basis[r][a]*rotation[a][b]*basis[c][b];
            }
        }
        for(auto& bone:bones)
        {
            Matrix next{};
            for(int r=0;r<3;++r)for(int c=0;c<4;++c)
            {
                next[r][c]=c==3?pivot[r]+shift[r]:0;
                for(int k=0;k<3;++k)next[r][c]+=world[r][k]*(bone[k][c]-(c==3?pivot[k]:0));
                if(!std::isfinite(next[r][c]))return false;
            }
            for(int r=0;r<3;++r)for(int c=0;c<4;++c)bone[r][c]=next[r][c];
        }
        return true;
    }
    inline bool CanReplaceKillIcon(int attacker,int victim,bool preserveDeathCause,std::string_view weapon)
    {
        if(attacker<=0||victim<=0||attacker==victim||preserveDeathCause)return false;
        const auto name=SkinModel::Lower(std::string(weapon));
        return name!="world"&&name!="worldspawn"&&name!="suicide"&&name!="fall"&&name!="trigger_hurt";
    }
    inline std::optional<int> KillWeaponDefinition(int reported,int held,int eventType,int heldType)
    {
        if(reported>=0&&reported<65535)return reported;
        if((reported==-1||reported==65535)&&held>=0&&held<65535&&eventType>0&&eventType==heldType)return held;
        return {}; // Unknown attribution must not borrow another weapon's kit.
    }
    template<class Profile> struct WeaponProfiles
    {
        int cls=0;std::map<int,Profile> values;
        void Remember(int weapon,int playerClass,const Profile& profile)
        {
            if(cls!=playerClass){values.clear();cls=playerClass;}
            if(!values.contains(weapon)&&values.size()>=32)values.erase(values.begin());
            values[weapon]=profile; // Disabled profiles revoke a previous selection too.
        }
        const Profile* Find(int weapon,int playerClass)const
        {auto it=values.find(weapon);return cls==playerClass&&it!=values.end()?&it->second:nullptr;}
    };
    struct MilestoneSoundGate
    {
        int last=0;
        bool Due(int count,bool mvm)
        {
            const int previous=last;last=std::max(0,count);
            return count>previous&&count>=5&&count<=1000000&&count%(mvm?20:5)==0;
        }
        void Reset(){last=0;}
    };
    struct SharedStreak
    {
        int count=-1,base=0,observed=0;uint32_t life=0,minimumLife=0;
        bool Sync(int value,uint32_t epoch,int witnessed)
        {
            if(value<0||value>1000000||!epoch||epoch<minimumLife||epoch<life)return false;
            if(epoch!=life||value<count||count<0){base=value;observed=witnessed;}
            count=value;life=epoch;minimumLife=0;return true;
        }
        void ResetLife()
        {
            if(count<0)return;
            // A fresh zero snapshot can arrive before the game's reset event.
            // Do not invalidate that new life a second time. Positive old-life
            // snapshots must advance their epoch before becoming visible again.
            if(count>0||base>0)minimumLife=std::max(minimumLife,life==UINT32_MAX?life:life+1);
            count=0;base=observed=0;
        }
        int Value(int witnessed)const{return count<0?-1:minimumLife?0:std::clamp(std::max(count,base+std::max(0,witnessed-observed)),0,1000000);}
    };
    inline int KillstreakCount(int tracked,int native,bool preview,int previewCount,bool allowPreview)
    {
        const int actual=native>=0&&native<=1000000?std::max(tracked,native):tracked;
        return allowPreview&&preview?previewCount:actual;
    }
    inline std::optional<int> KillstreakAttribute(std::string_view name,int tier,int sheen,int effect)
    {
        if(tier<1||tier>3||sheen<1||sheen>7||effect<2002||effect>2008)return {};
        if(name=="killstreak_tier")return tier;
        if(name=="killstreak_idleeffect")return tier>=2?sheen:0;
        if(name=="killstreak_effect")return tier==3?effect:0;
        return {};
    }
    struct LifeStreaks
    {
        std::map<uint32_t,int> counts;
        std::set<std::tuple<uintptr_t,uint32_t,uint32_t,bool>> seen;
        int seenFrame=-1;
        int Get(uint32_t player)const{auto it=counts.find(player);return it==counts.end()?0:it->second;}
        void Reset(uint32_t player){counts.erase(player);}
        void Clear(){counts.clear();seen.clear();seenFrame=-1;}
        void Spawn(uint32_t player)
        {
            Reset(player);
            std::erase_if(seen,[&](const auto& key){return std::get<1>(key)==player;});
        }
        bool DeathOnce(uintptr_t event,int frame,uint32_t victim,uint32_t attacker,bool feign,bool credit=true)
        {
            // Both the native HUD and our listener receive the same event.
            // Never retain event identities across frames or dereference them.
            if(frame!=seenFrame){seen.clear();seenFrame=frame;}
            if(seen.size()>=256)return false;
            if(!seen.emplace(event,victim,attacker,feign).second)return false;
            Death(victim,attacker,feign,credit);return true;
        }
        void Death(uint32_t victim,uint32_t attacker,bool feign,bool credit=true)
        {
            if(feign)return;Reset(victim);
            if(credit&&victim&&attacker&&attacker!=victim){if(!counts.contains(attacker)&&counts.size()>=128)counts.clear();auto& count=counts[attacker];if(count<1000000)++count;}
        }
    };
    struct PaintRequestGate
    {
        int kit=-1,wear=-1,seed=-1;double changed=0;bool immediate=false;
        void Observe(int nextKit,int nextWear,int nextSeed,double now)
        {
            if(kit==nextKit&&wear==nextWear&&seed==nextSeed)return;
            immediate=kit!=nextKit||wear!=nextWear;
            kit=nextKit;wear=nextWear;seed=nextSeed;changed=now;
        }
        bool Ready(double now)const{return immediate||now-changed>=.12;}
    };
    template<class Texture> struct RetainedPaint
    {
        Texture* texture=nullptr;
        RetainedPaint()=default;
        RetainedPaint(const RetainedPaint&)=delete;
        RetainedPaint& operator=(const RetainedPaint&)=delete;
        ~RetainedPaint(){Set(nullptr);}
        void Set(Texture* next)
        {if(next==texture)return;if(next)next->AddRef();if(texture)texture->Release();texture=next;}
    };
    inline int PaintMaterialCapacity(int reported,int modelType,int studioTextures)
    {
        // Installed engine Mod_GetModelMaterialCount only counts brush models;
        // Mod_GetModelMaterials does support studio models (mod_studio == 3).
        if(reported>0&&reported<=128)return reported;
        if(reported==0&&modelType==3&&studioTextures>0&&studioTextures<=128)return studioTextures;
        return 0;
    }
    // Hold both textures across SetTextureValue, which releases the material's
    // previous reference. Movable bindings also clean up failed constructors.
    template<class Var,class Texture> struct SurfaceTextureBinding
    {
        Var* var=nullptr;Texture* original=nullptr;Texture* painted=nullptr;
        SurfaceTextureBinding(Var* v,Texture* p):var(v),original(v->GetTextureValue()),painted(p)
        {original->AddRef();painted->AddRef();Apply();}
        SurfaceTextureBinding(SurfaceTextureBinding&& other)noexcept
            :var(std::exchange(other.var,nullptr)),original(std::exchange(other.original,nullptr)),painted(std::exchange(other.painted,nullptr)){}
        SurfaceTextureBinding(const SurfaceTextureBinding&)=delete;
        SurfaceTextureBinding& operator=(const SurfaceTextureBinding&)=delete;
        ~SurfaceTextureBinding(){if(var){Suspend();original->Release();painted->Release();}}
        void Apply(){if(var)var->SetTextureValue(painted);}
        void Suspend(){if(var)var->SetTextureValue(original);}
    };
    struct HandSequenceKey
    {
        uintptr_t viewmodel=0,header=0;
        uint32_t weapon=0;
        int item=0,reskin=0,team=0,playerClass=0,source=-1,activity=-1,count=0,sequenceParity=0,eventParity=0;
        bool operator==(const HandSequenceKey&) const = default;
    };
    // Weighted native selection is random. Select once per animation instance,
    // shared by event processing and every normal/chams/glow render pass.
    struct HandSequenceLatch
    {
        HandSequenceKey key{};bool valid=false;float lastCycle=0;int replacement=-1;
        void Clear(){valid=false;replacement=-1;lastCycle=0;}
        template<typename Select> int Resolve(const HandSequenceKey& next,float cycle,Select select)
        {
            if(!std::isfinite(cycle)){Clear();return -1;}
            // Small prediction/interpolation corrections are not a new swing.
            const bool restarted=valid && lastCycle-cycle>.5f;
            if(!valid||!(next==key)||restarted){key=next;replacement=select();valid=true;}
            lastCycle=cycle;return replacement;
        }
    };
    inline std::optional<std::string> BoundedModelString(std::span<const char> bytes,int64_t offset)
    {
        if(offset<0||uint64_t(offset)>=bytes.size())return std::nullopt;
        std::string result;
        for(size_t index=size_t(offset);index<bytes.size()&&result.size()<128;++index)
        {if(bytes[index]==0)return result;result.push_back(bytes[index]);}
        return std::nullopt;
    }
    inline bool AllowCosmeticRender(bool chams,bool glow,bool viewmodelEffect=false){return !chams&&!glow&&!viewmodelEffect;}
    // Escape-menu visibility pauses visual overlays, not enabled cosmetics.
    inline bool BypassMenuCosmetics(bool menuVisible,bool skinEnabled){return menuVisible&&!skinEnabled;}
    inline void SpyAllClassHands(std::map<std::string,std::string>& animations)
    {
        for(auto suffix:{"DRAW","HOLSTER","IDLE","HITCENTER","SWINGHARD"})
            animations[std::string("ACT_VM_")+suffix]=std::string("ACT_MELEE_ALLCLASS_VM_")+suffix;
        for(auto suffix:{"UP","DOWN","IDLE"})animations[std::string("ACT_BACKSTAB_VM_")+suffix]="ACT_MELEE_ALLCLASS_VM_IDLE";
        animations["ACT_MELEE_VM_STUN"]="ACT_MELEE_ALLCLASS_VM_IDLE";
    }
    template<class Map> std::map<std::string,std::string> PlayerActivityMap(std::string_view from,std::string_view to,const Map& original,const Map& chosen)
    {
        std::map<std::string,std::string> out;
        if(from!=to&&!from.empty()&&!to.empty())
            for(auto pose:{"STAND","CROUCH","RUN","WALK","AIRWALK","CROUCHWALK","JUMP","JUMP_START","JUMP_FLOAT","JUMP_LAND","SWIM","DOUBLEJUMP_CROUCH",
                "ATTACK_STAND","ATTACK_CROUCH","ATTACK_SWIM","ATTACK_AIRWALK","RELOAD_STAND","RELOAD_CROUCH","RELOAD_SWIM"})
            {
                const auto prefix=std::string("ACT_MP_")+pose+"_";
                out[prefix+std::string(from)]=prefix+std::string(to);
                for(auto suffix:{"_LOOP","_END","_DEPLOYED","_ALT"})out[prefix+std::string(from)+suffix]=prefix+std::string(to)+suffix;
            }
        for(const auto& [key,value]:original)if(key.starts_with("ACT_MP_"))
            out[value]=chosen.contains(key)?chosen.at(key):key;
        for(const auto& [key,value]:chosen)if(key.starts_with("ACT_MP_"))
            out[original.contains(key)?original.at(key):key]=value;
        std::erase_if(out,[](const auto& pair){return pair.first==pair.second;});return out;
    }
    inline bool AllowCosmeticPreparation(bool chams,bool glow,bool effectPass,bool customBones)
    {return effectPass?customBones:(!chams&&!glow);}
    // Resolve activity names through the game's registry, not assumed enum numbers
    // or borrowed CStudioHdr/sequence descriptors. An empty override means stock
    // class translation should continue normally.
    template<typename Map,typename Lookup> std::optional<int> CosmeticHandActivity(const Map& replacements,int activity,Lookup lookup)
    {
        if(activity<=0)return std::nullopt;
        for(const auto& [from,to]:replacements)
            if(lookup(from)==activity){int replacement=lookup(to);return replacement>0?std::optional<int>(replacement):std::nullopt;}
        return activity;
    }
    template<typename Map,typename Lookup> std::optional<int> RenderHandActivity(const Map& replacements,int activity,Lookup lookup)
    {
        if(activity<=0)return std::nullopt;
        for(const auto& [from,to]:replacements)
        {
            const int target=lookup(to);if(target==activity)return activity;
            bool match=lookup(from)==activity;
            if(from.starts_with("ACT_VM_"))for(auto prefix:{"ACT_MELEE_VM_","ACT_MELEE_ALLCLASS_VM_","ACT_PRIMARY_VM_","ACT_SECONDARY_VM_","ACT_PDA_VM_","ACT_BUILDING_VM_"})
                match=match||lookup(std::string(prefix)+from.substr(7))==activity;
            if(from=="ACT_RELOAD_START"||from=="ACT_RELOAD_FINISH")for(auto prefix:{"ACT_PRIMARY_","ACT_SECONDARY_","ACT_SECONDARY2_"})
                match=match||lookup(std::string(prefix)+from.substr(4))==activity;
            if(match)return target>0?std::optional<int>(target):std::nullopt;
        }
        return activity;
    }
    inline bool AllowEffectPreparation(bool effect,bool registered,bool headerReady,bool hardwareReady,bool customBones)
    {return !effect||(registered&&headerReady&&hardwareReady&&customBones);}
    template<typename T,typename Invalidate> struct RenderSequenceScope
    {
        T* entity;int previous;bool changed;Invalidate invalidate;
        RenderSequenceScope(T* value,int replacement,Invalidate fn):entity(value),previous(value?value->m_nSequence():-1),
            changed(value&&replacement>=0&&replacement!=previous),invalidate(fn)
        {if(changed){entity->m_nSequence()=replacement;invalidate(entity);}}
        ~RenderSequenceScope(){if(changed){entity->m_nSequence()=previous;invalidate(entity);}}
        RenderSequenceScope(const RenderSequenceScope&)=delete;
        RenderSequenceScope& operator=(const RenderSequenceScope&)=delete;
    };
    inline float AuthoredCycle(double elapsed,float duration,float rate,bool loops)
    {
        if(!std::isfinite(elapsed)||!std::isfinite(duration)||!std::isfinite(rate)||duration<=0)return 0;
        const double cycle=std::max(0.,elapsed)*std::max(0.f,rate)/duration;
        return float(loops?std::fmod(cycle,1.):std::min(cycle,1.));
    }
    template<typename T,typename Invalidate> struct TimedSequenceScope
    {
        T* entity;float cycle;RenderSequenceScope<T,Invalidate> sequence;
        TimedSequenceScope(T* value,int replacement,std::optional<float> replacementCycle,Invalidate fn)
            :entity(value),cycle(value?value->m_flCycle():0),sequence(value,replacement,fn)
        {if(value&&replacementCycle)value->m_flCycle()=*replacementCycle;}
        ~TimedSequenceScope(){if(entity)entity->m_flCycle()=cycle;}
    };
    inline bool ReadyModelHeader(bool error,std::string_view firstBone,int count)
    {return !error&&count>0&&count<=128&&firstBone!="dummy_bone";}
    inline std::string HandActivityKey(std::string_view activity)
    {
        // Activity names avoid dependence on Valve's changing enum numbers.
        if(activity=="ACT_MELEE_VM_STUN"||activity.starts_with("ACT_MELEE_VM_INSPECT_"))return std::string(activity);
        if(activity.starts_with("ACT_MELEE_VM_"))return "ACT_VM_"+std::string(activity.substr(13));
        if(activity.starts_with("ACT_PRIMARY_VM_"))return "ACT_VM_"+std::string(activity.substr(15));
        if(activity.starts_with("ACT_SECONDARY_VM_"))return "ACT_VM_"+std::string(activity.substr(17));
        return std::string(activity);
    }
    inline std::string NativeHandActivity(std::string_view activity,std::string_view slot)
    {
        if(activity.starts_with("ACT_VM_"))
        {
            // Generic activities need class translation; named item families
            // such as ACT_VM_RELOAD_QRL are already the native target names.
            for(auto generic:{"DRAW","HOLSTER","IDLE","PULLBACK","PRIMARYATTACK","SECONDARYATTACK","RELOAD","DRYFIRE","HITCENTER","SWINGHARD"})
                if(activity.substr(7)==generic)return "ACT_"+std::string(slot)+"_VM_"+std::string(generic);
        }
        if(activity=="ACT_RELOAD_START"||activity=="ACT_RELOAD_FINISH")return "ACT_"+std::string(slot)+"_"+std::string(activity.substr(4));
        return std::string(activity);
    }
    template<class Map> std::map<std::string,std::string> HandActivityMap(std::string_view from,std::string_view to,const Map& original,const Map& chosen)
    {
        std::map<std::string,std::string> out;
        for(const auto& [key,value]:chosen)if(!key.starts_with("ACT_MP_"))
            out[key]=NativeHandActivity(value,to);
        for(const auto& [key,value]:original)if(!key.starts_with("ACT_MP_"))
            out[NativeHandActivity(value,from)]=chosen.contains(key)?NativeHandActivity(chosen.at(key),to):NativeHandActivity(key,to);
        std::erase_if(out,[](const auto& pair){return pair.first==pair.second;});return out;
    }
    template<typename Map> std::string ReplacementHandActivity(std::string_view activity,const Map& original,const Map& chosen)
    {
        std::string key=HandActivityKey(activity);
        for(const auto& [from,to]:original)if(to==activity){key=from;break;}
        auto replacement=chosen.find(key);
        if(replacement!=chosen.end())return replacement->second;
        // Returning to the original stock animation family must also undo a
        // selected item's ITEM1/ITEM2 replacement, not leave the hands behind.
        if(original.contains(key)&&original.at(key)==activity && !chosen.contains(key))return key;
        return std::string(activity);
    }
    inline bool AllowCosmeticTracer(bool doEffects,bool cleanScreenshot,bool local,std::string_view configured)
    {return doEffects&&!cleanScreenshot&&(!local||configured=="Default");}
    template<typename Create,typename SetPoint> bool CreateNamedTracer(Create create,SetPoint setPoint)
    {
        auto effect=create();if(!effect)return false;
        setPoint(effect,0);setPoint(effect,1);return true;
    }
    template<typename T> struct ScopedPointer
    {
        const T*& value;const T* previous;
        ScopedPointer(const T*& target,const T* replacement):value(target),previous(target){value=replacement;}
        ~ScopedPointer(){value=previous;}
        ScopedPointer(const ScopedPointer&)=delete;
        ScopedPointer& operator=(const ScopedPointer&)=delete;
    };
    enum class ViewmodelPointerKind { None, Entity, Renderable };
    inline ViewmodelPointerKind MatchViewmodel(const void* drawing,const void* entity,const void* renderable)
    {
        if(!drawing||!entity)return ViewmodelPointerKind::None;
        if(drawing==entity)return ViewmodelPointerKind::Entity;
        if(renderable&&drawing==renderable)return ViewmodelPointerKind::Renderable;
        return ViewmodelPointerKind::None;
    }
    struct ViewmodelDrawScope
    {
        bool& context;bool previous;
        ViewmodelDrawScope(bool& value,bool local):context(value),previous(value){context=local;}
        ~ViewmodelDrawScope(){context=previous;}
        ViewmodelDrawScope(const ViewmodelDrawScope&)=delete;
        ViewmodelDrawScope& operator=(const ViewmodelDrawScope&)=delete;
    };
    inline bool LocalAttachment(bool modelMatches,bool localParent,bool localDraw,int entityIndex)
    {
        // A client-only addon has no network index or reliable EHANDLE parent.
        // Accept it only inside the verified local viewmodel draw, with an exact weapon-model match.
        return modelMatches && (localParent || (localDraw && entityIndex==-1));
    }
    enum class BoneSource { Custom, Setup, Unavailable, InvalidLayout };
    inline bool ValidSkin(int skin,int families){return families>0&&skin>=0&&skin<families;}
    inline int ReplacementLOD(int root,int count,int original,bool shadowLast)
    {
        const int visible=count-(shadowLast?1:0);
        if(count<1||count>8||root<0||root>=visible)return -1;
        return original<root?root:(original>=visible?visible-1:original);
    }
    template<typename Cache> struct ModelCacheScope
    {
        Cache* cache;
        explicit ModelCacheScope(Cache* value):cache(value){if(cache)cache->BeginLock();}
        ~ModelCacheScope(){if(cache)cache->EndLock();}
        ModelCacheScope(const ModelCacheScope&)=delete;
        ModelCacheScope& operator=(const ModelCacheScope&)=delete;
    };
    // A skin-only draw retains the engine's original pointer, including nullptr.
    template<typename Matrix> Matrix* SkinOnlyBones(Matrix* original){return original;}
    template<typename Matrix,size_t Capacity,typename Setup>
    BoneSource AcquireBones(Matrix* original,std::array<Matrix,Capacity>& scratch,int count,Matrix*& result,Setup setup)
    {
        result=nullptr;
        if(count<1||count>int(Capacity))return BoneSource::InvalidLayout;
        if(original){result=original;return BoneSource::Custom;}
        if(!setup(scratch.data(),int(Capacity)))return BoneSource::Unavailable;
        result=scratch.data();return BoneSource::Setup;
    }
    inline bool BoneMap(std::span<const std::string_view> source,std::span<const std::string_view> target,
        std::vector<int>& output,std::string_view& missing)
    {
        output.clear();missing={};if(source.empty()||target.empty()||source.size()>128||target.size()>128)return false;
        for(auto name:target)
        {
            int found=-1;for(size_t n=0;n<source.size();++n)if(!name.empty()&&source[n]==name){found=int(n);break;}
            if(found<0){output.clear();missing=name;return false;}output.push_back(found);
        }
        return true;
    }
    inline bool RestBoneMap(std::span<const std::string_view> source,std::span<const std::string_view> target,
        std::span<const int> parents,std::span<const int> procedural,std::vector<int>& output,std::string_view& missing,bool rigidJiggle=false)
    {
        output.clear();missing={};
        if(source.empty()||target.empty()||source.size()>128||target.size()>128||parents.size()!=target.size()||procedural.size()!=target.size())return false;
        for(size_t bone=0;bone<target.size();++bone)
        {
            int found=-1;for(size_t n=0;n<source.size();++n)if(!target[bone].empty()&&source[n]==target[bone]){found=int(n);break;}
            // A same-named spring on another reskin is not the target's spring.
            // Resolve it from its authored parent/bind pose and parameters too.
            if(rigidJiggle&&procedural[bone]==5&&parents[bone]>=0)found=-1;
            output.push_back(found);
        }
        for(size_t bone=0;bone<target.size();++bone)if(output[bone]<0)
        {
            int parent=int(bone);size_t hops=0;
            while(parent>=0&&parent<int(target.size())&&output[parent]<0&&hops++<target.size())
            {if(procedural[parent]!=0&&!(rigidJiggle&&procedural[parent]==5))break;parent=parents[parent];}
            if(parent<0||parent>=int(target.size())||output[parent]<0)
            {missing=target[bone];output.clear();return false;}
        }
        return true;
    }
    // Wearable skeletons can include unweighted ancestors absent from arms.
    // Permit reconstruction from a named descendant, never an arbitrary root.
    inline bool AnchoredBoneMap(std::span<const std::string_view> source,std::span<const std::string_view> target,
        std::span<const int> parents,std::span<const int> procedures,std::vector<int>& mapping)
    {
        if(source.empty()||target.empty()||source.size()>128||target.size()>128||parents.size()!=target.size()||procedures.size()!=target.size())return false;
        mapping.assign(target.size(),-1);std::vector<bool> reachable(target.size(),false);
        for(size_t n=0;n<target.size();++n)for(size_t s=0;s<source.size();++s)if(!target[n].empty()&&target[n]==source[s])
        {mapping[n]=int(s);reachable[n]=true;break;}
        for(size_t pass=0;pass<target.size();++pass)for(size_t n=0;n<target.size();++n)
        {
            const int parent=parents[n];if(parent<0)continue;if(parent>=int(target.size())||parent==int(n))return false;
            if(procedures[n]!=0||procedures[parent]!=0)continue;
            if(reachable[n]||reachable[parent])reachable[n]=reachable[parent]=true;
        }
        return std::all_of(reachable.begin(),reachable.end(),[](bool value){return value;});
    }
    template<typename Matrix> bool RebaseAttachment(const Matrix& oldParent,const Matrix& newParent,const Matrix& child,Matrix& out)
    {
        Matrix inverse{};
        for(int r=0;r<3;++r)for(int c=0;c<4;++c)
            if(!std::isfinite(oldParent[r][c])||!std::isfinite(newParent[r][c])||!std::isfinite(child[r][c]))return false;
        for(int r=0;r<3;++r){for(int c=0;c<3;++c)inverse[r][c]=oldParent[c][r];
            for(int c=0;c<3;++c)inverse[r][3]-=inverse[r][c]*oldParent[c][3];}
        auto concat=[](const Matrix& a,const Matrix& b,Matrix& result){for(int r=0;r<3;++r){
            for(int c=0;c<3;++c){result[r][c]=0;for(int n=0;n<3;++n)result[r][c]+=a[r][n]*b[n][c];}
            result[r][3]=a[r][3];for(int n=0;n<3;++n)result[r][3]+=a[r][n]*b[n][3];}};
        Matrix local{};concat(inverse,child,local);concat(newParent,local,out);return true;
    }
    template<typename Matrix> bool RestTransform(const Matrix& parentWorld,const Matrix& parentPoseToBone,
        const Matrix& childPoseToBone,Matrix& result)
    {
        // poseToBone maps model-space to a bone's bind space; invert the rigid child transform.
        for(int row=0;row<3;++row)for(int col=0;col<4;++col)
            if(!std::isfinite(parentWorld[row][col])||!std::isfinite(parentPoseToBone[row][col])||!std::isfinite(childPoseToBone[row][col]))return false;
        for(const Matrix* pose:{&parentPoseToBone,&childPoseToBone})for(int a=0;a<3;++a)for(int b=0;b<3;++b)
        {float dot=0;for(int n=0;n<3;++n)dot+=(*pose)[n][a]*(*pose)[n][b];if(std::abs(dot-(a==b?1.f:0.f))>.05f)return false;}
        Matrix inverse{},intermediate{};
        for(int row=0;row<3;++row)
        {for(int col=0;col<3;++col)inverse[row][col]=childPoseToBone[col][row];
            for(int n=0;n<3;++n)inverse[row][3]-=inverse[row][n]*childPoseToBone[n][3];}
        auto concat=[](const Matrix& a,const Matrix& b,Matrix& out)
        {for(int row=0;row<3;++row){for(int col=0;col<3;++col){out[row][col]=0;for(int n=0;n<3;++n)out[row][col]+=a[row][n]*b[n][col];}
            out[row][3]=a[row][3];for(int n=0;n<3;++n)out[row][3]+=a[row][n]*b[n][3];}};
        concat(parentWorld,parentPoseToBone,intermediate);concat(intermediate,inverse,result);
        for(int row=0;row<3;++row)for(int col=0;col<4;++col)if(!std::isfinite(result[row][col]))return false;
        return true;
    }
}
