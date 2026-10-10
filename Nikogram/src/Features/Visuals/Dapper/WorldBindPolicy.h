#pragma once
#include <optional>
#include <string_view>

namespace WorldBindPolicy
{
    struct Routes { void* immediate=nullptr; void* queued=nullptr; };
    // Discover both implementations through the public interface. Startup's
    // GetRenderContext alone returns the immediate context, not the game's queue.
    template<class System, class Address, class ContextType>
    Routes Discover(System* system, Address address, ContextType queuedType)
    {
        Routes result;
        if(auto context=system->GetRenderContext())
        {result.immediate=address(context);context->Release();}
        if(auto context=system->CreateRenderContext(queuedType))
        {result.queued=address(context);context->Release();}
        return result;
    }
    inline thread_local std::optional<const void*> sceneTarget;
    class SceneScope
    {
        std::optional<const void*> saved=sceneTarget;
    public:
        explicit SceneScope(const void* target)
        {
            // Nested auxiliary views must not borrow their parent's permission.
            sceneTarget=saved.has_value()?std::nullopt:std::optional<const void*>{target};
        }
        ~SceneScope(){sceneTarget=saved;}
        SceneScope(const SceneScope&)=delete;
        SceneScope& operator=(const SceneScope&)=delete;
    };
    inline bool MainTarget(const void* target,std::string_view name)
    {
        if(!sceneTarget)return false;
        if(target==*sceneTarget)return true;
        // HDR can move a backbuffer scene into the native full-frame buffer.
        // Reflections, refractions, shadows, monitors and our camera stay native.
        return !*sceneTarget&&(name=="_rt_FullFrameFB"||name=="_rt_FullFrameFB0"||name=="_rt_FullFrameFB1");
    }
    template<class Context,class Material,class Replace,class Observe>
    Material* Select(Context* context,Material* original,Replace replace,Observe observe)
    {
        if(!sceneTarget)return original;
        const auto replacement=replace(original);
        auto target=context->GetRenderTarget();
        const auto name=target?target->GetName():nullptr;
        const bool allowed=MainTarget(target,name?name:"");
        observe(replacement!=original,allowed);
        return allowed?replacement:original;
    }
    template<class Context,class Mesh,class Material,class SelectMaterial,class Forward>
    void SubmitBatch(Context* context,Mesh* vertices,Material* material,SelectMaterial select,Forward forward)
    {
        // BindBatch queues its auto-bind material directly, bypassing Bind on
        // the submission thread. Replace that argument before native queuing.
        // Null means "keep the current binding" and must remain null.
        forward(context,vertices,material?select(context,material):nullptr);
    }
}
