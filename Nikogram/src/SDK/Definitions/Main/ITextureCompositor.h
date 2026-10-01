#pragma once
// Valve Source SDK 2013, public/materialsystem/itexturecompositor.h.
// Keep this public interface in its authored virtual order; no engine layout casts.
class ITexture;
enum ECompositeResolveStatus
{
    ECRS_Idle=0,ECRS_Scheduled,ECRS_PendingTextureLoads,ECRS_PendingComposites,ECRS_Error,ECRS_Complete
};
class ITextureCompositor
{
public:
    virtual int AddRef()=0;
    virtual int Release()=0;
    virtual int GetRefCount()const=0;
    virtual void Update()=0;
    virtual ITexture* GetResultTexture()const=0;
    virtual ECompositeResolveStatus GetResolveStatus()const=0;
    virtual void ScheduleResolve()=0;
protected:
    virtual ~ITextureCompositor(){}
};
