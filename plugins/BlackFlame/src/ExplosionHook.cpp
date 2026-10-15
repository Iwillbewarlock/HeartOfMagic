#include "ExplosionHook.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "GameThread.h"
#include "Recolor.h"
#include "SelfTestProbe.h"

#include <atomic>

namespace BlackFlame
{
    namespace
    {
        constexpr std::size_t kSet3DIndex = 0x6C;  // RE/T/TESObjectREFR.h: Set3D, slot 6C (Explosion does not override it)
        constexpr int kMaxAttributionLogs = 5;

        std::atomic<bool> g_failureReported{ false };
        std::atomic<int> g_threadLogs{ 0 };
        std::atomic<int> g_otherLogs{ 0 };

        // False when the buff or the layer went off before a deferred recolour ran
        bool RecolorExplosion(RE::Explosion* a_explosion, RecolorStats& a_stats)
        {
            if (!IsStoked() || !GetConfig().explosion) {
                return false;
            }
            auto& data = a_explosion->GetExplosionRuntimeData();
            a_stats = RecolorTree(a_explosion->Get3D());
            const bool lightTinted = GetConfig().light && TintLight(data.light.get());
            const auto* base = a_explosion->GetBaseObject();
            logger::info("BlackFlame: explosion: {:08X}: {}; light {}", base ? base->GetFormID() : 0, a_stats.Describe(),
                !data.light ? "none (not created yet?)" : (lightTinted ? "tinted" : "left"));
            return true;
        }

        // Game thread: the recolour (when due) and, while the self-test runs, its record
        // (a_record carries what Set3D saw: owner, cause, caster spell, light)
        void HandleExplosion(RE::Explosion* a_explosion, bool a_recolor, SelfTestProbe::ExplosionRecord a_record)
        {
            if (a_recolor) {
                a_record.recolored = RecolorExplosion(a_explosion, a_record.stats);
            }
            if (SelfTestProbe::Recording()) {
                a_record.after = SelfTestProbe::Inspect(a_explosion->Get3D());
                SelfTestProbe::RecordExplosion(std::move(a_record));
            }
        }

        void OnSet3D(RE::Explosion* a_explosion)
        {
            auto* base = a_explosion->GetBaseObject();
            const auto* explosion = base ? base->As<RE::BGSExplosion>() : nullptr;
            if (!IsFireExplosion(explosion)) {
                return;
            }
            auto& data = a_explosion->GetExplosionRuntimeData();
            const auto owner = data.actorOwner.get();
            const auto cause = data.actorCause ? data.actorCause->actor.get() : RE::NiPointer<RE::Actor>{};
            auto* player = RE::PlayerCharacter::GetSingleton();
            const bool byPlayer = player && (owner.get() == player || cause.get() == player);
            if (!byPlayer && g_otherLogs.fetch_add(1) < kMaxAttributionLogs) {
                // Shows whether attribution works at all (an explosion with neither field set stays orange)
                logger::info("BlackFlame: fire explosion {:08X} not the player's: owner {:08X}, cause actor {:08X}",
                    explosion->GetFormID(), owner ? owner->GetFormID() : 0, cause ? cause->GetFormID() : 0);
            }
            const bool recolor = byPlayer && IsStoked() && GetConfig().explosion;
            if (!recolor && !SelfTestProbe::Recording()) {
                return;
            }

            SelfTestProbe::ExplosionRecord record;
            record.base = explosion->GetFormID();
            record.byPlayer = byPlayer;
            record.owner = owner ? owner->GetFormID() : 0;
            record.causeActor = cause ? cause->GetFormID() : 0;
            record.hasMagicCaster = data.magicCaster != nullptr;
            record.casterSpell = data.magicCaster && data.magicCaster->currentSpell ?
                                     data.magicCaster->currentSpell->GetFormID() : 0;
            record.lightAtSet3D = data.light != nullptr;
            record.hookThread = REX::W32::GetCurrentThreadId();

            const bool onGameThread = IsOnGameThread();
            if (recolor) {
                LogHookThread(g_threadLogs, "Explosion Set3D");
            }
            if (onGameThread) {
                HandleExplosion(a_explosion, recolor, std::move(record));
                return;
            }
            const auto handle = a_explosion->GetHandle();
            AddTaskToGameThread("BlackFlameExplosion", [handle, recolor, record = std::move(record)]() mutable {
                const auto ref = handle.get();
                if (auto* explosion = ref ? ref->AsExplosion() : nullptr) {
                    HandleExplosion(explosion, recolor, std::move(record));
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
                    if (a_this && a_object) {
                        OnSet3D(a_this);
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
        logger::info("BlackFlame: hooked Set3D (0x{:X}) on the Explosion vtable", kSet3DIndex);
    }
}
