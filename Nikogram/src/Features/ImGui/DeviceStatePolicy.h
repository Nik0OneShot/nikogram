#pragma once
namespace DeviceStatePolicy
{
    // Native/overlay Present must not leave Source's cached gamma-write state
    // out of sync with the device. Preserve its exact value, not forced TRUE.
    // Only repair a changed state after successful presentation/readback.
    template<class Device,class State,class Value> class PresentState
    {
        Device* device;
        State state;
        Value saved{};
        bool valid=false;
    public:
        PresentState(Device* d,State s):device(d),state(s)
        {valid=device&&device->GetRenderState(state,&saved)>=0;}
        template<class Result> bool Restore(Result result)
        {
            if(!valid||result<0)return false;
            Value current{};
            if(device->GetRenderState(state,&current)<0||current==saved)return false;
            return device->SetRenderState(state,saved)>=0;
        }
        PresentState(const PresentState&)=delete;
        PresentState& operator=(const PresentState&)=delete;
    };
    template<class Device, class Block, class BlockType> class Scope
    {
        Block* block = nullptr;
    public:
        Scope(Device* device, BlockType type)
        {
            if (device->CreateStateBlock(type, &block) < 0 || !block) { block = nullptr; return; }
            if (block->Capture() < 0) { block->Release(); block = nullptr; }
        }
        ~Scope() { if (block) { block->Apply(); block->Release(); } }
        bool Ready() const { return block != nullptr; }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };
}
