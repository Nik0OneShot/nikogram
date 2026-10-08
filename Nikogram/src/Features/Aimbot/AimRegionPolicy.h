#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

// Stack-only geometry. No entity, visibility or firing decisions live here.
// Projecting a convex box onto the view tangent plane lets us find its nearest
// angular boundary by checking its twelve edges, rather than a centre attractor.
namespace AimRegionPolicy
{
    using V=std::array<double,3>;
    inline V Add(V a,V b){return {a[0]+b[0],a[1]+b[1],a[2]+b[2]};}
    inline V Sub(V a,V b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
    inline V Mul(V a,double s){return {a[0]*s,a[1]*s,a[2]*s};}
    inline double Dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
    inline V Cross(V a,V b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
    inline bool Finite(V v){return std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2]);}
    struct Box
    {
        V origin{},mins{},maxs{};
        std::array<V,3> axis={V{1,0,0},V{0,1,0},V{0,0,1}};
    };
    inline V World(const Box& b,V p)
    {return Add(b.origin,Add(Mul(b.axis[0],p[0]),Add(Mul(b.axis[1],p[1]),Mul(b.axis[2],p[2]))));}
    inline bool Inverse(const Box& b,std::array<V,3>& rows)
    {
        const double det=Dot(b.axis[0],Cross(b.axis[1],b.axis[2]));
        if(!std::isfinite(det)||std::abs(det)<1.e-12)return false;
        rows={Mul(Cross(b.axis[1],b.axis[2]),1./det),Mul(Cross(b.axis[2],b.axis[0]),1./det),Mul(Cross(b.axis[0],b.axis[1]),1./det)};
        return true;
    }
    inline V Local(const std::array<V,3>& rows,V v){return {Dot(rows[0],v),Dot(rows[1],v),Dot(rows[2],v)};}
    inline bool Intersect(V eye,V direction,const Box& box,double& entry,double& exit)
    {
        if(!Finite(eye)||!Finite(direction)||!Finite(box.origin)||!Finite(box.mins)||!Finite(box.maxs))return false;
        for(int i=0;i<3;++i)if(box.mins[i]>box.maxs[i]||!Finite(box.axis[i]))return false;
        std::array<V,3> rows;
        if(!Inverse(box,rows)||Dot(direction,direction)<1.e-18)return false;
        const auto o=Local(rows,Sub(eye,box.origin)),d=Local(rows,direction);
        entry=0.;exit=std::numeric_limits<double>::infinity();
        for(int i=0;i<3;++i)
        {
            if(std::abs(d[i])<1.e-12){if(o[i]<box.mins[i]||o[i]>box.maxs[i])return false;continue;}
            double a=(box.mins[i]-o[i])/d[i],b=(box.maxs[i]-o[i])/d[i];
            if(a>b)std::swap(a,b);
            entry=std::max(entry,a);exit=std::min(exit,b);
            if(entry>exit)return false;
        }
        return std::isfinite(entry)&&std::isfinite(exit)&&exit>=0.;
    }
    struct Result{V point{};bool inside=false;double heightMiss=0.;};
    inline bool Nearest(V eye,V direction,const Box& box,Result& result,bool preserveHeight=false)
    {
        double entry,exit;
        if(Intersect(eye,direction,box,entry,exit))
        {
            // Preserve the original ray anywhere inside the valid volume,
            // including its outer margin. Never recenter an already valid aim.
            result={Add(eye,Mul(direction,entry+(exit-entry)*.5)),true};return true;
        }
        std::array<V,3> rows;
        if(!Finite(eye)||!Finite(direction)||!Finite(box.origin)||!Finite(box.mins)||!Finite(box.maxs)||!Inverse(box,rows))return false;
        const double length=std::sqrt(Dot(direction,direction));
        if(!std::isfinite(length)||length<1.e-9)return false;
        const auto forward=Mul(direction,1./length);
        auto right=Cross(forward,std::abs(forward[2])<.9?V{0,0,1}:V{0,1,0});
        right=Mul(right,1./std::sqrt(Dot(right,right)));const auto up=Cross(right,forward);
        Box inset=box;
        for(int i=0;i<3;++i)
        {
            if(box.mins[i]>=box.maxs[i])return false;
            const double margin=(box.maxs[i]-box.mins[i])*.02;
            inset.mins[i]+=margin;inset.maxs[i]-=margin;
        }
        std::array<V,8> points;
        std::array<double,8> x,y,depth;
        for(int i=0;i<8;++i)
        {
            points[i]=World(inset,{i&1?inset.maxs[0]:inset.mins[0],i&2?inset.maxs[1]:inset.mins[1],i&4?inset.maxs[2]:inset.mins[2]});
            const auto delta=Sub(points[i],eye);depth[i]=Dot(delta,forward);
            // Near-plane straddling is deliberately left to the existing
            // multipoint fallback, not an invalid perspective projection.
            if(!Finite(points[i])||depth[i]<=1.e-6)return false;
            x[i]=Dot(delta,right)/depth[i];y[i]=Dot(delta,up)/depth[i];
        }
        double best=std::numeric_limits<double>::infinity();V point{};
        const double minimumY=*std::min_element(y.begin(),y.end()),maximumY=*std::max_element(y.begin(),y.end());
        const bool intersectsRow=minimumY<=0.&&maximumY>=0.;
        const double heightMiss=preserveHeight?(intersectsRow?0.:std::min(std::abs(minimumY),std::abs(maximumY))):0.;
        for(int i=0;i<8;++i)for(int bit=1;bit<=4;bit*=2)
        {
            if(i&bit)continue;const int j=i|bit;
            const double dx=x[j]-x[i],dy=y[j]-y[i],squared=dx*dx+dy*dy;
            double t=squared>1.e-18?std::clamp(-(x[i]*dx+y[i]*dy)/squared,0.,1.):0.;
            // Intersect the projected box with the crosshair's horizontal row.
            // Lateral head-level approaches stay at head level instead of taking
            // a shorter diagonal route to the edge of a wider torso.
            if(preserveHeight&&intersectsRow)
            {
                if(y[i]*y[j]>0.)continue;
                if(std::abs(dy)>1.e-18)t=std::clamp(-y[i]/dy,0.,1.);
                else if(std::abs(y[i])>1.e-12)continue;
            }
            const double px=x[i]+dx*t,py=y[i]+dy*t,score=px*px+py*py;
            if(score>=best)continue;
            const double worldT=t*depth[i]/((1.-t)*depth[j]+t*depth[i]);
            point=Add(points[i],Mul(Sub(points[j],points[i]),worldT));best=score;
        }
        if(!std::isfinite(best)||!Finite(point))return false;
        result={point,false,heightMiss};return true;
    }
}
