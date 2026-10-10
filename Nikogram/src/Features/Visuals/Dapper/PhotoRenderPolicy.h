#pragma once
#include <string_view>

namespace PhotoRenderPolicy
{
    enum class Surface { Native, Brush, Unlit, Terrain, Prop };
    constexpr Surface WorldShader(std::string_view shader)
    {
        if(shader=="LightmappedGeneric")return Surface::Brush;
        if(shader=="UnlitGeneric")return Surface::Unlit;
        if(shader=="WorldVertexTransition"||shader=="WorldVertexTransition_DX9")return Surface::Terrain;
        // Do not feed a four-layer displacement an unrelated one-layer shader.
        return Surface::Native;
    }
    constexpr bool MapProp(std::string_view model)
    {return model.starts_with("models/props_")||model.starts_with("models/props/");}

    inline thread_local unsigned propDepth=0;
    // Submit the override on the scene thread, so StudioRender's queued draws
    // carry it with them. Worker threads must not consult our scene/cache TLS.
    template<class Model,class Material,class Override> class PropScope
    {
        Model* model;
        Material* saved=nullptr;
        Override savedType;
        bool changed=false;
    public:
        PropScope(Model* renderer,Material* replacement,Override normal):model(renderer),savedType(normal)
        {
            if(!model||!replacement)return;
            model->GetMaterialOverride(&saved,&savedType);
            if(saved||savedType!=normal)return; // Depth/glow/chams overrides own their pass.
            changed=true;++propDepth;
            model->ForcedMaterialOverride(replacement,normal);
        }
        ~PropScope()
        {
            if(!changed)return;
            model->ForcedMaterialOverride(saved,savedType);
            --propDepth;
        }
        bool Active() const{return changed;}
        PropScope(const PropScope&)=delete;
        PropScope& operator=(const PropScope&)=delete;
    };
}
