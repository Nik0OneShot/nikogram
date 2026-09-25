#pragma once
#include <fstream>
#include <string>

// Complete user-provided default.json, including binds and ESP groups.
namespace BundledDefault
{
inline constexpr char Json[] =
R"NIKODEFAULT({
    "Binds": {
        "0": {
            "Name": "aim",
            "Type": "0",
            "Info": "0",
            "Key": "6",
            "Enabled": "true",
            "Visibility": "0",
            "Not": "false",
            "Active": "false",
            "Parent": "-1"
        },
        "1": {
            "Name": "crit",
            "Type": "0",
            "Info": "0",
            "Key": "16",
            "Enabled": "true",
            "Visibility": "0",
            "Not": "false",
            "Active": "false",
            "Parent": "-1"
        },
        "2": {
            "Name": "thirdperson",
            "Type": "0",
            "Info": "1",
            "Key": "38",
            "Enabled": "true",
            "Visibility": "0",
            "Not": "false",
            "Active": "false",
            "Parent": "-1"
        }
    },
    "Vars": {
        "Vars::Menu::CheatTitle": {
            "-1": "Nikogram"
        },
        "Vars::Menu::CheatTag": {
            "-1": "[Nikogram]"
        },
        "Vars::Menu::PrimaryKey": {
            "-1": "45"
        },
        "Vars::Menu::SecondaryKey": {
            "-1": "114"
        },
        "Vars::Menu::BindWindow": {
            "-1": "true"
        },
        "Vars::Menu::BindWindowTitle": {
            "-1": "true"
        },
        "Vars::Menu::MenuShowsBinds": {
            "-1": "false"
        },
        "Vars::Menu::Indicators": {
            "-1": "0"
        },
        "Vars::Menu::BindsDisplay": {
            "-1": {
                "x": "100",
                "y": "100"
            }
        },
        "Vars::Menu::TicksDisplay": {
            "-1": {
                "x": "150",
                "y": "100"
            }
        },
        "Vars::Menu::CritsDisplay": {
            "-1": {
                "x": "150",
                "y": "100"
            }
        },
        "Vars::Menu::SpectatorsDisplay": {
            "-1": {
                "x": "150",
                "y": "100"
            }
        },
        "Vars::Menu::PingDisplay": {
            "-1": {
                "x": "150",
                "y": "100"
            }
        },
        "Vars::Menu::ConditionsDisplay": {
            "-1": {
                "x": "150",
                "y": "100"
            }
        },
        "Vars::Menu::SeedPredictionDisplay": {
            "-1": {
                "x": "150",
                "y": "100"
            }
        },
        "Vars::Menu::Scale": {
            "-1": "1"
        },
        "Vars::Menu::CheapText": {
            "-1": "false"
        },
        "Vars::Menu::Theme::Accent": {
            "-1": {
                "r": "175",
                "g": "150",
                "b": "255",
                "a": "255"
            }
        },
        "Vars::Menu::Theme::Background": {
            "-1": {
                "r": "0",
                "g": "0",
                "b": "0",
                "a": "250"
            }
        },
        "Vars::Menu::Theme::Active": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "255"
            }
        },
        "Vars::Menu::Theme::Inactive": {
            "-1": {
                "r": "150",
                "g": "150",
                "b": "150",
                "a": "255"
            }
        },
        "Vars::Colors::Local": {
            "-1": {
                "r": "217",
                "g": "191",
                "b": "51",
                "a": "255"
            }
        },
        "Vars::Colors::FOVCircle": {
            "-1": {
                "r": "217",
                "g": "191",
                "b": "51",
                "a": "255"
            }
        },
        "Vars::Colors::SpellFootstep": {
            "-1": {
                "r": "217",
                "g": "191",
                "b": "51",
                "a": "255"
            }
        },
        "Vars::Colors::WorldModulation": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "255"
            }
        },
        "Vars::Colors::SkyModulation": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "255"
            }
        },
        "Vars::Colors::PropModulation": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "255"
            }
        },
        "Vars::Colors::ParticleModulation": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "255"
            }
        },
        "Vars::Colors::FogModulation": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "255"
            }
        },
        "Vars::Colors::Line": {
            "-1": {
                "r": "217",
                "g": "191",
                "b": "51",
                "a": "255"
            }
        },
        "Vars::Colors::LineIgnoreZ": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "0"
            }
        },
        "Vars::Colors::BoneHitboxEdge": {
            "-1": {
                "r": "217",
                "g": "191",
                "b": "51",
                "a": "255"
            }
        },
        "Vars::Colors::BoneHitboxEdgeIgnoreZ": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "0"
            }
        },
        "Vars::Colors::BoneHitboxFace": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "0"
            }
        },
        "Vars::Colors::BoneHitboxFaceIgnoreZ": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "0"
            }
        },
        "Vars::Colors::TargetHitboxEdge": {
            "-1": {
                "r": "255",
                "g": "150",
                "b": "150",
                "a": "255"
            }
        },
        "Vars::Colors::TargetHitboxEdgeIgnoreZ": {
            "-1": {
                "r": "255",
                "g": "150",
                "b": "150",
                "a": "0"
            }
        },
        "Vars::Colors::TargetHitboxFace": {
            "-1": {
                "r": "255",
                "g": "150",
                "b": "150",
                "a": "0"
            }
        },
        ")NIKODEFAULT"
R"NIKODEFAULT(Vars::Colors::TargetHitboxFaceIgnoreZ": {
            "-1": {
                "r": "255",
                "g": "150",
                "b": "150",
                "a": "0"
            }
        },
        "Vars::Colors::BoundHitboxEdge": {
            "-1": {
                "r": "217",
                "g": "191",
                "b": "51",
                "a": "255"
            }
        },
        "Vars::Colors::BoundHitboxEdgeIgnoreZ": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "0"
            }
        },
        "Vars::Colors::BoundHitboxFace": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "0"
            }
        },
        "Vars::Colors::BoundHitboxFaceIgnoreZ": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "0"
            }
        },
        "Vars::Colors::PlayerPath": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "0"
            }
        },
        "Vars::Colors::PlayerPathIgnoreZ": {
            "-1": {
                "r": "217",
                "g": "191",
                "b": "51",
                "a": "255"
            }
        },
        "Vars::Colors::ProjectilePath": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "0"
            }
        },
        "Vars::Colors::ProjectilePathIgnoreZ": {
            "-1": {
                "r": "217",
                "g": "191",
                "b": "51",
                "a": "255"
            }
        },
        "Vars::Colors::TrajectoryPath": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "0"
            }
        },
        "Vars::Colors::TrajectoryPathIgnoreZ": {
            "-1": {
                "r": "217",
                "g": "191",
                "b": "51",
                "a": "255"
            }
        },
        "Vars::Colors::ShotPath": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "0"
            }
        },
        "Vars::Colors::ShotPathIgnoreZ": {
            "-1": {
                "r": "217",
                "g": "191",
                "b": "51",
                "a": "255"
            }
        },
        "Vars::Colors::SplashRadius": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "0"
            }
        },
        "Vars::Colors::SplashRadiusIgnoreZ": {
            "-1": {
                "r": "217",
                "g": "191",
                "b": "51",
                "a": "255"
            }
        },
        "Vars::Aimbot::General::AimType": {
            "-1": "0",
            "0": "3"
        },
        "Vars::Aimbot::General::TargetSelection": {
            "-1": "0"
        },
        "Vars::Aimbot::General::Target": {
            "-1": "127"
        },
        "Vars::Aimbot::General::Ignore": {
            "-1": "56"
        },
        "Vars::Aimbot::General::AimFOV": {
            "-1": "20",
            "0": "20"
        },
        "Vars::Aimbot::General::MaxTargets": {
            "-1": "2"
        },
        "Vars::Aimbot::General::IgnoreInvisible": {
            "-1": "50"
        },
        "Vars::Aimbot::General::AssistStrength": {
            "-1": "25"
        },
        "Vars::Aimbot::General::TickTolerance": {
            "-1": "4"
        },
        "Vars::Aimbot::General::AutoShoot": {
            "-1": "true"
        },
        "Vars::Aimbot::General::FOVCircle": {
            "-1": "true"
        },
        "Vars::Aimbot::General::LeadAndRestrict": {
            "-1": "true"
        },
        "Vars::Aimbot::General::NoSpread": {
            "-1": "true"
        },
        "Vars::Aimbot::Hitscan::Hitboxes": {
            "-1": "39"
        },
        "Vars::Aimbot::Hitscan::MultipointHitboxes": {
            "-1": "6"
        },
        "Vars::Aimbot::Hitscan::Modifiers": {
            "-1": "64"
        },
        "Vars::Aimbot::Hitscan::MultipointScale": {
            "-1": "25"
        },
        "Vars::Aimbot::Hitscan::TapfireDistance": {
            "-1": "1000"
        },
        "Vars::Aimbot::Projectile::StrafePrediction": {
            "-1": "3"
        },
        "Vars::Aimbot::Projectile::SplashPrediction": {
            "-1": "2"
        },
        "Vars::Aimbot::Projectile::AutoDetonate": {
            "-1": "0"
        },
        "Vars::Aimbot::Projectile::AutoAirblast": {
            "-1": "0"
        },
        "Vars::Aimbot::Projectile::Hitboxes": {
            "-1": "47"
        },
        "Vars::Aimbot::Projectile::Modifiers": {
            "-1": "34"
        },
        "Vars::Aimbot::Projectile::MaxSimulationTime": {
            "-1": "10"
        },
        "Vars::Aimbot::Projectile::HitChance": {
            "-1": "0"
        },
        "Vars::Aimbot::Projectile::AutodetRadius": {
            "-1": "80"
        },
        "Vars::Aimbot::Projectile::SplashRadius": {
            "-1": "80"
        },
        "Vars::Aimbot::Projectile::AutoRelease": {
            "-1": "0"
        },
        "Vars::Aimbot::Melee::AutoBackstab": {
            "-1": "true"
        },
        "Vars::Aimbot::Melee::IgnoreRazorback": {
            "-1": "true"
        },
        "Vars::Aimbot::Melee::SwingPrediction": {
            "-1": "true"
        },
        "Vars::Aimbot::Melee::WhipTeam": {
            "-1": "true"
        },
        "Vars::Aimbot::Healing::HealPriority": {
            "-1": "2"
        },
        "Vars::Aimbot::Healing::DangerIgnore": {
            "-1": "8"
        },
        "Vars::Aimbot::Healing::AutoHeal": {
            "-1": "true"
        },
        "Vars::Aimbot::Healing::AutoArrow": {
            "-1": "true"
        },
        "Vars::Aimbot::Healing::AutoRepair": {
            "-1": "true"
        },
        "Vars::Aimbot::Healing::AutoSandvich": {
            "-1": "true"
        },
        "Vars::Aimbot::Healing::AutoVaccinator": {
            "-1": "true"
        },
        "Vars::Aimbot::Healing::ActivateOnVoice": {
            "-1": "true"
        },
        "Vars::CritHack::ForceCrits": {
            "-1": "false",
            "1": "true"
        },
        "Vars::CritHack::AvoidRandomCrits": {
            "-1": "true"
        },
        "Vars::CritHack::AlwaysMeleeCrit": {
            "-1": "false"
        },
        "Vars::CritHack::Crit)NIKODEFAULT"
R"NIKODEFAULT(Effects": {
            "-1": "true"
        },
        "Vars::Backtrack::Latency": {
            "-1": "0"
        },
        "Vars::Backtrack::Interp": {
            "-1": "0"
        },
        "Vars::Backtrack::Window": {
            "-1": "185"
        },
        "Vars::Backtrack::PreferOnShot": {
            "-1": "false"
        },
        "Vars::Doubletap::Doubletap": {
            "-1": "false"
        },
        "Vars::Doubletap::Warp": {
            "-1": "false"
        },
        "Vars::Doubletap::RechargeTicks": {
            "-1": "false"
        },
        "Vars::Doubletap::AntiWarp": {
            "-1": "true"
        },
        "Vars::Doubletap::TickLimit": {
            "-1": "22"
        },
        "Vars::Doubletap::WarpRate": {
            "-1": "22"
        },
        "Vars::Doubletap::RechargeLimit": {
            "-1": "24"
        },
        "Vars::Doubletap::PassiveRecharge": {
            "-1": "0"
        },
        "Vars::Fakelag::Fakelag": {
            "-1": "0"
        },
        "Vars::Fakelag::Options": {
            "-1": "0"
        },
        "Vars::Fakelag::PlainTicks": {
            "-1": "22"
        },
        "Vars::Fakelag::RandomTicks": {
            "-1": {
                "Min": "14",
                "Max": "18"
            }
        },
        "Vars::Fakelag::UnchokeOnAttack": {
            "-1": "true"
        },
        "Vars::Fakelag::RetainBlastJump": {
            "-1": "false"
        },
        "Vars::AutoPeek::Enabled": {
            "-1": "false"
        },
        "Vars::Speedhack::Scale": {
            "-1": "1"
        },
        "Vars::AntiAim::Enabled": {
            "-1": "false"
        },
        "Vars::AntiAim::PitchReal": {
            "-1": "0"
        },
        "Vars::AntiAim::PitchFake": {
            "-1": "0"
        },
        "Vars::AntiAim::YawReal": {
            "-1": "4"
        },
        "Vars::AntiAim::YawFake": {
            "-1": "0"
        },
        "Vars::AntiAim::RealYawBase": {
            "-1": "0"
        },
        "Vars::AntiAim::FakeYawBase": {
            "-1": "0"
        },
        "Vars::AntiAim::RealYawOffset": {
            "-1": "0"
        },
        "Vars::AntiAim::FakeYawOffset": {
            "-1": "0"
        },
        "Vars::AntiAim::RealYawValue": {
            "-1": "150"
        },
        "Vars::AntiAim::FakeYawValue": {
            "-1": "-90"
        },
        "Vars::AntiAim::SpinSpeed": {
            "-1": "15"
        },
        "Vars::AntiAim::MinWalk": {
            "-1": "true"
        },
        "Vars::AntiAim::RealCompensation": {
            "-1": "true"
        },
        "Vars::AntiAim::HidePitchOnShot": {
            "-1": "false"
        },
        "Vars::Resolver::Enabled": {
            "-1": "false"
        },
        "Vars::Resolver::AutoResolve": {
            "-1": "false"
        },
        "Vars::Resolver::AutoResolveCheatersOnly": {
            "-1": "false"
        },
        "Vars::Resolver::AutoResolveHeadshotOnly": {
            "-1": "false"
        },
        "Vars::Resolver::AutoResolveYawAmount": {
            "-1": "90"
        },
        "Vars::Resolver::AutoResolvePitchAmount": {
            "-1": "90"
        },
        "Vars::Resolver::CycleYaw": {
            "-1": "0"
        },
        "Vars::Resolver::CyclePitch": {
            "-1": "0"
        },
        "Vars::Resolver::CycleView": {
            "-1": "false"
        },
        "Vars::Resolver::CycleMinwalk": {
            "-1": "false"
        },
        "Vars::ESP::ActiveGroups": {
            "-1": "-1"
        },
        "Vars::Visuals::UI::StreamerMode": {
            "-1": "0"
        },
        "Vars::Visuals::UI::ChatTags": {
            "-1": "15"
        },
        "Vars::Visuals::UI::FieldOfView": {
            "-1": "100"
        },
        "Vars::Visuals::UI::ZoomFieldOfView": {
            "-1": "0"
        },
        "Vars::Visuals::UI::AspectRatio": {
            "-1": "0"
        },
        "Vars::Visuals::UI::RevealScoreboard": {
            "-1": "true"
        },
        "Vars::Visuals::UI::ScoreboardUtility": {
            "-1": "true"
        },
        "Vars::Visuals::UI::ScoreboardColors": {
            "-1": "true"
        },
        "Vars::Visuals::UI::CleanScreenshots": {
            "-1": "true"
        },
        "Vars::Visuals::Thirdperson::Enabled": {
            "-1": "false",
            "2": "true"
        },
        "Vars::Visuals::Thirdperson::Crosshair": {
            "-1": "false"
        },
        "Vars::Visuals::Thirdperson::Distance": {
            "-1": "150"
        },
        "Vars::Visuals::Thirdperson::Right": {
            "-1": "0"
        },
        "Vars::Visuals::Thirdperson::Up": {
            "-1": "0"
        },
        "Vars::Visuals::Removals::Interpolation": {
            "-1": "false"
        },
        "Vars::Visuals::Removals::Lerp": {
            "-1": "true"
        },
        "Vars::Visuals::Removals::Disguises": {
            "-1": "false"
        },
        "Vars::Visuals::Removals::Taunts": {
            "-1": "false"
        },
        "Vars::Visuals::Removals::Scope": {
            "-1": "false"
        },
        "Vars::Visuals::Removals::PostProcessing": {
            "-1": "false"
        },
        "Vars::Visuals::Removals::ScreenOverlays": {
            "-1": "false"
        },
        "Vars::Visuals::Removals::ScreenEffects": {
            "-1": "false"
        },
        "Vars::Visuals::Removals::ViewPunch": {
            "-1": "false"
        },
        "Vars::Visuals::Removals::AngleForcing": {
            "-1": "false"
        },
        "Vars::Visuals::Removals::Ragdolls": {
            "-1": "false"
        },
        "Vars::Visuals::Removals::Gibs": {
            "-1": "false"
        },
        "Vars::Visuals::Removals::MOTD": {
            "-1": "true"
        },
        "Vars::Visuals::Animations::Interpolation": {
            "-1": "true"
        },
        "Vars::Visuals::Effects::BulletTracer": {
            "-1": "Default"
        },
        "Vars::Visuals::Effects::CritTracer": {
            "-1": "Default"
        },
        "Vars::Visuals::Effects::MedigunBeam": {
            "-1": "Default"
        },
        "Vars::Visuals::Effects::MedigunCharge": {
            "-1": "Default"
        },
        "Vars::Visuals::Effects::ProjectileTrail": {
            "-1": "Default"
        },
        "Vars::Visuals::Effects::SpellFootsteps": {
            "-1": "0"
        },
        "Vars::Visuals::Effects::RagdollEffects": {
            "-1": "0"
        },
        "Vars::Visuals::Effects::DrawIconsThroughWalls": {
            "-1": "false"
        },
        "Vars::Visuals::Effects::DrawDamageNumbersThroughWalls": {
            "-1": )NIKODEFAULT"
R"NIKODEFAULT("false"
        },
        "Vars::Visuals::Viewmodel::CrosshairAim": {
            "-1": "true"
        },
        "Vars::Visuals::Viewmodel::ViewmodelAim": {
            "-1": "false"
        },
        "Vars::Visuals::Viewmodel::OffsetX": {
            "-1": "0"
        },
        "Vars::Visuals::Viewmodel::OffsetY": {
            "-1": "0"
        },
        "Vars::Visuals::Viewmodel::OffsetZ": {
            "-1": "0"
        },
        "Vars::Visuals::Viewmodel::Pitch": {
            "-1": "0"
        },
        "Vars::Visuals::Viewmodel::Yaw": {
            "-1": "0"
        },
        "Vars::Visuals::Viewmodel::Roll": {
            "-1": "0"
        },
        "Vars::Visuals::Viewmodel::SwayScale": {
            "-1": "2"
        },
        "Vars::Visuals::Viewmodel::SwayInterp": {
            "-1": "0.100000001"
        },
        "Vars::Visuals::World::Modulations": {
            "-1": "0"
        },
        "Vars::Visuals::World::SkyboxChanger": {
            "-1": "Off"
        },
        "Vars::Visuals::World::WorldTexture": {
            "-1": "Default"
        },
        "Vars::Visuals::World::NearPropFade": {
            "-1": "false"
        },
        "Vars::Visuals::World::NoPropFade": {
            "-1": "false"
        },
        "Vars::Visuals::Beams::Model": {
            "-1": "sprites\/physbeam.vmt"
        },
        "Vars::Visuals::Beams::Life": {
            "-1": "2"
        },
        "Vars::Visuals::Beams::Width": {
            "-1": "2"
        },
        "Vars::Visuals::Beams::EndWidth": {
            "-1": "2"
        },
        "Vars::Visuals::Beams::FadeLength": {
            "-1": "10"
        },
        "Vars::Visuals::Beams::Amplitude": {
            "-1": "2"
        },
        "Vars::Visuals::Beams::Brightness": {
            "-1": "255"
        },
        "Vars::Visuals::Beams::Speed": {
            "-1": "0.200000003"
        },
        "Vars::Visuals::Beams::Segments": {
            "-1": "2"
        },
        "Vars::Visuals::Beams::Color": {
            "-1": {
                "r": "255",
                "g": "255",
                "b": "255",
                "a": "255"
            }
        },
        "Vars::Visuals::Beams::Flags": {
            "-1": "65792"
        },
        "Vars::Visuals::Line::TracersEnabled": {
            "-1": "true"
        },
        "Vars::Visuals::Line::DrawDuration": {
            "-1": "5"
        },
        "Vars::Visuals::Hitbox::BonesEnabled": {
            "-1": "0"
        },
        "Vars::Visuals::Hitbox::BoundsEnabled": {
            "-1": "0"
        },
        "Vars::Visuals::Hitbox::DrawDuration": {
            "-1": "5"
        },
        "Vars::Visuals::Prediction::PlayerPath": {
            "-1": "1"
        },
        "Vars::Visuals::Prediction::ProjectilePath": {
            "-1": "1"
        },
        "Vars::Visuals::Prediction::SwingLines": {
            "-1": "false"
        },
        "Vars::Visuals::Prediction::PlayerDrawDuration": {
            "-1": "5"
        },
        "Vars::Visuals::Prediction::ProjectileDrawDuration": {
            "-1": "5"
        },
        "Vars::Visuals::Simulation::TrajectoryPath": {
            "-1": "1"
        },
        "Vars::Visuals::Simulation::ShotPath": {
            "-1": "1"
        },
        "Vars::Visuals::Simulation::SplashRadius": {
            "-1": "0"
        },
        "Vars::Visuals::Simulation::ProjectileCamera": {
            "-1": "false"
        },
        "Vars::Visuals::Simulation::ProjectileWindow": {
            "-1": {
                "x": "200",
                "y": "100",
                "w": "200",
                "h": "200"
            }
        },
        "Vars::Visuals::Simulation::Box": {
            "-1": "true"
        },
        "Vars::Misc::Movement::AutoStrafe": {
            "-1": "0"
        },
        "Vars::Misc::Movement::AutoStrafeTurnScale": {
            "-1": "0.5"
        },
        "Vars::Misc::Movement::AutoStrafeMaxDelta": {
            "-1": "180"
        },
        "Vars::Misc::Movement::Bunnyhop": {
            "-1": "true"
        },
        "Vars::Misc::Movement::EdgeJump": {
            "-1": "false"
        },
        "Vars::Misc::Movement::AutoJumpbug": {
            "-1": "false"
        },
        "Vars::Misc::Movement::BreakJump": {
            "-1": "false"
        },
        "Vars::Misc::Movement::AutoRocketJump": {
            "-1": "false"
        },
        "Vars::Misc::Movement::AutoCTap": {
            "-1": "false"
        },
        "Vars::Misc::Movement::AutoFaNJump": {
            "-1": "false"
        },
        "Vars::Misc::Movement::AutoRevJump": {
            "-1": "false"
        },
        "Vars::Misc::Movement::FastStop": {
            "-1": "true"
        },
        "Vars::Misc::Movement::FastAccelerate": {
            "-1": "false"
        },
        "Vars::Misc::Movement::DuckSpeed": {
            "-1": "false"
        },
        "Vars::Misc::Movement::ShieldTurnRate": {
            "-1": "false"
        },
        "Vars::Misc::Movement::NoPush": {
            "-1": "true"
        },
        "Vars::Misc::Movement::MovementLock": {
            "-1": "false"
        },
        "Vars::Misc::Automation::AntiBackstab": {
            "-1": "0"
        },
        "Vars::Misc::Automation::TauntControl": {
            "-1": "false"
        },
        "Vars::Misc::Automation::KartControl": {
            "-1": "false"
        },
        "Vars::Misc::Automation::AntiAutobalance": {
            "-1": "false"
        },
        "Vars::Misc::Automation::AntiAFK": {
            "-1": "true"
        },
        "Vars::Misc::Automation::AutoF2Ignored": {
            "-1": "true"
        },
        "Vars::Misc::Automation::AutoF1Priority": {
            "-1": "true"
        },
        "Vars::Misc::Automation::AcceptItemDrops": {
            "-1": "true"
        },
        "Vars::Misc::Exploits::PureBypass": {
            "-1": "true"
        },
        "Vars::Misc::Exploits::CheatsBypass": {
            "-1": "true"
        },
        "Vars::Misc::Exploits::UnlockCVars": {
            "-1": "true"
        },
        "Vars::Misc::Exploits::EquipRegionUnlock": {
            "-1": "true"
        },
        "Vars::Misc::Exploits::BackpackExpander": {
            "-1": "true"
        },
        "Vars::Misc::Exploits::NoisemakerSpam": {
            "-1": "false"
        },
        "Vars::Misc::Exploits::PingReducer": {
            "-1": "false"
        },
        "Vars::Misc::Exploits::PingTarget": {
            "-1": "1"
        },
        "Vars::Misc::Game::NetworkFix": {
            "-1": "false"
        },
        "Vars::Misc::Game::SetupBonesOptimization": {
            "-1": "false"
        },
        "Vars::Misc::Game::AntiCheatCompati)NIKODEFAULT"
R"NIKODEFAULT(bility": {
            "-1": "false"
        },
        "Vars::Misc::Queueing::ForceRegions": {
            "-1": "0"
        },
        "Vars::Misc::Queueing::ExtendQueue": {
            "-1": "false"
        },
        "Vars::Misc::Queueing::AutoCasualQueue": {
            "-1": "false"
        },
        "Vars::Misc::MannVsMachine::InstantRespawn": {
            "-1": "false"
        },
        "Vars::Misc::MannVsMachine::InstantRevive": {
            "-1": "false"
        },
        "Vars::Misc::MannVsMachine::AllowInspect": {
            "-1": "false"
        },
        "Vars::Misc::Sound::Block": {
            "-1": "0"
        },
        "Vars::Misc::Sound::HitsoundAlways": {
            "-1": "false"
        },
        "Vars::Misc::Sound::RemoveDSP": {
            "-1": "false"
        },
        "Vars::Misc::Sound::GiantWeaponSounds": {
            "-1": "false"
        },
        "Vars::Logging::Logs": {
            "-1": "255"
        },
        "Vars::Logging::NotificationPosition": {
            "-1": "0"
        },
        "Vars::Logging::NotificationTime": {
            "-1": "5"
        },
        "Vars::Logging::MaxNotifications": {
            "-1": "10"
        },
        "Vars::Logging::VoteStart::LogTo": {
            "-1": "57"
        },
        "Vars::Logging::VoteCast::LogTo": {
            "-1": "57"
        },
        "Vars::Logging::ClassChange::LogTo": {
            "-1": "56"
        },
        "Vars::Logging::Damage::LogTo": {
            "-1": "56"
        },
        "Vars::Logging::CheatDetection::LogTo": {
            "-1": "57"
        },
        "Vars::Logging::Tags::LogTo": {
            "-1": "57"
        },
        "Vars::Logging::Aliases::LogTo": {
            "-1": "57"
        },
        "Vars::Logging::Resolver::LogTo": {
            "-1": "57"
        },
        "Vars::CheatDetection::Methods": {
            "-1": "15"
        },
        "Vars::CheatDetection::DetectionsRequired": {
            "-1": "10"
        },
        "Vars::CheatDetection::MinChoking": {
            "-1": "20"
        },
        "Vars::CheatDetection::MinFlick": {
            "-1": "20"
        },
        "Vars::CheatDetection::MaxNoise": {
            "-1": "1"
        },
        "Vars::Debug::CrashLogging": {
            "-1": "true"
        }
    },
    "Groups": {
        "0": {
            "Name": "enemy red",
            "Color": {
                "r": "255",
                "g": "0",
                "b": "0",
                "a": "255"
            },
            "TagsOverrideColor": "true",
            "Targets": "7",
            "Conditions": "9",
            "Players": "0",
            "Buildings": "0",
            "Projectiles": "0",
            "ESP": "8384593",
            "Chams": {
                "Visible": [
                    {
                        "Material": "Original",
                        "Color": {
                            "r": "255",
                            "g": "255",
                            "b": "255",
                            "a": "255"
                        }
                    }
                ],
                "Occluded": ""
            },
            "Glow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "OffscreenArrows": "true",
            "OffscreenArrowsOffset": "100",
            "OffscreenArrowsMaxDistance": "1000",
            "PickupTimer": "false",
            "Backtrack": "3",
            "BacktrackChams": {
                "Visible": "",
                "Occluded": ""
            },
            "BacktrackGlow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "Trajectory": "0",
            "Sightlines": "3"
        },
        "1": {
            "Name": "enemy red dormant",
            "Color": {
                "r": "255",
                "g": "0",
                "b": "0",
                "a": "255"
            },
            "TagsOverrideColor": "true",
            "Targets": "7",
            "Conditions": "521",
            "Players": "0",
            "Buildings": "0",
            "Projectiles": "0",
            "ESP": "8384593",
            "Chams": {
                "Visible": [
                    {
                        "Material": "Original",
                        "Color": {
                            "r": "255",
                            "g": "255",
                            "b": "255",
                            "a": "255"
                        }
                    }
                ],
                "Occluded": ""
            },
            "Glow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "OffscreenArrows": "true",
            "OffscreenArrowsOffset": "100",
            "OffscreenArrowsMaxDistance": "1000",
            "PickupTimer": "false",
            "Backtrack": "3",
            "BacktrackChams": {
                "Visible": "",
                "Occluded": ""
            },
            "BacktrackGlow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "Trajectory": "0",
            "Sightlines": "3"
        },
        "2": {
            "Name": "enemy blu",
            "Color": {
                "r": "0",
                "g": "255",
                "b": "241",
                "a": "255"
            },
            "TagsOverrideColor": "true",
            "Targets": "7",
            "Conditions": "5",
            "Players": "0",
            "Buildings": "0",
            "Projectiles": "0",
            "ESP": "8384593",
            "Chams": {
                "Visible": [
                    {
                        "Material": "Original",
                        "Color": {
                            "r": "255",
                            "g": "255",
                            "b": "255",
                            "a": "255"
                        }
                    }
                ],
                "Occluded": ""
            },
            "Glow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "OffscreenArrows": "true",
            "OffscreenArrowsOffset": "100",
            "OffscreenArrowsMaxDistance": "1000",
            "PickupTimer": "false",
            "Backtrack": "3",
            "BacktrackChams": {
                "Visible": "",
                "Occluded": ""
            },
            "BacktrackGlow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "Trajectory": "0",
            "Sightlines": "3"
        },
        "3": {
            "Name": "enemy blu dormant",
            "Color": {
                "r": "0",
                "g": "255",
             )NIKODEFAULT"
R"NIKODEFAULT(   "b": "241",
                "a": "255"
            },
            "TagsOverrideColor": "true",
            "Targets": "7",
            "Conditions": "517",
            "Players": "0",
            "Buildings": "0",
            "Projectiles": "0",
            "ESP": "8384593",
            "Chams": {
                "Visible": [
                    {
                        "Material": "Original",
                        "Color": {
                            "r": "255",
                            "g": "255",
                            "b": "255",
                            "a": "255"
                        }
                    }
                ],
                "Occluded": ""
            },
            "Glow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "OffscreenArrows": "true",
            "OffscreenArrowsOffset": "100",
            "OffscreenArrowsMaxDistance": "1000",
            "PickupTimer": "false",
            "Backtrack": "3",
            "BacktrackChams": {
                "Visible": "",
                "Occluded": ""
            },
            "BacktrackGlow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "Trajectory": "0",
            "Sightlines": "3"
        },
        "4": {
            "Name": "team red",
            "Color": {
                "r": "255",
                "g": "0",
                "b": "0",
                "a": "255"
            },
            "TagsOverrideColor": "true",
            "Targets": "7",
            "Conditions": "10",
            "Players": "0",
            "Buildings": "0",
            "Projectiles": "0",
            "ESP": "7340113",
            "Chams": {
                "Visible": [
                    {
                        "Material": "Original",
                        "Color": {
                            "r": "255",
                            "g": "255",
                            "b": "255",
                            "a": "255"
                        }
                    }
                ],
                "Occluded": ""
            },
            "Glow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "OffscreenArrows": "false",
            "OffscreenArrowsOffset": "100",
            "OffscreenArrowsMaxDistance": "1000",
            "PickupTimer": "false",
            "Backtrack": "0",
            "BacktrackChams": {
                "Visible": "",
                "Occluded": ""
            },
            "BacktrackGlow": {
                "Stencil": "0",
                "Blur": "0"
            },
            "Trajectory": "0",
            "Sightlines": "2"
        },
        "5": {
            "Name": "team blu",
            "Color": {
                "r": "0",
                "g": "255",
                "b": "241",
                "a": "255"
            },
            "TagsOverrideColor": "true",
            "Targets": "7",
            "Conditions": "6",
            "Players": "0",
            "Buildings": "0",
            "Projectiles": "0",
            "ESP": "7340113",
            "Chams": {
                "Visible": [
                    {
                        "Material": "Original",
                        "Color": {
                            "r": "255",
                            "g": "255",
                            "b": "255",
                            "a": "255"
                        }
                    }
                ],
                "Occluded": ""
            },
            "Glow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "OffscreenArrows": "false",
            "OffscreenArrowsOffset": "100",
            "OffscreenArrowsMaxDistance": "1000",
            "PickupTimer": "false",
            "Backtrack": "0",
            "BacktrackChams": {
                "Visible": "",
                "Occluded": ""
            },
            "BacktrackGlow": {
                "Stencil": "0",
                "Blur": "0"
            },
            "Trajectory": "0",
            "Sightlines": "2"
        },
        "6": {
            "Name": "friend",
            "Color": {
                "r": "14",
                "g": "255",
                "b": "0",
                "a": "255"
            },
            "TagsOverrideColor": "true",
            "Targets": "7",
            "Conditions": "96",
            "Players": "0",
            "Buildings": "0",
            "Projectiles": "0",
            "ESP": "8384593",
            "Chams": {
                "Visible": [
                    {
                        "Material": "Original",
                        "Color": {
                            "r": "255",
                            "g": "255",
                            "b": "255",
                            "a": "255"
                        }
                    }
                ],
                "Occluded": ""
            },
            "Glow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "OffscreenArrows": "true",
            "OffscreenArrowsOffset": "100",
            "OffscreenArrowsMaxDistance": "1000",
            "PickupTimer": "false",
            "Backtrack": "3",
            "BacktrackChams": {
                "Visible": "",
                "Occluded": ""
            },
            "BacktrackGlow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "Trajectory": "0",
            "Sightlines": "3"
        },
        "7": {
            "Name": "friend dormant",
            "Color": {
                "r": "14",
                "g": "255",
                "b": "0",
                "a": "255"
            },
            "TagsOverrideColor": "true",
            "Targets": "7",
            "Conditions": "544",
            "Players": "0",
            "Buildings": "0",
            "Projectiles": "0",
            "ESP": "8384593",
            "Chams": {
                "Visible": [
                    {
                        "Material": "Original",
                        "Color": {
                            "r": "255",
                            "g": "255",
                            "b": "255",
                            "a": "255"
                        }
                    }
                ],
                "Occluded": ""
            },
            "Glow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "OffscreenArrows": "true",
            "OffscreenArrowsOffset": "100",
            "OffscreenArrowsMaxDistance": "1000",
            "PickupTimer": "false",
            "Backtrack": "3",
            "BacktrackChams": {
                "Visible)NIKODEFAULT"
R"NIKODEFAULT(": "",
                "Occluded": ""
            },
            "BacktrackGlow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "Trajectory": "0",
            "Sightlines": "3"
        },
        "8": {
            "Name": "local",
            "Color": {
                "r": "217",
                "g": "191",
                "b": "51",
                "a": "255"
            },
            "TagsOverrideColor": "false",
            "Targets": "49159",
            "Conditions": "16",
            "Players": "0",
            "Buildings": "0",
            "Projectiles": "0",
            "ESP": "7340113",
            "Chams": {
                "Visible": [
                    {
                        "Material": "Original",
                        "Color": {
                            "r": "255",
                            "g": "255",
                            "b": "255",
                            "a": "255"
                        }
                    }
                ],
                "Occluded": ""
            },
            "Glow": {
                "Stencil": "5",
                "Blur": "0"
            },
            "OffscreenArrows": "false",
            "OffscreenArrowsOffset": "100",
            "OffscreenArrowsMaxDistance": "1000",
            "PickupTimer": "false",
            "Backtrack": "0",
            "BacktrackChams": {
                "Visible": "",
                "Occluded": ""
            },
            "BacktrackGlow": {
                "Stencil": "0",
                "Blur": "0"
            },
            "Trajectory": "0",
            "Sightlines": "2"
        },
        "9": {
            "Name": "fake",
            "Color": {
                "r": "217",
                "g": "191",
                "b": "51",
                "a": "255"
            },
            "TagsOverrideColor": "false",
            "Targets": "8192",
            "Conditions": "0",
            "Players": "0",
            "Buildings": "0",
            "Projectiles": "0",
            "ESP": "0",
            "Chams": {
                "Visible": "",
                "Occluded": ""
            },
            "Glow": {
                "Stencil": "5",
                "Blur": "0"
            },
            "OffscreenArrows": "false",
            "OffscreenArrowsOffset": "100",
            "OffscreenArrowsMaxDistance": "1000",
            "PickupTimer": "false",
            "Backtrack": "0",
            "BacktrackChams": {
                "Visible": "",
                "Occluded": ""
            },
            "BacktrackGlow": {
                "Stencil": "0",
                "Blur": "0"
            },
            "Trajectory": "0",
            "Sightlines": "2"
        },
        "10": {
            "Name": "target",
            "Color": {
                "r": "220",
                "g": "0",
                "b": "255",
                "a": "255"
            },
            "TagsOverrideColor": "true",
            "Targets": "7",
            "Conditions": "256",
            "Players": "0",
            "Buildings": "0",
            "Projectiles": "0",
            "ESP": "8384593",
            "Chams": {
                "Visible": [
                    {
                        "Material": "Original",
                        "Color": {
                            "r": "255",
                            "g": "255",
                            "b": "255",
                            "a": "255"
                        }
                    }
                ],
                "Occluded": ""
            },
            "Glow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "OffscreenArrows": "true",
            "OffscreenArrowsOffset": "100",
            "OffscreenArrowsMaxDistance": "1000",
            "PickupTimer": "false",
            "Backtrack": "3",
            "BacktrackChams": {
                "Visible": "",
                "Occluded": ""
            },
            "BacktrackGlow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "Trajectory": "0",
            "Sightlines": "3"
        },
        "11": {
            "Name": "target dormant",
            "Color": {
                "r": "220",
                "g": "0",
                "b": "255",
                "a": "255"
            },
            "TagsOverrideColor": "true",
            "Targets": "7",
            "Conditions": "768",
            "Players": "0",
            "Buildings": "0",
            "Projectiles": "0",
            "ESP": "8384593",
            "Chams": {
                "Visible": [
                    {
                        "Material": "Original",
                        "Color": {
                            "r": "255",
                            "g": "255",
                            "b": "255",
                            "a": "255"
                        }
                    }
                ],
                "Occluded": ""
            },
            "Glow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "OffscreenArrows": "true",
            "OffscreenArrowsOffset": "100",
            "OffscreenArrowsMaxDistance": "1000",
            "PickupTimer": "false",
            "Backtrack": "3",
            "BacktrackChams": {
                "Visible": "",
                "Occluded": ""
            },
            "BacktrackGlow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "Trajectory": "0",
            "Sightlines": "3"
        },
        "12": {
            "Name": "priority",
            "Color": {
                "r": "255",
                "g": "158",
                "b": "0",
                "a": "255"
            },
            "TagsOverrideColor": "true",
            "Targets": "7",
            "Conditions": "128",
            "Players": "0",
            "Buildings": "0",
            "Projectiles": "0",
            "ESP": "8384593",
            "Chams": {
                "Visible": [
                    {
                        "Material": "Original",
                        "Color": {
                            "r": "255",
                            "g": "255",
                            "b": "255",
                            "a": "255"
                        }
                    }
                ],
                "Occluded": ""
            },
            "Glow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "OffscreenArrows": "true",
            "OffscreenArrowsOffset": "100",
            "OffscreenArrowsMaxDistance": "1000",
            "PickupTimer": "false",
            "Backtrack": "3",
       )NIKODEFAULT"
R"NIKODEFAULT(     "BacktrackChams": {
                "Visible": "",
                "Occluded": ""
            },
            "BacktrackGlow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "Trajectory": "0",
            "Sightlines": "3"
        },
        "13": {
            "Name": "priority dormant",
            "Color": {
                "r": "255",
                "g": "158",
                "b": "0",
                "a": "255"
            },
            "TagsOverrideColor": "true",
            "Targets": "7",
            "Conditions": "640",
            "Players": "0",
            "Buildings": "0",
            "Projectiles": "0",
            "ESP": "8384593",
            "Chams": {
                "Visible": [
                    {
                        "Material": "Original",
                        "Color": {
                            "r": "255",
                            "g": "255",
                            "b": "255",
                            "a": "255"
                        }
                    }
                ],
                "Occluded": ""
            },
            "Glow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "OffscreenArrows": "true",
            "OffscreenArrowsOffset": "100",
            "OffscreenArrowsMaxDistance": "1000",
            "PickupTimer": "false",
            "Backtrack": "3",
            "BacktrackChams": {
                "Visible": "",
                "Occluded": ""
            },
            "BacktrackGlow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "Trajectory": "0",
            "Sightlines": "3"
        },
        "14": {
            "Name": "ammunition",
            "Color": {
                "r": "130",
                "g": "87",
                "b": "50",
                "a": "255"
            },
            "TagsOverrideColor": "true",
            "Targets": "128",
            "Conditions": "0",
            "Players": "0",
            "Buildings": "0",
            "Projectiles": "0",
            "ESP": "0",
            "Chams": {
                "Visible": [
                    {
                        "Material": "Original",
                        "Color": {
                            "r": "255",
                            "g": "255",
                            "b": "255",
                            "a": "255"
                        }
                    }
                ],
                "Occluded": [
                    {
                        "Material": "Shaded",
                        "Color": {
                            "r": "130",
                            "g": "87",
                            "b": "50",
                            "a": "255"
                        }
                    }
                ]
            },
            "Glow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "OffscreenArrows": "false",
            "OffscreenArrowsOffset": "100",
            "OffscreenArrowsMaxDistance": "1000",
            "PickupTimer": "true",
            "Backtrack": "0",
            "BacktrackChams": {
                "Visible": "",
                "Occluded": ""
            },
            "BacktrackGlow": {
                "Stencil": "0",
                "Blur": "0"
            },
            "Trajectory": "0",
            "Sightlines": "2"
        },
        "15": {
            "Name": "health",
            "Color": {
                "r": "0",
                "g": "255",
                "b": "0",
                "a": "255"
            },
            "TagsOverrideColor": "true",
            "Targets": "64",
            "Conditions": "0",
            "Players": "0",
            "Buildings": "0",
            "Projectiles": "0",
            "ESP": "0",
            "Chams": {
                "Visible": [
                    {
                        "Material": "Original",
                        "Color": {
                            "r": "255",
                            "g": "255",
                            "b": "255",
                            "a": "255"
                        }
                    }
                ],
                "Occluded": [
                    {
                        "Material": "Shaded",
                        "Color": {
                            "r": "0",
                            "g": "255",
                            "b": "0",
                            "a": "255"
                        }
                    }
                ]
            },
            "Glow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "OffscreenArrows": "false",
            "OffscreenArrowsOffset": "100",
            "OffscreenArrowsMaxDistance": "1000",
            "PickupTimer": "true",
            "Backtrack": "0",
            "BacktrackChams": {
                "Visible": "",
                "Occluded": ""
            },
            "BacktrackGlow": {
                "Stencil": "0",
                "Blur": "0"
            },
            "Trajectory": "0",
            "Sightlines": "2"
        },
        "16": {
            "Name": "intelligence",
            "Color": {
                "r": "91",
                "g": "0",
                "b": "255",
                "a": "255"
            },
            "TagsOverrideColor": "true",
            "Targets": "16",
            "Conditions": "0",
            "Players": "0",
            "Buildings": "0",
            "Projectiles": "0",
            "ESP": "16842752",
            "Chams": {
                "Visible": [
                    {
                        "Material": "Original",
                        "Color": {
                            "r": "255",
                            "g": "255",
                            "b": "255",
                            "a": "255"
                        }
                    }
                ],
                "Occluded": [
                    {
                        "Material": "Shaded",
                        "Color": {
                            "r": "91",
                            "g": "0",
                            "b": "255",
                            "a": "255"
                        }
                    }
                ]
            },
            "Glow": {
                "Stencil": "1",
                "Blur": "0"
            },
            "OffscreenArrows": "false",
            "OffscreenArrowsOffset": "100",
            "OffscreenArrowsMaxDistance": "1000",
            "PickupTimer": "true",
            "Backtrack": "0",
            "BacktrackChams": {
                "Visible": "",
                "Occluded": ""
)NIKODEFAULT"
R"NIKODEFAULT(            },
            "BacktrackGlow": {
                "Stencil": "0",
                "Blur": "0"
            },
            "Trajectory": "0",
            "Sightlines": "2"
        }
    }
})NIKODEFAULT";

inline bool Write(const std::string& path, bool overwrite = false)
{
    const std::string temporary = path + ".default.tmp";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream.write(Json, sizeof(Json) - 1);
    stream.close();
    if (!stream) return false;
    return MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0)) != 0;
}
}
