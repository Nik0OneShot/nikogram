#include "../SDK/SDK.h"
#include "../Features/Visuals/AnimInterp/AnimInterp.h"
#include "../Features/SkinChanger/RenderPolicy.h"
#include "../Features/SkinChanger/SkinChanger.h"

MAKE_SIGNATURE(CBaseAnimating_MaintainSequenceTransitions, "client.dll", "4C 89 4C 24 ? 41 56", 0x0);
// Installed x64 MaintainSequenceTransitions: queue at +0x7e0; previous
// sequence parity at +0xae4. Refuse the scoped native override if this verified
// instruction layout changes. CAnimationLayer stride is 0x2c in the same code.
MAKE_SIGNATURE(SkinNative_TransitionLayout, "client.dll", "49 8D B6 E0 07 00 00 48 89 BC 24 B0 00 00 00 40 0F 94 C7 41 3B 8E E4 0A 00 00", 0x0);

namespace
{
    struct NativeQueue
    {
        CAnimationLayer* memory;int allocated,grow,count,padding;CAnimationLayer* elements;
    };
    static_assert(sizeof(NativeQueue)==0x20&&offsetof(NativeQueue,count)==0x10);
    static_assert(sizeof(CAnimationLayer)==0x2c);
    using Identity=std::tuple<uintptr_t,uint32_t,int,int,int,int>;
    struct CosmeticHistory
    {
        Identity identity{};double last=-1;int count=0,parity=0;
        // Native code can append one transition per call. Limit the retained
        // history below half capacity so an external buffer never needs growth
        // or allocation through a different CRT/engine heap.
        std::array<CAnimationLayer,64> layers{};
    };
    thread_local std::map<uint32_t,CosmeticHistory> histories;
}

MAKE_HOOK(CBaseAnimating_MaintainSequenceTransitions, S::CBaseAnimating_MaintainSequenceTransitions(), void,
	void* rcx, void* boneSetup, float flCycle, Vec3 pos[], Vector4D q[])
{
	DEBUG_RETURN(CBaseAnimating_MaintainSequenceTransitions, rcx, boneSetup, flCycle, pos, q);
	if (SkinRender::CosmeticPoseActive(rcx))
	{
		if (!S::SkinNative_TransitionLayout()||!I::GlobalVars) return;
		auto player=reinterpret_cast<CTFPlayer*>(rcx);
		auto profile=SkinChanger::PlayerAnimationsFor(player);if(!profile)return;
		const auto handle=uint32_t(player->GetRefEHandle().ToInt());
		if(!histories.contains(handle)&&histories.size()>=128)histories.clear();
		auto& history=histories[handle];
		const Identity identity{uintptr_t(player->GetModel()),uint32_t(player->m_hActiveWeapon().ToInt()),
			profile->item,profile->reskin,player->m_iClass(),player->m_iTeamNum()};
		const double now=I::GlobalVars->curtime;
		if(!SkinRender::BlendHistoryFresh(now,history.last,history.identity==identity)||history.count<0||history.count>=32)
		{history.count=0;history.parity=player->m_nNewSequenceParity();}
		history.identity=identity;history.last=now;
		auto& queue=*reinterpret_cast<NativeQueue*>(uintptr_t(rcx)+0x7e0);
		auto& parity=*reinterpret_cast<int*>(uintptr_t(rcx)+0xae4);
		SkinRender::DrawStateScope queueScope(queue,NativeQueue{history.layers.data(),64,-1,history.count,0,history.layers.data()});
		SkinRender::DrawStateScope parityScope(parity,history.parity);
		// Native fade durations, cycle advancement and quaternion pose blending,
		// using ONLY this cosmetic's history. The stock queue/parity are restored
		// byte-for-byte before simulation, aimbot or another normal draw can run.
		CALL_ORIGINAL(rcx,boneSetup,flCycle,pos,q);
		history.count=queue.count;history.parity=parity;
		SkinChanger::ObserveCosmeticEffect("third_person_blending","native_cosmetic_history_gameplay_history_restored");
		return;
	}
	if (F::AnimInterp.ShouldTransition(reinterpret_cast<CBaseEntity*>(rcx)))
		CALL_ORIGINAL(rcx, boneSetup, flCycle, pos, q);
}
