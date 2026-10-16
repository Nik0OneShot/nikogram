#pragma once
#include <d3d9.h>
#include <array>
#include <algorithm>

namespace DeviceStatePolicy
{
    // Capture the incoming game state around our draw and the forwarded Present
    // chain. State blocks exclude render/depth targets; keep those references too.
    // Nothing is retained across frames or Reset, and failure only releases refs.
    class PresentBoundary
    {
        IDirect3DDevice9* device;
        IDirect3DStateBlock9* block=nullptr;
        std::array<IDirect3DSurface9*,4> targets{};
        IDirect3DSurface9* depth=nullptr;
        D3DVIEWPORT9 viewport{};
        std::array<D3DMATRIX,3> transforms{};
        static constexpr D3DTRANSFORMSTATETYPE transformKinds[]{D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION};
        DWORD count=0;
        bool valid=false,attempted=false,vertexTextures=false;
        template<class T> static void Release(T*& p){if(p){p->Release();p=nullptr;}}
    public:
        explicit PresentBoundary(IDirect3DDevice9* d):device(d)
        {
            if(!d||d->TestCooperativeLevel()!=D3D_OK)return;
            D3DCAPS9 caps{};
            if(FAILED(d->GetDeviceCaps(&caps)))return;
            count=std::clamp<DWORD>(caps.NumSimultaneousRTs,1,4);
            vertexTextures=caps.VertexShaderVersion>=D3DVS_VERSION(3,0);
            if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&block))||!block||FAILED(block->Capture()))return;
            if(FAILED(d->GetViewport(&viewport)))return;
            for(size_t i=0;i<transforms.size();++i)if(FAILED(d->GetTransform(transformKinds[i],&transforms[i])))return;
            for(DWORD i=0;i<count;++i)
            {
                const HRESULT hr=d->GetRenderTarget(i,&targets[i]);
                if(FAILED(hr)&&!(i>0&&hr==D3DERR_NOTFOUND))return;
            }
            const HRESULT hr=d->GetDepthStencilSurface(&depth);
            if(FAILED(hr)&&hr!=D3DERR_NOTFOUND)return;
            valid=targets[0]!=nullptr;
        }
        bool Ready()const{return valid;}
        bool Restore(HRESULT presentResult)
        {
            if(attempted)return false;
            attempted=true;
            if(!valid||FAILED(presentResult)||device->TestCooperativeLevel()!=D3D_OK)return false;
            // Remove feedback bindings before restoring targets, then let Apply
            // reinstate all original shader/texture/sampler/render states.
            for(DWORD i=0;i<16;++i)device->SetTexture(i,nullptr);
            if(vertexTextures)for(DWORD i=0;i<4;++i)device->SetTexture(D3DVERTEXTEXTURESAMPLER0+i,nullptr);
            bool ok=SUCCEEDED(device->SetDepthStencilSurface(nullptr));
            for(DWORD i=1;i<count;++i)ok=SUCCEEDED(device->SetRenderTarget(i,nullptr))&&ok;
            ok=SUCCEEDED(device->SetRenderTarget(0,targets[0]))&&ok;
            for(DWORD i=1;i<count;++i)ok=SUCCEEDED(device->SetRenderTarget(i,targets[i]))&&ok;
            ok=SUCCEEDED(device->SetDepthStencilSurface(depth))&&ok;
            ok=SUCCEEDED(block->Apply())&&ok;
            for(size_t i=0;i<transforms.size();++i)ok=SUCCEEDED(device->SetTransform(transformKinds[i],&transforms[i]))&&ok;
            return SUCCEEDED(device->SetViewport(&viewport))&&ok;
        }
        ~PresentBoundary()
        {
            for(auto& target:targets)Release(target);
            Release(depth);Release(block);
        }
        PresentBoundary(const PresentBoundary&)=delete;
        PresentBoundary& operator=(const PresentBoundary&)=delete;
    };
}
