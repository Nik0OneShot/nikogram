#pragma once
#include "Workspace.h"
#include <boost/property_tree/json_parser.hpp>
#include <array>
#include <vector>
#include <cmath>

namespace MenuPresets
{
    struct Preset
    {
        std::string name = "Niko - Yellow / Black";
        std::array<float, 3> accent = { .85f, .75f, .20f }, background = { 0.f, 0.f, 0.f };
        bool compact = false, top = false, snap = true;
        float scale = 1.f;
		bool pet = true, petRun = true, petJump = true, petClimb = true, petDrag = true;
		int petSize = 128, petRest = 60, petSleep = 180;
        bool petNiko=true,petAlula=true,petCalamus=true,petWorldMachine=true,petSocial=true;
        bool petScares=true,petChase=true;
        int petGameChoice=0;
        int petScarer=2;
        int petScaree=0;
        int petChaser=1;
    };
    inline std::vector<Preset> Items;
    inline bool Loaded = false, Writable = true;
    inline std::string Error;
    inline std::filesystem::path Path() { return std::filesystem::path(Workspace::PreferencesPath).parent_path() / "menu-presets.json"; }
    inline Preset Capture(const std::string& name)
    {
        Preset p; p.name = name;
        std::copy_n(Workspace::Accent, 3, p.accent.begin());
        std::copy_n(Workspace::Background, 3, p.background.begin());
        p.compact = Workspace::CompactTaskbar; p.top = Workspace::TopTaskbar;
        p.snap = Workspace::Snap; p.scale = Workspace::FontScale;
		p.pet = Workspace::PetEnabled; p.petSize = Workspace::PetSize;
		p.petRun = Workspace::PetRun; p.petJump = Workspace::PetJump; p.petClimb = Workspace::PetClimb; p.petDrag = Workspace::PetDrag;
		p.petRest = Workspace::PetRest; p.petSleep = Workspace::PetSleep;
        p.petNiko=Workspace::PetNiko;p.petAlula=Workspace::PetAlula;p.petCalamus=Workspace::PetCalamus;p.petSocial=Workspace::PetSocial;p.petWorldMachine=Workspace::PetWorldMachine;
        p.petScares=Workspace::PetScares;p.petChase=Workspace::PetChase;
        p.petGameChoice=Workspace::PetGameChoice;
        p.petScarer=Workspace::PetScarer;
        p.petScaree=Workspace::PetScaree;
        p.petChaser=Workspace::PetChaser;
        return p;
    }
    inline void Apply(const Preset& p)
    {
        std::copy(p.accent.begin(), p.accent.end(), Workspace::Accent);
        std::copy(p.background.begin(), p.background.end(), Workspace::Background);
        std::copy(p.accent.begin(),p.accent.end(),Workspace::InterfaceAccent);
        std::copy(p.background.begin(),p.background.end(),Workspace::InterfaceBackground);
        Workspace::InterfaceColoursOverride=true;
        Workspace::CompactTaskbar = p.compact; Workspace::TopTaskbar = p.top;
        Workspace::Snap = p.snap; Workspace::FontScale = p.scale;
		Workspace::PetEnabled = p.pet; Workspace::PetSize = std::clamp(p.petSize, 32, 128);
		Workspace::PetRun = p.petRun; Workspace::PetJump = p.petJump; Workspace::PetClimb = p.petClimb; Workspace::PetDrag = p.petDrag;
		Workspace::PetRest = std::clamp(p.petRest, 15, 600); Workspace::PetSleep = std::clamp(p.petSleep, Workspace::PetRest + 15, 1200);
        Workspace::PetNiko=p.petNiko;Workspace::PetAlula=p.petAlula;Workspace::PetCalamus=p.petCalamus;Workspace::PetSocial=p.petSocial;Workspace::PetWorldMachine=p.petWorldMachine;
        Workspace::PetScares=p.petScares;Workspace::PetChase=p.petChase;
        Workspace::PetGameChoice=std::clamp(p.petGameChoice,0,1);
        Workspace::PetScarer=std::clamp(p.petScarer,0,4);
        Workspace::PetScaree=std::clamp(p.petScaree,0,4);
        Workspace::PetChaser=std::clamp(p.petChaser,0,4);
    }
    inline void Load()
    {
        if (Loaded) return;
        Loaded = true;
        try
        {
            if (!std::filesystem::exists(Path())) { Items = { Preset{} }; return; }
            boost::property_tree::ptree root;
            boost::property_tree::read_json(Path().string(), root);
            std::vector<Preset> loaded;
            for (const auto& entry : root.get_child("presets"))
            {
                const auto& tree = entry.second;
                Preset p; p.name = tree.get<std::string>("name");
                if (p.name.empty() || p.name.size() > 127) throw std::runtime_error("Invalid name");
                for (int i = 0; i < 3; ++i)
                {
                    p.accent[i] = tree.get<float>("accent" + std::to_string(i));
                    p.background[i] = tree.get<float>("background" + std::to_string(i));
                    if (!std::isfinite(p.accent[i]) || !std::isfinite(p.background[i])) throw std::runtime_error("Invalid colour");
                    p.accent[i] = std::clamp(p.accent[i], 0.f, 1.f);
                    p.background[i] = std::clamp(p.background[i], 0.f, 1.f);
                }
                p.compact = tree.get<bool>("compact", false); p.top = tree.get<bool>("top", false);
                p.snap = tree.get<bool>("snap", true); p.scale = tree.get<float>("scale", 1.f);
                if (!std::isfinite(p.scale)) throw std::runtime_error("Invalid scale");
                p.scale = std::clamp(p.scale, .85f, 1.5f);
				p.pet = tree.get<bool>("petEnabled", true); p.petSize = std::clamp(tree.get<int>("petSize", 128), 32, 128);
                p.petNiko=tree.get<bool>("petNiko",true);p.petAlula=tree.get<bool>("petAlula",true);p.petCalamus=tree.get<bool>("petCalamus",true);p.petSocial=tree.get<bool>("petSocial",true);p.petWorldMachine=tree.get<bool>("petWorldMachine",true);
                p.petScares=tree.get<bool>("petScares",true);p.petChase=tree.get<bool>("petChase",true);
                p.petGameChoice=std::clamp(tree.get<int>("petGameChoice",0),0,1);
                p.petScarer=std::clamp(tree.get<int>("petScarer",2),0,4);
                p.petScaree=std::clamp(tree.get<int>("petScaree",0),0,4);
                p.petChaser=std::clamp(tree.get<int>("petChaser",1),0,4);
				p.petRun = tree.get<bool>("petRun", true); p.petJump = tree.get<bool>("petJump", true);
				p.petClimb = tree.get<bool>("petClimb", true); p.petDrag = tree.get<bool>("petDrag", true);
				p.petRest = std::clamp(tree.get<int>("petRest", 60), 15, 600);
				p.petSleep = std::clamp(tree.get<int>("petSleep", 180), p.petRest + 15, 1200);
                loaded.push_back(p);
            }
            // Older files kept the default outside the saved list. Migrate it once.
            if (root.get<int>("version", 1) < 2 && std::none_of(loaded.begin(), loaded.end(), [](const Preset& p) { return p.name == Preset{}.name; }))
                loaded.insert(loaded.begin(), Preset{});
            Items = std::move(loaded);
        }
        catch (...) { Writable = false; Error = "Could not read menu-presets.json; existing file has been left untouched."; }
    }
    inline bool Store(const std::vector<Preset>& items)
    {
        if (!Writable) return false;
        try
        {
            boost::property_tree::ptree root, list;
            for (const auto& p : items)
            {
                boost::property_tree::ptree tree;
                tree.put("name", p.name);
                for (int i = 0; i < 3; ++i)
                {
                    tree.put("accent" + std::to_string(i), p.accent[i]);
                    tree.put("background" + std::to_string(i), p.background[i]);
                }
                tree.put("compact", p.compact); tree.put("top", p.top); tree.put("snap", p.snap); tree.put("scale", p.scale);
				tree.put("petEnabled", p.pet); tree.put("petSize", p.petSize);
                tree.put("petNiko",p.petNiko);tree.put("petAlula",p.petAlula);tree.put("petCalamus",p.petCalamus);tree.put("petSocial",p.petSocial);tree.put("petWorldMachine",p.petWorldMachine);
                tree.put("petScares",p.petScares);tree.put("petChase",p.petChase);
                tree.put("petGameChoice",p.petGameChoice);
                tree.put("petScarer",p.petScarer);
                tree.put("petScaree",p.petScaree);
                tree.put("petChaser",p.petChaser);
				tree.put("petRun", p.petRun); tree.put("petJump", p.petJump); tree.put("petClimb", p.petClimb); tree.put("petDrag", p.petDrag);
				tree.put("petRest", p.petRest); tree.put("petSleep", p.petSleep);
                list.push_back({ "", tree });
            }
            root.put("version", 2);
            root.add_child("presets", list);
            const auto temp = Path().string() + ".tmp";
            boost::property_tree::write_json(temp, root);
            if (!MoveFileExA(temp.c_str(), Path().string().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Could not replace file");
            Items = items; Error.clear(); return true;
        }
        catch (...) { Error = "Could not save menu presets; check folder permissions."; return false; }
    }
}
