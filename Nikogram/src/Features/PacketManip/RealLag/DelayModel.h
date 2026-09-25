#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>

namespace RealLag
{
    // Wire-to-wire RTT: timestamp after a successful send, and before incoming
    // packets enter our delay queue. Neither artificial half-delay is included.
    class WirePing
    {
        struct Sent { uint32_t sequence; double time; };
        std::deque<Sent> sent;
        double lastSample=-1;
        float baseline=0;
        int samples=0;
    public:
        void Reset(){sent.clear();lastSample=-1;baseline=0;samples=0;}
        void Send(uint32_t sequence,double now)
        {
            sent.push_back({sequence,now});
            while(sent.size()>2048 || (!sent.empty() && now-sent.front().time>5))sent.pop_front();
        }
        void Ack(uint32_t sequence,double now)
        {
            for(auto it=sent.begin();it!=sent.end();++it)if(it->sequence==sequence)
            {
                const float ms=float((now-it->time)*1000);
                if(ms>=0 && ms<=5000)
                {
                    // Rise immediately; decay slowly so a spike cannot add delay
                    // while the observed natural ping is above the chosen target.
                    baseline=samples?std::max(ms,baseline+(ms-baseline)*.1f):ms;
                    lastSample=now;++samples;
                }
                sent.erase(sent.begin(),std::next(it));return;
            }
        }
        bool Ready(double now)const{return samples>=8 && lastSample>=0 && now-lastSample<2;}
        float Baseline()const{return baseline;}
        int Samples()const{return samples;}
        double SampleAge(double now)const{return lastSample<0?-1:now-lastSample;}
        float HalfDelay(int target,double now)const
        {
            if(!Ready(now))return 0;
            return std::max(0.f,float(std::clamp(target,0,1000))-baseline)*.5f;
        }
    };

    template<class T> class DelayQueue
    {
        struct Item { double arrived,due;size_t bytes;T value; };
        std::deque<Item> items;
        size_t bytes=0;
    public:
        static constexpr size_t MaxPackets=512,MaxBytes=2*1024*1024;
        bool Push(T value,size_t size,double now,float delayMs)
        {
            if(items.size()>=MaxPackets || bytes+size>MaxBytes)return false;
            double due=now+std::max(0.f,delayMs)/1000.;
            if(!items.empty())due=std::max(due,items.back().due);
            items.push_back({now,due,size,std::move(value)});bytes+=size;return true;
        }
        std::optional<T> Pop(double now)
        {
            if(items.empty() || items.front().due>now)return {};
            T value=std::move(items.front().value);bytes-=items.front().bytes;items.pop_front();return value;
        }
        void Reduce(float delayMs)
        {
            double previous=0;
            for(auto& item:items){item.due=std::max(previous,std::min(item.due,item.arrived+std::max(0.f,delayMs)/1000.));previous=item.due;}
        }
        void Clear(){items.clear();bytes=0;}
        bool Empty()const{return items.empty();}
        size_t Count()const{return items.size();}
        size_t Bytes()const{return bytes;}
    };
}
