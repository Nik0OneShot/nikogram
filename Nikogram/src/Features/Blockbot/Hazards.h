#pragma once
#include "Model.h"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <cctype>

namespace BlockbotHazards
{
    using BlockbotModel::Point;
    using Entity=std::unordered_map<std::string,std::string>;
    struct Box { Point min,max; };
    inline bool Parse(std::string_view text,std::vector<Entity>& entities)
    {
        size_t pos=0; bool bad=false; entities.clear();
        auto token=[&](std::string& out)->bool
        {
            out.clear();
            while(pos<text.size())
            {
                if(std::isspace(static_cast<unsigned char>(text[pos]))) { ++pos; continue; }
                if(text.substr(pos,2)=="//") { while(pos<text.size()&&text[pos]!='\n')++pos;continue; }
                break;
            }
            if(pos>=text.size())return false;
            char c=text[pos++];
            if(c=='{'||c=='}') {out=c;return true;}
            if(c!='"'){bad=true;return false;}
            while(pos<text.size())
            {
                c=text[pos++];
                if(c=='"')return true;
                if(c=='\\'&&pos<text.size()&&(text[pos]=='"'||text[pos]=='\\'))c=text[pos++];
                out+=c;
            }
            bad=true; return false;
        };
        std::string key,value;
        while(pos<text.size())
        {
            if(!token(key))return !bad && pos==text.size()&&!entities.empty();
            if(key!="{")return false;
            Entity entity; bool closed=false;
            while(token(key))
            {
                if(key=="}"){closed=true;break;}
                if(key=="{"||!token(value)||value=="{"||value=="}")return false;
                // Duplicate outputs are retained for conservative kill-output detection.
                if(entity.contains(key))entity[key]+="\n"+value;else entity[key]=value;
            }
            if(!closed)return false;
            entities.push_back(std::move(entity));
        }
        return !entities.empty();
    }
    inline bool Dangerous(const Entity& e)
    {
        auto it=e.find("classname");
        if(it==e.end())return false;
        if(it->second=="trigger_hurt"||it->second=="trigger_teleport")return true;
        if(!it->second.starts_with("trigger_"))return false;
        for(const auto& [key,value]:e)
        {
            if(!key.starts_with("On"))continue;
            std::string lower=value;
            for(auto& c:lower)c=char(std::tolower(static_cast<unsigned char>(c)));
            // Arbitrary trigger outputs may chain into server-side kill logic.
            // Treat all output-bearing trigger volumes as unknown hazards for drops.
            if(!lower.empty())return true;
        }
        return false;
    }
    inline bool Intersects(Point start,Point end,Box box,Point mins,Point maxs)
    {
        float enter=0,leave=1;
        const float a[]={start.x,start.y,start.z},b[]={end.x,end.y,end.z};
        const float lo[]={box.min.x-maxs.x,box.min.y-maxs.y,box.min.z-maxs.z};
        const float hi[]={box.max.x-mins.x,box.max.y-mins.y,box.max.z-mins.z};
        for(int i=0;i<3;++i)
        {
            const float d=b[i]-a[i];
            if(std::abs(d)<.0001f){if(a[i]<lo[i]||a[i]>hi[i])return false;continue;}
            float t1=(lo[i]-a[i])/d,t2=(hi[i]-a[i])/d;
            if(t1>t2)std::swap(t1,t2);
            enter=std::max(enter,t1);leave=std::min(leave,t2);
            if(enter>leave)return false;
        }
        return true;
    }
}
