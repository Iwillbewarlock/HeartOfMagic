#include "ProjectileHook.h"

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
        constexpr std::size_t kHandle3DLoadedIndex = 0xC0;  // RE/P/Projectile.h: SE/AE 0xC0 (VR 0xC1, not built)
        constexpr int kMaxMaterialLogs = 5;

        std::atomic<bool> g_failureReported{ false };
        std::atomic<int> g_threadLogs{ 0 };
        std::atomic<int> g_playerMaterialLogs{ 0 };
        std::atomic<int> g_npcMaterialLogs{ 0 };

        void RecolorProjectile(RE::Projectile* a_projectile, const char* a_kind)
        {
            if (!IsStoked() || !GetConfig().projectile) {
                return;
            }
            auto& data = a_projectile->GetProjectileRuntimeData();
            const auto stats = RecolorTree(a_projectile->Get3D());
            auto* light = data.light ? data.light->light.get() : nullptr;
            const bool lightTinted = GetConfig().light && TintLight(light);
            logger::info("BlackFlame: projectile: spell {:08X} ({}): {}; light {}",
                data.spell ? data.spell->GetFormID() : 0, a_kind, stats.Describe(),
                !data.light ? "none" : (lightTinted ? "tinted" : "left"));
        }

        void OnHandle3DLoaded(RE::Projectile* a_projectile, const char* a_kind)
        {
            auto& data = a_projectile->GetProjectileRuntimeData();
            if (!IsFireSpell(data.spell)) {
                return;
            }
            const auto shooter = data.shooter.get();
            const bool byPlayer = shooter && shooter.get() == RE::PlayerCharacter::GetSingleton();

            auto& counter = byPlayer ? g_playerMaterialLogs : g_npcMaterialLogs;
            if (counter.fetch_add(1) < kMaxMaterialLogs) {
                logger::info("BlackFlame: material check, {} fire projectile {:08X} ({}) before any recolour: {}",
                    byPlayer ? "PLAYER" : "NPC", data.spell->GetFormID(), a_kind, DescribeMaterials(a_projectile->Get3D()));
            }
            if (!byPlayer || !IsStoked() || !GetConfig().projectile) {
                return;
            }

            const bool onGameThread = IsOnGameThread();
            LogHookThread(g_threadLogs, "Projectile Handle3DLoaded");
            if (onGameThread) {
                RecolorProjectile(a_projectile, a_kind);
                return;
            }
            const auto handle = a_projectile->GetHandle();
            AddTaskToGameThread("BlackFlameProjectile", [handle, a_kind]() {
                const auto ref = handle.get();
                if (auto* projectile = skyrim_cast<RE::Projectile*>(ref.get())) {
                    RecolorProjectile(projectile, a_kind);
                }
            });
        }

        // One struct per projectile class: each vtable keeps its own original
        template <class T>
        struct Handle3DLoadedHook
        {
            static void thunk(T* a_this)
            {
                func(a_this);
                // Entered from the engine's vtable: nothing may throw out of here
                try {
                    if (a_this) {
                        OnHandle3DLoaded(a_this, m_kind);
                    }
                } catch (...) {
                    if (!g_failureReported.exchange(true)) {
                        logger::error("BlackFlame: projectile hook threw; further failures are not logged");
                    }
                }
            }

            static void Install(const char* a_name)
            {
                m_kind = a_name;
                REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[0] };
                func = vtbl.write_vfunc(kHandle3DLoadedIndex, thunk);
            }

            static inline REL::Relocation<decltype(thunk)> func;
            static inline const char* m_kind = "";
        };
    }

    void InstallProjectileHooks()
    {
        Handle3DLoadedHook<RE::MissileProjectile>::Install("missile");
        Handle3DLoadedHook<RE::FlameProjectile>::Install("flame");
        Handle3DLoadedHook<RE::ConeProjectile>::Install("cone");
        Handle3DLoadedHook<RE::BeamProjectile>::Install("beam");
        Handle3DLoadedHook<RE::GrenadeProjectile>::Install("grenade");
        Handle3DLoadedHook<RE::BarrierProjectile>::Install("barrier");
        logger::info("BlackFlame: hooked Handle3DLoaded (0x{:X}) on 6 projectile vtables", kHandle3DLoadedIndex);
    }
}
