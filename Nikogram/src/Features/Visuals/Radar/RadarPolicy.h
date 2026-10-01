#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <limits>
#include <vector>

namespace RadarPolicy
{
    struct Point {double x,y;};
    inline double Fraction(double value,double maximum)
    {return std::isfinite(value) && std::isfinite(maximum) && maximum>0 ? std::clamp(value/maximum,0.,1.) : 0.;}
    inline Point Center(int width,int height,double radius,int anchor,int offsetX,int offsetY)
    {
        if (width<=0 || height<=0 || !std::isfinite(radius)) return {};
        radius=std::clamp(radius,0.,std::min(width,height)*.5);
        Point p{width*.5,height*.5};
        if (anchor!=1) {p.x=(anchor==2 || anchor==4)?width-radius-20:radius+20;p.y=anchor>=3?height-radius-20:radius+20;}
        p.x=std::clamp(p.x+offsetX,radius,double(width)-radius);
        p.y=std::clamp(p.y+offsetY,radius,double(height)-radius);
        return p;
    }
    inline int DetailSize(double size,double base,int minimum,int maximum,double percent=100.)
    {return std::isfinite(size)&&std::isfinite(percent)?std::clamp(int(std::lround(std::clamp(size,64.,600.)/240.*base*std::clamp(percent,25.,200.)/100.)),minimum,maximum):minimum;}
    inline std::optional<Point> Project(double dx,double dy,double yaw,double range,double radius,bool square,bool clampEdge=false)
    {
        if (!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(yaw)||!std::isfinite(range)||!std::isfinite(radius)||range<=0||radius<=0) return {};
        const double a=yaw*3.14159265358979323846/180.,c=std::cos(a),s=std::sin(a);
        // View forward is up; Source's right vector is clockwise from forward.
        Point p{(dx*s-dy*c)/range*radius,-(dx*c+dy*s)/range*radius};
        const double extent=square?std::max(std::abs(p.x),std::abs(p.y)):std::hypot(p.x,p.y);
        if(extent>radius) {if(!clampEdge)return {};p.x*=radius/extent;p.y*=radius/extent;}
        return p;
    }
    inline int Height(double delta,double tolerance)
    {if (!std::isfinite(delta)||!std::isfinite(tolerance)) return 0;tolerance=std::max(0.,tolerance);return delta>tolerance?1:delta<-tolerance?-1:0;}
    inline bool Indicators(bool enabled,int mode) {return enabled && (mode==1 || mode==2);}
    inline bool InterfaceOutline(bool colors,bool follow,bool custom) {return colors && follow && !custom;}
    struct Span {int x,y,width;};
    // Rasterize the left-hand annulus with solid horizontal spans: no texture state.
    inline std::vector<Span> ArcSpans(double radius,double thickness,double fraction)
    {
        std::vector<Span> spans;
        if(!std::isfinite(radius)||!std::isfinite(thickness)||!std::isfinite(fraction)||radius<=0||radius>1024||thickness<=0||fraction<=0) return spans;
        fraction=std::clamp(fraction,0.,1.);thickness=std::min(thickness,radius);
        const double inner=radius-thickness,pi=3.14159265358979323846;
        const double end=(120.+120.*fraction)*pi/180.;
        const int edge=int(std::ceil(radius*std::sqrt(3.)*.5));
        spans.reserve(edge*2+1);
        for(int y=-edge;y<=edge;++y)
        {
            const double outerX=std::sqrt(std::max(0.,radius*radius-double(y)*y));
            const double innerX=std::sqrt(std::max(0.,inner*inner-double(y)*y));
            int start=0,width=0;
            for(int x=int(std::floor(-outerX));x<=int(std::ceil(-innerX));++x)
            {
                const double distance=std::hypot(double(x),double(y));
                double angle=std::atan2(double(y),double(x));if(angle<0)angle+=2*pi;
                if(distance>=inner && distance<=radius && angle>=120.*pi/180. && angle<=end)
                {if(!width)start=x;++width;}
            }
            if(width)spans.push_back({start,y,width});
        }
        return spans;
    }
    inline std::optional<Point> BindPoint(double radius,int row,double spacing,double minimumY=-std::numeric_limits<double>::infinity())
    {
        if(!std::isfinite(radius)||!std::isfinite(spacing)||std::isnan(minimumY)||radius<=0||spacing<=0||row<0) return {};
        const double edge=radius*std::sqrt(3.)*.5, y=std::max(-edge,minimumY)+row*spacing;
        if(y>edge) return {};
        return Point{-std::sqrt(std::max(0.,radius*radius-y*y)),y};
    }
    inline bool Suppress(bool enabled,int mode,bool hide,bool component) {return Indicators(enabled,mode)&&hide&&component;}
}
