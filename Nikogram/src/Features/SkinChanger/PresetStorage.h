#pragma once
#include "Model.h"
#include <boost/property_tree/ptree.hpp>
#include <stdexcept>

namespace SkinPresetStorage
{
    using namespace SkinModel;
    inline boost::property_tree::ptree Encode(const Preset& data)
    {
        boost::property_tree::ptree root,entries;root.put("format","nikogram-skin-presets");root.put("version",1);
        for(const auto& [key,s]:data)
        {
            boost::property_tree::ptree row;row.put("weapon",key.first);row.put("class",key.second);
            row.put("enabled",s.enabled);row.put("reskin",s.reskin);row.put("australium",s.australium);row.put("festive",s.festive);row.put("festivized",s.festivized);
            row.put("finish",s.finish);row.put("wear",s.wear);row.put("seed",s.seed);row.put("tier",s.tier);row.put("sheen",s.sheen);row.put("effect",s.effect);row.put("preview",s.preview);row.put("preview_count",s.previewCount);row.put("unusual",s.unusual);
            for(int axis=0;axis<3;++axis){row.put("position_"+std::to_string(axis),s.position[axis]);row.put("rotation_"+std::to_string(axis),s.rotation[axis]);}
            entries.push_back({"",row});
        }
        root.add_child("weapons",entries);return root;
    }
    struct Skipped {int weapon,cls,reskin;};
    struct Result {Preset selections;std::vector<Skipped> incompatible;};
    inline Result Decode(const boost::property_tree::ptree& root,const Catalog& catalog)
    {
        if(root.get<std::string>("format","")!="nikogram-skin-presets"||root.get<int>("version",0)!=1)throw std::runtime_error("unsupported format/version");
        auto entries=root.get_child_optional("weapons");if(!entries)throw std::runtime_error("missing weapon entries");
        if(entries->size()>512)throw std::runtime_error("too many entries");
        Result result;std::set<Key> seen;
        for(const auto& [unused,row]:*entries)
        {
            int weapon=row.get<int>("weapon",-1),cls=row.get<int>("class",-1);auto item=catalog.Find(weapon);
            if(!item||cls<0||cls>9||catalog.Canonical(weapon)!=weapon)throw std::runtime_error("unknown weapon/class");
            if(!seen.emplace(weapon,cls).second)throw std::runtime_error("duplicate weapon/class entry");
            if(item->Watch())continue; // Retired watch cosmetics do not invalidate other selections.
            Selection s;s.enabled=row.get<bool>("enabled",false);s.reskin=row.get<int>("reskin",0);s.australium=row.get<bool>("australium",false);
            s.festive=row.get<bool>("festive",false);s.festivized=row.get<bool>("festivized",false);s.finish=row.get<int>("finish",0);s.wear=row.get<float>("wear",0);
            s.seed=row.get<int>("seed",0);s.tier=row.get<int>("tier",0);s.sheen=row.get<int>("sheen",1);s.effect=row.get<int>("effect",2002);s.preview=row.get<bool>("preview",false);s.previewCount=row.get<int>("preview_count",5);
            s.unusual=row.get<int>("unusual",0);
            for(int axis=0;axis<3;++axis){s.position[axis]=row.get<float>("position_"+std::to_string(axis),0);s.rotation[axis]=row.get<float>("rotation_"+std::to_string(axis),0);}
            if(!s.Valid())throw std::runtime_error("invalid cosmetic values");
            if(s.reskin)
            {
                auto variant=catalog.Find(s.reskin);bool valid=false;
                if(variant)for(int c=1;c<=9;++c)if((!cls||cls==c)&&catalog.CanReskin(*item,*variant,c))valid=true;
                if(!valid){result.incompatible.push_back({weapon,cls,s.reskin});continue;}
            }
            result.selections.emplace(Key{weapon,cls},s);
        }
        return result;
    }
}
