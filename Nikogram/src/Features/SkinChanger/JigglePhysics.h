#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <span>
#include <cstdint>

namespace SkinJiggle
{
    // On-disk studio jiggle parameters, not a borrowed engine object/vtable.
    struct Params
    {
        int flags;
        float length,tipMass,yawStiffness,yawDamping,pitchStiffness,pitchDamping,alongStiffness,alongDamping;
        float angleLimit,minYaw,maxYaw,yawFriction,yawBounce,minPitch,maxPitch,pitchFriction,pitchBounce;
        float baseMass,baseStiffness,baseDamping,baseMinLeft,baseMaxLeft,baseLeftFriction;
        float baseMinUp,baseMaxUp,baseUpFriction,baseMinForward,baseMaxForward,baseForwardFriction;
        float boingImpactSpeed,boingImpactAngle,boingDampingRate,boingFrequency,boingAmplitude;
    };
    static_assert(sizeof(Params)==140);
    inline bool Valid(const Params& p)
    {
        std::array<float,34> fields{};std::memcpy(fields.data(),reinterpret_cast<const char*>(&p)+4,sizeof(fields));
        for(float f:fields)if(!std::isfinite(f)||std::abs(f)>100000)return false;
        // Installed Botkillers use flexible tips and/or constrained base springs.
        // Unsupported procedural types are left at their safe resting pose.
        return p.flags>0&&!(p.flags&~127)&&(p.flags&67)&&p.length>0&&p.length<1000&&p.tipMass>=0
            &&p.yawStiffness>=0&&p.pitchStiffness>=0&&p.alongStiffness>=0
            &&p.yawDamping>=0&&p.pitchDamping>=0&&p.alongDamping>=0
            &&p.baseMass>=0&&p.baseStiffness>=0&&p.baseDamping>=0
            &&(!(p.flags&16)||(p.angleLimit>=0&&p.angleLimit<=3.141593f))
            &&(!(p.flags&4)||p.minYaw<=p.maxYaw)&&(!(p.flags&8)||p.minPitch<=p.maxPitch)
            &&(!(p.flags&64)||(p.baseMinLeft<=p.baseMaxLeft&&p.baseMinUp<=p.baseMaxUp&&p.baseMinForward<=p.baseMaxForward));
    }
    inline bool Read(std::span<const char> model,int64_t boneOffset,int procedureOffset,Params& out)
    {
        const int64_t offset=boneOffset+int64_t(procedureOffset);
        if(procedureOffset<=0||offset<0||uint64_t(offset)>model.size()||sizeof(Params)>model.size()-size_t(offset))return false;
        std::memcpy(&out,model.data()+offset,sizeof(out));return Valid(out);
    }
    struct Vec
    {
        float x=0,y=0,z=0;
        Vec operator+(Vec b)const{return {x+b.x,y+b.y,z+b.z};}
        Vec operator-(Vec b)const{return {x-b.x,y-b.y,z-b.z};}
        Vec operator*(float f)const{return {x*f,y*f,z*f};}
        float Dot(Vec b)const{return x*b.x+y*b.y+z*b.z;}
        Vec Cross(Vec b)const{return {y*b.z-z*b.y,z*b.x-x*b.z,x*b.y-y*b.x};}
        float Length()const{return std::sqrt(Dot(*this));}
        Vec Unit(Vec fallback)const{float l=Length();return l>1e-6f?*this*(1/l):fallback;}
        bool Finite()const{return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z);}
    };
    using Matrix=std::array<std::array<float,4>,3>;
    template<typename M> Vec Column(const M& m,int col){return {m[0][col],m[1][col],m[2][col]};}
    template<typename M> void SetColumn(M& m,int col,Vec v){m[0][col]=v.x;m[1][col]=v.y;m[2][col]=v.z;}
    struct State
    {
        bool initialized=false;int frame=-1;double time=0;
        Vec tip{},tipVelocity{},base{},baseVelocity{},lastGoal{};
        Matrix cached{};
        template<typename M> bool Build(const Params& p,const M& goal,M& output,double now,int newFrame)
        {
            if(!Valid(p)||!std::isfinite(now))return false;
            const Vec origin=Column(goal,3),left=Column(goal,0),up=Column(goal,1),forward=Column(goal,2);
            if(!origin.Finite()||!left.Finite()||!up.Finite()||!forward.Finite())return false;
            for(Vec axis:{left,up,forward})if(std::abs(axis.Length()-1)>.05f)return false;
            if(std::abs(left.Dot(up))>.05f||std::abs(left.Dot(forward))>.05f||std::abs(up.Dot(forward))>.05f)return false;
            if(initialized&&frame==newFrame&&now==time)
            {for(int r=0;r<3;++r)for(int c=0;c<4;++c)output[r][c]=cached[r][c];return true;}
            double dt=now-time;
            if(!initialized||dt<0||dt>.1||(origin-lastGoal).Length()>128)
            {tip=origin+forward*p.length;tipVelocity={};base=origin;baseVelocity={};dt=0;initialized=true;}
            // Stable bounded substeps, independent of the number of render passes.
            int steps=std::max(1,int(std::ceil(dt*240)));float h=float(dt/steps);
            for(int step=0;step<steps&&h>0;++step)
            {
                if(p.flags&3)
                {
                    Vec acceleration{0,0,-p.tipMass};
                    if(p.flags&1)
                    {
                        Vec error=origin+forward*p.length-tip;
                        acceleration=acceleration+left*(p.yawStiffness*left.Dot(error)-p.yawDamping*left.Dot(tipVelocity))
                            +up*(p.pitchStiffness*up.Dot(error)-p.pitchDamping*up.Dot(tipVelocity));
                        if(!(p.flags&32))acceleration=acceleration+forward*(p.alongStiffness*forward.Dot(error)-p.alongDamping*forward.Dot(tipVelocity));
                    }
                    tipVelocity=tipVelocity+acceleration*h;tip=tip+tipVelocity*h;
                    Vec along=tip-origin;
                    // Project against the authored yaw/pitch planes, removing
                    // velocity at the limit rather than reflecting into a blowup.
                    if(p.flags&4)
                    {
                        float angle=std::atan2(left.Dot(along),forward.Dot(along)),limit=std::clamp(angle,p.minYaw,p.maxYaw);
                        if(angle!=limit){Vec axis=left*std::sin(limit)+forward*std::cos(limit);along=up*up.Dot(along)+axis*axis.Dot(along);tipVelocity={};}
                    }
                    if(p.flags&8)
                    {
                        float angle=std::atan2(up.Dot(along),forward.Dot(along)),limit=std::clamp(angle,p.minPitch,p.maxPitch);
                        if(angle!=limit){Vec axis=up*std::sin(limit)+forward*std::cos(limit);along=left*left.Dot(along)+axis*axis.Dot(along);tipVelocity={};}
                    }
                    Vec direction=along.Unit(forward);
                    if(p.flags&16)
                    {
                        float dot=std::clamp(direction.Dot(forward),-1.f,1.f);
                        if(dot<std::cos(p.angleLimit))
                        {Vec tangent=(direction-forward*dot).Unit(left);direction=forward*std::cos(p.angleLimit)+tangent*std::sin(p.angleLimit);}
                    }
                    tip=origin+direction*((p.flags&32)?p.length:along.Length());
                    if(p.flags&32)tipVelocity=tipVelocity-direction*tipVelocity.Dot(direction);
                }
                if(p.flags&64)
                {
                    Vec acceleration=(origin-base)*p.baseStiffness-baseVelocity*p.baseDamping+Vec{0,0,-p.baseMass};
                    baseVelocity=baseVelocity+acceleration*h;Vec next=base+baseVelocity*h-origin;
                    float x=std::clamp(left.Dot(next),p.baseMinLeft,p.baseMaxLeft);
                    float y=std::clamp(up.Dot(next),p.baseMinUp,p.baseMaxUp);
                    float z=std::clamp(forward.Dot(next),p.baseMinForward,p.baseMaxForward);
                    Vec constrained=origin+left*x+up*y+forward*z;
                    baseVelocity=(constrained-base)*(1/h);base=constrained;
                }
            }
            if(!tip.Finite()||!tipVelocity.Finite()||!base.Finite()||!baseVelocity.Finite())
            {initialized=false;return false;}
            for(int r=0;r<3;++r)for(int c=0;c<4;++c)cached[r][c]=goal[r][c];
            if(p.flags&3)
            {
                Vec direction=(tip-origin).Unit(forward);
                float handedness=left.Cross(up).Dot(forward)<0?-1.f:1.f;
                Vec side=(up.Cross(direction)*handedness).Unit(left);
                Vec vertical=direction.Cross(side)*handedness;
                SetColumn(cached,0,side);SetColumn(cached,1,vertical);SetColumn(cached,2,direction);
            }
            if(p.flags&64)SetColumn(cached,3,base);
            frame=newFrame;time=now;lastGoal=origin;
            for(int r=0;r<3;++r)for(int c=0;c<4;++c)output[r][c]=cached[r][c];
            return true;
        }
    };
}
