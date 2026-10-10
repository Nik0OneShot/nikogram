#pragma once
#include "Dapper.h"
#include "PhotoAssetPolicy.h"
#include "WorldBindPolicy.h"
#include "../CapturePolicy.h"
#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

namespace DapperSky
{
    inline IMaterial* material=nullptr;
    inline std::vector<IMaterial*> owned;
    inline int photo=0;
    inline float repeat=0;
    inline unsigned serial=0;
    inline bool enabled=false;
    inline thread_local bool drawing=false;

    // Use the engine's sky shader, not a world/model shader. Its native sky
    // depth state, HDR output and position + one-UV layout remain intact.
    // Valve's Sky_DX9 supports an ordinary LDR base texture and UV transform:
    // source-sdk-2013/src/materialsystem/stdshaders/sky_dx9.cpp.
    inline IMaterial* Create(const char* texture,ITexture* pixels,float scale)
    {
        auto kv=new KeyValues("Sky_DX9");
        kv->SetString("$basetexture",texture);
        kv->SetString("$color","[1 1 1]");kv->SetString("$alpha","1");
        kv->SetInt("$nofog",1);kv->SetInt("$ignorez",1);
        kv->SetInt("$translucent",0);kv->SetInt("$alphatest",0);
        kv->SetString("$basetexturetransform",std::format("center .5 .5 scale {} {} rotate 0 translate 0 0",scale,scale).c_str());
        auto result=I::MaterialSystem->CreateMaterial(std::format("{}_sky_{}",texture,++serial).c_str(),kv);
        if(!result||result->IsErrorMaterial())return nullptr;
        result->IncrementReferenceCount();
        bool found=false;auto base=result->FindVar("$basetexture",&found,false);
        if(found&&base&&pixels)base->SetTextureValue(pixels);
        const auto shader=result->GetShaderName();
        const bool valid=shader&&std::string_view(shader)=="Sky_DX9"
            &&found&&base&&pixels&&!IsErrorTexture(pixels)&&base->GetTextureValue()==pixels
            &&!result->IsTranslucent()&&!result->IsAlphaTested();
        if(!valid){result->DecrementReferenceCount();return nullptr;}
        owned.push_back(result);return result;
    }
    inline void Update(bool capture)
    {
        enabled=false;
        if(G::Unload||!I::EngineClient->IsInGame()){photo=0;material=nullptr;return;}
        const int choice=Vars::Visuals::World::DapperSky.Value;
        const int index=PhotoAssetPolicy::Index(choice);
        if(index<0){photo=0;material=nullptr;return;}
        auto pixels=Dapper::Texture(index);const auto texture=Dapper::TextureName(index);
        if(!texture||!pixels||IsErrorTexture(pixels)){photo=0;material=nullptr;return;}
        const float value=Vars::Visuals::World::DapperSkyRepeat.Value;
        const float scale=std::isfinite(value)?std::clamp(value,.25f,16.f):2.f;
        if(photo!=choice||repeat!=scale)
        {photo=choice;repeat=scale;material=Create(texture,pixels,scale);}
        enabled=material&&!capture;
    }
    class DrawScope
    {
        bool saved=drawing;
    public:
        explicit DrawScope(bool allowed)
        {
            drawing=allowed&&enabled&&!G::Unload&&CapturePolicy::scene.has_value()
                &&!*CapturePolicy::scene&&WorldBindPolicy::sceneTarget.has_value();
        }
        ~DrawScope(){drawing=saved;}
        bool Active() const{return drawing;}
        DrawScope(const DrawScope&)=delete;
        DrawScope& operator=(const DrawScope&)=delete;
    };
    inline IMaterial* Replacement(IMaterial* original)
    {
        // Replace only actual sky submissions in the native skybox draw. Never
        // touch tools/toolsskybox brushes, 3D sky geometry, reflections or UI.
        if(!drawing||!CapturePolicy::scene.has_value()||*CapturePolicy::scene
            ||!WorldBindPolicy::sceneTarget.has_value()||!enabled||G::Unload||!material||!original)return original;
        if(original->IsErrorMaterial())return original;
        const auto name=original->GetName(),group=original->GetTextureGroupName(),shader=original->GetShaderName();
        if(!name||!group||!shader)return original;
        const std::string_view n=name,g=group,s=shader;
        if(!n.starts_with("skybox/")||!g.starts_with(TEXTURE_GROUP_SKYBOX)
            ||!(s=="Sky"||s=="Sky_DX9"||s=="Sky_HDR_DX9"||s=="Sky_HDR"))return original;
        return material;
    }
    inline void Restore()
    {
        enabled=false;material=nullptr;photo=0;repeat=0;
        // Keep superseded materials alive until unload: queued sky commands
        // can still own the previous selection after a menu change.
        for(auto previous:owned)previous->DecrementReferenceCount();
        owned.clear();
    }
}
