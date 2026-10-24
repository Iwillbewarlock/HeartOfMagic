// =============================================================================
// COPY SWAP - hazard creation (CopySwap.h, device 6: swap point 4 of the expansion plan)
// =============================================================================
// Hazard creation (AE 43954) takes one argument, a create data block: +0x00 BGSHazard*, +0x08 TESObjectCELL*, +0x38 the
// cause actor (1.7.104 code; 1.6.1170 pdata/unwind equal; checked per pointer at runtime). Its prologue is detoured on
// every AE runtime the replay decoder takes (1.7.104: 48 8B C4 + pushes); the thunk forwards all four argument registers
// and returns rax (1.7.104: one argument, no float or stack argument - verify-signatures.md). The base becomes its copy,
// for the call only, when:
//   (a) a copy window is open (the impacts of a copy projectile or explosion: SpawnCollisionEffects / Explosion::Update,
//       CopySwap.h) - an impact copy whose hazard link is already the copy needs nothing;
//   (b) the call comes from SpawnHazardEffect's own start (AE 36885, fix F2: 1.7.104 0x666ed0, reached from
//       SpawnHazardEffect::Start 36879 directly and from slot 0xF HandleQueuedStart through a je, both with rcx = the
//       effect; it calls 43954 at 0x66700b) while that effect is a fire effect the player cast, the player is stoked,
//       and the create data's base is the effect's MGEF associated hazard. 36885's prologue (48 89 5C 24 18) is detoured
//       too: its thunk opens a thread-local window naming the effect when it carries the SpawnHazardEffect vtable. (The
//       earlier window on 34360 was GuideEffect's path trail, never a SpawnHazardEffect.)
// SE (fix F1): no SE id of 43954 / 36885 is derived (an attempted AE->SE match only), so neither is installed on SE; the
// hook list says so, and testerMode records the candidate's first bytes for an offline check.
// A pointer that fails its check (form type, vtable) swaps nothing and is counted; a fault trips the SEH guard once and
// stops that swap decision for the session (the detour stays and calls the original).

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
        constexpr std::uint64_t kCreateSeCandidate = 42786, kCreateAe = 43954;  // SE: an unverified match
        constexpr std::uint64_t kSpawnAe = 36885;                              // SE: no id found
        constexpr std::size_t kCreateBase = 0x00;  // BGSHazard*
        constexpr std::size_t kCreateCell = 0x08;  // TESObjectCELL*
        constexpr const char* kCreateName = "hazard creation (v2 copy swap)";
        constexpr const char* kSpawnName = "SpawnHazardEffect start (v2 hazard window)";

        using Fn4 = std::uintptr_t(void*, void*, void*, void*);

        // Vtable ids (AE; CommonLib Offsets_VTABLE.h), checked and resolved at install
        constexpr std::uint64_t kHazardVtableAe = 189079;  // VTABLE_BGSHazard[0]
        constexpr std::uint64_t kCellVtableAe = 190143;    // VTABLE_TESObjectCELL[0]
        constexpr std::uint64_t kSpawnVtableAe = 207209;   // VTABLE_SpawnHazardEffect[0]
        std::uintptr_t g_hazardVtable = 0;
        std::uintptr_t g_cellVtable = 0;
        std::uintptr_t g_spawnVtable = 0;

        std::uintptr_t g_createOriginal = 0;
        std::uintptr_t g_spawnOriginal = 0;
        thread_local RE::ActiveEffect* t_spawnEffect = nullptr;  // the SpawnHazardEffect starting on this thread, or null

        // F3: the window restored however the original returns
        class SpawnScope
        {
        public:
            explicit SpawnScope(RE::ActiveEffect* a_effect) noexcept : m_previous(t_spawnEffect) { t_spawnEffect = a_effect; }
            ~SpawnScope() { t_spawnEffect = m_previous; }
            SpawnScope(const SpawnScope&) = delete;
            SpawnScope& operator=(const SpawnScope&) = delete;

        private:
            RE::ActiveEffect* m_previous;
        };

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

        // G2-1: the hazard the spawn-hazard effect's MGEF names (its associated form)
        const RE::TESForm* SpawnedHazardOf(const RE::ActiveEffect* a_effect)
        {
            const auto* base = a_effect->GetBaseObject();
            return base ? base->data.associatedForm : nullptr;
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
            if (t_spawnEffect && IsStoked() && SpawnedHazardOf(t_spawnEffect) == base && IsPlayerVisualEffect(t_spawnEffect)) {
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
            try {
                if (a_data && GetConfig().copyHazardCreate) {
                    (void)Guarded("copy hazard create", [&]() { swapTo = Decide(a_data); });
                }
            } catch (...) {
                swapTo = nullptr;
                if (!g_failure.exchange(true)) {
                    logger::error("BlackFlame: the hazard creation detour threw; the original went on; not logged again");
                }
            }
            // F3: the caller's create data is put back however the original returns
            const CopySwapDetail::SlotSwap<RE::TESForm> swap(static_cast<RE::TESForm**>(a_data), swapTo);
            return reinterpret_cast<Fn4*>(g_createOriginal)(a_data, a_rdx, a_r8, a_r9);
        }

        std::uintptr_t SpawnThunk(void* a_effect, void* a_rdx, void* a_r8, void* a_r9)
        {
            g_spawnCalls.fetch_add(1);
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
            const SpawnScope window(effect);
            return reinterpret_cast<Fn4*>(g_spawnOriginal)(a_effect, a_rdx, a_r8, a_r9);
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
            if (!REL::Module::IsAE()) {
                // F1: neither function's SE id is derived; nothing is detoured on SE (bytes recorded in testerMode)
                a_status.hazardCreate.note = kSeNotVerified;
                a_status.spawnHazard.note = kSeNotVerified;
                RecordSeCandidate(kCreateName, kCreateSeCandidate);
                Patch::RecordHook(kSpawnName, false, 0, {}, std::string(kSeNotVerified) + " (no SE id found)");
                return;
            }
            if (GetCopyCounts().hazards == 0) {
                a_status.hazardCreate.note = "no hazard copies";
                return;
            }
            if (!Patch::HasIds({ kHazardVtableAe, kCellVtableAe, kSpawnVtableAe }, "copy hazard create (vtables)")) {
                a_status.hazardCreate.note = "an Address Library vtable id is missing";
                Patch::RecordHook(kCreateName, false, 0, {}, a_status.hazardCreate.note);
                return;
            }
            g_hazardVtable = Patch::AddressOf(kHazardVtableAe);
            g_cellVtable = Patch::AddressOf(kCellVtableAe);
            g_spawnVtable = Patch::AddressOf(kSpawnVtableAe);
            if (!Patch::HasIds({ kCreateAe }, "copy hazard create")) {
                a_status.hazardCreate.note = "Address Library id missing";  // F7
                Patch::RecordHook(kCreateName, false, 0, {}, a_status.hazardCreate.note);
            } else {
                a_status.hazardCreate = Patch::PrologueDetour(kCreateName, Patch::AddressOf(kCreateAe),
                    reinterpret_cast<std::uintptr_t>(&CreateThunk), g_createOriginal);
            }
            if (!a_status.hazardCreate.hooked) {
                logger::warn("BlackFlame: copy swap: hazard creation not detoured ({}); bytes {}", a_status.hazardCreate.note,
                    a_status.hazardCreate.bytes);
                return;
            }
            if (!Patch::HasIds({ kSpawnAe }, "copy hazard spawn window")) {
                a_status.spawnHazard.note = "Address Library id missing";  // F7
                Patch::RecordHook(kSpawnName, false, 0, {}, a_status.spawnHazard.note);
            } else {
                a_status.spawnHazard = Patch::PrologueDetour(kSpawnName, Patch::AddressOf(kSpawnAe),
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
