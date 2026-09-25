#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace PetMotion
{
    struct Rect { int id = -1; float left = 0, top = 0, right = 0, bottom = 0; };
    struct Options { float size = 64, rest = 60, sleep = 180; bool run = true, jump = true, climb = true; };
    struct World { float width = 1280, height = 720, barWidth = 1280, barHeight = 26; bool top = false; std::vector<Rect> windows; };
    enum class Mode { Stand, Walk, Run, Jump, Fall, Climb, Hang, UnderHang, Sit, Sleep, Drag };

    class Pet
    {
        uint32_t random = 0xA512DE31;
        float Rand() { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return (random & 0xffffff) / float(0x1000000); }
        float destination = 0, decision = 0, lastMouseX = 0, lastMouseY = 0, grabX = 0, grabY = 0;
        float supportLeft = 0, regrip = 0;
        bool climbDown = false, leaveRoof = false;
        int jumpWindow = -1;
        bool wrap = false, lastTop = false;
        const Rect* Window(const World& w, int id) const { for (const auto& r : w.windows) if (r.id == id) return &r; return nullptr; }
        void Set(Mode m) { if (mode != m) { mode = m; animation = 0; } }
        float Half(const Options& o) const { return o.size * .44f; }
        void Drop(const Options& o)
        {
            if(mode==Mode::UnderHang || mode==Mode::Hang) y+=o.size*.75f;
            Set(Mode::Fall); support=-1; vx=vy=0; regrip=.8f; wrap=false;
        }
        void Settle(const World& w, const Options& o, int id)
        {
            support = id; vx = vy = 0; wrap = false; jumpWindow = -1; leaveRoof=false;
            if (id < 0) { x = std::clamp(x, Half(o), std::max(Half(o), w.barWidth - Half(o))); y = w.height - w.barHeight; }
            else if (const auto* r = Window(w, id)) { y = r->top; supportLeft = r->left; x = std::clamp(x, r->left + Half(o), std::max(r->left + Half(o), r->right - Half(o))); }
            Set(Mode::Stand); decision = 1.f + Rand() * 3.f;
        }
    public:
        bool social=false;
        bool game=false,gameSit=false;
        float gameTarget=0,gameSpeed=100;
        void GameWalk(float target,float speed=100) { gameTarget=target;gameSpeed=speed;gameSit=false; }
        void GameDrop(const Options& o) { Drop(o); }
        bool Descending() const { return mode==Mode::Climb && climbDown; }
        void GameDescend() { climbDown=true;decision=1000; }
        void GameAscend() { climbDown=false;decision=1000; }
        void GameJump(float horizontal,float height,const Options& o)
        {
            if(!Grounded())return;
            vx=horizontal;vy=-std::sqrt(2.f*850.f*(o.size/64.f)*height);
            wrap=false;leaveRoof=false;jumpWindow=-1;Set(Mode::Jump);
        }
        void GameLeave(const World& w,const Options& o)
        {
            if(support<0 || !Grounded())return;
            if(const auto* r=Window(w,support))
            {
                climbRight=x>(r->left+r->right)*.5f;
                const float edge=climbRight?r->right-Half(o):r->left+Half(o);
                GameWalk(edge,100);
                if(std::abs(x-edge)<3) {x=climbRight?r->right+Half(o):r->left-Half(o);climbDown=true;decision=1000;Set(Mode::Climb);}
            }
        }
        bool GameWindow(const Rect& r,const Options& o)
        {
            if(!Grounded() || !o.climb)return false;
            const float half=Half(o),scale=o.size/64;
            const float edge=std::abs(x-r.left)<std::abs(x-r.right)?r.left-half:r.right+half;
            GameWalk(edge,110);
            if(std::abs(edge-x)>o.size*1.5f)return false;
            const float target=std::max(r.top+o.size*.7f,std::min(y-65*scale,r.bottom+o.size*.5f));
            const float velocity=(target-y-.5f*850*scale*.65f*.65f)/.65f;
            if(velocity<-620*scale || target>y)return false;
            vx=(edge-x)/.65f*1.35f;vy=velocity;jumpWindow=r.id;wrap=false;Set(Mode::Jump);return true;
        }
        float socialTarget=0;
        void Seed(uint32_t seed) { random=seed?seed:1; }
        bool Grounded() const { return mode==Mode::Stand || mode==Mode::Walk || mode==Mode::Run; }
        void SocialHop(const Options& o)
        {
            if(!Grounded()) return;
            vx=0;vy=-std::sqrt(2.f*850.f*(o.size/64.f)*10.f*(o.size/64.f));
            wrap=false;leaveRoof=false;jumpWindow=-1;Set(Mode::Jump);
        }
        bool active = false, faceRight = false, climbRight = false;
        Mode mode = Mode::Stand;
        float x = 0, y = 0, vx = 0, vy = 0, age = 0, animation = 0;
        int support = -1;
        void Close() { active = false; social=game=gameSit=false; mode = Mode::Stand; age = animation = 0; vx = vy = regrip = 0; support = -1; wrap = false; jumpWindow = -1; climbDown=leaveRoof=false; }
        void Suspend()
        {
            age=0;
            if(mode==Mode::Sit || mode==Mode::Sleep) { Set(Mode::Stand); decision=2; }
            if(mode==Mode::Drag) { Set(Mode::Fall); vx=vy=0; wrap=true; }
        }
        void Start(const World& w, const Options& o)
        {
            Close(); active = true; lastTop = w.top;
            x = std::clamp(w.barWidth * .6f, Half(o), std::max(Half(o), w.barWidth - Half(o)));
            if (w.top) { mode = Mode::Hang; y = w.barHeight; }
            else y = w.height - w.barHeight;
            decision = 2.f; destination = x;
        }
        void Grab(float mx, float my, float centre, float feet)
        {
            x = centre; y = feet; grabX = x - mx; grabY = y - my; lastMouseX = mx; lastMouseY = my;
            vx = vy = 0; age = 0; support = -1; jumpWindow = -1; leaveRoof=false; Set(Mode::Drag);
        }
        void Drag(float mx, float my, float dt)
        {
            const float blend = 1.f - std::exp(-25.f * dt);
            if (dt > 0.f) { vx += (((mx-lastMouseX)/dt)-vx)*blend; vy += (((my-lastMouseY)/dt)-vy)*blend; }
            x = mx + grabX; y = my + grabY; lastMouseX = mx; lastMouseY = my;
            if (std::abs(vx) > 10) faceRight = vx > 0;
        }
        void Release(const World& w, const Options& o)
        {
            if (std::hypot(vx,vy) < 100.f) vx = vy = 0;
            age = 0; wrap = true; Set(Mode::Fall);
            if (w.top && std::hypot(vx,vy)<100.f && y - o.size < w.barHeight + 18 && x >= 0 && x <= w.barWidth)
            { x = std::clamp(x, Half(o), std::max(Half(o),w.barWidth-Half(o))); y = w.barHeight; vx = vy = 0; Set(Mode::Hang); destination=x; decision=1; wrap=false; }
        }
        void Step(float dt, const World& w, const Options& o)
        {
            if (w.width < o.size || w.height < o.size) { Close(); return; }
            if (!active) Start(w,o);
            if(w.top!=lastTop)
            {
                lastTop=w.top;
                // Preserve position/support instead of spawning on the new bar.
                if(mode==Mode::Hang || (support<0 && mode!=Mode::Fall && mode!=Mode::Jump && mode!=Mode::Drag)) Drop(o);
                return;
            }
            dt = std::clamp(dt, 0.f, .05f); age += dt; animation += dt; decision -= dt;
            regrip=std::max(0.f,regrip-dt);
            if (mode == Mode::Drag) return;
            const float half = Half(o), scale = o.size / 64.f;
            if (mode == Mode::Hang)
            {
                y = w.barHeight;
                x = std::clamp(x,half,std::max(half,w.barWidth-half));
                if (decision <= 0) { destination=half+Rand()*std::max(0.f,w.barWidth-2*half); decision=4+Rand()*8; }
                const float step = std::clamp(destination-x,-14.f*scale*dt,14.f*scale*dt);
                x += step; if (std::abs(step)>.001f) faceRight=step>0;
                return; // No run, jump, rest, or sleep on the top taskbar.
            }
            if(mode==Mode::UnderHang)
            {
                const auto* r=Window(w,support);
                if(!r || !o.climb) { Drop(o); return; }
                x+=r->left-supportLeft;supportLeft=r->left;y=r->bottom;
                if(decision<=0 && !game)
                {
                    if(!w.top && Rand()<.2f) { Drop(o); return; }
                    decision=4+Rand()*6;
                }
                x+= (faceRight?1.f:-1.f)*14.f*scale*dt;
                if(x<=r->left || x>=r->right)
                {
                    climbRight=x>=r->right;faceRight=!climbRight;climbDown=false;
                    x=climbRight?r->right+half:r->left-half;y=r->bottom+o.size*.5f;
                    Set(Mode::Climb);decision=3+Rand()*5;
                }
                return;
            }
            if (mode == Mode::Climb)
            {
                const auto* r=Window(w,support);
                if (!r || !o.climb) { Set(Mode::Fall); support=-1; vx=vy=0; return; }
                if(decision<=0 && !game)
                {
                    if(!w.top && Rand()<.16f) { Drop(o); return; }
                    climbDown=Rand()<.35f;decision=4+Rand()*7;
                }
                x = climbRight ? r->right+half : r->left-half;
                y += (climbDown?1.f:-1.f)*22.f*scale*dt; faceRight=!climbRight;
                if(climbDown && y>=r->bottom+o.size*.5f)
                {
                    x=climbRight?r->right:r->left;y=r->bottom;supportLeft=r->left;
                    faceRight=!climbRight;Set(Mode::UnderHang);decision=4+Rand()*6;return;
                }
                if (y <= r->top) Settle(w,o,r->id);
                return;
            }
            if (mode == Mode::Fall || mode == Mode::Jump)
            {
                const float oldY=y;
                vx *= std::exp(-1.0f*dt); vy+=850.f*scale*dt;
                x+=vx*dt; y+=vy*dt;
                if (wrap)
                {
                    // Let thrown sprites travel fully off-screen before wrapping.
                    // Do not apply the wandering clamp on pre-wrap frames.
                    if(x-half>w.width || x+half<0)
                    {
                        const float span=w.width+2*half;
                        x=std::fmod(std::fmod(x+half,span)+span,span)-half;
                    }
                }
                else x=std::clamp(x,half,w.width-half);
                if (y-o.size < 0) { y=o.size; vy=std::max(vy,0.f); }
                for (const auto& r:w.windows)
                {
                    if(o.climb && regrip<=0 && vy<0 && oldY-o.size*.75f>=r.bottom && y-o.size*.75f<=r.bottom && x>=r.left && x<=r.right)
                    {
                        support=r.id;supportLeft=r.left;y=r.bottom;faceRight=vx>=0;
                        vx=vy=0;wrap=false;Set(Mode::UnderHang);decision=4+Rand()*6;return;
                    }
                    if (o.climb && regrip<=0 && y>r.top+6 && y-o.size<r.bottom &&
                        ((std::abs(x-(r.left-half))<14 && (vx>=0 || jumpWindow==r.id)) ||
                         (std::abs(x-(r.right+half))<14 && (vx<=0 || jumpWindow==r.id))))
                    {
                        support=r.id; climbRight=x>(r.left+r.right)*.5f; faceRight=!climbRight; climbDown=false; vx=vy=0; wrap=false; Set(Mode::Climb); decision=3+Rand()*5; return;
                    }
                    if (regrip<=0 && vy>=0 && oldY<=r.top+2 && y>=r.top && x>=r.left+half && x<=r.right-half)
                    { Settle(w,o,r.id); return; }
                }
                if (!w.top && vy>=0 && oldY<=w.height-w.barHeight+2 && y>=w.height-w.barHeight && x>=0 && x<=w.barWidth)
                { Settle(w,o,-1); return; }
                if (y-o.size>w.height)
                {
                    x=std::clamp(x,half,std::max(half,w.barWidth-half)); vx=vy=0; wrap=false; support=-1;
                    if(w.top) { y=w.barHeight; Set(Mode::Hang); destination=x; decision=1; }
                    else Settle(w,o,-1);
                }
                return;
            }
            float left=0,right=w.barWidth;
            if(support>=0)
            {
                const auto* r=Window(w,support);
                if(!r) { Set(Mode::Fall); support=-1; vx=vy=0; return; }
                x+=r->left-supportLeft; supportLeft=r->left; left=r->left; right=r->right; y=r->top;
            }
            else y=w.height-w.barHeight;
            x=std::clamp(x,left+half,std::max(left+half,right-half));
            if(game)
            {
                if(gameSit){Set(Mode::Sit);return;}
                const float target=std::clamp(gameTarget,left+half,std::max(left+half,right-half));
                const float delta=target-x;
                if(std::abs(delta)>1){Set(gameSpeed>60?Mode::Run:Mode::Walk);x+=std::clamp(delta,-gameSpeed*scale*dt,gameSpeed*scale*dt);faceRight=delta>0;}
                else Set(Mode::Stand);
                return;
            }
            if(!w.top && age>=o.sleep) { Set(Mode::Sleep); return; }
            if(!w.top && age>=o.rest) { Set(Mode::Sit); return; }
            if(social)
            {
                const float target=std::clamp(socialTarget,left+half,std::max(left+half,right-half));
                const float delta=target-x;
                if(std::abs(delta)>1.f) { Set(Mode::Walk);x+=std::clamp(delta,-25.f*scale*dt,25.f*scale*dt);faceRight=delta>0; }
                else { Set(Mode::Stand);decision=2.f; }
                return;
            }
            if(mode==Mode::Sit || mode==Mode::Sleep) { Set(Mode::Stand); decision=1; }
            if(mode==Mode::Walk || mode==Mode::Run)
            {
                destination=std::clamp(destination,left+half,std::max(left+half,right-half));
                float speed=(mode==Mode::Run && o.run && !w.top ? 135.f : 25.f)*scale;
                const float step=std::clamp(destination-x,-speed*dt,speed*dt); x+=step; faceRight=step>0;
                if(std::abs(destination-x)<1)
                {
                    if(leaveRoof && support>=0)
                    {
                        leaveRoof=false;climbRight=x>(left+right)*.5f;faceRight=!climbRight;
                        x=climbRight?right+half:left-half;climbDown=true;
                        Set(Mode::Climb);decision=4+Rand()*7;
                        if(!w.top && Rand()<.5f) Drop(o);
                        return;
                    }
                    Set(Mode::Stand); decision=1+Rand()*4;
                }
            }
            if(decision>0 || mode!=Mode::Stand) return;
            const float choice=Rand();
            destination=left+half+Rand()*std::max(0.f,right-left-2*half);
            if(support>=0 && o.climb && choice<.2f)
            {
                leaveRoof=true;destination=Rand()<.5f?left+half:std::max(left+half,right-half);
                Set(Mode::Walk);return;
            }
            if(!w.top && o.jump && o.climb && choice<.23f)
            {
                for(const auto& r:w.windows)
                {
                    if(r.id==support || r.top<o.size || r.bottom<y-230*scale || r.top>y) continue;
                    const bool useRight=std::abs(x-r.right)<std::abs(x-r.left);
                    const float targetX=useRight?r.right+half:r.left-half;
                    if(targetX<half || targetX>w.width-half || std::abs(targetX-x)>220*scale) continue;
                    // Reach the lower side first; climbing handles tall windows.
                    const float targetY=std::max(r.top+o.size*.7f,std::min(y-65*scale,r.bottom+o.size*.5f));
                    const float travel=.65f;
                    vx=(targetX-x)/travel*1.35f; vy=(targetY-y-.5f*850*scale*travel*travel)/travel;
                    if(vy < -620*scale) continue;
                    jumpWindow=r.id; Set(Mode::Jump); return;
                }
            }
            if(!w.top && o.jump && choice<.38f)
            { vx=std::clamp((destination-x)/.65f,-180.f*scale,180.f*scale); vy=-270.f*scale; Set(Mode::Jump); return; }
            Set(!w.top && o.run && choice>.82f ? Mode::Run : Mode::Walk);
        }
        void Update(float dt, const World& w, const Options& o)
        {
            if(active && w.top!=lastTop) { Step(0.f,w,o); return; }
            dt=std::clamp(dt,0.f,.05f);
            // Substeps keep fast, uncapped throws from skipping thin surfaces.
            const int steps=int(std::clamp(std::ceil(std::max(std::abs(vx),std::abs(vy))*dt/8.f),1.f,256.f));
            for(int i=0;i<steps;i++) Step(dt/steps,w,o);
        }
        int Frame() const
        {
            const int slow=int(animation*5), fast=int(animation*11);
            switch(mode)
            {
            case Mode::Walk: { constexpr int f[]={1,2,1,3}; return f[slow%4]; }
            case Mode::Run: { constexpr int f[]={1,2,1,3}; return f[fast%4]; }
            case Mode::Jump: return vy<0?22:4;
            case Mode::Fall: return 4;
            case Mode::Climb: { constexpr int f[]={12,13,14,13}; return f[slow%4]; }
            case Mode::UnderHang:
            case Mode::Hang: { constexpr int f[]={23,24,25,24}; return f[int(animation*3)%4]; }
            // Upright seated variant; 15 has a compressed/turned head pose.
            // Alula and Calamus select their own sitting frames in CharacterFrame.
            case Mode::Sit: return 26;
            case Mode::Sleep: return int(animation)%4<2?16:17;
            case Mode::Drag: return std::abs(vx)+std::abs(vy)>180 ? 7+fast%4 : 5+slow%2;
            default: return 1;
            }
        }
    };
}
