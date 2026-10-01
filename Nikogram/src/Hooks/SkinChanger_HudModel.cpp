#include "../SDK/SDK.h"
#include "../Features/SkinChanger/SkinChanger.h"

// Installed-client HUD/model-panel functions, confirmed against the native
// callers and Valve's model-panel implementation. These offsets describe UI
// copies only: never access or modify the real inventory item's definition.
MAKE_SIGNATURE(SkinHud_UpdateModel, "client.dll", "40 57 48 83 EC 60 80 B9 C8 02 00 00 00 48 8B F9", 0x0);
MAKE_SIGNATURE(SkinHud_Think, "client.dll", "40 55 56 48 8D AC 24 68 FD FF FF 48 81 EC 98 03 00 00", 0x0);
MAKE_SIGNATURE(SkinHud_ThinkTimerLayout, "client.dll", "0F 2F 81 54 02 00 00 0F 82 ? ? ? ? F3 0F 58 05 ? ? ? ? 4C 89 B4 24 70 03 00 00 F3 0F 11 81 54 02 00 00", 0x0);
MAKE_SIGNATURE(SkinHud_EquipItem, "client.dll", "41 56 41 57 48 83 EC 28 83 B9 CC 0E 00 00 00 4C 8B FA 4C 8B F1", 0x0);
MAKE_SIGNATURE(SkinHud_LoadAttachment, "client.dll", "48 89 5C 24 10 56 57 41 56 48 83 EC 20 48 8B F1 4D 8B F0 49 8B C8 48 8B DA", 0x0);
MAKE_SIGNATURE(SkinHud_ClearItems, "client.dll", "48 89 5C 24 08 57 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8B 0D ? ? ? ? 4C 8D 83 C0 0E 00 00", 0x0);
MAKE_SIGNATURE(SkinHud_ItemSkin, "client.dll", "48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 30 48 63 FA 41 0F B6 E8 48 8B F1 83 FF 04", 0x0);
MAKE_SIGNATURE(SkinHud_ItemAnimationSlot, "client.dll", "48 89 5C 24 08 57 48 83 EC 30 0F B7 59 48 48 8B F9 E8 ? ? ? ? 48 8B C8 8B D3 E8 ? ? ? ? 4C 8D 0D ? ? ? ? C7 44 24 20 00 00 00 00 4C 8D 05 ? ? ? ? 33 D2 48 8B C8 E8 ? ? ? ? 48 85 C0 75 10 B8 FF FF FF FF", 0x0);

namespace
{
    using Profile=SkinChanger::HudAppearance;
    struct OwnedItem {void* panel=nullptr;Profile profile;int skin=-1;};
    std::map<void*,OwnedItem> ownedItems;
    thread_local void* hudPanel=nullptr;
    thread_local void* equipItem=nullptr;
    thread_local const Profile* equipProfile=nullptr;
    thread_local int attachmentSkin=-1;
    thread_local int loadSkin=-1;
    thread_local bool firstAttachment=false;
    thread_local void* lastHud=nullptr;
    thread_local std::optional<Profile> lastRefresh;
    thread_local void* lastThinkHud=nullptr;
    thread_local uint32_t lastThinkWeapon=0;
    bool Contains(void* panel,void* item,size_t offset)
    {
        if(!panel||!item)return false;
        const auto& copies=*reinterpret_cast<CUtlVector<void*>*>(uintptr_t(panel)+offset);
        if(copies.Count()<0||copies.Count()>64)return false;
        for(int n=0;n<copies.Count();++n)if(copies[n]==item)return true;
        return false;
    }
    void Forget(void* panel)
    {std::erase_if(ownedItems,[&](const auto& row){return row.second.panel==panel;});}
    void Remember(void* panel,void* item,const Profile& profile,int skin)
    {
        if(!item)return;
        if(ownedItems.size()>=256&&!ownedItems.contains(item))ownedItems.clear();
        ownedItems[item]={panel,profile,skin};
    }
    bool Current(const OwnedItem& item)
    {auto current=SkinChanger::HudAppearanceFor();return current&&*current==item.profile;}
}

void SkinChanger::RestoreHudPreview()
{
    // lastHud was observed by native Think, not guessed from an entity pointer.
    // Run on the game's frame thread with G::Unload set, so EquipItem requests
    // only the real loadout models. Native refresh owns deletion of UI copies.
    if(lastHud&&S::SkinHud_UpdateModel())S::SkinHud_UpdateModel.Call<void>(lastHud);
    ownedItems.clear();lastRefresh.reset();
}

MAKE_HOOK(SkinHud_UpdateModel, S::SkinHud_UpdateModel(), void, void* self)
{
    DEBUG_RETURN(SkinHud_UpdateModel,self);
    // +0x278 is read by the native refresh before ClearCarriedItems/AddCarriedItem.
    auto panel=*reinterpret_cast<void**>(uintptr_t(self)+0x278);
    SkinRender::DrawStateScope context(hudPanel,panel);
    CALL_ORIGINAL(self);
    // A native refresh in Think already consumed the new cosmetic selection.
    // Do not clear/reload the same preview again after it returns.
    lastHud=self;lastRefresh=SkinChanger::HudAppearanceFor();
}

MAKE_HOOK(SkinHud_Think, S::SkinHud_Think(), void, void* self)
{
    DEBUG_RETURN(SkinHud_Think,self);
    // Native Think can switch the held HUD item directly, without UpdateModel.
    // Keep the verified panel context active for that entire native switch so
    // EquipItem substitutes the cosmetic before the stock model is requested.
    auto panel=*reinterpret_cast<void**>(uintptr_t(self)+0x278);
    SkinRender::DrawStateScope context(hudPanel,panel);
    auto local=H::Entities.GetLocal();auto active=local?local->m_hActiveWeapon().Get():nullptr;
    if(local&&local->IsAlive()&&!local->InCond(TF_COND_DISGUISED)&&!active&&!G::Unload
        &&Vars::Misc::SkinChanger::Enabled.Value&&lastHud==self&&lastRefresh)return;
    const uint32_t weapon=active?uint32_t(local->m_hActiveWeapon().ToInt()):0;
    if(active&&local->IsAlive()&&!local->InCond(TF_COND_DISGUISED)&&!G::Unload&&I::GlobalVars
        &&S::SkinHud_ThinkTimerLayout()
        &&SkinRender::HudImmediateThink(Vars::Misc::SkinChanger::Enabled.Value,lastRefresh.has_value(),
            lastThinkHud==self,lastThinkWeapon,weapon))
    {
        // Native Think normally waits 0.5s. Update its REAL held-slot state
        // before rebuilding the preview, rather than refreshing the old slot.
        *reinterpret_cast<float*>(uintptr_t(self)+0x254)=I::GlobalVars->curtime;
        SkinChanger::ObserveCosmeticEffect("hud_preview_switch","native_poll_due_before_slot_and_cosmetic_refresh");
    }
    CALL_ORIGINAL(self);
    if(active){lastThinkHud=self;lastThinkWeapon=weapon;}
    // A transient missing active entity during switching is not a request to
    // replace the cosmetic with a default weapon. Wait for the real next one.
    if(local&&local->IsAlive()&&!active&&!G::Unload)return;
    // Native Think already handles real weapon/class/disguise changes. A
    // selection edit or disabling the feature also needs one preview refresh.
    auto current=SkinChanger::HudAppearanceFor();
    if(lastHud==self&&lastRefresh==current)return;
    lastHud=self;lastRefresh=current;
    if(!G::Unload&&S::SkinHud_UpdateModel())S::SkinHud_UpdateModel.Call<void>(self);
    SkinChanger::ObserveCosmeticEffect("hud_preview_refresh",current?"selection_changed":"original_appearance_restored");
}

MAKE_HOOK(SkinHud_ClearItems, S::SkinHud_ClearItems(), void, void* self)
{
    DEBUG_RETURN(SkinHud_ClearItems,self);
    Forget(self); // revoke pointer identities BEFORE the engine deletes its UI copies
    CALL_ORIGINAL(self);
}

MAKE_HOOK(SkinHud_EquipItem, S::SkinHud_EquipItem(), void, void* self,void* item)
{
    DEBUG_RETURN(SkinHud_EquipItem,self,item);
    auto profile=SkinChanger::HudAppearanceFor();
    if(self!=hudPanel||!profile||!Contains(self,item,0xed8))return CALL_ORIGINAL(self,item);
    auto& definition=*reinterpret_cast<uint16_t*>(uintptr_t(item)+0x48);
    if(!SkinRender::HudCopyEligible(self==hudPanel,true,definition,profile->item))return CALL_ORIGINAL(self,item);
    // HoldItemInSlot has already selected the real loadout slot. Temporarily
    // change ONLY its owned copy while native EquipItem selects the cosmetic's
    // stand pose, model and authored attachments; restore it before slot queries.
    Remember(self,item,*profile,profile->skin);
    if(hudPanel)SkinChanger::ObserveCosmeticEffect("hud_preview_native_equip","cosmetic_selected_before_native_model_request");
    SkinRender::DrawStateScope definitionScope(definition,uint16_t(profile->reskin));
    SkinRender::DrawStateScope itemScope(equipItem,item);
    const Profile* selected=&*profile;
    SkinRender::DrawStateScope profileScope(equipProfile,selected);
    SkinRender::DrawStateScope firstScope(firstAttachment,true);
    CALL_ORIGINAL(self,item);
    // Some all-class cosmetics have no authored Spy display-model entry. The
    // catalog provides the same client-only fallback used in the world draw.
    if(firstAttachment)S::SkinHud_LoadAttachment.Call<void>(self,profile->model.c_str(),item);
    for(const auto& extra:profile->extras)
    {
        SkinRender::DrawStateScope skinScope(attachmentSkin,extra.skin);
        S::SkinHud_LoadAttachment.Call<void>(self,extra.model.c_str(),item);
    }
    SkinChanger::ObserveCosmeticEffect("hud_preview_equip",std::format("item={} reskin={} skin={} extras={} inventory_unchanged=true",profile->item,profile->reskin,profile->skin,profile->extras.size()));
}

MAKE_HOOK(SkinHud_LoadAttachment, S::SkinHud_LoadAttachment(), void, void* self,const char* model,void* item)
{
    DEBUG_RETURN(SkinHud_LoadAttachment,self,model,item);
    if(self!=hudPanel||item!=equipItem||!equipProfile)
    {
        // EquipAllWearables runs before EquipItem; don't retain the stock
        // Botkiller head and then also append the selected cosmetic's head.
        if(self==hudPanel&&model&&Contains(self,item,0xed8))
        {
            auto profile=SkinChanger::HudAppearanceFor();
            if(profile&&!profile->originalExtra.empty()&&profile->originalExtra==model
                &&*reinterpret_cast<uint16_t*>(uintptr_t(item)+0x48)==profile->item)return;
        }
        return CALL_ORIGINAL(self,model,item);
    }
    const bool primary=std::exchange(firstAttachment,false);
    const int skin=primary?equipProfile->skin:attachmentSkin;
    SkinRender::DrawStateScope skinScope(loadSkin,skin);
    const auto& loaded=*reinterpret_cast<CUtlVector<void*>*>(uintptr_t(self)+0xf68);
    const int before=loaded.Count();
    CALL_ORIGINAL(self,primary?equipProfile->model.c_str():model,item);
    // Async model-load callbacks use further engine-owned copies. Track these
    // too so their skin remains correct after this equip scope has ended.
    if(before>=0&&loaded.Count()>=before&&loaded.Count()<=64)
        for(int n=before;n<loaded.Count();++n)Remember(self,loaded[n],*equipProfile,skin);
}

MAKE_HOOK(SkinHud_ItemSkin, S::SkinHud_ItemSkin(), int, void* item,int team,bool alternate)
{
    DEBUG_RETURN(SkinHud_ItemSkin,item,team,alternate);
    if(equipProfile&&(item==equipItem||Contains(hudPanel,item,0xf68)))
        return loadSkin>=0?loadSkin:CALL_ORIGINAL(item,team,alternate);
    auto found=ownedItems.find(item);
    if(found!=ownedItems.end()&&found->second.skin>=0&&Current(found->second))return found->second.skin;
    return CALL_ORIGINAL(item,team,alternate);
}

MAKE_HOOK(SkinHud_ItemAnimationSlot, S::SkinHud_ItemAnimationSlot(), int, void* item)
{
    DEBUG_RETURN(SkinHud_ItemAnimationSlot,item);
    if(item==equipItem&&equipProfile&&equipProfile->cls==8&&equipProfile->reskin==154)
        return TF_WPN_TYPE_MELEE_ALLCLASS; // match Pain Train's cosmetic pan-style pose on Spy
    return CALL_ORIGINAL(item);
}

MAKE_HOOK(SkinHud_DrawModel, U::Memory.GetVirtual(I::StudioRender,29), void,
    void* self,DrawModelResults_t* results,const DrawModelInfo_t& info,matrix3x4* bones,
    float* flex,float* delayed,const Vector& origin,int flags)
{
    DEBUG_RETURN(SkinHud_DrawModel,self,results,info,bones,flex,delayed,origin,flags);
    // CMDL passes the owned CEconItemView's IClientRenderable subobject (+8).
    // Never reinterpret arbitrary renderables: require a registered UI copy.
    auto key=reinterpret_cast<void*>(uintptr_t(info.m_pClientEntity)-8);
    auto found=ownedItems.find(key);
    if(found==ownedItems.end()||!Current(found->second)||!info.m_pStudioHdr)
        return CALL_ORIGINAL(self,results,info,bones,flex,delayed,origin,flags);
    auto localWeapon=H::Entities.GetWeapon();if(!localWeapon)return CALL_ORIGINAL(self,results,info,bones,flex,delayed,origin,flags);
    const auto& profile=found->second.profile;
    auto copy=info;
    if(SkinRender::ValidSkin(found->second.skin,info.m_pStudioHdr->numskinfamilies))copy.m_Skin=found->second.skin;
    auto model=SkinChanger::HudPaintModel(profile);
    if(!model||I::ModelInfoClient->GetStudiomodel(model)!=info.m_pStudioHdr)
        return CALL_ORIGINAL(self,results,copy,bones,flex,delayed,origin,flags);
    ModelRenderInfo_t paintInfo{};paintInfo.pModel=model;paintInfo.entity_index=localWeapon->entindex();
    SkinChanger::PaintDrawScope paint(paintInfo,false);
    SkinChanger::ObserveCosmeticEffect("hud_preview_draw","selected_weapon_native_ui_bones_paint_scope");
    CALL_ORIGINAL(self,results,copy,bones,flex,delayed,origin,flags);
}
