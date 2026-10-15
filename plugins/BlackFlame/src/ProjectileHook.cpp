#include "ProjectileHook.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "GameThread.h"
#include "LogLimit.h"
#include "Recolor.h"
#include "SelfTestProbe.h"

#include <atomic>
#include <chrono>
#include <format>

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


        // False when the buff or the layer went off before a deferred recolour ran
        bool RecolorProjectile(RE::Projectile* a_projectile, const char* a_kind, RecolorStats& a_stats)
        {
            if (!IsStoked() || !GetConfig().projectile) {
                return false;
            }
            auto& data = a_projectile->GetProjectileRuntimeData();
            a_stats = RecolorTree(a_projectile->Get3D());
            auto* light = data.light ? data.light->light.get() : nullptr;
            const bool lightTinted = GetConfig().light && TintLight(light);
            const auto spellId = data.spell ? data.spell->GetFormID() : 0;
            int suppressed = 0;
            if (LogLimit::Allow(a_kind, spellId, suppressed)) {
                logger::info("BlackFlame: projectile: spell {:08X} ({}): {}; light {}{}", spellId, a_kind,
                    a_stats.Describe(), !data.light ? "none" : (lightTinted ? "tinted" : "left"),
                    suppressed ? std::format(" ({} similar lines held back)", suppressed) : "");
            }
            return true;
        }

        // Game thread: the material check (a scene-graph walk), the recolour (when due) and,
        // while the self-test runs, its record
        void HandleProjectile(RE::Projectile* a_projectile, const char* a_kind, bool a_recolor, bool a_byPlayer,
            bool a_logMaterials, std::uint32_t a_hookThread, bool a_hookOnGameThread)
        {
            const auto* spell = a_projectile->GetProjectileRuntimeData().spell;
            if (a_logMaterials) {
                logger::info("BlackFlame: material check, {} fire projectile {:08X} ({}) before any recolour{}: {}",
                    a_byPlayer ? "PLAYER" : "NPC", spell ? spell->GetFormID() : 0, a_kind,
                    a_byPlayer ? "" : " (any grey=yes is contamination)", DescribeMaterials(a_projectile->Get3D()));
            }
            const bool probe = SelfTestProbe::Recording();
            SelfTestProbe::ProjectileRecord record;
            if (probe) {
                record.before = SelfTestProbe::Capture(a_projectile->Get3D());
            }
            if (a_recolor) {
                const auto start = std::chrono::steady_clock::now();
                record.recolored = RecolorProjectile(a_projectile, a_kind, record.stats);
                record.recolorMicros = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count();
            }
            if (!probe) {
                return;
            }
            auto& data = a_projectile->GetProjectileRuntimeData();
            const auto shooter = data.shooter.get();
            record.projectile = a_projectile->GetHandle();
            record.shooter = shooter ? shooter->GetFormID() : 0;
            auto* light = data.light ? data.light->light.get() : nullptr;
            record.hasLight = light != nullptr;
            if (light) {
                record.lightDiffuse = light->GetLightRuntimeData().diffuse;
            }
            record.spell = spell ? spell->GetFormID() : 0;
            record.kind = a_kind;
            record.byPlayer = a_byPlayer;
            record.hookThread = a_hookThread;
            record.hookOnGameThread = a_hookOnGameThread;
            record.after = SelfTestProbe::Inspect(a_projectile->Get3D());
            SelfTestProbe::RecordProjectile(std::move(record));
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
            const bool logMaterials = counter.fetch_add(1) < kMaxMaterialLogs;
            const bool recolor = byPlayer && IsStoked() && GetConfig().projectile;
            if (!recolor && !logMaterials && !SelfTestProbe::Recording()) {
                return;
            }

            // Nothing here walks the scene graph: off the game thread it all goes to a task
            const bool onGameThread = IsOnGameThread();
            const auto hookThread = REX::W32::GetCurrentThreadId();
            if (recolor) {
                LogHookThread(g_threadLogs, "Projectile Handle3DLoaded");
            }
            if (onGameThread) {
                HandleProjectile(a_projectile, a_kind, recolor, byPlayer, logMaterials, hookThread, true);
                return;
            }
            const auto handle = a_projectile->GetHandle();
            AddTaskToGameThread("BlackFlameProjectile", [handle, a_kind, recolor, byPlayer, logMaterials, hookThread]() {
                const auto ref = handle.get();
                if (auto* projectile = skyrim_cast<RE::Projectile*>(ref.get())) {
                    HandleProjectile(projectile, a_kind, recolor, byPlayer, logMaterials, hookThread, false);
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
                        OnHandle3DLoaded(a_this, s_kind);
                    }
                } catch (...) {
                    if (!g_failureReported.exchange(true)) {
                        logger::error("BlackFlame: projectile hook threw; further failures are not logged");
                    }
                }
            }

            static void Install(const char* a_name)
            {
                s_kind = a_name;
                REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[0] };
                func = vtbl.write_vfunc(kHandle3DLoadedIndex, thunk);
            }

            static inline REL::Relocation<decltype(thunk)> func;
            static inline const char* s_kind = "";
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
        logger::info("BlackFlame: hooked Handle3DLoaded (0x{:X}) on the missile, flame, cone, beam, grenade and "
                     "barrier projectile vtables", kHandle3DLoadedIndex);
    }
}
