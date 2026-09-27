#pragma once
#include "../ImGui/Workspace.h"
#include <boost/property_tree/ptree.hpp>
#include <cmath>

namespace ConfigColours
{
    inline void Save(boost::property_tree::ptree& root)
    {
        root.put("MenuColours.BorderOverride",Workspace::BorderColourOverride);
        root.put("MenuColours.TextActiveOverride",Workspace::TextColourOverride);
        root.put("MenuColours.TextTitleOverride",Workspace::TitleTextOverride);
        root.put("MenuColours.TextInactiveOverride",Workspace::InactiveTextOverride);
        for(int i=0;i<3;i++)
        {
            root.put("MenuColours.Accent"+std::to_string(i),Workspace::Accent[i]);
            root.put("MenuColours.Border"+std::to_string(i),Workspace::BorderColour[i]);
            root.put("MenuColours.TextActive"+std::to_string(i),Workspace::TextColour[i]);
            root.put("MenuColours.TextTitle"+std::to_string(i),Workspace::TitleTextColour[i]);
            root.put("MenuColours.TextInactive"+std::to_string(i),Workspace::InactiveTextColour[i]);
            root.put("MenuColours.Background"+std::to_string(i),Workspace::Background[i]);
        }
    }
    inline void Load(const boost::property_tree::ptree& root)
    {
        Workspace::BorderColourOverride=root.get<bool>("MenuColours.BorderOverride",false);
        Workspace::TextColourOverride=root.get<bool>("MenuColours.TextActiveOverride",false);
        Workspace::TitleTextOverride=root.get<bool>("MenuColours.TextTitleOverride",false);
        Workspace::InactiveTextOverride=root.get<bool>("MenuColours.TextInactiveOverride",false);
        const auto read=[&](const std::string& key,float fallback)
        {
            const auto value=root.get_optional<float>(key);
            return value && std::isfinite(*value)?std::clamp(*value,0.f,1.f):fallback;
        };
        for(int i=0;i<3;i++)
        {
            Workspace::BorderColour[i]=read("MenuColours.Border"+std::to_string(i),Workspace::InterfaceAccent[i]);
            Workspace::TextColour[i]=read("MenuColours.TextActive"+std::to_string(i),Workspace::Accent[i]);
            Workspace::TitleTextColour[i]=read("MenuColours.TextTitle"+std::to_string(i),Workspace::Accent[i]);
            Workspace::InactiveTextColour[i]=read("MenuColours.TextInactive"+std::to_string(i),Workspace::Accent[i]*.65f);
            if (!Workspace::InterfaceColoursOverride)
            {
                Workspace::Accent[i]=read("MenuColours.Accent"+std::to_string(i),Workspace::InterfaceAccent[i]);
                Workspace::Background[i]=read("MenuColours.Background"+std::to_string(i),Workspace::InterfaceBackground[i]);
            }
        }
    }
}
