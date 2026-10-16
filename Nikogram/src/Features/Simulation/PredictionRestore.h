#pragma once
#include "../../SDK/SDK.h"

// Movement-only packed snapshots. This is not a replacement for the engine's
// general RestoreData: model and hierarchy changes invalidate these snapshots.
namespace PredictionRestore
{
    inline bool PackedSize(const datamap_t* map)
    { return map && map->packed_offsets_computed && map->packed_size >= 4 && map->packed_size <= 65536; }

    inline int FlagsOffset(datamap_t* map)
    {
        for (int depth=0; map && depth<32; ++depth,map=map->baseMap)
        {
            if (map->dataNumFields<0 || map->dataNumFields>2048 || (map->dataNumFields && !map->dataDesc)) return -1;
            for (int i=0;i<map->dataNumFields;++i)
            {
                const auto& field=map->dataDesc[i];
                if (field.fieldName && !strcmp(field.fieldName,"m_iEFlags") && field.fieldType==FIELD_INTEGER && field.fieldSize==1)
                {
                    const int offset=field.fieldOffset[TD_OFFSET_NORMAL];
                    return offset>0 && offset<=65532 ? offset : -1;
                }
            }
        }
        return -1;
    }
    struct Snapshot
    {
        datamap_t* map=nullptr;
        CBaseHandle handle{},parent{};
        const model_t* model=nullptr;
        int size=0,flagsOffset=-1,modelIndex=0,spawn=0,server=0;
        bool Capture(CTFPlayer* player)
        {
            *this={};
            if (!player || !S::CBaseEntity_InvalidatePhysicsRecursive()) return false;
            map=player->GetPredDescMap();
            if (!PackedSize(map) || (flagsOffset=FlagsOffset(map))<0) return false;
            handle=player->GetRefEHandle();
            if (I::ClientEntityList->GetClientEntityFromHandle(handle)!=player) return false;
            if (auto p=player->GetMoveParent()) parent=p->GetRefEHandle();
            model=player->GetModel();modelIndex=player->m_nModelIndex();
            size=map->packed_size;spawn=player->m_iSpawnCounter();server=I::ClientState->m_nServerCount;
            return model!=nullptr;
        }
        bool Matches(CTFPlayer* player) const
        {
            // Resolve the full serial handle before dereferencing the stored pointer.
            if (!player || !map || I::ClientState->m_nServerCount!=server
                || I::ClientEntityList->GetClientEntityFromHandle(handle)!=player) return false;
            CBaseHandle currentParent;
            if (auto p=player->GetMoveParent()) currentParent=p->GetRefEHandle();
            return player->GetPredDescMap()==map && PackedSize(map) && map->packed_size==size
                && player->GetModel()==model && player->m_nModelIndex()==modelIndex
                && player->m_iSpawnCounter()==spawn && currentParent==parent;
        }
    };
    inline bool Restore(CTFPlayer* player,const Snapshot& snapshot,const void* data,const char* operation)
    {
        if (!data || !snapshot.Matches(player) || !S::CBaseEntity_InvalidatePhysicsRecursive()) return false;
        auto& flags=*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(player)+snapshot.flagsOffset);
        // Preserve CURRENT queue-membership flags: the external shadow and
        // spatial-partition dirty lists are not part of the packed snapshot.
        constexpr int queueFlags=EFL_DIRTY_SHADOWUPDATE|EFL_DIRTY_SPATIAL_PARTITION;
        const int currentQueueFlags=flags&queueFlags;
        CPredictionCopy copy{PC_EVERYTHING,player,PC_DATA_NORMAL,data,PC_DATA_PACKED};
        copy.TransferData(operation,player->entindex(),snapshot.map);
        flags=(flags&~queueFlags)|currentQueueFlags;
        // Valve OnPostRestoreData invalidates these derived transforms recursively.
        player->InvalidatePhysicsRecursive(7); // POSITION_CHANGED | ANGLES_CHANGED | VELOCITY_CHANGED
        return true;
    }
}
