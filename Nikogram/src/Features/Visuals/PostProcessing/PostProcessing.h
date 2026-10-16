#pragma once
#include "../../../SDK/SDK.h"
#include "../CapturePolicy.h"
#include "../CameraWindow/CameraWindow.h"
#include "Renderer.h"

namespace PostFX
{
inline thread_local const CViewSetup* currentView=nullptr;
class ViewScope
{
    const CViewSetup* saved=currentView;
public:
    explicit ViewScope(const CViewSetup& v){currentView=saved?nullptr:&v;}
    ~ViewScope(){currentView=saved;}
};
inline Options ReadOptions()
{
    namespace V=Vars::Visuals::PostProcessing;
    Options o;
#define FX_READ(member,variable) { \
    if constexpr(std::is_same_v<decltype(o.member),float>) \
        o.member=std::isfinite(float(V::variable.Value))?std::clamp(float(V::variable.Value),V::variable.m_unMin.f,V::variable.m_unMax.f):float(V::variable.Default); \
    else o.member=V::variable.Value; }
    FX_READ(enabled,Enabled);FX_READ(bloom,Bloom);FX_READ(motion,MotionBlur);FX_READ(aa,AntiAliasing);FX_READ(dof,DepthOfField);
    FX_READ(grade,ColourGrading);FX_READ(sharpen,Sharpening);FX_READ(exposure,Exposure);
    FX_READ(vignette,Vignette);FX_READ(poster,Posterisation);FX_READ(halftone,Halftone);FX_READ(comic,ComicOutlines);
    FX_READ(grain,FilmGrain);FX_READ(dirt,LensDirt);FX_READ(chromatic,ChromaticAberration);FX_READ(letterbox,Letterboxing);
    FX_READ(bloomStrength,BloomStrength);FX_READ(bloomRadius,BloomRadius);FX_READ(bloomThreshold,BloomThreshold);
    FX_READ(turnBlur,TurnBlur);FX_READ(forwardBlur,ForwardBlur);FX_READ(aaStrength,AAStrength);
    FX_READ(focus,FocusDistance);FX_READ(focusRange,FocusRange);FX_READ(dofRadius,BlurRadius);FX_READ(autofocus,Autofocus);
    FX_READ(saturation,Saturation);FX_READ(contrast,Contrast);FX_READ(warmth,Warmth);FX_READ(tint,Tint);
    FX_READ(shadows,Shadows);FX_READ(highlights,Highlights);FX_READ(gradeMix,GradeMix);FX_READ(preset,Preset);FX_READ(presetMix,PresetMix);
    FX_READ(sharpness,Sharpness);FX_READ(ev,ExposureEV);FX_READ(compression,HighlightCompression);
    FX_READ(vignetteStrength,VignetteStrength);FX_READ(posterLevels,PosterLevels);FX_READ(dotSize,DotSize);FX_READ(dotStrength,DotStrength);
    FX_READ(inkStrength,InkStrength);FX_READ(grainStrength,GrainStrength);FX_READ(dirtStrength,DirtStrength);FX_READ(chromaticStrength,ChromaticStrength);FX_READ(bars,BarSize);
#undef FX_READ
    return o;
}
// Minimal ABI surface of Valve's public tier1 CFunctor/ICallQueue interface.
// Queue ownership is one reference. A pending command also keeps hook teardown
// from detaching this DLL before the render worker invokes/releases it.
class Command : public IRefCounted
{
public:
    virtual ~Command()=default;
    virtual void operator()()=0;
    unsigned userID=0;
};
class EffectCommand final:public Command
{
    std::atomic<int> refs=1;
    HookLifetime::Scope lifetime;
    Options options;View view;
public:
    EffectCommand(Options o,View v):options(o),view(v){}
    int AddRef()override{return ++refs;}
    int Release()override{const int n=--refs;if(!n)delete this;return n;}
    void operator()()override{if(!G::Unload)renderer.Render(I::DirectXDevice,options,view);}
};
inline void Apply(int x,int y,int w,int h,bool postVGui)
{
    const auto o=ReadOptions();
    if(!o.Any()){status=Status::Off;return;}
    if(postVGui||G::Unload||SDK::CleanScreenshot()||F::CameraWindow.m_bDrawing||!currentView||!I::EngineClient->IsInGame())return;
    // Do not stretch a subview, reflection, monitor or auxiliary camera across
    // the main image. The scene view is captured before native rendering.
    if(x||y||w!=currentView->width||h!=currentView->height||currentView->x||currentView->y||currentView->m_bOrtho)return;
    View v;v.pitch=currentView->angles.x;v.yaw=currentView->angles.y;
    v.x=currentView->origin.x;v.y=currentView->origin.y;v.z=currentView->origin.z;
    v.nearPlane=currentView->zNear;v.farPlane=currentView->zFar;v.fov=currentView->fov;v.time=SDK::PlatFloatTime();
    auto context=I::MaterialSystem->GetRenderContext();if(!context)return;
    auto target=context->GetRenderTarget();
    if(target){context->Release();return;} // main resolved backbuffer only
    if(auto queue=context->GetCallQueue())
    {
        auto command=new EffectCommand(o,v);
        // QueueFunctorInternal takes ownership of the initial reference, as
        // Valve's QueueCall does. Do not touch command after this call.
        using Submit=void(__fastcall*)(ICallQueue*,Command*);
        reinterpret_cast<Submit>(U::Memory.GetVirtual(queue,0))(queue,command);
    }
    else renderer.Render(I::DirectXDevice,o,v);
    context->Release();
}
}
