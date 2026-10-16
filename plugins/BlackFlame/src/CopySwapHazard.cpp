// =============================================================================
// COPY SWAP - hazard creation (CopySwap.h, device 6: swap point 4 of the expansion plan)
// =============================================================================
// Hazard creation (SE 42786 / AE 43954) takes a create data block: +0x00 BGSHazard*, +0x08 TESObjectCELL*, +0x38 the
// cause actor (1.7.104, research-swap notes; checked per pointer at runtime on every build). Its prologue is detoured on
// every runtime the replay decoder takes (1.7.104: 48 8B C4 + pushes); the thunk forwards all four argument registers and
// returns rax. The base becomes its copy, for the call only, when:
//   (a) a copy window is open (the impacts of a copy projectile or explosion: SpawnCollisionEffects / Explosion::Update,
//       CopySwap.h) - an impact copy whose hazard link is already the copy needs nothing;
//   (b) the call comes from the spawn-hazard effect (SE 33580 / AE 34360, research R-H on 1.7.104: it builds the create
//       data on its stack and calls 43954 with the effect still in r13) while that effect is a fire effect the player cast
//       and the player is stoked. 34360's prologue is detoured as well (mov rax, rsp + pushes): its thunk opens a
//       thread-local window naming the effect, when the effect carries the SpawnHazardEffect vtable.
// A pointer that fails its check (form type, vtable) swaps nothing and is counted; a fault trips the SEH guard once and
// turns the device off for the session.

#include "CopySwap.h"

#include "BuffState.h"
#include "Config.h"
#include "FormCopies.h"
#include "HitEffectHook.h"
#include "MainThreadQueue.h"
#include "SehGuard.h"
#include "ThreadTrace.h"

#include <atomic>
#include <cstring>

namespace BlackFlame
{
    namespace
    {
        constexpr std::uint64_t kCreateSe = 42786, kCreateAe = 43954;
        constexpr std::uint64_t kSpawnSe = 33580, kSpawnAe = 34360;
        constexpr std::size_t kCreateBase = 0x00;  // BGSHazard*
        constexpr std::size_t kCreateCell = 0x08;  // TESObjectCELL*

        using Fn4 = std::uintptr_t(void*, void*, void*, void*);

        // Vtable ids (SE, AE; CommonLib Offsets_VTABLE.h), checked and resolved at install
        constexpr std::uint64_t kHazardVtableSe = 233487, kHazardVtableAe = 189079;  // VTABLE_BGSHazard[0]
        constexpr std::uint64_t kCellVtableSe = 235197, kCellVtableAe = 190143;      // VTABLE_TESObjectCELL[0]
        constexpr std::uint64_t kSpawnVtableSe = 259802, kSpawnVtableAe = 207209;    // VTABLE_SpawnHazardEffect[0]
        std::uintptr_t g_hazardVtable = 0;
        std::uintptr_t g_cellVtable = 0;
        std::uintptr_t g_spawnVtable = 0;

        std::uintptr_t g_createOriginal = 0;
        std::uintptr_t g_spawnOriginal = 0;
        thread_local RE::ActiveEffect* t_spawnEffect = nullptr;  // the spawn-hazard effect on this thread's stack, or null

        std::atomic<std::uint64_t> g_calls{ 0 }, g_swapsWindow{ 0 }, g_swapsSpawn{ 0 }, g_rejected{ 0 }, g_spawnCalls{ 0 },
            g_spawnOther{ 0 };
        std::atomic<bool> g_failure{ false };

        template <class T>
        T ReadAt(const void* a_base, std::size_t a_offset)
        {
            T value{};
            std::memcpy(&value, static_cast<const std::uint8_t*>(a_base) + a_offset, sizeof(T));
            return value;
        }

        // What the creation should get instead of its base, or null
        RE::BGSHazard* Decide(void* a_data)
        {
            auto* base = ReadAt<RE::TESForm*>(a_data, kCreateBase);
            auto* cell = ReadAt<void*>(a_data, kCreateCell);
            if (!base || ReadAt<std::uintptr_t>(base, 0) != g_hazardVtable || base->GetFormType() != RE::FormType::Hazard ||
                (cell && ReadAt<std::uintptr_t>(cell, 0) != g_cellVtable)) {
                g_rejected.fetch_add(1);
                return nullptr;
            }
            if (IsCopy(base)) {
                return nullptr;  // already the copy (an impact copy's hazard link)
            }
            auto* copy = CopyOfAs<RE::BGSHazard>(static_cast<RE::BGSHazard*>(base));
            if (!copy) {
                return nullptr;
            }
            if (CopyWindowOpen()) {
                g_swapsWindow.fetch_add(1);
                return copy;
            }
            if (t_spawnEffect && IsStoked() && IsPlayerFireEffect(t_spawnEffect)) {
                g_swapsSpawn.fetch_add(1);
                return copy;
            }
            return nullptr;
        }

        std::uintptr_t CreateThunk(void* a_data, void* a_rdx, void* a_r8, void* a_r9)
        {
            const ThreadTrace::Scope trace("hazard create (v2 detour)");
            MainQueue::NoteJobSideHook();
            g_calls.fetch_add(1);
            RE::BGSHazard* swapTo = nullptr;
            RE::TESForm* before = nullptr;
            try {
                if (a_data && GetConfig().copyHazardCreate) {
                    (void)Guarded("copy hazard create", [&]() {
                        swapTo = Decide(a_data);
                        before = swapTo ? ReadAt<RE::TESForm*>(a_data, kCreateBase) : nullptr;
                    });
                }
            } catch (...) {
                swapTo = nullptr;
                if (!g_failure.exchange(true)) {
                    logger::error("BlackFlame: the hazard creation detour threw; the original went on; not logged again");
                }
            }
            auto** slot = static_cast<RE::TESForm**>(a_data);
            if (swapTo) {
                *slot = swapTo;
            }
            const auto result = reinterpret_cast<Fn4*>(g_createOriginal)(a_data, a_rdx, a_r8, a_r9);
            if (swapTo) {
                *slot = before;  // the caller's create data as it was
            }
            return result;
        }

        std::uintptr_t SpawnThunk(void* a_effect, void* a_rdx, void* a_r8, void* a_r9)
        {
            g_spawnCalls.fetch_add(1);
            auto* previous = t_spawnEffect;
            RE::ActiveEffect* effect = nullptr;
            try {
                (void)Guarded("spawn hazard window", [&]() {
                    if (a_effect && ReadAt<std::uintptr_t>(a_effect, 0) == g_spawnVtable) {
                        effect = static_cast<RE::ActiveEffect*>(a_effect);
                    } else {
                        g_spawnOther.fetch_add(1);
                    }
                });
            } catch (...) {
                effect = nullptr;
            }
            t_spawnEffect = effect;
            const auto result = reinterpret_cast<Fn4*>(g_spawnOriginal)(a_effect, a_rdx, a_r8, a_r9);
            t_spawnEffect = previous;
            return result;
        }
    }

    namespace CopySwapDetail
    {
        void InstallHazardDetours(CopySwapStatus& a_status)
        {
            if (!GetConfig().copyHazardCreate) {
                a_status.hazardCreate.note = "copyHazardCreate false";
                return;
            }
            if (GetCopyCounts().hazards == 0) {
                a_status.hazardCreate.note = "no hazard copies";
                return;
            }
            const auto createId = Patch::IdFor(kCreateSe, kCreateAe);
            const auto hazardVtableId = Patch::IdFor(kHazardVtableSe, kHazardVtableAe);
            const auto cellVtableId = Patch::IdFor(kCellVtableSe, kCellVtableAe);
            const auto spawnVtableId = Patch::IdFor(kSpawnVtableSe, kSpawnVtableAe);
            if (!Patch::HasIds({ hazardVtableId, cellVtableId, spawnVtableId }, "copy hazard create (vtables)")) {
                a_status.hazardCreate.note = "a vtable id is missing";
                Patch::RecordHook("hazard creation (v2 copy swap)", false, 0, {}, a_status.hazardCreate.note);
                return;
            }
            g_hazardVtable = Patch::AddressOf(hazardVtableId);
            g_cellVtable = Patch::AddressOf(cellVtableId);
            g_spawnVtable = Patch::AddressOf(spawnVtableId);
            if (Patch::HasIds({ createId }, "copy hazard create")) {
                a_status.hazardCreate = Patch::PrologueDetour("hazard creation (v2 copy swap)", Patch::AddressOf(createId),
                    reinterpret_cast<std::uintptr_t>(&CreateThunk), g_createOriginal);
            }
            if (!a_status.hazardCreate.hooked) {
                logger::warn("BlackFlame: copy swap: hazard creation not detoured ({}); bytes {}", a_status.hazardCreate.note,
                    a_status.hazardCreate.bytes);
                return;
            }
            const auto spawnId = Patch::IdFor(kSpawnSe, kSpawnAe);
            if (Patch::HasIds({ spawnId }, "copy hazard spawn window")) {
                a_status.spawnHazard = Patch::PrologueDetour("spawn-hazard effect (v2 hazard window)", Patch::AddressOf(spawnId),
                    reinterpret_cast<std::uintptr_t>(&SpawnThunk), g_spawnOriginal);
            }
            if (!a_status.spawnHazard.hooked) {
                logger::warn("BlackFlame: copy swap: the spawn-hazard window is not detoured ({}): spawn-hazard archetype hazards "
                             "stay orange; bytes {}", a_status.spawnHazard.note, a_status.spawnHazard.bytes);
            }
        }

        void AddHazardCounters(CopySwapCounters& a_out) noexcept
        {
            a_out.hazardCreateCalls = g_calls.load();
            a_out.hazardSwapsWindow = g_swapsWindow.load();
            a_out.hazardSwapsSpawn = g_swapsSpawn.load();
            a_out.hazardCreateRejected = g_rejected.load();
            a_out.spawnWindowCalls = g_spawnCalls.load();
            a_out.spawnWindowOther = g_spawnOther.load();
        }
    }
}
