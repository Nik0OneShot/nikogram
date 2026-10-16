#pragma once
#include "IndicatorOverlayPolicy.h"
#include "IndicatorTextRaster.h"
#include <ImGui/imgui.h>
#include <d3d9.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <map>
#include <tuple>
#include <chrono>
#include <bit>

namespace IndicatorOverlay
{
    enum class Kind {Line,Fill,Outline,Polygon,Polyline,Gradient,Text};
    struct Command
    {
        Kind kind{};ImVec2 a{},b{};ImVec4 clip{};ImU32 color=0,color2=0;
        bool horizontal=false;std::vector<ImVec2> points;
        std::wstring text;std::string family;int height=0,weight=0;
    };
    struct Frame
    {
        std::vector<Command> commands;size_t points=0,textUnits=0;bool failed=false;
        int width=0,height=0;double time=0;uint64_t generation=0,visibility=0;
    };
    inline std::atomic<uint64_t> generation=1;
    inline std::mutex mutex;
    inline std::shared_ptr<const Frame> ready;
    inline thread_local Frame* building=nullptr;
    inline thread_local bool paused=false;
    inline thread_local ImVec4 clip{};
    inline double Now(){return double(GetTickCount64())*.001;}
    inline uint64_t Visibility(int indicators,int style,float scale,bool menu,bool gameUI)
    {return uint64_t(uint32_t(indicators))^(uint64_t(uint32_t(style))<<16)^(uint64_t(std::bit_cast<uint32_t>(scale))<<32)^(uint64_t(menu)<<30)^(uint64_t(gameUI)<<31);}
    inline bool Active(){return building&&!paused;}
    inline void Invalidate(){generation.fetch_add(1,std::memory_order_relaxed);std::lock_guard lock(mutex);ready.reset();}
    class Capture
    {
        Frame frame;Frame* previous=building;ImVec4 previousClip=clip;
    public:
        Capture(int width,int height,uint64_t visibility)
        {
            frame.width=width;frame.height=height;frame.visibility=visibility;
            frame.generation=generation.load(std::memory_order_relaxed);frame.time=Now();
            building=&frame;clip={0,0,float(width),float(height)};
        }
        ~Capture()
        {
            building=previous;clip=previousClip;
            std::lock_guard lock(mutex);
            if(!frame.failed&&frame.generation==generation.load(std::memory_order_relaxed))ready=std::make_shared<Frame>(std::move(frame));
            else ready.reset();
        }
        Capture(const Capture&)=delete;
    };
    class Pause
    {
        bool previous=paused;
    public:Pause(){paused=true;}~Pause(){paused=previous;}
    };
    inline void SetClip(int x,int y,int w,int h)
    {if(Active())clip={float(x),float(y),float(x+w),float(y+h)};}
    inline void ClearClip(){if(Active())clip={0,0,float(building->width),float(building->height)};}
    inline void Push(Command value)
    {
        if(!Active()||building->failed)return;
        if(building->commands.size()>=IndicatorOverlayPolicy::MaxCommands||building->points+value.points.size()>IndicatorOverlayPolicy::MaxPoints
            ||value.text.size()>IndicatorOverlayPolicy::MaxText||building->textUnits+value.text.size()>65536)
        {building->failed=true;building->commands.clear();return;}
        value.clip=clip;building->points+=value.points.size();building->textUnits+=value.text.size();building->commands.push_back(std::move(value));
    }
    template<class Color> inline ImU32 Colour(Color c){return IM_COL32(c.r,c.g,c.b,c.a);}
    template<class Color> inline bool Rect(Kind kind,int x,int y,int w,int h,Color color)
    {if(!Active())return false;Command c;c.kind=kind;c.a={float(x),float(y)};c.b={float(x+w),float(y+h)};c.color=Colour(color);Push(std::move(c));return true;}
    template<class Color> inline bool Line(int x,int y,int x2,int y2,Color color)
    {return Rect(Kind::Line,x,y,x2-x,y2-y,color);}
    template<class Color> inline bool Gradient(int x,int y,int w,int h,Color top,Color bottom,bool horizontal)
    {if(!Active())return false;Command c;c.kind=Kind::Gradient;c.a={float(x),float(y)};c.b={float(x+w),float(y+h)};c.color=Colour(top);c.color2=Colour(bottom);c.horizontal=horizontal;Push(std::move(c));return true;}
    template<class Vertex,class Color> inline bool Polygon(const std::vector<Vertex>& vertices,Color color,bool fill)
    {if(!Active())return false;Command c;c.kind=fill?Kind::Polygon:Kind::Polyline;c.color=Colour(color);for(auto& v:vertices)c.points.emplace_back(v.m_Position.x,v.m_Position.y);Push(std::move(c));return true;}
    template<class Font,class Color> inline void Text(const Font& font,int x,int y,int w,int h,Color color,const wchar_t* value)
    {
        Command c;c.kind=Kind::Text;c.a={float(x),float(y)};c.b={float(x+w),float(y+h)};c.color=Colour(color);
        c.family=font.m_szName?font.m_szName:"Tahoma";c.height=font.m_nTall;c.weight=font.m_nWeight;c.text=value?value:L"";Push(std::move(c));
    }
    struct Texture {IDirect3DTexture9* value=nullptr;size_t bytes=0;uint64_t used=0;};
    using TextKey=std::tuple<std::string,int,int,std::wstring>;
    inline std::map<TextKey,Texture> textCache;
    inline size_t cacheBytes=0;
    inline uint64_t serial=0;
    inline size_t rasterCursor=0;
    inline IDirect3DDevice9* owner=nullptr;
    inline void ReleaseTextures()
    {for(auto& [key,item]:textCache)if(item.value)item.value->Release();textCache.clear();cacheBytes=0;owner=nullptr;Invalidate();}
    struct RasterBudget
    {
        size_t created=0;
        std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(2);
        bool Available()const{return created<16&&std::chrono::steady_clock::now()<deadline;}
    };
    inline IDirect3DTexture9* TextTexture(IDirect3DDevice9* device,const Command& command,RasterBudget& budget)
    {
        const TextKey key{command.family,command.height,command.weight,command.text};
        auto found=textCache.find(key);if(found!=textCache.end()){found->second.used=serial;return found->second.value;}
        // Bound allocation work. Cached strings (including outlines/halos) reuse one mask.
        if(!budget.Available())return nullptr;++budget.created;
        auto mask=IndicatorTextRaster::Build(command.family,command.height,command.weight,command.text);
        if(mask.pixels.empty())return nullptr;const size_t bytes=mask.pixels.size()*4;
        if(bytes>4*1024*1024)return nullptr;
        while(textCache.size()>=512||cacheBytes+bytes>16*1024*1024)
        {
            auto oldest=textCache.end();for(auto it=textCache.begin();it!=textCache.end();++it)
                if(it->second.used!=serial&&(oldest==textCache.end()||it->second.used<oldest->second.used))oldest=it;
            if(oldest==textCache.end())return nullptr;
            cacheBytes-=oldest->second.bytes;oldest->second.value->Release();textCache.erase(oldest);
        }
        IDirect3DTexture9* texture=nullptr;
        if(FAILED(device->CreateTexture(mask.width,mask.height,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr)))return nullptr;
        D3DLOCKED_RECT locked{};
        if(FAILED(texture->LockRect(0,&locked,nullptr,D3DLOCK_DISCARD))){texture->Release();return nullptr;}
        for(int y=0;y<mask.height;++y)std::memcpy(static_cast<unsigned char*>(locked.pBits)+y*locked.Pitch,mask.pixels.data()+size_t(y)*mask.width,size_t(mask.width)*4);
        texture->UnlockRect(0);textCache.emplace(key,Texture{texture,bytes,serial});cacheBytes+=bytes;return texture;
    }
    inline bool Draw(IDirect3DDevice9* device,int width,int height,uint64_t visibility)
    {
        if(owner!=device){ReleaseTextures();owner=device;return false;}
        std::shared_ptr<const Frame> frame;{std::lock_guard lock(mutex);frame=ready;}
        if(!frame||!IndicatorOverlayPolicy::Fresh(Now(),frame->time,frame->width,frame->height,width,height,
            frame->generation,generation.load(std::memory_order_relaxed),frame->visibility,visibility))return false;
        ++serial;auto* draw=ImGui::GetBackgroundDrawList();
        // Pin this frame's existing masks before eviction. Prepare missing text
        // with a rotating start so continuously-changing early counters cannot
        // indefinitely starve static labels later in the display list.
        std::map<TextKey,const Command*> missing;
        for(const auto& c:frame->commands)if(c.kind==Kind::Text&&!c.text.empty())
        {
            TextKey key{c.family,c.height,c.weight,c.text};auto found=textCache.find(key);
            if(found!=textCache.end())found->second.used=serial;else missing.emplace(std::move(key),&c);
        }
        std::vector<const Command*> pending;pending.reserve(missing.size());
        for(auto& [key,command]:missing)pending.push_back(command);
        RasterBudget budget;size_t attempted=0;
        if(!pending.empty())
        {
            const size_t start=rasterCursor%pending.size();
            while(attempted<pending.size()&&budget.Available())
            {TextTexture(device,*pending[(start+attempted)%pending.size()],budget);++attempted;}
            rasterCursor=start+std::max(size_t(1),attempted);
        }
        for(const auto& c:frame->commands)
        {
            draw->PushClipRect({c.clip.x,c.clip.y},{c.clip.z,c.clip.w},true);
            switch(c.kind)
            {
            case Kind::Line:draw->AddLine(c.a,c.b,c.color);break;
            case Kind::Fill:draw->AddRectFilled(c.a,c.b,c.color);break;
            case Kind::Outline:draw->AddRect(c.a,{c.b.x-1,c.b.y-1},c.color);break;
            case Kind::Polygon:if(c.points.size()>2)draw->AddConvexPolyFilled(c.points.data(),int(c.points.size()),c.color);break;
            case Kind::Polyline:if(c.points.size()>1)draw->AddPolyline(c.points.data(),int(c.points.size()),c.color,ImDrawFlags_Closed,1);break;
            case Kind::Gradient:
            {
                // Surface draws two alpha fades, not one opaque colour lerp.
                const ImU32 topClear=c.color&0x00FFFFFF,bottomClear=c.color2&0x00FFFFFF;
                draw->AddRectFilledMultiColor(c.a,c.b,c.color,c.horizontal?topClear:c.color,topClear,c.horizontal?c.color:topClear);
                draw->AddRectFilledMultiColor(c.a,c.b,bottomClear,c.horizontal?c.color2:bottomClear,c.color2,c.horizontal?bottomClear:c.color2);break;
            }
            case Kind::Text:
            {
                auto found=textCache.find(TextKey{c.family,c.height,c.weight,c.text});
                if(found!=textCache.end())draw->AddImage(reinterpret_cast<ImTextureID>(found->second.value),c.a,c.b,{0,0},{1,1},c.color);break;
            }
            }
            draw->PopClipRect();
        }
        return true;
    }
}
