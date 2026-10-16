#pragma once
// Include after existing SDK VPhysics.h (objectparams_t,
// Vec3 and forward constraint/vehicle types). Native declarations transcribed
// from public/vcollide_parse.h and public/vphysics_interface.h. No packing change.
// Do not add virtual destructors to IVPhysicsKeyHandler or collision recorder.
#include <cstddef>
#include <cstdlib>
#include <cstring>

struct solid_t {
    int index;
    char name[512];
    char parent[512];
    char surfaceprop[512];
    Vec3 massCenterOverride;
    objectparams_t params;
};
struct fluid_t;
class IVPhysicsKeyHandler {
public:
    virtual void ParseKeyValue(void* pData,const char* pKey,const char* pValue)=0;
    virtual void SetDefaults(void* pData)=0;
};
class IVPhysicsKeyParser {
public:
    virtual ~IVPhysicsKeyParser() {}
    virtual const char* GetCurrentBlockName(void)=0;
    virtual bool Finished(void)=0;
    virtual void ParseSolid(solid_t* pSolid,IVPhysicsKeyHandler* unknownKeyHandler)=0;
    virtual void ParseFluid(fluid_t* pFluid,IVPhysicsKeyHandler* unknownKeyHandler)=0;
    virtual void ParseRagdollConstraint(constraint_ragdollparams_t* pConstraint,IVPhysicsKeyHandler* unknownKeyHandler)=0;
    virtual void ParseSurfaceTable(int* table,IVPhysicsKeyHandler* unknownKeyHandler)=0;
    virtual void ParseCustom(void* pCustom,IVPhysicsKeyHandler* unknownKeyHandler)=0;
    virtual void ParseVehicle(vehicleparams_t* pVehicle,IVPhysicsKeyHandler* unknownKeyHandler)=0;
    virtual void SkipBlock(void)=0;
};
struct surfacedata_t;
struct surfacephysicsparams_t {
    float friction,elasticity,density,thickness,dampening;
};
class IPhysicsSurfaceProps {
public:
    virtual ~IPhysicsSurfaceProps(void) {}
    virtual int ParseSurfaceData(const char* pFilename,const char* pTextfile)=0;
    virtual int SurfacePropCount(void)const=0;
    virtual int GetSurfaceIndex(const char* pSurfacePropName)const=0;
    virtual void GetPhysicsProperties(int surfaceDataIndex,float* density,float* thickness,float* friction,float* elasticity)const=0;
    virtual surfacedata_t* GetSurfaceData(int surfaceDataIndex)=0;
    virtual const char* GetString(unsigned short stringTableIndex)const=0;
    virtual const char* GetPropName(int surfaceDataIndex)const=0;
    virtual void SetWorldMaterialIndexTable(int* pMapArray,int mapSize)=0;
    virtual void GetPhysicsParameters(int surfaceDataIndex,surfacephysicsparams_t* pParamsOut)const=0;
};

namespace NativeParseReview {
// shared/physics_shared.cpp42-56, exact default values and aggregate order.
inline const objectparams_t Defaults = {
    nullptr,1.f,1.f,.1f,.1f,.05f,"DEFAULT",nullptr,0.f,1.f,true
};
// Own callback instance. Equivalent native g_SolidSetup behavior for the two
// methods; no use of mutable native global handler or live entity pointer.
class SolidDefaults final:public IVPhysicsKeyHandler {
public:
    unsigned contents=1; // CONTENTS_SOLID
    void ParseKeyValue(void*,const char* key,const char* value)override {
        if(_stricmp(key,"contents")==0)contents=static_cast<unsigned>(std::atoi(value));
    }
    void SetDefaults(void* data)override {
        static_cast<solid_t*>(data)->params=Defaults;
        contents=1;
    }
};
// If storing/copying a parsed solid, rebase any local mass-center pointer into
// the owned copy. Reject unexpected pointers rather than retain parser storage.
inline bool CopySolid(const solid_t& source,solid_t& owned) {
    const auto* center=source.params.massCenterOverride;
    if(center&&center!=&source.massCenterOverride)return false;
    owned=source;
    if(center)owned.params.massCenterOverride=&owned.massCenterOverride;
    return true;
}
// Expected normal MSVC x64 layouts. Compile checks do not verify a running DLL.
static_assert(sizeof(void*)==8);
static_assert(sizeof(Vec3)==12 && alignof(Vec3)==4);
static_assert(sizeof(objectparams_t)==64);
static_assert(offsetof(objectparams_t,pName)==32);
static_assert(offsetof(objectparams_t,pGameData)==40);
static_assert(offsetof(objectparams_t,enableCollisions)==56);
static_assert(offsetof(solid_t,massCenterOverride)==1540);
static_assert(offsetof(solid_t,params)==1552);
static_assert(sizeof(solid_t)==1616);
static_assert(sizeof(surfacephysicsparams_t)==20);
}
