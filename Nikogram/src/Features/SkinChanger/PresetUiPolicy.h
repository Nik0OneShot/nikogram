#pragma once
#include "Model.h"

namespace SkinPresetUi
{
    class DefaultActivation
    {
        bool attempted=false;
    public:
        bool Observe(bool enabled,bool catalogReady,bool unloading=false)
        {
            if(!enabled){attempted=false;return false;}
            if(unloading||!catalogReady||attempted)return false;
            attempted=true;return true;
        }
    };
    inline bool NameAllowed(std::string_view name)
    {return SkinModel::SafeName(name)&&SkinModel::Lower(std::string(name))!="current";}
    inline bool Default(std::string_view name)
    {return SkinModel::Lower(std::string(name))=="default";}
    inline bool Less(const std::string& a,const std::string& b)
    {
        if(Default(a)!=Default(b))return Default(a);
        const auto lowerA=SkinModel::Lower(a),lowerB=SkinModel::Lower(b);
        return lowerA==lowerB?a<b:lowerA<lowerB;
    }
    inline int Columns(float width,float scale)
    {return std::isfinite(width)&&std::isfinite(scale)&&scale>0&&width>=620.f*scale?2:1;}
    enum class Action { None, Create, Load, Save, Delete, Reset };
    struct Request {Action action=Action::None;std::string name;};
}
