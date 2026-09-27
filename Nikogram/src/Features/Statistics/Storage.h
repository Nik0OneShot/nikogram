#pragma once
#include "Model.h"
#include <boost/property_tree/json_parser.hpp>
#include <filesystem>
#include <stdexcept>
#include <Windows.h>

namespace Statistics::Storage
{
    using Tree=boost::property_tree::ptree;
    inline Tree Encode(const Counts& counts)
    {Tree t;t.put("observed",counts.observed);for(int i=0;i<MetricCount;++i)t.put(Keys[i],counts.n[i]);return t;}
    inline uint64_t Unsigned(const std::string& value)
    {
        if(value.empty() || value.size()>18 || value.find_first_not_of("0123456789")!=std::string::npos)
            throw std::runtime_error("Invalid unsigned statistic");
        return std::stoull(value);
    }
    inline Counts Decode(const Tree& t)
    {
        Counts counts;auto observed=Unsigned(t.get<std::string>("observed","0"));
        if(observed>=(1u<<MetricCount))throw std::runtime_error("Unknown metrics");
        counts.observed=uint32_t(observed);
        for(int i=0;i<MetricCount;++i)
        {
            auto value=t.get_optional<std::string>(Keys[i]);
            if(value)counts.n[i]=Unsigned(*value);else counts.observed&=~(1u<<i);
        }
        return counts;
    }
    inline Tree EncodeView(const View& view,uint32_t owner)
    {
        Tree root,players,breakdowns;root.put("version",1);root.put("account",owner);
        root.add_child("overview",Encode(view.total));
        for(const auto& [id,r]:view.players)
        {auto t=Encode(r.counts);t.put("name",r.name);players.push_back({std::to_string(id),t});}
        for(const auto& [key,r]:view.breakdowns)
        {auto t=Encode(r.counts);t.put("name",r.name);breakdowns.push_back({key,t});}
        root.add_child("players",players);root.add_child("breakdowns",breakdowns);return root;
    }
    inline View DecodeView(const Tree& root,uint32_t owner)
    {
        if(root.get<int>("version")!=1 || root.get<uint32_t>("account")!=owner)
            throw std::runtime_error("Unknown statistics format/account");
        View loaded;loaded.total=Decode(root.get_child("overview"));
        for(const auto& [key,t]:root.get_child("players"))
        {
            if(loaded.players.size()>=100000)throw std::runtime_error("Too many records");
            const auto account=Unsigned(key);
            if(!account || account>UINT32_MAX || loaded.players.contains(uint32_t(account)))throw std::runtime_error("Invalid account");
            loaded.players[uint32_t(account)]={t.get<std::string>("name","Unknown"),Decode(t)};
        }
        for(const auto& [key,t]:root.get_child("breakdowns"))
        {
            if(loaded.breakdowns.size()>=100000 || loaded.breakdowns.contains(key))throw std::runtime_error("Invalid breakdowns");
            loaded.breakdowns[key]={t.get<std::string>("name",key),Decode(t)};
        }
        return loaded;
    }
    inline View Load(const std::filesystem::path& file,uint32_t owner)
    {Tree root;boost::property_tree::read_json(file.string(),root);return DecodeView(root,owner);}
    inline void Save(const std::filesystem::path& file,const View& view,uint32_t owner)
    {
        auto root=EncodeView(view,owner);
        std::filesystem::create_directories(file.parent_path());
        auto temporary=file;temporary+=".tmp";
        boost::property_tree::write_json(temporary.string(),root);
        if(std::filesystem::exists(file))std::filesystem::copy_file(file,file.string()+".bak",std::filesystem::copy_options::overwrite_existing);
        if(!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace statistics file");
    }
}
