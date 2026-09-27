#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// Engine-independent decisions shared by the live controller and its tests.
namespace BlockbotModel
{
    struct Point { float x = 0, y = 0, z = 0; };
    inline float Distance(Point a, Point b) { return std::hypot(a.x-b.x, a.y-b.y); }
    inline bool Finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
    inline bool TeamAllowed(int filter, int local, int target)
    { return (local == 2 || local == 3) && (target == 2 || target == 3) && (filter == 2 || (filter == 0 && target != local) || (filter == 1 && target == local)); }
    inline float HullSpacing(Point direction, Point halfSize)
    {
        const float x=std::abs(direction.x), y=std::abs(direction.y);
        return std::min(x>.001f ? halfSize.x/x : 10000.f, y>.001f ? halfSize.y/y : 10000.f)+4.f;
    }
    inline bool ManualMatches(int selectedUser, unsigned selectedAccount, int user, unsigned account)
    { return selectedUser>0 && selectedUser==user && selectedAccount==account; }
    inline bool Eligible(int mode, int priority, float distance, float range)
    { return mode>=0 && mode<=2 && std::isfinite(distance) && distance<=range && (mode==2 || (mode==1 ? priority>=1 : priority>=0)); }
    struct Override
    {
        double lastInput = -std::numeric_limits<double>::infinity();
        bool Paused(double now, bool input, double delay = .5)
        {
            if (input) lastInput = now;
            delay = std::isfinite(delay) ? std::max(0., delay) : .5;
            return input || now - lastInput < delay;
        }
    };
    inline bool UseProjectileIntercept(int mode) { return mode == 1; }
    enum class Terrain { Safe, Recoverable, Dangerous, Blocked };
    inline float SupportHeight(Point normal,float planeDistance,float x,float y)
    { return (planeDistance-normal.x*x-normal.y*y)/normal.z; }
    inline bool PermitDrop(bool mapReady,bool landing,bool hazardFree)
    { return mapReady && landing && hazardFree; }
    // Search the target's projected opening, including fine offsets that the
    // coarse recovery spokes miss. Validation owns full hull/landing checks.
    template<class Validate>
    bool DropEntry(Point here,Point target,Validate validate,Point& next)
    {
        if(!Finite(here)||!Finite(target)||target.z>=here.z-18.f)return false;
        float best=std::numeric_limits<float>::max(); bool found=false;
        for(int x=-4;x<=4;++x)for(int y=-4;y<=4;++y)
        {
            Point entry{target.x+x*8.f,target.y+y*8.f,here.z};
            if(Distance(here,entry)>128.f)continue;
            const float score=Distance(here,entry)+2.f*Distance(target,entry);
            if(score>=best||!validate(entry))continue;
            best=score;next=entry;found=true;
        }
        return found;
    }
    inline bool CanTraverse(Terrain terrain, bool ignoreDanger)
    { return terrain==Terrain::Safe || (ignoreDanger && (terrain==Terrain::Recoverable || terrain==Terrain::Dangerous)); }
    inline float RecoveryScore(float distance, Terrain terrain)
    { return distance + (terrain==Terrain::Safe ? 0.f : terrain==Terrain::Recoverable ? 100.f : 1000.f); }
    inline bool RetainTarget(bool enabled, int mode, int oldMode, int user, unsigned account, int oldUser, unsigned oldAccount)
    { return enabled && mode==oldMode && ManualMatches(oldUser,oldAccount,user,account); }
    inline bool RecoverHead(Point feet, Point target, float height)
    {
        return Finite(feet) && Finite(target) && std::isfinite(height) &&
            feet.z>=target.z+height-6.f && feet.z<=target.z+height+96.f && Distance(feet,target)<=128.f;
    }
    inline bool ManualOverride(bool movement, bool jump, bool duck, bool allowDuck)
    { return movement || jump || (duck && !allowDuck); }
    inline bool UIBlocked(bool menu, bool allowMenu, bool gameUI, bool cursor, bool focused)
    { return !focused || gameUI || (menu && !allowMenu) || (cursor && !(menu && allowMenu)); }
    // Tune the command's correction relative to measured velocity, not the server's
    // acceleration cvars. 100% exactly preserves the original requested velocity.
    inline Point Response(Point desired, Point current, float acceleration, float deceleration)
    {
        if (!Finite(desired) || !Finite(current)) return {};
        const Point error{desired.x-current.x,desired.y-current.y,0};
        float percent=(error.x*current.x+error.y*current.y<0) ? deceleration : acceleration;
        percent=std::isfinite(percent) ? std::clamp(percent,25.f,200.f) : 100.f;
        if (percent==100.f) return desired;
        Point result{current.x+error.x*percent*.01f,current.y+error.y*percent*.01f,0};
        const float length=std::hypot(result.x,result.y);
        if (length>450.f) { result.x*=450.f/length; result.y*=450.f/length; }
        return result;
    }
    inline bool Better(int mode, int priority, float distance, int oldPriority, float oldDistance)
    { return mode == 1 && priority != oldPriority ? priority > oldPriority : distance < oldDistance; }
    inline Point Velocity(Point here, Point goal, Point feed, float maximum)
    {
        if (!Finite(here) || !Finite(goal) || !Finite(feed) || !std::isfinite(maximum)) return {};
        Point v{(goal.x-here.x)*6.f+feed.x, (goal.y-here.y)*6.f+feed.y, 0};
        const float length = std::hypot(v.x,v.y), cap = std::clamp(maximum,0.f,450.f);
        if (length > cap) { v.x *= cap/length; v.y *= cap/length; }
        return v;
    }
    // A short navigation waypoint sets direction, not the arrival slowdown.
    // Use the actual blocking/following destination to choose pursuit speed.
    inline Point PursuitVelocity(Point here, Point steering, Point arrival, Point feed, float maximum)
    {
        if (!Finite(arrival)) return {};
        Point direction=Velocity(here,steering,feed,maximum);
        const Point desired=Velocity(here,arrival,feed,maximum);
        const float length=std::hypot(direction.x,direction.y), speed=std::hypot(desired.x,desired.y);
        if (length<.001f) return {};
        direction.x*=speed/length; direction.y*=speed/length;
        return direction;
    }
    inline Point Command(Point world, float yaw)
    {
        if (!Finite(world) || !std::isfinite(yaw)) return {};
        const float a = yaw * .017453292519943295f;
        return {world.x*std::cos(a)+world.y*std::sin(a), world.x*std::sin(a)-world.y*std::cos(a), 0};
    }
    // Bounded A*: unlike the short recovery fan, intermediate steps may move away
    // from the target to exit a hallway or go around an L-shaped obstruction.
    template<class Floor,class Segment>
    bool Navigate(Point start,Point goal,Floor&& floor,Segment&& segment,Point& next)
    {
        if(!Finite(start)||!Finite(goal))return false;
        struct Node { Point p; int x,y,parent; float cost; bool closed=false; };
        std::vector<Node> nodes={{start,0,0,-1,0}};
        auto first=[&](int index)
        {
            while(nodes[index].parent>0)index=nodes[index].parent;
            next=nodes[index].p;
        };
        int best=0;
        for(int count=0;count<128;++count)
        {
            int current=-1;float score=std::numeric_limits<float>::max();
            for(int i=0;i<int(nodes.size());++i)
            {
                float f=nodes[i].cost+Distance(nodes[i].p,goal);
                if(!nodes[i].closed&&f<score){score=f;current=i;}
            }
            if(current<0)break;
            nodes[current].closed=true;
            const Node node=nodes[current];
            if(Distance(node.p,goal)<Distance(nodes[best].p,goal))best=current;
            if(Distance(node.p,goal)<=96 && segment(node.p,goal))
            { if(current==0)next=goal;else first(current);return true; }
            for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)
            {
                if(!dx&&!dy)continue;
                const int x=node.x+dx,y=node.y+dy;
                if(std::abs(x)>12||std::abs(y)>12)continue;
                int existing=-1;
                for(int i=0;i<int(nodes.size());++i)if(nodes[i].x==x&&nodes[i].y==y){existing=i;break;}
                const float cost=node.cost+std::hypot(float(dx),float(dy))*32.f;
                if(existing>=0&&(nodes[existing].closed||nodes[existing].cost<=cost))continue;
                Point p{start.x+x*32.f,start.y+y*32.f,node.p.z},ground;
                if(!floor(p,ground)||!Finite(ground)||!segment(node.p,ground))continue;
                if(existing>=0)nodes[existing]={ground,x,y,current,cost};
                else nodes.push_back({ground,x,y,current,cost});
            }
        }
        // For destinations beyond the local search radius, return a reachable
        // progress step. Nearby unreachable destinations do not get a false route.
        if(Distance(start,goal)>384 && best>0 && Distance(nodes[best].p,goal)+32<Distance(start,goal))
        {first(best);return true;}
        return false;
    }
    inline bool OnHead(Point local, Point target, Point mins, Point maxs)
    {
        return std::abs(local.z-(target.z+maxs.z)) <= 6.f &&
            local.x >= target.x+mins.x+4 && local.x <= target.x+maxs.x-4 &&
            local.y >= target.y+mins.y+4 && local.y <= target.y+maxs.y-4;
    }
    template<class Floor, class Clear>
    bool WalkSegment(Point a, Point b, Floor&& floor, Clear&& clear)
    {
        if (!Finite(a) || !Finite(b)) return false;
        const float distance=Distance(a,b);
        if (distance>1600 || std::abs(a.z-b.z)>128) return false;
        const int count=std::max(1,int(std::ceil(distance/16.f)));
        Point previous;
        if (!floor(a,previous)) return false;
        for (int i=1;i<=count;++i)
        {
            const float f=float(i)/count;
            Point sample{a.x+(b.x-a.x)*f,a.y+(b.y-a.y)*f,previous.z},grounded;
            if (!floor(sample,grounded) || !Finite(grounded) || std::abs(grounded.z-previous.z)>18) return false;
            if (!clear(previous,grounded))
            {
                Point highA=previous,highB=grounded; highA.z+=18; highB.z+=18;
                if (!clear(previous,highA) || !clear(highA,highB) || !clear(highB,grounded)) return false;
            }
            previous=grounded;
        }
        return std::abs(previous.z-b.z)<=18;
    }
    // Segment must reject unsupported ground, excessive drops and blocked hulls.
    // No partial route is accepted: a detour must also connect to the destination.
    template<class Segment>
    bool Route(Point from, Point goal, Segment&& segment, Point& next)
    {
        if (!Finite(from) || !Finite(goal)) return false;
        if (segment(from,goal)) { next=goal; return true; }
        const float length=Distance(from,goal);
        if (length < 1) return false;
        const float nx=-(goal.y-from.y)/length, ny=(goal.x-from.x)/length;
        float best=std::numeric_limits<float>::max(); bool found=false;
        for (float offset : {64.f,-64.f,128.f,-128.f})
        {
            Point via{(from.x+goal.x)*.5f+nx*offset,(from.y+goal.y)*.5f+ny*offset,from.z};
            const float cost=Distance(from,via)+Distance(via,goal);
            if (cost < best && segment(from,via) && segment(via,goal)) { next=via; best=cost; found=true; }
        }
        return found;
    }
}
