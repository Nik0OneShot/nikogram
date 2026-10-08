#pragma once
#include <algorithm>
#include <array>
#include <string>
#include "../../Binds/ConditionPolicy.h"

// Quick binds retain the ordinary single-setting bind/config representation.
// The default key helpers do not adopt conditions; the two opt-in activation
// controls below also recognize their specific independent conditions.
// Shared, nested, inverted and multi-value legacy binds are never overwritten.
namespace MoonlitBinding
{
    struct Match { int index = -1; bool advanced = false; };
    enum Activation { Hold=0, Toggle=1, Dangersense=2, OnLethal=3 };
    // These two feature controls may adopt ONE independent condition as well
    // as an ordinary key bind. Shared/nested/multiple overrides stay protected.
    template<class Var,class Binds>
    Match InspectActivation(const Var& var,const Binds& binds,Activation special)
    {
        Match result;int count=0;
        for(const auto& [id,value]:var.Map)
        {
            if(id==-1)continue;++count;
            if(id<0||id>=int(binds.size())){result.advanced=true;continue;}
            const auto& b=binds[id];
            const bool children=std::any_of(binds.begin(),binds.end(),[id](const auto& child){return child.m_iParent==id;});
            const bool key=b.m_iType==0&&(b.m_iInfo==Hold||b.m_iInfo==Toggle);
            const bool condition=special==Dangersense?(b.m_iType==5&&b.m_tConditions.enemyClass==8):b.m_iType==6;
            const bool simple=(key||condition)&&b.m_iParent==-1&&!b.m_bNot&&!children
                &&b.m_vVars.size()==1&&b.m_vVars.front()==&var&&value==true
                &&var.Map.contains(-1)&&var.Map.at(-1)==false;
            if(simple)result.index=id;else result.advanced=true;
        }
        for(const auto& b:binds)
            if(std::find(b.m_vVars.begin(),b.m_vVars.end(),&var)!=b.m_vVars.end())
            {
                const int id=int(&b-binds.data());
                if(!var.Map.contains(id))result.advanced=true;
            }
        if(count>1)result.advanced=true;
        if(result.advanced)result.index=-1;
        return result;
    }
    template<class Bind> int ActivationMode(const Bind& b,Activation special)
    {
        if(b.m_iType==0)return b.m_iInfo;
        if(special==Dangersense&&b.m_iType==5&&b.m_tConditions.enemyClass==8&&!b.m_tConditions.behindVisible)return Dangersense;
        if(special==OnLethal&&b.m_iType==6&&b.m_tConditions.sniperAnyAim)return OnLethal;
        return -1; // Existing custom condition can be converted explicitly.
    }
    template<class Var,class Binds>
    bool AssignActivation(Var& var,Binds& binds,Activation special,int mode,int key,const std::string& label)
    {
        const auto match=InspectActivation(var,binds,special);
        if(match.advanced||(mode!=Hold&&mode!=Toggle&&mode!=special)||key<0||key>254)return false;
        int id=match.index;
        if(id<0)
        {
            typename Binds::value_type fresh;
            fresh.m_sName=label;fresh.m_iInfo=Toggle;fresh.m_vVars.push_back(&var);
            id=int(binds.size());binds.push_back(fresh);
        }
        auto& b=binds[id];
        if(mode==Hold||mode==Toggle){b.m_iType=0;b.m_iInfo=mode;if(key)b.m_iKey=key;}
        else if(mode==Dangersense){b.m_iType=5;b.m_tConditions.enemyClass=8;b.m_tConditions.behindVisible=false;}
        else{b.m_iType=6;b.m_tConditions.sniperAnyAim=true;}
        b.m_tConditions=ConditionPolicy::Normalize(b.m_tConditions);
        b.m_bNot=false;b.m_bActive=false;b.m_tKeyStorage={};
        var.Map[-1]=false;var.Map[id]=true;
        return true;
    }
    template<class Var, class Binds>
    Match InspectValue(const Var& var, const Binds& binds, const auto& on, const auto& off, bool notActive=false)
    {
        Match result;
        int count = 0;
        for (const auto& [id, value] : var.Map)
        {
            if (id == -1) continue;
            ++count;
            if (id < 0 || id >= int(binds.size())) { result.advanced = true; continue; }
            const auto& b = binds[id];
            const bool children = std::any_of(binds.begin(), binds.end(), [id](const auto& child) { return child.m_iParent == id; });
            const bool simple = b.m_iType == 0 && (b.m_iInfo == 0 || b.m_iInfo == 1)
                && b.m_iParent == -1 && b.m_bNot==notActive && !children && value == on
                && b.m_vVars.size() == 1 && b.m_vVars.front() == &var
                && var.Map.contains(-1) && var.Map.at(-1) == off;
            if (simple) result.index = id; else result.advanced = true;
        }
        for (int id = 0; id < int(binds.size()); ++id)
            if (std::find(binds[id].m_vVars.begin(), binds[id].m_vVars.end(), &var) != binds[id].m_vVars.end() && !var.Map.contains(id))
                result.advanced = true;
        if (count > 1) result.advanced = true;
        if (result.advanced) result.index = -1;
        return result;
    }
    template<class Var,class Binds>
    Match Inspect(const Var& var,const Binds& binds,bool defaultOn=false,bool notActive=false)
    {return InspectValue(var,binds,!defaultOn,defaultOn,notActive);}
    template<class Var, class Binds>
    bool AssignValue(Var& var, Binds& binds, int key, int mode, const std::string& label, const auto& on, const auto& off, bool notActive=false)
    {
        const auto match = InspectValue(var, binds, on, off, notActive);
        if (match.advanced || key < 1 || key > 254 || (mode != 0 && mode != 1)) return false;
        if (match.index >= 0)
        {
            auto& b = binds[match.index];
            b.m_iKey = key; b.m_iInfo = mode; b.m_bActive = notActive; b.m_tKeyStorage = {};
        }
        else
        {
            typename Binds::value_type b;
            b.m_sName = label; b.m_iKey = key; b.m_iInfo = mode; b.m_iType = 0; b.m_iParent = -1;b.m_bNot=notActive;
            b.m_bActive=notActive;
            b.m_vVars.push_back(&var);
            const int id = int(binds.size());
            binds.push_back(b);
            var.Map[-1] = off; var.Map[id] = on;
        }
        return true;
    }
    template<class Var,class Binds>
    bool Assign(Var& var,Binds& binds,int key,int mode,const std::string& label,bool defaultOn=false,bool notActive=false)
    {return AssignValue(var,binds,key,mode,label,!defaultOn,defaultOn,notActive);}
    // One-time migration of simple hold-to-show ESP bindings. Complex bindings
    // are never rewritten; keep the user's key, Hold/Toggle choice and name.
    template<class Var,class Binds> bool MigrateDefaultOn(Var& var,Binds& binds)
    {
        const auto match=Inspect(var,binds,true,true);
        if(match.index<0)return false;
        auto& bind=binds[match.index];bind.m_bNot=false;bind.m_bActive=false;bind.m_tKeyStorage={};
        return true;
    }
    // A held mouse button / modifier from the initiating click is not a new key.
    struct CaptureGate
    {
        std::array<bool, 256> blocked{};
        template<class Down> void Begin(Down down) { for (int i = 1; i < 255; ++i) blocked[i] = down(i); }
        template<class Down, class Pressed>
        int Poll(Down down, Pressed pressed, int primary, int secondary)
        {
            for (int i = 1; i < 255; ++i)
            {
                if (blocked[i]) { if (!down(i)) blocked[i] = false; continue; }
                if (i != primary && i != secondary && pressed(i)) return i;
            }
            return 0;
        }
    };
    inline int Columns(float width, float scale) { return width >= 620.f * scale ? 2 : 1; }
}
