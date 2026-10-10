#pragma once
#include "TextStyle.h"
#include "MenuMode.h"
#include "MenuPalette.h"
#include "DapperStyle.h"
#include "../../MenuStartup.h"
#include <filesystem>
#include <string>
#include <algorithm>

// UI preferences deliberately do not belong to gameplay/bind configurations.
namespace Workspace
{
    inline bool CompactTaskbar = false, TopTaskbar = false, Snap = true;
    inline float FontScale = 1.f;
    inline float Background[3] = { 0.f, 0.f, 0.f };
    inline float InterfaceAccent[3] = { .85f, .75f, .20f }, InterfaceBackground[3] = {0,0,0};
    // Explicitly loading an Interface preset takes precedence for this session.
    inline bool InterfaceColoursOverride = false;
    inline std::string PreferencesPath, LayoutPath;
    inline bool Ready = false;
    // Captured before the config constructor creates directories/default.json.
    inline bool ExistingInstall = false;
	inline bool PetEnabled = true, PetRun = true, PetJump = true, PetClimb = true, PetDrag = true;
	inline int PetSize = 128, PetRest = 60, PetSleep = 180;
    inline bool PetNiko=true,PetAlula=true,PetCalamus=true,PetWorldMachine=true,PetSocial=true;
    inline bool PetScares=true,PetChase=true;
    inline int PetGameChoice=0;
    inline int PetScarer=2;
    inline int PetScaree=0;
    inline int PetChaser=1;

    inline void Load()
    {
        if (Ready) return;
        const auto directory = std::filesystem::current_path() / "Nikogram" / "Core";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        PreferencesPath = (directory / "workspace.ini").string();
        LayoutPath = (directory / "workspace-layout.ini").string();
        const auto read = [](const char* key, int fallback) {
            return int(GetPrivateProfileIntA("Workspace", key, fallback, PreferencesPath.c_str()));
        };
        CompactTaskbar = read("CompactTaskbar", 0) != 0;
        const int preferred = MenuMode::InitialPreference(read("MenuStyle", -1), read("MenuStyleRevision", 0), ExistingInstall);
        MenuMode::Initialize(preferred, MenuStartup::Override);
        const int indicator=read("IndicatorStyle",-1);
        MenuMode::Indicator=MenuMode::Valid(indicator)?indicator:-1;
        for(int index=0;index<3;++index)
        {
            auto& p=DapperStyle::Themes[index];const auto prefix="Dapper"+std::to_string(index);
            p.photo=read((prefix+"Photo").c_str(),index+1);p.opacity=read((prefix+"PhotoOpacity").c_str(),100);
            p.reactive=read((prefix+"ReactivePhoto").c_str(),0)!=0;
            for(int r=0;r<MenuPalette::Count;++r)for(int c=0;c<3;++c)
                p.colours[r][c]=read((prefix+"Colour"+std::to_string(r)+"_"+std::to_string(c)).c_str(),DapperStyle::Defaults[index][r][c])/255.f;
            DapperStyle::Normalize(p);
        }
        // One-time migration: old installs get Nullcore; later explicit choices survive.
        WritePrivateProfileStringA("Workspace", "MenuStyle", std::to_string(preferred).c_str(), PreferencesPath.c_str());
        WritePrivateProfileStringA("Workspace", "MenuStyleRevision", "2", PreferencesPath.c_str());
        for(int role=0;role<MenuPalette::Count;++role) for(int channel=0;channel<3;++channel)
            MenuPalette::Colours[role][channel]=std::clamp(read(("NikogramColour"+std::to_string(role)+"_"+std::to_string(channel)).c_str(),MenuPalette::Defaults[role][channel]),0,255)/255.f;
        MenuMode::Animations = read("MoonlitAnimations", 1) != 0;
        MenuMode::SmoothSliders = read("MoonlitSmoothSliders", 1) != 0;
        MenuMode::AnimationSpeed = std::clamp(read("MoonlitAnimationSpeed", 100), 50, 150) / 100.f;
        TextColourOverride = read("TextColourOverride", 0) != 0;
        TitleTextOverride = read("TitleTextOverride", 0) != 0;
        for (int i = 0; i < 3; ++i)
            TitleTextColour[i] = std::clamp(read(("TitleTextColour" + std::to_string(i)).c_str(), 255), 0, 255) / 255.f;
        InactiveTextOverride = read("InactiveTextOverride", 0) != 0;
        InterfaceBorderSelected = read("InterfaceBorderSelected", 0) != 0;
        InterfaceBorderCustom = read("InterfaceBorderCustom", 0) != 0;
        for (int i = 0; i < 3; ++i)
        {
            InterfaceBorderColour[i] = std::clamp(read(("InterfaceBorderColour" + std::to_string(i)).c_str(), 255), 0, 255) / 255.f;
            InactiveTextColour[i] = std::clamp(read(("InactiveTextColour" + std::to_string(i)).c_str(), 150), 0, 255) / 255.f;
        }
        BorderColourOverride = read("BorderColourOverride", 0) != 0;
        for (int i = 0; i < 3; ++i)
            BorderColour[i] = std::clamp(read(("BorderColour" + std::to_string(i)).c_str(), 255), 0, 255) / 255.f;
        for (int i = 0; i < 3; ++i)
            TextColour[i] = std::clamp(read(("TextColour" + std::to_string(i)).c_str(), 255), 0, 255) / 255.f;
        TopTaskbar = read("TopTaskbar", 0) != 0;
        Snap = read("Snap", 1) != 0;
        FontScale = std::clamp(read("FontScale", 100), 85, 150) / 100.f;
		PetEnabled = read("PetEnabled", 1) != 0;
        PetNiko=read("PetNiko",1)!=0;PetAlula=read("PetAlula",1)!=0;PetCalamus=read("PetCalamus",1)!=0;PetSocial=read("PetSocial",1)!=0;PetWorldMachine=read("PetWorldMachine",1)!=0;
        PetScares=read("PetScares",1)!=0;PetChase=read("PetChase",1)!=0;
        PetGameChoice=std::clamp(read("PetGameChoice",0),0,1);
        PetScarer=std::clamp(read("PetScarer",2),0,4);
        PetScaree=std::clamp(read("PetScaree",0),0,4);
        PetChaser=std::clamp(read("PetChaser",1),0,4);
		PetRun = read("PetRun", 1) != 0; PetJump = read("PetJump", 1) != 0;
		PetClimb = read("PetClimb", 1) != 0; PetDrag = read("PetDrag", 1) != 0;
		PetSize = std::clamp(read("PetSize", 128), 32, 128);
		PetRest = std::clamp(read("PetRest", 60), 15, 600);
		PetSleep = std::clamp(read("PetSleep", 180), PetRest + 15, 1200);
        const char* accentKeys[] = { "AccentR", "AccentG", "AccentB" };
        const char* backgroundKeys[] = { "BackgroundR", "BackgroundG", "BackgroundB" };
        for (int i = 0; i < 3; ++i)
        {
            Accent[i] = std::clamp(read(accentKeys[i], int(Accent[i] * 255)), 0, 255) / 255.f;
            Background[i] = std::clamp(read(backgroundKeys[i], int(Background[i] * 255)), 0, 255) / 255.f;
        }
        std::copy_n(Accent,3,InterfaceAccent);std::copy_n(Background,3,InterfaceBackground);
        Ready = true;
    }

    inline bool Save()
    {
        if (!Ready) return false;
        bool result = true;
        const auto write = [&](const char* key, int value) {
            if (!WritePrivateProfileStringA("Workspace", key, std::to_string(value).c_str(), PreferencesPath.c_str())) result = false;
        };
        write("CompactTaskbar", CompactTaskbar);
        write("MenuStyle", MenuMode::Saved); // Startup override is deliberately not persisted.
        write("MenuStyleRevision", 2);
        write("IndicatorStyle",MenuMode::Indicator);
        for(int index=0;index<3;++index)
        {
            auto& p=DapperStyle::Themes[index];DapperStyle::Normalize(p);const auto prefix="Dapper"+std::to_string(index);
            write((prefix+"Photo").c_str(),p.photo);write((prefix+"PhotoOpacity").c_str(),p.opacity);
            write((prefix+"ReactivePhoto").c_str(),p.reactive);
            for(int r=0;r<MenuPalette::Count;++r)for(int c=0;c<3;++c)
                write((prefix+"Colour"+std::to_string(r)+"_"+std::to_string(c)).c_str(),int(p.colours[r][c]*255+.5f));
        }
        for(int role=0;role<MenuPalette::Count;++role) for(int channel=0;channel<3;++channel)
            write(("NikogramColour"+std::to_string(role)+"_"+std::to_string(channel)).c_str(),int(MenuPalette::Colours[role][channel]*255+.5f));
        write("MoonlitAnimations", MenuMode::Animations);
        write("MoonlitSmoothSliders", MenuMode::SmoothSliders);
        write("MoonlitAnimationSpeed", int(MenuMode::AnimationSpeed * 100 + .5f));
        write("TextColourOverride", TextColourOverride);
        write("TitleTextOverride", TitleTextOverride);
        for (int i = 0; i < 3; ++i)
            write(("TitleTextColour" + std::to_string(i)).c_str(), int(TitleTextColour[i] * 255 + .5f));
        write("InactiveTextOverride", InactiveTextOverride);
        write("InterfaceBorderSelected", InterfaceBorderSelected);
        write("InterfaceBorderCustom", InterfaceBorderCustom);
        for (int i = 0; i < 3; ++i)
        {
            write(("InterfaceBorderColour" + std::to_string(i)).c_str(), int(InterfaceBorderColour[i] * 255 + .5f));
            write(("InactiveTextColour" + std::to_string(i)).c_str(), int(InactiveTextColour[i] * 255 + .5f));
        }
        write("BorderColourOverride", BorderColourOverride);
        for (int i = 0; i < 3; ++i)
            write(("BorderColour" + std::to_string(i)).c_str(), int(BorderColour[i] * 255 + .5f));
        for (int i = 0; i < 3; ++i)
            write(("TextColour" + std::to_string(i)).c_str(), int(TextColour[i] * 255 + .5f));
        write("TopTaskbar", TopTaskbar);
        write("Snap", Snap);
        write("FontScale", int(FontScale * 100 + 0.5f));
		write("PetEnabled", PetEnabled); write("PetSize", PetSize);
        write("PetNiko",PetNiko);write("PetAlula",PetAlula);write("PetCalamus",PetCalamus);write("PetSocial",PetSocial);write("PetWorldMachine",PetWorldMachine);
        write("PetScares",PetScares);write("PetChase",PetChase);
        write("PetGameChoice",PetGameChoice);
        write("PetScarer",PetScarer);
        write("PetScaree",PetScaree);
        write("PetChaser",PetChaser);
		write("PetRun", PetRun); write("PetJump", PetJump); write("PetClimb", PetClimb); write("PetDrag", PetDrag);
		write("PetRest", PetRest); write("PetSleep", PetSleep);
        const char* accentKeys[] = { "AccentR", "AccentG", "AccentB" };
        const char* backgroundKeys[] = { "BackgroundR", "BackgroundG", "BackgroundB" };
        for (int i = 0; i < 3; ++i)
        {
            write(accentKeys[i], int(Accent[i] * 255 + 0.5f));
            write(backgroundKeys[i], int(Background[i] * 255 + 0.5f));
        }
        return result;
    }
}
