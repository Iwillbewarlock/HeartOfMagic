#include "DrawTimeGreyShared.h"

#include "BuffState.h"
#include "GameThread.h"
#include "Palette.h"
#include "Patch.h"
#include "SehGuard.h"
#include "TrackedMutex.h"

#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <chrono>
#include <format>
#include <string>

// =============================================================================
// DRAW-TIME GREY - the BSEffectShader SetupGeometry / RestoreGeometry hook (DrawTimeGrey.h)
// =============================================================================
// The palette slot is measured, not assumed: Community Shaders' Effect.hlsl declares TexGrayscaleSampler at t4, but
// what the engine binds is read here - the one PSTexture[] index holding the player shape's own greyscale texture
// view after the original SetupGeometry, agreed on by kSlotAgreement materials (MeasureSlot) (the engine binds material textures in SetupMaterial, vfunc 04, so a diff
// across SetupGeometry alone would show nothing).

namespace BlackFlame::DrawTime
{
    namespace
    {
        using ShadowData = RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA;  // VR is not built

        constexpr std::size_t kSetupGeometryIndex = 0x6;    // RE/B/BSShader.h: SetupGeometry
        constexpr std::size_t kRestoreGeometryIndex = 0x7;  // RE/B/BSShader.h: RestoreGeometry
        constexpr std::uint64_t kVtableSE = 305447;         // RE/Offsets_VTABLE.h VTABLE_BSEffectShader[0]
        constexpr std::uint64_t kVtableAE = 255194;
        constexpr std::uint64_t kShadowStateSE = 524773;    // RE/R/RendererShadowState.h GetSingleton
        constexpr std::uint64_t kShadowStateAE = 388819;
        constexpr int kTextureSlots = 16;                   // RendererShadowState PSTexture[16]
        constexpr int kMaxOpenBinds = 4;                    // binds open on one thread at once (1 expected)
        constexpr int kThreadLogs = 8;
        constexpr int kHitLogs = 6;  // per root kind
        constexpr int kNonHitLogs = 10;
        constexpr int kLeakLogs = 10;
        constexpr int kSameMaterialLogs = 6;
        constexpr int kMismatchLogs = 6;
        constexpr int kSlotLogs = 8;
        constexpr int kSlotAgreement = 3;           // distinct materials that must name the same slot before it is used
        constexpr int kMaxMeasuredMaterials = 64;   // distinct eligible materials examined per buff-on, then no more
        constexpr double kNsPerUs = 1000.0;

        std::atomic<bool> g_installed{ false };
        std::uintptr_t g_vtable = 0;

        Counters& CountersRef()
        {
            static auto* counters = std::make_unique<Counters>().release();  // nothing runs at DLL unload
            return *counters;
        }

        std::atomic<std::shared_ptr<const Snapshot>>& SnapshotRef()
        {
            static auto* snapshot = std::make_unique<std::atomic<std::shared_ptr<const Snapshot>>>().release();
            return *snapshot;
        }

        std::atomic<int> g_threadLogs{ 0 };
        std::array<std::atomic<int>, static_cast<std::size_t>(RootKind::kCount)> g_hitLogs{};
        std::atomic<int> g_nonHitLogs{ 0 };
        std::atomic<int> g_leakLogs{ 0 };
        std::atomic<int> g_sameMaterialLogs{ 0 };
        std::atomic<int> g_mismatchLogs{ 0 };
        std::atomic<int> g_slotLogs{ 0 };
        // Render hooks off for the session after a fault (their own flag: a kDisableStep Guarded would take
        // SehGuard's lock and build a std::string key on every pass once any step anywhere has faulted)
        std::atomic<bool> g_setupOff{ false };

        // The slot vote: one candidate slot and the distinct materials that named it, and every eligible material
        // examined (each looked at once). Under g_voteMutex: the render side try_locks, the buff-on reset locks
        struct SlotVote
        {
            int candidate = -1;
            std::array<const RE::BSShaderMaterial*, kSlotAgreement> materials{};
            int agreed = 0;
            std::array<const RE::BSShaderMaterial*, kMaxMeasuredMaterials> examined{};
            int examinedCount = 0;
        };
        TrackedMutex g_voteMutex;
        SlotVote g_vote;
        std::atomic<int> g_examined{ 0 };  // g_vote.examinedCount, readable without the lock (the cap check)

        // A log line is due while its counter is under the cap (read first, so the counter stops growing there and
        // cannot wrap)
        bool AllowLog(std::atomic<int>& a_counter, int a_cap)
        {
            return a_counter.load(std::memory_order_relaxed) < a_cap &&
                   a_counter.fetch_add(1, std::memory_order_relaxed) < a_cap;
        }

        // One open bind: what the slot held before ours, for that pass's RestoreGeometry
        struct OpenBind
        {
            const RE::BSRenderPass* pass = nullptr;
            Srv* saved = nullptr;
            Srv* ours = nullptr;
            int slot = -1;
        };

        struct ThreadBinds
        {
            std::array<OpenBind, kMaxOpenBinds> open{};
            int count = 0;
            const RE::BSShaderMaterial* lastBoundMaterial = nullptr;  // the player->NPC->player same-material log
        };

        thread_local ThreadBinds t_binds;

        ShadowData& Shadow()
        {
            return RE::BSGraphics::RendererShadowState::GetSingleton()->GetRuntimeData();
        }

        // RendererShadowState::SetPSTexture (RendererShadowState.h:278-286) with a raw view: the engine applies the slot
        // at the draw because its modified bit is set
        void WriteSlot(ShadowData& a_state, int a_slot, Srv* a_view)
        {
            if (a_state.PSTexture[a_slot] != a_view) {
                a_state.PSTexture[a_slot] = a_view;
                a_state.PSResourceModifiedBits |= (1u << a_slot);
            }
        }

        std::uint64_t NowNs()
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        }

        std::uintptr_t Addr(const void* a_pointer)
        {
            return reinterpret_cast<std::uintptr_t>(a_pointer);
        }

        // Puts back one open bind when the slot still holds ours
        void Close(ShadowData& a_state, const OpenBind& a_bind)
        {
            auto& counters = CountersRef();
            if (a_state.PSTexture[a_bind.slot] == a_bind.ours) {
                WriteSlot(a_state, a_bind.slot, a_bind.saved);
                counters.restores.fetch_add(1, std::memory_order_relaxed);
            } else {
                counters.restoreChanged.fetch_add(1, std::memory_order_relaxed);
            }
        }

        void LogThreadOnce()
        {
            if (!AllowLog(g_threadLogs, kThreadLogs)) {
                return;
            }
            const auto* main = RE::Main::GetSingleton();
            logger::info("BlackFlame: draw-time grey: SetupGeometry on thread {} (stamped main thread {}, Main::threadID {})",
                REX::W32::GetCurrentThreadId(), g_gameThreadNativeId.load(), main ? main->threadID : 0);
        }

        // The slot: the PSTexture[] index holding the shape's own greyscale view. Only a shape whose greyscale view is not
        // also its source texture's (that would name the diffuse slot) and sits in exactly one slot votes; the slot is
        // used once kSlotAgreement distinct materials named it. No allocation unless a line is logged
        void MeasureSlot(const ShadowData& a_state, const Target& a_target)
        {
            if (g_examined.load(std::memory_order_relaxed) >= kMaxMeasuredMaterials) {
                return;
            }
            if (!a_target.source || a_target.source == a_target.base || !a_target.material) {
                return;  // not eligible: not counted
            }
            int found = -1;
            int matches = 0;
            int baseSlot = -1;
            for (int i = 0; i < kTextureSlots; ++i) {
                if (a_state.PSTexture[i] == a_target.source) {
                    found = found < 0 ? i : found;
                    ++matches;
                }
                if (a_target.base && baseSlot < 0 && a_state.PSTexture[i] == a_target.base) {
                    baseSlot = i;
                }
            }
            int agreed = 0;
            bool locked = false;
            {
                std::unique_lock lock(g_voteMutex, std::try_to_lock);
                if (!lock.owns_lock()) {
                    return;  // another thread holds the vote: not counted, this pass waits for the next
                }
                auto& vote = g_vote;
                const auto examinedEnd = vote.examined.begin() + vote.examinedCount;
                if (vote.examinedCount >= kMaxMeasuredMaterials ||
                    std::find(vote.examined.begin(), examinedEnd, a_target.material) != examinedEnd) {
                    return;  // the cap, or this material was looked at already: not counted again
                }
                vote.examined[static_cast<std::size_t>(vote.examinedCount++)] = a_target.material;
                g_examined.store(vote.examinedCount, std::memory_order_relaxed);
                if (matches == 1) {
                    if (vote.candidate != found) {
                        // a different slot: the count starts again from this material (the examined list stays)
                        vote.candidate = found;
                        vote.materials = {};
                        vote.agreed = 0;
                    }
                    if (vote.agreed < kSlotAgreement) {
                        vote.materials[static_cast<std::size_t>(vote.agreed++)] = a_target.material;
                    }
                    if (vote.agreed >= kSlotAgreement) {
                        int expected = -1;
                        locked = CountersRef().slot.compare_exchange_strong(expected, found);
                    }
                }
                agreed = vote.agreed;  // the real count, also for a material that did not vote
            }
            if (AllowLog(g_slotLogs, kSlotLogs)) {
                logger::info("BlackFlame: draw-time grey: slot measure on material 0x{:X}: greyscale view 0x{:X} in {} PS "
                             "slot(s), first {} (source texture view 0x{:X} in slot {}); votes {}/{}{}",
                    Addr(a_target.material), Addr(a_target.source), matches, found, Addr(a_target.base), baseSlot, agreed,
                    kSlotAgreement, locked ? std::format(" - slot {} in use from now on", found) : "");
            }
        }

        // A pass not in the set: the NPC leak check and the first logs
        void OnNonPlayerPass(const RE::BSRenderPass* a_pass, const Snapshot& a_snapshot, ShadowData& a_state, int a_slot)
        {
            auto& counters = CountersRef();
            Srv* inSlot = a_slot >= 0 ? a_state.PSTexture[a_slot] : nullptr;
            const bool ours = inSlot && a_snapshot.ours.contains(inSlot);
            const auto* material = a_pass->shaderProperty ? a_pass->shaderProperty->material : nullptr;
            // Only when the slot holds one of ours (rare): the pass's own effect material's palette, read from the live
            // pass (the engine is drawing it), never from our snapshot
            bool ownView = false;
            if (ours) {
                auto* effect = netimmerse_cast<RE::BSEffectShaderProperty*>(a_pass->shaderProperty);
                const auto* effectMaterial = effect ? effect->GetMaterial() : nullptr;
                const auto* ownPalette = effectMaterial ? effectMaterial->greyscaleTexture.get() : nullptr;
                ownView = ownPalette && ownPalette->rendererTexture && ownPalette->rendererTexture->resourceView == inSlot;
            }
            const bool skippedPlayer = a_snapshot.playerSkipped.contains(a_pass->geometry);
            if (ours && (ownView || skippedPlayer)) {
                // A copy's own grey palette, bound by the engine (also one the next walk has not listed yet, or after
                // a clear): no leak, and left out of the diagnostic lines below
                counters.playerOwnPalette.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (skippedPlayer) {
                return;  // the player's, not a non-player pass: no diagnostic line
            }
            if (ours) {
                counters.npcLeaks.fetch_add(1, std::memory_order_relaxed);
                if (AllowLog(g_leakLogs, kLeakLogs)) {
                    logger::warn("BlackFlame: draw-time grey: NPC LEAK: pass geometry 0x{:X} property 0x{:X} (not the player's) "
                                 "has our palette view 0x{:X} in slot {}",
                        Addr(a_pass->geometry), Addr(a_pass->shaderProperty), Addr(inSlot), a_slot);
                }
            }
            if (material && material == t_binds.lastBoundMaterial && AllowLog(g_sameMaterialLogs, kSameMaterialLogs)) {
                logger::info("BlackFlame: draw-time grey: same-material pass after a bind: geometry 0x{:X} (not the "
                             "player's), material 0x{:X}, slot {} holds 0x{:X} ({})",
                    Addr(a_pass->geometry), Addr(material), a_slot, Addr(inSlot), ours ? "OURS - leak" : "not ours");
            }
            t_binds.lastBoundMaterial = nullptr;
            if (AllowLog(g_nonHitLogs, kNonHitLogs)) {
                logger::info("BlackFlame: draw-time grey: non-player pass: geometry 0x{:X} property 0x{:X} material 0x{:X}, "
                             "slot {} holds 0x{:X}",
                    Addr(a_pass->geometry), Addr(a_pass->shaderProperty), Addr(material), a_slot, Addr(inSlot));
            }
        }

        void OnPlayerPass(const RE::BSRenderPass* a_pass, const Target& a_target, ShadowData& a_state)
        {
            auto& counters = CountersRef();
            counters.playerHits.fetch_add(1, std::memory_order_relaxed);
            if (a_pass->shaderProperty != a_target.property) {
                counters.propMismatch.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (!a_target.grey) {
                counters.leftAlone.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (counters.slot.load(std::memory_order_relaxed) < 0) {
                MeasureSlot(a_state, a_target);
            }
            const int slot = counters.slot.load(std::memory_order_relaxed);
            if (slot < 0) {
                counters.unmeasured.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            Srv* before = a_state.PSTexture[slot];
            if (before != a_target.source) {
                counters.slotMismatch.fetch_add(1, std::memory_order_relaxed);
                if (AllowLog(g_mismatchLogs, kMismatchLogs)) {
                    logger::info("BlackFlame: draw-time grey: slot {} holds 0x{:X}, not the shape's palette 0x{:X} (geometry "
                                 "0x{:X}) - not bound",
                        slot, Addr(before), Addr(a_target.source), Addr(a_pass->geometry));
                }
                return;
            }
            if (t_binds.count >= kMaxOpenBinds) {
                return;  // counted as a hit without a bind
            }
            // Recorded before the write: a fault in between leaves a restore that finds the slot unchanged
            t_binds.open[t_binds.count++] = OpenBind{ a_pass, before, a_target.grey, slot };
            WriteSlot(a_state, slot, a_target.grey);
            counters.binds.fetch_add(1, std::memory_order_relaxed);
            t_binds.lastBoundMaterial = a_target.material;
            if (AllowLog(g_hitLogs[static_cast<std::size_t>(a_target.root)], kHitLogs)) {
                constexpr std::array kGreyKindNames{ "derived", "fixed", "fixed add", "left alone" };
                logger::info("BlackFlame: draw-time grey: bind ({}): geometry 0x{:X} property 0x{:X} material 0x{:X}, slot {}: "
                             "0x{:X} -> 0x{:X} ({} palette)",
                    RootKindName(a_target.root), Addr(a_pass->geometry), Addr(a_pass->shaderProperty), Addr(a_target.material),
                    slot, Addr(before), Addr(a_target.grey), kGreyKindNames[static_cast<std::size_t>(a_target.kind)]);
            }
        }

        // Binds still open when a new SetupGeometry starts on this thread: closed now (a missed RestoreGeometry would
        // otherwise leave grey for the next same-material draw)
        void CloseUnpaired()
        {
            if (t_binds.count == 0) {
                return;
            }
            auto& state = Shadow();
            while (t_binds.count > 0) {  // one at a time: a fault leaves count = the binds really left open
                Close(state, t_binds.open[static_cast<std::size_t>(t_binds.count - 1)]);
                --t_binds.count;
                CountersRef().unpaired.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Closing them faulted: what is left open is forgotten (never written back blind) and said once per drop
        void DropOpenBinds(const char* a_why) noexcept
        {
            const int left = t_binds.count;
            t_binds.count = 0;
            CountersRef().failures.fetch_add(1, std::memory_order_relaxed);
            if (left > 0) {
                try {
                    logger::warn("BlackFlame: draw-time grey: dropped {} open bind(s) unrestored ({} while closing them on "
                                 "thread {})",
                        left, a_why, REX::W32::GetCurrentThreadId());
                } catch (...) {
                }
            }
        }

        void OnSetup(const RE::BSRenderPass* a_pass)
        {
            auto& counters = CountersRef();
            const auto start = NowNs();
            const auto snapshot = LoadSnapshot();  // after the thunk's inside increment
            counters.passes.fetch_add(1, std::memory_order_relaxed);
            if (!IsOnGameThread()) {
                counters.passesOffMain.fetch_add(1, std::memory_order_relaxed);
            }
            counters.renderThread.store(REX::W32::GetCurrentThreadId(), std::memory_order_relaxed);
            LogThreadOnce();
            if (snapshot) {
                counters.seenGeneration.store(snapshot->generation, std::memory_order_release);
                auto& state = Shadow();
                const auto it = snapshot->targets.find(a_pass->geometry);
                if (it != snapshot->targets.end()) {
                    OnPlayerPass(a_pass, it->second, state);
                } else {
                    OnNonPlayerPass(a_pass, *snapshot, state, counters.slot.load(std::memory_order_relaxed));
                }
            }
            counters.setupNs.fetch_add(NowNs() - start, std::memory_order_relaxed);
        }

        void OnRestore(const RE::BSRenderPass* a_pass)
        {
            const auto start = NowNs();
            auto& state = Shadow();
            for (int i = 0; i < t_binds.count; ++i) {
                if (t_binds.open[i].pass != a_pass) {
                    continue;
                }
                Close(state, t_binds.open[i]);
                t_binds.open[i] = t_binds.open[--t_binds.count];
                break;
            }
            CountersRef().restoreNs.fetch_add(NowNs() - start, std::memory_order_relaxed);
        }

        struct SetupGeometryHook
        {
            static void thunk(RE::BSShader* a_this, RE::BSRenderPass* a_pass, std::uint32_t a_flags)
            {
                // Entered from the engine's vtable: nothing may throw out of here
                try {
                    if (t_binds.count > 0 && !GuardedKeep("draw-time grey unpaired close", [] { CloseUnpaired(); })) {
                        DropOpenBinds("a fault");
                    }
                } catch (...) {
                    DropOpenBinds("an exception");
                }
                func(a_this, a_pass, a_flags);
                if (!a_pass || !IsStoked() || g_setupOff.load(std::memory_order_relaxed)) {
                    return;
                }
                // seq_cst, before the snapshot load and outside the guard (neither a fault nor a throw leaves it
                // raised): DrawTimeGreySet.cpp releases retired geometries only when it reads 0 after a publish
                auto& inside = CountersRef().inside;
                inside.fetch_add(1);
                bool ok = false;
                try {
                    // GuardedKeep, not Guarded: no SehGuard lock or string key per pass; our own flag turns it off
                    ok = GuardedKeep("draw-time grey SetupGeometry", [&]() { OnSetup(a_pass); });
                } catch (...) {
                }
                inside.fetch_sub(1);
                if (!ok) {
                    CountersRef().failures.fetch_add(1, std::memory_order_relaxed);
                    if (!g_setupOff.exchange(true)) {
                        try {
                            logger::error("BlackFlame: draw-time grey: SetupGeometry body failed - no more binds this "
                                          "session (open binds are still restored)");
                        } catch (...) {
                        }
                    }
                }
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        struct RestoreGeometryHook
        {
            static void thunk(RE::BSShader* a_this, RE::BSRenderPass* a_pass, std::uint32_t a_flags)
            {
                // Not gated on the buff: a bind made while stoked is put back even when the buff went off in between.
                // Ours first, then the original (setup order reversed)
                try {
                    if (t_binds.count > 0 && !GuardedKeep("draw-time grey RestoreGeometry", [&]() { OnRestore(a_pass); })) {
                        CountersRef().failures.fetch_add(1, std::memory_order_relaxed);
                    }
                } catch (...) {
                    CountersRef().failures.fetch_add(1, std::memory_order_relaxed);
                }
                func(a_this, a_pass, a_flags);
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        std::string ModuleOf(std::uintptr_t a_address)
        {
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(a_address), &module) ||
                !module) {
                return std::format("0x{:X} (no module)", a_address);
            }
            char path[MAX_PATH]{};
            GetModuleFileNameA(module, path, MAX_PATH);
            std::string name(path);
            if (const auto slash = name.find_last_of("\\/"); slash != std::string::npos) {
                name.erase(0, slash + 1);
            }
            return std::format("{}+0x{:X}", name, a_address - reinterpret_cast<std::uintptr_t>(module));
        }

        std::uintptr_t SlotValue(std::size_t a_index)
        {
            return *reinterpret_cast<const std::uintptr_t*>(g_vtable + a_index * sizeof(void*));
        }

        // Another plugin (Community Shaders) may write the slot after us: a zero-hit run must not be read as "it does
        // not work" when our thunk is simply not called
        std::string SlotState()
        {
            if (!g_installed.load()) {
                return "not hooked";
            }
            const bool setupOurs = SlotValue(kSetupGeometryIndex) == reinterpret_cast<std::uintptr_t>(&SetupGeometryHook::thunk);
            const bool restoreOurs =
                SlotValue(kRestoreGeometryIndex) == reinterpret_cast<std::uintptr_t>(&RestoreGeometryHook::thunk);
            if (setupOurs && restoreOurs) {
                return "ours";
            }
            return std::format("REWRITTEN (setup {}, restore {})", ModuleOf(SlotValue(kSetupGeometryIndex)),
                ModuleOf(SlotValue(kRestoreGeometryIndex)));
        }

        struct Totals
        {
            std::uint64_t passes = 0, offMain = 0, hits = 0, binds = 0, setupNs = 0, restoreNs = 0, rewalkNs = 0;
        };
        Totals g_last;  // main thread only (LogSummary)
        std::chrono::steady_clock::time_point g_lastTime{};
    }

    std::shared_ptr<const Snapshot> LoadSnapshot() noexcept
    {
        return SnapshotRef().load();
    }

    void PublishSnapshot(std::shared_ptr<const Snapshot> a_snapshot) noexcept
    {
        SnapshotRef().store(std::move(a_snapshot));
    }

    Counters& GetCounters() noexcept
    {
        return CountersRef();
    }

    bool HookInstalled() noexcept
    {
        return g_installed.load();
    }

    void LogSummary(std::size_t a_setSize, std::size_t a_roots, const std::string& a_kinds)
    {
        auto& c = CountersRef();
        const auto now = std::chrono::steady_clock::now();
        const Totals totals{ c.passes.load(), c.passesOffMain.load(), c.playerHits.load(), c.binds.load(), c.setupNs.load(),
            c.restoreNs.load(), c.rewalkNs.load() };
        const double seconds = g_lastTime == std::chrono::steady_clock::time_point{} ?
                                   0.0 :
                                   std::chrono::duration<double>(now - g_lastTime).count();
        const auto rate = [&](std::uint64_t a_now, std::uint64_t a_before) {
            return seconds > 0.0 ? static_cast<double>(a_now - a_before) / seconds : 0.0;
        };
        logger::info("BlackFlame: draw-time grey: thread {} (main {}), passes {:.0f}/s, player hits {:.0f}/s, palette binds "
                     "{:.0f}/s, slot {}, failures {}",
            c.renderThread.load(), g_gameThreadNativeId.load(), rate(totals.passes, g_last.passes),
            rate(totals.hits, g_last.hits), rate(totals.binds, g_last.binds), c.slot.load(), c.failures.load());
        logger::info("BlackFlame: draw-time grey: detail: passes off main {:.0f}/s, restores {}, restore found slot changed {}, "
                     "unpaired {}, left alone {}, unmeasured {} (materials examined {}/{}), slot mismatch {}, property "
                     "mismatch {}, NPC LEAKS {}, own grey palette passes {}, set {} geometries ({}) from {} roots, cost "
                     "setup {:.0f} us/s restore {:.0f} us/s rewalk {:.0f} us/s, snapshot seen {}, vtable slots {}{}",
            rate(totals.offMain, g_last.offMain), c.restores.load(), c.restoreChanged.load(), c.unpaired.load(),
            c.leftAlone.load(), c.unmeasured.load(), g_examined.load(), kMaxMeasuredMaterials, c.slotMismatch.load(),
            c.propMismatch.load(), c.npcLeaks.load(), c.playerOwnPalette.load(), a_setSize, a_kinds, a_roots,
            rate(totals.setupNs, g_last.setupNs) / kNsPerUs,
            rate(totals.restoreNs, g_last.restoreNs) / kNsPerUs, rate(totals.rewalkNs, g_last.rewalkNs) / kNsPerUs,
            c.seenGeneration.load(), SlotState(), g_setupOff.load() ? ", SETUP HOOK OFF after a fault" : "");
        g_last = totals;
        g_lastTime = now;
    }

    void StartSummaryWindow()
    {
        auto& c = CountersRef();
        g_last = Totals{ c.passes.load(), c.passesOffMain.load(), c.playerHits.load(), c.binds.load(), c.setupNs.load(),
            c.restoreNs.load(), c.rewalkNs.load() };
        g_lastTime = std::chrono::steady_clock::now();
    }

    void ResetSlotVote()
    {
        if (CountersRef().slot.load() >= 0) {
            return;  // measured: kept for the session
        }
        std::lock_guard lock(g_voteMutex);  // main thread; the render side only try_locks it
        g_vote = SlotVote{};
        g_examined.store(0, std::memory_order_relaxed);
    }

    bool InstallHook()
    {
        if (!PaletteReady()) {
            logger::warn("BlackFlame: draw-time grey: the grey palette did not load - not hooked");
            return false;
        }
        if (!Patch::HasIds({ Patch::IdFor(kVtableSE, kVtableAE), Patch::IdFor(kShadowStateSE, kShadowStateAE) },
                "draw-time grey")) {
            return false;
        }
        // Relocations resolved here, not on the render thread (function-local statics in CommonLib)
        (void)RE::BSGraphics::RendererShadowState::GetSingleton();
        (void)RE::Main::GetSingleton();
        REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_BSEffectShader[0] };
        g_vtable = vtbl.address();
        const auto setupBefore = SlotValue(kSetupGeometryIndex);
        const auto restoreBefore = SlotValue(kRestoreGeometryIndex);
        // Each thunk's original is set BEFORE its slot is written: a render thread may call the thunk the moment the
        // slot changes. Restore first: a SetupGeometry of ours never runs without our RestoreGeometry in place
        RestoreGeometryHook::func = restoreBefore;
        const auto restoreOriginal = Patch::WriteVfunc("BSEffectShader RestoreGeometry (draw-time grey)", vtbl,
            kRestoreGeometryIndex, RestoreGeometryHook::thunk);
        SetupGeometryHook::func = setupBefore;
        const auto setupOriginal = Patch::WriteVfunc("BSEffectShader SetupGeometry (draw-time grey)", vtbl,
            kSetupGeometryIndex, SetupGeometryHook::thunk);
        if (restoreOriginal != restoreBefore || setupOriginal != setupBefore) {
            // Another thread wrote the slot between our read and our write: chain onto what it wrote
            RestoreGeometryHook::func = restoreOriginal;
            SetupGeometryHook::func = setupOriginal;
            logger::warn("BlackFlame: draw-time grey: a vtable slot changed while it was hooked (restore {} -> {}, setup "
                         "{} -> {}) - chained onto the newer one",
                ModuleOf(restoreBefore), ModuleOf(restoreOriginal), ModuleOf(setupBefore), ModuleOf(setupOriginal));
        }
        g_installed.store(true);
        logger::info("BlackFlame: draw-time grey: hooked BSEffectShader vtable {} slots 0x{:X} / 0x{:X} ({}): the originals "
                     "were in {} / {} (another module = chained onto it)",
            ModuleOf(g_vtable), kSetupGeometryIndex, kRestoreGeometryIndex, REL::Module::IsAE() ? "AE" : "SE",
            ModuleOf(setupBefore), ModuleOf(restoreBefore));
        return true;
    }
}
