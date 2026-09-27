#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Statistics
{
    inline bool TrackingAllowed(bool accountMatches,bool inGame,bool demo,bool hasChannel,bool loopback,bool playback)
    {return accountMatches && inGame && !demo && hasChannel && !loopback && !playback;}
    struct AssignedTag {std::string name;int priority;bool targeting;bool manual=true;};
    inline std::string ManualPriorityLabel(const std::vector<AssignedTag>& tags,int effectivePriority)
    {
        if(effectivePriority<1)return {};
        int best=0;std::string result;
        for(const auto& t:tags)if(t.manual && t.targeting && t.priority>best){best=t.priority;result=t.name.empty()?"Unnamed priority":t.name;}
        return result;
    }
    enum Metric { Kills, Deaths, Assists, AssistedDeaths, DamageOut, DamageIn, Dominations,
        Dominated, Revenges, Revenged, Headshots, HeadshotDeaths, Backstabs, BackstabDeaths,
        CritDamageOut, CritDamageIn, MiniDamageOut, MiniDamageIn, PeakStreak, MetricCount };
    inline constexpr const char* Keys[MetricCount]={"kills","deaths","assists","assisted_deaths","damage_out","damage_in",
        "dominations","dominated","revenges","revenged","headshots","headshot_deaths","backstabs","backstab_deaths",
        "crit_damage_out","crit_damage_in","mini_damage_out","mini_damage_in","peak_streak"};
    inline constexpr const char* Labels[MetricCount]={"Kills","Deaths","Assists","Deaths they assisted","Damage dealt","Damage taken",
        "Dominations earned","Times dominated","Revenges earned","Revenges against you","Headshot kills","Headshot deaths",
        "Backstab kills","Backstab deaths","Critical damage dealt","Critical damage taken","Mini-critical damage dealt","Mini-critical damage taken","Highest killstreak"};
    struct Counts
    {
        std::array<uint64_t,MetricCount> n{};
        uint32_t observed=0;
        bool Known(int metric)const{return (observed & (1u<<metric))!=0;}
        void Add(const Counts& other)
        {observed|=other.observed;for(int i=0;i<MetricCount;++i)n[i]=i==PeakStreak?std::max(n[i],other.n[i]):n[i]+other.n[i];}
    };
    struct Record {std::string name;Counts counts;};
    struct View
    {
        Counts total;
        std::map<uint32_t,Record> players;
        std::map<std::string,Record> breakdowns;
        void Add(const Counts& delta,uint32_t peer,const std::string& name,bool eligible,
            const std::array<std::string,3>& keys,const std::array<std::string,3>& names)
        {
            total.Add(delta);
            if(eligible && peer){auto& r=players[peer];r.name=name;auto pair=delta;pair.n[PeakStreak]=0;r.counts.Add(pair);}
            for(int i=0;i<3;++i)if(!keys[i].empty()){auto& r=breakdowns[keys[i]];r.name=names[i];r.counts.Add(delta);}
        }
    };
    template<class Label> std::map<std::string,Counts> GroupPlayers(const View& view,Label labelFor)
    {
        std::map<std::string,Counts> groups;
        for(const auto& [id,r]:view.players)if(auto label=labelFor(id);!label.empty())groups[label].Add(r.counts);
        return groups;
    }
    struct Model
    {
        View match,session,lifetime;
        uint64_t streak=0;
        void NewMatch(){match={};streak=0;}
        void Add(Counts delta,uint32_t peer=0,const std::string& name={},bool eligible=false,
            const std::array<std::string,3>& keys={},const std::array<std::string,3>& names={})
        {
            if(delta.n[Deaths])streak=0;
            if(delta.n[Kills]){streak+=delta.n[Kills];delta.n[PeakStreak]=streak;}
            match.Add(delta,peer,name,eligible,keys,names);session.Add(delta,peer,name,eligible,keys,names);lifetime.Add(delta,peer,name,eligible,keys,names);
        }
    };
    struct Person {uint32_t id=0;std::string name;bool human=false,present=false,priority=false;};
    struct CombatEvent
    {
        enum Kind {Hurt,Death,Spawn} kind=Hurt;
        Person attacker,victim,assister;
        uint32_t local=0;
        int damage=0;
        bool feign=false,headshot=false,backstab=false,crit=false,mini=false;
        bool domination=false,revenge=false,assistDomination=false,assistRevenge=false;
        bool hasFlags=false,hasCustom=false,hasCrit=false,hasMini=false;
        std::array<std::string,3> keys{},names{};
    };
    // Shared by the live event adapter and the isolated tests. World/self deaths
    // count in Overview; bot interactions and Dead Ringer feigns do not.
    inline bool Apply(Model& model,const CombatEvent& e)
    {
        if(!e.local)return false;
        auto add=[&](const Counts& delta,const Person& peer)
        {model.Add(delta,peer.id,peer.name,peer.human && peer.priority,e.keys,e.names);};
        const bool victimLocal=e.victim.id==e.local,attackerLocal=e.attacker.id==e.local;
        if(e.kind==CombatEvent::Spawn){if(victimLocal)model.streak=0;return false;}
        Counts delta;
        if(e.kind==CombatEvent::Hurt)
        {
            if(e.attacker.id==e.victim.id || e.damage<=0)return false;
            const bool outgoing=attackerLocal && e.victim.human,incoming=victimLocal && e.attacker.human;
            if(!outgoing && !incoming)return false;
            delta.observed=(1u<<DamageOut)|(1u<<DamageIn);
            if(e.hasCrit)delta.observed|=(1u<<CritDamageOut)|(1u<<CritDamageIn);
            if(e.hasMini)delta.observed|=(1u<<MiniDamageOut)|(1u<<MiniDamageIn);
            delta.n[outgoing?DamageOut:DamageIn]=e.damage;
            if(e.hasCrit && e.crit)delta.n[outgoing?CritDamageOut:CritDamageIn]=e.damage;
            else if(e.hasMini && e.mini)delta.n[outgoing?MiniDamageOut:MiniDamageIn]=e.damage;
            add(delta,outgoing?e.victim:e.attacker);return true;
        }
        if(e.feign)return false;
        delta.observed=(1u<<Kills)|(1u<<Deaths)|(1u<<Assists)|(1u<<AssistedDeaths)|(1u<<PeakStreak);
        if(e.hasFlags)delta.observed|=(1u<<Dominations)|(1u<<Dominated)|(1u<<Revenges)|(1u<<Revenged);
        if(e.hasCustom)delta.observed|=(1u<<Headshots)|(1u<<HeadshotDeaths)|(1u<<Backstabs)|(1u<<BackstabDeaths);
        if(attackerLocal && !victimLocal && e.victim.human)
        {
            delta.n[Kills]=1;delta.n[Headshots]=e.headshot;delta.n[Backstabs]=e.backstab;
            delta.n[Dominations]=e.domination;delta.n[Revenges]=e.revenge;add(delta,e.victim);return true;
        }
        if(e.assister.id==e.local && !victimLocal && e.victim.human && e.attacker.human)
        {
            delta.n[Assists]=1;delta.n[Dominations]=e.assistDomination;delta.n[Revenges]=e.assistRevenge;add(delta,e.victim);return true;
        }
        if(victimLocal)
        {
            model.streak=0;
            if(e.attacker.present && !attackerLocal && !e.attacker.human)return false;
            delta.n[Deaths]=1;delta.n[HeadshotDeaths]=e.headshot;delta.n[BackstabDeaths]=e.backstab;
            delta.n[Dominated]=e.domination;delta.n[Revenged]=e.revenge;add(delta,attackerLocal?Person{}:e.attacker);
            if(e.assister.human && e.assister.id!=e.local && e.assister.id!=e.attacker.id)
            {
                Counts pair;pair.observed=delta.observed;pair.n[AssistedDeaths]=1;
                pair.n[Dominated]=e.assistDomination;pair.n[Revenged]=e.assistRevenge;add(pair,e.assister);
            }
            return true;
        }
        return false;
    }
}
