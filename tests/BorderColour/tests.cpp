#define NOMINMAX
#include <Windows.h>
#include <cassert>
#include <iostream>
#include "../../Nikogram/src/Features/Configs/ConfigColours.h"
#include "../../Nikogram/src/Features/ImGui/MenuPresets.h"
int main()
{
    using namespace Workspace;
    Accent[0]=.3f; BorderColour[0]=.7f; BorderColourOverride=false;
    assert(BorderChannel(0)==.3f);
    Accent[0]=.4f; assert(BorderChannel(0)==.4f);
    BorderColourOverride=true; assert(BorderChannel(0)==.7f);
    Accent[0]=.6f; assert(BorderChannel(0)==.7f);
    BorderColour[1]=2.f; assert(BorderChannel(1)==1.f); BorderColour[1]=.5f;
    boost::property_tree::ptree config;
    TitleTextOverride=false;
    assert(std::abs(TitleTextChannel(0)-(.6f+.4f*.2f))<.0001f);
    Accent[0]=.4f; assert(std::abs(TitleTextChannel(0)-.52f)<.0001f);
    TitleTextOverride=true; TitleTextColour[0]=.83f;
    ConfigColours::Save(config);
    BorderColourOverride=false; BorderColour[0]=0.f;
    ConfigColours::Load(config); assert(BorderColourOverride && BorderChannel(0)==.7f);
    assert(TitleTextOverride && TitleTextChannel(0)==.83f);
    auto preset=MenuPresets::Capture("Border test");
    BorderColour[0]=0.f; MenuPresets::Apply(preset);
    assert(BorderColourOverride && BorderChannel(0)==.7f);
    config.put("MenuColours.Border0",.2f); ConfigColours::Load(config);
    assert(BorderChannel(0)==.7f); // Explicit Interface selection wins.
    assert(BorderColour[0]==.2f);
    boost::property_tree::ptree saved; ConfigColours::Save(saved);
    assert(saved.get<float>("MenuColours.Border0")==.2f);
    MenuPresets::Apply(MenuPresets::Preset{});
    assert(!InterfaceBorderSelected && !InterfaceColoursOverride);
    assert(BorderChannel(0)==.2f);
    ConfigColours::Load(config); assert(BorderChannel(0)==.2f);
    ConfigColours::Load({}); assert(!BorderColourOverride && BorderChannel(0)==Accent[0]);
    assert(!TitleTextOverride);
    TextColourOverride=false; InactiveTextOverride=false;
    assert(TextChannel(0)==Accent[0] && InactiveTextChannel(0)==Accent[0]*.65f);
    TextColourOverride=true; TextColour[0]=.9f;
    InactiveTextOverride=true; InactiveTextColour[0]=.15f;
    ConfigColours::Save(saved); TextColour[0]=0; InactiveTextColour[0]=0;
    ConfigColours::Load(saved);
    assert(TextChannel(0)==.9f && InactiveTextChannel(0)==.15f);
    const auto directory=std::filesystem::temp_directory_path()/std::format("nikogram-border-test-{}-{}",GetCurrentProcessId(),GetTickCount64());
    assert(std::filesystem::create_directory(directory));
    PreferencesPath=(directory/"workspace.ini").string();
    assert(MenuPresets::Store({preset}));
    MenuPresets::Loaded=false; MenuPresets::Load();
    assert(MenuPresets::Writable && MenuPresets::Items.size()==1);
    assert(MenuPresets::Items[0].borderOverride && MenuPresets::Items[0].borderColour[0]==.7f);
    assert(MenuPresets::Items[0].titleTextOverride && MenuPresets::Items[0].titleTextColour[0]==.83f);
    MenuPresets::Apply(MenuPresets::Items[0]); assert(Save());
    BorderColourOverride=false; BorderColour[0]=0.f;
    assert(GetPrivateProfileIntA("Workspace","InterfaceBorderSelected",0,PreferencesPath.c_str())==1);
    assert(GetPrivateProfileIntA("Workspace","InterfaceBorderCustom",0,PreferencesPath.c_str())==1);
    assert(GetPrivateProfileIntA("Workspace","TitleTextOverride",0,PreferencesPath.c_str())==1);
    std::filesystem::remove(MenuPresets::Path());
    std::filesystem::remove(PreferencesPath);
    std::filesystem::remove(directory);
    std::cout<<"BorderColour: accent inheritance, independent colour, config round trip, old config fallback, interface priority, preset and workspace persistence passed\n";
}
