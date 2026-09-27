#include <Windows.h>
#include <format>
#include "../../Nikogram/src/Features/ImGui/MenuPresets.h"
#include <iostream>
#include <limits>
#include <stdexcept>
int main()
{
    int checks = 0;
    auto check = [&](bool ok) { ++checks; if (!ok) throw std::runtime_error("Text theme regression"); };
    using namespace Workspace;
    check(!TextColourOverride);
    check(!PreserveTextColour);
    {
        ScopedTextColour outer;
        check(PreserveTextColour);
        { ScopedTextColour inner; check(PreserveTextColour); }
        check(PreserveTextColour);
    }
    check(!PreserveTextColour);
    try { ScopedTextColour temporary; throw 1; } catch (int) {}
    check(!PreserveTextColour);
    Accent[0]=.2f; Accent[1]=.4f; Accent[2]=.6f;
    for(int i=0;i<3;++i) check(TextChannel(i)==Accent[i]);
    TextColourOverride=true; TextColour[0]=.9f; TextColour[1]=.3f; TextColour[2]=.1f;
    for(int i=0;i<3;++i) check(TextChannel(i)==TextColour[i]);
    const auto custom=MenuPresets::Capture("Custom");
    TextColourOverride=false; TextColour[0]=0;
    MenuPresets::Apply(custom);
    check(TextColourOverride && TextChannel(0)==.9f);
    MenuPresets::Apply(MenuPresets::Preset{});
    check(!TextColourOverride && TextChannel(0)==Accent[0]);
    const auto directory=std::filesystem::temp_directory_path()/std::format("nikogram-text-tests-{}-{}",GetCurrentProcessId(),GetTickCount64());
    std::filesystem::create_directories(directory);
    PreferencesPath=(directory/"workspace.ini").string();
    check(MenuPresets::Store({custom,MenuPresets::Preset{}}));
    MenuPresets::Loaded=false; MenuPresets::Load();
    check(MenuPresets::Writable && MenuPresets::Items.size()==2);
    check(MenuPresets::Items[0].textOverride && MenuPresets::Items[0].textColour==custom.textColour);
    check(!MenuPresets::Items[1].textOverride);
    // Old interface presets have no override fields and must still follow the accent.
    boost::property_tree::ptree root;
    boost::property_tree::read_json(MenuPresets::Path().string(),root);
    for(auto& entry:root.get_child("presets"))
    {
        entry.second.erase("textOverride");
        for(int i=0;i<3;++i) entry.second.erase("textColour"+std::to_string(i));
    }
    boost::property_tree::write_json(MenuPresets::Path().string(),root);
    MenuPresets::Loaded=false; MenuPresets::Load();
    check(MenuPresets::Writable && !MenuPresets::Items[0].textOverride);
    MenuPresets::Apply(custom); check(Save());
    check(GetPrivateProfileIntA("Workspace","TextColourOverride",0,PreferencesPath.c_str())==1);
    check(GetPrivateProfileIntA("Workspace","TextColour0",0,PreferencesPath.c_str())==230);
    TextColourOverride=false; check(Save());
    check(GetPrivateProfileIntA("Workspace","TextColourOverride",1,PreferencesPath.c_str())==0);
    TextColourOverride=true; TextColour[0]=-1; TextColour[1]=2; TextColour[2]=std::numeric_limits<float>::quiet_NaN();
    check(TextChannel(0)==0); check(TextChannel(1)==1); check(TextChannel(2)==0);
    std::cout << checks << " text theme and persistence checks passed; fixtures: " << directory << '\n';
}
