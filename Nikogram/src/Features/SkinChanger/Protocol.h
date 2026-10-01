#pragma once
#include "Model.h"
#include <span>

namespace SkinProtocol
{
    constexpr int Channel=0x4e47;
    constexpr size_t MaxPacketBytes=4096;
    constexpr double DiscoverySeconds=10,PeerTimeoutSeconds=30,StyleRefreshSeconds=3,StyleMinSeconds=.2;
    inline bool NativeContextAllowed(int user,int pipe,uint32_t app)
    {return user>0&&pipe>0&&app==440;}
    inline const char* TransportStateName(int state)
    {
        switch(state){case 0:return "none";case 1:return "connecting";case 2:return "finding_route";
        case 3:return "connected";case 4:return "closed_by_peer";case 5:return "problem_detected";
        case -1:return "fin_wait";case -2:return "linger";case -3:return "dead";default:return "unknown";}
    }
    // Replicated human roster + connection user IDs, not endpoint addresses.
    // Host loopback, LAN and public-address clients must agree. This is a
    // context discriminator, not authentication; Steam identity, membership
    // and nonce checks remain mandatory. Join/leave/rejoin invalidates it.
    inline uint64_t SessionContext(std::string_view map,std::vector<std::pair<uint64_t,int>> roster)
    {
        if(map.empty()||map.size()>512||roster.empty()||roster.size()>128)return 0;
        std::sort(roster.begin(),roster.end());
        for(size_t n=0;n<roster.size();++n)
            if(!roster[n].first||roster[n].second<=0||(n&&roster[n-1].first==roster[n].first))return 0;
        uint64_t h=14695981039346656037ull;
        auto byte=[&](uint8_t b){h=(h^b)*1099511628211ull;};
        for(unsigned char c:std::string_view("nikogram-roster-context-v93"))byte(c);
        for(unsigned char c:map)byte(c);
        byte(0);byte(uint8_t(roster.size()));
        for(const auto& [id,user]:roster)
        {
            for(int n=0;n<8;++n)byte(uint8_t(id>>(8*n)));
            for(int n=0;n<4;++n)byte(uint8_t(uint32_t(user)>>(8*n)));
        }
        return h?h:1;
    }
    inline bool PacketSizeAllowed(size_t size){return size>=38&&size<=MaxPacketBytes;}
    inline bool NetworkAllowed(bool cosmetics,bool networking,bool share,bool receive,bool inGame,bool unloading)
    {return cosmetics&&networking&&(share||receive)&&inGame&&!unloading;}
    inline const char* NetworkWaitReason(bool cosmetics,bool networking,bool share,bool receive,bool inGame,bool unloading)
    {
        if(unloading)return "unloading";
        if(!cosmetics)return "skin_changer_disabled";
        if(!networking)return "networking_disabled";
        if(!share&&!receive)return "both_directions_disabled";
        if(!inGame)return "not_in_game";
        return "active";
    }
    enum Type {Hello=1,Ack=2,Style=3,Withdraw=4,PlayerAppearance=5,EquipmentStyle=6};
    inline bool HasSelection(int type){return type==Style||type==EquipmentStyle;}
    inline bool PlayerAppearanceAllowed(bool networking,bool receive,bool ready,bool sharing,int claimedClass,int actualClass,double now,double seen)
    {return networking&&receive&&ready&&sharing&&actualClass>=1&&actualClass<=9&&claimedClass==actualClass
        &&std::isfinite(now)&&std::isfinite(seen)&&now>=seen&&now-seen<=10;}
    struct Message
    {
        int type=Hello,flags=0,weapon=0,cls=0;
        int streak=-1,user=0;uint32_t life=0;
        uint64_t server=0,nonce=0,echo=0;
        SkinModel::Selection selection;
        bool pipBoy=false,authenticAnimations=true;
    };
    inline std::vector<uint8_t> Encode(const Message& m)
    {
        std::vector<uint8_t> out={'N','I','K','O','S','K','I','N',1};
        if(HasSelection(m.type)&&m.selection.unusual)out[8]=2;
        if(m.type==Style&&m.streak>=0)out[8]=3;
        auto put=[&](uint64_t v,int bytes){for(int n=0;n<bytes;++n)out.push_back(uint8_t(v>>(8*n)));};
        put(m.type,1);put(m.flags,1);put(m.server,8);put(m.nonce,8);put(m.echo,8);put(m.weapon,2);put(m.cls,1);
        if(m.type==PlayerAppearance)put(int(m.pipBoy)|(int(m.authenticAnimations)<<1),1);
        if(HasSelection(m.type))
        {
            const auto& s=m.selection;
            put(int(s.enabled)|(int(s.australium)<<1)|(int(s.festive)<<2)|(int(s.festivized)<<3),1);
            put(s.reskin,2);put(s.finish,2);put(s.seed,4);put(s.tier,1);put(s.sheen,1);put(s.effect,2);
            put(uint32_t(std::lround(s.wear*10000)),2);
            if(out[8]>=2)put(s.unusual,2);
            if(out[8]==3){put(m.streak,4);put(m.life,4);put(m.user,4);}
            // Preview counts are deliberately local-only, never received or advertised.
        }
        return out;
    }
    inline std::optional<Message> Decode(std::span<const uint8_t> data)
    {
        constexpr std::array<uint8_t,9> header={'N','I','K','O','S','K','I','N',1};
        if(!PacketSizeAllowed(data.size())||!std::equal(header.begin(),header.begin()+8,data.begin())||data[8]<1||data[8]>3)return {};
        size_t pos=9;auto get=[&](int count){uint64_t v=0;for(int n=0;n<count;++n)v|=uint64_t(data[pos++])<<(8*n);return v;};
        Message m;m.type=int(get(1));m.flags=int(get(1));
        if(m.type<Hello||m.type>EquipmentStyle||m.flags>3||data.size()!=(HasSelection(m.type)?(data[8]==3?67u:data[8]==2?55u:53u):m.type==PlayerAppearance?39u:38u)||(data[8]>=2&&!HasSelection(m.type))||(m.type==EquipmentStyle&&data[8]==3))return {};
        m.server=get(8);m.nonce=get(8);m.echo=get(8);m.weapon=int(get(2));m.cls=int(get(1));
        if(!m.server||!m.nonce||m.cls>9)return {};
        if(m.type==PlayerAppearance)
        {
            const auto bits=get(1);if(bits>3||m.weapon!=0||m.cls<1||(bits&1&&m.cls!=9))return {};
            m.pipBoy=bits&1;m.authenticAnimations=bits&2;
        }
        if(HasSelection(m.type))
        {
            auto& s=m.selection;int bits=int(get(1));if(bits>15)return {};
            s.enabled=bits&1;s.australium=bits&2;s.festive=bits&4;s.festivized=bits&8;
            s.reskin=int(get(2));s.finish=int(get(2));s.seed=int(get(4));s.tier=int(get(1));s.sheen=int(get(1));s.effect=int(get(2));s.wear=float(get(2))/10000.f;
            if(data[8]>=2)s.unusual=int(get(2));
            if(data[8]==3)
            {
                const auto count=get(4),life=get(4),user=get(4);
                if(count>1000000||!life||!user||user>0x7fffffffu)return {};
                m.streak=int(count);m.life=uint32_t(life);m.user=int(user);
            }
            if(m.cls<1||!s.Valid())return {};
        }
        return m;
    }
    struct Peer
    {
        uint64_t local=0,remote=0;bool ready=false;int flags=0;
        double lastHello=-100,lastSeen=0,lastSend=-100,lastReply=-100;
        std::vector<uint8_t> lastPayload;
        std::vector<uint8_t> lastAppearance;
        double lastAppearanceSend=-100;
        std::map<int,std::pair<std::vector<uint8_t>,double>> equipment;
        bool EquipmentDue(int weapon,std::span<const uint8_t> payload,double now)const
        {
            if(!ready||!std::isfinite(now)||now<lastSeen||now-lastSeen>=PeerTimeoutSeconds)return false;
            auto it=equipment.find(weapon);if(it==equipment.end())return true;
            const auto& [last,time]=it->second;
            return now-time>=StyleMinSeconds&&(last.size()!=payload.size()||!std::equal(payload.begin(),payload.end(),last.begin())||now-time>=StyleRefreshSeconds);
        }
        void SentEquipment(int weapon,std::span<const uint8_t> payload,double now)
        {if(equipment.size()>=8&&!equipment.contains(weapon))equipment.clear();equipment[weapon]={std::vector<uint8_t>(payload.begin(),payload.end()),now};}
        bool AppearanceDue(std::span<const uint8_t> payload,double now)const
        {return ready&&std::isfinite(now)&&now>=lastSeen&&now-lastSeen<PeerTimeoutSeconds&&now-lastAppearanceSend>=StyleMinSeconds
            &&(lastAppearance.size()!=payload.size()||!std::equal(payload.begin(),payload.end(),lastAppearance.begin())||now-lastAppearanceSend>=StyleRefreshSeconds);}
        void SentAppearance(std::span<const uint8_t> payload,double now)
        {lastAppearance.assign(payload.begin(),payload.end());lastAppearanceSend=now;}
        bool DiscoveryDue(double now)const{return std::isfinite(now)&&now-lastHello>=DiscoverySeconds;}
        bool Expire(double now)
        {
            if(!ready||!std::isfinite(now)||now<lastSeen||now-lastSeen<PeerTimeoutSeconds)return false;
            ready=false;remote=0;flags=0;lastPayload.clear();lastAppearance.clear();equipment.clear();lastAppearanceSend=-100;lastHello=-100;lastSend=-100;lastReply=-100;return true;
        }
        bool StyleDue(std::span<const uint8_t> payload,double now)const
        {
            return ready&&std::isfinite(now)&&now>=lastSeen&&now-lastSeen<PeerTimeoutSeconds&&now-lastSend>=StyleMinSeconds
                &&(lastPayload.size()!=payload.size()||!std::equal(payload.begin(),payload.end(),lastPayload.begin())||now-lastSend>=StyleRefreshSeconds);
        }
        void SentStyle(std::span<const uint8_t> payload,double now)
        {lastPayload.assign(payload.begin(),payload.end());lastSend=now;}
        std::optional<SkinProtocol::Message> Accept(const Message& m,uint64_t server,double now,int ownFlags)
        {
            if(!local||m.server!=server||!m.nonce||!std::isfinite(now)||now<lastSeen)return {};
            if(m.type==Hello)
            {if(remote!=m.nonce){ready=false;lastPayload.clear();lastSend=-100;lastAppearance.clear();lastAppearanceSend=-100;equipment.clear();}remote=m.nonce;flags=m.flags;lastSeen=now;
                Message reply;reply.type=Ack;reply.server=server;reply.nonce=local;reply.echo=remote;reply.flags=ownFlags;return reply;}
            if(m.type==Ack && m.echo==local && (!remote||remote==m.nonce))
            {
                bool first=!ready;remote=m.nonce;ready=true;flags=m.flags;lastSeen=now;
                if(first){Message reply;reply.type=Ack;reply.server=server;reply.nonce=local;reply.echo=remote;reply.flags=ownFlags;return reply;}
            }
            return {};
        }
        bool CanReceive(const Message& m,uint64_t server,double now) const
        {return (HasSelection(m.type)||m.type==Withdraw||m.type==PlayerAppearance) && ready && m.server==server && m.nonce==remote && m.echo==local && std::isfinite(now) && now>=lastSeen && now-lastSeen<PeerTimeoutSeconds && ((flags&1)||m.type==Withdraw);}
    };
    // Read-only explanations: never bypass or change the protocol's validation.
    inline const char* RejectReason(const Peer& p,const Message& m,uint64_t server,double now)
    {
        if(m.server!=server)return "server_context_mismatch";
        if(!p.local||!m.nonce)return "missing_nonce";
        if(!std::isfinite(now)||now<p.lastSeen)return "invalid_time";
        if(m.type==Hello)return "none";
        if(m.type==Ack)
        {
            if(m.echo!=p.local)return "nonce_echo_mismatch";
            if(p.remote&&m.nonce!=p.remote)return "remote_nonce_mismatch";
            return "none";
        }
        if(!p.ready)return "handshake_not_ready";
        if(m.nonce!=p.remote)return "remote_nonce_mismatch";
        if(m.echo!=p.local)return "nonce_echo_mismatch";
        if(now-p.lastSeen>=PeerTimeoutSeconds)return "peer_timed_out";
        if((HasSelection(m.type)||m.type==PlayerAppearance)&&!(p.flags&1))return "sender_sharing_disabled";
        return "none";
    }
}
