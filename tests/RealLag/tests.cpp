#define NOMINMAX
#define REAL_LAG_TEST
#include "../../Nikogram/src/Features/PacketManip/RealLag/RealLag.cpp"
#include <iostream>
#include <stdexcept>
#include <thread>
int checks=0;
void Check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
int main()try
{
    using namespace RealLag;
    DiagnosticCounts d;
    Check(std::string(d.StallReason(0)).find("No sendto")!=std::string::npos,"diagnose missing send API");
    d.sends=3;Check(std::string(d.StallReason(0)).find("none match")!=std::string::npos,"diagnose endpoint filter");
    d.sendMatches=2;Check(std::string(d.StallReason(0)).find("No usable")!=std::string::npos,"diagnose rejected send header");
    d.sendSuccess=1;Check(std::string(d.StallReason(0)).find("No data")!=std::string::npos,"diagnose missing receive API");
    d.receiveData=1;Check(std::string(d.StallReason(0)).find("does not match")!=std::string::npos,"diagnose receive endpoint filter");
    d.receiveMatches=1;Check(std::string(d.StallReason(0)).find("no usable")!=std::string::npos,"diagnose receive header or socket");
    d.receiveHeaders=1;Check(std::string(d.StallReason(0)).find("no matching")!=std::string::npos,"diagnose unmatched acknowledgements");
    Check(std::string(d.StallReason(3)).find("Too few")!=std::string::npos,"diagnose partial sample collection");
    Check(std::string(d.StallReason(8)).find("stale")!=std::string::npos,"diagnose stale samples");
    Check(d.Snapshot().find("matching_ack_samples=")!=std::string::npos,"snapshot includes acknowledgement count");
    WirePing model;
    for(int i=0;i<8;i++){model.Send(i,i);model.Ack(i,i+.08);}
    Check(model.Ready(7.1),"eight valid wire samples required");
    Check(std::abs(model.HalfDelay(200,7.1)-60)<.01,"80 natural + 60 send + 60 receive = 200");
    for(int requested=0;requested<=80;requested++)Check(model.HalfDelay(requested,7.1)==0,"target below or equal natural adds nothing");
    model.Send(9,8);model.Ack(9,8.25);
    Check(model.HalfDelay(200,8.3)==0,"natural ping spike above target stops delay immediately");
    Check(model.HalfDelay(1000,11)==0,"stale samples fail open");
    model.Reset();Check(model.HalfDelay(1000,0)==0,"no artificial delay before measurements");
    for(int i=0;i<8;i++){model.Send(i,i);model.Ack(i,i+.04);}
    Check(model.HalfDelay(9999,7.1)<=480.01f,"target hard clamped at 1000");
    DelayQueue<int> queue;
    Check(queue.Push(1,1,0,100) && queue.Push(2,1,.01,5),"enqueue");
    Check(!queue.Pop(.099),"not delivered early");
    Check(queue.Pop(.1)==1 && queue.Pop(.1)==2,"preserve order across delay decrease");
    queue.Push(3,1,1,500);queue.Reduce(0);Check(queue.Pop(1)==3,"turn off releases pending traffic");
    for(int i=0;i<512;i++)Check(queue.Push(i,1,0,500),"bounded queue accepts capacity");
    Check(!queue.Push(513,1,0,500),"queue packet cap enforced");queue.Clear();Check(queue.Empty() && queue.Bytes()==0,"clear frees accounting");

    WSADATA ws;Check(WSAStartup(MAKEWORD(2,2),&ws)==0,"winsock startup");
    SOCKET client=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP),server=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    Check(bind(client,reinterpret_cast<sockaddr*>(&local),sizeof(local))==0,"client bind");
    Check(bind(server,reinterpret_cast<sockaddr*>(&local),sizeof(local))==0,"server bind");
    sockaddr_in clientAddr{},serverAddr{};int size=sizeof(local);
    getsockname(client,reinterpret_cast<sockaddr*>(&clientAddr),&size);getsockname(server,reinterpret_cast<sockaddr*>(&serverAddr),&size);
    u_long nonblock=1;ioctlsocket(client,FIONBIO,&nonblock);ioctlsocket(server,FIONBIO,&nonblock);
    sendOriginal=::sendto;recvOriginal=::recvfrom;closeOriginal=::closesocket;
    recording=true;counts={};
    endpoint=serverAddr;direct=available=true;gameSocket=client;target=200;halfDelay=60;rawDelayEnabled=true;
    // Seed an 80 ms natural RTT estimate. Socket test uses localhost only and no game/server traffic.
    double time=Now();for(int i=0;i<8;i++){ping.Send(100+i,time-.08);ping.Ack(100+i,time);}
    int32_t packet[3]={500,0,0};char buffer[128];sockaddr_in from{};int fromlen=sizeof(from);
    double start=Now();Check(Send(client,reinterpret_cast<char*>(packet),sizeof(packet),0,reinterpret_cast<sockaddr*>(&serverAddr),sizeof(serverAddr))==sizeof(packet),"outbound accepted into queue");
    Check(recvfrom(server,buffer,sizeof(buffer),0,reinterpret_cast<sockaddr*>(&from),&fromlen)==SOCKET_ERROR,"outbound not sent immediately");
    while(Now()-start<.065){Flush(Now());std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    Flush(Now());
    Check(recvfrom(server,buffer,sizeof(buffer),0,reinterpret_cast<sockaddr*>(&from),&fromlen)==sizeof(packet),"outbound delivered after half delay");
    packet[0]=600;packet[1]=999; // unknown ack must not contaminate seeded measurement
    sendto(server,reinterpret_cast<char*>(packet),sizeof(packet),0,reinterpret_cast<sockaddr*>(&clientAddr),sizeof(clientAddr));
    start=Now();fromlen=sizeof(from);
    Check(Receive(client,buffer,sizeof(buffer),0,reinterpret_cast<sockaddr*>(&from),&fromlen)==SOCKET_ERROR && WSAGetLastError()==WSAEWOULDBLOCK,"incoming held back");
    std::this_thread::sleep_for(std::chrono::milliseconds(65));fromlen=sizeof(from);
    Check(Receive(client,buffer,sizeof(buffer),0,reinterpret_cast<sockaddr*>(&from),&fromlen)==sizeof(packet),"incoming released after half delay");
    Check(memcmp(buffer,packet,sizeof(packet))==0,"incoming bytes preserved");
    target=20;halfDelay=ping.HalfDelay(target,Now());incoming.Reduce(halfDelay);outgoing.Reduce(halfDelay);
    Check(halfDelay==0,"below-natural target is zero in adapter");
    packet[0]=501;Check(Send(client,reinterpret_cast<char*>(packet),sizeof(packet),0,reinterpret_cast<sockaddr*>(&serverAddr),sizeof(serverAddr))==sizeof(packet),"low target outgoing passthrough");
    Check(recvfrom(server,buffer,sizeof(buffer),0,reinterpret_cast<sockaddr*>(&from),&fromlen)==sizeof(packet),"low target arrives without queue");
    packet[0]=601;sendto(server,reinterpret_cast<char*>(packet),sizeof(packet),0,reinterpret_cast<sockaddr*>(&clientAddr),sizeof(clientAddr));fromlen=sizeof(from);
    Check(Receive(client,buffer,sizeof(buffer),0,reinterpret_cast<sockaddr*>(&from),&fromlen)==sizeof(packet),"low target incoming passthrough");
    // Lowering the target releases queued packets in order, without waiting out
    // the old delay. This also exercises both paths' disable transition.
    halfDelay=400;target=900;
    packet[0]=502;Send(client,reinterpret_cast<char*>(packet),sizeof(packet),0,reinterpret_cast<sockaddr*>(&serverAddr),sizeof(serverAddr));
    packet[0]=503;Send(client,reinterpret_cast<char*>(packet),sizeof(packet),0,reinterpret_cast<sockaddr*>(&serverAddr),sizeof(serverAddr));
    Check(outgoing.Count()==2,"two outgoing packets retained");
    target=0;halfDelay=0;outgoing.Reduce(0);Flush(Now());
    fromlen=sizeof(from);Check(recvfrom(server,buffer,sizeof(buffer),0,reinterpret_cast<sockaddr*>(&from),&fromlen)==sizeof(packet),"first queued packet flushed");
    int32_t observed=0;memcpy(&observed,buffer,4);Check(observed==502,"first outgoing sequence preserved");
    fromlen=sizeof(from);Check(recvfrom(server,buffer,sizeof(buffer),0,reinterpret_cast<sockaddr*>(&from),&fromlen)==sizeof(packet),"second queued packet flushed");
    memcpy(&observed,buffer,4);Check(observed==503,"second outgoing sequence preserved");
    halfDelay=400;target=900;packet[0]=700;packet[1]=999;
    sendto(server,reinterpret_cast<char*>(packet),sizeof(packet),0,reinterpret_cast<sockaddr*>(&clientAddr),sizeof(clientAddr));
    fromlen=sizeof(from);Receive(client,buffer,sizeof(buffer),0,reinterpret_cast<sockaddr*>(&from),&fromlen);
    Check(incoming.Count()==1,"incoming packet retained");
    target=0;halfDelay=0;incoming.Reduce(0);fromlen=sizeof(from);
    Check(Receive(client,buffer,sizeof(buffer),0,reinterpret_cast<sockaddr*>(&from),&fromlen)==sizeof(packet),"disable releases incoming immediately");
    memcpy(&observed,buffer,4);Check(observed==700,"incoming release preserves payload");
    SOCKET unrelated=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    halfDelay=400;packet[0]=800;
    Check(Send(unrelated,reinterpret_cast<char*>(packet),sizeof(packet),0,reinterpret_cast<sockaddr*>(&serverAddr),sizeof(serverAddr))==sizeof(packet),"unrelated socket bypasses delay");
    fromlen=sizeof(from);Check(recvfrom(server,buffer,sizeof(buffer),0,reinterpret_cast<sockaddr*>(&from),&fromlen)==sizeof(packet),"unrelated traffic delivered immediately");
    closesocket(unrelated);
    Close(client);closesocket(server);WSACleanup();
    Check(incoming.Empty() && outgoing.Empty() && gameSocket==INVALID_SOCKET,"socket closure clears session state");
    Check(counts.sends>=5 && counts.sendMatches>=5 && counts.sendSuccess>=4,"send counters include matches and successful sends");
    Check(counts.receives>=4 && counts.receiveData>=3 && counts.receiveMatches>=3,"receive counters distinguish calls and received data");
    plainSendOriginal=[](SOCKET,const char*,int,int)->int{WSASetLastError(1234);return SOCKET_ERROR;};
    Check(ProbeSend(0,nullptr,0,0)==SOCKET_ERROR && WSAGetLastError()==1234,"send probe preserves return and error");
    wsaSendOriginal=[](SOCKET,LPWSABUF,DWORD,LPDWORD,DWORD,LPWSAOVERLAPPED,LPWSAOVERLAPPED_COMPLETION_ROUTINE)->int{WSASetLastError(WSA_IO_PENDING);return SOCKET_ERROR;};
    Check(ProbeWsaSend(0,nullptr,0,nullptr,0,nullptr,nullptr)==SOCKET_ERROR && WSAGetLastError()==WSA_IO_PENDING,"WSASend probe preserves pending operation");
    wsaSendToOriginal=[](SOCKET,LPWSABUF,DWORD,LPDWORD,DWORD,const sockaddr*,int,LPWSAOVERLAPPED,LPWSAOVERLAPPED_COMPLETION_ROUTINE)->int{WSASetLastError(1234);return SOCKET_ERROR;};
    {std::lock_guard lock(mutex);Check(ProbeWsaSendTo(0,nullptr,0,nullptr,0,nullptr,0,nullptr,nullptr)==SOCKET_ERROR && WSAGetLastError()==1234,"nested Winsock probe does not deadlock or change error");}
    wsaRecvFromOriginal=[](SOCKET,LPWSABUF,DWORD,LPDWORD,LPDWORD,sockaddr*,LPINT,LPWSAOVERLAPPED,LPWSAOVERLAPPED_COMPLETION_ROUTINE)->int{WSASetLastError(1234);return 0;};
    Check(ProbeWsaRecvFrom(0,nullptr,0,nullptr,nullptr,reinterpret_cast<sockaddr*>(1),reinterpret_cast<int*>(1),reinterpret_cast<LPWSAOVERLAPPED>(1),nullptr)==0 && WSAGetLastError()==1234,"async receive probe does not inspect receive buffers");
    Check(counts.sendCalls==1 && counts.wsaSendCalls==1 && counts.wsaSendToCalls==1 && counts.wsaRecvFromCalls==1,"alternate API counters recorded");
    counts={};fakePing.Reset();fakeGamePort=nullptr;rawDelayEnabled=false;
    SteamNetworkingIPAddr fakeAddress;fakeAddress.SetIPv4(ntohl(endpoint.sin_addr.s_addr),ntohs(endpoint.sin_port));
    struct TestSteamMessage:SteamNetworkingMessage_t {};
    static TestSteamMessage fakeMessage{};static SteamNetworkingMessage_t* fakePointer=&fakeMessage;
    static int releases=0,destroys=0;static bool argumentsOK=true;
    fakeSendOriginal=[](void* p,const SteamNetworkingIPAddr&,const void* data,uint32 bytes,int flags)->EResult
    {argumentsOK &= p==reinterpret_cast<void*>(0x11) && data && bytes==12 && flags==64;return k_EResultOK;};
    fakeReceiveOriginal=[](void*,SteamNetworkingMessage_t** out,int max)->int{if(max>0)out[0]=fakePointer;return max>0?1:0;};
    fakeDestroyOriginal=[](void*){++destroys;};
    fakeMessage.m_pfnRelease=[](SteamNetworkingMessage_t*){++releases;};
    fakeMessage.m_identityPeer.SetIPAddr(fakeAddress);fakeMessage.m_nFlags=17;
    int32_t fakePayload[3]={0,0,0};fakeMessage.m_pData=fakePayload;fakeMessage.m_cbSize=sizeof(fakePayload);
    void* port=reinterpret_cast<void*>(0x11);
    for(int i=0;i<8;++i)
    {
        fakePayload[0]=1000+i;
        Check(ProbeFakeSend(port,fakeAddress,fakePayload,sizeof(fakePayload),64)==k_EResultOK,"Steam send return preserved");
        fakePayload[0]=2000+i;fakePayload[1]=1000+i;SteamNetworkingMessage_t* returned=nullptr;
        Check(ProbeFakeReceive(port,&returned,1)==1 && returned==fakePointer,"Steam receive count and original pointer preserved");
    }
    Check(argumentsOK,"Steam send arguments forwarded intact");
    Check(fakePing.Samples()==8 && fakePing.Ready(Now()),"Steam FakeUDP samples match acknowledgements");
    Check(counts.fakeSends==8 && counts.fakeSendMatches==8 && counts.fakeReceiveMatches==8 && counts.fakeAcks==8,"Steam diagnostic counters");
    Check(releases==0 && fakeMessage.m_pData==fakePayload && fakeMessage.m_nFlags==17,"Steam messages remain untouched and owned by caller");
    fakeSendOriginal=[](void*,const SteamNetworkingIPAddr&,const void*,uint32,int)->EResult{return k_EResultFail;};
    Check(ProbeFakeSend(port,fakeAddress,fakePayload,12,0)==k_EResultFail && counts.fakeSendFailures==1,"Steam send failures preserved and counted");
    auto unrelatedAddress=fakeAddress;unrelatedAddress.m_port++;
    const auto matches=counts.fakeSendMatches;ProbeFakeSend(port,unrelatedAddress,fakePayload,12,0);
    Check(counts.fakeSendMatches==matches,"Steam peer filtering excludes unrelated endpoint");
    SteamNetworkingMessage_t* returned=nullptr;ProbeFakeReceive(reinterpret_cast<void*>(0x22),&returned,1);
    Check(counts.fakePortRejects==1,"other Steam port excluded from RTT matching");
    ProbeFakeDestroy(port);Check(destroys==1 && fakeGamePort==nullptr && fakePing.Samples()==0,"Steam port destruction clears measurement state");
    DiagnosticCounts steamReason;
    Check(std::string(steamReason.FakeStallReason(0)).find("no sends")!=std::string::npos,"Steam no-send diagnostic");
    steamReason.fakeSends=1;Check(std::string(steamReason.FakeStallReason(0)).find("none match")!=std::string::npos,"Steam endpoint diagnostic");
    Check(counts.Snapshot().find("FakeUDP_ack_samples=8")!=std::string::npos,"Steam counts serialized without identities or payload");
    // Exercise the actual Steam adapter with owned mock messages, not live game traffic.
    struct OwnedTestMessage:SteamNetworkingMessage_t {int32_t payload[3];OwnedTestMessage():SteamNetworkingMessage_t{},payload{} {}};
    static std::deque<SteamNetworkingMessage_t*> arrivals;
    static std::vector<int32_t> sentSequences;
    static int ownedReleases=0;static bool sendFailure=false;
    fakeSendOriginal=[](void* p,const SteamNetworkingIPAddr& remote,const void* data,uint32 bytes,int flags)->EResult
    {
        Check(p==reinterpret_cast<void*>(0x11) && remote.m_port==ntohs(endpoint.sin_port),"delayed send keeps port and destination");
        Check(bytes==12 && flags==64,"delayed send keeps size and flags");
        int32_t sequence;memcpy(&sequence,data,4);sentSequences.push_back(sequence);
        // State mutex must not be held across a transport call.
        std::thread observer([]{std::lock_guard lock(mutex);});observer.join();
        return sendFailure?k_EResultFail:k_EResultOK;
    };
    fakeReceiveOriginal=[](void*,SteamNetworkingMessage_t** out,int max)->int
    {
        std::thread observer([]{std::lock_guard lock(mutex);});observer.join();
        int n=0;while(n<max && !arrivals.empty()){out[n++]=arrivals.front();arrivals.pop_front();}return n;
    };
    auto arrive=[&](int sequence,int ack,bool matching=true)
    {
        auto* m=new OwnedTestMessage{};m->payload[0]=sequence;m->payload[1]=ack;
        m->m_pData=m->payload;m->m_cbSize=12;m->m_nFlags=17;
        m->m_identityPeer.SetIPAddr(matching?fakeAddress:unrelatedAddress);
        m->m_pfnRelease=[](SteamNetworkingMessage_t* p){++ownedReleases;delete static_cast<OwnedTestMessage*>(p);};
        arrivals.push_back(m);
    };
    auto seed=[&](int requested=200)
    {
        ResetFake();fault=false;stopped=false;recording=true;target=requested;fakeGamePort=port;
        const double now=Now();for(int i=0;i<8;++i){fakePing.Send(100+i,now-.04);fakePing.Ack(100+i,now);}
        ReduceFake();
    };
    seed();fakePayload[0]=3000;
    Check(ProbeFakeSend(port,fakeAddress,fakePayload,12,64)==k_EResultOK,"Steam delayed send accepted");
    fakePayload[0]=9999;
    Check(sentSequences.empty() && fakeOutgoing.Count()==1,"outbound owned copy held until deadline");
    std::this_thread::sleep_for(std::chrono::milliseconds(85));PumpFake(port);
    Check(sentSequences.size()==1 && sentSequences[0]==3000,"outbound copy delivered after delay");
    arrive(4000,3000);SteamNetworkingMessage_t* batch[64]{};
    Check(ProbeFakeReceive(port,batch,64)==0 && fakeIncoming.Count()==1,"incoming owned message held");
    Check(fakePing.Baseline()<45,"natural RTT excludes outbound queue wait");
    Check(ownedReleases==0,"held message not prematurely released");
    std::this_thread::sleep_for(std::chrono::milliseconds(90));
    Check(ProbeFakeReceive(port,batch,64)==1 && batch[0]->m_nFlags==17,"incoming released unchanged after delay");
    Check(static_cast<int32_t*>(batch[0]->m_pData)[0]==4000,"incoming payload preserved");batch[0]->Release();
    Check(ownedReleases==1 && fakePing.Baseline()<45,"caller owns delivered message and receive delay excluded from RTT");
    seed(20);fakePayload[0]=3001;
    ProbeFakeSend(port,fakeAddress,fakePayload,12,64);arrive(4001,999);
    Check(fakeHalfDelay==0 && fakeOutgoing.Empty() && sentSequences.back()==3001,"below natural ping outgoing has no delay");
    Check(ProbeFakeReceive(port,batch,64)==1 && fakeIncoming.Empty(),"below natural ping incoming has no delay");batch[0]->Release();
    seed();fakePayload[0]=3002;ProbeFakeSend(port,fakeAddress,fakePayload,12,64);
    fakePayload[0]=3003;ProbeFakeSend(port,fakeAddress,fakePayload,12,64);
    arrive(4002,999);arrive(4003,999);Check(ProbeFakeReceive(port,batch,64)==0,"two inbound packets held");
    target=0;recording=false;ReduceFake();
    Check(ProbeFakeReceive(port,batch,1)==1,"disable immediately releases first incoming packet");
    Check(static_cast<int32_t*>(batch[0]->m_pData)[0]==4002,"first queued incoming stays first");batch[0]->Release();
    Check(ProbeFakeReceive(port,batch,1)==1,"disable immediately releases second incoming packet");
    Check(static_cast<int32_t*>(batch[0]->m_pData)[0]==4003,"second queued incoming stays second");batch[0]->Release();
    Check(sentSequences[sentSequences.size()-2]==3002 && sentSequences.back()==3003,"disable flushes outgoing FIFO on next port poll");
    seed();arrive(4100,999);arrive(4101,999,false);
    Check(ProbeFakeReceive(port,batch,64)==1 && static_cast<int32_t*>(batch[0]->m_pData)[0]==4101,"unrelated peer bypasses delay");batch[0]->Release();
    const int beforeRelease=ownedReleases;const auto beforeSent=sentSequences.size();
    fakePayload[0]=3100;ProbeFakeSend(port,fakeAddress,fakePayload,12,64);
    ProbeFakeDestroy(port);PumpFake(port);
    Check(ownedReleases==beforeRelease+1 && fakeIncoming.Empty(),"destroy releases queued message exactly once");
    Check(sentSequences.size()==beforeSent && fakeOutgoing.Empty(),"destroy discards unsent packets without calling destroyed port");
    seed();fakePayload[0]=3200;ProbeFakeSend(port,fakeAddress,fakePayload,12,64);
    sendFailure=true;target=0;PumpFake(port);sendFailure=false;
    Check(fault && fakeHalfDelay==0 && fakeOutgoing.Empty(),"queued send failure disables artificial delay");
    seed();arrive(4200,999);ProbeFakeReceive(port,batch,64);
    const int beforeReset=ownedReleases;ResetFake();
    Check(ownedReleases==beforeReset+1 && fakeIncoming.Empty(),"connection reset releases retained messages");
    seed(1000);
    for(int i=0;i<512;++i)arrive(5000+i,999);
    for(int i=0;i<8;++i)Check(ProbeFakeReceive(port,batch,64)==0,"bounded incoming batches retained");
    Check(fakeIncoming.Count()==512,"incoming reaches safe packet cap");
    Check(ProbeFakeReceive(port,batch,64)==64 && fault && fakeHalfDelay==0,"full incoming queue fails open");
    for(int i=0;i<64;++i){Check(static_cast<int32_t*>(batch[i]->m_pData)[0]==5000+i,"overflow release preserves FIFO");batch[i]->Release();}
    ResetFake();Check(fakeIncoming.Empty() && arrivals.empty(),"overflow cleanup owns no pending messages");
    seed(1000);sentSequences.clear();
    for(int i=0;i<512;++i)
    {
        int32_t p[3]={6000+i,0,0};
        FakePacket q{port,fakeAddress,64,std::vector<char>(reinterpret_cast<char*>(p),reinterpret_cast<char*>(p)+12)};
        Check(fakeOutgoing.Push(std::move(q),12,Now(),480),"fill outbound queue to cap");
    }
    fakePayload[0]=6512;
    Check(ProbeFakeSend(port,fakeAddress,fakePayload,12,64)==k_EResultOK && fault,"outbound overflow releases old packets and accepts new packet");
    Check(sentSequences.size()==513 && fakeOutgoing.Empty(),"outbound overflow does not lose accepted packets");
    for(int i=0;i<513;++i)Check(sentSequences[i]==6000+i,"outbound overflow maintains FIFO");
    seed();fakePayload[0]=7000;ProbeFakeSend(port,fakeAddress,fakePayload,12,64);
    target=20;ReduceFake();PumpFake(port);
    Check(fakeHalfDelay==0 && fakeOutgoing.Empty() && sentSequences.back()==7000,"lower nonzero target flushes pending send");
    seed();fakePayload[0]=7001;ProbeFakeSend(port,fakeAddress,fakePayload,12,64);
    fakePing.Send(9900,Now()-.3);fakePing.Ack(9900,Now());ReduceFake();PumpFake(port);
    Check(fakeHalfDelay==0 && fakeOutgoing.Empty(),"natural RTT spike flushes pending Steam packets");
    seed();fakePayload[0]=3300;ProbeFakeSend(port,fakeAddress,fakePayload,12,64);arrive(4300,999);ProbeFakeReceive(port,batch,64);
    // A release callback can also acquire transport/state locks: it must run
    // after Shutdown has relinquished our state mutex.
    fakeIncoming.Reduce(0);auto releaseProbe=fakeIncoming.Pop(Now());
    (*releaseProbe)->m_pfnRelease=[](SteamNetworkingMessage_t* p)
    {std::thread observer([]{std::lock_guard lock(mutex);});observer.join();++ownedReleases;delete static_cast<OwnedTestMessage*>(p);};
    fakeIncoming.Push(std::move(*releaseProbe),12,Now(),80);
    const int beforeShutdown=ownedReleases;available=false;Shutdown();
    Check(ownedReleases==beforeShutdown+1 && fakeIncoming.Empty() && fakeOutgoing.Empty(),"shutdown releases incoming and discards unsent outgoing safely");
    std::cout<<checks<<" model, localhost UDP, and Steam adapter checks passed\n";
}
catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
