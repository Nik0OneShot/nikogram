#pragma once
#include "../../SDK/Vars.h"
#include <memory>
#include <functional>

// Two independently serialized banks; only evaluated Values are routed to the
// existing algorithms. Never replace the legacy Map/Default settings at runtime.
namespace AimModes
{
    enum Mode { None=-1, Rage=0, Legit=1 };
    inline thread_local int Editing=None;
    inline int Active=None;
    inline bool Banked=false;
    // Auxiliary controls retain their saved Rage bank even while aim is off.
    // Timing retains its existing active-mode profile, falling back to Rage.
    inline bool RageAuxiliary(const BaseVar* source)
    {
        return source==&Vars::Aimbot::General::NoSpread
            || std::string_view(source->Name()).starts_with("Vars::CritHack::")
            || source==&Vars::Aimbot::Projectile::AutoDetonate
            || source==&Vars::Aimbot::Projectile::AutodetRadius
            || source==&Vars::Aimbot::Projectile::AutoAirblast;
    }
    inline int RuntimeBank(const BaseVar* source)
    {
        if(RageAuxiliary(source))return Rage;
        if(Active==None&&std::string_view(source->Name()).starts_with("Vars::Doubletap::"))return Rage;
        return Active;
    }
    inline bool DetonateAllowed(){return !Banked||Active!=Legit;}
    struct Entry
    {
        BaseVar* original;
        std::shared_ptr<BaseVar> mode[2];
        std::function<void(int)> apply;
        std::function<void()> seed;
        std::function<void(bool)> auxiliary;
    };
    inline std::vector<Entry>& Entries(){static std::vector<Entry> values;return values;}
    inline std::unordered_map<BaseVar*,size_t>& Index(){static std::unordered_map<BaseVar*,size_t> values;return values;}
    template<class T> void Add(ConfigVar<T>& original)
    {
        Entry e;e.original=&original;ConfigVar<T>* banks[2];
        for(int i=0;i<2;++i)
        {
            const std::string name=std::string("Vars::AimModes::")+(i==Rage?"Rage::":"Legit::")+original.Name()+"_";
            auto bank=std::make_shared<ConfigVar<T>>(original.Default,original.m_vNames,name.c_str(),original.Section(),original.m_iFlags,original.m_vValues,original.m_sExtra);
            bank->m_unMin=original.m_unMin;bank->m_unMax=original.m_unMax;bank->m_unStep=original.m_unStep;
            banks[i]=bank.get();e.mode[i]=std::move(bank);
        }
        e.apply=[&original,a=banks[0],b=banks[1]](int mode){original.Value=(mode==Rage?a:b)->Value;};
        e.seed=[&original,a=banks[0],b=banks[1]]{a->Map={{DEFAULT_BIND,original.Map.at(DEFAULT_BIND)}};b->Map=a->Map;};
        e.auxiliary=[&original,a=banks[0],previous=original.Default](bool enter) mutable
        {
            if(enter){previous=original.Value;original.Value=a->Value;}
            else original.Value=previous;
        };
        Index()[&original]=Entries().size();Entries().push_back(std::move(e));
    }
    inline void Ensure()
    {
        static const bool initialized=[]{
        const auto originals=G::Vars; // registration appends to G::Vars
        for(auto* original:originals)
        {
            const std::string_view name=original->Name();
            if(original->m_iFlags&(NOSAVE|DEBUGVAR))continue;
            if(!(name.starts_with("Vars::Aimbot::")||name.starts_with("Vars::Backtrack::")||name.starts_with("Vars::CritHack::")||name.starts_with("Vars::Doubletap::")||original==&Vars::Colors::FOVCircle))continue;
            if(auto* v=original->As<bool>())Add(*v);
            else if(auto* v=original->As<int>())Add(*v);
            else if(auto* v=original->As<float>())Add(*v);
            else if(auto* v=original->As<Color_t>())Add(*v);
            else if(auto* v=original->As<IntRange_t>())Add(*v);
            else if(auto* v=original->As<FloatRange_t>())Add(*v);
        }
        const auto defaults=[]<class T>(ConfigVar<T>& source,int mode,T value){auto* bank=Entries()[Index().at(&source)].mode[mode]->As<T>();bank->Default=bank->Value=bank->Map[DEFAULT_BIND]=value;};
        defaults(Vars::Aimbot::General::AimType,Rage,3);defaults(Vars::Aimbot::General::AimType,Legit,5);
        defaults(Vars::Aimbot::General::AimFOV,Rage,20.f);defaults(Vars::Aimbot::General::AimFOV,Legit,12.f);
        defaults(Vars::Backtrack::Window,Legit,0);
        defaults(Vars::Aimbot::General::SmoothFormula,Legit,1);
        defaults(Vars::Aimbot::General::CombinedGuidance,Legit,true);
        defaults(Vars::Aimbot::General::AimDestination,Legit,1);
        return true;}();(void)initialized;
    }
    template<class T> ConfigVar<T>& Get(ConfigVar<T>& original,int mode)
    {
        Ensure();auto it=Index().find(&original);
        return mode>=Rage&&mode<=Legit&&it!=Index().end()?*Entries()[it->second].mode[mode]->As<T>():original;
    }
    template<class T> ConfigVar<T>& Resolve(ConfigVar<T>& original)
    {
        // Advanced/search editors must edit the same auxiliary bank used at runtime.
        return Get(original,Editing!=None&&RageAuxiliary(&original)?Rage:Editing);
    }
    struct EditScope
    {
        int previous=Editing;EditScope(int mode){Ensure();Editing=mode;}~EditScope(){Editing=previous;}
    };
    inline bool AuxiliaryDependency(const BaseVar* source)
    {
        return std::string_view(source->Name()).starts_with("Vars::Aimbot::")
            &&!RageAuxiliary(source)&&source!=&Vars::Aimbot::General::AimType
            &&source!=&Vars::Aimbot::General::AutoShoot;
    }
    // Helpers share the projectile targeting/safety solver, but must not inherit
    // an unrelated legacy/Legit FOV or self-damage threshold. Scope evaluated
    // values only; leave activation, AutoShoot and serialized maps untouched.
    // Nested helper scopes reuse the outer snapshot without overwriting it.
    inline thread_local int AuxiliaryDepth=0;
    struct AuxiliaryScope
    {
        bool entered=false;
        explicit AuxiliaryScope(bool enabled)
        {
            if(!enabled||!Banked||Active==Rage)return;
            entered=true;if(AuxiliaryDepth++!=0)return;
            for(auto& e:Entries())if(AuxiliaryDependency(e.original))e.auxiliary(true);
            Vars::Aimbot::Projectile::AimFOV.Value=std::min(Vars::Aimbot::Projectile::AimFOV.Value,Get(Vars::Aimbot::General::AimFOV,Rage).Value);
        }
        ~AuxiliaryScope()
        {
            if(!entered||--AuxiliaryDepth!=0)return;
            for(auto& e:Entries())if(AuxiliaryDependency(e.original))e.auxiliary(false);
        }
        AuxiliaryScope(const AuxiliaryScope&)=delete;
        AuxiliaryScope& operator=(const AuxiliaryScope&)=delete;
    };
    inline int NormalizeStyle(int mode,int style)
    {
        if(mode==Legit)return style==2||style==5?style:5;
        return style==1||style==3||style==4?style:3;
    }
    inline void SeedLegacy()
    {
        Ensure();for(auto& e:Entries())e.seed();
        Get(Vars::Aimbot::General::AimType,Rage).Map[DEFAULT_BIND]=3;
        Get(Vars::Aimbot::General::AimType,Legit).Map[DEFAULT_BIND]=5;
        Get(Vars::Aimbot::General::AimFOV,Rage).Map[DEFAULT_BIND]=20.f;
        Get(Vars::Aimbot::General::AimFOV,Legit).Map[DEFAULT_BIND]=12.f;
        Get(Vars::Backtrack::Window,Legit).Map[DEFAULT_BIND]=0;
        Get(Vars::Aimbot::General::SmoothFormula,Legit).Map[DEFAULT_BIND]=1;
        Get(Vars::Aimbot::General::CombinedGuidance,Legit).Map[DEFAULT_BIND]=true;
        Get(Vars::Aimbot::General::AimDestination,Legit).Map[DEFAULT_BIND]=1;
    }
    inline int Choose(bool rage,bool legit){return rage?Rage:legit?Legit:None;}
    inline void Apply(bool nikogram)
    {
        Ensure();Active=None;Banked=nikogram;if(!nikogram)return;
        Active=Choose(Vars::AimModes::RageEnabled.Value&&Vars::AimModes::RageActivation.Value,
            Vars::AimModes::LegitEnabled.Value&&Vars::AimModes::LegitActivation.Value);
        if(Active!=None)
        {
            for(auto& e:Entries())e.apply(Active);
            Vars::Aimbot::General::AimType.Value=NormalizeStyle(Active,Vars::Aimbot::General::AimType.Value);
            // Combined Legit guidance is one controller, routed through the
            // existing visible Smooth path. The saved solo style is untouched.
            if(Active==Legit&&Vars::Aimbot::General::CombinedGuidance.Value)
                Vars::Aimbot::General::AimType.Value=Vars::Aimbot::General::AimTypeEnum::Smooth;
            // General FOV is the selected mode's master. Projectile/melee keep
            // explicit per-weapon limits, capped by the master field of view.
            Vars::Aimbot::Projectile::AimFOV.Value=std::min(Vars::Aimbot::Projectile::AimFOV.Value,Vars::Aimbot::General::AimFOV.Value);
            Vars::Aimbot::Melee::AimFOV.Value=std::min(Vars::Aimbot::Melee::AimFOV.Value,Vars::Aimbot::General::AimFOV.Value);
        }
        else Vars::Aimbot::General::AimType.Value=0;
        for(auto& e:Entries())
        {
            const int bank=RuntimeBank(e.original);
            if(bank!=None&&bank!=Active)e.apply(bank);
        }
        // Never let a stale/legacy Legit detonation value escape through another caller.
        if(!DetonateAllowed())Vars::Aimbot::Projectile::AutoDetonate.Value=0;
        Vars::Aimbot::General::FOVCircle.Value=Vars::AimModes::CircleEnabled.Value
            &&(Get(Vars::Aimbot::General::FOVCircle,Rage).Value||Get(Vars::Aimbot::General::FOVCircle,Legit).Value);
        if(Vars::AimModes::CircleOverride.Value)Vars::Colors::FOVCircle.Value=Vars::AimModes::CircleColour.Value;
        Vars::ESP::MoonlitEnabled.Value=Vars::ESP::MoonlitEnabled.Value&&Vars::ESP::MoonlitMaster.Value;
    }
}
