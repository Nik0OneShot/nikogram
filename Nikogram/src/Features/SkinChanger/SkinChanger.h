#pragma once
#include "../../SDK/SDK.h"
#include "Model.h"
#include "RenderPolicy.h"

namespace SkinChanger
{
    void Tick();
    void Menu();
    void Shutdown();
    bool PrepareUnload();
    void ServiceUnload();
    void RestoreHudPreview();
    void UnloadDiagnostic(const char* stage);
    void SessionEvent(IGameEvent* event);
    struct HandAnimationProfile {int item=0,reskin=0;std::map<std::string,std::string> replacements;};
    std::optional<HandAnimationProfile> HandAnimationsFor(CTFWeaponBase* weapon);
    std::optional<HandAnimationProfile> PlayerAnimationsFor(CTFPlayer* player);
    bool PreparePlayerBones(CTFPlayer* player,std::array<matrix3x4,MAXSTUDIOBONES>& output);
    bool AuthenticAnimationsFor(CTFPlayer* player);
    bool PipBoyFor(CTFPlayer* player);
    struct HudAppearance
    {
        int item=0,reskin=0,cls=0,team=0,skin=0;uint32_t weapon=0;
        SkinModel::Selection selection;std::string model,originalExtra;
        std::vector<SkinModel::Attachment> extras;
        bool operator==(const HudAppearance&)const=default;
    };
    std::optional<HudAppearance> HudAppearanceFor();
    const model_t* HudPaintModel(const HudAppearance& appearance);
    bool BuildingMenuPipBoy(void* player);
    std::optional<float> CosmeticVoiceAttribute(const char* name,void* weapon);
    std::string KillIconFor(IGameEvent* event);
    bool PreparePipBoyDraw(const DrawModelState_t& state,const ModelRenderInfo_t& info,matrix3x4* bones,
        DrawModelState_t& outputState,ModelRenderInfo_t& outputInfo,std::array<matrix3x4,MAXSTUDIOBONES>& outputBones,bool localViewmodelDraw=false);
    void ObserveHandActivity(int item,int reskin,int input,int output,const char* result,int sourceSequence=-1,int targetSequence=-1,int sequenceCount=-1);
    std::optional<int> RenderHandSequence(CBaseAnimating* viewmodel);
    std::optional<float> RenderHandCycle(CBaseAnimating* viewmodel,int sequence);
    bool PlaySapperVoice(int entity,const char* name);
    SkinModel::CosmeticEffects EffectsFor(CBaseCombatWeapon* weapon);
    std::string UnusualFor(CBaseCombatWeapon* weapon);
    void UpdateUnusualParticles(bool clear=false);
    struct KillstreakProfile
    {
        CTFWeaponBase* weapon=nullptr;
        int tier=0,sheen=1,effect=2002,count=0;bool preview=false;
    };
    std::optional<KillstreakProfile> KillstreakFor(CTFWeaponBase* weapon,bool allowPreview=true);
    std::optional<int> LocalKillstreakDisplay();
    std::optional<int> PlayerKillstreakDisplay(int playerIndex);
    void ObserveKillstreakUI(const char* surface,int count,bool remote=false);
    void SharedKillstreakMilestone(int playerIndex,int count);
    bool PlayKillstreakMilestoneSound();
    std::optional<KillstreakProfile> CurrentDrawKillstreak();
    std::optional<int> KillstreakAttribute(const char* name,void* entity);
    class KillstreakAttributeScope
    {
        std::optional<KillstreakProfile> profile;const KillstreakProfile* previous=nullptr;
    public:
        explicit KillstreakAttributeScope(std::optional<KillstreakProfile> value);
        ~KillstreakAttributeScope();
        KillstreakAttributeScope(const KillstreakAttributeScope&)=delete;
        KillstreakAttributeScope& operator=(const KillstreakAttributeScope&)=delete;
    };
    void UpdateKillstreakParticles(bool clear=false);
    void KillstreakEvent(IGameEvent* event);
    bool KillstreakKillEligible(IGameEvent* event);
    int RagdollEffectsFor(CTFRagdoll* ragdoll);
    void RagdollCreated(CTFRagdoll* ragdoll,int effects);
    bool PlayCosmeticDeathSound(int entity,int effects);
    std::optional<bool> BeginDeathSound(int entity);
    std::optional<Vec3> DeathSoundOrigin(int entity);
    void ObserveDeathSound(int entity,bool started,int guid,const char* source);
    bool PrepareDeathDraw(const DrawModelState_t& state,const ModelRenderInfo_t& info,matrix3x4* bones,
        std::array<matrix3x4,MAXSTUDIOBONES>& output,matrix3x4*& drawBones,IMaterial*& material,bool effectPass);
    // Scoped surface-texture substitution; never paints hands or attachments.
    class PaintDrawScope
    {
        using Binding=SkinRender::SurfaceTextureBinding<IMaterialVar,ITexture>;
        std::vector<Binding> bindings;
        ITexture* painted=nullptr;
        PaintDrawScope* previous=nullptr;
        std::optional<KillstreakProfile> killstreak;
        IMaterial* forced=nullptr;
        IMaterial* oldForced=nullptr;
        OverrideType_t oldOverride=OVERRIDE_NORMAL;
        void Suspend();
    public:
        PaintDrawScope(const ModelRenderInfo_t& info,bool localViewmodelDraw);
        ~PaintDrawScope();
        PaintDrawScope(const PaintDrawScope&)=delete;
        PaintDrawScope& operator=(const PaintDrawScope&)=delete;
        void Apply();
        std::optional<KillstreakProfile> Killstreak()const{return killstreak;}
    };
    void ApplyPaintDraw();
    std::string SoundFor(CBaseCombatWeapon* weapon,int category);
    std::string RemoteSoundFor(int entity,const char* original,bool wave=false);
    std::string MuzzleFor(const void* particleProperty,const char* original);
    void ObserveCosmeticEffect(const char* kind,const std::string& name);
    void ObserveParticleCreation(const char* kind,const char* original,const std::string& requested,bool created);
    void ObserveParticleLookup(const char* name,int index);
    bool CreateCosmeticTracer(const std::string& name,const Vector& start,const Vector& end,int entity,int attachment);
    void ObserveDraw(const ModelRenderInfo_t& info,matrix3x4* bones,const char* route);
    void ObserveViewmodelDraw(const void* drawing,bool verified);
    // Geometry-only draw under the caller's existing glow/chams material.
    // Ready-only preparation never loads models or computes new bone poses.
    void DrawEffectGeometry(const DrawModelState_t& state,const ModelRenderInfo_t& info,matrix3x4* bones,bool localViewmodel,const char* route);
    bool Prepare(const DrawModelState_t& state,const ModelRenderInfo_t& info,matrix3x4* bones,
        DrawModelState_t& replacementState,ModelRenderInfo_t& replacementInfo,std::array<matrix3x4,MAXSTUDIOBONES>& replacementBones,
        matrix3x4*& drawBones,bool localViewmodelDraw=false,bool accessory=false,bool effectPass=false,int accessoryIndex=0,int* accessoryCount=nullptr);
}
