#include "../SDK/SDK.h"
#include "../Core/Core.h"
#include "../Features/Visuals/Dapper/WorldPhoto.h"
#include "../Features/Visuals/Dapper/SkyPhoto.h"
#include "../Features/Visuals/Dapper/WorldBindPolicy.h"
#include "../Features/Visuals/RenderAudit.h"

namespace
{
    using BindFn=void(__fastcall*)(IMatRenderContext*,IMaterial*,void*);
    BindFn queuedOriginal=nullptr;
    using BatchFn=void(__fastcall*)(IMatRenderContext*,IMesh*,IMaterial*);
    BatchFn queuedBatchOriginal=nullptr;
    IMaterial* Select(IMatRenderContext* context,IMaterial* material,bool queued,bool batch=false)
    {
        // No cache or game-interface access on a render worker outside our view.
        if(!CapturePolicy::scene.has_value()||!WorldBindPolicy::sceneTarget.has_value())return material;
        return WorldBindPolicy::Select(context,material,[](auto original){
            auto sky=DapperSky::Replacement(original);
            return sky!=original?sky:DapperWorld::Replacement(original);
        },
            [queued,batch](bool candidate,bool allowed){RenderAudit::WorldBind(queued,candidate,allowed,batch);});
    }
    void __fastcall QueuedBind(IMatRenderContext* context,IMaterial* material,void* proxyData)
    {
        HookLifetime::Scope lifetime;
        if(StartupPolicy::gate.Ready()&&!G::Unload)material=Select(context,material,true);
        queuedOriginal(context,material,proxyData);
    }
    void __fastcall QueuedBindBatch(IMatRenderContext* context,IMesh* vertices,IMaterial* material)
    {
        HookLifetime::Scope lifetime;
        WorldBindPolicy::SubmitBatch(context,vertices,material,[](auto c,auto m){
            return StartupPolicy::gate.Ready()&&!G::Unload?Select(c,m,true,true):m;
        },queuedBatchOriginal);
    }
}

static void* RenderContextBatchAddress()
{
    const auto routes=WorldBindPolicy::Discover(I::MaterialSystem,
        [](auto context){return U::Memory.GetVirtual(context,144);},MATERIAL_QUEUED_CONTEXT);
    bool ready=routes.queued&&routes.queued==routes.immediate;
    if(routes.queued&&routes.queued!=routes.immediate)
    {
        ready=MH_CreateHook(routes.queued,QueuedBindBatch,reinterpret_cast<void**>(&queuedBatchOriginal))==MH_OK;
        if(!ready)U::Core.AppendFailText("World photo queued BindBatch hook could not be created.");
    }
    RenderAudit::WorldBatchRoutes(routes.immediate,routes.queued,ready);
    return routes.immediate;
}

MAKE_HOOK(IMatRenderContext_BindBatch, RenderContextBatchAddress(), void,
    IMatRenderContext* context, IMesh* vertices, IMaterial* material)
{
    DEBUG_RETURN(IMatRenderContext_BindBatch,context,vertices,material);
    WorldBindPolicy::SubmitBatch(context,vertices,material,[](auto c,auto m){
        return !G::Unload?Select(c,m,false,true):m;
    },Hook.As<FN>());
}

static void* RenderContextBindAddress()
{
    const auto routes=WorldBindPolicy::Discover(I::MaterialSystem,
        [](auto context){return U::Memory.GetVirtual(context,9);},MATERIAL_QUEUED_CONTEXT);
    bool queuedReady=routes.queued&&routes.queued==routes.immediate;
    if(routes.queued&&routes.queued!=routes.immediate)
    {
        const auto status=MH_CreateHook(routes.queued,QueuedBind,reinterpret_cast<void**>(&queuedOriginal));
        queuedReady=status==MH_OK;
        if(!queuedReady)U::Core.AppendFailText("World photo queued Bind hook could not be created.");
    }
    RenderAudit::WorldRoutes(routes.immediate,routes.queued,queuedReady);
    return routes.immediate;
}

MAKE_HOOK(IMatRenderContext_Bind, RenderContextBindAddress(), void,
    IMatRenderContext* context, IMaterial* material, void* proxyData)
{
    DEBUG_RETURN(IMatRenderContext_Bind,context,material,proxyData);
    if(!G::Unload)material=Select(context,material,false);
    CALL_ORIGINAL(context,material,proxyData);
}
