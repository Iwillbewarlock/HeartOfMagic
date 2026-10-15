#include "HazardHook.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "GameThread.h"
#include "LogLimit.h"
#include "Recolor.h"
#include "SehGuard.h"

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

        constexpr std::size_t kSet3DIndex = 0x6C;       // RE/T/TESObjectREFR.h: Set3D (Hazard does not override it)
        constexpr std::size_t kInitializeIndex = 0xA2;  // RE/H/Hazard.h: Initialize
        constexpr int kMaxDiagnostics = 20;             // the first hook calls on any hazard
        constexpr auto kOwnerWait = std::chrono::seconds(2);
        constexpr std::size_t kMaxQueued = 512;  // hazards live seconds: the once-only set starts over past this

        struct PendingHazard
        {
            RE::ObjectRefHandle handle;
            std::uint32_t generation = 0;
            Clock::time_point queuedAt{};
        };

        std::mutex g_mutex;
        std::atomic<std::size_t> g_pendingCount{ 0 };
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
            logger::info("BlackFlame: hazard diagnostics [{}] {}: base {:08X}, spell {:08X} (fire: {}), owner {:08X}, light "
                         "{}, 3D {}, thread {}",
                a_hook, line, base ? base->GetFormID() : 0, spell ? spell->GetFormID() : 0, IsFireHazard(base) ? "yes" : "no",
                owner ? owner->GetFormID() : 0, data.light ? "set" : "null", a_has3D ? "present" : "null",
                REX::W32::GetCurrentThreadId());
        }

        // Any thread: queue a hazard with 3D once, and have it handled on the next game-thread task
        void Queue(RE::Hazard* a_hazard)
        {
            if (!IsStoked() || !a_hazard->Get3D()) {
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
            LogHookThread(g_threadLogs, "Hazard Set3D/Initialize", "handled by a game-thread task",
                "handled by a game-thread task");
            QueueTask("BlackFlameHazard", &TickHazards);
        }

        void RecolorHazard(RE::Hazard* a_hazard, const RE::BGSHazard* a_base)
        {
            RecolorStats stats;
            if (!Guarded("hazard recolour", [&]() { stats = RecolorTree(a_hazard->Get3D()); })) {
                return;
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
                logger::info("BlackFlame: hazard {:08X} (spell {:08X}): {}; light {} {}{}", a_base->GetFormID(),
                    spell ? spell->GetFormID() : 0, stats.Describe(), lightKind, tinted ? "tinted" : "left",
                    suppressed ? std::format(" ({} similar lines held back)", suppressed) : "");
            }
        }

        // One queued hazard: handled or dropped (true), or its owner still unset (false: wait)
        bool Handle(const PendingHazard& a_pending, RE::PlayerCharacter* a_player)
        {
            if (a_pending.generation != WorldGeneration()) {
                return true;
            }
            const auto ref = a_pending.handle.get();
            auto* hazard = ref ? ref->As<RE::Hazard>() : nullptr;
            if (!hazard || !hazard->Get3D()) {
                return true;
            }
            const auto owner = hazard->GetHazardRuntimeData().ownerActor.get();
            if (!owner) {
                if (Clock::now() - a_pending.queuedAt < kOwnerWait) {
                    return false;
                }
                int suppressed = 0;
                if (LogLimit::Allow("hazard no owner", 0, suppressed)) {
                    logger::info("BlackFlame: hazard {:08X}: no ownerActor after {} s - left", hazard->GetFormID(),
                        kOwnerWait.count());
                }
                return true;
            }
            const auto* base = BaseOf(hazard);
            if (owner.get() == a_player && IsFireHazard(base) && IsStoked()) {
                RecolorHazard(hazard, base);
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
        auto* player = RE::PlayerCharacter::GetSingleton();
        std::vector<PendingHazard> again;
        for (const auto& pending : due) {
            if (!Handle(pending, player)) {
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
