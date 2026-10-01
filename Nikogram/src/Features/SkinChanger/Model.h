#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace SkinModel
{
    struct Node
    {
        std::string name,value;
        std::vector<Node> children;
        const Node* Find(std::string_view key) const {for(const auto& n:children)if(n.name==key)return &n;return nullptr;}
        std::string Text(std::string_view key,std::string fallback={}) const {auto p=Find(key);return p?p->value:fallback;}
    };
    inline int Number(std::string_view s,int fallback=0)
    {int value=0;auto result=std::from_chars(s.data(),s.data()+s.size(),value);return result.ec==std::errc() && result.ptr==s.data()+s.size()?value:fallback;}
    class Parser
    {
        std::string_view text;size_t pos=0,nodes=0;bool valid=true;
        std::optional<std::string> Token()
        {
            for(;;){while(pos<text.size() && std::isspace(static_cast<unsigned char>(text[pos])))++pos;
                if(pos+1<text.size() && text[pos]=='/' && text[pos+1]=='/'){while(pos<text.size() && text[pos]!='\n')++pos;}else break;}
            if(pos==text.size())return {};
            char c=text[pos++];if(c=='{'||c=='}')return std::string(1,c);
            std::string value;
            if(c=='"')
            {
                bool closed=false;
                while(pos<text.size()) {c=text[pos++];if(c=='"'){closed=true;break;}
                    if(c=='\\' && pos<text.size() && (text[pos]=='"'||text[pos]=='\\'))c=text[pos++];
                    value+=c;if(value.size()>4096){valid=false;return {};}}
                if(!closed)valid=false;
            }
            else {value+=c;while(pos<text.size() && !std::isspace(static_cast<unsigned char>(text[pos])) && text[pos]!='{' && text[pos]!='}')
                {value+=text[pos++];if(value.size()>4096){valid=false;return {};}}}
            return value;
        }
        bool Read(Node& out,int depth,bool block)
        {
            if(depth>64)return false;
            while(auto name=Token())
            {
                if(*name=="}")return block;
                if(*name=="{" || ++nodes>500000)return false;
                auto value=Token();if(!value || *value=="}")return false;
                Node child;child.name=std::move(*name);
                if(*value=="{"){if(!Read(child,depth+1,true))return false;}else child.value=std::move(*value);
                out.children.push_back(std::move(child));
            }
            return !block && valid;
        }
    public:
        explicit Parser(std::string_view data):text(data){}
        std::optional<Node> Parse(){Node root;if(text.size()>16*1024*1024 || !Read(root,0,false) || !valid)return {};return root;}
    };
    inline void Merge(Node& destination,const Node& source)
    {
        for(const auto& value:source.children)
        {
            auto it=std::find_if(destination.children.begin(),destination.children.end(),[&](const Node& n){return n.name==value.name;});
            if(it==destination.children.end())destination.children.push_back(value);
            else if(!value.children.empty())Merge(*it,value);else *it=value;
        }
    }
    inline Node Resolve(const Node& item,const Node* prefabs,std::set<std::string>& visiting,int depth=0)
    {
        Node result;if(depth>24)return result;
        std::istringstream names(item.Text("prefab"));std::vector<std::string> list;std::string name;
        while(names>>name)list.push_back(name);
        // Valve applies prefabs right-to-left, followed by the item's own fields.
        for(auto it=list.rbegin();it!=list.rend();++it)if(prefabs && visiting.insert(*it).second)
        {if(auto prefab=prefabs->Find(*it))Merge(result,Resolve(*prefab,prefabs,visiting,depth+1));visiting.erase(*it);}
        Merge(result,item);return result;
    }
    inline std::string Lower(std::string value){for(char& c:value)c=char(std::tolower(static_cast<unsigned char>(c)));return value;}
    inline bool CosmeticAttribute(std::string_view name)
    {
        return name.starts_with("kill eater") || name.starts_with("killstreak") || name.starts_with("set_item_texture")
            || name=="is australium item" || name=="item style override" || name=="is_festivized" || name=="paintkit_proto_def_index"
            || name=="disable fancy class select anim" || name=="turn to gold" || name=="cannot trade"
            || name=="always tradable" || name=="is marketable" || name=="ragdolls become ash"
            || name=="ragdolls plasma effect" || name=="freeze backstab victim";
    }
    constexpr std::array<const char*,16> SoundCategories={"empty","single_shot","single_shot_npc","double_shot","double_shot_npc","burst","reload","reload_npc","melee_miss","melee_hit","melee_hit_world","special1","special2","special3","taunt","deploy"};
    inline bool SafeEffectName(std::string_view name)
    {
        if(name.empty()||name.size()>127||name.find("..")!=std::string_view::npos)return false;
        for(unsigned char c:name)if(!std::isalnum(c)&&c!='_'&&c!='.'&&c!='/'&&c!='-')return false;
        return true;
    }
    struct CosmeticEffects
    {
        bool active=false;
        std::array<std::string,16> sounds{},sourceSounds{};
        std::string muzzle,tracer,sourceMuzzle;
        void Read(const Node* node)
        {
            if(!node)return;
            auto read=[&](const std::string& key,std::string& out){auto value=node->Find(key);if(value&&SafeEffectName(value->value))out=value->value;};
            for(size_t n=0;n<sounds.size();++n)read(std::string("sound_")+SoundCategories[n],sounds[n]);
            read("muzzle_flash",muzzle);read("tracer_effect",tracer);
        }
        std::string Sound(int index) const {return index>=0&&index<int(sounds.size())?sounds[index]:std::string{};}
        std::string ReplaceSound(std::string_view original) const
        {
            if(!active||original.empty())return {};
            for(size_t n=0;n<sounds.size();++n)if(!sourceSounds[n].empty()&&Lower(sourceSounds[n])==Lower(std::string(original))&&sounds[n]!=sourceSounds[n])return sounds[n];
            return {};
        }
        void Fallback(const CosmeticEffects& script)
        {
            for(size_t n=0;n<sounds.size();++n)if(sounds[n].empty())sounds[n]=script.sounds[n];
            if(muzzle.empty())muzzle=script.muzzle;if(tracer.empty())tracer=script.tracer;
        }
        std::string Muzzle(std::string_view original) const
        {return active&&!muzzle.empty()&&!sourceMuzzle.empty()&&sourceMuzzle==original&&muzzle!=original?muzzle:std::string{};}
        std::string Tracer(int team,bool critical) const
        {return tracer.empty()||(team!=2&&team!=3)?std::string{}:tracer+(team==2?"_red":"_blue")+(critical?"_crit":"");}
    };
    inline std::string WaveKey(std::string_view wave)
    {
        if(wave.size()>260||wave.find("..")!=std::string_view::npos||wave.find(':')!=std::string_view::npos)return {};
        while(!wave.empty()&&!std::isalnum(static_cast<unsigned char>(wave.front()))&&wave.front()!='_'&&wave.front()!='/')wave.remove_prefix(1);
        if(wave.empty()||wave.size()>260||wave.find("..")!=std::string_view::npos||wave.find(':')!=std::string_view::npos)return {};
        auto key=Lower(std::string(wave));std::replace(key.begin(),key.end(),'\\','/');return key;
    }
    inline void ReadSoundWaves(const Node& root,std::map<std::string,std::set<std::string>>& out)
    {
        for(const auto& script:root.children)
        {
            if(!SafeEffectName(script.name)||(!out.contains(Lower(script.name))&&out.size()>=16384))continue;
            auto add=[&](const Node& entry){if(Lower(entry.name)!="wave")return;auto key=WaveKey(entry.value);if(!key.empty()&&out[Lower(script.name)].size()<64)out[Lower(script.name)].insert(key);};
            for(const auto& entry:script.children){add(entry);if(Lower(entry.name)=="rndwave")for(const auto& wave:entry.children)add(wave);}
        }
    }
    constexpr int MaxCosmeticAttachments=8;
    struct Attachment
    {
        std::string model;int skin=-1;
        bool operator==(const Attachment&) const = default;
    };
    inline std::vector<Attachment> ReadAttachments(const Node* visuals,std::string_view field)
    {
        std::vector<Attachment> result;
        auto list=visuals?visuals->Find(field):nullptr;if(!list)return result;
        for(const auto& child:list->children)
        {
            auto path=child.Text("model");
            if(path.size()>260||!path.starts_with("models/")||!path.ends_with(".mdl")||path.find("..")!=std::string::npos)continue;
            Attachment value{path,Number(child.Text("skin"),-1)};
            if(value.skin < -1 || value.skin>255)continue;
            if(std::find(result.begin(),result.end(),value)==result.end()&&result.size()<MaxCosmeticAttachments)result.push_back(std::move(value));
        }
        return result;
    }
    enum DeathEffect { DeathGold=1,DeathAsh=2,DeathPlasma=4,DeathIce=8 };
    inline int AuthoredDeathEffect(std::string_view name,std::string_view value)
    {
        if(Number(value)==0)return 0;
        if(name=="turn to gold")return DeathGold;
        if(name=="ragdolls become ash")return DeathAsh;
        if(name=="ragdolls plasma effect")return DeathPlasma;
        if(name=="freeze backstab victim")return DeathIce;
        return 0;
    }
    struct Item
    {
        int id=0,classes=0,goldRed=-1,goldBlue=-1;bool festive=false,festivized=false,decorated=false,paintable=false;
        std::string name,token,family,model,slot,materialOverride,weaponType,animSlot,killIcon;std::map<int,std::string> classModels;
        int defaultRed=0,defaultBlue=1,deathEffects=0;bool golden=false;int sapperVoice=0;float sapperVoiceWait=0;
        std::string viewModel,worldModel,particleSuffix;std::map<std::string,std::string> animations;
        std::array<std::map<std::string,std::string>,3> teamAnimations;
        const auto& Animations(int team) const {return teamAnimations[team==2?1:team==3?2:0];}
        std::string ViewModel(int cls) const {return viewModel.empty()?Model(cls):viewModel;}
        std::string WorldModel(int cls) const {return worldModel.empty()?Model(cls):worldModel;}
        bool MatchesModel(std::string_view path,int cls)const
        {return path==Model(cls)||path==ViewModel(cls)||path==WorldModel(cls)
            ||(Sapper()&&SapperBase()=="sapper"&&path=="models/weapons/w_models/w_sapper.mdl");}
        bool Sapper()const{return slot=="building"&&(weaponType=="tf_weapon_sapper"||weaponType=="tf_weapon_builder");}
        bool Watch()const{return weaponType=="tf_weapon_invis"&&slot=="pda2";}
        std::string SapperBase()const
        {
            if(!Sapper())return {};auto path=WorldModel(8);auto slash=path.find_last_of('/');
            auto name=path.substr(slash==std::string::npos?0:slash+1);
            if(!name.starts_with("c_")||!name.ends_with(".mdl"))return {};
            return name.substr(2,name.size()-6);
        }
        std::string PlacedSapper(bool placement)const
        {auto base=SapperBase();return base.empty()?std::string{}:"models/buildables/"+base+(placement?"_placement.mdl":"_placed.mdl");}
        std::string SapperTimer()const
        {auto base=SapperBase();return base.empty()?std::string{}:"Weapon_"+base+".Timer";}
        std::array<CosmeticEffects,3> effects; // base, RED, BLU; team fields overlay base fields
        std::array<std::vector<Attachment>,3> attachments,festiveAttachments;
        const auto& Attachments(int team)const{return attachments[team==2?1:team==3?2:0];}
        const auto& FestivizerAttachments(int team)const{return festiveAttachments[team==2?1:team==3?2:0];}
        bool CanFestivize()const{return festivized&&!festive&&!botkiller&&(!festiveAttachments[0].empty()||!festiveAttachments[1].empty()||!festiveAttachments[2].empty());}
        bool botkiller=false;
        std::string extraWorld,extraView;int extraRed=0,extraBlue=1;
        std::string ExtraModel(bool viewmodel) const {return botkiller?(viewmodel?extraView:extraWorld):std::string{};}
        int ExtraSkin(int team) const {return team==3?extraBlue:extraRed;}
        const CosmeticEffects& Effects(int team) const {return effects[team==2?1:team==3?2:0];}
        std::string Model(int cls) const {auto it=classModels.find(cls);if(it!=classModels.end())return it->second;
            if(!model.empty())return model;if(cls==8&&slot=="melee"&&MultiClass()&&!classModels.empty())return classModels.begin()->second;return {};}
        bool Supports(int cls) const {return cls>=1 && cls<=9 && (classes&(1<<cls));}
        bool Gold() const {return goldRed>=0 && goldBlue>=0;}
        bool MultiClass()const{return classes&&(classes&(classes-1));}
    };
    constexpr std::array<const char*,10> Classes={"All classes","scout","sniper","soldier","demoman","medic","heavy","pyro","spy","engineer"};
    struct Paint {int id=0;std::string name,token;};
    // Zero retains the real inventory finish; paint index zero is a valid kit.
    inline int PaintIndex(int finish){return finish>0&&finish<=65535?finish-1:-1;}
    inline int WearLevel(float wear){return wear<=.2f?1:wear<=.4f?2:wear<=.6f?3:wear<=.8f?4:5;}
    inline const char* WearName(float wear)
    {constexpr const char* names[]={"Factory New","Minimal Wear","Field-Tested","Well-Worn","Battle Scarred"};return names[WearLevel(wear)-1];}
    class Catalog
    {
    public:
        std::map<int,Item> items;
        std::map<int,Paint> paints;
        bool Load(std::string_view data)
        {
            auto parsed=Parser(data).Parse();if(!parsed)return false;
            const Node* game=parsed->Find("items_game");if(!game)game=&*parsed;
            auto definitions=game->Find("items");if(!definitions)return false;
            std::map<int,Item> next;
            std::map<int,Paint> nextPaints;
            for(const auto& definition:definitions->children)
            {
                int id=Number(definition.name,-1);if(id<0||id>65535)continue;
                std::set<std::string> visiting;auto item=Resolve(definition,game->Find("prefabs"),visiting);
                if(auto attrs=item.Find("static_attrs"))
                {
                    int paint=Number(attrs->Text("paintkit_proto_def_index"),-1);
                    if(paint>=0&&paint<65535)
                        nextPaints.try_emplace(paint,Paint{paint,"War paint #"+std::to_string(paint),"#9_"+std::to_string(paint)+"_field { field_number: 2 }"});
                }
                auto type=item.Text("item_class");if(!type.starts_with("tf_weapon_")&&type!="saxxy")continue;
                Item entry;entry.id=id;entry.name=item.Text("name");entry.token=item.Text("item_name",entry.name);
                entry.model=item.Text("model_player");
                entry.slot=item.Text("item_slot");
                entry.weaponType=type;entry.animSlot=item.Text("anim_slot",entry.slot);
                std::transform(entry.animSlot.begin(),entry.animSlot.end(),entry.animSlot.begin(),[](unsigned char c){return char(std::toupper(c));});
                entry.killIcon=item.Text("item_iconname",type=="saxxy"?"saxxy":type.substr(10));
                if(!SafeEffectName(entry.killIcon))entry.killIcon.clear();
                entry.viewModel=item.Text("model_viewmodel");
                entry.worldModel=item.Text("model_world");
                entry.particleSuffix=item.Text("particle_suffix");
                if(!SafeEffectName(entry.particleSuffix))entry.particleSuffix.clear();
                // Animation-slot changes are cosmetic too. Resolve activities by
                // name against the current class model; unavailable sequences
                // retain their native animation, never a foreign class skeleton.
                if(item.Text("anim_slot")=="MELEE_ALLCLASS")
                    for(auto suffix:{"DRAW","HOLSTER","IDLE","PULLBACK","PRIMARYATTACK","SECONDARYATTACK","RELOAD","DRYFIRE","IDLE_TO_LOWERED","IDLE_LOWERED","LOWERED_TO_IDLE","HITCENTER","SWINGHARD"})
                        entry.animations[std::string("ACT_VM_")+suffix]=std::string("ACT_MELEE_ALLCLASS_VM_")+suffix;
                if(auto visuals=item.Find("visuals"))if(auto replacements=visuals->Find("animation_replacement"))
                    for(const auto& replacement:replacements->children)
                        if(replacement.name.starts_with("ACT_")&&replacement.value.starts_with("ACT_")&&replacement.name.size()<128&&replacement.value.size()<128)
                            entry.animations[replacement.name]=replacement.value;
                // This animation family is selected by the native weapon class,
                // not schema animation_replacement. Verified against installed
                // Soldier hand activities; keep the cosmetic reload stages.
                if(type=="tf_weapon_particle_cannon")
                {
                    entry.animations["ACT_RELOAD_START"]="ACT_PRIMARY_RELOAD_START_3";
                    entry.animations["ACT_VM_RELOAD"]="ACT_PRIMARY_VM_RELOAD_3";
                    entry.animations["ACT_RELOAD_FINISH"]="ACT_PRIMARY_RELOAD_FINISH_3";
                }
                entry.teamAnimations.fill(entry.animations);
                for(int team=1;team<=2;++team)if(auto visuals=item.Find(team==1?"visuals_red":"visuals_blu"))
                    if(auto replacements=visuals->Find("animation_replacement"))for(const auto& replacement:replacements->children)
                        if(replacement.name.starts_with("ACT_")&&replacement.value.starts_with("ACT_")&&replacement.name.size()<128&&replacement.value.size()<128)
                            entry.teamAnimations[team][replacement.name]=replacement.value;
                if(auto perClass=item.Find("model_player_per_class"))for(int c=1;c<=9;++c)
                    if(auto path=perClass->Find(Classes[c]);path && !path->value.empty())entry.classModels[c]=path->value;
                if(entry.model.empty() && entry.classModels.empty())continue;
                if(auto used=item.Find("used_by_classes"))for(int c=1;c<=9;++c)if(Number(used->Text(Classes[c]))>0)entry.classes|=1<<c;
                if(!entry.classes)continue;
                // Cosmetic-only Spy exception. Never change the game's class
                // eligibility, weapon definition or attribute list.
                if(entry.slot=="melee"&&entry.MultiClass()&&id!=357)entry.classes|=1<<8;
                if(type=="tf_weapon_pistol_scout")type="tf_weapon_pistol";
                if(type=="tf_weapon_shotgun_soldier" || type=="tf_weapon_shotgun_hwg" || type=="tf_weapon_shotgun_pyro")type="tf_weapon_shotgun_primary";
                entry.family=type+"|"+item.Text("item_slot");
                std::map<std::string,std::string> attributes;
                if(auto attrs=item.Find("attributes"))for(const auto& attribute:attrs->children)
                {
                    const auto value=attribute.Text("value",attribute.value);
                    if(attribute.name=="sapper voice pak")entry.sapperVoice=Number(value.substr(0,value.find('.')));
                    if(attribute.name=="sapper voice pak idle wait")entry.sapperVoiceWait=float(Number(value.substr(0,value.find('.'))));
                    entry.deathEffects|=AuthoredDeathEffect(attribute.name,value);
                    if(!CosmeticAttribute(attribute.name))attributes[attribute.name]=value;
                }
                if(auto attrs=item.Find("static_attrs"))for(const auto& attribute:attrs->children)
                    entry.deathEffects|=AuthoredDeathEffect(attribute.name,attribute.value);
                for(const auto& [key,value]:attributes)entry.family+="|"+key+"="+value;
                const auto lower=Lower(entry.name);entry.festive=lower.find("festive")!=std::string::npos;
                entry.botkiller=lower.find("botkiller")!=std::string::npos;
                if(entry.botkiller)
                {
                    entry.extraWorld=item.Text("extra_wearable");entry.extraView=item.Text("extra_wearable_vm",entry.extraWorld);
                    if(auto visual=item.Find("visuals"))if(auto skin=visual->Find("skin"))entry.extraRed=entry.extraBlue=Number(skin->value,0);
                    if(auto visual=item.Find("visuals_red"))entry.extraRed=Number(visual->Text("skin"),entry.extraRed);
                    if(auto visual=item.Find("visuals_blu"))entry.extraBlue=Number(visual->Text("skin"),entry.extraBlue);
                }
                entry.decorated=item.Text("item_quality")=="paintkitweapon";
                if(auto tags=item.Find("tags")){entry.festivized=Number(tags->Text("can_be_festivized"))!=0;entry.paintable=Number(tags->Text("can_apply_paintkit"))!=0;}
                auto baseVisuals=item.Find("visuals");
                if(baseVisuals)entry.materialOverride=baseVisuals->Text("material_override");
                entry.golden=id==169||id==1071;
                if(id==264||id==1071){entry.classes=0x3fe;entry.goldRed=2;entry.goldBlue=3;}
                if(id==1071){entry.defaultRed=2;entry.defaultBlue=3;}
                entry.attachments[0]=ReadAttachments(baseVisuals,"attached_models");
                entry.festiveAttachments[0]=ReadAttachments(baseVisuals,"attached_models_festive");
                for(int team=1;team<=2;++team)
                {
                    Node visuals=baseVisuals?*baseVisuals:Node{};
                    if(auto overlay=item.Find(team==1?"visuals_red":"visuals_blu"))Merge(visuals,*overlay);
                    entry.attachments[team]=ReadAttachments(&visuals,"attached_models");
                    entry.festiveAttachments[team]=ReadAttachments(&visuals,"attached_models_festive");
                }
                if(auto visuals=item.Find("visuals"))if(auto styles=visuals->Find("styles"))for(const auto& style:styles->children)
                    if(Lower(style.Text("image_inventory")).find("_gold")!=std::string::npos)
                    {entry.goldRed=Number(style.Text("skin_red"),-1);entry.goldBlue=Number(style.Text("skin_blu"),-1);}
                entry.effects[0].Read(item.Find("visuals"));
                entry.effects[1]=entry.effects[2]=entry.effects[0];
                entry.effects[1].Read(item.Find("visuals_red"));entry.effects[2].Read(item.Find("visuals_blu"));
                next.emplace(id,std::move(entry));
            }
            // Requested appearance: Golden Wrench uses the native Australium
            // wrench mesh/skin, but keeps definition 169 and its death effects.
            if(auto gold=next.find(169);gold!=next.end())
                for(const auto& [id,appearance]:next)if(id!=169&&appearance.Gold()&&appearance.family==gold->second.family&&!appearance.botkiller&&!appearance.festive)
                {
                    auto& item=gold->second;item.model=appearance.model;item.viewModel=appearance.viewModel;item.classModels=appearance.classModels;
                    item.goldRed=item.defaultRed=appearance.goldRed;item.goldBlue=item.defaultBlue=appearance.goldBlue;
                    item.materialOverride.clear();break;
                }
            // Untradable stock definitions sometimes lack tool tags, while the
            // identical upgradeable model carries them. Share only exact-model,
            // same-stat-family attachments, never guess from weapon type alone.
            for(auto& [id,item]:next)if(!item.festive&&!item.botkiller&&!item.CanFestivize())
                for(const auto& [otherID,other]:next)if(other.CanFestivize()&&other.family==item.family&&other.model==item.model
                    &&other.viewModel==item.viewModel&&other.classModels==item.classModels&&(other.classes&item.classes)==item.classes)
                {item.festivized=true;item.festiveAttachments=other.festiveAttachments;break;}
            if(next.empty())return false;items=std::move(next);paints=std::move(nextPaints);return true;
        }
        const Item* Find(int id) const {auto it=items.find(id);return it==items.end()?nullptr:&it->second;}
        int Canonical(int id) const {auto source=Find(id);if(!source)return id;for(const auto& [n,item]:items)if(item.family==source->family)return n;return id;}
        bool CanReskin(const Item& base,const Item& chosen,int cls)const
        {return !base.Watch()&&!chosen.Watch()&&base.Supports(cls)&&chosen.Supports(cls)&&(chosen.family==base.family||(!base.slot.empty()&&chosen.slot==base.slot));}
        std::vector<int> CosmeticPaintWeapons(const Item& chosen,int cls)const
        {
            auto out=PaintWeapons(chosen,cls);
            if(std::find(out.begin(),out.end(),chosen.id)==out.end())out.insert(out.begin(),chosen.id);
            // Prefer the authored layout. Unsupported models can use a native
            // atlas as a client-only fallback, without claiming native support.
            for(int id:{18,205,14,201,13,200,7,197,21,208,19,206,15,202,9,199})
                if(Find(id)&&std::find(out.begin(),out.end(),id)==out.end())out.push_back(id);
            for(const auto& [id,item]:items)if(item.paintable&&std::find(out.begin(),out.end(),id)==out.end())out.push_back(id);
            return out;
        }
        std::vector<int> PaintWeapons(const Item& chosen,int cls)const
        {
            std::vector<int> out;
            // Only models with matching UVs/stats can borrow a paint recipe.
            // Reskins with unique meshes (AWPer/CAPPER/etc.) cannot use stock UVs.
            for(const auto& [id,item]:items)if(item.paintable&&item.Supports(cls)&&item.family==chosen.family
                &&item.Model(cls)==chosen.Model(cls)&&item.ViewModel(cls)==chosen.ViewModel(cls))out.push_back(id);
            if(!out.empty())for(const auto& [id,item]:items)if(!item.decorated&&item.Supports(cls)&&item.family==chosen.family
                &&item.Model(cls)==chosen.Model(cls)&&item.ViewModel(cls)==chosen.ViewModel(cls)
                &&std::find(out.begin(),out.end(),id)==out.end())out.insert(out.begin(),id);
            return out;
        }
        std::vector<const Item*> Variants(int id,int cls,bool festive=false) const
        {std::vector<const Item*> out;auto base=Find(id);if(base)for(const auto& [n,item]:items)
            if(CanReskin(*base,item,cls) && (!festive||item.festive) && !item.decorated
                &&(!festive||item.family==base->family))out.push_back(&item);return out;}
    };
    inline std::vector<Attachment> CosmeticAttachments(const Item& original,const Item& chosen,int team,bool viewmodel,bool festivized,bool originallyFestivized=false)
    {
        std::vector<Attachment> result;
        auto add=[&](Attachment a){if(!a.model.empty()&&std::find(result.begin(),result.end(),a)==result.end()&&result.size()<MaxCosmeticAttachments)result.push_back(std::move(a));};
        auto extra=chosen.ExtraModel(viewmodel);
        if(!extra.empty()&&(extra!=original.ExtraModel(viewmodel)||chosen.ExtraSkin(team)!=original.ExtraSkin(team)))add({extra,chosen.ExtraSkin(team)});
        for(const auto& a:chosen.Attachments(team))
            if(std::find(original.Attachments(team).begin(),original.Attachments(team).end(),a)==original.Attachments(team).end())add(a);
        if(festivized&&chosen.CanFestivize())for(const auto& a:chosen.FestivizerAttachments(team))
            if(!originallyFestivized||std::find(original.FestivizerAttachments(team).begin(),original.FestivizerAttachments(team).end(),a)==original.FestivizerAttachments(team).end())add(a);
        return result;
    }
    struct Selection
    {
        bool enabled=false,australium=false,festive=false,festivized=false,preview=false;
        int reskin=0,finish=0,seed=0,tier=0,sheen=1,effect=2002,previewCount=5;float wear=0.f;
        int unusual=0; // Weapon particles, independent of paint and professional killstreaks.
        std::array<float,3> position{},rotation{}; // Local first-person alignment; not shared with peers.
        bool Adjusted()const{return position!=std::array<float,3>{}||rotation!=std::array<float,3>{};}
        bool ValidAlignment()const
        {for(float v:position)if(!std::isfinite(v)||std::abs(v)>50)return false;
         for(float v:rotation)if(!std::isfinite(v)||std::abs(v)>180)return false;return true;}
        bool operator==(const Selection&) const = default;
        bool Valid() const {return reskin>=0&&reskin<=65535&&finish>=0&&finish<=65535&&seed>=0&&seed<=1000000&&tier>=0&&tier<=3
            && sheen>=1&&sheen<=7&&effect>=2002&&effect<=2008&&previewCount>=0&&previewCount<=100&&std::isfinite(wear)&&wear>=0&&wear<=1
            && (unusual==0||(unusual>=701&&unusual<=704))&&ValidAlignment();}
    };
    inline std::string WeaponUnusualName(int effect,std::string_view suffix)
    {
        if(!SafeEffectName(suffix)||suffix=="paintkit")return {};
        const char* base=nullptr;
        switch(effect){case 701:base="weapon_unusual_hot";break;case 702:base="weapon_unusual_isotope";break;
            case 703:base="weapon_unusual_cool";break;case 704:base="weapon_unusual_energyorb";break;default:return {};}
        return std::string(base)+"_"+std::string(suffix);
    }
    using Key=std::pair<int,int>; // canonical weapon, class (zero = shared)
    inline bool NormalizeReskinChange(Selection& selection,bool selectedReskinHasGold)
    {
        bool adjusted=selection.festive;selection.festive=false;
        if(selection.reskin && selection.australium && !selectedReskinHasGold){selection.australium=false;adjusted=true;}
        return adjusted;
    }
    using Preset=std::map<Key,Selection>;
    inline Selection Get(const Preset& preset,int item,int cls)
    {auto it=preset.find({item,cls});if(it==preset.end())it=preset.find({item,0});return it==preset.end()?Selection{}:it->second;}
    inline bool SafeName(std::string_view name)
    {if(name.empty()||name.size()>64||name=="."||name==".."||name.back()=='.'||name.back()==' ')return false;
        for(unsigned char c:name)if(!std::isalnum(c)&&c!='_'&&c!='-'&&c!=' ')return false;
        auto stem=Lower(std::string(name));if(stem=="con"||stem=="prn"||stem=="aux"||stem=="nul")return false;
        for(int n=1;n<=9;++n)if(stem=="com"+std::to_string(n)||stem=="lpt"+std::to_string(n))return false;return true;}
}
