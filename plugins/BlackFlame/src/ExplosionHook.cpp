#include "ExplosionHook.h"
#include "Patch.h"
#include "MainThreadQueue.h"
#include "TrackedMutex.h"

#include "Attribution.h"
#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "CopySwap.h"
#include "FormCopies.h"
#include "ThreadTrace.h"
#include "GameThread.h"
#include "LogLimit.h"
#include "EffectLight.h"
#include "Recolor.h"
#include "ImpactSwap.h"
#include "ScriptTags.h"
#include "SehGuard.h"
#include "VtableSlots.h"
#include "SelfTestProbe.h"

#include <atomic>
#include <chrono>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BlackFlame
{
    namespace
    {
        constexpr int kMaxAttributionLogs = 5;
        constexpr int kMaxNpcMaterialLogs = 5;

        std::atomic<bool> g_failureReported{ false };
        std::atomic<int> g_threadLogs{ 0 };
        std::atomic<int> g_otherLogs{ 0 };
        std::atomic<int> g_npcMaterialLogs{ 0 };
        std::atomic<int> g_tints{ 0 };
        std::atomic<int> g_lightMissing{ 0 };
        std::atomic<int> g_diagnostics{ 0 };
        constexpr int kMaxDiagnostics = 20;
        constexpr std::size_t kUpdateIndex = 0xA3;      // RE/E/Explosion.h: Update(float), SE/AE 0xA3

        // The explosion's light through the RTTI check (the same declared-type trap as the projectile's
        // light); its real type is logged once
        RE::NiLight* ExplosionLight(RE::Explosion* a_explosion)
        {
            return CheckedLight(a_explosion->GetExplosionRuntimeData().light.get(), "explosion");
        }

        // Explosions to tint once more next tick, under g_lightsMutex (tasks and the message handler
        // reach it). Heap-held and never freed like the other layers' lists.
        TrackedMutex g_lightsMutex;

        std::vector<RE::ObjectRefHandle>& PendingLights()
        {
            static auto* lights = std::make_unique<std::vector<RE::ObjectRefHandle>>().release();
            return *lights;
        }

        // The player's fire explosion's light (config light). C6: its 3D is never recoloured any more - the copy is
        // grey by construction. Nothing when the buff went off before a deferred run
        void TintExplosionLight(RE::Explosion* a_explosion)
        {
            if (!IsStoked() || !GetConfig().light) {
                return;
            }
            auto& data = a_explosion->GetExplosionRuntimeData();
            const int tints = g_tints.fetch_add(1) + 1;
            const bool lightTinted = TintLight(ExplosionLight(a_explosion));
            bool lightQueued = false;
            if (!data.light) {
                // Not created yet at Set3D: one more try on the next ticker tick
                std::lock_guard lock(g_lightsMutex);
                PendingLights().push_back(a_explosion->GetHandle());
                g_lightMissing.fetch_add(1);
                lightQueued = true;
            }
            const auto* base = a_explosion->GetBaseObject();
            const auto baseId = base ? base->GetFormID() : 0;
            int suppressed = 0;
            if (LogLimit::Allow("explosion", baseId, suppressed)) {
                logger::info("BlackFlame: explosion: {:08X}: light {} (missing at Set3D {} of {} times){}", baseId,
                    lightQueued ? "not there yet, retried next tick" : (!data.light ? "none" : (lightTinted ? "tinted" : "left")),
                    g_lightMissing.load(), tints,
                    suppressed ? std::format(" ({} similar lines held back)", suppressed) : "");
            }
        }

        // Game thread: the light tint (when due), the NPC contamination check and, while the
        // self-test runs, its record (a_record carries what Set3D saw)
        void HandleExplosion(RE::Explosion* a_explosion, bool a_tint, bool a_logMaterials,
            SelfTestProbe::ExplosionRecord a_record)
        {
            if (a_logMaterials) {
                logger::info("BlackFlame: material check, NPC fire explosion {:08X} (any grey=yes is contamination): {}",
                    a_record.base, DescribeMaterials(a_explosion->Get3D()));
            }
            if (a_tint) {
                TintExplosionLight(a_explosion);
            }
            if (SelfTestProbe::Recording()) {
                a_record.after = SelfTestProbe::Inspect(a_explosion->Get3D());
                a_record.position = a_explosion->GetPosition();
                a_record.at = std::chrono::steady_clock::now();
                SelfTestProbe::RecordExplosion(std::move(a_record));
            }
        }

        // Every explosion is decided once, whichever hook sees its 3D first with its attribution known
        // (Set3D, Initialize or Update). Keyed by its reference handle, whose age bits differ when a
        // slot is reused. Past the cap only handles whose explosion is gone are dropped.
        TrackedMutex g_processedMutex;
        constexpr std::size_t kMaxProcessed = 512;

        std::unordered_map<std::uint32_t, RE::ObjectRefHandle>& Processed()
        {
            static auto* processed = std::make_unique<std::unordered_map<std::uint32_t, RE::ObjectRefHandle>>().release();
            return *processed;
        }

        bool IsProcessed(RE::Explosion* a_explosion)
        {
            std::lock_guard lock(g_processedMutex);
            return Processed().contains(a_explosion->GetHandle().native_handle());
        }

        // False when another hook got there first
        // Undecided explosions (no owner, cause, blame or tag yet), with when they were first seen: past
        // kUndecidedLimit one is marked processed so the Update route stops looking at it every frame
        constexpr auto kUndecidedLimit = std::chrono::seconds(2);

        std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point>& FirstSeen()
        {
            static auto* seen =
                std::make_unique<std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point>>().release();
            return *seen;
        }

        // Handles whose explosion is gone, looked up outside the lock
        void EvictGone()
        {
            std::vector<RE::ObjectRefHandle> handles;
            {
                std::lock_guard lock(g_processedMutex);
                if (Processed().size() <= kMaxProcessed) {
                    return;
                }
                for (const auto& [value, handle] : Processed()) {
                    handles.push_back(handle);
                }
            }
            std::vector<std::uint32_t> gone;
            for (const auto& handle : handles) {
                if (!handle.get()) {
                    gone.push_back(handle.native_handle());
                }
            }
            std::lock_guard lock(g_processedMutex);
            for (const auto value : gone) {
                Processed().erase(value);
                FirstSeen().erase(value);
            }
        }

        bool MarkProcessed(RE::Explosion* a_explosion)
        {
            EvictGone();
            const auto handle = a_explosion->GetHandle();
            std::lock_guard lock(g_processedMutex);
            FirstSeen().erase(handle.native_handle());
            return Processed().try_emplace(handle.native_handle(), handle).second;
        }

        // True when this explosion has stayed undecided too long (then it is marked processed)
        bool UndecidedTooLong(RE::Explosion* a_explosion)
        {
            const auto value = a_explosion->GetHandle().native_handle();
            const auto now = std::chrono::steady_clock::now();
            {
                std::lock_guard lock(g_processedMutex);
                // Undecided explosions that died inside the window never reach Processed(): dropped here
                std::erase_if(FirstSeen(), [&](const auto& a_entry) { return now - a_entry.second > 2 * kUndecidedLimit; });
                const auto [it, inserted] = FirstSeen().try_emplace(value, now);
                if (inserted || now - it->second < kUndecidedLimit) {
                    return false;
                }
            }
            (void)MarkProcessed(a_explosion);
            return true;
        }

        // The first kMaxDiagnostics hook calls on ANY explosion: does a hook fire at all, and with what
        void Diagnose(const char* a_hook, RE::Explosion* a_explosion, bool a_has3D)
        {
            if (g_diagnostics.fetch_add(1) >= kMaxDiagnostics) {
                return;
            }
            auto* base = a_explosion->GetBaseObject();
            const auto* explosion = base ? base->As<RE::BGSExplosion>() : nullptr;
            auto& data = a_explosion->GetExplosionRuntimeData();
            const auto* casterSpell = data.magicCaster ? data.magicCaster->currentSpell : nullptr;
            const auto owner = data.actorOwner.get();
            const auto cause = data.actorCause ? data.actorCause->actor.get() : RE::NiPointer<RE::Actor>{};
            logger::info("BlackFlame: explosion diagnostics [{}]: base {:08X} (fire set: {}), owner {:08X}, cause actor "
                         "{:08X}, magicCaster {} currentSpell {:08X}, 3D {}, callerThread={}",
                a_hook, explosion ? explosion->GetFormID() : 0, IsFireExplosion(explosion) ? "yes" : "no",
                owner ? owner->GetFormID() : 0, cause ? cause->GetFormID() : 0, data.magicCaster ? "set" : "null",
                casterSpell ? casterSpell->GetFormID() : 0, a_has3D ? "present" : "null", REX::W32::GetCurrentThreadId());
        }

        // a_everyFrame: the Update route, which runs every frame on every explosion - nothing is done
        // there unless the buff is on or the self-test records
        void OnSet3D(RE::Explosion* a_explosion, bool a_everyFrame = false)
        {
            if (a_everyFrame && !IsStoked() && !SelfTestProbe::Recording()) {
                return;
            }
            if (IsProcessed(a_explosion)) {
                return;
            }
            auto* base = a_explosion->GetBaseObject();
            const auto* explosion = base ? base->As<RE::BGSExplosion>() : nullptr;
            auto& data = a_explosion->GetExplosionRuntimeData();
            const auto* casterSpell = data.magicCaster ? data.magicCaster->currentSpell : nullptr;
            if (!IsFireExplosion(explosion)) {
                (void)MarkProcessed(a_explosion);
                return;
            }
            const auto owner = data.actorOwner.get();
            const auto cause = data.actorCause ? data.actorCause->actor.get() : RE::NiPointer<RE::Actor>{};
            // The caster's object reference (NonActorMagicCaster::GetCasterObjectReference, vfunc 0x0D): checked
            // against the script tags too (a projectile from a tagged source may leave it there)
            RE::Actor* casterActor = nullptr;
            auto* casterRef = data.magicCaster ? data.magicCaster->GetCasterObjectReference(&casterActor) : nullptr;
            // Attribution not set yet (no owner, blame or script tag; the cause is no rule): decided by a
            // later hook call, or given up after kUndecidedLimit
            const bool pendingTag = TagStateOf(a_explosion) == TagState::kPending || TagStateOf(casterRef) == TagState::kPending;
            if (pendingTag) {
                LogDeferredOnce("explosion");  // the ticker judges it; a later hook call decides then
            }
            // A pending tag is not confirmed: such an explosion waits here like an undecided one
            if (!owner && !(data.magicCaster && data.magicCaster->blameActor.get()) && !IsTaggedSource(a_explosion) &&
                !IsTaggedSource(casterRef)) {
                (void)UndecidedTooLong(a_explosion);
                return;
            }
            if (!MarkProcessed(a_explosion)) {
                return;  // another hook already handled this explosion
            }
            const auto link = LinkToPlayer("explosion", a_explosion, owner.get(), data.magicCaster, casterRef);
            const bool byPlayer = link != PlayerLink::kNone;
            LogLinkOnce("explosion", link);
            if (!byPlayer && g_otherLogs.fetch_add(1) < kMaxAttributionLogs) {
                // Shows whether attribution works at all (an explosion with neither field set stays orange)
                logger::info("BlackFlame: fire explosion {:08X} not the player's: owner {:08X}, cause actor {:08X}",
                    explosion->GetFormID(), owner ? owner->GetFormID() : 0, cause ? cause->GetFormID() : 0);
            }
            // Explosion.h:87 magicCaster is the explosion's OWN NonActorMagicCaster (it applies the
            // explosion's effect in its radius), not the player's caster, so it cannot name a spell the
            // player cast later. A whitelist on its currentSpell (MagicCaster.h:105): none, a fire spell
            // (kSpell), or an enchantment - the explosion's own, which cannot be told apart from a weapon
            // enchantment here (README limit). Anything else (scroll, staff, power, potion...) is left.
            using SpellType = RE::MagicSystem::SpellType;
            const bool fireCast = !casterSpell ||
                                  (casterSpell->GetSpellType() == SpellType::kSpell && IsFireSpell(casterSpell)) ||
                                  casterSpell->GetSpellType() == SpellType::kEnchantment;
            const bool notFireCast = !fireCast;
            if (byPlayer && notFireCast) {
                int suppressed = 0;
                if (LogLimit::Allow("explosion skipped", casterSpell->GetFormID(), suppressed)) {
                    logger::info("BlackFlame: explosion {:08X} left: its magicCaster's current spell {:08X} is not a fire "
                                 "spell (type {})", explosion->GetFormID(), casterSpell->GetFormID(),
                        static_cast<int>(casterSpell->GetSpellType()));
                }
            }
            const bool tint = byPlayer && !notFireCast && IsStoked() && GetConfig().light;  // C6: the light only
            // G5: the material logs walk 3D in a task: not without the main-thread queue
            const bool logMaterials = MainQueue::Installed() && !byPlayer && g_npcMaterialLogs.fetch_add(1) < kMaxNpcMaterialLogs;
            if (!tint && !logMaterials && !SelfTestProbe::Recording()) {
                return;
            }

            SelfTestProbe::ExplosionRecord record;
            record.base = SourceOrSelf(explosion)->GetFormID();
            record.copyBase = IsCopy(explosion) ? explosion->GetFormID() : 0;
            record.byPlayer = byPlayer;
            record.owner = owner ? owner->GetFormID() : 0;
            record.causeActor = cause ? cause->GetFormID() : 0;
            record.hasMagicCaster = data.magicCaster != nullptr;
            record.casterSpell = casterSpell ? casterSpell->GetFormID() : 0;
            record.casterRef = casterRef ? casterRef->GetFormID() : 0;
            record.link = PlayerLinkName(link);
            record.lightAtSet3D = data.light != nullptr;
            record.hookThread = REX::W32::GetCurrentThreadId();

            // D1: the hook only records and posts; the recolour runs from the main-thread queue
            if (tint) {
                LogHookThread(g_threadLogs, "Explosion Set3D");
            }
            const auto handle = a_explosion->GetHandle();
            const auto generation = WorldGeneration();
            QueueTask("BlackFlameExplosion",
                [handle, tint, logMaterials, generation, record = std::move(record)]() mutable {
                    if (generation != WorldGeneration()) {
                        return;  // queued before a load
                    }
                    const auto ref = handle.get();
                    if (auto* explosion = ref ? ref->AsExplosion() : nullptr) {
                        (void)Guarded("explosion recolour (task)",
                            [&]() { HandleExplosion(explosion, tint, logMaterials, std::move(record)); });
                    }
                });
        }

        struct Set3DHook
        {
            static void thunk(RE::Explosion* a_this, RE::NiAVObject* a_object, bool a_queue3DTasks)
            {
                func(a_this, a_object, a_queue3DTasks);
                // Entered from the engine's vtable: nothing may throw out of here
                try {
                    if (!a_this) {
                        return;
                    }
                    (void)Guarded("explosion diagnostics (Set3D)", [&]() { Diagnose("Set3D", a_this, a_object != nullptr); });
                    if (a_object) {
                        (void)Guarded("explosion Set3D", [&]() { OnSet3D(a_this); });
                    }
                } catch (...) {
                    if (!g_failureReported.exchange(true)) {
                        logger::error("BlackFlame: explosion hook threw; further failures are not logged");
                    }
                }
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        // A second route in case Set3D is not how an explosion gets its 3D: after Initialize, and on
        // every Update until the 3D exists (cheap checks first; each explosion handled once)
        struct InitializeHook
        {
            static void thunk(RE::Explosion* a_this)
            {
                func(a_this);
                try {
                    if (a_this) {
                        (void)Guarded("explosion Initialize", [&]() {
                            Diagnose("Initialize", a_this, a_this->Get3D() != nullptr);
                            if (a_this->Get3D()) {
                                OnSet3D(a_this);
                            }
                        });
                    }
                } catch (...) {
                    if (!g_failureReported.exchange(true)) {
                        logger::error("BlackFlame: explosion hook threw; further failures are not logged");
                    }
                }
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        // The player's fire explosion while stoked: its impact effects (spawned inside Update) may take
        // the grey copies (ImpactSwap.h)
        bool OpensImpactWindow(RE::Explosion* a_explosion)
        {
            if (!IsStoked() || !GetConfig().impactSwap) {
                return false;
            }
            auto* base = a_explosion->GetBaseObject();
            if (!IsFireExplosion(base ? base->As<RE::BGSExplosion>() : nullptr)) {
                return false;
            }
            auto& data = a_explosion->GetExplosionRuntimeData();
            RE::Actor* casterActor = nullptr;
            auto* casterRef = data.magicCaster ? data.magicCaster->GetCasterObjectReference(&casterActor) : nullptr;
            const auto owner = data.actorOwner.get();
            return LinkToPlayer("explosion impact", a_explosion, owner.get(), data.magicCaster, casterRef) !=
                   PlayerLink::kNone;
        }

        struct UpdateHook
        {
            static void thunk(RE::Explosion* a_this, float a_delta)
            {
                bool open = false;
                bool copyOpen = false;
                try {
                    if (a_this) {
                        (void)Guarded("explosion impact window", [&]() {
                            // v2: an explosion whose base is a copy opens the copy window (its impacts take the impact
                            // copies); the round-10 model window is for explosions without a copy
                            const auto* base = a_this->GetBaseObject();
                            copyOpen = base && IsCopy(base);
                            open = !copyOpen && OpensImpactWindow(a_this);
                        });
                    }
                } catch (...) {
                    open = false;
                    copyOpen = false;
                }
                {
                    std::optional<ThreadTrace::Scope> trace;
                    if (copyOpen) {
                        trace.emplace("Explosion::Update (copy window)");
                        MainQueue::NoteJobSideHook();  // G1
                    }
                    const CopyWindow copyWindow(copyOpen);
                    const ImpactWindow window(open);
                    func(a_this, a_delta);
                }
                try {
                    if (a_this && a_this->Get3D()) {
                        (void)Guarded("explosion Update", [&]() { OnSet3D(a_this, true); });
                    }
                } catch (...) {
                    if (!g_failureReported.exchange(true)) {
                        logger::error("BlackFlame: explosion hook threw; further failures are not logged");
                    }
                }
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };
    }

    void InstallExplosionHook()
    {
        REL::Relocation<std::uintptr_t> vtbl{ RE::Explosion::VTABLE[0] };
        Set3DHook::func = Patch::WriteVfunc("Explosion Set3DHook", vtbl, kSet3DIndex, Set3DHook::thunk);
        InitializeHook::func = Patch::WriteVfunc("Explosion InitializeHook", vtbl, kInitializeIndex, InitializeHook::thunk);
        UpdateHook::func = Patch::WriteVfunc("Explosion UpdateHook", vtbl, kUpdateIndex, UpdateHook::thunk);
        logger::info("BlackFlame: hooked Set3D (0x{:X}), Initialize (0x{:X}) and Update (0x{:X}) on the Explosion vtable",
            kSet3DIndex, kInitializeIndex, kUpdateIndex);
    }

    void ResetExplosionState()
    {
        {
            std::lock_guard lock(g_lightsMutex);
            PendingLights().clear();
        }
        {
            std::lock_guard lock(g_processedMutex);
            Processed().clear();
            FirstSeen().clear();
        }
        g_lightMissing.store(0);
        g_tints.store(0);
    }

    void TickExplosionLights()
    {
        std::vector<RE::ObjectRefHandle> due;
        {
            std::lock_guard lock(g_lightsMutex);
            due.swap(PendingLights());
        }
        if (due.empty()) {
            return;
        }
        if (!IsStoked() || !GetConfig().light) {
            return;  // the buff or the light layer went off meanwhile: dropped
        }
        int tinted = 0;
        for (const auto& handle : due) {
            const auto ref = handle.get();
            auto* explosion = ref ? ref->AsExplosion() : nullptr;
            if (explosion && TintLight(ExplosionLight(explosion))) {
                ++tinted;
            }
        }
        int suppressed = 0;
        if (LogLimit::Allow("explosion light retry", 0, suppressed)) {
            logger::info("BlackFlame: explosion light retry: {} of {} tinted one tick late", tinted, due.size());
        }
    }
}
