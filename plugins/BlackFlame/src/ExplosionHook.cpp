#include "ExplosionHook.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "GameThread.h"
#include "LogLimit.h"
#include "Recolor.h"
#include "SehGuard.h"
#include "SelfTestProbe.h"

#include <atomic>
#include <format>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace BlackFlame
{
    namespace
    {
        constexpr std::size_t kSet3DIndex = 0x6C;  // RE/T/TESObjectREFR.h: Set3D, slot 6C (Explosion does not override it)
        constexpr int kMaxAttributionLogs = 5;
        constexpr int kMaxNpcMaterialLogs = 5;

        std::atomic<bool> g_failureReported{ false };
        std::atomic<int> g_threadLogs{ 0 };
        std::atomic<int> g_otherLogs{ 0 };
        std::atomic<int> g_npcMaterialLogs{ 0 };
        std::atomic<int> g_recolors{ 0 };
        std::atomic<int> g_lightMissing{ 0 };
        std::atomic<int> g_diagnostics{ 0 };
        constexpr int kMaxDiagnostics = 20;
        constexpr std::size_t kInitializeIndex = 0xA2;  // RE/E/Explosion.h: Initialize, SE/AE 0xA2
        constexpr std::size_t kUpdateIndex = 0xA3;      // RE/E/Explosion.h: Update(float), SE/AE 0xA3

        // The explosion's light through the RTTI check (the same declared-type trap as the projectile's
        // light); its real type is logged once
        RE::NiLight* ExplosionLight(RE::Explosion* a_explosion)
        {
            return CheckedLight(a_explosion->GetExplosionRuntimeData().light.get(), "explosion");
        }

        // Explosions to tint once more next tick, under g_lightsMutex (tasks and the message handler
        // reach it). Heap-held and never freed like the other layers' lists.
        std::mutex g_lightsMutex;

        std::vector<RE::ObjectRefHandle>& PendingLights()
        {
            static auto* lights = std::make_unique<std::vector<RE::ObjectRefHandle>>().release();
            return *lights;
        }

        // False when the buff or the layer went off before a deferred recolour ran
        bool RecolorExplosion(RE::Explosion* a_explosion, RecolorStats& a_stats)
        {
            if (!IsStoked() || !GetConfig().explosion) {
                return false;
            }
            auto& data = a_explosion->GetExplosionRuntimeData();
            a_stats = RecolorTree(a_explosion->Get3D());
            const int recolors = g_recolors.fetch_add(1) + 1;
            bool lightTinted = false;
            bool lightQueued = false;
            if (GetConfig().light) {
                lightTinted = TintLight(ExplosionLight(a_explosion));
                if (!data.light) {
                    // Not created yet at Set3D: one more try on the next ticker tick
                    std::lock_guard lock(g_lightsMutex);
                    PendingLights().push_back(a_explosion->GetHandle());
                    g_lightMissing.fetch_add(1);
                    lightQueued = true;
                }
            }
            const auto* base = a_explosion->GetBaseObject();
            const auto baseId = base ? base->GetFormID() : 0;
            int suppressed = 0;
            if (LogLimit::Allow("explosion", baseId, suppressed)) {
                logger::info("BlackFlame: explosion: {:08X}: {}; light {} (missing at recolour {} of {} times){}", baseId,
                    a_stats.Describe(),
                    lightQueued ? "not there yet, retried next tick" : (!data.light ? "none" : (lightTinted ? "tinted" : "left")),
                    g_lightMissing.load(), recolors,
                    suppressed ? std::format(" ({} similar lines held back)", suppressed) : "");
            }
            return true;
        }

        // Game thread: the recolour (when due), the NPC contamination check and, while the
        // self-test runs, its record (a_record carries what Set3D saw)
        void HandleExplosion(RE::Explosion* a_explosion, bool a_recolor, bool a_logMaterials,
            SelfTestProbe::ExplosionRecord a_record)
        {
            if (a_logMaterials) {
                logger::info("BlackFlame: material check, NPC fire explosion {:08X} (any grey=yes is contamination): {}",
                    a_record.base, DescribeMaterials(a_explosion->Get3D()));
            }
            if (a_recolor) {
                a_record.recolored = RecolorExplosion(a_explosion, a_record.stats);
            }
            if (SelfTestProbe::Recording()) {
                a_record.after = SelfTestProbe::Inspect(a_explosion->Get3D());
                SelfTestProbe::RecordExplosion(std::move(a_record));
            }
        }

        // Every explosion is handled once, whichever hook sees its 3D first (Set3D, Initialize or Update).
        // Keyed by its reference handle, whose age bits differ when an address or slot is reused.
        bool MarkProcessed(RE::Explosion* a_explosion)
        {
            static std::mutex mutex;
            static auto* processed = std::make_unique<std::unordered_set<std::uint32_t>>().release();
            constexpr std::size_t kMaxProcessed = 512;  // explosions live about a second: start over past this
            std::lock_guard lock(mutex);
            if (processed->size() > kMaxProcessed) {
                processed->clear();
            }
            return processed->insert(a_explosion->GetHandle().native_handle()).second;
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
                         "{:08X}, magicCaster {} currentSpell {:08X}, 3D {}, thread {}",
                a_hook, explosion ? explosion->GetFormID() : 0, IsFireExplosion(explosion, casterSpell) ? "yes" : "no",
                owner ? owner->GetFormID() : 0, cause ? cause->GetFormID() : 0, data.magicCaster ? "set" : "null",
                casterSpell ? casterSpell->GetFormID() : 0, a_has3D ? "present" : "null", REX::W32::GetCurrentThreadId());
        }

        void OnSet3D(RE::Explosion* a_explosion)
        {
            if (!MarkProcessed(a_explosion)) {
                return;  // another hook already handled this explosion
            }
            auto* base = a_explosion->GetBaseObject();
            const auto* explosion = base ? base->As<RE::BGSExplosion>() : nullptr;
            auto& data = a_explosion->GetExplosionRuntimeData();
            const auto* casterSpell = data.magicCaster ? data.magicCaster->currentSpell : nullptr;
            if (!IsFireExplosion(explosion, casterSpell)) {
                return;
            }
            const auto owner = data.actorOwner.get();
            const auto cause = data.actorCause ? data.actorCause->actor.get() : RE::NiPointer<RE::Actor>{};
            auto* player = RE::PlayerCharacter::GetSingleton();
            const bool byPlayer = player && (owner.get() == player || cause.get() == player);
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
            const bool recolor = byPlayer && !notFireCast && IsStoked() && GetConfig().explosion;
            const bool logMaterials = !byPlayer && g_npcMaterialLogs.fetch_add(1) < kMaxNpcMaterialLogs;
            if (!recolor && !logMaterials && !SelfTestProbe::Recording()) {
                return;
            }

            SelfTestProbe::ExplosionRecord record;
            record.base = explosion->GetFormID();
            record.byPlayer = byPlayer;
            record.owner = owner ? owner->GetFormID() : 0;
            record.causeActor = cause ? cause->GetFormID() : 0;
            record.hasMagicCaster = data.magicCaster != nullptr;
            record.casterSpell = casterSpell ? casterSpell->GetFormID() : 0;
            record.lightAtSet3D = data.light != nullptr;
            record.hookThread = REX::W32::GetCurrentThreadId();

            const bool onGameThread = IsOnGameThread();
            if (recolor) {
                LogHookThread(g_threadLogs, "Explosion Set3D");
            }
            if (onGameThread) {
                HandleExplosion(a_explosion, recolor, logMaterials, std::move(record));
                return;
            }
            const auto handle = a_explosion->GetHandle();
            const auto generation = WorldGeneration();
            QueueTask("BlackFlameExplosion",
                [handle, recolor, logMaterials, generation, record = std::move(record)]() mutable {
                    if (generation != WorldGeneration()) {
                        return;  // queued before a load
                    }
                    const auto ref = handle.get();
                    if (auto* explosion = ref ? ref->AsExplosion() : nullptr) {
                        (void)Guarded("explosion recolour (task)",
                            [&]() { HandleExplosion(explosion, recolor, logMaterials, std::move(record)); });
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

        struct UpdateHook
        {
            static void thunk(RE::Explosion* a_this, float a_delta)
            {
                func(a_this, a_delta);
                try {
                    if (a_this && a_this->Get3D()) {
                        (void)Guarded("explosion Update", [&]() { OnSet3D(a_this); });
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
        Set3DHook::func = vtbl.write_vfunc(kSet3DIndex, Set3DHook::thunk);
        InitializeHook::func = vtbl.write_vfunc(kInitializeIndex, InitializeHook::thunk);
        UpdateHook::func = vtbl.write_vfunc(kUpdateIndex, UpdateHook::thunk);
        logger::info("BlackFlame: hooked Set3D (0x{:X}), Initialize (0x{:X}) and Update (0x{:X}) on the Explosion vtable",
            kSet3DIndex, kInitializeIndex, kUpdateIndex);
    }

    void ResetExplosionState()
    {
        {
            std::lock_guard lock(g_lightsMutex);
            PendingLights().clear();
        }
        g_lightMissing.store(0);
        g_recolors.store(0);
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
