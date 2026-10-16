#include "HazardHook.h"
#include "TrackedMutex.h"

#include "Attribution.h"
#include "BuffState.h"
#include "Config.h"
#include "CopySwap.h"
#include "FormCopies.h"
#include "FireSet.h"
#include "GameThread.h"
#include "LogLimit.h"
#include "Recolor.h"
#include "ImpactSwap.h"
#include "ScriptTags.h"
#include "SehGuard.h"
#include "VtableSlots.h"

#include <atomic>
#include <chrono>
#include <format>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace BlackFlame
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        constexpr int kMaxDiagnostics = 20;             // the first hook calls on any hazard
        constexpr auto kOwnerWait = std::chrono::seconds(2);
        constexpr std::size_t kMaxQueued = 512;  // hazards live seconds: the once-only set starts over past this

        struct PendingHazard
        {
            RE::ObjectRefHandle handle;
            std::uint32_t generation = 0;
            Clock::time_point queuedAt{};
        };

        TrackedMutex g_mutex;
        std::atomic<std::size_t> g_pendingCount{ 0 };
        std::atomic<bool> g_tickQueued{ false };  // E10: a TickHazards task is waiting in the queue
        std::atomic<int> g_diagnostics{ 0 };
        std::atomic<int> g_threadLogs{ 0 };
        std::atomic<bool> g_failureReported{ false };

        // Both guarded by g_mutex; heap-held like the other layers' lists
        std::vector<PendingHazard>& Pending()
        {
            static auto* pending = std::make_unique<std::vector<PendingHazard>>().release();
            return *pending;
        }

        std::unordered_set<std::uint32_t>& Queued()
        {
            static auto* queued = std::make_unique<std::unordered_set<std::uint32_t>>().release();
            return *queued;
        }

        void ReportFailure()
        {
            if (!g_failureReported.exchange(true)) {
                logger::error("BlackFlame: hazard hook threw; further failures are not logged");
            }
        }

        const RE::BGSHazard* BaseOf(RE::Hazard* a_hazard)
        {
            auto* base = a_hazard->GetBaseObject();
            return base ? base->As<RE::BGSHazard>() : nullptr;
        }

        void Diagnose(const char* a_hook, RE::Hazard* a_hazard, bool a_has3D)
        {
            if (g_diagnostics.load() >= kMaxDiagnostics) {
                return;
            }
            const int line = g_diagnostics.fetch_add(1) + 1;
            if (line > kMaxDiagnostics) {
                return;
            }
            const auto* base = BaseOf(a_hazard);
            const auto* spell = base ? base->data.spell : nullptr;
            auto& data = a_hazard->GetHazardRuntimeData();
            const auto owner = data.ownerActor.get();
            // Hazard overrides only SetActorCause (0x50); what the base GetActorCause returns is logged here
            const auto* cause = a_hazard->GetActorCause();
            const auto causeActor = cause ? cause->actor.get() : RE::NiPointer<RE::Actor>{};
            logger::info("BlackFlame: hazard diagnostics [{}] {}: base {:08X}, spell {:08X} (fire: {}), owner {:08X}, cause "
                         "{} {:08X}, light {}, 3D {}, callerThread={}",
                a_hook, line, base ? base->GetFormID() : 0, spell ? spell->GetFormID() : 0, IsFireHazard(base) ? "yes" : "no",
                owner ? owner->GetFormID() : 0, cause ? "set" : "null", causeActor ? causeActor->GetFormID() : 0, data.light ? "set" : "null", a_has3D ? "present" : "null",
                REX::W32::GetCurrentThreadId());
        }

        // Any thread: queue a hazard with 3D once, and have it handled on the next game-thread task
        void Queue(RE::Hazard* a_hazard)
        {
            // Made inside the player's impact (SpawnCollisionEffects creates the impact hazard; its owner is
            // not set there and the cause is no rule): tagged, so the attribution below knows it. v2: also inside
            // a copy window, and a hazard whose base is a copy (only the player's fire chain reaches a copy)
            const auto* hazardBase = a_hazard->GetBaseObject();
            if (ImpactWindowOpen() || CopyWindowOpen() || (hazardBase && IsCopy(hazardBase))) {
                TagSource(a_hazard);
            }
            if (!IsStoked() || (!GetConfig().hazard && !GetConfig().light) || !a_hazard->Get3D()) {
                return;
            }
            const auto handle = a_hazard->GetHandle();
            {
                std::lock_guard lock(g_mutex);
                auto& queued = Queued();
                if (queued.size() > kMaxQueued) {
                    queued.clear();
                }
                if (!queued.insert(handle.native_handle()).second) {
                    return;
                }
                auto& pending = Pending();
                pending.push_back({ handle, WorldGeneration(), Clock::now() });
                g_pendingCount.store(pending.size());
            }
            LogHookThread(g_threadLogs, "Hazard Set3D/Initialize");
            if (!g_tickQueued.exchange(true)) {  // E10: one TickHazards in the queue at a time
                QueueTask("BlackFlameHazard", []() {
                    g_tickQueued.store(false);
                    TickHazards();
                });
            }
        }

        // Back into the queue on a later Set3D/Initialize (an early call may have had placeholder 3D)
        void Unqueue(const RE::ObjectRefHandle& a_handle)
        {
            std::lock_guard lock(g_mutex);
            Queued().erase(a_handle.native_handle());
        }

        // True when something changed (a shape recoloured or already grey, or the light tinted)
        bool RecolorHazard(RE::Hazard* a_hazard, const RE::BGSHazard* a_base, PlayerLink a_link)
        {
            RecolorStats stats;
            // The 3D only with config hazard (off by default since v2); the light with config light
            if (GetConfig().hazard && !Guarded("hazard recolour", [&]() { stats = RecolorTree(a_hazard->Get3D()); })) {
                return true;  // faulted: not again
            }
            const char* lightKind = "not touched (light false)";
            bool tinted = false;
            if (GetConfig().light) {
                (void)Guarded("hazard light", [&]() {
                    auto* light = CheckedLight(a_hazard->GetHazardRuntimeData().light.get(), "hazard");
                    lightKind = light ? "" : "none or not an NiLight,";
                    tinted = TintLight(light);
                });
            }
            const auto* spell = a_base->data.spell;
            int suppressed = 0;
            if (LogLimit::Allow("hazard", a_base->GetFormID(), suppressed)) {
                logger::info("BlackFlame: hazard {:08X} (spell {:08X}, the player's through its {}): {}; light {} {}{}",
                    a_base->GetFormID(), spell ? spell->GetFormID() : 0, PlayerLinkName(a_link), stats.Describe(),
                    lightKind, tinted ? "tinted" : "left",
                    suppressed ? std::format(" ({} similar lines held back)", suppressed) : "");
            }
            return stats.Recolored() + stats.alreadyOurs > 0 || tinted;
        }

        // One queued hazard: handled or dropped (true), or its owner still unset (false: wait)
        bool Handle(const PendingHazard& a_pending)
        {
            if (a_pending.generation != WorldGeneration()) {
                return true;
            }
            const auto ref = a_pending.handle.get();
            auto* hazard = ref ? ref->As<RE::Hazard>() : nullptr;
            if (!hazard || !hazard->Get3D()) {
                return true;
            }
            (void)ResolvePending(hazard);  // a task, not a hook: a pending script tag may be judged here
            const auto owner = hazard->GetHazardRuntimeData().ownerActor.get();
            if (!owner && !IsTaggedSource(hazard)) {
                if (Clock::now() - a_pending.queuedAt < kOwnerWait) {
                    return false;
                }
                int suppressed = 0;
                if (LogLimit::Allow("hazard no owner", 0, suppressed)) {
                    logger::info("BlackFlame: hazard {:08X}: no ownerActor or script tag after {} s - left",
                        hazard->GetFormID(), kOwnerWait.count());
                }
                return true;
            }
            const auto* base = BaseOf(hazard);
            const auto link = LinkToPlayer("hazard", hazard, owner.get());
            if (link != PlayerLink::kNone && IsFireHazard(base) && IsStoked()) {
                LogLinkOnce("hazard", link);
                if (!RecolorHazard(hazard, base, link)) {
                    Unqueue(a_pending.handle);
                }
            }
            return true;
        }

        struct Set3DHook
        {
            static void thunk(RE::Hazard* a_this, RE::NiAVObject* a_object, bool a_queue3DTasks)
            {
                func(a_this, a_object, a_queue3DTasks);
                // Entered from the engine's vtable: nothing may throw out of here
                try {
                    if (a_this) {
                        (void)Guarded("hazard Set3D", [&]() {
                            Diagnose("Set3D", a_this, a_object != nullptr);
                            Queue(a_this);
                        });
                    }
                } catch (...) {
                    ReportFailure();
                }
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        struct InitializeHook
        {
            static void thunk(RE::Hazard* a_this)
            {
                func(a_this);
                try {
                    if (a_this) {
                        (void)Guarded("hazard Initialize", [&]() {
                            Diagnose("Initialize", a_this, a_this->Get3D() != nullptr);
                            Queue(a_this);
                        });
                    }
                } catch (...) {
                    ReportFailure();
                }
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };
    }

    void InstallHazardHook()
    {
        REL::Relocation<std::uintptr_t> vtbl{ RE::Hazard::VTABLE[0] };
        Set3DHook::func = vtbl.write_vfunc(kSet3DIndex, Set3DHook::thunk);
        InitializeHook::func = vtbl.write_vfunc(kInitializeIndex, InitializeHook::thunk);
        logger::info("BlackFlame: hooked Set3D (0x{:X}) and Initialize (0x{:X}) on the Hazard vtable", kSet3DIndex,
            kInitializeIndex);
    }

    void TickHazards()
    {
        if (g_pendingCount.load() == 0) {
            return;
        }
        std::vector<PendingHazard> due;
        {
            std::lock_guard lock(g_mutex);
            due.swap(Pending());
        }
        std::vector<PendingHazard> again;
        for (const auto& pending : due) {
            if (!Handle(pending)) {
                again.push_back(pending);
            }
        }
        std::lock_guard lock(g_mutex);
        auto& pending = Pending();
        pending.insert(pending.end(), again.begin(), again.end());
        g_pendingCount.store(pending.size());
    }

    bool HasPendingHazards() noexcept
    {
        return g_pendingCount.load() > 0;
    }

    void ResetHazardState()
    {
        std::lock_guard lock(g_mutex);
        Pending().clear();
        Queued().clear();
        g_pendingCount.store(0);
    }
}
