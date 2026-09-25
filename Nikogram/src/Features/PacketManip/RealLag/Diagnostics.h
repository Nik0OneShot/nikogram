#pragma once
#include <cstdint>
#include <sstream>
#include <string>
namespace RealLag
{
    struct DiagnosticCounts
    {
        uint64_t sends=0,sendMatches=0,sendSocketRejects=0,sendHeaderRejects=0,sendSuccess=0,sendHeaders=0;
        uint64_t receives=0,receiveData=0,receiveMatches=0,receiveSocketRejects=0,receiveHeaders=0,acks=0;
        uint64_t sendCalls=0,wsaSendCalls=0,wsaSendToCalls=0,wsaSendToMatches=0,wsaRecvFromCalls=0,wsaRecvFromMatches=0;
        uint64_t fakeSends=0,fakeSendMatches=0,fakeSendFailures=0,fakeSentHeaders=0,fakeReceiveCalls=0,fakeMessages=0,fakeReceiveMatches=0,fakeReceivedHeaders=0,fakeAcks=0,fakePortRejects=0,fakeDestroyed=0;
        std::string Snapshot()const
        {
            std::ostringstream s;
            s<<"sendto_calls="<<sends<<" sendto_endpoint_matches="<<sendMatches
             <<" sendto_other_socket="<<sendSocketRejects<<" sendto_rejected_initial_header="<<sendHeaderRejects
             <<" sendto_success="<<sendSuccess<<" sent_sequence_headers="<<sendHeaders
             <<" recvfrom_calls="<<receives<<" recvfrom_data="<<receiveData<<" recvfrom_endpoint_matches="<<receiveMatches
             <<" recvfrom_other_socket="<<receiveSocketRejects<<" received_sequence_headers="<<receiveHeaders
             <<" matching_ack_samples="<<acks<<" send_calls="<<sendCalls<<" WSASend_calls="<<wsaSendCalls
             <<" WSASendTo_calls="<<wsaSendToCalls<<" WSASendTo_endpoint_matches="<<wsaSendToMatches
             <<" WSARecvFrom_calls="<<wsaRecvFromCalls<<" WSARecvFrom_endpoint_matches="<<wsaRecvFromMatches
             <<" FakeUDP_send_calls="<<fakeSends<<" FakeUDP_send_matches="<<fakeSendMatches
             <<" FakeUDP_send_failures="<<fakeSendFailures<<" FakeUDP_sent_headers="<<fakeSentHeaders
             <<" FakeUDP_receive_calls="<<fakeReceiveCalls<<" FakeUDP_messages="<<fakeMessages
             <<" FakeUDP_receive_matches="<<fakeReceiveMatches<<" FakeUDP_received_headers="<<fakeReceivedHeaders
             <<" FakeUDP_ack_samples="<<fakeAcks<<" FakeUDP_wrong_port="<<fakePortRejects<<" FakeUDP_port_destroyed="<<fakeDestroyed;
            return s.str();
        }
        const char* StallReason(int samples)const
        {
            if(!sends)return "No sendto calls observed; connection may use another send API.";
            if(!sendMatches)return "Send calls observed, but none match the server endpoint.";
            if(!sendSuccess)return "No usable game-socket sends; check initial-header / socket rejection counts.";
            if(!receiveData)return "No data returned through recvfrom; connection may use another receive API.";
            if(!receiveMatches)return "Received data does not match the server endpoint.";
            if(!receiveHeaders)return "Matching receive traffic has no usable game-socket sequence headers.";
            if(!samples)return "Sequence headers observed, but no matching acknowledgements.";
            if(samples<8)return "Too few matching acknowledgements (need eight).";
            return "Acknowledgement samples stopped arriving or are stale.";
        }
        const char* FakeStallReason(int samples)const
        {
            if(!fakeSends)return "Steam FakeUDP probe installed, but no sends observed.";
            if(!fakeSendMatches)return "Steam FakeUDP sends observed, but none match the server endpoint.";
            if(!fakeSentHeaders)return "Steam FakeUDP endpoint matched; no successful sequenced sends.";
            if(!fakeMessages)return "Steam FakeUDP sends matched, but no received messages observed.";
            if(!fakeReceiveMatches)return "Steam FakeUDP messages received; none match the server endpoint.";
            if(!fakeReceivedHeaders)return "Steam FakeUDP endpoint matched; receive headers / port not usable.";
            if(!samples)return "Steam FakeUDP headers observed; acknowledgements did not match.";
            if(samples<8)return "Steam FakeUDP collecting acknowledgements (need eight).";
            return "Steam FakeUDP acknowledgement samples are stale.";
        }
    };
}
