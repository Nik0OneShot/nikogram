#include "Radar.h"
#include "../Groups/Groups.h"
#include "../../CritHack/CritHack.h"
#include "../../CritHack/CritIndicatorStyle.h"
#include "../../Ticks/Ticks.h"
#include "../../PacketManip/AntiAim/AntiAim.h"
#include "../../Binds/Binds.h"
#include "../../Binds/BindPresentation.h"
#include "../../Players/PlayerUtils.h"
#include "../../ImGui/Workspace.h"
#include "../../ImGui/MoonlitHud.h"

namespace
{
    namespace R=Vars::Visuals::Radar;
    constexpr double Pi=3.14159265358979323846;
    std::string ShortName(std::string name,size_t limit)
    {
        if(name.size()<=limit) return name;
        size_t end=limit>3?limit-3:0;
        while(end && (static_cast<unsigned char>(name[end])&0xc0)==0x80) --end;
        return name.substr(0,end)+"...";
    }
    Color_t Theme(float (*channel)(int)) {return {byte(channel(0)*255),byte(channel(1)*255),byte(channel(2)*255),255};}
    void Arc(int x,int y,double radius,double thickness,double fraction,Color_t color)
    {
        for(const auto& span:RadarPolicy::ArcSpans(radius,thickness,fraction))
            H::Draw.FillRect(x+span.x,y+span.y,span.width,1,color);
    }
    const char* ClassTexture(int c)
    {
        switch(c) {
        case TF_CLASS_SCOUT:return "hud/leaderboard_class_scout.vtf";
        case TF_CLASS_SOLDIER:return "hud/leaderboard_class_soldier.vtf";
        case TF_CLASS_PYRO:return "hud/leaderboard_class_pyro.vtf";
        case TF_CLASS_DEMOMAN:return "hud/leaderboard_class_demo.vtf";
        case TF_CLASS_HEAVY:return "hud/leaderboard_class_heavy.vtf";
        case TF_CLASS_ENGINEER:return "hud/leaderboard_class_engineer.vtf";
        case TF_CLASS_MEDIC:return "hud/leaderboard_class_medic.vtf";
        case TF_CLASS_SNIPER:return "hud/leaderboard_class_sniper.vtf";
        case TF_CLASS_SPY:return "hud/leaderboard_class_spy.vtf";
        default:return "vgui/glyph_multiplayer.vtf";}
    }
}

void CRadar::Draw(CTFPlayer* local)
{
    if (!R::Enabled.Value || !local || !I::EngineClient->IsInGame() || I::EngineVGui->IsGameUIVisible()) return;
    const int w=H::Draw.m_nScreenW,h=H::Draw.m_nScreenH;
    if (w<128 || h<128) return;
    const int size=std::clamp(R::Size.Value,64,std::min(600,std::min(w,h)-16)),radius=size/2;
    const auto center=RadarPolicy::Center(w,h,radius,R::Position.Value,R::OffsetX.Value,R::OffsetY.Value);
    int x=int(center.x);const int y=int(center.y);
    // Corner anchors reserve attachment space; screen-center never moves for attachments.
    if ((R::Position.Value==0 || R::Position.Value==3) && RadarPolicy::Indicators(true,R::Mode.Value))
        x=std::min(w-radius,std::max(x,radius+(R::Binds.Value?210:40)));
    const bool square=R::Shape.Value==1, body=R::Mode.Value!=2;
    const bool moonlit=MoonlitHud::Enabled(), themed=moonlit && R::InterfaceColors.Value;
    Workspace::ScopedTextColour preserveText(moonlit);
    // Honor previously saved custom colors as well as newly edited swatches.
    const auto border=RadarPolicy::InterfaceOutline(R::InterfaceColors.Value,R::InterfaceBorder.Value,R::Border.Value!=R::Border.Default)?(themed?MoonlitHud::Border:Theme(Workspace::BorderChannel)):R::Border.Value;
    const auto text=R::InterfaceColors.Value?(themed?MoonlitHud::Ink:Theme(Workspace::TextChannel)):R::Text.Value;
    const auto& font=H::Fonts.GetFont(moonlit?FONT_MOONLIT_DETAIL:FONT_CRIT_LABEL);
    const int baseIcon=std::clamp(int(H::Draw.Scale(16,Scale_Round)),10,24),line=moonlit?MoonlitHud::Label().m_nTall+MoonlitHud::S(18):font.m_nTall+4;
    const int icon=RadarPolicy::DetailSize(size,baseIcon,4,std::min(64,radius/2),R::IconScale.Value);
    const auto& nameFont=H::Fonts.GetRadarFont(RadarPolicy::DetailSize(size,font.m_nTall,6,48,R::NameScale.Value));
    const auto& distanceFont=H::Fonts.GetRadarFont(RadarPolicy::DetailSize(size,font.m_nTall,6,48,R::DistanceScale.Value));
    const int healthWidth=RadarPolicy::DetailSize(size,baseIcon+6,4,110,R::HealthWidthScale.Value),healthHeight=RadarPolicy::DetailSize(size,3,1,12,R::HealthHeightScale.Value);
    const int arrow=RadarPolicy::DetailSize(size,4,2,16,R::HeightScale.Value);
    auto label=[&](int lx,int ly,Color_t color,const std::string& value,EAlign align=ALIGN_CENTER) {H::Draw.String(font,lx,ly,color,align,value.c_str());};
    if (body)
    {
        const auto background=themed && R::Background.Value==R::Background.Default?MoonlitHud::Panel:R::Background.Value;
        const auto bg=background.Alpha(byte(background.a*std::clamp(R::Opacity.Value,0,100)/100));
        if (bg.a) {if(square) H::Draw.FillRect(x-radius,y-radius,size,size,bg);else H::Draw.FillCircle(x,y,float(radius),96,bg);}
        if (R::Outline.Value) {if(square) H::Draw.LineRect(x-radius,y-radius,size,size,border);else H::Draw.LineCircle(x,y,float(radius),96,border);}
        if (R::Rings.Value) for (int n=1;n<=2;++n) {const int r=radius*n/3;if(square) H::Draw.LineRect(x-r,y-r,r*2,r*2,border.Alpha(60));else H::Draw.LineCircle(x,y,float(r),64,border.Alpha(60));}
        const double yaw=R::Orientation.Value==1?90.:I::EngineClient->GetViewAngles().y;
        const auto origin=local->GetAbsOrigin();
        auto resource=H::Entities.GetResource();
        for (auto entity:H::Entities.GetGroup(EntityEnum::PlayerAll))
        {
            if (!entity || !entity->IsPlayer() || entity==local || entity->IsDormant()) continue;
            auto player=entity->As<CTFPlayer>();
            if (!player->IsAlive() || player->IsAGhost() || player->m_iTeamNum()<2) continue;
            const bool team=player->m_iTeamNum()==local->m_iTeamNum();
            if (!(R::Players.Value&(team?2:1))) continue;
            const auto delta=player->GetAbsOrigin()-origin;
            const auto p=RadarPolicy::Project(delta.x,delta.y,yaw,std::clamp(R::Range.Value,128,4096),radius-icon-16,square,!team);
            if (!p) continue;
            const int px=x+int(p->x),py=y+int(p->y);
            auto color=team?R::Team.Value:R::Enemy.Value;
            Group_t* group=nullptr;
            if (R::GroupColors.Value && F::Groups.GetGroup(entity,local,group,false) && group) color=F::Groups.GetColor(entity,group);
            if (R::ClassIcons.Value) H::Draw.Texture(ClassTexture(player->m_iClass()),px,py,icon,icon,ALIGN_CENTER,color);
            else if (R::Markers.Value) H::Draw.FillCircle(px,py,3,12,color);
            if (R::Height.Value)
            {
                const int hx=px+icon/2+arrow+1,hy=py;
                const int height=RadarPolicy::Height(delta.z,std::clamp(R::HeightTolerance.Value,0,256));
                if (!height) {H::Draw.Line(hx-arrow,hy,hx+arrow,hy,text);H::Draw.Line(hx-arrow,hy,hx-arrow/2,hy-arrow/2,text);H::Draw.Line(hx-arrow,hy,hx-arrow/2,hy+arrow/2,text);H::Draw.Line(hx+arrow,hy,hx+arrow/2,hy-arrow/2,text);H::Draw.Line(hx+arrow,hy,hx+arrow/2,hy+arrow/2,text);}
                else {H::Draw.Line(hx,hy-arrow,hx,hy+arrow,text);H::Draw.Line(hx,hy-height*arrow,hx-arrow*3/4,hy-height,text);H::Draw.Line(hx,hy-height*arrow,hx+arrow*3/4,hy-height,text);}
            }
            if (R::Health.Value) {const int by=py+icon/2+3;H::Draw.FillRect(px-healthWidth/2,by,healthWidth,healthHeight,R::HealthColor.Value.Alpha(40));H::Draw.FillRect(px-healthWidth/2,by,int(healthWidth*RadarPolicy::Fraction(player->m_iHealth(),player->GetMaxHealth())),healthHeight,R::HealthColor.Value);}
            if (R::Names.Value && resource) {const auto name=ShortName(F::PlayerUtils.GetPlayerName(player->entindex(),resource->GetName(player->entindex())),20);H::Draw.String(nameFont,px,py-icon/2-3,text,ALIGN_BOTTOM,name.c_str());}
            if (R::Distance.Value) H::Draw.String(distanceFont,px,py+icon/2+3+(R::Health.Value?healthHeight+3:0),text,ALIGN_TOP,std::format("{} HU",int(std::hypot(delta.x,delta.y))).c_str());
        }
        if (R::Local.Value)
        {
            const double facing=(I::EngineClient->GetViewAngles().y-yaw)*Pi/180.;
            const double fx=-std::sin(facing),fy=-std::cos(facing),rx=-fy,ry=fx;
            H::Draw.FillPolygon({Vertex_t({float(x+fx*6),float(y+fy*6)}),
                Vertex_t({float(x-fx*4+rx*4),float(y-fy*4+ry*4)}),
                Vertex_t({float(x-fx*4-rx*4),float(y-fy*4-ry*4)})},R::LocalColor.Value);
        }
    }
    if (!RadarPolicy::Indicators(true,R::Mode.Value)) return;
    const bool curved=R::BarStyle.Value==0 && !square;
    auto weapon=local->IsAlive()?H::Entities.GetWeapon():nullptr;
    const int crits=F::CritHack.RadarAvailable(weapon),potential=F::CritHack.RadarPotential(weapon);
    const bool streaming=weapon && local->IsAlive() && weapon->AreRandomCritsEnabled() && weapon->m_flCritTime()>TICKS_TO_TIME(local->m_nTickBase());
    const int anti=F::AntiAim.YawOn()?F::AntiAim.AntiAimTicks():0;
    const int maximum=std::max(F::Ticks.m_iMaxUsrCmdProcessTicks-anti,0);
    const int ticks=local->IsAlive()?std::clamp(F::Ticks.m_iShiftedTicks+std::max(I::ClientState->chokedcommands-anti,0),0,maximum):0;
    const int thickness=5,gap=4;
    int attached=0;
    // Inner tick gauge first; crit gauge is always the outer (leftmost) one.
    auto gauge=[&](bool enabled,Color_t color,double charge)
    {
        if (!enabled) return;
        const int r=radius+7+(++attached)*(thickness+gap);
        const auto dim=color.Lerp({0,0,0,color.a},.75f);
        if(curved) {Arc(x,y,r,thickness,1,dim);Arc(x,y,r,thickness,charge,color);}
        else {const int bx=x-radius-7-attached*(thickness+gap);H::Draw.FillRect(bx,y-radius,thickness,size,dim);const int fill=int(size*charge);H::Draw.FillRect(bx,y+radius-fill,thickness,fill,color);}
    };
    const auto tickColour=themed && R::TickColor.Value==R::TickColor.Default?MoonlitHud::Lavender:R::TickColor.Value;
    const auto critColour=themed && R::CritColor.Value==R::CritColor.Default?MoonlitHud::Gold:R::CritColor.Value;
    gauge(R::Ticks.Value,tickColour,RadarPolicy::Fraction(ticks,maximum));
    gauge(R::Crit.Value,critColour,streaming?1.:RadarPolicy::Fraction(crits,potential));
    // Both captions belong to one compact cluster above the gauge ends.
    const int labelX=x-int((radius+7+attached*(thickness+gap))*(curved?.50:1.));
    int labelY=std::max(4,y-radius-2*line-6);
    auto caption=[&](const std::string& name,const std::string& value,Color_t colour) {
        if(moonlit) MoonlitHud::RadarCaption(labelX,labelY,name,value,colour);
        else label(labelX,labelY,colour,value=="STREAMING"?value:name+" "+value,ALIGN_TOP);
        labelY+=line;
    };
    if(R::Crit.Value) caption("CRIT",streaming?"STREAMING":std::format("{} / {}",crits,potential),critColour);
    if(R::Ticks.Value) caption("TICKS",std::format("{} / {}",ticks,maximum),tickColour);
    if (R::Binds.Value)
    {
        const int right=std::max(100,x-radius-16-attached*(thickness+gap)),top=std::max({8,y-radius+12,labelY+8});
        int row=0;
        const auto active=R::InterfaceColors.Value?(Vars::Menu::BindTextGlowCustom.Value?Vars::Menu::BindTextGlowColour.Value:text.Lerp({255,255,255,255},.35f)):R::BindActive.Value;
        const auto inactive=R::InterfaceColors.Value?(themed?MoonlitHud::Muted:Theme(Workspace::InactiveTextChannel)):R::BindInactive.Value;
        // Walk visibility/parent state without recursion or trusting corrupt parent graphs.
        for (int n=0;n<int(F::Binds.m_vBinds.size()) && top+(row+1)*line<h-8;++n)
        {
            auto& bind=F::Binds.m_vBinds[n];
            if (!bind.m_bEnabled || bind.m_iVisibility==BindVisibilityEnum::Hidden) continue;
            bool visible=true;int parent=bind.m_iParent,depth=0;
            while(parent!=DEFAULT_BIND) {if(parent<0||parent>=int(F::Binds.m_vBinds.size())||++depth>int(F::Binds.m_vBinds.size())) {visible=false;break;}const auto& b=F::Binds.m_vBinds[parent];if(!b.m_bEnabled||!b.m_bActive) {visible=false;break;}parent=b.m_iParent;}
            const bool effective=BindPresentation::Get(bind,n,visible,MenuMode::Active==MenuMode::Moonlit).active;
            if (!visible || (bind.m_iVisibility==BindVisibilityEnum::WhileActive && !effective)) continue;
            const auto name=ShortName(bind.m_sName,24);
            const std::string info=R::ShowBindKey.Value && bind.m_iType==BindEnum::Key?U::KeyHandler.String(byte(std::clamp(bind.m_iKey,0,255))):"";
            const auto value=info.empty()?name:std::format("{}  {}",name,info);
            int bx=right,by=top+row*line;
            if(curved)
            {
                const auto p=RadarPolicy::BindPoint(radius+7+attached*(thickness+gap)+18,row,line,top-y+font.m_nTall/2);
                if(!p) break;
                bx=x+int(p->x)-4;by=y+int(p->y)-font.m_nTall/2;
            }
            ++row;
            if (moonlit && R::BindBackground.Value)
            {
                MoonlitHud::Pill(bx-MoonlitHud::Measure(value,font)/2,by-MoonlitHud::S(6),value,effective?active:inactive,font);
                continue;
            }
            if (R::BindBackground.Value) {auto extent=H::Draw.GetTextSize(value.c_str(),font);H::Draw.FillRect(bx-int(extent.x)-3,by-2,int(extent.x)+6,line,R::Background.Value);}
            if(effective) for(int oy=-1;oy<=1;++oy) for(int ox=-1;ox<=1;++ox) if(ox||oy) label(bx+ox,by+oy,active.Alpha(byte(active.a/12)),value,ALIGN_TOPRIGHT);
            label(bx,by,effective?active:inactive,value,ALIGN_TOPRIGHT);
        }
    }
}
