#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <initializer_list>

// Engine-independent predictor/planner. This is a model of native feet turning,
// not a claim of server hitbox equivalence. It never writes animation state.
namespace BodyYawPolicy
{
    struct State { float feet=0.f, goal=0.f; };
    inline float Normalize(float x)
    {
        if(!std::isfinite(x))return 0.f;
        // Match native AngleNormalize, including the sign at exactly 180.
        x=std::fmod(x,360.f);
        if(x>180.f)x-=360.f;
        if(x<-180.f)x+=360.f;
        return x;
    }
    inline float Distance(float a,float b){return std::abs(Normalize(a-b));}
    inline bool Valid(State s){return std::isfinite(s.feet)&&std::isfinite(s.goal);}
    inline int Budget(bool moving){return moving?2:5;}
    inline float Yaw(float base,float offset,float mode){return Normalize(base+offset+mode);}
    inline void Step(State& s,float eye,bool moving,float interval)
    {
        if(!Valid(s)||!std::isfinite(eye)||!std::isfinite(interval)||interval<=0.f)return;
        eye=Normalize(eye);
        if(moving)s.goal=eye;
        else
        {
            const float delta=Normalize(s.goal-eye);
            if(std::abs(delta)>45.f)s.goal+=delta>0.f?-45.f:45.f;
        }
        s.goal=Normalize(s.goal);
        // Native ConvergeYawAngles takes abs BEFORE wrapping the signed delta.
        const float raw=s.goal-s.feet,absolute=std::abs(raw),delta=Normalize(raw);
        const float turn=720.f*interval*std::clamp(absolute/60.f,.01f,1.f);
        s.feet=Normalize(absolute<turn?s.goal:s.feet+(delta<0.f?-turn:turn));
    }
    struct Plan { float yaw=0.f,error=0.f;State end{}; };
    // Previous batch's predicted last-real body, not an interpolated render frame.
    struct RealPose { float body=0.f,target=0.f;bool valid=false; };
    inline Plan Choose(State initial,float target,float fake,bool moving,int remaining,float interval,RealPose previous={})
    {
        target=Normalize(target);fake=Normalize(fake);
        Plan best{target,std::numeric_limits<float>::infinity(),initial};
        if(!Valid(initial)||!std::isfinite(interval)||interval<=0.f)return best;
        remaining=std::clamp(remaining,1,5);
        float bestTie=std::numeric_limits<float>::infinity();
        // Real yaw names the genuine real-command pose (eye AND body), not the
        // completed body after the outgoing fake. Steering in earlier commands
        // may differ; the terminal real eye should face the requested direction.
        const float preferred=target;
        const auto consider=[&](State end,float first,float last)
        {
            const float realBodyError=Distance(end.feet,target);
            // Transport continuity with the NEW requested direction so turning
            // the camera or changing selectors is never delayed. Bound the old
            // tracking error; it must not pin a bad startup pose. A small free
            // correction range lets endpoint accuracy recover instead of locking
            // the previous body yaw. This only scores candidates, never clamps
            // a command or writes native animation state.
            const float reference=Normalize(target+std::clamp(Normalize(previous.body-previous.target),-10.f,10.f));
            const bool validReference=previous.valid&&std::isfinite(previous.body)&&std::isfinite(previous.target);
            const float realBodyJump=validReference?std::max(0.f,Distance(end.feet,reference)-3.f):0.f;
            Step(end,fake,moving,interval);
            const float error=Distance(end.feet,target);
            // Align the true last-real body and eye, retaining the continuity
            // penalty that removed body alternation. The post-fake endpoint is
            // only a secondary preference; fake is still emitted unmodified.
            const float tie=error*.1f+realBodyError*2.f+realBodyJump*2.f
                +Distance(last,preferred)*4.f+Distance(first,preferred)*.001f;
            if(tie<bestTie)
                best={Normalize(first),error,end},bestTie=tie;
        };
        if(!moving)
        {
            // Standing feet goals have only three outcomes per command: hold,
            // left 45, right 45. Many eye angles cause the SAME native outcome.
            // Choose the nearest equivalent eye to preferred, rather than always
            // snapping it to the +/-45 threshold. Enumerate at most 3^5 leaves.
            const auto representative=[&](float goal,float low,float high)
            {
                const float delta=Normalize(preferred-goal);
                if(delta>=low&&delta<=high)return preferred;
                const float a=Normalize(goal+low),b=Normalize(goal+high);
                return Distance(a,preferred)<=Distance(b,preferred)?a:b;
            };
            const auto search=[&](auto&& self,State state,int depth,float first,float last)->void
            {
                if(!depth){consider(state,first,last);return;}
                for(float yaw:{representative(state.goal,-44.9f,44.9f),
                    representative(state.goal,45.1f,179.9f),representative(state.goal,-179.9f,-45.1f)})
                {
                    State next=state;Step(next,yaw,false,interval);
                    self(self,next,depth-1,depth==remaining?yaw:first,yaw);
                }
            };
            search(search,initial,remaining,target,target);
        }
        else
        {
            const auto trial=[&](float yaw)
            {
                State state=initial;
                // Moving feet follow eye yaw directly. Earlier commands steer
                // the body; the last real command always uses the requested eye.
                for(int n=0;n<remaining;++n)Step(state,n+1==remaining?target:yaw,true,interval);
                consider(state,remaining==1?target:yaw,target);
            };
            // Cover both wrap branches, then refine locally. Candidate evaluation
            // is pure; the live prediction advances only on the selected command.
            if(remaining==1){trial(target);return best;}
            for(int offset=-180;offset<180;offset+=2)trial(Normalize(target+float(offset)));
            float centre=best.yaw;
            for(int n=-20;n<=20;++n)trial(Normalize(centre+n*.1f));
            centre=best.yaw;
            for(int n=-10;n<=10;++n)trial(Normalize(centre+n*.01f));
        }
        return best;
    }
}
