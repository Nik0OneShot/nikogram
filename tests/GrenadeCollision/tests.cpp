#include "../../Nikogram/src/Features/Aimbot/GrenadeCollisionPolicy.h"
#include "../../Nikogram/src/Features/Aimbot/BowChargePolicy.h"
#include "../../Nikogram/src/Features/Aimbot/AmmoEvidencePolicy.h"
#include "../../Nikogram/src/Features/Aimbot/AmmoLifetimePolicy.h"
#include "../../Nikogram/src/Features/Aimbot/AutoDetonateCandidates.h"
#include "../../Nikogram/src/Features/Aimbot/MeleePredictionPolicy.h"
#include "../../Nikogram/src/Features/Aimbot/MeleeContactPolicy.h"
#include "../../Nikogram/src/Features/Backtrack/BacktrackPolicy.h"
#include "../../Nikogram/src/Features/Backtrack/CursorBacktrackPolicy.h"
#include "../../Nikogram/src/Features/Triggerbot/TriggerPolicy.h"
#include "../../Nikogram/src/Features/Aimbot/CombatPriorityPolicy.h"
#include "../../Nikogram/src/Features/Aimbot/FlareComboPolicy.h"
#include "../../Nikogram/src/Features/Aimbot/AimFOVVisualPolicy.h"
#include "../../Nikogram/src/Features/Visuals/Radar/RadarPolicy.h"
#include "../../Nikogram/src/Features/Aimbot/MeleeTracePolicy.h"
#include "../../Nikogram/src/Features/Aimbot/AmmoConservationPolicy.h"
#include "../../Nikogram/src/Features/Aimbot/ArcPolicy.h"
#include "../../Nikogram/src/Features/Aimbot/AimbotAuditPolicy.h"
#include "../../Nikogram/src/Features/Aimbot/ProjectileAimPolicy.h"
#include "../../Nikogram/src/Features/Aimbot/ProjectileMuzzlePolicy.h"
#include <limits>
#include <iostream>
#include "../../Nikogram/src/Features/Simulation/MovementSimulation/CounterStrafe.h"
#include "../../Nikogram/src/Features/Simulation/MovementSimulation/PredictionPolicy.h"
#include "../../Nikogram/src/Features/Aimbot/ObservationPolicy.h"

int main()
{
    using GrenadeCollisionPolicy::PreferTarget;
    int checks=0;
    auto check=[&](bool result){++checks;if(!result){std::cerr<<"FAIL "<<checks<<'\n';std::exit(1);}};
    // Weapon IDs are deliberately not part of this gate. All own-shot direct
    // player candidates qualify; reflected projectiles and splash geometry do not.
    for (int direct=0;direct<2;++direct)
        for (int player=0;player<2;++player)
            for (int supported=0;supported<2;++supported)
                check(GrenadeCollisionPolicy::TargetClip(direct,player,supported)==bool(direct && player && supported));
    check(GrenadeCollisionPolicy::TargetClip(true,true,true));
    check(!GrenadeCollisionPolicy::TargetClip(true,true,false));
    check(!GrenadeCollisionPolicy::TargetClip(false,true,true));
    check(!GrenadeCollisionPolicy::TargetClip(true,false,true));
    // Missing broadphase target can be recovered for every supported projectile;
    // a nearer wall/actor, equal-distance blocker, or filter exclusion still wins.
    for (int weaponKind=0;weaponKind<3;++weaponKind) {
        check(GrenadeCollisionPolicy::TargetClip(true,true,true));
        check(PreferTarget(true,true,.6f,false,1.f,false,false));
        check(PreferTarget(true,true,.2f,true,.6f,false,false));
        check(!PreferTarget(true,true,.6f,true,.2f,false,false));
        check(!PreferTarget(true,true,.6f,true,.6f,false,false));
        check(!PreferTarget(false,true,.2f,false,1.f,false,false));
        check(!PreferTarget(true,true,.2f,true,.6f,true,false));
        check(!PreferTarget(true,true,.2f,true,.6f,false,true));
    }
    check(PreferTarget(true,true,.4f,false,1.f,false,false));
    check(PreferTarget(true,true,.4f,true,.8f,false,false));
    check(!PreferTarget(true,true,.8f,true,.4f,false,false));
    check(!PreferTarget(true,true,.4f,true,.4f,false,false));
    check(!PreferTarget(false,true,.4f,false,1.f,false,false));
    check(!PreferTarget(true,false,1.f,false,1.f,false,false));
    check(!PreferTarget(true,true,0.f,true,0.f,true,false));
    check(!PreferTarget(true,true,0.f,true,0.f,false,true));
    check(PreferTarget(true,true,0.f,false,1.f,false,false));
    check(!PreferTarget(true,true,std::numeric_limits<float>::quiet_NaN(),false,1.f,false,false));
    check(!PreferTarget(true,true,.4f,true,std::numeric_limits<float>::infinity(),false,false));
    check(!PreferTarget(true,true,-.1f,false,1.f,false,false));
    check(!PreferTarget(true,true,1.1f,false,1.f,false,false));
    check(!PreferTarget(true,true,.4f,true,-.1f,false,false));
    auto standing=GrenadeCollisionPolicy::FeetHeights(0,82,5,5);
    check(std::abs(standing[0]-16.4f)<.001f);
    check(std::abs(standing[1]-26.24f)<.001f);
    auto crouched=GrenadeCollisionPolicy::FeetHeights(0,62,5,5);
    check(crouched[0]>=9 && crouched[1]>crouched[0] && crouched[1]<31);
    auto shifted=GrenadeCollisionPolicy::FeetHeights(10,92,5,1000);
    check(shifted[0]>=10 && shifted[1]<=10+82*.45f);
    auto zero=GrenadeCollisionPolicy::FeetHeights(10,10,5,5);
    check(zero[0]==10 && zero[1]==10);
    auto small=GrenadeCollisionPolicy::FeetHeights(0,8,5,-100);
    check(small[0]>=0 && small[1]<=8*.45f && small[1]>=small[0]);
    std::vector<CounterStrafe::Sample> early,straight,single;
    for(int i=0;i<=40;++i) {
        const float t=i*.015f,omega=2*3.14159265f/.6f;
        early.push_back({t,100*t,12*std::sin(omega*t),100,12*omega*std::cos(omega*t)});
        straight.push_back({t,100*t,0,100,0});
        single.push_back({t,100*t,12*std::sin(omega*t*.5f),100,6*omega*std::cos(omega*t*.5f)});
    }
    CounterStrafe::Audit audit;
    const auto estimate=CounterStrafe::Detect(early,&audit);
    check(estimate.valid && audit.reversals==2);
    check(estimate.confidence<=.8f);
    check(!CounterStrafe::Detect(straight).valid);
    check(!CounterStrafe::Detect(single).valid);
    auto jitter=early; for(auto& s:jitter){s.y=0;}
    check(!CounterStrafe::Detect(jitter).valid);
    check(PredictionPolicy::LandingForward(40,300)==40);
    check(PredictionPolicy::LandingForward(520,300)==300);
    check(PredictionPolicy::LandingForward(0,300)==0);
    check(PredictionPolicy::LandingForward(4,300)==0);
    check(PredictionPolicy::LandingForward(40,0)==0);
    check(PredictionPolicy::LandingForward(std::numeric_limits<float>::quiet_NaN(),300)==0);
    check(PredictionPolicy::LandingYaw(4)==0 && PredictionPolicy::LandingYaw(-4)==0);
    check(ObservationPolicy::Bracket(1,1.02f,1.04f));
    check(std::abs(ObservationPolicy::Weight(1,1.02f,1.04f)-.5f)<.0001f);
    check(!ObservationPolicy::Bracket(1,1.2f,1.04f));
    check(!ObservationPolicy::Bracket(1,.9f,1.04f));
    check(!ObservationPolicy::Bracket(1,1,1));
    check(!ObservationPolicy::Bracket(1,1.1f,1.2f));
    check(!ObservationPolicy::Bracket(1,std::numeric_limits<float>::quiet_NaN(),1.04f));
    check(ObservationPolicy::Weight(1,1.04f,1.04f)==1.f);
    check(ObservationPolicy::Linear(10,100,.5f)==60);
    check(ObservationPolicy::Linear(10,-100,.5f)==-40);
    check(ObservationPolicy::Linear(10,100,0)==10);
    check(ObservationPolicy::Improvement(80,30)==50);
    check(ObservationPolicy::Improvement(30,80)==-50);
    check(ObservationPolicy::Improvement(30,30)==0);
    auto alongLedge=PredictionPolicy::PreContactIntent(-42.5f,0.f,320.f);
    check(alongLedge.x==-42.5f && alongLedge.y==0.f && alongLedge.speed==42.5f);
    auto still=PredictionPolicy::PreContactIntent(0,0,320);
    check(still.speed==0 && still.x==0 && still.y==0);
    auto diagonal=PredictionPolicy::PreContactIntent(300,400,320);
    check(std::abs(diagonal.x-192)<.001f && std::abs(diagonal.y-256)<.001f && diagonal.speed==320);
    check(PredictionPolicy::PreContactIntent(1,1,320).speed==0);
    check(PredictionPolicy::PreContactIntent(40,0,-1).speed==0);
    check(PredictionPolicy::PreContactIntent(std::numeric_limits<float>::infinity(),0,320).speed==0);
    auto landing=[](float vz,float ax,float ay,float nx,float ny,float nz){return PredictionPolicy::VerticalLandingDeflection(-40,0,vz,ax,ay,nx,ny,nz);};
    check(landing(-413,-40,-165.2f,0,-.4472136f,.8944272f));
    check(!landing(-50,-40,-165.2f,0,-.4472136f,.8944272f));
    check(!landing(-413,-40,0,0,-.4472136f,.8944272f));
    check(!landing(-413,-40,165.2f,0,-.4472136f,.8944272f));
    check(!landing(-413,-40,-165.2f,0,-1,0));
    check(!landing(-413,-40,-165.2f,0,0,1));
    check(!PredictionPolicy::VerticalLandingDeflection(-40,100,-413,-40,-65,0,-.4472136f,.8944272f));
    check(!landing(-413,-40,-165.2f,0,-.4f,.8f));
    check(!landing(-413,std::numeric_limits<float>::quiet_NaN(),-165.2f,0,-.4472136f,.8944272f));
    check(PredictionPolicy::NearLandingSupport(-128.20166f,-128.2304f));
    check(PredictionPolicy::NearLandingSupport(0,0));
    check(PredictionPolicy::NearLandingSupport(0,-.125f));
    check(!PredictionPolicy::NearLandingSupport(0,-.126f));
    check(!PredictionPolicy::NearLandingSupport(0,.01f));
    check(!PredictionPolicy::NearLandingSupport(std::numeric_limits<float>::quiet_NaN(),0));
    check(!PredictionPolicy::NearLandingSupport(0,std::numeric_limits<float>::infinity()));
    check(PredictionPolicy::VerticalLandingDeflection(-37.5f,0,-258.55557f,-37.5f,-105.82223f,0,-.44721356f,.8944271f));
    check(ObservationPolicy::LandingReplayEligible(false,true,.7f,1.f));
    check(ObservationPolicy::LandingReplayEligible(false,true,1.5f,2.f));
    check(!ObservationPolicy::LandingReplayEligible(true,true,.7f,2.f));
    check(ObservationPolicy::LandingReplayEligible(false,false,.7f,2.f));
    check(!ObservationPolicy::LandingReplayEligible(false,true,1.51f,2.f));
    check(!ObservationPolicy::LandingReplayEligible(false,true,0.f,2.f));
    check(!ObservationPolicy::LandingReplayEligible(false,true,.7f,.99f));
    check(!ObservationPolicy::LandingReplayEligible(false,true,std::numeric_limits<float>::quiet_NaN(),2.f));
    check(!ObservationPolicy::LandingReplayEligible(false,true,.7f,std::numeric_limits<float>::infinity()));
    for(bool candidate:{false,true}) for(bool replay:{false,true}) for(bool control:{false,true})
        check(PredictionPolicy::ApplyLandingAlternative(candidate,replay,control)==(candidate && replay && !control));
    const auto direction=ObservationPolicy::RelativeError(10,0,30,-20);
    check(direction.valid && direction.along==30 && direction.across==-20);
    const auto rotated=ObservationPolicy::RelativeError(0,10,20,30);
    check(rotated.valid && rotated.along==30 && rotated.across==-20);
    check(!ObservationPolicy::RelativeError(0,0,30,20).valid);
    check(!ObservationPolicy::RelativeError(4,0,30,20).valid);
    check(ObservationPolicy::RelativeError(5,0,30,20).valid);
    check(!ObservationPolicy::RelativeError(10,0,std::numeric_limits<float>::quiet_NaN(),20).valid);
    check(ObservationPolicy::BoundedCorridorShift(200,-50,50)==-64);
    check(ObservationPolicy::BoundedCorridorShift(-200,-50,50)==64);
    check(ObservationPolicy::BoundedCorridorShift(60,-50,50)==-10);
    check(ObservationPolicy::BoundedCorridorShift(0,-50,50)==0);
    check(ObservationPolicy::BoundedCorridorShift(50,-50,50)==0);
    check(ObservationPolicy::BoundedCorridorShift(0,50,-50)==0);
    check(ObservationPolicy::BoundedCorridorShift(std::numeric_limits<float>::quiet_NaN(),-50,50)==0);
    auto wideHistory=early;for(auto& sample:wideHistory){sample.y*=6;sample.vy*=6;}
    CounterStrafe::Audit wideAudit;
    check(!CounterStrafe::Detect(wideHistory,&wideAudit).valid);
    check((wideAudit.rejectionMask&16u)!=0);
    check(wideAudit.high>wideAudit.low && wideAudit.width>96);
    CounterStrafe::Audit acceptedAudit;
    check(CounterStrafe::Detect(early,&acceptedAudit).valid && acceptedAudit.rejectionMask==0);
    check(PredictionPolicy::UseCompressedHull(false,false));
    check(PredictionPolicy::UseCompressedHull(false,true));
    check(PredictionPolicy::UseCompressedHull(true,false));
    check(!PredictionPolicy::UseCompressedHull(true,true));
    check(ObservationPolicy::AdjustmentClear(1,false,false,true,1));
    check(!ObservationPolicy::AdjustmentClear(.9f,false,false,true,1));
    check(!ObservationPolicy::AdjustmentClear(1,true,false,true,1));
    check(!ObservationPolicy::AdjustmentClear(1,false,true,true,1));
    check(!ObservationPolicy::AdjustmentClear(1,false,false,false,1));
    check(!ObservationPolicy::AdjustmentClear(1,false,false,true,.5f));
    check(!ObservationPolicy::AdjustmentClear(std::numeric_limits<float>::quiet_NaN(),false,false,true,1));
    check(!ObservationPolicy::AdjustmentClear(1,false,false,true,std::numeric_limits<float>::quiet_NaN()));
    check(std::abs(ObservationPolicy::SteeringStep(300,-300,.015f,300)+30)<.001f);
    check(std::abs(ObservationPolicy::SteeringStep(-300,300,.015f,300)-30)<.001f);
    check(ObservationPolicy::SteeringStep(10,15,.015f,300)==5);
    check(ObservationPolicy::SteeringStep(300,1000,.015f,300)==0);
    check(ObservationPolicy::SteeringStep(0,100,0,300)==0);
    check(ObservationPolicy::SteeringStep(0,100,.2f,300)==0);
    check(ObservationPolicy::SteeringStep(0,100,.015f,-1)==0);
    check(ObservationPolicy::SteeringStep(0,std::numeric_limits<float>::quiet_NaN(),.015f,300)==0);
    check(std::abs(ObservationPolicy::SegmentBox({-2,0,0},{2,0,0},{-1,-1,-1},{1,1,1})-.25f)<.001f);
    check(ObservationPolicy::SegmentBox({0,0,0},{2,0,0},{-1,-1,-1},{1,1,1})==0);
    check(ObservationPolicy::SegmentBox({-2,2,0},{2,2,0},{-1,-1,-1},{1,1,1})<0);
    check(ObservationPolicy::SegmentBox({2,0,0},{3,0,0},{-1,-1,-1},{1,1,1})<0);
    check(ObservationPolicy::SegmentBox({0,0,0},{0,0,0},{-1,-1,-1},{1,1,1})==0);
    check(ObservationPolicy::SegmentBox({0,0,0},{0,0,0},{1,-1,-1},{-1,1,1})<0);
    check(ObservationPolicy::SegmentBox({std::numeric_limits<float>::quiet_NaN(),0,0},{0,0,0},{-1,-1,-1},{1,1,1})<0);
    check(CounterStrafe::MinimumReversals==2);
    for(const float duration:{.36f,.24f}) {
        std::vector<CounterStrafe::Sample> two;
        const float amplitude=duration<.3f?3.f:8.f;
        const float omega=2*3.14159265f/duration;
        for(int i=0;i<=40;++i) {
            const float t=duration*i/40;
            two.push_back({t,0,amplitude*std::sin(omega*t),0,amplitude*omega*std::cos(omega*t)});
        }
        CounterStrafe::Audit twoAudit;
        check(CounterStrafe::Detect(two,&twoAudit).valid);
        check(twoAudit.reversals==2 && twoAudit.rapid==(duration<.3f));
        check((twoAudit.rejectionMask&1u)==0);
        auto rotatedTwo=two;for(auto& point:rotatedTwo){std::swap(point.x,point.y);std::swap(point.vx,point.vy);}
        check(CounterStrafe::Detect(rotatedTwo).valid);
    }
    check(!ObservationPolicy::AuditBudgetExpired(0));
    check(!ObservationPolicy::AuditBudgetExpired(2.999));
    check(ObservationPolicy::AuditBudgetExpired(3));
    check(ObservationPolicy::AuditBudgetExpired(50.3));
    check(ObservationPolicy::AuditBudgetExpired(-1));
    check(ObservationPolicy::AuditBudgetExpired(std::numeric_limits<double>::quiet_NaN()));
    check(ObservationPolicy::AuditBudgetExpired(std::numeric_limits<double>::infinity()));
    const CounterStrafe::Estimate diagnostic{true,1,0,50,100,300,.9f};
    for(float elapsed:{0.f,.075f,.15f,.7f}) {
        const auto a=CounterStrafe::Explain(diagnostic,25,-200,elapsed);
        check(a.correction==CounterStrafe::Correction(diagnostic,25,-200,elapsed));
        check(std::fabs(a.driftOffset-100*elapsed)<.001f);
        check(a.opposesVelocity);
        check(a.blend>=0 && a.blend<=1);
    }
    const auto saturated=CounterStrafe::Explain(diagnostic,0,-200,.7f);
    check(saturated.saturated && saturated.steering==300);
    check(CounterStrafe::Explain(diagnostic,0,-200,0).correction==0);
    auto noDrift=diagnostic; noDrift.drift=0;
    const auto neutral=CounterStrafe::Explain(noDrift,25,100,.7f);
    check(neutral.correction==neutral.zeroDriftCorrection);
    check(!CounterStrafe::Explain(diagnostic,25,0,.7f).opposesVelocity);
    // Slow, broad strafes at different phases must not become an unbounded linear lead.
    for(float period:{1.2f,1.5f}) for(int phase=0;phase<12;++phase) {
        std::vector<CounterStrafe::Sample> slow;
        const float omega=2*3.14159265f/period, offset=phase*2*3.14159265f/12;
        for(int i=0;i<=140;++i) {
            const float t=i*.015f;
            slow.push_back({t,65*std::sin(omega*t+offset),0,65*omega*std::cos(omega*t+offset),0});
        }
        CounterStrafe::Audit slowAudit;
        const auto slowEstimate=CounterStrafe::Detect(slow,&slowAudit);
        check(slowEstimate.valid);
        check(std::fabs(slowEstimate.drift)<10);
        check(std::fabs(slowEstimate.center)<6);
        check(slowAudit.widthLimit>96 && slowAudit.freshness>.25f);
        float pos=slow.back().x,velocity=slow.back().vx;
        for(int tick=0;tick<50;++tick) {
            velocity+=CounterStrafe::Correction(slowEstimate,pos,velocity,tick*.015f);
            pos+=velocity*.015f;
            check(std::isfinite(pos) && std::fabs(pos)<80);
        }
        auto stopped=slow;
        for(int i=1;i<=20;++i) stopped.push_back({2.1f+i*.015f,slow.back().x,0,0,0});
        while(stopped.back().time-stopped.front().time>CounterStrafe::HistorySeconds) stopped.erase(stopped.begin());
        check(!CounterStrafe::Detect(stopped).valid);
        auto committed=slow;
        for(int i=1;i<=70;++i) committed.push_back({2.1f+i*.015f,slow.back().x+300*i*.015f,0,300,0});
        while(committed.back().time-committed.front().time>CounterStrafe::HistorySeconds) committed.erase(committed.begin());
        check(!CounterStrafe::Detect(committed).valid);
        auto translated=slow;
        for(auto& point:translated){point.x+=40*point.time;point.vx+=40;}
        const auto translating=CounterStrafe::Detect(translated);
        check(translating.valid);
        check(std::fabs(CounterStrafe::DriftOffset(translating,2))<=24);
    }
    auto capped=diagnostic; capped.driftLimit=10;
    // Constant-speed legs with abrupt A/D reversals, not just sinusoidal movement.
    for(int phase=0;phase<8;++phase) {
        std::vector<CounterStrafe::Sample> triangle;
        for(int i=0;i<=140;++i) {
            const float t=i*.015f, cycle=std::fmod(t+phase*.15f,1.2f);
            const float x=cycle<.6f?-66+220*cycle:66-220*(cycle-.6f);
            triangle.push_back({t,x,0,cycle<.6f?220.f:-220.f,0});
        }
        const auto estimate=CounterStrafe::Detect(triangle);
        check(estimate.valid);
        check(std::fabs(estimate.drift)<8);
        check(std::fabs(estimate.center)<5);
    }
    check(CounterStrafe::DriftOffset(capped,1)==10);
    check(CounterStrafe::DriftVelocity(capped,1)==0);
    check(CounterStrafe::Correction(capped,60,0,1)==0);
    capped.drift=-100;
    check(CounterStrafe::DriftOffset(capped,1)==-10);
    check(BowChargePolicy::Fraction(15000,0)==0);
    check(BowChargePolicy::Fraction(10,10)==0);
    check(BowChargePolicy::Fraction(10,11)==0);
    check(BowChargePolicy::Fraction(10.5f,10)==.5f);
    check(BowChargePolicy::Fraction(15,10)==1);
    check(BowChargePolicy::Fraction(15,-1)==0);
    check(BowChargePolicy::Fraction(std::numeric_limits<float>::quiet_NaN(),10)==0);
    check(BowChargePolicy::Fraction(10,std::numeric_limits<float>::infinity())==0);
    check(BowChargePolicy::Fraction(10.25f,10,.5f)==.5f);
    check(BowChargePolicy::Fraction(10.5f,10,.5f)==1);
    check(BowChargePolicy::Fraction(12,10,2)==.5f);
    check(BowChargePolicy::Fraction(10.5f,10,0)==.5f);
    check(BowChargePolicy::Fraction(10.5f,10,std::numeric_limits<float>::quiet_NaN())==.5f);
    check(BowChargePolicy::BodyDamage(0,50,70,1,1)==50);
    check(BowChargePolicy::BodyDamage(1,50,70,1,1)==120);
    check(BowChargePolicy::BodyDamage(.5f,50,70,1,1)==85);
    check(BowChargePolicy::BodyDamage(1,50,70,3,1)==360);
    check(BowChargePolicy::BodyDamage(1,25,35,1,1)==60);
    check(BowChargePolicy::BodyDamage(1,50,70,1,.5f)==60);
    check(BowChargePolicy::BodyDamage(1,50,70,1,2)==120);
    check(BowChargePolicy::BodyDamage(1,-50,70,1,1)==0);
    check(BowChargePolicy::BodyDamage(1,50,70,1,std::numeric_limits<float>::quiet_NaN())==0);
    check(BowChargePolicy::Lethal(120,120,false));
    check(!BowChargePolicy::Lethal(119.9f,120,false));
    check(!BowChargePolicy::Lethal(360,100,true));
    check(!BowChargePolicy::Lethal(std::numeric_limits<float>::infinity(),100,false));
    check(BowChargePolicy::Center(-2,6)==2);
    check(BowChargePolicy::Center(4,8)==6);
    check(AmmoEvidencePolicy::SplashScreen(100,0,100)==50);
    for (int bits=0; bits<64; ++bits)
        check(AmmoEvidencePolicy::TimingCandidate(bits&1,bits&2,bits&4,bits&8,bits&16,bits&32) == (bits==15));
    check(AmmoEvidencePolicy::SplashScreen(100,50,100)==37.5f);
    check(AmmoEvidencePolicy::SplashScreen(100,100,100)==0);
    check(AmmoEvidencePolicy::SplashScreen(100,-1,100)==0);
    check(AmmoEvidencePolicy::SplashScreen(100,0,0)==0);
    check(AmmoEvidencePolicy::SplashScreen(-100,0,100)==0);
    check(AmmoEvidencePolicy::SplashScreen(std::numeric_limits<float>::infinity(),0,100)==0);
    check(AmmoEvidencePolicy::SplashScreen(100,std::numeric_limits<float>::quiet_NaN(),100)==0);
    check(AmmoEvidencePolicy::ScreenCovered(100,100,true,false));
    check(!AmmoEvidencePolicy::ScreenCovered(99.9f,100,true,false));
    check(!AmmoEvidencePolicy::ScreenCovered(100,100,false,false));
    check(!AmmoEvidencePolicy::ScreenCovered(100,100,true,true));
    check(!AmmoEvidencePolicy::ScreenCovered(100,0,true,false));
    check(!AmmoEvidencePolicy::ScreenCovered(std::numeric_limits<float>::infinity(),100,true,false));
    AmmoLifetimePolicy::RequestGate requestGate;
    check(!requestGate.Due(false,100));
    check(requestGate.Due(true,101));
    check(!requestGate.Due(true,102));
    check(!requestGate.Due(true,350));
    check(requestGate.Due(true,351));
    check(!requestGate.Due(false,352));
    check(requestGate.Due(true,353));
    check(requestGate.Due(true,354,1));
    check(!requestGate.Due(true,355,1));
    check(requestGate.Due(true,1,1));
    int misses=0;
    check(!AmmoLifetimePolicy::Missing(false,false,misses) && misses==0);
    check(!AmmoLifetimePolicy::Missing(false,true,misses) && misses==1);
    check(!AmmoLifetimePolicy::Missing(true,true,misses) && misses==0);
    check(!AmmoLifetimePolicy::Missing(false,true,misses));
    check(AmmoLifetimePolicy::Missing(false,true,misses));
    check(!AmmoLifetimePolicy::ResetNeeded(5,5,11,10,3,2));
    check(AmmoLifetimePolicy::ResetNeeded(6,5,11,10,3,2));
    check(!AmmoLifetimePolicy::ResetNeeded(5,5,10,10,3,2));
    check(!AmmoLifetimePolicy::ResetNeeded(5,5,10,10,2,2));
    check(!AmmoLifetimePolicy::ResetNeeded(5,5,9,10,3,2));
    check(AmmoLifetimePolicy::ResetNeeded(5,5,10,10,1,2));
    check(std::string(AmmoLifetimePolicy::ResetReason(6,5,11,10,3,2))=="local_identity_changed");
    check(AmmoLifetimePolicy::ResetReason(5,5,9,10,3,2)==nullptr);
    check(std::string(AmmoLifetimePolicy::ResetReason(5,5,10,10,1,2))=="time_rewind");
    check(std::string(AmmoLifetimePolicy::ResetReason(5,5,10,10,std::numeric_limits<float>::quiet_NaN(),2))=="invalid_time");
    // Reproduce the consecutive-tick sequence seen in the v31 log. The
    // second observation must retain both the first time and request gate.
    {
        float firstObservation=1314.f, lastTime=1314.6f;
        int lastCommand=87320;
        AmmoLifetimePolicy::RequestGate heldRequest;
        check(heldRequest.Due(true,1000));
        for (const auto command : {87320,87321,87322,87200,87500,87201})
        {
            const float nextTime=lastTime+.015f;
            if (AmmoLifetimePolicy::ResetNeeded(5,5,command,lastCommand,nextTime,lastTime))
            { firstObservation=nextTime; heldRequest={}; }
            check(firstObservation==1314.f);
            check(!heldRequest.Due(true,1010));
            lastTime=nextTime; lastCommand=command;
        }
    }
    check(AmmoLifetimePolicy::ResetNeeded(5,5,11,10,1,2));
    check(AmmoLifetimePolicy::ResetNeeded(5,5,11,10,std::numeric_limits<float>::quiet_NaN(),2));
    {
        AmmoLifetimePolicy::CommandRequests requests;
        requests.Begin(145711);
        requests.Record(145711,1);
        // A final rewritten command of 146059 does not participate in attribution.
        const auto observed = requests.Take();
        check(observed.requested && observed.sources==1);
        check(observed.originalCommand==145711 && observed.requestCommand==145711);
        check(!requests.Take().requested);
        requests.Begin(145712);
        check(!requests.Take().requested);
        requests.Begin(10); requests.Record(10,2);
        requests.Begin(11); // Interrupted/early-return invocation cannot leak forward.
        check(!requests.Take().requested);
        requests.Begin(12); requests.Record(12,1); requests.Record(13,2);
        const auto combined=requests.Take();
        check(combined.requested && combined.sources==3 && combined.originalCommand==12 && combined.requestCommand==13);
        check(!AmmoLifetimePolicy::ResetNeeded(5,5,145711,146059,2190.5378f,2190.5242f));
        check(AmmoLifetimePolicy::ResetNeeded(5,5,145711,146059,2100.f,2190.5242f));
    }
    {
        // Missing sphere-query players must still reach the normal predicate.
        struct Player { bool allowed, visible; float distance; };
        std::vector<Player> players={{false,true,1},{true,false,5},{true,true,120.46f},{true,true,5.375f}};
        int calls=0;
        auto accept=[&](const Player& p){++calls;return p.allowed && p.visible && p.distance<=145;};
        check(AutoDetonateCandidates::CheckPlayers(players,accept));
        check(calls==3);
        players[2].distance=200; calls=0;
        check(AutoDetonateCandidates::CheckPlayers(players,accept));
        check(calls==4);
        players[3].visible=false; calls=0;
        check(!AutoDetonateCandidates::CheckPlayers(players,accept));
        check(calls==4);
        players.clear(); calls=0;
        check(!AutoDetonateCandidates::CheckPlayers(players,accept));
        check(calls==0);
    }
    check(MeleePredictionPolicy::Usable(true,false,1,2,3));
    check(!MeleePredictionPolicy::Usable(false,false,1,2,3));
    check(!MeleePredictionPolicy::Usable(true,true,1,2,3));
    check(!MeleePredictionPolicy::Usable(false,true,0,0,0));
    check(!MeleePredictionPolicy::Usable(true,false,std::numeric_limits<float>::quiet_NaN(),2,3));
    check(!MeleePredictionPolicy::Usable(true,false,1,std::numeric_limits<float>::infinity(),3));
    check(!MeleePredictionPolicy::Usable(true,false,1,2,-std::numeric_limits<float>::infinity()));
    check(MeleePredictionPolicy::Usable(true,false,0,0,0));
    check(!AmmoLifetimePolicy::ResetNeeded(5,5,1190,1189,568.16815f,568.17f));
    check(!AmmoLifetimePolicy::ResetNeeded(5,5,2,1,9.96f,10.f));
    check(AmmoLifetimePolicy::ResetNeeded(5,5,2,1,9.94f,10.f));
    float clockHigh=10.f;
    for(float time : {9.99f,9.98f,9.97f,9.96f})
    {
        check(!AmmoLifetimePolicy::ResetNeeded(5,5,2,1,time,clockHigh));
        clockHigh=std::max(clockHigh,time);
        check(clockHigh==10.f);
    }
    check(AmmoLifetimePolicy::ResetNeeded(5,5,2,1,9.94f,clockHigh));
    // Reproduce non-overlapping direct hits missed by the world broadphase.
    for (float fraction : {0.6161511f,0.17599809f,0.7117925f,0.18281186f,0.15934245f,0.003092448f})
    {
        check(MeleeTracePolicy::Recover(true,true,fraction,false,false,1,false,false));
        check(!MeleeTracePolicy::Recover(false,true,fraction,false,false,1,false,false));
        check(!MeleeTracePolicy::Recover(true,false,fraction,false,false,1,false,false));
        check(!MeleeTracePolicy::Recover(true,true,fraction,true,false,1,false,false));
        check(!MeleeTracePolicy::Recover(true,true,fraction,false,true,1,false,false));
        check(!MeleeTracePolicy::Recover(true,true,fraction,false,false,1,true,false));
        check(!MeleeTracePolicy::Recover(true,true,fraction,false,false,1,false,true));
        check(!MeleeTracePolicy::Recover(true,true,fraction,false,false,fraction,false,false));
        check(!MeleeTracePolicy::Recover(true,true,fraction,false,false,fraction*.5f,false,false));
        check(MeleeTracePolicy::Recover(true,true,fraction,false,false,(1+fraction)*.5f,false,false));
    }
    for (float invalid : {-1.f,0.f,1.f,2.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
        check(!MeleeTracePolicy::Recover(true,true,invalid,false,false,1,false,false));
    for (float invalid : {-1.f,2.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
        check(!MeleeTracePolicy::Recover(true,true,.5f,false,false,invalid,false,false));
    check(!MeleeTracePolicy::Recover(true,true,.5f,false,false,.50005f,false,false));
    check(!AmmoConservationPolicy::Covered(33.53275f,28)); // Old screening-only example is not enough.
    check(AmmoConservationPolicy::Covered(45.f,28));
    check(!AmmoConservationPolicy::Covered(44.99f,28));
    check(!AmmoConservationPolicy::Covered(100,0));
    check(!AmmoConservationPolicy::Covered(100,-1));
    check(!AmmoConservationPolicy::Covered(std::numeric_limits<float>::quiet_NaN(),1));
    check(!AmmoConservationPolicy::Covered(std::numeric_limits<float>::infinity(),1));
    for (int hp=1;hp<=300;++hp)
    {
        check(!AmmoConservationPolicy::Covered(float(hp),hp));
        check(AmmoConservationPolicy::Covered(hp*1.25f+10.f,hp));
    }
    {
        AmmoConservationPolicy::Window action;
        check(action.Allow(1000));
        check(action.Allow(1050));
        check(action.Allow(1099));
        // Repeated requests/target changes cannot extend the same window.
        for (unsigned long long now=1100;now<2100;++now) check(!action.Allow(now));
        check(action.Allow(2100));
        check(!action.Allow(2200));
        check(!action.Allow(2199)); // Monotonic-clock violation fails open.
    }
    check(AmmoConservationPolicy::ImpactWindow(.05f,.04f,.02f));
    check(AmmoConservationPolicy::ImpactWindow(.05f,0,0));
    check(!AmmoConservationPolicy::ImpactWindow(.1f,.06f,0));
    check(!AmmoConservationPolicy::ImpactWindow(.05f,.101f,0));
    check(!AmmoConservationPolicy::ImpactWindow(.02f,.01f,.101f));
    check(!AmmoConservationPolicy::ImpactWindow(0,.01f,0));
    check(!AmmoConservationPolicy::ImpactWindow(.02f,-.01f,0));
    check(!AmmoConservationPolicy::ImpactWindow(.02f,.01f,-.01f));
    check(!AmmoConservationPolicy::ImpactWindow(std::numeric_limits<float>::quiet_NaN(),.01f,0));
    check(!AmmoConservationPolicy::ImpactWindow(.02f,std::numeric_limits<float>::infinity(),0));
    check(!AmmoConservationPolicy::ImpactWindow(.02f,.01f,std::numeric_limits<float>::quiet_NaN()));
    // The Direct Hit's smaller radius must never inherit standard rocket coverage.
    check(AmmoEvidencePolicy::SplashScreen(45.f,80.f,146.f)>0);
    check(AmmoEvidencePolicy::SplashScreen(45.f,80.f,43.8f)==0);
    // Discounted Scorch damage cannot promise a healthy target's death.
    check(!AmmoConservationPolicy::Covered(AmmoEvidencePolicy::SplashScreen(20.f,0,110.f),25));
    check(AmmoConservationPolicy::Covered(8.f,4,2.f));
    check(!AmmoConservationPolicy::Covered(8.f,5,2.f));
    check(!AmmoConservationPolicy::Covered(8.f,4,10.f));
    check(!AmmoConservationPolicy::Covered(8.f,4,1.f));
    check(!AmmoConservationPolicy::Covered(8.f,4,std::numeric_limits<float>::quiet_NaN()));
    using AmmoConservationPolicy::CoreFraction;
    check(std::abs(CoreFraction({-40,0,40},{40,0,40},{-24,-24,0},{24,24,82},8)-.3f)<.0001f);
    check(CoreFraction({-40,23,40},{40,23,40},{-24,-24,0},{24,24,82},8)<0); // Grazing is not enough.
    check(CoreFraction({-40,0,40},{-25,0,40},{-24,-24,0},{24,24,82},8)<0);
    check(CoreFraction({0,0,40},{0,0,40},{-24,-24,0},{24,24,82},8)==0);
    check(CoreFraction({-40,0,40},{40,0,40},{-24,-24,0},{24,24,82},24)<0); // No interior left.
    check(CoreFraction({-40,0,40},{40,0,40},{24,-24,0},{-24,24,82},8)<0);
    check(CoreFraction({-40,0,40},{40,0,40},{-24,-24,0},{24,24,82},-1)<0);
    check(CoreFraction({-40,0,40},{40,0,40},{-24,-24,0},{24,24,82},std::numeric_limits<float>::quiet_NaN())<0);
    check(CoreFraction({-40,std::numeric_limits<float>::infinity(),40},{40,0,40},{-24,-24,0},{24,24,82},8)<0);
    check(CoreFraction({40,0,40},{-40,0,40},{-24,-24,0},{24,24,82},8)>=0);
    // A grenade alone or with a sticky can cover health; neither source is mandatory.
    check(AmmoConservationPolicy::Covered(30,10));
    check(!AmmoConservationPolicy::Covered(30,30));
    check(AmmoConservationPolicy::Covered(30+40,30));
    check(AmmoConservationPolicy::Covered(40,20));
    check(AmmoConservationPolicy::FreshPill(.65f,2.f,10.05f,10.f));
    check(!AmmoConservationPolicy::FreshPill(.95f,2.f,10.05f,10.f));
    check(!AmmoConservationPolicy::FreshPill(.65f,1.f,10.05f,10.f));
    check(!AmmoConservationPolicy::FreshPill(.65f,2.f,10.05f,9.f));
    check(!AmmoConservationPolicy::FreshPill(-.1f,2.f,10.05f,10.f));
    check(!AmmoConservationPolicy::FreshPill(.65f,2.f,10.05f,0));
    check(!AmmoConservationPolicy::FreshPill(.65f,std::numeric_limits<float>::quiet_NaN(),10.05f,10.f));
    using AmmoConservationPolicy::StickyHoldSafe;
    check(StickyHoldSafe(0,10,4,.05f));
    check(StickyHoldSafe(10,10.6f,4,.05f));
    check(!StickyHoldSafe(10,13.75f,4,.05f));
    check(!StickyHoldSafe(10,14,4,.05f));
    check(!StickyHoldSafe(10,9.99f,4,.05f));
    check(!StickyHoldSafe(10,10.6f,0,.05f));
    check(!StickyHoldSafe(10,10.6f,4,0));
    check(!StickyHoldSafe(10,10.6f,4,.16f));
    check(!StickyHoldSafe(std::numeric_limits<float>::quiet_NaN(),10.6f,4,.05f));
    check(!StickyHoldSafe(10,std::numeric_limits<float>::infinity(),4,.05f));
    check(!StickyHoldSafe(10,10.6f,std::numeric_limits<float>::quiet_NaN(),.05f));
    {
        AmmoConservationPolicy::ChargeOwnership charge;
        charge.Observe(1,2,0,false);
        check(charge.Owned(0)); // Can prevent a new automated charge.
        check(!charge.Owned(10)); // Unknown in-progress charge cannot be held.
        charge.Record(true,true);
        charge.Observe(1,2,10,false);
        check(charge.Owned(10)); // Recognized automation may hold or switch.
        charge.Record(true,true);
        charge.Observe(1,2,10,true); // Human takes control.
        check(!charge.Owned(10));
        charge.Record(true,true);
        charge.Observe(1,2,10,false); // Key-up still belongs to the human.
        check(!charge.Owned(10));
        charge.Observe(1,2,0,false); // Charge has actually ended.
        check(charge.Owned(0));
        charge.Record(true,true);
        charge.Observe(1,3,10,false); // Weapon switch invalidates ownership.
        check(!charge.Owned(10));
        charge.Record(true,true);
        charge.Observe(4,3,10,false); // Respawn/entity serial change.
        check(!charge.Owned(10));
        charge.Record(true,true);
        charge.Record(false,true); // Releasing ends automated hold ownership.
        check(!charge.Owned(10));
        charge.Record(true,false); // Disabling Auto shoot does not claim a charge.
        check(!charge.Owned(10));
        charge.Observe(4,3,std::numeric_limits<float>::quiet_NaN(),false);
        check(!charge.Owned(10));
        check(!charge.Owned(-1));
    }
    check(ArcPolicy::Eligible(true,800)); // No splash-radius or underpredict dependency.
    check(!ArcPolicy::Eligible(false,800));
    check(!ArcPolicy::Eligible(true,0));
    check(!ArcPolicy::Eligible(true,-800));
    check(!ArcPolicy::Eligible(true,std::numeric_limits<double>::quiet_NaN()));
    ArcPolicy::Solution low,high;
    check(ArcPolicy::Solve(500,0,1217,800,false,low));
    check(ArcPolicy::Solve(500,0,1217,800,true,high));
    check(high.pitch>low.pitch && high.time>low.time);
    check(!ArcPolicy::FlightWithin(high.time,2,1./66,int(std::ceil(high.time*66)))); // A real high arc can exceed a pill's fuse.
    check(ArcPolicy::Solve(0,100,1000,800,false,low));
    check(ArcPolicy::Solve(0,100,1000,800,true,high));
    check(low.time<high.time && low.pitch==high.pitch);
    check(std::abs(1000*low.time-400*low.time*low.time-100)<1e-6);
    check(ArcPolicy::Solve(0,-100,1000,800,false,low));
    check(low.pitch<0 && std::abs(-1000*low.time-400*low.time*low.time+100)<1e-6);
    check(!ArcPolicy::Solve(0,0,1000,800,false,low));
    check(ArcPolicy::Solve(0,0,1000,800,true,high));
    check(ArcPolicy::Solve(0,100,1000,0,false,low));
    check(std::abs(low.time-.1)<1e-9);
    check(!ArcPolicy::Solve(500,0,0,800,false,low));
    check(!ArcPolicy::Solve(500,0,-1,800,false,low));
    check(!ArcPolicy::Solve(500,0,1000,-800,false,low));
    check(!ArcPolicy::Solve(-500,0,1000,800,false,low));
    check(!ArcPolicy::Solve(500,0,std::numeric_limits<double>::infinity(),800,false,low));
    check(!ArcPolicy::Solve(500,std::numeric_limits<double>::quiet_NaN(),1000,800,false,low));
    check(ArcPolicy::Solve(1250,0,1000,800,true,high)); // Maximum range: roots coincide.
    check(!ArcPolicy::Solve(1250.1,0,1000,800,true,high));
    for (double speed : {900.,1217.,2400.}) for (double distance : {1.,50.,500.,1000.})
        for (double height : {-100.,0.,100.}) for (bool lob : {false,true})
        {
            ArcPolicy::Solution solution;
            if (!ArcPolicy::Solve(distance,height,speed,800,lob,solution)) continue;
            check(std::abs(speed*std::cos(solution.pitch)*solution.time-distance)<1e-6);
            check(std::abs(speed*std::sin(solution.pitch)*solution.time-400*solution.time*solution.time-height)<1e-6);
        }
    const int arcInterval=ArcPolicy::TraceInterval(20,800,1./66);
    check(arcInterval==3);
    check(800*std::pow(arcInterval/66.,2)/8<=ArcPolicy::CollisionChordError);
    for (double gravity : {200.,400.,800.,1600.})
        for (int requested : {1,2,4,8,20})
        {
            const int interval=ArcPolicy::TraceInterval(requested,gravity,1./66);
            check(interval>=1 && interval<=requested);
            check(gravity*std::pow(interval/66.,2)/8<=ArcPolicy::CollisionChordError);
        }
    check(ArcPolicy::TraceInterval(1,800,1./66)==1);
    check(ArcPolicy::TraceInterval(20,0,1./66)==20);
    check(ArcPolicy::TraceInterval(20,800,0)==1);
    check(ArcPolicy::TraceInterval(0,800,1./66)==1);
    check(ArcPolicy::TraceInterval(20,std::numeric_limits<double>::quiet_NaN(),1./66)==1);
    check(ArcPolicy::FlightWithin(1,2,1./66,66));
    check(!ArcPolicy::FlightWithin(2.1,2,1./66,139));
    check(!ArcPolicy::FlightWithin(1,2,1./66,150));
    check(!ArcPolicy::FlightWithin(0,2,1./66,1));
    check(!ArcPolicy::FlightWithin(std::numeric_limits<double>::quiet_NaN(),2,1./66,1));
    {
        const ArcPolicy::DragKey original={1217,0,1,true,false,false};
        auto changed=original;
        check(original.Matches(changed));
        changed.type=2;check(!original.Matches(changed));
        changed=original;changed.lob=true;check(!original.Matches(changed));
        changed=original;changed.noSpin=true;check(!original.Matches(changed));
        changed=original;changed.physics=false;check(!original.Matches(changed));
        changed=original;changed.overrideValue=.1f;check(!original.Matches(changed));
        changed=original;changed.speed=1218;check(!original.Matches(changed));
    }
    {
        using namespace AimbotAuditPolicy;
        check(Bounds({-24,-24,0},{24,24,62}));
        check(!Bounds({0,0,0},{0,24,62}));
        check(!Bounds({0,0,0},{24,24,std::numeric_limits<float>::quiet_NaN()}));
        check(ClearAdvance(1,false,false));
        check(!ClearAdvance(.9f,false,false));
        check(!ClearAdvance(1,true,false));
        check(!ClearAdvance(1,false,true));
        check(!ClearAdvance(2,false,false));
        check(!ClearAdvance(std::numeric_limits<float>::quiet_NaN(),false,false));
        check(RedirectLifetime(false,-1,.1f,10)==10);
        check(RedirectLifetime(true,-1,.1f,10)==0);
        check(RedirectLifetime(true,.1f,.1f,10)==0);
        check(std::abs(RedirectLifetime(true,1,.25f,10)-.75f)<.00001f);
        check(RedirectLifetime(true,2,0,1)==1);
        check(RedirectLifetime(false,-1,-1,10)==0);
        check(RedirectLifetime(false,-1,0,0)==0);
        check(RedirectLifetime(true,std::numeric_limits<float>::infinity(),0,10)==0);
        DamageHistory damage;
        damage.Record(123,1,100,10);
        check(damage.Applies(123,10));
        check(damage.Applies(123,10.9f));
        check(!damage.Applies(124,10));
        check(!damage.Applies(123,11));
        check(!damage.Applies(123,9));
        damage.Record(124,0,50,10);
        check(!damage.Applies(123,10));
        check(damage.Applies(124,10));
        damage={};check(!damage.Applies(124,10));
        damage.Record(124,0,0,10);check(!damage.Applies(124,10));
        damage.Record(124,3,100,10);check(!damage.Applies(124,10));
    }
    {
        namespace Aim=ProjectileAimPolicy;
        for(int mask=0;mask<128;++mask)
        {
            check(Aim::Hitboxes(mask,true,false)==mask);
            check(Aim::Hitboxes(mask,false,true)==((mask&Aim::TrueFeet)?mask|Aim::Feet:mask));
            const int bodyMask=Aim::Hitboxes(mask,true,true);
            if(mask&Aim::TrueFeet) {check(bodyMask&Aim::Feet);check(Aim::Priority(2,2,mask,true,true)==0);}
            else if(mask&(Aim::Head|Aim::Body|Aim::Feet)) {check(bodyMask&Aim::Body);check(!(bodyMask&Aim::Feet));check(Aim::Priority(1,1,mask,true,true)==0);}
            else check(bodyMask==mask);
        }
        check(Aim::Hitboxes(Aim::Feet,true,true)==Aim::Body);
        check(Aim::Priority(0,0,0,true,true)==1);
        check(Aim::Priority(2,0,0,true,true)==-1);
        check(Aim::Priority(2,0,0,false,true)==0);
        for(int mode=0;mode<4;++mode)
        {
            check(Aim::Splash(mode,true,false)==Aim::SplashOff);
            check(Aim::Splash(mode,true,true)==Aim::SplashInclude);
            check(Aim::Splash(mode,false,true)==mode);
            check(Aim::Splash(mode,false,false)==mode);
        }
        namespace Muzzle=ProjectileMuzzlePolicy;
        check(!Muzzle::flipOverride.has_value());
        {Muzzle::Trial left(true);check(Muzzle::flipOverride==true);{Muzzle::Trial right(false);check(Muzzle::flipOverride==false);}check(Muzzle::flipOverride==true);}
        check(!Muzzle::flipOverride.has_value());
        check(Muzzle::NearWall(true,false,false,10));
        check(!Muzzle::NearWall(true,false,true,10));
        check(!Muzzle::NearWall(true,false,false,48));
        check(!Muzzle::NearWall(false,false,false,10));
        check(Muzzle::NearWall(false,true,false,100));
        check(!Muzzle::NearWall(true,false,false,std::numeric_limits<float>::quiet_NaN()));
        check(Muzzle::Retry(true,true,false,false,false,true,0));
        check(!Muzzle::Retry(false,true,false,false,false,true,0));
        check(!Muzzle::Retry(true,false,false,false,false,true,0));
        check(!Muzzle::Retry(true,true,true,false,false,true,0));
        check(!Muzzle::Retry(true,true,false,true,false,true,0));
        check(!Muzzle::Retry(true,true,false,false,true,true,0));
        check(!Muzzle::Retry(true,true,false,false,false,false,0));
        check(!Muzzle::Retry(true,true,false,false,false,true,1));
        check(!Muzzle::Retry(true,true,false,false,false,true,2));
        check(Muzzle::Pending(10,11,20,19));
        check(Muzzle::Pending(11,11,18,19));
        check(!Muzzle::Pending(11,11,20,19));
        check(!Muzzle::Pending(0,0,0,0));
        check(Muzzle::Pending(11,11,20,std::numeric_limits<float>::quiet_NaN()));
        check(Muzzle::RejectImpact(false,true,false,false,10));
        check(!Muzzle::RejectImpact(true,true,false,false,10));
        check(!Muzzle::RejectImpact(true,true,false,false,47));
        check(Muzzle::RejectImpact(true,true,true,false,10));
        check(!Muzzle::RejectImpact(true,true,true,true,0));
        check(!Muzzle::RejectImpact(false,true,false,true,10));
        check(!Muzzle::RejectImpact(false,true,false,false,48));
        check(Muzzle::StickySwitchSafe(true,0));
        check(!Muzzle::StickySwitchSafe(true,.1f));
        check(!Muzzle::StickySwitchSafe(true,-1));
        check(!Muzzle::StickySwitchSafe(true,std::numeric_limits<float>::quiet_NaN()));
        check(Muzzle::StickySwitchSafe(false,10));
        check(Muzzle::ChargedSwitchSafe(true,0));
        check(!Muzzle::ChargedSwitchSafe(true,.1f));
        check(!Muzzle::ChargedSwitchSafe(true,-1));
        check(!Muzzle::ChargedSwitchSafe(true,std::numeric_limits<float>::quiet_NaN()));
        check(!Muzzle::ChargedSwitchSafe(true,std::numeric_limits<float>::infinity()));
        check(Muzzle::ChargedSwitchSafe(false,10));
        check(Muzzle::SwitchImpact(true,false,false,60));
        check(Muzzle::SwitchImpact(true,false,false,127));
        check(!Muzzle::SwitchImpact(true,false,false,128));
        check(!Muzzle::SwitchImpact(true,false,true,10));
        check(Muzzle::SwitchImpact(false,true,false,0));
        check(!Muzzle::SwitchImpact(false,false,false,60));
        check(!Muzzle::SwitchImpact(true,false,false,std::numeric_limits<float>::quiet_NaN()));
        Muzzle::Probe obstruction={true,true,64,4,"path_obstructed"};
        Muzzle::Probe alternate={true,false,128,10,"clear_to_range"};
        check(Muzzle::CanSwitch(obstruction,alternate));
        check(!Muzzle::CanSwitch(obstruction,obstruction));
        check(!Muzzle::CanSwitch(alternate,alternate));
        Muzzle::Probe incomplete;check(!Muzzle::CanSwitch(obstruction,incomplete));
        check(Muzzle::ReturnRight(true,true,alternate));
        check(!Muzzle::ReturnRight(false,true,alternate));
        check(!Muzzle::ReturnRight(true,false,alternate));
        check(!Muzzle::ReturnRight(true,true,obstruction));
        check(!Muzzle::ReturnRight(true,true,incomplete));
        check(!Muzzle::CanSwitch(incomplete,alternate));
        alternate={true,false,20,1,"actor_first"};check(Muzzle::CanSwitch(obstruction,alternate));
    }
    {
        namespace Contact=MeleeContactPolicy;
        using Point=Contact::Point;
        Point out;
        check(Contact::InFOV(0,1));check(Contact::InFOV(.99,1));
        check(!Contact::InFOV(1,1));check(!Contact::InFOV(0,0));
        check(!Contact::InFOV(-1,1));check(!Contact::InFOV(std::numeric_limits<double>::quiet_NaN(),1));
        check(!Contact::InFOV(1,std::numeric_limits<double>::infinity()));
        // A narrow view ray intersects the body while its closest-to-eye point
        // lies more than five degrees away from that ray.
        const double slope=std::tan(10*3.141592653589793/180);
        check(Contact::ViewPoint({0,0,0},{100,100*slope,0},{40,3,-10},{60,23,10},out));
        check(std::abs(out[1]-out[0]*slope)<1e-8);
        check(out[0]>=40 && out[0]<=60 && out[1]>=3 && out[1]<=23);
        check(Contact::ViewPoint({0,0,0},{100,0,0},{40,3,-10},{60,23,10},out));
        check(std::abs(out[1]-3)<1e-8 && out[0]>=40 && out[0]<=60);
        check(Contact::ViewPoint({0,0,0},{10,0,0},{40,-1,-1},{60,1,1},out));
        check(out[0]==40 && out[1]==0);
        check(Contact::ViewPoint({0,0,0},{100,0,0},{-60,-1,-1},{-40,1,1},out));
        check(out[0]==-40);
        check(Contact::ViewPoint({50,0,0},{100,0,0},{40,-1,-1},{60,1,1},out));
        check(out[0]>50 && out[0]<=60);
        check(!Contact::ViewPoint({0,0,0},{0,0,0},{40,-1,-1},{60,1,1},out));
        check(!Contact::ViewPoint({0,0,0},{100,0,0},{60,-1,-1},{40,1,1},out));
        check(!Contact::ViewPoint({0,0,0},{std::numeric_limits<double>::infinity(),0,0},{40,-1,-1},{60,1,1},out));
        for(double y : {-50.,-20.,0.,20.,50.}) for(double z : {-30.,0.,30.}) for(double x : {-40.,40.,100.})
        {
            const Point mins={x,y-10,z-10},maxs={x+20,y+10,z+10};
            check(Contact::ViewPoint({0,0,0},{100,0,0},mins,maxs,out));
            for(int axis=0;axis<3;++axis) check(std::isfinite(out[axis]) && out[axis]>=mins[axis] && out[axis]<=maxs[axis]);
            const double distance=std::pow(out[0]-std::clamp(out[0],0.,100.),2)+out[1]*out[1]+out[2]*out[2];
            const double expected=std::pow(std::clamp(0.,mins[0],maxs[0])-std::clamp(std::clamp(0.,mins[0],maxs[0]),0.,100.),2)
                +std::pow(std::clamp(0.,mins[1],maxs[1]),2)+std::pow(std::clamp(0.,mins[2],maxs[2]),2);
            check(distance<=expected+1e-5);
        }
    }
    {
        namespace BT=BacktrackPolicy;
        const double tick=1./66,safe=BT::SafeWindow(tick);
        check(std::abs(safe-(.2-tick))<1e-12);
        check(BT::Window(.2,tick)==safe);check(BT::Window(.19,tick)==safe);
        check(BT::Window(.1,tick)==.1);check(BT::Window(0,tick)==0);
        check(BT::Window(-1,tick)==0);check(BT::SafeWindow(0)==0);
        check(BT::Window(std::numeric_limits<double>::quiet_NaN(),tick)==0);
        check(BT::Within(safe,safe));check(!BT::Within(safe+.0001,safe));
        check(!BT::Within(.199,safe));check(!BT::Within(0,0));
        check(!BT::Within(std::numeric_limits<double>::quiet_NaN(),safe));
        check(BT::Usable(10,false,10.1,1,tick,0));
        check(!BT::Usable(10,true,10.1,1,tick,0));
        check(!BT::Usable(8,false,10.1,1,tick,0));
        check(!BT::Usable(11,false,10.1,1,tick,0));
        check(BT::Usable(10.2,false,10,1,tick,-.2));
        check(!BT::Usable(10.22,false,10,1,tick,-.2));
        check(!BT::Usable(std::numeric_limits<double>::max(),false,10,1,tick,0));
        check(!BT::Usable(std::numeric_limits<double>::quiet_NaN(),false,10,1,tick,0));
        check(BT::NewSample(10.1,10));check(!BT::NewSample(10,10));check(!BT::NewSample(9.9,10));
        check(!BT::NewSample(std::numeric_limits<double>::infinity(),10));
        for(double interval : {1./30,1./66,1./100,1./128})
            for(double requested : {0.,.05,.1,.185,.19,.2,.3})
            {
                const double window=BT::Window(requested,interval);
                check(window>=0 && window<=BT::SafeWindow(interval));
                if(window>0)
                {check(BT::Within(window,window));check(!BT::Within(window+interval,window));}
            }
    }
    {
        namespace Cursor=CursorBacktrackPolicy;
        const Cursor::Matrix identity={{{1,0,0,0},{0,1,0,0},{0,0,1,0}}};
        double distance=-1;
        check(Cursor::HitDistance({0,0,0},{1,0,0},{10,-1,-1},{12,1,1},identity,100,distance));
        check(distance==10);
        check(!Cursor::HitDistance({0,2,0},{1,0,0},{10,-1,-1},{12,1,1},identity,100,distance));
        check(!Cursor::HitDistance({0,0,0},{-1,0,0},{10,-1,-1},{12,1,1},identity,100,distance));
        check(!Cursor::HitDistance({0,0,0},{1,0,0},{10,-1,-1},{12,1,1},identity,9.99,distance));
        check(Cursor::HitDistance({0,0,0},{1,0,0},{10,-1,-1},{12,1,1},identity,10,distance));
        check(!Cursor::HitDistance({0,0,0},{0,0,0},{10,-1,-1},{12,1,1},identity,100,distance));
        check(!Cursor::HitDistance({0,0,0},{2,0,0},{10,-1,-1},{12,1,1},identity,100,distance));
        check(!Cursor::HitDistance({0,0,0},{1,0,0},{12,-1,-1},{10,1,1},identity,100,distance));
        check(!Cursor::HitDistance({11,0,0},{1,0,0},{10,-1,-1},{12,1,1},identity,100,distance));
        auto bad=identity;bad[0][0]=0;
        check(!Cursor::HitDistance({0,0,0},{1,0,0},{10,-1,-1},{12,1,1},bad,100,distance));
        bad=identity;bad[0][3]=std::numeric_limits<double>::quiet_NaN();
        check(!Cursor::HitDistance({0,0,0},{1,0,0},{10,-1,-1},{12,1,1},bad,100,distance));
        check(!Cursor::HitDistance({0,0,0},{1,0,0},{10,-1,-1},{12,1,1},identity,std::numeric_limits<double>::infinity(),distance));
        // Rotation, translation, uniform/nonuniform scale, negative axes and
        // shear all retain the world-distance ray parameter.
        for (double scale : {.5,1.,2.,4.})
        {
            const Cursor::Matrix transformed={{{0,-scale,0,50},{scale,0,0,0},{0,0,scale,0}}};
            check(Cursor::HitDistance({0,0,0},{1,0,0},{-1,-2,-1},{1,2,1},transformed,100,distance));
            check(std::abs(distance-(50-2*scale))<1e-8);
            check(!Cursor::HitDistance({0,5*scale,0},{1,0,0},{-1,-2,-1},{1,2,1},transformed,100,distance));
        }
        const Cursor::Matrix shear={{{2,1,0,50},{0,3,0,0},{0,0,4,0}}};
        check(Cursor::HitDistance({0,0,0},{1,0,0},{-1,-1,-1},{1,1,1},shear,100,distance));
        check(distance==48);
        // Exhaust all gates: assistance, generated attacks, use actions,
        // switching and non-hitscan/cooldown commands must never rewind.
        for (int mask=0;mask<256;++mask)
        {
            const bool expected=(mask&31)==31 && !(mask&224);
            check(Cursor::ManualShot(mask&1,mask&2,mask&4,mask&8,mask&16,mask&32,mask&64,mask&128)==expected);
        }
    }
    {
        namespace Trigger=TriggerPolicy;
        auto delay=Trigger::Delay(false,999,0,0,0);
        check(delay && *delay==999);
        check(*Trigger::Delay(false,-2,0,0,0)==0);
        check(*Trigger::Delay(false,.001,0,0,0)==.001);
        check(!Trigger::Delay(false,std::numeric_limits<double>::infinity(),0,0,0));
        check(!Trigger::Delay(false,std::numeric_limits<double>::quiet_NaN(),0,0,0));
        check(!Trigger::Delay(true,0,.1,.15,std::numeric_limits<double>::quiet_NaN()));
        for (double lower : {-.5,.05,.1,.3,.5,10.}) for (double upper : {-.5,.05,.1,.3,.5,10.})
        {
            const auto bounds=Trigger::Bounds(lower,upper);
            check(bounds.first>=.05 && bounds.second<=.5 && bounds.first<bounds.second);
            for (double unit : {0.,.1,.5,.9,1.})
            {
                const auto value=Trigger::Delay(true,999,lower,upper,unit);
                check(value && *value>=bounds.first && *value<=bounds.second);
            }
        }
        for (int step=0;step<=1000;++step)
        {
            const auto value=Trigger::Delay(true,999,.1,.15,step/1000.);
            check(value && *value>=.1 && *value<=.15);
        }
        Trigger::Timer timer;
        check(!timer.Update(1,2,10,.15,true));
        check(!timer.Update(1,2,10.1,.001,true));check(timer.delay==.15);
        check(timer.Update(1,2,10.15,999,true)); // Sample is held, not rerolled.
        timer.Reset();check(!timer.Update(1,2,11,999,true));
        check(!timer.Update(1,2,1009.99,999,true));check(timer.Update(1,2,1010,999,true));
        timer.Reset();check(!timer.Update(1,2,12,.1,true));
        check(!timer.Update(-1,2,12.05,.1,true));check(!timer.active);
        check(!timer.Update(1,2,12.2,.1,true));check(!timer.Update(1,2,12.25,.1,true));
        check(timer.Update(1,2,12.3,.1,true));
        check(!timer.Update(3,2,12.35,.1,true)); // Different enemy resets.
        check(!timer.Update(3,4,12.4,.1,true)); // Different weapon resets.
        check(!timer.Update(3,4,12.5,.1,false)); // Ready timer cannot bypass cooldown.
        check(timer.Update(3,4,12.6,.1,true));
        check(!timer.Update(3,4,12.3,.1,true)); // Clock reversal resets.
        timer.Reset();check(timer.Update(1,2,13,0,true));
        check(!timer.Update(1,2,std::numeric_limits<double>::quiet_NaN(),0,true));
        check(!timer.Update(1,2,13,std::numeric_limits<double>::quiet_NaN(),true));
    }
    {
        namespace Combat=CombatPriorityPolicy;
        check(Combat::Priority(5,false,true,true,true)==5);
        check(Combat::Priority(0,true,false,false,false)==0);
        check(Combat::Priority(0,true,true,false,false)>Combat::Priority(0,true,false,false,false));
        check(Combat::Priority(0,true,false,false,true)>Combat::Priority(0,true,true,true,false));
        check(Combat::Priority(1,true,false,false,false)>Combat::Priority(0,true,true,true,true));
        check(Combat::Priority(std::numeric_limits<int>::max(),true,true,true,true)==std::numeric_limits<int>::max());
        check(Combat::Priority(std::numeric_limits<int>::min(),true,false,false,false)==std::numeric_limits<int>::min());
        check(Combat::FOV(true,180,20)==20);check(Combat::FOV(false,180,20)==180);
        check(Combat::FOV(true,90,0)==0);check(Combat::FOV(true,90,999)==180);
        check(Combat::FOV(true,90,-1)==0);
        check(Combat::FOV(true,90,std::numeric_limits<float>::quiet_NaN())==0);
        check(Combat::FOV(false,90,std::numeric_limits<float>::quiet_NaN())==90);
    }
    {
        FlareComboPolicy::State state;
        check(state.Arm(123,456,10));
        check(!state.Arm(789,456,10.1)); // A new flame tick cannot reroll the combo.
        check(state.Valid(123,456,10.2,true,true));
        state.switched=true;
        check(state.Valid(123,456,11.5,true,true));
        check(!state.Valid(123,456,11.501,true,true));check(!state.active);
        check(state.Arm(123,456,12));check(!state.Valid(124,456,12.1,true,true));
        check(state.Arm(123,456,12));check(!state.Valid(123,457,12.1,true,true));
        check(state.Arm(123,456,12));check(!state.Valid(123,456,12.1,false,true));
        check(state.Arm(123,456,12));check(!state.Valid(123,456,12.1,true,false));
        check(state.Arm(123,456,12));check(!state.Valid(123,456,11.9,true,true));
        check(!state.Arm(-1,456,12));check(!state.Arm(123,-1,12));
        check(!state.Arm(123,456,std::numeric_limits<double>::infinity()));
    }
    {
        FlareComboPolicy::State state;
        check(state.Arm(123,456,20));
        check(!state.Shot(20.1));
        state.switched=true;
        check(state.Shot(20.1));
        check(state.returning && !state.switched && !state.waiting);
        check(!state.Retry(20.2));
        check(!state.Shot(20.2));
        check(state.Valid(123,456,20.2,true,true));
        check(state.Returned(20.3));
        check(state.waiting && !state.returning && !state.switched);
        check(state.Valid(123,456,22.3,true,true)); // Cooldown longer than the old 1.5 s attempt window.
        check(state.Retry(22.3));
        check(!state.waiting && !state.switched);
        state.switched=true;
        check(state.Shot(22.4));
        check(state.Returned(22.5));
        check(!state.Valid(124,456,22.6,true,true));
        check(!state.active);
        check(state.Arm(123,456,30));state.switched=true;
        check(!state.Shot(std::numeric_limits<double>::quiet_NaN()));
        check(!state.Shot(29));
        check(state.Shot(30.1));
        check(!state.Returned(30));
        check(state.Returned(30.2));
        check(!state.Valid(123,456,40.201,true,true)); // Do not retain an idle stale target indefinitely.
        check(FlareComboPolicy::Ready(12,12,12,12));
        check(!FlareComboPolicy::Ready(12,12.01,11,11));
        check(!FlareComboPolicy::Ready(12,11,12.01,11));
        check(!FlareComboPolicy::Ready(12,11,11,12.01));
        check(!FlareComboPolicy::Ready(12,std::numeric_limits<double>::quiet_NaN(),11,11));
        check(!FlareComboPolicy::Ready(std::numeric_limits<double>::infinity(),11,11,11));
    }
    {
        namespace Flare=FlareComboPolicy;
        check(Flare::Enabled(true,true,true,true));
        check(!Flare::Enabled(false,true,true,true));
        check(!Flare::Enabled(true,false,true,true));
        check(!Flare::Enabled(true,true,false,true));
        check(!Flare::Enabled(true,true,true,false));
        check(Flare::ShotMatches(101,101,2,2));
        check(!Flare::ShotMatches(100,101,2,2));
        check(!Flare::ShotMatches(101,101,3,2));
        check(!Flare::ShotMatches(101,101,2,3));
        check(!Flare::ShotMatches(0,0,2,2));
        check(!Flare::ShotMatches(101,101,-1,-1));
        Flare::State state;
        check(state.Arm(123,456,50));
        state.switched=true;
        check(!Flare::Ready(50.1,50.5,50.5,50));
        check(state.Valid(123,456,50.1,true,Flare::Enabled(true,true,true,true)));
        check(state.active && state.switched); // Deploy cooldown is not disabling the feature.
        check(state.Shot(50.6));
        check(!Flare::Ready(50.7,52,51,52));
        check(state.Valid(123,456,50.7,true,true) && state.returning);
        check(state.Returned(51));
        check(!Flare::Ready(52,53,51,53));
        check(state.Valid(123,456,52.8,true,true) && state.waiting);
        check(Flare::Ready(53,53,51,53));
        check(state.Retry(53));
        check(state.active && !state.waiting);
        check(!state.Valid(123,456,53.1,true,Flare::Enabled(false,true,true,true)));
        check(!state.active);
    }
    {
        FlareComboPolicy::State state;
        check(!state.AbortToReturn(60));
        check(state.Arm(123,456,60));
        check(!state.AbortToReturn(60.1));
        state.switched=true;
        auto probe=state;
        check(!probe.Valid(124,456,60.2,true,true));
        check(state.active && state.switched); // Target validation must not erase cleanup state.
        check(!state.AbortToReturn(59));
        check(!state.AbortToReturn(std::numeric_limits<double>::quiet_NaN()));
        check(state.AbortToReturn(60.2)); // Extinguished/dead/lost target: return even without firing.
        check(state.returning && !state.switched && !state.waiting);
        check(!state.Shot(60.3));
        check(!state.AbortToReturn(60.3));
        check(state.Valid(123,456,62.5,true,true)); // Return survives a temporary switch delay.
        check(state.Returned(62.6));
        check(state.waiting && !state.returning);
        state.Reset();check(state.Arm(123,456,70));state.switched=true;
        check(state.Shot(70.1));
        check(state.Valid(123,456,72.2,true,true));
        check(!state.Valid(123,456,80.101,true,true));
        check(!state.active); // Cleanup still has a bounded lifetime.
    }
    {
        FlareComboPolicy::State state;
        check(state.Arm(123,456,90));
        check(state.IsValid(123,456,91.5,true,true));
        check(!state.IsValid(123,456,91.501,true,true));
        check(state.active); // Read-only validation preserves cleanup state.
        check(!state.AbortToReturn(91.501));
        state.Reset();check(state.Arm(123,456,92));state.switched=true;
        check(!state.IsValid(124,456,92.1,true,true));
        check(state.active && state.switched);
        check(state.AbortToReturn(92.1));
        check(state.IsValid(123,456,95,true,true));
        check(!state.IsValid(123,457,95,true,true));
        check(!state.IsValid(123,456,95,false,true));
        check(!state.IsValid(123,456,95,true,false));
        check(!state.IsValid(123,456,102.101,true,true));
        check(state.Returned(95));
        check(state.IsValid(123,456,104.9,true,true));
        check(state.Retry(104.9));
        check(!state.IsValid(123,456,106.401,true,true));
        check(!state.IsValid(123,456,std::numeric_limits<double>::infinity(),true,true));
        check(!state.IsValid(123,456,104,true,true));
    }
    {
        namespace FOV=AimFOVVisualPolicy;
        const auto small=FOV::Project(20,90,1920,1080);
        const auto medium=FOV::Project(40,90,1920,1080);
        const auto large=FOV::Project(50,90,1920,1080);
        check(small.visible && !small.fullViewport);
        check(medium.visible && !medium.fullViewport);
        check(large.radius>medium.radius && medium.radius>small.radius);
        check(medium.radius>1080*.48); // No old artificial cap.
        check(FOV::Project(60,90,1920,1080).fullViewport);
        check(FOV::Project(89,90,1920,1080).fullViewport);
        check(FOV::Project(90,90,1920,1080).fullViewport);
        check(FOV::Project(180,90,1920,1080).fullViewport);
        check(!FOV::Project(0,90,1920,1080).visible);
        check(!FOV::Project(-1,90,1920,1080).visible);
        check(!FOV::Project(20,0,1920,1080).visible);
        check(!FOV::Project(20,180,1920,1080).visible);
        check(!FOV::Project(20,90,0,1080).visible);
        check(!FOV::Project(std::numeric_limits<double>::quiet_NaN(),90,1920,1080).visible);
        check(!FOV::Project(20,std::numeric_limits<double>::infinity(),1920,1080).visible);
        check(FOV::Project(50,90,3440,1440).radius>FOV::Project(40,90,3440,1440).radius);
        check(FOV::Project(50,70,1920,1080).radius>large.radius);
    }
    {
        namespace R=RadarPolicy;
        auto c=R::Center(3440,1440,120,1,0,0);check(c.x==1720 && c.y==720);
        for(int w:{320,1280,1920,2560,3440,3840}) for(int h:{240,720,1080,1440,2160})
        {
            auto p=R::Center(w,h,120,1,0,0);check(p.x==w*.5 && p.y==h*.5);
            for(int anchor=0;anchor<5;++anchor) for(int offset:{-2000,0,2000})
            {p=R::Center(w,h,120,anchor,offset,offset);check(p.x>=120 && p.x<=w-120 && p.y>=120 && p.y<=h-120);}
        }
        c=R::Center(3440,1440,120,1,40,-20);check(c.x==1760 && c.y==700);
        check(R::Center(0,1440,120,1,0,0).x==0);
        auto p=R::Project(600,0,0,1200,100,false);check(p && std::abs(p->x)<.001 && p->y==-50);
        p=R::Project(0,-600,0,1200,100,false);check(p && p->x==50 && std::abs(p->y)<.001);
        p=R::Project(0,600,90,1200,100,false);check(p && std::abs(p->x)<.001 && std::abs(p->y+50)<.001);
        check(!R::Project(1201,0,0,1200,100,false));
        check(!R::Project(1000,1000,0,1200,100,false));
        check(R::Project(1000,1000,0,1200,100,true).has_value());
        check(!R::Project(0,0,0,0,100,false));check(!R::Project(0,0,0,1200,0,false));
        check(!R::Project(std::numeric_limits<double>::quiet_NaN(),0,0,1200,100,false));
        check(!R::Project(0,0,std::numeric_limits<double>::infinity(),1200,100,false));
        check(R::Height(64,64)==0 && R::Height(-64,64)==0);
        check(R::Height(65,64)==1 && R::Height(-65,64)==-1);
        check(R::Fraction(5,7)>0.71 && R::Fraction(5,7)<0.72);
        check(R::Fraction(24,24)==1 && R::Fraction(99,24)==1 && R::Fraction(-1,24)==0);
        check(R::Fraction(1,0)==0 && R::Fraction(std::numeric_limits<double>::quiet_NaN(),1)==0);
        for(int mode=0;mode<3;++mode) for(bool enabled:{false,true}) for(bool hide:{false,true}) for(bool component:{false,true})
            check(R::Suppress(enabled,mode,hide,component)==(enabled && mode!=0 && hide && component));
        check(!R::Indicators(true,99));
        for(bool square:{false,true}) for(double yaw:{0.,45.,90.,180.})
            for(double dx:{-5000.,-1400.,0.,1400.,5000.}) for(double dy:{-5000.,0.,5000.})
        {
            auto edge=R::Project(dx,dy,yaw,1200,100,square,true);
            check(edge.has_value());
            const double extent=square?std::max(std::abs(edge->x),std::abs(edge->y)):std::hypot(edge->x,edge->y);
            check(extent<=100.001);
            auto normal=R::Project(dx,dy,yaw,1200,100,square);
            if(normal)check(std::hypot(normal->x-edge->x,normal->y-edge->y)<.001);
            else check(std::abs(extent-100)<.001);
        }
        check(!R::Project(5000,0,0,0,100,false,true));
        check(!R::Project(std::numeric_limits<double>::quiet_NaN(),0,0,1200,100,false,true));
        check(R::DetailSize(240,14,8,24)==14);
        check(R::DetailSize(128,14,8,24)==8);
        check(R::DetailSize(600,14,8,24)==24);
        for(double base:{3.,14.,22.})
        {
            int previous=0;
            for(int size=64;size<=600;size+=8)
            {int value=R::DetailSize(size,base,2,55);check(value>=previous && value>=2 && value<=55);previous=value;}
        }
        check(R::DetailSize(std::numeric_limits<double>::quiet_NaN(),14,8,24)==8);
        check(R::DetailSize(240,16,4,64,50)==8);
        check(R::DetailSize(240,16,4,64,200)==32);
        check(R::DetailSize(480,16,4,64,50)==16);
        check(R::DetailSize(240,14,6,48,25)==6);
        check(R::DetailSize(600,14,6,48,200)==48);
        check(R::DetailSize(240,3,1,12,200)==6);
        check(R::DetailSize(240,22,4,110,50)==11);
        check(R::DetailSize(240,4,2,16,200)==8);
        check(R::DetailSize(240,14,6,48,std::numeric_limits<double>::quiet_NaN())==6);
        check(R::ArcSpans(120,5,0).empty());check(R::ArcSpans(0,5,1).empty());
        check(R::ArcSpans(120,0,1).empty());check(R::ArcSpans(2048,5,1).empty());
        check(R::ArcSpans(120,5,std::numeric_limits<double>::quiet_NaN()).empty());
        for(double radius:{48.,136.,145.,325.}) for(double fraction:{.1,.5,1.})
        {
            const auto spans=R::ArcSpans(radius,5,fraction);
            check(!spans.empty());int lastY=-2000;
            for(const auto& span:spans)
            {
                check(span.width>0 && span.y>lastY);lastY=span.y;
                for(int x=span.x;x<span.x+span.width;++x)
                {
                    const double d=std::hypot(double(x),double(span.y));
                    double a=std::atan2(double(span.y),double(x));if(a<0)a+=2*3.14159265358979323846;
                    check(d>=radius-5-.001 && d<=radius+.001);
                    check(a>=120.*3.14159265358979323846/180.-.001 && a<=(120.+120.*fraction)*3.14159265358979323846/180.+.001);
                }
            }
        }
        for(double radius:{64.,160.,325.}) for(double minimumY:{-20.,0.,30.})
        {
            auto p=R::BindPoint(radius,0,14,minimumY);check(p && p->y>=minimumY);
            auto next=R::BindPoint(radius,1,14,minimumY);check(next && std::abs(next->y-p->y-14)<.001);
        }
        check(!R::BindPoint(160,0,14,200));
        check(!R::BindPoint(160,0,14,std::numeric_limits<double>::quiet_NaN()));
        for(bool colors:{false,true}) for(bool follow:{false,true}) for(bool custom:{false,true})
            check(R::InterfaceOutline(colors,follow,custom)==(colors && follow && !custom));
        check(!R::BindPoint(0,0,14));check(!R::BindPoint(-1,0,14));
        check(!R::BindPoint(160,-1,14));check(!R::BindPoint(160,0,0));
        check(!R::BindPoint(160,0,-14));
        check(!R::BindPoint(std::numeric_limits<double>::infinity(),0,14));
        check(!R::BindPoint(160,0,std::numeric_limits<double>::quiet_NaN()));
        for(double radius:{64.,120.,160.,300.})
        {
            auto first=R::BindPoint(radius,0,14);
            check(first && std::abs(first->x+radius*.5)<.001);
            double lastY=first->y;
            int row=0;
            for(;row<100;++row)
            {
                auto point=R::BindPoint(radius,row,14);if(!point) break;
                check(point->x<0 && std::abs(std::hypot(point->x,point->y)-radius)<.001);
                check(std::abs(point->y)<=radius*std::sqrt(3.)*.5+.001);
                if(row) check(std::abs(point->y-lastY-14)<.001);
                lastY=point->y;
            }
            check(row>0 && row<100 && !R::BindPoint(radius,row,14));
        }
    }
    std::cout<<"PASS: "<<checks<<" weapon, prediction, backtrack, trigger, priority, flare-combo, FOV and radar checks\n";
}
