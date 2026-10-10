#pragma once
// Temporary diagnostic candidate: getters only. No gamma/exposure setters,
// device resets, rendering-mode changes, or raw native-layout assumptions.
#include <array>
#include <atomic>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <d3d9.h>
#include "CapturePolicy.h"
#include "../../Utils/ExceptionHandler/CrashLog.h"

namespace RenderAudit
{
    inline std::atomic<bool> pendingCapture=false;
    inline std::atomic<bool> disabled=false;
    inline std::atomic<unsigned long long> immediateBinds=0,queuedBinds=0,candidateBinds=0,photoBinds=0,targetRejected=0;
    inline void* immediateRoute=nullptr;
    inline void* queuedRoute=nullptr;
    inline bool queuedReady=false;
    inline void* immediateBatchRoute=nullptr;
    inline void* queuedBatchRoute=nullptr;
    inline bool queuedBatchReady=false;
    inline std::atomic<unsigned long long> batchBinds=0,batchPhotos=0;
    inline std::atomic<int> worldChoice=0;
    inline std::atomic<size_t> worldCandidates=0;
    inline std::atomic<bool> worldEnabled=false;
    inline std::atomic<const void*> worldBrush=nullptr;
    inline std::atomic<unsigned long long> worldWrapperMatches=0;
    inline std::atomic<const void*> worldTerrain=nullptr,worldProp=nullptr,worldUnlit=nullptr;
    inline std::atomic<bool> worldPropsEnabled=false;
    inline std::atomic<unsigned long long> staticPhotoDraws=0,dynamicPhotoDraws=0;
    inline void WorldModels(bool props,const void* prop,const void* terrain,const void* unlit)
    {worldPropsEnabled=props;worldProp=prop;worldTerrain=terrain;worldUnlit=unlit;}
    inline void WorldPropDraw(bool dynamic)
    {(dynamic?dynamicPhotoDraws:staticPhotoDraws).fetch_add(1,std::memory_order_relaxed);}
    inline void SignalCapture(bool capturing){if(capturing)pendingCapture.store(true,std::memory_order_relaxed);}
    inline void WorldRoutes(void* immediate,void* queued,bool ready)
    {immediateRoute=immediate;queuedRoute=queued;queuedReady=ready;}
    inline void WorldBatchRoutes(void* immediate,void* queued,bool ready)
    {immediateBatchRoute=immediate;queuedBatchRoute=queued;queuedBatchReady=ready;}
    inline void WorldCache(int choice,bool enabled,size_t candidates,const void* brush,unsigned long long wrapperMatches=0)
    {
        worldEnabled=enabled;worldCandidates=candidates;worldBrush=brush;worldWrapperMatches=wrapperMatches;
        if(worldChoice.exchange(choice)!=choice&&choice)pendingCapture.store(true,std::memory_order_relaxed);
    }
    inline void WorldBind(bool queued,bool candidate,bool allowed,bool batch=false)
    {
        (queued?queuedBinds:immediateBinds).fetch_add(1,std::memory_order_relaxed);
        if(batch)batchBinds.fetch_add(1,std::memory_order_relaxed);
        if(candidate)
        {
            candidateBinds.fetch_add(1,std::memory_order_relaxed);
            (allowed?photoBinds:targetRejected).fetch_add(1,std::memory_order_relaxed);
            if(batch&&allowed)batchPhotos.fetch_add(1,std::memory_order_relaxed);
        }
    }
    struct Record
    {
        ULONGLONG tick=0;
        DWORD thread=0;
        const char* stage="";
        int frame=0;
        bool nativeCapture=false,clean=false,sceneKnown=false,sceneCapture=false;
        bool postRemoval=false;
        int photo=0,modulation=0;
        const void* context=nullptr;
        std::array<char,96> target{};
        Vec3 tone{};
        float blend=0;
        std::array<float,3> color{};
        std::array<DWORD,8> device{};
        unsigned psHash=0,vsHash=0;
        HRESULT gpuRead=S_FALSE;
        std::array<float,8> shader{};
        std::array<float,10> cvars{};
    };
    inline std::mutex mutex;
    inline std::array<Record,256> ring;
    inline size_t next=0,count=0;
    inline std::array<float,10> latestCvars{};
    inline unsigned Hash(const float* values,size_t n)
    {
        unsigned result=2166136261u;
        auto bytes=reinterpret_cast<const unsigned char*>(values);
        for(size_t i=0;i<n*sizeof(float);++i)result=(result^bytes[i])*16777619u;
        return result;
    }
    inline void Gather(const char* stage,IDirect3DDevice9* device,bool readCvars)
    {
        if(G::Unload)return;
        Record r;r.tick=GetTickCount64();r.thread=GetCurrentThreadId();r.stage=stage;r.frame=I::GlobalVars->framecount;
        r.nativeCapture=I::EngineClient->IsTakingScreenshot();SignalCapture(r.nativeCapture);
        r.clean=Vars::Visuals::UI::CleanScreenshots.Value;
        r.sceneKnown=CapturePolicy::scene.has_value();r.sceneCapture=CapturePolicy::scene.value_or(false);
        r.postRemoval=Vars::Visuals::Removals::PostProcessing.Value;
        r.photo=Vars::Visuals::World::DapperPhoto.Value;r.modulation=Vars::Visuals::World::Modulations.Value;
        if(auto context=I::MaterialSystem->GetRenderContext())
        {
            r.context=context;r.tone=context->GetToneMappingScaleLinear();
            auto target=context->GetRenderTarget();
            strncpy_s(r.target.data(),r.target.size(),target?target->GetName():"backbuffer",_TRUNCATE);
            context->Release();
        }
        r.blend=I::RenderView->GetBlend();I::RenderView->GetColorModulation(r.color.data());
        if(readCvars)
        {
            constexpr const char* names[]{"mat_hdr_level","mat_fullbright","mat_force_tonemap_scale","mat_dynamic_tonemapping",
                "mat_queue_mode","mat_monitorgamma","mat_monitorgamma_tv_enabled","mat_disable_bloom","mat_postprocessing_combine","mat_norendering"};
            std::array<float,10> values{};
            for(size_t i=0;i<values.size();++i)
                values[i]=H::ConVars.FindVar(names[i])?H::ConVars.FindVar(names[i])->GetFloat():NAN;
            std::lock_guard lock(mutex);latestCvars=values;
        }
        if(device)
        {
            constexpr D3DRENDERSTATETYPE states[]{D3DRS_SRGBWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,
                D3DRS_ZENABLE,D3DRS_STENCILENABLE,D3DRS_COLORWRITEENABLE,D3DRS_FOGENABLE};
            r.gpuRead=S_OK;
            for(size_t i=0;i<r.device.size();++i)
            {const auto hr=device->GetRenderState(states[i],&r.device[i]);if(FAILED(hr))r.gpuRead=hr;}
            std::array<float,224*4> ps{};std::array<float,256*4> vs{};
            const auto ph=device->GetPixelShaderConstantF(0,ps.data(),224),vh=device->GetVertexShaderConstantF(0,vs.data(),256);
            if(SUCCEEDED(ph)){r.psHash=Hash(ps.data(),ps.size());std::copy_n(ps.begin(),8,r.shader.begin());}else r.gpuRead=ph;
            if(SUCCEEDED(vh))r.vsHash=Hash(vs.data(),vs.size());else r.gpuRead=vh;
        }
        std::lock_guard lock(mutex);r.cvars=latestCvars;ring[next]=r;next=(next+1)%ring.size();count=std::min(count+1,ring.size());
    }
    inline void Sample(const char* stage,IDirect3DDevice9* device=nullptr,bool readCvars=false) noexcept
    {
        if(disabled.load(std::memory_order_relaxed))return;
        try{Gather(stage,device,readCvars);}catch(...){disabled.store(true,std::memory_order_relaxed);}
    }
    inline void Write(std::ofstream& file,const Record& r)
    {
        file<<std::format("t={} tid={} frame={} phase={} native={} clean={} scene={}/{} postRemoval={} photo={} mod={} ctx={} rt={} tone=({:.6g},{:.6g},{:.6g}) blend={:.6g} color=({:.6g},{:.6g},{:.6g}) gpuHr={} states=({},{},{},{},{},{},{},{}) ps={:08X} vs={:08X} c0=({:.6g},{:.6g},{:.6g},{:.6g}) c1=({:.6g},{:.6g},{:.6g},{:.6g}) cvars=({:.6g},{:.6g},{:.6g},{:.6g},{:.6g},{:.6g},{:.6g},{:.6g},{:.6g},{:.6g})\n",
            r.tick,r.thread,r.frame,r.stage,r.nativeCapture,r.clean,r.sceneKnown,r.sceneCapture,r.postRemoval,r.photo,r.modulation,
            r.context,r.target.data(),r.tone.x,r.tone.y,r.tone.z,r.blend,r.color[0],r.color[1],r.color[2],r.gpuRead,
            r.device[0],r.device[1],r.device[2],r.device[3],r.device[4],r.device[5],r.device[6],r.device[7],r.psHash,r.vsHash,
            r.shader[0],r.shader[1],r.shader[2],r.shader[3],r.shader[4],r.shader[5],r.shader[6],r.shader[7],
            r.cvars[0],r.cvars[1],r.cvars[2],r.cvars[3],r.cvars[4],r.cvars[5],r.cvars[6],r.cvars[7],r.cvars[8],r.cvars[9]);
    }
    // I/O happens only at Present: keep a pre-capture ring, then eight seconds
    // of 250 ms snapshots. Four captures and four MiB per loaded diagnostic DLL.
    inline void FlushImpl()
    {
        static ULONGLONG lastEvent=0,until=0,lastFlush=0;
        static size_t bytes=0;static unsigned events=0;static bool initialized=false;
        const auto now=GetTickCount64();const bool requested=pendingCapture.exchange(false,std::memory_order_relaxed);
        const bool trigger=requested&&(lastEvent==0||now-lastEvent>1000)&&events<4;
        if(trigger){lastEvent=now;until=now+8000;++events;}
        if((!trigger&&(now>until||now-lastFlush<250))||bytes>=4*1024*1024||!CrashLog::Path[0])return;
        lastFlush=now;
        auto path=std::filesystem::path(CrashLog::Path).parent_path()/L"render_diagnostics.txt";
        std::ofstream file(path,initialized?std::ios::app:std::ios::trunc);if(!file)return;
        if(!initialized)
        {
            file<<"Nikogram 0.6.7 sky-photo candidate; live photo acceptance pending; 0.6.3 screenshot fix preserved. PID="<<GetCurrentProcessId()<<"\n"
                <<"Read-only native state. cvars: hdr,fullbright,forceTonemap,dynamicTonemap,queue,gamma,tvGamma,disableBloom,combinePost,norendering\n";
            initialized=true;
        }
        const auto start=file.tellp();
        file<<std::format("event={} trigger={} t={} immediate={} queued={} queueHook={} bindsImmediate={} bindsQueued={} candidates={} replacements={} targetRejected={}\n",
            events,trigger,now,immediateRoute,queuedRoute,queuedReady,immediateBinds.load(),queuedBinds.load(),candidateBinds.load(),photoBinds.load(),targetRejected.load());
        file<<std::format("worldChoice={} enabled={} cache={} brush={} batchImmediate={} batchQueued={} batchHook={} batchBinds={} batchPhotos={} wrapperMatches={}\n",
            worldChoice.load(),worldEnabled.load(),worldCandidates.load(),worldBrush.load(),immediateBatchRoute,queuedBatchRoute,queuedBatchReady,batchBinds.load(),batchPhotos.load(),worldWrapperMatches.load());
        file<<std::format("worldProps={} prop={} terrain={} unlit={} staticPhotoDraws={} dynamicPhotoDraws={}\n",
            worldPropsEnabled.load(),worldProp.load(),worldTerrain.load(),worldUnlit.load(),staticPhotoDraws.load(),dynamicPhotoDraws.load());
        std::lock_guard lock(mutex);
        // Whole pre-event buffer; thereafter the most recent frame/worker phases.
        const size_t n=trigger?count:std::min(count,size_t(32));
        for(size_t i=0;i<n;++i)Write(file,ring[(next+ring.size()-n+i)%ring.size()]);
        const auto end=file.tellp();if(end>start)bytes+=size_t(end-start);
    }
    inline void Flush() noexcept
    {
        if(disabled.load(std::memory_order_relaxed))return;
        try{FlushImpl();}catch(...){disabled.store(true,std::memory_order_relaxed);}
    }
}
