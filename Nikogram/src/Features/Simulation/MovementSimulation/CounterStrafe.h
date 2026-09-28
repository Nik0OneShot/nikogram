#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace CounterStrafe
{
    inline constexpr int MinimumReversals=2; // Shared by ground, air, normal and rapid detection.
    inline constexpr float HistorySeconds=2.1f;
    struct Sample { float time, x, y, vx, vy; };
    struct Estimate { bool valid = false; float ax = 0, ay = 0, center = 0, drift = 0, speed = 0, confidence = 0; float driftLimit=1e30f; };
    struct Audit { const char* reason="not_evaluated"; float duration=0,variance=0,width=0,drift=0,agreement=0; int reversals=0,positive=0,negative=0; bool rapid=false;
        float axisX=0,axisY=0,low=0,high=0,age=0; unsigned rejectionMask=0;
        float meanDrift=0,interval=0,freshness=0,widthLimit=96; bool extended=false; };

    inline float DriftOffset(const Estimate& e,float elapsed) { return std::clamp(e.drift*elapsed,-e.driftLimit,e.driftLimit); }
    inline float DriftVelocity(const Estimate& e,float elapsed) { return std::fabs(e.drift*elapsed)>=e.driftLimit?0.f:e.drift; }
    inline float Correction(const Estimate& e, float position, float velocity, float elapsed)
    {
        const float error=e.center+DriftOffset(e,elapsed)-position;
        const float desired=DriftVelocity(e,elapsed)+std::clamp(error/.15f,-e.speed,e.speed);
        return (desired-velocity)*std::clamp(elapsed/.15f,0.f,1.f);
    }

    // Diagnostic decomposition only. Zero-drift is a same-state sensitivity check,
    // not a resimulated trajectory or a proposed change to the live controller.
    struct CorrectionAudit {
        float driftOffset, centerError, rawSteering, steering, blend, desired, correction, zeroDriftCorrection;
        bool saturated, opposesVelocity;
    };
    inline CorrectionAudit Explain(const Estimate& e, float position, float velocity, float elapsed)
    {
        const float driftOffset=DriftOffset(e,elapsed), error=e.center+driftOffset-position;
        const float raw=error/.15f, steering=std::clamp(raw,-e.speed,e.speed);
        const float blend=std::clamp(elapsed/.15f,0.f,1.f), desired=DriftVelocity(e,elapsed)+steering;
        auto zero=e; zero.drift=0;
        return {driftOffset,error,raw,steering,blend,desired,Correction(e,position,velocity,elapsed),
            Correction(zero,position,velocity,elapsed),std::fabs(raw)>e.speed,
            std::fabs(velocity)>5.f && desired*velocity<0.f};
    }

    // Chronological, same-mode samples. Estimate a lateral corridor, not future input reversals.
    inline Estimate DetectWindow(const std::vector<Sample>& s, Audit* audit=nullptr, bool extended=false)
    {
        if(audit) *audit={};
        const auto reject=[&](const char* reason)->Estimate{if(audit) audit->reason=reason;return {};};
        if (s.size() < 12) return reject("insufficient_samples");
        for (size_t i = 0; i < s.size(); ++i)
        {
            const auto& p = s[i];
            if (!std::isfinite(p.time) || !std::isfinite(p.x) || !std::isfinite(p.y)
                || !std::isfinite(p.vx) || !std::isfinite(p.vy)) return reject("nonfinite_sample");
            if (i && (p.time <= s[i-1].time || p.time-s[i-1].time > .1f)) return reject("invalid_sample_timing");
        }
        const float duration = s.back().time-s.front().time;
        if(audit) audit->duration=duration;
        if (duration < .18f || duration > HistorySeconds+.01f) return reject("history_duration");
        float mx=0, my=0;
        for (const auto& p:s) { mx+=p.vx; my+=p.vy; }
        mx/=s.size(); my/=s.size();
        float xx=0, xy=0, yy=0;
        for (const auto& p:s) { float x=p.vx-mx,y=p.vy-my; xx+=x*x; xy+=x*y; yy+=y*y; }
        const float angle=.5f*std::atan2(2*xy,xx-yy), ax=std::cos(angle), ay=std::sin(angle);
        const float variance=(xx*ax*ax+2*xy*ax*ay+yy*ay*ay)/s.size();
        if(audit) audit->variance=variance;
        if (variance < 1600 || variance < .8f*(xx+yy)/s.size()) return reject("weak_or_multiaxis_variation");
        const float meanDrift=mx*ax+my*ay, threshold=std::max(25.f,std::sqrt(variance)*.25f);
        // Locate actual direction changes, not changes relative to a biased window mean.
        std::vector<size_t> turns;
        int turnSign=0;
        for(size_t i=0;i<s.size();++i) {
            const float v=s[i].vx*ax+s[i].vy*ay;
            const int next=v>threshold?1:v<-threshold?-1:0;
            if(next && next!=turnSign) { if(turnSign) turns.push_back(i); turnSign=next; }
        }
        float interval=0;
        if(turns.size()>=2) interval=s[turns.back()].time-s[turns[turns.size()-2]].time;
        // A full cycle joins equivalent turning points. Two turns establish oscillation,
        // but cannot establish translation: default to zero drift until a cycle exists.
        float drift=0;
        if(turns.size()>=3) {
            const auto& a=s[turns[turns.size()-3]]; const auto& b=s[turns.back()];
            drift=((b.x-a.x)*ax+(b.y-a.y)*ay)/(b.time-a.time);
        }
        if(extended && interval<=.3f) return reject("extended_requires_slow_pattern");
        if(extended && turns.size()>=3) {
            const float previous=s[turns[turns.size()-2]].time-s[turns[turns.size()-3]].time;
            if(std::max(previous,interval)>1.6f*std::min(previous,interval)) return reject("inconsistent_slow_period");
        }
        int sign=0, changes=0, positive=0, negative=0; float lastChange=s.front().time, lastRun=s.front().time;
        bool fastReversal=false;
        float low=1e30f,high=-1e30f,speed=0;
        for (const auto& p:s)
        {
            const float v=p.vx*ax+p.vy*ay-drift;
            const float rawVelocity=p.vx*ax+p.vy*ay;
            const int next=rawVelocity>threshold?1:rawVelocity<-threshold?-1:0;
            if (next>0) ++positive; if(next<0) ++negative;
            if (next && next!=sign)
            {
                if (sign) { fastReversal |= p.time-lastRun<.045f; ++changes; lastChange=p.time; }
                sign=next; lastRun=p.time;
            }
            const float position=p.x*ax+p.y*ay-drift*(p.time-s.back().time);
            low=std::min(low,position); high=std::max(high,position); speed=std::max(speed,std::fabs(v));
        }
        const bool rapid=fastReversal || high-low<6 || duration<.3f;
        const float freshness=rapid?.12f:std::clamp(interval*1.25f,.25f,.85f);
        const float widthLimit=interval>.3f?std::clamp(speed*interval*1.4f,96.f,256.f):96.f;
        if(audit){audit->meanDrift=meanDrift;audit->interval=interval;audit->freshness=freshness;audit->widthLimit=widthLimit;audit->extended=extended;}
        if(audit){audit->rapid=rapid;audit->reversals=changes;audit->positive=positive;audit->negative=negative;audit->width=high-low;audit->drift=drift;}
        const bool early=!rapid && changes==MinimumReversals;
        if(audit) {
            audit->axisX=ax;audit->axisY=ay;audit->low=low;audit->high=high;audit->age=s.back().time-lastChange;
            // Bits: reversal count, directional sample count, stale reversal, narrow, wide, drift.
            audit->rejectionMask=(changes<MinimumReversals?1u:0u)
                |(positive<(rapid?5:3) || negative<(rapid?5:3)?2u:0u)
                |(s.back().time-lastChange>freshness?4u:0u)
                |(high-low<(rapid?1.5f:6.f)?8u:0u)|(high-low>widthLimit?16u:0u)
                |(std::fabs(drift)>std::sqrt(variance)*.6f?32u:0u);
        }
        if(changes<MinimumReversals || positive<(rapid?5:3) || negative<(rapid?5:3)
            || s.back().time-lastChange>freshness
            || high-low<(rapid?1.5f:6.f) || high-low>widthLimit || std::fabs(drift)>std::sqrt(variance)*.6f) return reject("reversal_corridor_or_drift");
        // Do not drag a stopped player back toward a remembered center.
        size_t moving=s.size()-1;
        while(moving && std::fabs(s[moving].vx*ax+s[moving].vy*ay)<5.f) --moving;
        if(s.back().time-s[moving].time>.12f) return reject("stopped_pattern");
        {
            // Fast key taps have small displacement. Require position/velocity agreement
            // rather than accepting packet jitter solely because velocity changes sign.
            float dot=0, observedEnergy=0, predictedEnergy=0;
            for(size_t i=1;i<s.size();++i)
            {
                const auto& a=s[i-1]; const auto& b=s[i];
                const float dt=b.time-a.time;
                if(rapid && dt>.04f) return reject("rapid_sampling_too_sparse"); // insufficient temporal resolution for rapid reversals
                const float observed=(b.x-a.x)*ax+(b.y-a.y)*ay-drift*dt;
                const float predicted=((a.vx+b.vx)*ax*.5f+(a.vy+b.vy)*ay*.5f-drift)*dt;
                dot+=observed*predicted; observedEnergy+=observed*observed; predictedEnergy+=predicted*predicted;
            }
            if(audit && observedEnergy>0 && predictedEnergy>0) audit->agreement=dot/std::sqrt(observedEnergy*predictedEnergy);
            if(observedEnergy<.1f || predictedEnergy<.1f
                || dot < (early?.9f:.8f)*std::sqrt(observedEnergy*predictedEnergy)
                || observedEnergy>predictedEnergy*4.f || predictedEnergy>observedEnergy*4.f) return reject("position_velocity_disagreement");
        }
        const float current=s.back().x*ax+s.back().y*ay;
        if(current<low-4 || current>high+4) return reject("outside_corridor");
        // A consistency score, not a calibrated probability of hitting a projectile.
        const float balance=2.f*std::min(positive,negative)/(positive+negative);
        if(audit) audit->reason=early?"accepted_early_reversals":"accepted";
        return {true,ax,ay,(low+high)*.5f,drift,speed,balance*(early?.8f:1.f),std::min(24.f,(high-low)*.25f)};
    }

    inline Estimate Detect(const std::vector<Sample>& samples, Audit* audit=nullptr)
    {
        // Preserve short-window responsiveness; consult older history only for slow patterns.
        if(samples.empty()) return DetectWindow(samples,audit);
        size_t first=0;
        while(first<samples.size() && samples.back().time-samples[first].time>1.f) ++first;
        if(!first) return DetectWindow(samples,audit);
        const std::vector<Sample> recent(samples.begin()+first,samples.end());
        const auto shortResult=DetectWindow(recent,audit);
        if(shortResult.valid) return shortResult;
        Audit longAudit;
        const auto longResult=DetectWindow(samples,&longAudit,true);
        if(longResult.valid) {if(audit)*audit=longAudit;return longResult;}
        return shortResult;
    }
}
