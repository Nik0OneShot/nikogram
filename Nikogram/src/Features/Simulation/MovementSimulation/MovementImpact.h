#pragma once
struct MoveStorage;
namespace MovementImpact {
using Callback=void(*)(void*,MoveStorage&);
inline thread_local Callback callback=nullptr,beforeMovement=nullptr;
inline thread_local void* context=nullptr;
inline thread_local bool hypothetical=false;
struct Scope {
    Callback previous=callback,oldBefore=beforeMovement;void* old=context;bool oldHypothetical=hypothetical;
    Scope(Callback fn,void* data,bool hit=false,Callback before=nullptr){callback=fn;context=data;hypothetical=hit;beforeMovement=before;}
    ~Scope(){beforeMovement=oldBefore;callback=previous;context=old;hypothetical=oldHypothetical;}
    Scope(const Scope&)=delete;
};
inline bool Active(){return callback!=nullptr;}
inline void BeforeMovement(MoveStorage& storage){if(beforeMovement)beforeMovement(context,storage);}
inline void AtBoundary(MoveStorage& storage){if(callback)callback(context,storage);}
}
