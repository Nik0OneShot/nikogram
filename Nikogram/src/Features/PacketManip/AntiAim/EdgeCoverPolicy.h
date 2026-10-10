#pragma once
#include <algorithm>
#include <cmath>
#include <array>
namespace EdgeCoverPolicy
{
    inline float Visibility(float fraction,bool hit,bool solid)
    {return solid||!hit||!std::isfinite(fraction)?1.f:std::clamp(fraction,0.f,1.f);}
    inline float Score(float head,float body){return 2.f*head+body;}
    // A head-margin leak in even one of eight approach sectors must outweigh
    // all body/separation tie-breaks. Blocked-ray distance only breaks ties.
    inline float CoverVisibility(float fraction,bool hit,bool solid)
    {return solid||!hit||!std::isfinite(fraction)?1.f:.02f*std::clamp(fraction,0.f,1.f);}
    inline float CoverScore(float head,float body){return 2.9f*head+.1f*body;}
    inline bool Keep(float previous,float best)
    {return std::isfinite(previous)&&std::isfinite(best)&&previous<=best+.08f;}
    inline bool MayExposeFake(float real){return std::isfinite(real)&&real<1.f;}
    inline bool Useful(float score){return std::isfinite(score)&&score<2.9f;}
    inline float Normalize(float yaw){return std::isfinite(yaw)?std::remainder(yaw,360.f):0.f;}
    inline float Distance(float a,float b){return std::abs(Normalize(a-b));}
    inline float Coordinate(float yaw,float other)
    {const float value=Normalize(yaw-other);return value<0.f?value+360.f:value;}
    inline bool Separated(float yaw,float other)
    {return Distance(yaw,other)>=89.999f;}
    inline bool ClearArc(float from,float to,float other,float margin=5.f)
    {
        if(!std::isfinite(from)||!std::isfinite(to)||!std::isfinite(other))return false;
        const float a=Coordinate(from,other),b=a+Normalize(to-from);
        const float low=std::min(a,b),high=std::max(a,b);
        // If already inside the exclusion zone, permit escape on that side,
        // never a traversal through the fake. Moving the fake itself cannot be
        // controlled here; this is a real-command route check, not invulnerability.
        if(a<.001f)return std::abs(b)<=180.f;
        if(a<margin)return b>=a&&b<=360.f-margin;
        if(a>360.f-margin)return b<=a&&b>=margin;
        return low>=margin-.001f&&high<=360.f-margin+.001f;
    }
    inline float Route(float from,float target,float other,bool valid,float step=20.f)
    {
        const float destination=std::clamp(Coordinate(target,other),90.f,270.f);
        if(!valid||!Separated(from,other))return Normalize(other+destination);
        const float start=Coordinate(from,other);
        return Normalize(other+start+std::clamp(destination-start,-step,step));
    }
    // A camera-independent cover probe, not a claim of exact server hitboxes.
    // Never recover model-space coordinates from a render-time bone cache.
    struct Probe { float forward,height; };
    inline Probe HeadProbe(float eyeHeight,float modelScale)
    {
        const float scale=std::isfinite(modelScale)?std::clamp(modelScale,.1f,10.f):1.f;
        return {8.f*scale,std::isfinite(eyeHeight)?std::clamp(eyeHeight,0.f,128.f*scale):64.f*scale};
    }
    inline float SeparationQuality(float yaw,float other)
    {return std::min(Distance(yaw,other),120.f);}
    struct Selection {float yaw=0,score=3;bool covered=false;};
    template<class Exposure>
    Selection SelectReal(float base,float other,bool separate,bool previousValid,float previous,Exposure&& exposure)
    {
        struct Candidate {float yaw,score;};
        std::array<Candidate,29> choices{};float best=3.f;
        for(int i=0;i<int(choices.size());++i)
        {
            // Fixed world-space samples don't rotate their grid with the camera.
            const float yaw=Normalize(i<24?float(i*15):i==24?base+90.f:i==25?other+90.f:
                i==26?other-90.f:i==27?other+180.f:previousValid?previous:base+90.f);
            const float value=exposure(yaw),score=std::isfinite(value)?std::clamp(value,0.f,3.f):3.f;
            choices[i]={yaw,score};best=std::min(best,score);
        }
        if(!Useful(best))return {};
        Selection result{};bool selected=false;
        for(const auto& c:choices)
        {
            if(!Useful(c.score)||!Keep(c.score,best))continue;
            const float separation=separate?SeparationQuality(c.yaw,other):0.f;
            const float oldSeparation=separate?SeparationQuality(result.yaw,other):0.f;
            const bool equal=std::abs(separation-oldSeparation)<.001f;
            if(!selected||separation>oldSeparation+.001f||(equal&&(c.score<result.score-.001f||
                (std::abs(c.score-result.score)<.001f&&Distance(c.yaw,base+90.f)<Distance(result.yaw,base+90.f)))))
                result={c.yaw,c.score,true},selected=true;
        }
        // Retain continuity only if the previous yaw still has useful cover AND
        // doesn't keep a cramped real/fake pair when a wider safe pair exists.
        const auto& last=choices.back();
        if(previousValid&&Useful(last.score)&&Keep(last.score,best)&&
            (!separate||SeparationQuality(last.yaw,other)>=SeparationQuality(result.yaw,other)-.001f))
            result={last.yaw,last.score,true};
        return result;
    }
    struct Pair {float real,fake;};
    // A fit is best-effort local geometry, never a server protection certificate.
    // Shot visibility is measured from actual enemy eye positions when available;
    // wall support is the fallback objective, not a substitute for that sightline.
    struct CoverFit
    {
        int worstVisible=0,visible=0,bodyVisible=0,misses=3;
        float head=96.f,body=96.f;
        bool valid=false;
    };
    inline bool BetterCover(const CoverFit& a,const CoverFit& b)
    {
        if(!a.valid)return false;if(!b.valid)return true;
        if(a.worstVisible!=b.worstVisible)return a.worstVisible<b.worstVisible;
        if(a.visible!=b.visible)return a.visible<b.visible;
        if(a.misses!=b.misses)return a.misses<b.misses;
        if(a.bodyVisible!=b.bodyVisible)return a.bodyVisible<b.bodyVisible;
        if(std::abs(a.head-b.head)>.05f)return a.head<b.head;
        return a.body<b.body-.05f;
    }
    inline bool ComparableCover(const CoverFit& a,const CoverFit& best,float slack)
    {
        return a.valid&&best.valid&&a.worstVisible==best.worstVisible&&a.visible==best.visible
            &&a.misses==best.misses&&a.bodyVisible==best.bodyVisible&&a.head<=best.head+slack;
    }
    inline bool MoreExposed(const CoverFit& a,const CoverFit& b)
    {
        if(!a.valid)return false;if(!b.valid)return true;
        if(a.visible!=b.visible)return a.visible>b.visible;
        if(a.misses!=b.misses)return a.misses>b.misses;
        if(std::abs(a.head-b.head)>.05f)return a.head>b.head;
        if(a.bodyVisible!=b.bodyVisible)return a.bodyVisible>b.bodyVisible;
        return a.body>b.body+.05f;
    }
    inline float Lateral(float reference,bool previousValid,float previous,
        const CoverFit& left,const CoverFit& right)
    {
        const float a=Normalize(reference+90.f),b=Normalize(reference-90.f);
        // Visibility remains meaningful even when no nearby wall support exists.
        if(left.worstVisible!=right.worstVisible)return left.worstVisible<right.worstVisible?a:b;
        if(left.visible!=right.visible)return left.visible<right.visible?a:b;
        if(BetterCover(left,right))return a;
        if(BetterCover(right,left))return b;
        // Retain the nearest previous side on ties. Five degrees of hysteresis
        // avoids changing sides at the perpendicular boundary due to tiny turns.
        if(previousValid&&Distance(b,previous)+5.f<Distance(a,previous))return b;
        return a;
    }
    // Explicit wall fit, measured in world units rather than an average of
    // unrelated approach directions. Missing head margins outrank distance;
    // distance outranks body fit and all angular preferences.
    struct WallFit {int misses=3;float head=96.f,body=96.f;bool valid=false;};
    inline bool SameHead(WallFit a,WallFit b)
    {return a.valid&&b.valid&&a.misses==b.misses&&std::abs(a.head-b.head)<=.05f;}
    inline bool BetterReal(WallFit a,WallFit b)
    {
        if(!a.valid)return false;if(!b.valid)return true;
        if(a.misses!=b.misses)return a.misses<b.misses;
        if(std::abs(a.head-b.head)>.05f)return a.head<b.head;
        return a.body<b.body-.05f;
    }
    inline bool BetterFake(WallFit a,WallFit b)
    {
        if(!a.valid)return false;if(!b.valid)return true;
        if(a.misses!=b.misses)return a.misses>b.misses;
        return a.head>b.head+.05f;
    }
    inline Pair Fallback(float realBase,float fakeBase,bool realEdge,bool fakeEdge,float real,float fake)
    {
        real=Normalize(real);fake=Normalize(fake);
        if(realEdge)
        {
            fake=fakeEdge?Normalize(fakeBase):fake;
            real=Normalize(fake+180.f);
        }
        else if(fakeEdge)fake=Normalize(real+180.f);
        return {real,fake};
    }
}
