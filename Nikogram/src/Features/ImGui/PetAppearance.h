#pragma once
#include "PetMotion.h"
#include "PetSprites.h"
namespace NikoPet
{
    inline int CharacterFrame(const PetMotion::Pet& pet,int character)
    {
        using PetMotion::Mode;
        if(character==0 || character==3)return pet.Frame();
        switch(pet.mode)
        {
        case Mode::Climb: if(character==1)return 13+int(pet.animation*5)%2;break;
        case Mode::Sit:return character==1?(int(pet.animation*.5f)%2?16:15):11;
        case Mode::Sleep:return character==1?17:30;
        case Mode::Drag:if(std::abs(pet.vx)+std::abs(pet.vy)<=180)return 5;break;
        default:break;
        }
        return pet.Frame();
    }
    struct SpriteRect { float left,top,right,bottom; };
    inline SpriteRect CharacterRect(const PetMotion::Pet& pet,int character,int frame,const PetMotion::World& world,float size)
    {
        const auto& a=GetAsset(frame,character);const float scale=size/128.f;
        const float width=(a.right-a.left)*scale,height=(a.bottom-a.top)*scale;
        const bool hanging=pet.mode==PetMotion::Mode::Hang || pet.mode==PetMotion::Mode::UnderHang;
        const float top=hanging?HangSpriteTop(frame,size,pet.y,character):pet.y-height;
        // Feet use each frame's opaque bottom, accounting for transparent margins.
        float left=pet.x-width*.5f;
        if(character==1 || character==2)
        {
            const float foot=FootAnchorX(character);
            left=pet.x-(pet.faceRight?float(a.right)-foot:foot-float(a.left))*scale;
        }
        if(pet.mode==PetMotion::Mode::Climb)
            for(const auto& r:world.windows)if(r.id==pet.support)
            {left=ClimbSpriteLeft(frame,size,pet.climbRight?r.right:r.left,!pet.climbRight,character);break;}
        return {left,top,left+width,top+height};
    }
}
