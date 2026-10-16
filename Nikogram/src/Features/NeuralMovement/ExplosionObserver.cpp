#include "ExplosionObserver.h"
#include "../../SDK/SDK.h"
#include "../../Utils/Hooks/Lifetime.h"
#include "../../Utils/Hooks/StartupPolicy.h"
#include <MinHook/MinHook.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>

namespace NeuralMovement::ExplosionObserver
{
namespace
{
    struct Module { uintptr_t begin = 0, end = 0; };
    struct Layout { std::array<int, 3> origin{}; int normal = 0, weapon = 0, definition = 0; size_t extent = 0; };
    using PostUpdate = void (__fastcall*)(void*, DataUpdateType_t);
    using GetBase = void* (__fastcall*)(void*);
    std::atomic<bool> attempted = false, available = false, stopped = false;
    std::atomic<Callback> listener = nullptr;
    std::atomic<const char*> status = "Explosion observer not initialized";
    PostUpdate original = nullptr;
    GetBase getBase = nullptr;
    void* instance = nullptr;
    Layout layout{};
    Module client{};
    std::mutex lifecycle;

    bool Span(const void* pointer, size_t length, bool executable = false) noexcept
    {
        const auto start = reinterpret_cast<uintptr_t>(pointer);
        if (!start || !length || length > 65536 || start > std::numeric_limits<uintptr_t>::max() - length) return false;
        const auto end = start + length;
        auto cursor = start;
        for (unsigned regions = 0; cursor < end && regions < 32; ++regions)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info)) || info.State != MEM_COMMIT
                || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
            const DWORD protection = info.Protect & 0xff;
            const bool read = protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY
                || protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
            const bool execute = protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
            if (!read || (executable && !execute)) return false;
            const auto region = reinterpret_cast<uintptr_t>(info.BaseAddress);
            if (info.RegionSize > std::numeric_limits<uintptr_t>::max() - region || region + info.RegionSize <= cursor) return false;
            cursor = (std::min)(end, region + info.RegionSize);
        }
        return cursor == end;
    }

    // Only our metadata reads are guarded. Never suppress faults/exceptions from
    // the game's original PostDataUpdate or invoke it a second time.
    bool Copy(void* destination, const void* source, size_t bytes) noexcept
    {
        if (!Span(source, bytes)) return false;
        __try { std::memcpy(destination, source, bytes); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool Name(const char* pointer, const char* expected) noexcept
    {
        const size_t length = std::strlen(expected) + 1;
        char buffer[64]{};
        return length <= sizeof(buffer) && Copy(buffer, pointer, length) && std::memcmp(buffer, expected, length) == 0;
    }
    bool InClient(const void* pointer, size_t bytes, bool executable = false) noexcept
    {
        const auto address = reinterpret_cast<uintptr_t>(pointer);
        return address >= client.begin && address < client.end && bytes <= client.end - address && Span(pointer, bytes, executable);
    }
    bool ClientImage() noexcept
    {
        const auto handle = GetModuleHandleW(L"client.dll");
        IMAGE_DOS_HEADER dos{};
        if (!handle || !Copy(&dos, handle, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE
            || dos.e_lfanew < 0 || dos.e_lfanew > 1024 * 1024) return false;
        IMAGE_NT_HEADERS64 nt{};
        if (!Copy(&nt, reinterpret_cast<const char*>(handle) + dos.e_lfanew, sizeof(nt))
            || nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
            || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.OptionalHeader.SizeOfImage < 4096) return false;
        const auto base = reinterpret_cast<uintptr_t>(handle);
        if (base > std::numeric_limits<uintptr_t>::max() - nt.OptionalHeader.SizeOfImage) return false;
        client = {base, base + nt.OptionalHeader.SizeOfImage};
        return true;
    }
    bool Method(void* object, unsigned index, void*& result) noexcept
    {
        void** table = nullptr;
        return Copy(&table, object, sizeof(table)) && index <= 16
            && InClient(table, (index + 1) * sizeof(void*))
            && Copy(&result, table + index, sizeof(result)) && InClient(result, 1, true);
    }
    bool Metadata(RecvTable* table, Layout& result) noexcept
    {
        if (!InClient(table, sizeof(RecvTable))) return false;
        const auto* tableName = table->m_pNetTableName;
        const int count = table->m_nProps;
        auto* props = table->m_pProps;
        if (!Name(tableName, "DT_TETFExplosion") || count < 6 || count > 64
            || !InClient(props, size_t(count) * sizeof(RecvProp))) return false;
        const char* names[] = {"m_vecOrigin[0]", "m_vecOrigin[1]", "m_vecOrigin[2]", "m_vecNormal", "m_iWeaponID", "m_nDefID"};
        const SendPropType types[] = {DPT_Float, DPT_Float, DPT_Float, DPT_Vector, DPT_Int, DPT_Int};
        int offsets[6]{};
        unsigned found = 0;
        for (int i = 0; i < count; ++i)
        {
            // GetOffset is an inline field accessor on the already-readable
            // native metadata. No network proxy/getter is executed here.
            auto* prop = &props[i];
            for (unsigned k = 0; k < 6; ++k)
            {
                if (!Name(prop->m_pVarName, names[k])) continue;
                const int offset = prop->GetOffset();
                if ((found & (1u << k)) || prop->m_RecvType != types[k] || offset < 16 || offset > 1008 || offset % 4) return false;
                found |= 1u << k;
                offsets[k] = offset;
            }
        }
        if (found != 63 || offsets[1] != offsets[0] + 4 || offsets[2] != offsets[0] + 8) return false;
        size_t extent = 0;
        for (unsigned i = 0; i < 6; ++i)
        {
            const int width = i == 3 ? 12 : 4;
            extent = (std::max)(extent, size_t(offsets[i] + width));
            for (unsigned j = 0; j < i; ++j)
            {
                const int otherWidth = j == 3 ? 12 : 4;
                if (offsets[i] < offsets[j] + otherWidth && offsets[j] < offsets[i] + width) return false;
            }
        }
        result = {{offsets[0], offsets[1], offsets[2]}, offsets[3], offsets[4], offsets[5], extent};
        return true;
    }
    ClientClass* Classes(void* object, void* method) noexcept
    {
        __try { return reinterpret_cast<ClientClass* (__fastcall*)(void*)>(method)(object); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    void* Factory(CreateEventFn factory) noexcept
    {
        __try { return factory(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    void* Base(void* object, GetBase method) noexcept
    {
        __try { return method(object); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    bool Payload(void* base, Event& event) noexcept
    {
        if (!Span(base, layout.extent)) return false;
        const auto* bytes = static_cast<const char*>(base);
        std::array<float, 3> normal{};
        for (unsigned i = 0; i < 3; ++i)
            if (!Copy(&event.origin[i], bytes + layout.origin[i], sizeof(float))) return false;
        if (!Copy(normal.data(), bytes + layout.normal, sizeof(normal))
            || !Copy(&event.weaponID, bytes + layout.weapon, sizeof(int))
            || !Copy(&event.itemDefinition, bytes + layout.definition, sizeof(int))) return false;
        for (unsigned i = 0; i < 3; ++i)
            if (!std::isfinite(event.origin[i]) || std::abs(event.origin[i]) > 32768.f
                || !std::isfinite(normal[i]) || std::abs(normal[i]) > 1.01f) return false;
        return event.weaponID > 0 && event.weaponID <= 1023 && event.itemDefinition >= -1 && event.itemDefinition <= 65535;
    }
    void __fastcall Observe(void* self, DataUpdateType_t update)
    {
        HookLifetime::Scope lifetime;
        original(self, update);
        const auto callback = listener.load(std::memory_order_acquire);
        if (!callback || !available.load(std::memory_order_acquire) || stopped.load(std::memory_order_acquire)
            || !StartupPolicy::gate.Ready() || self != instance) return;
        Event event{};
        if (Payload(Base(self, getBase), event)) callback(event);
    }
}

bool Initialize(Callback callback) noexcept
{
    std::lock_guard lock(lifecycle);
    if (!callback || stopped.load(std::memory_order_acquire)) return false;
    if (attempted.exchange(true, std::memory_order_acq_rel)) return Available();
    status = "Explosion observer unavailable: client ABI not verified";
#if defined(_WIN64)
    if (!ClientImage()) return false;
    void* classesMethod = nullptr;
    if (!I::Client || !Method(I::Client, 8, classesMethod)) return false;
    ClientClass* found = nullptr;
    unsigned visited = 0;
    for (auto* entry = Classes(I::Client, classesMethod); entry && visited < 1024; ++visited)
    {
        ClientClass node{};
        if (!InClient(entry, sizeof(node)) || !Copy(&node, entry, sizeof(node))) return false;
        if (Name(node.m_pNetworkName, "CTETFExplosion"))
        {
            if (found) return false;
            found = entry;
        }
        entry = node.m_pNext;
        if (entry && visited == 1023) return false;
    }
    ClientClass node{};
    if (!found || !Copy(&node, found, sizeof(node)) || node.m_pCreateFn || !node.m_pCreateEventFn
        || !InClient(reinterpret_cast<void*>(node.m_pCreateEventFn), 1, true) || !Metadata(node.m_pRecvTable, layout)) return false;
    instance = Factory(node.m_pCreateEventFn);
    void *post = nullptr, *baseMethod = nullptr, *classMethod = nullptr;
    if (!instance || !Method(instance, 2, classMethod) || Classes(instance, classMethod) != found
        || !Method(instance, 7, post) || !Method(instance, 11, baseMethod)) return false;
    getBase = reinterpret_cast<GetBase>(baseMethod);
    if (!Span(Base(instance, getBase), layout.extent)) return false;
    status = "Explosion observer unavailable: optional hook not installed";
    if (MH_CreateHook(post, reinterpret_cast<void*>(Observe), reinterpret_cast<void**>(&original)) != MH_OK || !original) return false;
    // Fully publish immutable native state before MinHook can dispatch to us.
    listener.store(callback, std::memory_order_release);
    available.store(true, std::memory_order_release);
    if (MH_EnableHook(post) != MH_OK)
    {
        available.store(false, std::memory_order_release);
        listener.store(nullptr, std::memory_order_release);
        // Keep the disabled hook registered for global MH_Uninitialize cleanup.
        return false;
    }
    status = "Explosion observer available (client event only)";
    return true;
#else
    return false;
#endif
}
bool Available() noexcept { return available.load(std::memory_order_acquire) && !stopped.load(std::memory_order_acquire); }
const char* Status() noexcept { return status.load(std::memory_order_acquire); }
void Shutdown() noexcept
{
    std::lock_guard lock(lifecycle);
    stopped.store(true, std::memory_order_release);
    listener.store(nullptr, std::memory_order_release);
    available.store(false, std::memory_order_release);
    status = "Explosion observer stopped";
}
}
