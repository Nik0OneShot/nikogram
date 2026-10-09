#pragma once
#include <array>
#include <unordered_map>

namespace RenderState
{
    // Restoration must reach the engine even when an enclosing effect draw is
    // protecting its own override from native model callbacks.
    inline thread_local unsigned overrideWrites = 0;
    struct OverrideWriteScope
    {
        OverrideWriteScope() { ++overrideWrites; }
        ~OverrideWriteScope() { --overrideWrites; }
        OverrideWriteScope(const OverrideWriteScope&) = delete;
        OverrideWriteScope& operator=(const OverrideWriteScope&) = delete;
    };

    template<class View, class Model, class Material, class Override>
    class ModelScope
    {
        View* view;
        Model* model;
        float color[3]{};
        float blend;
        Material* material = nullptr;
        Override type{};
    public:
        ModelScope(View* v, Model* m) : view(v), model(m), blend(v->GetBlend())
        {
            view->GetColorModulation(color);
            model->GetMaterialOverride(&material, &type);
        }
        ~ModelScope()
        {
            view->SetColorModulation(color);
            view->SetBlend(blend);
            OverrideWriteScope restoring;
            model->ForcedMaterialOverride(material, type);
        }
        ModelScope(const ModelScope&) = delete;
        ModelScope& operator=(const ModelScope&) = delete;
    };

    template<class Context> class ContextScope
    {
        Context* context;
    public:
        explicit ContextScope(Context* c) : context(c) {}
        ~ContextScope() { if(context) context->Release(); }
        Context* Get() const { return context; }
        ContextScope(const ContextScope&) = delete;
        ContextScope& operator=(const ContextScope&) = delete;
    };

    // Keep the engine's authored color, including water tints. Disabling a
    // feature restores only materials that feature actually modified.
    template<class Material> class MaterialModulation
    {
        std::unordered_map<Material*, std::array<float, 3>> originals;
    public:
        bool Empty() const { return originals.empty(); }
        void Apply(Material* material, float r, float g, float b)
        {
            auto [it, inserted] = originals.try_emplace(material);
            if(inserted)
            {
                material->GetColorModulation(&it->second[0], &it->second[1], &it->second[2]);
                material->IncrementReferenceCount();
            }
            const auto& color = it->second;
            material->ColorModulate(color[0] * r, color[1] * g, color[2] * b);
        }
        void Restore()
        {
            for(const auto& [material, color] : originals)
            {
                material->ColorModulate(color[0], color[1], color[2]);
                material->DecrementReferenceCount();
            }
            originals.clear();
        }
        MaterialModulation() = default;
        MaterialModulation(const MaterialModulation&) = delete;
        MaterialModulation& operator=(const MaterialModulation&) = delete;
    };
}
