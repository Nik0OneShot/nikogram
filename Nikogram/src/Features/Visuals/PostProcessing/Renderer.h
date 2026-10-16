#pragma once
#include <Windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include "Shader.h"

namespace PostFX
{
template<class T> inline void Release(T*& p){if(p){p->Release();p=nullptr;}}
struct Options
{
    bool enabled=false,bloom=false,motion=false,aa=false,dof=false,grade=false,sharpen=false,exposure=false;
    bool vignette=false,poster=false,halftone=false,comic=false,grain=false,dirt=false,chromatic=false,letterbox=false;
    float bloomStrength=.25f,bloomRadius=2,bloomThreshold=.7f,aaStrength=1,sharpness=.5f;
    float saturation=1,contrast=1,warmth=0,tint=0,shadows=0,highlights=0,gradeMix=1,presetMix=.5f;
    int preset=0;
    float ev=0,compression=.2f,turnBlur=.5f,forwardBlur=.3f;
    float focus=500,focusRange=300,dofRadius=6;bool autofocus=false;
    float vignetteStrength=.3f,grainStrength=.04f,dirtStrength=.4f,chromaticStrength=.2f,bars=.1f;
    float posterLevels=8,dotSize=5,dotStrength=.7f,inkStrength=.8f;
    bool Any()const{return enabled&&(bloom||motion||aa||dof||grade||sharpen||exposure||vignette||poster||halftone||comic||grain||dirt||chromatic||letterbox);}
};
struct View
{
    float pitch=0,yaw=0,x=0,y=0,z=0,nearPlane=1,farPlane=30000,fov=90;
    double time=0;
};
enum class Status { Off, Ready, NoCompiler, ShaderFailed, ResourceFailed, UnsupportedTarget, DrawFailed };
inline std::atomic<Status> status=Status::Off;
inline std::atomic<bool> depthAvailable=false;

// D3DSBT_ALL does not save render/depth targets. Preserve those explicitly,
// including MRT slots and viewport, before any raw draw bypasses Source's cache.
class StateScope
{
    IDirect3DDevice9* device;
    IDirect3DStateBlock9* block=nullptr;
    std::array<IDirect3DSurface9*,4> targets{};
    IDirect3DSurface9* depth=nullptr;
    D3DVIEWPORT9 viewport{};
    DWORD count=1;
public:
    bool valid=false;
    explicit StateScope(IDirect3DDevice9* d):device(d)
    {
        D3DCAPS9 caps{};if(FAILED(d->GetDeviceCaps(&caps)))return;count=std::min<DWORD>(4,caps.NumSimultaneousRTs);
        if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&block))||FAILED(block->Capture())||FAILED(d->GetViewport(&viewport)))return;
        for(DWORD i=0;i<count;i++)d->GetRenderTarget(i,&targets[i]);
        d->GetDepthStencilSurface(&depth);valid=targets[0]!=nullptr;
    }
    ~StateScope()
    {
        if(valid)
        {
            device->SetDepthStencilSurface(nullptr);
            for(DWORD i=1;i<count;i++)device->SetRenderTarget(i,nullptr);
            device->SetRenderTarget(0,targets[0]);
            for(DWORD i=1;i<count;i++)device->SetRenderTarget(i,targets[i]);
            device->SetDepthStencilSurface(depth);block->Apply();device->SetViewport(&viewport);
        }
        for(auto& t:targets)Release(t);Release(depth);Release(block);
    }
};
class Renderer
{
    enum Pass { Copy,Threshold,Blur,Bloom,AA,Sharpen,Grade,Exposure,Motion,DOF,Style,Lens,Count };
    std::array<IDirect3DPixelShader9*,Count> shaders{};
    std::array<IDirect3DTexture9*,4> textures{};
    std::array<IDirect3DSurface9*,4> surfaces{};
    IDirect3DTexture9* depthTexture=nullptr;
    IDirect3DDevice9* owner=nullptr; // borrowed; do not keep the game alive
    unsigned width=0,height=0;D3DFORMAT format=D3DFMT_UNKNOWN;
    bool depthCapable=false,attempted=false;
    View previous{};bool havePrevious=false;
    std::mutex mutex;
    std::array<std::array<float,4>,12> constants{};
    bool CreateShaders(IDirect3DDevice9* device)
    {
        auto library=LoadLibraryExW(L"d3dcompiler_47.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!library){status=Status::NoCompiler;return false;}
        auto compile=reinterpret_cast<pD3DCompile>(GetProcAddress(library,"D3DCompile"));
        const char* names[]={"copy","threshold","blur","bloom","aa","sharpen","grade","exposure","motion","dof","style","lens"};
        bool ok=compile!=nullptr;
        for(int i=0;ok&&i<Count;i++)
        {
            ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;
            ok=SUCCEEDED(compile(ShaderSource,sizeof(ShaderSource)-1,"NikogramPostFX",nullptr,nullptr,names[i],"ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors));
            if(ok)ok=SUCCEEDED(device->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&shaders[i]));
            Release(code);Release(errors);
        }
        FreeLibrary(library);if(!ok)status=Status::ShaderFailed;return ok;
    }
    void FreeResources()
    {
        for(auto& s:surfaces)Release(s);for(auto& t:textures)Release(t);Release(depthTexture);
        width=height=0;havePrevious=false;depthAvailable=false;
    }
    bool Resources(IDirect3DDevice9* device,const D3DSURFACE_DESC& desc)
    {
        if(width==desc.Width&&height==desc.Height&&format==desc.Format&&textures[0])return true;
        FreeResources();format=desc.Format;
        for(int i=0;i<4;i++)
            if(FAILED(device->CreateTexture(i<2?desc.Width:std::max(1u,desc.Width/4),i<2?desc.Height:std::max(1u,desc.Height/4),1,D3DUSAGE_RENDERTARGET,desc.Format,D3DPOOL_DEFAULT,&textures[i],nullptr))||FAILED(textures[i]->GetSurfaceLevel(0,&surfaces[i])))
            {FreeResources();status=Status::ResourceFailed;return false;}
        width=desc.Width;height=desc.Height;return true;
    }
    bool ResolveDepth(IDirect3DDevice9* device)
    {
        if(!depthCapable)return false;
        IDirect3DSurface9* ds=nullptr;D3DSURFACE_DESC d{};
        if(FAILED(device->GetDepthStencilSurface(&ds)))return false;
        bool valid=ds&&SUCCEEDED(ds->GetDesc(&d))&&d.Width==width&&d.Height==height;Release(ds);
        if(!valid)return false;
        constexpr auto intz=static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z'));
        if(!depthTexture&&FAILED(device->CreateTexture(width,height,1,D3DUSAGE_DEPTHSTENCIL,intz,D3DPOOL_DEFAULT,&depthTexture,nullptr)))return false;
        // Vendor RESZ extension resolves the current native depth without replacing it.
        if(FAILED(device->SetTexture(0,depthTexture)))return false;
        // AMD's RESZ contract uses a magic *point-size value*, not the format
        // FOURCC. A dummy draw commits the texture binding before the resolve.
        device->SetVertexShader(nullptr);device->SetPixelShader(nullptr);
        device->SetFVF(D3DFVF_XYZ);device->SetRenderState(D3DRS_ZENABLE,FALSE);device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
        device->SetRenderState(D3DRS_STENCILENABLE,FALSE);
        device->SetRenderState(D3DRS_COLORWRITEENABLE,0);device->SetRenderState(D3DRS_COLORWRITEENABLE1,0);
        device->SetRenderState(D3DRS_COLORWRITEENABLE2,0);device->SetRenderState(D3DRS_COLORWRITEENABLE3,0);
        const float point[3]={0,0,0};
        auto hr=device->DrawPrimitiveUP(D3DPT_POINTLIST,1,point,sizeof(point));
        if(SUCCEEDED(hr))hr=device->SetRenderState(D3DRS_POINTSIZE,0x7fa05000);
        device->SetRenderState(D3DRS_POINTSIZE,0);device->SetTexture(0,nullptr);
        return SUCCEEDED(hr);
    }
    bool Draw(IDirect3DDevice9* device,Pass pass,IDirect3DSurface9* target,IDirect3DTexture9* input,IDirect3DTexture9* auxiliary=nullptr)
    {
        struct Vertex {float x,y,z,rhw,u,v;};
        D3DSURFACE_DESC desc{};if(FAILED(target->GetDesc(&desc)))return false;
        Vertex quad[]={{-.5f,-.5f,0,1,0,0},{float(desc.Width)-.5f,-.5f,0,1,1,0},{-.5f,float(desc.Height)-.5f,0,1,0,1},{float(desc.Width)-.5f,float(desc.Height)-.5f,0,1,1,1}};
        // Unbind before changing targets: D3D9 forbids read/write feedback.
        for(int i=0;i<3;i++)device->SetTexture(i,nullptr);
        if(FAILED(device->SetRenderTarget(0,target)))return false;
        D3DVIEWPORT9 viewport{0,0,desc.Width,desc.Height,0,1};device->SetViewport(&viewport);
        device->SetTexture(0,input);device->SetTexture(1,auxiliary);device->SetTexture(2,depthTexture);
        device->SetPixelShader(shaders[pass]);device->SetPixelShaderConstantF(0,constants[0].data(),12);
        return SUCCEEDED(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));
    }
public:
    void Reset()
    {
        std::lock_guard lock(mutex);FreeResources();for(auto& s:shaders)Release(s);owner=nullptr;attempted=false;status=Status::Off;
    }
    void Render(IDirect3DDevice9* device,const Options& o,const View& view,IDirect3DTexture9* testDepth=nullptr)
    {
        std::lock_guard lock(mutex);
        if(!o.Any()||!device){havePrevious=false;status=Status::Off;return;}
        if(owner!=device){FreeResources();for(auto& s:shaders)Release(s);owner=device;attempted=false;}
        if(!attempted)
        {
            attempted=true;if(!CreateShaders(device))return;
            IDirect3D9* api=nullptr;D3DDEVICE_CREATION_PARAMETERS creation{};D3DDISPLAYMODE mode{};
            depthCapable=false;
            if(SUCCEEDED(device->GetDirect3D(&api))&&SUCCEEDED(device->GetCreationParameters(&creation))&&SUCCEEDED(api->GetAdapterDisplayMode(creation.AdapterOrdinal,&mode)))
                depthCapable=SUCCEEDED(api->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,mode.Format,D3DUSAGE_DEPTHSTENCIL,D3DRTYPE_TEXTURE,static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z'))))&&
                  SUCCEEDED(api->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,mode.Format,D3DUSAGE_RENDERTARGET,D3DRTYPE_SURFACE,static_cast<D3DFORMAT>(MAKEFOURCC('R','E','S','Z'))));
            Release(api);
        }
        if(!shaders[Lens])return;
        IDirect3DSurface9* target=nullptr;D3DSURFACE_DESC desc{};
        if(FAILED(device->GetRenderTarget(0,&target)))return;
        const bool targetOK=target&&SUCCEEDED(target->GetDesc(&desc))&&desc.Width>0&&desc.Height>0;
        if(!targetOK){Release(target);status=Status::UnsupportedTarget;return;}
        StateScope state(device);if(!state.valid){Release(target);return;}
        if(!Resources(device,desc)){Release(target);return;}
        bool depth=o.dof&&(testDepth||ResolveDepth(device));depthAvailable=depth;
        // Fixture depth is supplied only by the standalone GPU test; production
        // always samples a resolved native depth buffer.
        IDirect3DTexture9* savedDepth=depthTexture;if(testDepth)depthTexture=testDepth;
        device->SetTexture(0,nullptr);
        if(FAILED(device->StretchRect(target,nullptr,surfaces[0],nullptr,D3DTEXF_NONE)))
        {depthTexture=savedDepth;Release(target);status=Status::UnsupportedTarget;return;}
        D3DCAPS9 caps{};device->GetDeviceCaps(&caps);
        for(DWORD i=1;i<std::min<DWORD>(4,caps.NumSimultaneousRTs);i++)device->SetRenderTarget(i,nullptr);
        device->SetDepthStencilSurface(nullptr);
        device->SetVertexShader(nullptr);device->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
        device->SetRenderState(D3DRS_ZENABLE,FALSE);device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
        device->SetRenderState(D3DRS_STENCILENABLE,FALSE);device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
        device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
        device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
        device->SetRenderState(D3DRS_FOGENABLE,FALSE);device->SetRenderState(D3DRS_LIGHTING,FALSE);
        device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);device->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        for(int i=0;i<3;i++)
        {
            device->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);device->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
            device->SetSamplerState(i,D3DSAMP_MINFILTER,i==2?D3DTEXF_POINT:D3DTEXF_LINEAR);device->SetSamplerState(i,D3DSAMP_MAGFILTER,i==2?D3DTEXF_POINT:D3DTEXF_LINEAR);
            device->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);device->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE);
        }
        constants={};constants[0]={1.f/width,1.f/height,float(view.time),0};
        constants[1]={0,0,o.bloomThreshold,o.bloomStrength};constants[2]={o.aaStrength,o.sharpness,o.ev,o.compression};
        constants[3]={o.saturation,o.contrast,o.warmth,o.tint};constants[4]={o.shadows,o.highlights,o.presetMix,float(o.preset)};
        constants[5]={o.gradeMix,o.chromatic?o.chromaticStrength:0,o.vignette?o.vignetteStrength:0,o.grain?o.grainStrength:0};
        if(havePrevious&&view.time>previous.time&&view.time-previous.time<.25)
        {
            const float dt=float(view.time-previous.time), shutter=std::min(4.f,(1.f/60)/dt);
            const float yaw=std::remainder(view.yaw-previous.yaw,360.f),pitch=std::remainder(view.pitch-previous.pitch,360.f);
            const float r=view.yaw*.0174532925f;
            const float forward=((view.x-previous.x)*cosf(r)+(view.y-previous.y)*sinf(r));
            if(std::abs(forward)<128&&std::abs(yaw)<60&&std::abs(pitch)<60)
                constants[6]={yaw/std::max(20.f,view.fov)*o.turnBlur*shutter,pitch/std::max(20.f,view.fov)*o.turnBlur*shutter,forward*.001f*o.forwardBlur*shutter,0};
        }
        previous=view;havePrevious=true;
        constants[7]={std::max(.01f,view.nearPlane),std::max(view.nearPlane+1,view.farPlane),o.focus,o.autofocus?1.f:0.f};
        constants[8]={o.focusRange,o.dofRadius,0,0};constants[9]={0,o.poster?1.f:0.f,std::max(2.f,o.posterLevels),o.halftone?1.f:0.f};
        constants[10]={o.dotSize,o.dotStrength,o.comic?1.f:0.f,o.inkStrength};constants[11]={o.dirt?o.dirtStrength:0,o.letterbox?o.bars:0,0,0};
        bool ok=true;int current=0;
        auto pass=[&](Pass p,IDirect3DTexture9* aux=nullptr){if(ok){ok=Draw(device,p,surfaces[1-current],textures[current],aux);if(ok)current=1-current;}};
        if(o.aa)pass(AA);
        if(o.dof&&depth)pass(DOF);
        if(o.motion)pass(Motion);
        if(o.bloom||o.dirt)
        {
            ok=ok&&Draw(device,Threshold,surfaces[2],textures[current]);
            constants[0][0]=1.f/std::max(1u,width/4);constants[0][1]=1.f/std::max(1u,height/4);
            constants[1][0]=o.bloomRadius;ok=ok&&Draw(device,Blur,surfaces[3],textures[2]);
            constants[1][0]=0;constants[1][1]=o.bloomRadius;ok=ok&&Draw(device,Blur,surfaces[2],textures[3]);
            constants[0][0]=1.f/width;constants[0][1]=1.f/height;
            if(o.bloom)pass(Bloom,textures[2]);
        }
        if(o.exposure)pass(Exposure);if(o.grade)pass(Grade);if(o.sharpen)pass(Sharpen);
        if(o.poster||o.halftone||o.comic)pass(Style);
        if(o.vignette||o.grain||o.dirt||o.chromatic||o.letterbox)pass(Lens,textures[2]);
        // Commit only after every pass succeeded. A failure leaves native colour intact.
        if(ok)ok=Draw(device,Copy,target,textures[current]);
        for(int i=0;i<3;i++)device->SetTexture(i,nullptr);
        depthTexture=savedDepth;Release(target);status=ok?Status::Ready:Status::DrawFailed;
    }
};
inline Renderer renderer;
inline const char* StatusText()
{
    switch(status.load())
    {
    case Status::Off:return "Post processing is bypassed.";
    case Status::Ready:return "Post-processing renderer active.";
    case Status::NoCompiler:return "Windows shader compiler unavailable. Effects bypassed.";
    case Status::ShaderFailed:return "Shader compilation failed. Effects bypassed.";
    case Status::ResourceFailed:return "Effect render targets unavailable. Effects bypassed.";
    case Status::UnsupportedTarget:return "Unsupported scene target. Effects bypassed.";
    default:return "Effect draw failed. Native image preserved.";
    }
}
}
