#include "MoonlitControls.inl"

void CMenu::MenuMoonlitMisc()
{
    using namespace ImGui;
    using namespace MoonlitUI;
    namespace M=Vars::Misc;
    const bool inputBlocked=BlockCaptureFrame || CaptureFrame==GetFrameCount();
    BeginDisabled(inputBlocked);
    if(MiscPage==5)
    {
        // Keep the real catalog, per-weapon selections and preset confirmations.
        // The existing skin editor is isolated on its own page, not reimplemented.
        SkinChanger::Menu();
    }
    else if(BeginTable("MoonlitMiscColumns",MoonlitBinding::Columns(GetContentRegionAvail().x,Vars::Menu::Scale.Value),ImGuiTableFlags_SizingStretchSame))
    {
        TableNextColumn();
        switch(MiscPage)
        {
        case 0:
            if(Card c{"Jumping & strafing"};c){Setting(M::Movement::Bunnyhop,"Keep jumping while jump is held.",true);Setting(M::Movement::EdgeJump,"Jump when leaving an edge.",true);SettingChoice(M::Movement::AutoStrafe);}
            if(Card c{"Directional strafe tuning",true};c){BeginDisabled(FGet(M::Movement::AutoStrafe)!=M::Movement::AutoStrafeEnum::Directional);SettingSlider(M::Movement::AutoStrafeTurnScale);SettingSlider(M::Movement::AutoStrafeMaxDelta);EndDisabled();}
            if(Card c{"Specialist jumps",true};c){Setting(M::Movement::AutoJumpbug,"Setup-sensitive; not guaranteed to prevent fall damage.",true);Setting(M::Movement::BreakJump,nullptr,true);}
            TableNextColumn();
            if(Card c{"Class movement"};c){Setting(M::Movement::AutoRocketJump,nullptr,true);Setting(M::Movement::AutoCTap,nullptr,true);Setting(M::Movement::AutoFaNJump,nullptr,true);Setting(M::Movement::AutoRevJump,nullptr,true);Setting(M::Movement::ShieldTurnRate,nullptr,true);}
            if(Card c{"Movement control",true};c){Setting(M::Movement::FastStop,nullptr,true);Setting(M::Movement::FastAccelerate,nullptr,true);Setting(M::Movement::DuckSpeed,nullptr,true);Setting(M::Movement::NoPush,nullptr,true);Setting(M::Movement::MovementLock,nullptr,true);}
            if(Vars::Debug::Options.Value)if(Card c{"Rocket-jump debug tuning",true};c){SettingSlider(M::Movement::AutoRocketJumpChokeGrounded);SettingSlider(M::Movement::AutoRocketJumpChokeAir);SettingSlider(M::Movement::AutoRocketJumpSkipGround);SettingSlider(M::Movement::AutoRocketJumpSkipAir);SettingSlider(M::Movement::AutoRocketJumpTimingOffset);SettingSlider(M::Movement::AutoRocketJumpApplyAbove);}
            break;
        case 1:
            if(Card c{"Freelook"};c)
            {
                PendingModes.try_emplace(&M::Freelook::Enabled,BindEnum::KeyEnum::Hold);
                Setting(M::Freelook::Enabled,"Hold a key to look around without turning your normal aim or movement. Works in first and third person, including while zoomed.",true);
                Setting(M::Freelook::Limited,"Off allows continuous horizontal rotation; vertical view always stops at straight up/down.");
                BeginDisabled(!FGet(M::Freelook::Limited));
                SettingSlider(M::Freelook::Horizontal);SettingSlider(M::Freelook::Vertical);EndDisabled();
                SettingChoice(M::Freelook::Return);
                BeginDisabled(FGet(M::Freelook::Return)!=M::Freelook::ReturnEnum::Smooth);
                SettingSlider(M::Freelook::ReturnTime);EndDisabled();
            }
            if(Card c{"OptiFine Zoom"};c)
            {
                PendingModes.try_emplace(&M::OptifineZoom::Enabled,BindEnum::KeyEnum::Hold);
                Setting(M::OptifineZoom::Enabled,"Assign a key to hold-to-zoom. Release restores your current normal or scoped view.",true);
                SettingSlider(M::OptifineZoom::Magnification,"Optical magnification relative to your current view.");
                Setting(M::OptifineZoom::Smooth);BeginDisabled(!FGet(M::OptifineZoom::Smooth));
                SettingSlider(M::OptifineZoom::Transition);EndDisabled();
                Setting(M::OptifineZoom::ScaleSensitivity,"Reduce mouse sensitivity in proportion to the zoom, including its transition.");
            }
            if(Card c{"Player actions"};c){SettingChoice(M::Automation::AntiBackstab);Setting(M::Automation::TauntControl,nullptr,true);Setting(M::Automation::KartControl,nullptr,true);}
            if(Card c{"Mann vs. Machine",true};c){Setting(M::MannVsMachine::InstantRespawn);Setting(M::MannVsMachine::InstantRevive);Setting(M::MannVsMachine::AllowInspect);}
            TableNextColumn();
            if(Card c{"Session conveniences"};c){Setting(M::Automation::AntiAFK);Setting(M::Automation::AntiAutobalance);Setting(M::Automation::AcceptItemDrops);}
            if(Card c{"Automatic voting",true};c){Setting(M::Automation::AutoF2Ignored);Setting(M::Automation::AutoF1Priority);}
            break;
        case 2:
            if(Card c{"Blockbot"};c)
            {
                Setting(M::Blockbot::Enabled,"Automatically position around a selected player.",true);
                SettingChoice(M::Blockbot::Target);SettingChoice(M::Blockbot::Team);SettingChoice(M::Blockbot::Behavior);
                if(FGet(M::Blockbot::Team)!=M::Blockbot::TeamEnum::Enemies)SettingChoice(M::Blockbot::TeammateBehavior);
                SettingSlider(M::Blockbot::Range);
                if(FGet(M::Blockbot::Target)==M::Blockbot::TargetEnum::Manual){TextWrapped("Manual target: %s",F::Blockbot.ManualName().c_str());Help("Right-click a player in the player list to select them.");}
                TextWrapped("%s",F::Blockbot.Status().c_str());
            }
            if(Card c{"Following & response",true};c){SettingSlider(M::Blockbot::ResumeDelay);SettingSlider(M::Blockbot::Acceleration);SettingSlider(M::Blockbot::Deceleration);Setting(M::Blockbot::WhileMenuOpen);Setting(M::Blockbot::WhileCrouching);Setting(M::Blockbot::ContinueFollowing);Setting(M::Blockbot::IgnoreDanger);}
            TableNextColumn();
            if(Card c{"Cheat detection"};c){SettingChoice(Vars::CheatDetection::Methods,"Suspicious behavior is not conclusive proof.");}
            if(Card c{"Detection thresholds",true};c){SettingSlider(Vars::CheatDetection::DetectionsRequired);BeginDisabled(!(FGet(Vars::CheatDetection::Methods)&Vars::CheatDetection::MethodsEnum::PacketChoking));SettingSlider(Vars::CheatDetection::MinChoking);EndDisabled();BeginDisabled(!(FGet(Vars::CheatDetection::Methods)&Vars::CheatDetection::MethodsEnum::AimFlicking));SettingSlider(Vars::CheatDetection::MinFlick);SettingSlider(Vars::CheatDetection::MaxNoise);EndDisabled();}
            break;
        case 3:
            if(Card c{"Matchmaking"};c){Setting(M::Queueing::AutoCasualQueue);Setting(M::Queueing::ExtendQueue);}
            if(Card c{"Region selection",true};c){SettingChoice(M::Queueing::ForceRegions,"Availability depends on matchmaking.");}
            TableNextColumn();
            if(Card c{"Sound"};c){Setting(M::Sound::HitsoundAlways);Setting(M::Sound::RemoveDSP,"Remove environmental sound processing.");Setting(M::Sound::GiantWeaponSounds);}
            if(Card c{"Sound removals",true};c){SettingChoice(M::Sound::Block);}
            break;
        case 4:
            if(Card c{"Compatibility & performance"};c){Setting(M::Game::NetworkFix);Setting(M::Game::SetupBonesOptimization);Setting(M::Game::AntiCheatCompatibility,"Compatibility setting, not a guarantee of protection.");}
            if(Vars::Debug::Options.Value)if(Card c{"Debug compatibility",true};c){Setting(M::Game::AntiCheatCritHack);}
            TableNextColumn();
            if(Card c{"Advanced game utilities",true};c){Setting(M::Exploits::PureBypass);Setting(M::Exploits::CheatsBypass);Setting(M::Exploits::UnlockCVars);Setting(M::Exploits::EquipRegionUnlock);Setting(M::Exploits::BackpackExpander);Setting(M::Exploits::NoisemakerSpam,nullptr,true);Setting(M::Exploits::PingReducer,"Changes reported ping behavior, not physical network latency.");BeginDisabled(!FGet(M::Exploits::PingReducer));SettingSlider(M::Exploits::PingTarget);EndDisabled();}
            break;
        }
        EndTable();
    }
    EndDisabled();
    if(Capturing)TextColored(Gold,"Press a key... Escape cancels.");
    if(!BindStatus.empty())Help(BindStatus.c_str());
    Help("Settings share your existing config. Save changes from Configs & binds.");
}
