#include "../../SDK/SDK.h"
#include "AimObserver.h"
#include "AimModes.h"
#include "ProjectilePerformancePolicy.h"
#include "../ImGui/Menu/Menu.h"
#include <ImGui/imgui.h>
#include <chrono>
#include <mutex>

namespace AimObserver {
namespace {
using namespace AimObservation;
std::mutex mutex;
Decision working{},published{};
Comparisons comparisons;
Cohort pending;bool pendingReady=false;uint64_t pendingEpoch=0;
Cohort visible{},previous{};double changed=0,lastCapture=-1;
Identity localContext{},tracked{};uint32_t localWeapon=0;bool contextReady=false,contextUse=false,contextForce=false;int target=0;double lastTime=0;Vec lastPosition{};
uintptr_t channel=0;uint64_t generation=0;
double Now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
Identity Key(CTFPlayer*p){return {uint32_t(p->GetRefEHandle().ToInt()),p->m_iSpawnCounter(),I::ClientState->m_nServerCount,AimModes::Active};}
bool World(){return I::EngineClient->IsInGame()&&!I::EngineClient->IsPlayingDemo()&&I::EngineClient->GetNetChannelInfo();}
bool Current(const Decision&d,CTFPlayer*p,CTFWeaponBase*w){return p&&w&&p->IsAlive()&&d.owner==uint32_t(p->GetRefEHandle().ToInt())&&d.weapon==uint32_t(w->GetRefEHandle().ToInt())&&d.spawn==p->m_iSpawnCounter()&&d.server==I::ClientState->m_nServerCount&&d.mode==AimModes::Active&&d.style==Vars::Aimbot::General::AimType.Value;}
void ClearComparison(){++generation;pendingReady=false;comparisons.Clear();visible={};previous={};tracked={};target=0;lastTime=0;lastCapture=-1;}
bool LocalContext(){
    auto p=H::Entities.GetLocal();auto w=H::Entities.GetWeapon();
    if(!p||!w||!p->IsAlive()){ClearComparison();contextReady=false;return false;}
    const auto key=Key(p);const auto weapon=uint32_t(w->GetRefEHandle().ToInt());
    const bool use=Vars::NeuralMovement::Use.Value,force=NeuralMovement::ForceEnabled();
    if(contextReady&&(key!=localContext||weapon!=localWeapon||use!=contextUse||force!=contextForce))ClearComparison();
    contextUse=use;contextForce=force;
    localContext=key;localWeapon=weapon;contextReady=true;return true;
}
void Refresh(double now){
    const auto* c=comparisons.Display(now);
    if(!c){visible={};previous={};return;}
    if(c->serial!=visible.serial){previous=visible;visible=*c;changed=now;}
    else visible=*c; // Only newly received outcomes change; predictions stay frozen.
    if(previous.identity!=visible.identity||now-previous.created>1.5)previous={};
}
}
Scope::Scope(bool live){previous=working.live;working.live=live&&working.enabled;}
Scope::~Scope(){working.live=previous;}
void Begin(CTFPlayer*p,CTFWeaponBase*w,CUserCmd*cmd){
    working={};working.enabled=Vars::Visuals::Prediction::FireReason.Value;working.live=working.enabled;
    if(!working.enabled||!p||!w||!cmd)return;
    working.owner=p->GetRefEHandle().ToInt();working.weapon=w->GetRefEHandle().ToInt();working.spawn=p->m_iSpawnCounter();working.server=I::ClientState->m_nServerCount;
    working.mode=AimModes::Active;working.style=Vars::Aimbot::General::AimType.Value;working.command=G::OriginalCmd.command_number;
    if(!working.style)working.reason=Reason::AimInactive;
    else if(!Vars::Aimbot::General::AutoShoot.Value&&!(G::OriginalCmd.buttons&IN_ATTACK))working.reason=Reason::Manual;
}
void Mark(Reason r){working.Set(r);}
void Selected(CTFPlayer*p){if(working.enabled&&working.live){working.selected=true;working.target=p?uint32_t(p->GetRefEHandle().ToInt()):0;}}
void Seal(CTFPlayer*p,CTFWeaponBase*w,CUserCmd*cmd){
    if(!working.enabled||!cmd||!Current(working,p,w)){std::lock_guard lock(mutex);published={};return;}
    // Use existing classification only. IsAttacking/CanAttack have side effects.
    const bool cancel=bool(cmd->buttons&IN_ATTACK2)&&w->GetWeaponID()==TF_WEAPON_COMPOUND_BOW;
    const int id=w->GetWeaponID();
    const bool mortar=id==TF_WEAPON_CANNON&&SDK::AttribHookValue(0.f,"grenade_launcher_mortar_mode",w)>0.f;
    const bool release=id==TF_WEAPON_COMPOUND_BOW||id==TF_WEAPON_PIPEBOMBLAUNCHER||mortar||id==TF_WEAPON_SNIPERRIFLE_CLASSIC;
    const bool charged=(id==TF_WEAPON_COMPOUND_BOW||id==TF_WEAPON_PIPEBOMBLAUNCHER)?w->As<CTFPipebombLauncher>()->m_flChargeBeginTime()>0.f
        :mortar?w->As<CTFGrenadeLauncher>()->m_flDetonateTime()>0.f
        :id==TF_WEAPON_SNIPERRIFLE_CLASSIC&&w->As<CTFSniperRifle>()->m_flChargedDamage()>0.f;
    if(cmd->weaponselect)working.reason=Reason::Switching;
    else if(cancel)working.reason=Reason::Cancelling;
    else if(G::Attacking==1&&((!release&&(cmd->buttons&IN_ATTACK))||(release&&!(cmd->buttons&IN_ATTACK))))working.reason=release&&!(cmd->buttons&IN_ATTACK)?Reason::ReleaseRequested:working.reason==Reason::SwingRequested?Reason::SwingRequested:Reason::ShotRequested;
    else if(working.reason==Reason::SwingRequested&&!(cmd->buttons&IN_ATTACK))working.reason=Reason::Withdrawn;
    else if(release&&(cmd->buttons&IN_ATTACK)&&charged&&working.reason!=Reason::ReleaseHeld)working.reason=Reason::Charging;
    else if(release&&(cmd->buttons&IN_ATTACK)&&working.reason==Reason::Charging)working.reason=G::CanPrimaryAttack?Reason::ChargeRequested:Reason::WeaponNotReady;
    working.published=Now();std::lock_guard lock(mutex);published=working;
}
void Reset(){std::lock_guard lock(mutex);published={};ClearComparison();channel=0;contextReady=false;}
void Network(){
    const bool enabled=Vars::Visuals::Prediction::Comparison.Value;
    std::lock_guard lock(mutex);
    if(!World()){published={};ClearComparison();channel=0;return;}
    const auto currentChannel=reinterpret_cast<uintptr_t>(I::EngineClient->GetNetChannelInfo());
    if(channel&&channel!=currentChannel){published={};ClearComparison();}channel=currentChannel;
    if(!enabled||!LocalContext()){ClearComparison();return;}
    if(!target)return;
    auto e=reinterpret_cast<CBaseEntity*>(I::ClientEntityList->GetClientEntity(target));
    if(!e||!e->IsPlayer()||e->IsDormant()){ClearComparison();return;}
    auto p=e->As<CTFPlayer>();if(!p->IsAlive()||Key(p)!=tracked){ClearComparison();return;}
    const double time=p->m_flSimulationTime();const auto origin=p->m_vecOrigin();Vec pos{origin.x,origin.y,origin.z};
    if(!Finite(pos)||!std::isfinite(time)||time<lastTime){ClearComparison();return;}
    if(time==lastTime)return;
    const double dt=time-lastTime;
    // Censor impossible discontinuities, never ordinary reversals or forecast error.
    if(lastTime&&(dt>.2||Distance(pos,lastPosition)>64.f+3000.f*float(dt))){ClearComparison();return;}
    comparisons.Observe(tracked,time,pos,Now());lastTime=time;lastPosition=pos;
}
// Exclude only passive observer copying from cooperative aim deadlines. Native
// prediction/geometry work and every existing work-count limit remain unchanged.
struct CopyTime {
    using Budget=ProjectilePerformancePolicy::SearchBudget;
    using Phase=ProjectilePerformancePolicy::TracePhaseBudget;
    inline static thread_local unsigned depth=0;
    bool outer=depth++==0;
    Budget* budget=ProjectilePerformancePolicy::currentSearchBudget;
    Phase* phase=ProjectilePerformancePolicy::currentTracePhase;
    Budget before=budget?*budget:Budget{};
    Phase phaseBefore=phase?*phase:Phase{};
    Budget::Clock::time_point start=Budget::Clock::now();
    ~CopyTime(){--depth;if(!outer)return;const auto elapsed=Budget::Clock::now()-start;
        if(budget&&budget==ProjectilePerformancePolicy::currentSearchBudget&&budget->start==before.start
            &&budget->command==before.command&&budget->initialized==before.initialized
            &&budget->used==before.used&&budget->limited==before.limited)budget->start+=elapsed;
        if(phase&&phase==ProjectilePerformancePolicy::currentTracePhase&&phase->start==phaseBefore.start
            &&phase->used==phaseBefore.used)phase->start+=elapsed;}
    CopyTime()=default;CopyTime(const CopyTime&)=delete;CopyTime&operator=(const CopyTime&)=delete;
};
Capture::Capture(CTFPlayer*p,CTFWeaponBase*w,bool eligible,bool ordinaryPath):ordinary(ordinaryPath){
    if(!Vars::Visuals::Prediction::Comparison.Value||!eligible)return;
    CopyTime copying;
    if(!p||!w||!World()||!p->IsAlive()||p->IsDormant())return;
    const double now=Now();const auto key=Key(p);const double anchor=p->m_flSimulationTime();
    {std::lock_guard lock(mutex);
        if(lastCapture>=0&&now-lastCapture<.1){
            if(ordinary&&pendingReady&&pending.identity==key&&pending.anchor==anchor&&pending.command==G::OriginalCmd.command_number&&pending.ordinary.count==1){
                row=pending;epoch=generation;attach=active=true;
            }return;
        }lastCapture=now;epoch=generation;
    }
    const auto origin=p->m_vecOrigin();row.identity=key;row.entity=p->entindex();row.command=G::OriginalCmd.command_number;row.anchor=anchor;row.created=now;
    if(!row.ordinary.Add(row.anchor,{origin.x,origin.y,origin.z}))return;
    row.ordinaryState=ordinary?Availability::Available:Availability::NotRun;
    active=true;
}
void Capture::Point(double time,float x,float y,float z,bool valid){
    if(!active)return;CopyTime copying;
    if(!ordinary||!valid||time<=row.anchor||time>row.anchor+.8)return;
    if(row.ordinary.count&&time-row.ordinary.points[row.ordinary.count-1].time<.025)return;
    row.ordinary.Add(time,{x,y,z});
}
Capture::~Capture(){
    if(!active)return;CopyTime copying;
    row.ordinaryState=row.ordinary.count<2?Availability::NotRun:Availability::Available;
    std::lock_guard lock(mutex);
    if(epoch!=generation||!Vars::Visuals::Prediction::Comparison.Value)return;
    if(attach){if(pendingReady&&pending.identity==row.identity&&pending.anchor==row.anchor&&pending.command==row.command){pending.ordinary=row.ordinary;pending.ordinaryState=row.ordinaryState;}return;}
    pending=row;pendingEpoch=epoch;pendingReady=true;
}
void FinishComparison(CTFWeaponBase*weapon){
    // Main command thread, after all aim/final command decisions, never Draw.
    Cohort row;uint64_t epoch=0;
    {std::lock_guard lock(mutex);if(!pendingReady)return;row=pending;epoch=pendingEpoch;pendingReady=false;}
    if(!weapon||!World()||!Vars::Visuals::Prediction::Comparison.Value||row.command!=G::OriginalCmd.command_number)return;
    auto e=reinterpret_cast<CBaseEntity*>(I::ClientEntityList->GetClientEntity(row.entity));
    if(!e||!e->IsPlayer()||e->IsDormant())return;auto p=e->As<CTFPlayer>();
    if(!p->IsAlive()||Key(p)!=row.identity||std::abs(p->m_flSimulationTime()-row.anchor)>.0001)return;
    const auto origin=row.ordinary.points[0].position;
    const auto model=NeuralMovement::ObserverBegin(p,weapon,row.anchor,{origin.x,origin.y,origin.z});row.neuralState=model.state;
    if(model.state==Availability::Available){
        row.neural.Add(row.anchor,origin);
        const size_t count=row.ordinary.count>1?row.ordinary.count:26;
        for(size_t i=1;i<count;++i){
            const double time=row.ordinary.count>1?row.ordinary.points[i].time:row.anchor+i*.03;
            if(!model.distribution.valid&&time-row.anchor<.08)continue;
            const auto ordinary=row.ordinary.count>1?row.ordinary.points[i].position:origin;movement::Vec result{};
            if(!NeuralMovement::ObserverPoint(model,time,{ordinary.x,ordinary.y,ordinary.z},result)){row.neuralState=row.neural.count>1?Availability::Partial:Availability::Unsupported;break;}
            row.neural.Add(time,{result[0],result[1],result[2]});
        }
        if(row.neural.count<2&&row.neuralState==Availability::Available)row.neuralState=Availability::Unsupported;
    }
    std::lock_guard lock(mutex);if(epoch!=generation||!Vars::Visuals::Prediction::Comparison.Value)return;
    if(tracked!=row.identity){ClearComparison();lastCapture=row.created;tracked=row.identity;target=row.entity;lastTime=row.anchor;lastPosition=origin;}
    comparisons.Publish(row);
}
bool RenderRows(Cohort&current,Cohort&old,double&transition){
    std::lock_guard lock(mutex);
    if(!Vars::Visuals::Prediction::FireReason.Value||!World()||SDK::CleanScreenshot())published={};
    if(!Vars::Visuals::Prediction::Comparison.Value||!World()||SDK::CleanScreenshot()){ClearComparison();return false;}
    if(!LocalContext())return false;
    if(target){auto e=reinterpret_cast<CBaseEntity*>(I::ClientEntityList->GetClientEntity(target));
        if(!e||!e->IsPlayer()||e->IsDormant()||!e->As<CTFPlayer>()->IsAlive()||Key(e->As<CTFPlayer>())!=tracked){ClearComparison();return false;}}
    Refresh(Now());current=visible;old=previous;transition=changed;return bool(current.serial);
}
void DrawWorld(){
    Cohort current,old;double transition=0;if(!RenderRows(current,old,transition)||I::EngineVGui->IsGameUIVisible())return;
    const double now=Now();
    const auto draw=[&](const Cohort&row,float opacity){
        const auto path=[&](const Path&p,Color_t color,bool observed){color.a=uint8_t(255.f*opacity);for(size_t i=1;i<p.count;++i){if(p.points[i].time-p.points[i-1].time>.06)continue;auto a=p.points[i-1].position,b=p.points[i].position;
            if(observed&&i+1==p.count)b=Lerp(a,b,std::clamp((now-row.received)/.03,0.,1.));
            H::Draw.RenderLine({a.x,a.y,a.z},{b.x,b.y,b.z},color,true);}};
        path(row.ordinary,{70,190,255,255},false);path(row.neural,{230,140,255,255},false);path(row.observed,{100,240,140,255},true);
    };
    const float fade=AimObservation::Fade(now,transition);if(old.serial)draw(old,1.f-fade);draw(current,old.serial?fade:1.f);
}
void DrawHud(){
    Cohort row,old;double transition=0;RenderRows(row,old,transition);
    if(!World()||SDK::CleanScreenshot()||I::EngineVGui->IsGameUIVisible())return;
    Decision decision;{std::lock_guard lock(mutex);decision=published;}
    const bool status=Vars::Visuals::Prediction::FireReason.Value&&decision.enabled&&Now()-decision.published<.25&&Current(decision,H::Entities.GetLocal(),H::Entities.GetWeapon());
    if(!status&&!Vars::Visuals::Prediction::Comparison.Value)return;
    ImGui::SetNextWindowBgAlpha(.72f);ImGui::SetNextWindowPos({18,ImGui::GetIO().DisplaySize.y*.65f},ImGuiCond_Always);
    if(ImGui::Begin("##AimObservation",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoSavedSettings)){
        if(status)ImGui::Text("Primary aim: %s",Text(decision.reason));
        if(Vars::Visuals::Prediction::Comparison.Value){
            if(!row.serial)ImGui::TextUnformatted("Prediction comparison: waiting for evaluated player");
            else {
                ImGui::Text("Evaluated target #%d | anchor %.2fs ago",row.entity,Now()-row.created);
                ImGui::TextColored({.27f,.75f,1,1},"Ordinary: %s",Name(row.ordinaryState));ImGui::SameLine();
                ImGui::TextColored({.9f,.55f,1,1},"Neural: %s",Name(row.neuralState));
                ImGui::TextColored({.39f,.94f,.55f,1},"Observed: %s",Name(row.outcome));
                if(row.observed.count)ImGui::TextDisabled("Received %.2fs ago; observed through +%.3fs",Now()-row.received,row.observed.points[row.observed.count-1].time-row.anchor);
                float e=0,n=0;double h=0;
                if(PairedError(row,e,n,h))ImGui::Text("Error @ +%.3fs: ordinary %.1fu | neural %.1fu",h,e,n);
                else if(Error(row,false,e,h))ImGui::Text("Ordinary error %.1fu @ +%.3fs (unpaired)",e,h);
                else if(Error(row,true,e,h))ImGui::Text("Neural error %.1fu @ +%.3fs (unpaired)",e,h);
                ImGui::TextDisabled("Target positions | neural model, no collision validation");
            }
        }
    }ImGui::End();
}
}
