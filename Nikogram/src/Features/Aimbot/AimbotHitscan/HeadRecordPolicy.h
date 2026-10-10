#pragma once
#include "../../Backtrack/CursorBacktrackPolicy.h"
namespace HeadRecordPolicy
{
    // Compare entry distances from the same record, not the rendered pose.
    struct Nearest
    {
        int hitbox=-1;double distance=0;
        explicit Nearest(double range):distance(range){}
        void Consider(int index,bool head,double entry)
        {
            if(!std::isfinite(entry)||entry<=0||entry>distance+1e-4)return;
            if(hitbox<0||entry<distance-1e-4 || (std::abs(entry-distance)<=1e-4&&!head))
            {hitbox=index;distance=entry;}
        }
    };
}
