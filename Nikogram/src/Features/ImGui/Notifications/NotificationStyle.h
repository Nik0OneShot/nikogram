#pragma once
#include <algorithm>
#include <cmath>

namespace NotificationStyle
{
    inline float Remaining(float elapsed,float lifetime,float entrance)
    {
        if(!std::isfinite(elapsed)||!std::isfinite(lifetime)||!std::isfinite(entrance)||lifetime<=0)return 0;
        return std::clamp(1.f-std::max(0.f,elapsed-std::max(0.f,entrance))/lifetime,0.f,1.f);
    }
    inline int Filled(float remaining,int count)
    {return count<=0||!std::isfinite(remaining)?0:std::clamp(int(std::ceil(std::clamp(remaining,0.f,1.f)*count)),0,count);}
    struct Bar
    {
        int count=0;float cell=0,gap=0;
        float Left(int index)const{return index*(cell+gap);}
        float Right(int index)const{return Left(index)+cell;}
    };
    inline Bar Layout(float width,float scale)
    {
        if(!std::isfinite(width)||!std::isfinite(scale)||width<=0||scale<=0)return {};
        const float gap=std::max(1.f,2.f*scale);
        const int count=std::clamp(int(std::floor((width+gap)/(4.f*scale+gap))),1,24);
        return {count,(width-gap*(count-1))/count,gap};
    }
}
