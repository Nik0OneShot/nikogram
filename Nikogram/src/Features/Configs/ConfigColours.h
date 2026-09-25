#pragma once
#include "../ImGui/Workspace.h"
#include <boost/property_tree/ptree.hpp>
#include <cmath>

namespace ConfigColours
{
    inline void Save(boost::property_tree::ptree& root)
    {
        for(int i=0;i<3;i++)
        {
            root.put("MenuColours.Accent"+std::to_string(i),Workspace::Accent[i]);
            root.put("MenuColours.Background"+std::to_string(i),Workspace::Background[i]);
        }
    }
    inline void Load(const boost::property_tree::ptree& root)
    {
        if(Workspace::InterfaceColoursOverride)return;
        const auto read=[&](const std::string& key,float fallback)
        {
            const auto value=root.get_optional<float>(key);
            return value && std::isfinite(*value)?std::clamp(*value,0.f,1.f):fallback;
        };
        for(int i=0;i<3;i++)
        {
            Workspace::Accent[i]=read("MenuColours.Accent"+std::to_string(i),Workspace::InterfaceAccent[i]);
            Workspace::Background[i]=read("MenuColours.Background"+std::to_string(i),Workspace::InterfaceBackground[i]);
        }
    }
}
