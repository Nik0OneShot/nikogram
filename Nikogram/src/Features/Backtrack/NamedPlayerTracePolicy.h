#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NamedPlayerTracePolicy
{
    using Point=std::array<double,3>;
    using Matrix=std::array<std::array<double,4>,3>;
    constexpr int MaxPlayers=128, MaxBoxes=32, Routes=5, PosesPerRoute=2;
    enum class Status { Unknown, Miss, Hit };
    struct Result { Status status=Status::Unknown; double enter=-1,leave=-1; int box=-1; };
    struct Box { Point mins{},maxs{}; Matrix matrix{}; int index=-1,group=-1,bone=-1; };
    struct Pose
    {
        std::array<Box,MaxBoxes> boxes{};
        int count=0,total=0,modelIndex=-1,hitboxSet=-1;
        unsigned handle=0,thread=0; int drawFlags=0,infoFlags=0;std::uint64_t captured=0;
        Point origin{},angles{};
        bool observed=false,complete=false,occluded=false;
    };
    inline bool Finite(const Point& p)
    {return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2]);}
    inline bool Span(int length,int offset,int count,int size)
    {return length>0&&length<=64*1024*1024&&offset>=0&&count>=0&&size>0
        &&std::uint64_t(offset)+std::uint64_t(count)*std::uint64_t(size)<=std::uint64_t(length);}
    // Complete affine inverse, including model scale. Invalid geometry is unknown.
    // A ray originating inside a box is a hit at distance zero, not a miss.
    inline Result Intersect(const Point& eye,const Point& direction,double range,const Box& box)
    {
        Result out;
        if(!Finite(eye)||!Finite(direction)||!Finite(box.mins)||!Finite(box.maxs)||!std::isfinite(range)||range<=0)return out;
        double length=0;for(int j=0;j<3;++j){length+=direction[j]*direction[j];if(box.mins[j]>box.maxs[j])return out;for(double n:box.matrix[j])if(!std::isfinite(n))return out;}
        if(std::abs(length-1.)>1e-4)return out;
        const auto& m=box.matrix;
        const double a=m[0][0],b=m[0][1],c=m[0][2],d=m[1][0],e=m[1][1],f=m[1][2],g=m[2][0],h=m[2][1],i=m[2][2];
        const double det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
        if(!std::isfinite(det)||std::abs(det)<1e-12)return out;
        const std::array<Point,3> inv={{{(e*i-f*h)/det,(c*h-b*i)/det,(b*f-c*e)/det},
            {(f*g-d*i)/det,(a*i-c*g)/det,(c*d-a*f)/det},{(d*h-e*g)/det,(b*g-a*h)/det,(a*e-b*d)/det}}};
        Point origin{},ray{};
        for(int r=0;r<3;++r)for(int col=0;col<3;++col){origin[r]+=inv[r][col]*(eye[col]-m[col][3]);ray[r]+=inv[r][col]*direction[col];}
        double enter=0,leave=range;
        for(int axis=0;axis<3;++axis)
        {
            if(!std::isfinite(origin[axis])||!std::isfinite(ray[axis]))return out;
            if(std::abs(ray[axis])<1e-12){if(origin[axis]<box.mins[axis]||origin[axis]>box.maxs[axis])return {Status::Miss};continue;}
            double lo=(box.mins[axis]-origin[axis])/ray[axis],hi=(box.maxs[axis]-origin[axis])/ray[axis];
            if(lo>hi)std::swap(lo,hi);enter=std::max(enter,lo);leave=std::min(leave,hi);
            if(enter>leave)return {Status::Miss};
        }
        return {Status::Hit,enter,leave,box.index};
    }
    inline bool Fresh(std::uint64_t now,std::uint64_t then,unsigned age=400)
    {return then&&now>=then&&now-then<=age;}
    inline Result Evaluate(const Pose& pose,const Point& eye,const Point& direction,double range)
    {
        Result nearest{Status::Miss}; bool unknown=!pose.complete||pose.count!=pose.total;
        if(!pose.observed||pose.count<=0||pose.count>MaxBoxes)return {};
        for(int n=0;n<pose.count;++n){auto hit=Intersect(eye,direction,range,pose.boxes[n]);
            unknown|=hit.status==Status::Unknown;
            if(hit.status==Status::Hit&&(nearest.status!=Status::Hit||hit.enter<nearest.enter))nearest=hit;}
        // A hit remains a proven intersection even if other boxes are unknown;
        // its distance is then only the nearest OBSERVED intersection.
        if(nearest.status==Status::Hit)return nearest;
        return unknown?Result{}:nearest;
    }
    inline const char* Name(Status s){return s==Status::Hit?"hit":s==Status::Miss?"miss":"unknown";}
    inline const char* Relation(const Result& r,double worldDistance,bool worldHit)
    {
        if(r.status==Status::Unknown)return "unknown";
        if(r.status==Status::Miss)return "ray_misses_observed_volume";
        if(!worldHit)return "volume_on_ray_without_world_comparison";
        if(!std::isfinite(worldDistance)||worldDistance<0)return "unknown";
        if(r.enter>worldDistance+.125)return "observed_volume_behind_world";
        if(r.enter<worldDistance-.125)return "observed_volume_before_world";
        return "near_world_boundary";
    }
}
