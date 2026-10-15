#include "ExplosionHook.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "GameThread.h"
#include "Recolor.h"

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

        void RecolorExplosion(RE::Explosion* a_explosion)
        {
            if (!IsStoked() || !GetConfig().explosion) {
                return;
            }
            auto& data = a_explosion->GetExplosionRuntimeData();
            const auto stats = RecolorTree(a_explosion->Get3D());
            const bool lightTinted = GetConfig().light && TintLight(data.light.get());
            const auto* base = a_explosion->GetBaseObject();
            logger::info("BlackFlame: explosion: {:08X}: {}; light {}", base ? base->GetFormID() : 0, stats.Describe(),
                !data.light ? "none (not created yet?)" : (lightTinted ? "tinted" : "left"));
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
            if (!byPlayer) {
                // Shows whether attribution works at all (an explosion with neither field set stays orange)
                if (g_otherLogs.fetch_add(1) < kMaxAttributionLogs) {
                    logger::info("BlackFlame: fire explosion {:08X} not the player's: owner {:08X}, cause actor {:08X}",
                        explosion->GetFormID(), owner ? owner->GetFormID() : 0, cause ? cause->GetFormID() : 0);
                }
                return;
            }
            if (!IsStoked() || !GetConfig().explosion) {
                return;
            }

            const bool onGameThread = IsOnGameThread();
            LogHookThread(g_threadLogs, "Explosion Set3D");
            if (onGameThread) {
                RecolorExplosion(a_explosion);
                return;
            }
            const auto handle = a_explosion->GetHandle();
            AddTaskToGameThread("BlackFlameExplosion", [handle]() {
                const auto ref = handle.get();
                if (auto* explosion = ref ? ref->AsExplosion() : nullptr) {
                    RecolorExplosion(explosion);
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
