#pragma once
#include "Dapper.h"
#include "PhotoAssetPolicy.h"
#include "PhotoRenderPolicy.h"
#include "WorldBindPolicy.h"
#include "../CapturePolicy.h"
#include <unordered_map>
#include <vector>
#include <string_view>
#include <string>
#include <cmath>

namespace DapperWorld
{
    // Select owned materials at draw time. Never rewrite map shaders or vars.
    // Queued rendering receives the replacement as an ordinary Bind argument.
    inline std::unordered_map<IMaterial*,PhotoRenderPolicy::Surface> candidates;
    struct NameHash
    {
        using is_transparent=void;
        size_t operator()(std::string_view name) const noexcept{return std::hash<std::string_view>{}(name);}
    };
    // Enumeration yields IMaterial's real-time interface, whereas queued world
    // calls submit its queue-friendly wrapper. Their addresses differ, but the
    // public material name/group/shader identify the same material. Do not use
    // private GetRealTimeVersion vtable slots or embedded-wrapper offsets.
    inline std::unordered_map<std::string,IMaterial*,NameHash,std::equal_to<>> names;
    inline unsigned long long wrapperMatches=0;
    inline std::vector<IMaterial*> owned;
    inline IMaterial* brush=nullptr;
    inline IMaterial* prop=nullptr;
    inline IMaterial* terrain=nullptr;
    inline IMaterial* unlit=nullptr;
    inline int photo=0, count=-1;
    inline bool props=false, enabled=false;
    inline float repeat=0;
    inline unsigned serial=0;

    inline bool Eligible(IMaterial* material,bool useProps,bool& isProp)
    {
        isProp=false;
        if(!material||material->IsErrorMaterial()||!material->IsPrecached()
            ||material->IsTranslucent()||material->IsAlphaTested()
            ||material->IsTwoSided()||material->GetMaterialVarFlag(MATERIAL_VAR_NO_DRAW)
            ||material->NeedsFullFrameBufferTexture()||material->NeedsPowerOfTwoFrameBufferTexture())return false;
        const auto name=material->GetName(),group=material->GetTextureGroupName(),shader=material->GetShaderName();
        if(!name||!group||!shader)return false;
        const std::string_view n=name,g=group,s=shader;
        // The owned replacement has no proxies; originals are never modified.
        // Ordinary opaque TF2 world materials often have harmless proxies.
        // Water, sky, tools, decals and framebuffer shaders stay native.
        if(n.starts_with("skybox/")||n.starts_with("tools/")||n.starts_with("nikogram/")
            ||n.starts_with("decals/")||n.starts_with("effects/")||n.starts_with("sprites/")||n.starts_with("engine/")
            ||n.find("water")!=n.npos)return false;
        isProp=useProps&&n.starts_with("models/props")&&s=="VertexLitGeneric";
        return isProp||(g.starts_with(TEXTURE_GROUP_WORLD)&&PhotoRenderPolicy::WorldShader(s)!=PhotoRenderPolicy::Surface::Native);
    }
    inline void ClearCandidates()
    {
        names.clear();wrapperMatches=0;
        for(const auto& [material,isProp]:candidates)material->DecrementReferenceCount();
        candidates.clear();count=-1;
    }
    inline void Restore()
    {
        enabled=false;brush=prop=terrain=unlit=nullptr;photo=0;ClearCandidates();
        // Release our references only. Do not force deletion while queued
        // native draws may still reference the material.
        for(auto material:owned)material->DecrementReferenceCount();
        owned.clear();
    }
    inline IMaterial* Create(const char* texture,ITexture* pixels,float scale,bool model,bool transition=false,bool simple=false)
    {
        // World/displacement meshes were built for lightmapped shader layouts.
        // Keep that layout; an opaque self-illumination mask gives the photo
        // full brightness without changing lighting, gamma or engine cvars.
        auto kv=new KeyValues(model||simple?"UnlitGeneric":transition?"WorldVertexTransition":"LightmappedGeneric");
        kv->SetString("$basetexture",texture);kv->SetInt("$model",model?1:0);
        if(transition)kv->SetString("$basetexture2",texture);
        kv->SetInt("$selfillum",model||simple?0:1);kv->SetString("$selfillumtint","[1 1 1]");
        kv->SetInt("$translucent",0);kv->SetInt("$alphatest",0);kv->SetString("$alpha","1");
        kv->SetInt("$vertexcolor",0);kv->SetInt("$vertexalpha",0);
        kv->SetInt("$nofog",1);kv->SetInt("$ignorez",0);
        kv->SetString("$color","[1 1 1]");
        kv->SetString("$basetexturetransform",std::format("center .5 .5 scale {} {} rotate 0 translate 0 0",scale,scale).c_str());
        auto result=I::MaterialSystem->CreateMaterial(PhotoAssetPolicy::MaterialName(texture,model,++serial).c_str(),kv);
        if(!result||result->IsErrorMaterial())return nullptr;
        result->IncrementReferenceCount();
        // Only our fresh, feature-owned material may be written. Verify the
        // actual texture, not just its VMT string or IsErrorMaterial flag.
        bool found=false;auto base=result->FindVar("$basetexture",&found,false);
        if(found&&base&&pixels)base->SetTextureValue(pixels);
        bool valid=found&&base&&pixels&&base->GetTextureValue()==pixels&&!IsErrorTexture(pixels);
        if(transition)
        {
            bool secondFound=false;auto second=result->FindVar("$basetexture2",&secondFound,false);
            if(secondFound&&second&&pixels)second->SetTextureValue(pixels);
            valid=valid&&secondFound&&second&&second->GetTextureValue()==pixels;
        }
        valid=valid&&!result->IsTranslucent()&&!result->IsAlphaTested()
            &&(model||simple||result->GetMaterialVarFlag(MATERIAL_VAR_SELFILLUM));
        if(!valid)
        {result->DecrementReferenceCount();return nullptr;}
        owned.push_back(result);return result;
    }
    inline void Update(bool capture)
    {
        namespace W=Vars::Visuals::World;
        enabled=false;
        if(G::Unload||!I::EngineClient->IsInGame())
        {ClearCandidates();photo=0;brush=prop=terrain=unlit=nullptr;return;}
        const int choice=std::clamp(W::DapperPhoto.Value,0,3);
        if(!choice){ClearCandidates();photo=0;brush=prop=terrain=unlit=nullptr;return;}
        const int index=PhotoAssetPolicy::Index(choice);
        const auto texture=Dapper::TextureName(index);auto pixels=Dapper::Texture(index);
        const auto worldTexture=Dapper::TextureName(index,true);auto worldPixels=Dapper::Texture(index,true);
        if(!texture||!pixels||IsErrorTexture(pixels)||!worldTexture||!worldPixels||IsErrorTexture(worldPixels))return;
        const float scale=std::isfinite(W::DapperRepeat.Value)?std::clamp(W::DapperRepeat.Value,.25f,16.f):4.f;
        const bool useProps=W::DapperProps.Value;
        if(choice!=photo||scale!=repeat||useProps!=props)
        {
            ClearCandidates();photo=choice;repeat=scale;props=useProps;
            brush=Create(worldTexture,worldPixels,scale,false);prop=useProps?Create(texture,pixels,scale,true):nullptr;
            terrain=Create(worldTexture,worldPixels,scale,false,true);
            unlit=Create(texture,pixels,scale,false,false,true);
        }
        if(!brush)return; // Missing assets leave the map unchanged.
        const int materials=I::MaterialSystem->GetNumMaterials();
        if(count!=materials)
        {
            for(auto h=I::MaterialSystem->FirstMaterial();h!=I::MaterialSystem->InvalidMaterial();h=I::MaterialSystem->NextMaterial(h))
            {
                auto material=I::MaterialSystem->GetMaterial(h);bool isProp=false;
                if(candidates.contains(material)||!Eligible(material,useProps,isProp))continue;
                material->IncrementReferenceCount();candidates.emplace(material,isProp?PhotoRenderPolicy::Surface::Prop:
                    PhotoRenderPolicy::WorldShader(material->GetShaderName()));
                names.emplace(material->GetName(),material);
            }
            count=I::MaterialSystem->GetNumMaterials();
        }
        enabled=!capture;
    }
    inline IMaterial* Replacement(IMaterial* original)
    {
        // Worker threads and draws outside our scene never access the cache.
        // Captures need no global material restoration/reapplication cycle.
        if(!CapturePolicy::scene.has_value()||*CapturePolicy::scene||!enabled||G::Unload)return original;
        auto it=candidates.find(original);
        if(it==candidates.end()&&original)
        {
            const auto name=original->GetName();
            if(!name)return original;
            const auto named=names.find(std::string_view(name));if(named==names.end())return original;
            const auto group=original->GetTextureGroupName(),shader=original->GetShaderName();
            const auto expectedGroup=named->second->GetTextureGroupName(),expectedShader=named->second->GetShaderName();
            if(!group||!shader||!expectedGroup||!expectedShader||std::string_view(group)!=expectedGroup||std::string_view(shader)!=expectedShader)return original;
            it=candidates.find(named->second);
            if(it!=candidates.end())++wrapperMatches;
        }
        if(it==candidates.end())return original;
        // Model materials are submitted by PropScope, never by this general
        // world Bind hook. Otherwise a shadow/debug/foreign override could be
        // replaced even when the scoped model path deliberately rejected it.
        if(it->second==PhotoRenderPolicy::Surface::Prop)return original;
        const auto replacement=it->second==PhotoRenderPolicy::Surface::Terrain?terrain:
            it->second==PhotoRenderPolicy::Surface::Unlit?unlit:brush;
        return replacement?replacement:original;
    }
    inline IMaterial* PropMaterial(bool blocked)
    {
        if(!CapturePolicy::scene.has_value()||*CapturePolicy::scene||!WorldBindPolicy::sceneTarget.has_value()
            ||blocked||G::Unload||!enabled||!props||!prop)return nullptr;
        auto context=I::MaterialSystem->GetRenderContext();if(!context)return nullptr;
        auto target=context->GetRenderTarget();const auto name=target?target->GetName():nullptr;
        const bool allowed=WorldBindPolicy::MainTarget(target,name?name:"");context->Release();
        return allowed?prop:nullptr;
    }
}
