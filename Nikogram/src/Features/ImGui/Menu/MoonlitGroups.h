#pragma once
// Pure editing operations. Stored order remains unchanged: the last group wins.
// Every activation map (including bind overrides) must follow the same edit.
namespace MoonlitGroups
{
    inline uint32_t SwapMask(uint32_t mask, int a, int b)
    {
        if(a<0||a>=32||b<0||b>=32)return mask;
        if(((mask>>a)^(mask>>b))&1u)mask^=(1u<<a)|(1u<<b);
        return mask;
    }
    inline uint32_t RemoveMask(uint32_t mask,int index)
    {
        if(index<0||index>=32)return mask;
        return (mask&((1u<<index)-1u))|(index<31?(mask>>(index+1))<<index:0u)|(1u<<31);
    }
    inline std::string UniqueName(const std::vector<Group_t>& groups,std::string base)
    {
        const auto first=base.find_first_not_of(" \t\r\n");
        base=first==std::string::npos?"New group":base.substr(first,base.find_last_not_of(" \t\r\n")-first+1);
        auto exists=[&](const std::string& s){return std::any_of(groups.begin(),groups.end(),[&](const auto& g){return g.m_sName==s;});};
        std::string name=base;for(int n=2;exists(name);++n)name=base+" ("+std::to_string(n)+")";
        return name;
    }
    template<class Remap> bool Add(std::vector<Group_t>& groups,int& selected,Group_t group,Remap remap,int copyActive=-1)
    {
        if(groups.size()>=32)return false;
        group.m_sName=UniqueName(groups,group.m_sName);
        const int index=int(groups.size());groups.push_back(std::move(group));selected=index;
        remap([=](uint32_t mask){const bool enabled=copyActive<0||(copyActive<32&&(mask&(1u<<copyActive)));return enabled?mask|(1u<<index):mask&~(1u<<index);});
        return true;
    }
    template<class Remap> bool Move(std::vector<Group_t>& groups,int& selected,int direction,Remap remap)
    {
        // Positive means visually up (towards the end of the stored array).
        const int to=selected+direction;
        if((direction!=1&&direction!=-1)||selected<0||selected>=int(groups.size())||to<0||to>=int(groups.size())||selected>=32||to>=32)return false;
        const int from=selected;std::swap(groups[from],groups[to]);selected=to;
        remap([=](uint32_t mask){return SwapMask(mask,from,to);});return true;
    }
    template<class Remap> bool Remove(std::vector<Group_t>& groups,int& selected,Remap remap)
    {
        if(selected<0||selected>=int(groups.size()))return false;
        const int old=selected;groups.erase(groups.begin()+old);selected=std::min(old,int(groups.size())-1);
        remap([=](uint32_t mask){return RemoveMask(mask,old);});return true;
    }
    inline std::string LayerSummary(const std::vector<std::pair<std::string,Color_t>>& layers)
    {
        std::string out;for(const auto& layer:layers){if(!out.empty())out+=" + ";out+=layer.first;}return out.empty()?"None":out;
    }
    inline void AddLayer(std::vector<std::pair<std::string,Color_t>>& layers,const std::string& name)
    {
        if(std::any_of(layers.begin(),layers.end(),[&](const auto& p){return p.first==name;}))return;
        auto layer=std::make_pair(name,Color_t(255,255,255,255));
        if(name=="Original")layers.insert(layers.begin(),layer);else layers.push_back(layer);
    }
}
