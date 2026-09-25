#include <winsock2.h>
#include <ws2tcpip.h>
#ifndef REAL_LAG_TEST
#include "../../../SDK/SDK.h"
#endif
#include "RealLag.h"
#include "DelayModel.h"
#include "Diagnostics.h"
#include "../../../SDK/Definitions/Steam/SteamNetworkingFakeIP.h"
#include <chrono>
#include <mutex>
#include <atomic>
#include <vector>
#include <filesystem>
#include <fstream>
#include <memory>
#pragma comment(lib,"Ws2_32.lib")

namespace RealLag
{
    namespace
    {
        using SendFn=int (WSAAPI*)(SOCKET,const char*,int,int,const sockaddr*,int);
        using RecvFn=int (WSAAPI*)(SOCKET,char*,int,int,sockaddr*,int*);
        using CloseFn=int (WSAAPI*)(SOCKET);
        SendFn sendOriginal=nullptr;RecvFn recvOriginal=nullptr;CloseFn closeOriginal=nullptr;
        using PlainSendFn=int (WSAAPI*)(SOCKET,const char*,int,int);
        using WsaSendFn=int (WSAAPI*)(SOCKET,LPWSABUF,DWORD,LPDWORD,DWORD,LPWSAOVERLAPPED,LPWSAOVERLAPPED_COMPLETION_ROUTINE);
        using WsaSendToFn=int (WSAAPI*)(SOCKET,LPWSABUF,DWORD,LPDWORD,DWORD,const sockaddr*,int,LPWSAOVERLAPPED,LPWSAOVERLAPPED_COMPLETION_ROUTINE);
        using WsaRecvFromFn=int (WSAAPI*)(SOCKET,LPWSABUF,DWORD,LPDWORD,LPDWORD,sockaddr*,LPINT,LPWSAOVERLAPPED,LPWSAOVERLAPPED_COMPLETION_ROUTINE);
        PlainSendFn plainSendOriginal=nullptr;WsaSendFn wsaSendOriginal=nullptr;
        WsaSendToFn wsaSendToOriginal=nullptr;WsaRecvFromFn wsaRecvFromOriginal=nullptr;
        DiagnosticCounts counts;
        bool recording=false,wasRecording=false;
        double diagnosticStart=0,lastLog=0;
        unsigned diagnosticSession=0;
        std::string diagnosticPath,hookReport;
        std::ofstream diagnosticLog;
        // Winsock may implement one observed API through another observed API.
        std::recursive_mutex mutex;
        std::atomic<bool> controlling=false;
        bool initialized=false,available=false,stopped=false,fault=false;
        std::string status="Off",address;
        sockaddr_in endpoint={};bool direct=false;
        SOCKET gameSocket=INVALID_SOCKET;
        const void* channel=nullptr;
        float connectedTime=0;
        int target=0;
        float halfDelay=0;
        bool rawDelayEnabled=false;
        WirePing ping;
        WirePing fakePing;
        void* fakeGamePort=nullptr;
        bool steamInitialized=false,steamAvailable=false;
        using FakeSendFn=EResult(__fastcall*)(void*,const SteamNetworkingIPAddr&,const void*,uint32,int);
        using FakeReceiveFn=int(__fastcall*)(void*,SteamNetworkingMessage_t**,int);
        using FakeDestroyFn=void(__fastcall*)(void*);
        FakeSendFn fakeSendOriginal=nullptr;FakeReceiveFn fakeReceiveOriginal=nullptr;FakeDestroyFn fakeDestroyOriginal=nullptr;
        struct Packet { SOCKET socket;int flags;sockaddr_in remote;std::vector<char> data; };
        DelayQueue<Packet> incoming,outgoing;
        struct FakePacket { void* port;SteamNetworkingIPAddr remote;int flags;std::vector<char> data; };
        struct ReleaseMessage { void operator()(SteamNetworkingMessage_t* p)const {if(p)p->Release();} };
        using OwnedMessage=std::unique_ptr<SteamNetworkingMessage_t,ReleaseMessage>;
        DelayQueue<FakePacket> fakeOutgoing;
        DelayQueue<OwnedMessage> fakeIncoming;
        float fakeHalfDelay=0;
        bool fakePumping=false;
        size_t fakeReserved=0;
        uint64_t fakeGeneration=0;
        void ReduceFake()
        {
            fakeHalfDelay=(!stopped && !fault)?fakePing.HalfDelay(target,
                std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count()):0;
            fakeIncoming.Reduce(fakeHalfDelay);fakeOutgoing.Reduce(fakeHalfDelay);
        }
        DelayQueue<OwnedMessage> ResetFake()
        {
            // Caller destroys this retired queue after dropping the state lock:
            // Steam owns each message's Release callback, too.
            auto retired=std::move(fakeIncoming);fakeIncoming={};
            ++fakeGeneration;fakeReserved=0;fakeOutgoing.Clear();
            fakePing.Reset();fakeGamePort=nullptr;fakeHalfDelay=0;
            return retired;
        }
        double Now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
        bool Matches(const sockaddr* remote,int size)
        {
            if(!direct || !remote || size<int(sizeof(sockaddr_in)) || remote->sa_family!=AF_INET)return false;
            const auto& a=*reinterpret_cast<const sockaddr_in*>(remote);
            return a.sin_addr.s_addr==endpoint.sin_addr.s_addr && a.sin_port==endpoint.sin_port;
        }
        bool MatchesFake(const SteamNetworkingIPAddr& remote)
        {return direct && remote.IsIPv4() && remote.GetIPv4()==ntohl(endpoint.sin_addr.s_addr) && remote.m_port==ntohs(endpoint.sin_port);}
        EResult SendFakeNow(void* port,const SteamNetworkingIPAddr& remote,const void* data,uint32 bytes,int flags)
        {
            bool match=false;uint64_t generation=0;
            {
                std::lock_guard lock(mutex);
                const bool observe=recording && !stopped;
                match=observe && MatchesFake(remote);generation=fakeGeneration;
            }
            // Never hold our lock across a Steam call: it can take its own
            // locks or invoke another observed networking path.
            const EResult result=fakeSendOriginal(port,remote,data,bytes,flags);
            std::lock_guard lock(mutex);
            if(match && !stopped && fakeGeneration==generation && MatchesFake(remote))
            {
                if(result!=k_EResultOK){++counts.fakeSendFailures;return result;}
                int32_t sequence=-1;if(data && bytes>=12)memcpy(&sequence,data,4);
                if(sequence>=0)
                {
                    if(!fakeGamePort)fakeGamePort=port;
                    if(fakeGamePort==port){fakePing.Send(uint32_t(sequence),Now());++counts.fakeSentHeaders;}
                    else ++counts.fakePortRejects;
                }
            }
            return result;
        }
        // Pump only inside the engine's Steam-port calls, not FRAME_START. This
        // keeps port access within the caller's existing destroy/lifetime rules.
        // No state lock is held while invoking a Steam transport method.
        void PumpFake(void* port)
        {
            {
                std::lock_guard lock(mutex);
                if(fakePumping || port!=fakeGamePort || stopped)return;
                fakePumping=true;
            }
            for(;;)
            {
                std::optional<FakePacket> packet;
                {
                    std::lock_guard lock(mutex);ReduceFake();
                    if(port==fakeGamePort && !stopped)packet=fakeOutgoing.Pop(Now());
                    if(!packet){fakePumping=false;return;}
                }
                if(SendFakeNow(packet->port,packet->remote,packet->data.data(),uint32(packet->data.size()),packet->flags)!=k_EResultOK)
                {std::lock_guard lock(mutex);fault=true;ReduceFake();}
            }
        }
        EResult __fastcall ProbeFakeSend(void* port,const SteamNetworkingIPAddr& remote,const void* data,uint32 bytes,int flags)
        {
            bool queued=false;
            {
                std::lock_guard lock(mutex);
                if(recording){++counts.fakeSends;if(MatchesFake(remote))++counts.fakeSendMatches;}
                ReduceFake();
                if(!stopped && port==fakeGamePort && MatchesFake(remote) && data && bytes>0 &&
                    bytes<=k_cbSteamNetworkingSocketsFakeUDPPortMaxMessageSize &&
                    !(flags & k_nSteamNetworkingSend_Reliable) &&
                    (fakeHalfDelay>0 || !fakeOutgoing.Empty() || fakePumping))
                {
                    FakePacket packet{port,remote,flags,std::vector<char>(static_cast<const char*>(data),static_cast<const char*>(data)+bytes)};
                    queued=fakeOutgoing.Push(std::move(packet),bytes,Now(),fakeHalfDelay);
                    if(!queued){fault=true;ReduceFake();}
                }
                else if(fakeOutgoing.Empty())queued=false;
            }
            PumpFake(port);
            if(queued)return k_EResultOK;
            {
                std::lock_guard lock(mutex);
                // Never let a new packet bypass older accepted packets on overflow.
                if(port==fakeGamePort && MatchesFake(remote) && (!fakeOutgoing.Empty() || fakePumping))return k_EResultLimitExceeded;
            }
            return SendFakeNow(port,remote,data,bytes,flags);
        }
        int __fastcall ProbeFakeReceive(void* port,SteamNetworkingMessage_t** messages,int maximum)
        {
            if(!messages || maximum<=0)return fakeReceiveOriginal(port,messages,maximum);
            PumpFake(port);
            int delivered=0,capacity=std::min(maximum,64);uint64_t generation;bool reserved=false;
            {
                std::lock_guard lock(mutex);generation=fakeGeneration;ReduceFake();
                if(port==fakeGamePort)
                {
                    if(fakeIncoming.Count()>=decltype(fakeIncoming)::MaxPackets ||
                        fakeIncoming.Bytes()+k_cbSteamNetworkingSocketsFakeUDPPortMaxMessageSize>decltype(fakeIncoming)::MaxBytes)
                    {fault=true;ReduceFake();}
                    while(delivered<maximum)
                    {auto p=fakeIncoming.Pop(Now());if(!p)break;messages[delivered++]=p->release();}
                    capacity=std::min(capacity,maximum-delivered);
                    capacity=std::min(capacity,int(decltype(fakeIncoming)::MaxPackets-fakeIncoming.Count()-fakeReserved));
                    capacity=std::min(capacity,int((decltype(fakeIncoming)::MaxBytes-fakeIncoming.Bytes())/k_cbSteamNetworkingSocketsFakeUDPPortMaxMessageSize-fakeReserved));
                    fakeReserved+=capacity;reserved=true;
                }
            }
            if(capacity<=0)return delivered;
            SteamNetworkingMessage_t* received[64]{};
            const int result=fakeReceiveOriginal(port,received,capacity);
            std::lock_guard lock(mutex);
            if(reserved && generation==fakeGeneration)fakeReserved-=capacity;
            if(recording)++counts.fakeReceiveCalls;
            if(result<=0)return delivered?delivered:result;
            for(int i=0;i<std::min(result,capacity);++i)
            {
                auto* message=received[i];if(!message)continue;
                if(recording)++counts.fakeMessages;
                const auto* remote=message->m_identityPeer.GetIPAddr();
                const bool match=!stopped && generation==fakeGeneration && remote && MatchesFake(*remote);
                if(match && recording)++counts.fakeReceiveMatches;
                if(match && port!=fakeGamePort && recording)++counts.fakePortRejects;
                if(match && port==fakeGamePort)
                {
                    if(recording && message->m_pData && message->m_cbSize>=12)
                    {
                        int32_t sequence,ack;memcpy(&sequence,message->m_pData,4);memcpy(&ack,static_cast<const char*>(message->m_pData)+4,4);
                        if(sequence>=0 && ack>=0)
                        {
                            ++counts.fakeReceivedHeaders;
                            const int before=fakePing.Samples();fakePing.Ack(uint32_t(ack),Now());counts.fakeAcks+=fakePing.Samples()-before;
                        }
                    }
                    ReduceFake();
                    if(fakeHalfDelay>0 || !fakeIncoming.Empty())
                    {
                        // FakeUDP guarantees a maximum 4096-byte message. Space
                        // for every requested message was reserved above.
                        if(!fakeIncoming.Push(OwnedMessage(message),size_t(std::max(0,message->m_cbSize)),Now(),fakeHalfDelay))
                        {fault=true;ReduceFake();} // RAII releases an invalid/oversized message.
                        continue;
                    }
                }
                messages[delivered++]=message;
            }
            if(port==fakeGamePort)while(delivered<maximum)
            {auto p=fakeIncoming.Pop(Now());if(!p)break;messages[delivered++]=p->release();}
            return delivered;
        }
        void __fastcall ProbeFakeDestroy(void* port)
        {
            DelayQueue<OwnedMessage> retired;
            {std::lock_guard lock(mutex);if(port==fakeGamePort){if(recording)++counts.fakeDestroyed;retired=ResetFake();}}
            retired.Clear();
            fakeDestroyOriginal(port);
        }
        // Source connectionless/split/compressed headers are negative. Only
        // ordinary sequenced datagrams can contribute an RTT sample.
        void Sent(const char* data,int len,double now)
        {
            if(recording)++counts.sendSuccess;
            if(len<12)return;int32_t seq;memcpy(&seq,data,4);if(seq>=0){ping.Send(uint32_t(seq),now);if(recording)++counts.sendHeaders;}
        }
        void Received(const char* data,int len,double now)
        {
            if(len<12)return;int32_t seq,ack;memcpy(&seq,data,4);memcpy(&ack,data+4,4);
            if(seq>=0 && ack>=0)
            {
                const int before=ping.Samples();ping.Ack(uint32_t(ack),now);
                if(recording){++counts.receiveHeaders;counts.acks+=ping.Samples()-before;}
            }
            halfDelay=(!fault && rawDelayEnabled)?ping.HalfDelay(target,now):0;
            incoming.Reduce(halfDelay);outgoing.Reduce(halfDelay);
        }
        void Flush(double now)
        {
            while(auto packet=outgoing.Pop(now))
            {
                const int result=sendOriginal(packet->socket,packet->data.data(),int(packet->data.size()),packet->flags,
                    reinterpret_cast<const sockaddr*>(&packet->remote),sizeof(sockaddr_in));
                if(result==int(packet->data.size()))Sent(packet->data.data(),result,Now());
                else {fault=true;halfDelay=0;incoming.Reduce(0);outgoing.Reduce(0);}
            }
        }
        int WSAAPI Send(SOCKET socket,const char* data,int len,int flags,const sockaddr* to,int tolen)
        {
            std::lock_guard lock(mutex);
            if(recording)
            {
                ++counts.sends;
                if(Matches(to,tolen)){++counts.sendMatches;if(gameSocket!=INVALID_SOCKET && socket!=gameSocket)++counts.sendSocketRejects;}
            }
            if(stopped || !Matches(to,tolen) || len<0 || (gameSocket!=INVALID_SOCKET && socket!=gameSocket))
                return sendOriginal(socket,data,len,flags,to,tolen);
            if(gameSocket==INVALID_SOCKET)
            {
                int32_t sequence=-1;if(data && len>=12)memcpy(&sequence,data,4);
                // A server-browser query to the same address is not the game socket.
                if(sequence<0){if(recording)++counts.sendHeaderRejects;return sendOriginal(socket,data,len,flags,to,tolen);}
            }
            gameSocket=socket;const double now=Now();Flush(now);
            if(halfDelay>0 && flags==0 && data && len>0)
            {
                Packet p{socket,flags,*reinterpret_cast<const sockaddr_in*>(to),std::vector<char>(data,data+len)};
                if(outgoing.Push(std::move(p),len,now,halfDelay))return len;
                fault=true;halfDelay=0;incoming.Reduce(0);outgoing.Reduce(0);Flush(now);
            }
            const int result=sendOriginal(socket,data,len,flags,to,tolen);
            if(result>=0)Sent(data,result,Now());return result;
        }
        int Deliver(Packet& p,char* data,int len,sockaddr* from,int* fromlen)
        {
            if(from && fromlen){const int size=std::min(*fromlen,int(sizeof(sockaddr_in)));memcpy(from,&p.remote,std::max(0,size));*fromlen=sizeof(sockaddr_in);}
            const int copied=std::min(len,int(p.data.size()));if(copied>0)memcpy(data,p.data.data(),copied);
            if(len<int(p.data.size())){WSASetLastError(WSAEMSGSIZE);return SOCKET_ERROR;}return copied;
        }
        int WSAAPI Receive(SOCKET socket,char* data,int len,int flags,sockaddr* from,int* fromlen)
        {
            {
                std::lock_guard lock(mutex);
                if(recording)++counts.receives;
                if(!stopped && socket==gameSocket && flags==0 && data && len>0 && from && fromlen && *fromlen>=int(sizeof(sockaddr_in)))
                {
                    if(incoming.Count()>=decltype(incoming)::MaxPackets || incoming.Bytes()+65536>decltype(incoming)::MaxBytes)
                    {fault=true;halfDelay=0;incoming.Reduce(0);outgoing.Reduce(0);}
                    Flush(Now());
                    if(auto packet=incoming.Pop(Now()))return Deliver(*packet,data,len,from,fromlen);
                }
            }
            const int result=recvOriginal(socket,data,len,flags,from,fromlen);
            const int error=WSAGetLastError();
            if(result<=0){WSASetLastError(error);return result;}
            std::lock_guard lock(mutex);
            if(recording)
            {
                ++counts.receiveData;
                if(fromlen && Matches(from,*fromlen)){++counts.receiveMatches;if(socket!=gameSocket)++counts.receiveSocketRejects;}
            }
            if(stopped || socket!=gameSocket || flags!=0 || !fromlen || !Matches(from,*fromlen))return result;
            const double now=Now();Received(data,result,now);Flush(now);
            if(halfDelay<=0 && incoming.Empty())return result;
            Packet p{socket,0,*reinterpret_cast<sockaddr_in*>(from),std::vector<char>(data,data+result)};
            // recv queues cannot be discarded or reordered on overflow. The
            // bounded queue normally drains first above; disable future delay.
            if(!incoming.Push(p,result,now,halfDelay))
            {
                fault=true;halfDelay=0;incoming.Reduce(0);outgoing.Reduce(0);
                auto oldest=incoming.Pop(now);
                // One UDP datagram fits the bounded queue after making room.
                if(oldest){incoming.Push(std::move(p),result,now,0);return Deliver(*oldest,data,len,from,fromlen);}
                return result;
            }
            if(auto packet=incoming.Pop(now))return Deliver(*packet,data,len,from,fromlen);
            WSASetLastError(WSAEWOULDBLOCK);return SOCKET_ERROR;
        }
        int WSAAPI Close(SOCKET socket)
        {
            std::lock_guard lock(mutex);
            if(socket==gameSocket){incoming.Clear();outgoing.Clear();ping.Reset();gameSocket=INVALID_SOCKET;halfDelay=0;}
            return closeOriginal(socket);
        }
        // Alternate APIs are observation-only. Never inspect WSABUF payloads or
        // asynchronous receive buffers, and preserve Winsock return/error state.
        int WSAAPI ProbeSend(SOCKET s,const char* b,int n,int f)
        { {std::lock_guard lock(mutex);if(recording)++counts.sendCalls;}return plainSendOriginal(s,b,n,f); }
        int WSAAPI ProbeWsaSend(SOCKET s,LPWSABUF b,DWORD n,LPDWORD sent,DWORD f,LPWSAOVERLAPPED o,LPWSAOVERLAPPED_COMPLETION_ROUTINE c)
        { {std::lock_guard lock(mutex);if(recording)++counts.wsaSendCalls;}return wsaSendOriginal(s,b,n,sent,f,o,c); }
        int WSAAPI ProbeWsaSendTo(SOCKET s,LPWSABUF b,DWORD n,LPDWORD sent,DWORD f,const sockaddr* to,int len,LPWSAOVERLAPPED o,LPWSAOVERLAPPED_COMPLETION_ROUTINE c)
        {
            {std::lock_guard lock(mutex);if(recording){++counts.wsaSendToCalls;if(Matches(to,len))++counts.wsaSendToMatches;}}
            return wsaSendToOriginal(s,b,n,sent,f,to,len,o,c);
        }
        int WSAAPI ProbeWsaRecvFrom(SOCKET s,LPWSABUF b,DWORD n,LPDWORD received,LPDWORD f,sockaddr* from,LPINT len,LPWSAOVERLAPPED o,LPWSAOVERLAPPED_COMPLETION_ROUTINE c)
        {
            {std::lock_guard lock(mutex);if(recording)++counts.wsaRecvFromCalls;}
            const int result=wsaRecvFromOriginal(s,b,n,received,f,from,len,o,c);const int error=WSAGetLastError();
            if(result==0 && !o){std::lock_guard lock(mutex);if(recording && len && Matches(from,*len))++counts.wsaRecvFromMatches;}
            WSASetLastError(error);return result;
        }
#ifndef REAL_LAG_TEST
        void InitializeSteamProbes()
        {
            if(steamInitialized)return;steamInitialized=true;
            auto module=GetModuleHandleA("steam_api64.dll");
            if(!module){hookReport+="SteamFakeUDP=module_unavailable ";return;}
            auto getUser=reinterpret_cast<HSteamUser(*)()>(GetProcAddress(module,"SteamAPI_GetHSteamUser"));
            auto find=reinterpret_cast<void*(*)(HSteamUser,const char*)>(GetProcAddress(module,"SteamInternal_FindOrCreateUserInterface"));
            if(!getUser || !find || !getUser()){hookReport+="SteamFakeUDP=accessor_unavailable ";return;}
            auto sockets=static_cast<ISteamNetworkingSockets*>(find(getUser(),STEAMNETWORKINGSOCKETS_INTERFACE_VERSION));
            if(!sockets){hookReport+="SteamFakeUDP=interface012_unavailable ";return;}
            // -1 creates a distinct, local ephemeral client port. No peer is
            // contacted. Destroy only this owned probe object, never engine ports.
            auto probe=sockets->CreateFakeUDPPort(-1);
            if(!probe){hookReport+="SteamFakeUDP=probe_port_unavailable ";return;}
            auto table=*reinterpret_cast<void***>(probe);
            void* destroy=table[0];void* send=table[1];void* receive=table[2];
            probe->DestroyFakeUDPPort();
            auto create=[&](void* function,void* detour,void** original,const char* name)
            {auto result=MH_CreateHook(function,detour,original);hookReport+=std::string(name)+"="+MH_StatusToString(result)+" ";return result==MH_OK;};
            if(!create(send,reinterpret_cast<void*>(ProbeFakeSend),reinterpret_cast<void**>(&fakeSendOriginal),"FakeUDP_send"))return;
            if(!create(receive,reinterpret_cast<void*>(ProbeFakeReceive),reinterpret_cast<void**>(&fakeReceiveOriginal),"FakeUDP_receive")){MH_RemoveHook(send);return;}
            if(!create(destroy,reinterpret_cast<void*>(ProbeFakeDestroy),reinterpret_cast<void**>(&fakeDestroyOriginal),"FakeUDP_destroy")){MH_RemoveHook(send);MH_RemoveHook(receive);return;}
            steamAvailable=MH_EnableHook(send)==MH_OK && MH_EnableHook(receive)==MH_OK && MH_EnableHook(destroy)==MH_OK;
            if(!steamAvailable){MH_DisableHook(send);MH_DisableHook(receive);MH_DisableHook(destroy);}
            hookReport+=steamAvailable?"SteamFakeUDP_enabled=1 ":"SteamFakeUDP_enabled=0 ";
        }
        void ProbeHook(HMODULE module,const char* name,void* detour,void** original)
        {
            auto address=reinterpret_cast<void*>(GetProcAddress(module,name));
            auto created=MH_CreateHook(address,detour,original);
            auto enabled=created==MH_OK?MH_EnableHook(address):created;
            hookReport+=std::string(name)+"="+MH_StatusToString(enabled)+" ";
        }
        void Initialize()
        {
            if(initialized)return;initialized=true;
            auto module=GetModuleHandleA("ws2_32.dll");if(!module){hookReport="ws2_32 module unavailable";return;}
            void* send=reinterpret_cast<void*>(GetProcAddress(module,"sendto"));
            void* recv=reinterpret_cast<void*>(GetProcAddress(module,"recvfrom"));
            void* close=reinterpret_cast<void*>(GetProcAddress(module,"closesocket"));
            auto create=[&](void* address,void* detour,void** original,const char* name)
            {auto result=MH_CreateHook(address,detour,original);hookReport+=std::string(name)+"="+MH_StatusToString(result)+" ";return result==MH_OK;};
            if(!create(send,reinterpret_cast<void*>(Send),reinterpret_cast<void**>(&sendOriginal),"create_sendto"))return;
            if(!create(recv,reinterpret_cast<void*>(Receive),reinterpret_cast<void**>(&recvOriginal),"create_recvfrom")){MH_RemoveHook(send);return;}
            if(!create(close,reinterpret_cast<void*>(Close),reinterpret_cast<void**>(&closeOriginal),"create_closesocket")){MH_RemoveHook(send);MH_RemoveHook(recv);return;}
            available=MH_EnableHook(send)==MH_OK && MH_EnableHook(recv)==MH_OK && MH_EnableHook(close)==MH_OK;
            if(!available){MH_DisableHook(send);MH_DisableHook(recv);MH_DisableHook(close);}
            hookReport+=available?"core_sendto_recvfrom_close=OK ":"core_sendto_recvfrom_close=FAILED ";
            ProbeHook(module,"send",reinterpret_cast<void*>(ProbeSend),reinterpret_cast<void**>(&plainSendOriginal));
            ProbeHook(module,"WSASend",reinterpret_cast<void*>(ProbeWsaSend),reinterpret_cast<void**>(&wsaSendOriginal));
            ProbeHook(module,"WSASendTo",reinterpret_cast<void*>(ProbeWsaSendTo),reinterpret_cast<void**>(&wsaSendToOriginal));
            ProbeHook(module,"WSARecvFrom",reinterpret_cast<void*>(ProbeWsaRecvFrom),reinterpret_cast<void**>(&wsaRecvFromOriginal));
        }
#endif
    }
    bool Controlling(){return controlling.load();}
    std::string Status(){std::lock_guard lock(mutex);return status;}
    std::string DiagnosticPath(){std::lock_guard lock(mutex);return diagnosticPath;}
#ifndef REAL_LAG_TEST
    void Update()
    {
        const int desired=std::clamp(Vars::Backtrack::RealLag.Value,0,1000);
        // Hook activation must not hold our mutex while MinHook suspends threads.
        if(desired>0 && !initialized)Initialize();
        if(desired>0 && !steamInitialized)InitializeSteamProbes();
        DelayQueue<OwnedMessage> retired;
        std::lock_guard lock(mutex);if(stopped)return;
        recording=desired>0;
        auto net=I::EngineClient->GetNetChannelInfo();
        const std::string remote=net && net->GetAddress()?net->GetAddress():"";
        const float timeConnected=net?net->GetTimeConnected():0;
        const bool connectionChanged=channel!=net || address!=remote || timeConnected<connectedTime;
        if(channel!=net || address!=remote || !net || net->IsPlayback() || timeConnected<connectedTime)
        {
            channel=net;address=remote;incoming.Clear();outgoing.Clear();ping.Reset();retired=ResetFake();halfDelay=0;fault=false;gameSocket=INVALID_SOCKET;direct=false;
            const auto colon=remote.rfind(':');
            if(net && !net->IsLoopback() && !net->IsPlayback() && colon!=std::string::npos)
            {
                const auto host=remote.substr(0,colon);char* end=nullptr;long port=strtol(remote.c_str()+colon+1,&end,10);
                endpoint={};endpoint.sin_family=AF_INET;
                if(end && *end==0 && port>0 && port<=65535 && InetPtonA(AF_INET,host.c_str(),&endpoint.sin_addr)==1)
                {endpoint.sin_port=htons(static_cast<u_short>(port));direct=true;}
            }
        }
        connectedTime=timeConnected;
        if(recording && (!wasRecording || connectionChanged))
        {
            if(wasRecording && diagnosticLog && diagnosticLog.tellp()<262144)diagnosticLog<<"connection_changed previous "<<counts.Snapshot()<<"\n";
            diagnosticStart=Now();counts={};ping.Reset();fakePing.Reset();++diagnosticSession;lastLog=0;
            if(!diagnosticLog.is_open())
            {
                try
                {
                    const auto folder=std::filesystem::current_path()/"Nikogram"/"Core";
                    std::filesystem::create_directories(folder);
                    const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
                    auto path=folder/("real-lag-steam-delay-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(stamp)+".log");
                    diagnosticLog.open(path,std::ios::out);diagnosticPath=diagnosticLog?path.string():"Could not create diagnostic log.";
                    if(diagnosticLog)diagnosticLog<<"Real lag Steam FakeUDP delay | target is total RTT; separate send/receive queues\n"
                        <<"Privacy: counts/timings only; no payloads, addresses, player IDs, credentials or chat.\n"
                        <<"API totals are process-wide while enabled; endpoint matches are counted separately.\n"
                        <<"Async WSARecvFrom completions are not inspected. Nested API calls may appear in multiple totals.\n"
                        <<"Hooks: "<<hookReport<<"\n";
                }
                catch(...){diagnosticPath="Could not create diagnostic log.";}
            }
        }
        if(!recording && wasRecording && diagnosticLog){diagnosticLog<<"session="<<diagnosticSession<<" disabled "<<counts.Snapshot()<<"\n";diagnosticLog.flush();}
        wasRecording=recording;
        // Delay features must not stack: leave the user's existing values intact
        // and ask them to turn conflicting mechanisms off explicitly.
        const bool conflict=Vars::Backtrack::Latency.Value>0 || Vars::Fakelag::Fakelag.Value!=0;
        auto engineLag=H::ConVars.FindVar("net_fakelag");
        const bool external=engineLag && engineLag->GetFloat()>0;
        const bool timingOut=net && net->IsTimingOut();
        const bool enabled=desired>0 && direct && steamAvailable && !conflict && !external && !fault && !timingOut;
        target=enabled?desired:0;
        ReduceFake();
        // This release activates only the runtime-confirmed Steam transport.
        // Keep the older raw-UDP adapter observation-only to avoid double delay.
        halfDelay=0;
        incoming.Reduce(halfDelay);outgoing.Reduce(halfDelay);if(available)Flush(Now());
        controlling=enabled && fakeHalfDelay>0;
        const bool steamFakeAddress=direct && I::SteamNetworkingUtils && I::SteamNetworkingUtils->IsFakeIPv4(ntohl(endpoint.sin_addr.s_addr));
        if(!desired)status="Off";
        else if(!steamAvailable)status="Unavailable: Steam packet hooks could not be initialized.";
        else if(!net)status="Waiting for a server connection.";
        else if(!direct)status="Unsupported server transport; no added delay.";
        else if(conflict || external)status="Turn off Fake latency, Fakelag and net_fakelag first.";
        else if(fault)status="Delay stopped after a socket/queue error; reconnect to reset.";
        else if(timingOut)status="Connection is timing out; no additional delay.";
        else if(fakePing.Ready(Now()))status=std::format("Natural ~{:.0f} ms | target {} ms | +{:.0f} ms outgoing / +{:.0f} ms incoming",fakePing.Baseline(),desired,fakeHalfDelay,fakeHalfDelay);
        else if(steamAvailable && (steamFakeAddress || counts.fakeSends))status=Now()-diagnosticStart>=10?std::string("No added delay: ")+counts.FakeStallReason(fakePing.Samples()):"Measuring natural ping on Steam FakeUDP; no added delay yet.";
        else status="Waiting for supported Steam FakeUDP traffic; no added delay.";
        if(recording && diagnosticLog && Now()-lastLog>=5 && diagnosticLog.tellp()<262144)
        {
            lastLog=Now();
            diagnosticLog<<"session="<<diagnosticSession<<" elapsed_s="<<Now()-diagnosticStart
                <<" requested_ms="<<desired<<" connected="<<(net!=nullptr)<<" direct_ipv4="<<direct
                <<" steam_fake_address="<<steamFakeAddress<<" FakeUDP_probe_available="<<steamAvailable
                <<" FakeUDP_port_identified="<<(fakeGamePort!=nullptr)<<" FakeUDP_samples="<<fakePing.Samples()
                <<" FakeUDP_sample_age_s="<<fakePing.SampleAge(Now())<<" FakeUDP_rtt_ms="<<fakePing.Baseline()
                <<" outgoing_delay_ms="<<fakeHalfDelay<<" incoming_delay_ms="<<fakeHalfDelay
                <<" outgoing_queued="<<fakeOutgoing.Count()<<" incoming_queued="<<fakeIncoming.Count()
                <<" game_socket_identified="<<(gameSocket!=INVALID_SOCKET)<<" conflict="<<(conflict||external)
                <<" timing_out="<<timingOut<<" fault="<<fault<<" samples="<<ping.Samples()
                <<" sample_age_s="<<ping.SampleAge(Now())<<" baseline_ms="<<ping.Baseline()
                <<" "<<counts.Snapshot()<<"\nstatus="<<status<<"\n";
            diagnosticLog.flush();
        }
    }
#endif
    void Shutdown()
    {
        DelayQueue<OwnedMessage> retired;
        std::lock_guard lock(mutex);stopped=true;controlling=false;halfDelay=0;target=0;
        recording=false;
        if(diagnosticLog){diagnosticLog<<"unload "<<counts.Snapshot()<<"\n";diagnosticLog.close();}
        outgoing.Reduce(0);if(available)Flush(Now());incoming.Clear();ping.Reset();
        // Do not invoke an engine-owned Steam port from the unload thread.
        // Discard unsent datagrams and release every retained Steam message.
        retired=ResetFake();
    }
}
