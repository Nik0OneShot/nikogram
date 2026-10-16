#pragma once
#include <cmath>
#include <cstdint>

namespace BowReleasePolicy
{
    struct Identity
    {
        uint32_t owner=0, weapon=0;
        int spawn=0, server=0;
        bool operator==(const Identity&) const = default;
    };
    enum class Action { Unchanged, Hold, Cancel };
    struct Ownership
    {
        Identity identity{};
        int command=0;
        bool initialized=false, owned=false, manual=false, cancelPending=false;
        float observedBegin=0;
        void Observe(Identity id,int cmd,bool alive,float begin,bool rawAttack,bool rawCancel)
        {
            const bool contiguous=initialized && (cmd==command || (int64_t(cmd)==int64_t(command)+1));
            if (!alive || cmd<=0 || !id.owner || !id.weapon || !std::isfinite(begin) || begin<0)
            { *this={}; return; }
            const bool gapCancel=initialized && owned && id==identity && cmd>command && !contiguous && begin>0;
            if (!contiguous || !(id==identity)) {*this={};owned=cancelPending=gapCancel;}
            if (begin==0 && (observedBegin>0 || cancelPending)) owned=cancelPending=false;
            identity=id;command=cmd;initialized=true;
            observedBegin=begin;
            if (begin==0 && !rawAttack) manual=false;
            if (rawAttack || rawCancel) {owned=cancelPending=false;manual=true;}
        }
        Action Finish(float begin,float now,float overhold,float tick,bool active,
            bool rawAttack,bool rawCancel,bool outgoingAttack,bool outgoingCancel,
            bool validated,bool cancelOnFailure)
        {
            if (!initialized || manual || rawAttack || rawCancel) {owned=cancelPending=false;return Action::Unchanged;}
            // A requested secondary attack can be deferred by native cooldown/state.
            // Keep cancelling our charge until Observe confirms it actually ended.
            if (cancelPending) return Action::Cancel;
            if (outgoingCancel) {cancelPending=owned && begin>0;return Action::Unchanged;}
            // Never adopt a charge which was already present when first observed.
            const bool wasOwned=owned;
            if (!owned && active && begin==0 && outgoingAttack) owned=true;
            if (!owned) return Action::Unchanged;
            if (validated && !outgoingAttack) {owned=false;return Action::Unchanged;}
            const bool expiring=begin>0 && (!std::isfinite(now) || !std::isfinite(overhold)
                || !std::isfinite(tick) || tick<=0 || now<begin || now-begin+tick>=overhold);
            if (!active || expiring || (!validated && cancelOnFailure && (wasOwned || begin>0)))
            {owned=cancelPending=begin>0;return Action::Cancel;}
            // A failed/interrupted search is not permission to release an auto charge.
            return Action::Hold;
        }
    };
    // Release ownership is committed only against the final outgoing buttons.
    // A later writer may retain charge; it must not turn our charge into an
    // unowned pre-existing charge on the next command. Never alter that writer.
    struct ReleaseCommit {
        Ownership prior{};int originalCommand=0;bool pending=false;
        void Stage(const Ownership&before,const Ownership&after,int cmd,bool validated,bool attack,bool cancel){
            pending=before.owned&&!after.owned&&validated&&!attack&&!cancel;
            if(pending){prior=before;originalCommand=cmd;}
        }
        void Seal(Ownership&ownership,Identity id,int cmd,bool alive,float begin,bool rawAttack,bool rawCancel,bool attack,bool cancel,bool switching){
            const bool same=pending&&cmd==originalCommand&&id==prior.identity&&alive&&std::isfinite(begin)&&begin>0&&begin==prior.observedBegin;
            pending=false;
            if(!same||rawAttack||rawCancel||switching)return;
            if(attack||cancel){ownership=prior;ownership.owned=true;ownership.cancelPending=cancel;}
        }
    };
}
