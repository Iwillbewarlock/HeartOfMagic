#include "ProjectileHook.h"
#include "Patch.h"
#include "MainThreadQueue.h"

#include "Attribution.h"
#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "VisualSet.h"
#include "FormCopies.h"
#include "GameThread.h"
#include "LogLimit.h"
#include "PlayerArt.h"
#include "EffectLight.h"
#include "Recolor.h"
#include "ScriptTags.h"
#include "SehGuard.h"
#include "SelfTestProbe.h"

#include <atomic>
#include <chrono>
#include <format>

namespace BlackFlame
{
    namespace
    {
        constexpr std::size_t kHandle3DLoadedIndex = 0xC0;  // RE/P/Projectile.h: SE/AE 0xC0 (VR 0xC1, not built)
        constexpr const char* kArrowKind = "arrow";  // compared by pointer: the hook keeps this very literal
        constexpr int kMaxMaterialLogs = 5;

        std::atomic<bool> g_failureReported{ false };
        std::atomic<int> g_threadLogs{ 0 };
        std::atomic<int> g_playerMaterialLogs{ 0 };
        std::atomic<int> g_npcMaterialLogs{ 0 };


        // The player's fire projectile's light (config light). C6: its 3D is never recoloured any more - the copy
        // (CopySwapLaunch) is grey by construction, a launch without a copy stays orange. Nothing when the buff went
        // off before a deferred run or the light step hit a hardware exception (SehGuard names it)
        void TintProjectileLight(RE::Projectile* a_projectile, const char* a_kind)
        {
            if (!IsStoked() || !GetConfig().light) {
                return;
            }
            bool hasLight = false;
            bool lightTinted = false;
            if (!Guarded("projectile light", [&]() {
                    auto* light = ProjectileLight(a_projectile);
                    hasLight = light != nullptr;
                    lightTinted = TintLight(light);
                })) {
                return;
            }
            const auto* spell = a_projectile->GetProjectileRuntimeData().spell;
            const auto spellId = spell ? spell->GetFormID() : 0;
            int suppressed = 0;
            if (LogLimit::Allow(a_kind, spellId, suppressed)) {
                logger::info("BlackFlame: projectile: spell {:08X} ({}): light {}{}", spellId, a_kind,
                    !hasLight ? "none" : (lightTinted ? "tinted" : "left"),
                    suppressed ? std::format(" ({} similar lines held back)", suppressed) : "");
            }
        }

        // Game thread: the material check (a scene-graph walk), the light tint (when due) and,
        // while the self-test runs, its record
        // v2i: one line the first time a copy projectile's 3D is seen: the copies' grey rule on its effect shapes and the
        // first materials (the material check above stops after kMaxMaterialLogs projectiles of any kind)
        void LogFirstCopy3D(RE::Projectile* a_projectile, const char* a_kind, bool a_byPlayer)
        {
            const auto* base = a_projectile->GetBaseObject();
            if (!base || !IsCopy(base) || !LogLimit::FirstTime("copy 3D", base->GetFormID())) {
                return;
            }
            const auto* spell = a_projectile->GetProjectileRuntimeData().spell;
            auto* root = a_projectile->Get3D();
            const auto seen = SelfTestProbe::Inspect(root);
            logger::info("BlackFlame: first 3D of copy projectile {:08X} for {:08X} (spell {:08X}, {}, {}): {}; copies' rule grey "
                         "on {} of {} effect shapes ({} palette shaders, {} with our palette, {} of them the additive one); {}",
                base->GetFormID(), SourceOrSelf(base)->GetFormID(), spell ? spell->GetFormID() : 0, a_kind,
                a_byPlayer ? "player" : "not the player", root ? "3D present" : "NO 3D", seen.greyByRule, seen.effectProperties,
                seen.paletteShaders, seen.withOurPalette, seen.withAddPalette, DescribeMaterials(root));
        }

        void HandleProjectile(RE::Projectile* a_projectile, const char* a_kind, bool a_tint, bool a_byPlayer,
            const char* a_link,
            bool a_logMaterials, std::uint32_t a_hookThread, bool a_hookOnGameThread, bool a_firstCopy3D = false)
        {
            const auto* spell = a_projectile->GetProjectileRuntimeData().spell;
            if (a_firstCopy3D) {
                (void)Guarded("first copy 3D line", [&]() { LogFirstCopy3D(a_projectile, a_kind, a_byPlayer); });
            }
            if (a_logMaterials && !Guarded("projectile material check", [&]() {
                    logger::info("BlackFlame: material check, {} fire projectile {:08X} ({}) before any recolour{}: {}",
                        a_byPlayer ? "PLAYER" : "NPC", spell ? spell->GetFormID() : 0, a_kind,
                        a_byPlayer ? "" : " (any grey=yes is contamination)", DescribeMaterials(a_projectile->Get3D()));
                })) {
                return;
            }
            const bool probe = SelfTestProbe::Recording();
            SelfTestProbe::ProjectileRecord record;
            if (probe) {
                record.before = SelfTestProbe::Capture(a_projectile->Get3D());
            }
            if (a_tint) {
                TintProjectileLight(a_projectile, a_kind);
            }
            if (!probe) {
                return;
            }
            auto& data = a_projectile->GetProjectileRuntimeData();
            const auto shooter = data.shooter.get();
            record.projectile = a_projectile->GetHandle();
            record.shooter = shooter ? shooter->GetFormID() : 0;
            RE::NiLight* light = nullptr;
            (void)Guarded("projectile light (self-test)", [&]() { light = ProjectileLight(a_projectile); });
            record.hasLight = light != nullptr;
            if (light) {
                record.lightDiffuse = light->GetLightRuntimeData().diffuse;
            }
            record.spell = spell ? spell->GetFormID() : 0;
            if (const auto* base = a_projectile->GetBaseObject()) {
                record.base = SourceOrSelf(base)->GetFormID();
                record.copyBase = IsCopy(base) ? base->GetFormID() : 0;
            }
            record.kind = a_kind;
            record.byPlayer = a_byPlayer;
            record.link = a_link;
            record.hookThread = a_hookThread;
            record.hookOnGameThread = a_hookOnGameThread;
            auto* root = a_projectile->Get3D();
            record.after = SelfTestProbe::Inspect(root);
            record.inspected3D = root != nullptr;
            record.valueNodes = SelfTestProbe::ValueNodeValues(root);
            SelfTestProbe::RecordProjectile(std::move(record));
        }

        // Only a pending script tag names this projectile or its shooter: a task judges it (never this
        // hook) and decides the projectile then
        void DeferForPendingTag(RE::Projectile* a_projectile, const char* a_kind)
        {
            LogDeferredOnce("projectile");
            const auto handle = a_projectile->GetHandle();
            const auto generation = WorldGeneration();
            const auto hookThread = REX::W32::GetCurrentThreadId();
            QueueTask("BlackFlameProjectileTag", [handle, a_kind, generation, hookThread]() {
                if (generation != WorldGeneration()) {
                    return;
                }
                const auto ref = handle.get();
                auto* projectile = skyrim_cast<RE::Projectile*>(ref.get());
                if (!projectile) {
                    return;
                }
                const auto shooter = projectile->GetProjectileRuntimeData().shooter.get();
                (void)ResolvePending(projectile);
                (void)ResolvePending(shooter.get());
                const auto link = LinkToPlayer("projectile", projectile, shooter.get(), NonActorCasterOf(shooter.get()));
                const bool byPlayer = link != PlayerLink::kNone;
                const bool tint = byPlayer && IsStoked() && GetConfig().light;
                LogLinkOnce("projectile", link);
                // Not the player's after all: the normal path still runs for the records and logs
                if (tint || SelfTestProbe::Recording()) {
                    HandleProjectile(projectile, a_kind, tint, byPlayer, PlayerLinkName(link), false, hookThread, false);
                }
            });
        }

        void OnHandle3DLoaded(RE::Projectile* a_projectile, const char* a_kind)
        {
            auto& data = a_projectile->GetProjectileRuntimeData();
            // A fire spell's projectile, or an arrow of the player's bound fire weapon (PlayerArt.h)
            const bool arrow = a_kind == kArrowKind;
            if (!IsVisualSpell(data.spell) && !(arrow && IsBoundFireArrow(a_projectile))) {
                return;
            }
            const auto shooter = data.shooter.get();
            const auto link = LinkToPlayer("projectile", a_projectile, shooter.get(), NonActorCasterOf(shooter.get()));
            if (link == PlayerLink::kNone && (TagStateOf(a_projectile) == TagState::kPending ||
                                                 TagStateOf(shooter.get()) == TagState::kPending)) {
                DeferForPendingTag(a_projectile, a_kind);
                return;
            }
            const bool byPlayer = link != PlayerLink::kNone;
            LogLinkOnce("projectile", link);
            const char* linkName = PlayerLinkName(link);

            auto& counter = byPlayer ? g_playerMaterialLogs : g_npcMaterialLogs;
            const bool logMaterials = MainQueue::Installed() && counter.fetch_add(1) < kMaxMaterialLogs;  // G5
            const bool tint = byPlayer && IsStoked() && GetConfig().light;
            // v2i: a copy's first 3D gets its line (marked in the task: a task dropped by a load leaves it for the next one)
            const auto* base = a_projectile->GetBaseObject();
            const bool firstCopy3D = MainQueue::Installed() && base && IsCopy(base) && LogLimit::NotYet("copy 3D", base->GetFormID());
            if (!tint && !logMaterials && !firstCopy3D && !SelfTestProbe::Recording()) {
                return;
            }

            // D1: the hook only records and posts; the 3D walk and the light tint run from the main-thread queue
            const bool onGameThread = IsOnGameThread();
            const auto hookThread = REX::W32::GetCurrentThreadId();
            if (tint) {
                LogHookThread(g_threadLogs, "Projectile Handle3DLoaded");
            }
            const auto handle = a_projectile->GetHandle();
            const auto generation = WorldGeneration();
            QueueTask("BlackFlameProjectile",
                [handle, a_kind, tint, byPlayer, linkName, logMaterials, hookThread, onGameThread, generation, firstCopy3D]() {
                if (generation != WorldGeneration()) {
                    return;  // queued before a load
                }
                const auto ref = handle.get();
                if (auto* projectile = skyrim_cast<RE::Projectile*>(ref.get())) {
                    HandleProjectile(projectile, a_kind, tint, byPlayer, linkName, logMaterials, hookThread, onGameThread,
                        firstCopy3D);
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
                func = Patch::WriteVfunc("projectile vtable Handle3DLoaded", vtbl, kHandle3DLoadedIndex, thunk);
            }

            static inline REL::Relocation<decltype(thunk)> func;
            static inline const char* s_kind = "";
        };
    }

    // The projectile's light. PROJECTILE_RUNTIME_DATA declares NiPointer<BSLight> (Projectile.h:256,
    // "smart ptr"), but nothing in the headers proves the object there is a BSLight on AE 1.6.1170 -
    // an explosion keeps an NiPointLight in the same role (Explosion.h:80). So its real type is
    // asked through MSVC RTTI (skyrim_cast, a dynamic cast from the object's own type info): a
    // BSLight gives its NiLight, an NiLight is used as is, anything else no light. Logged once.
    RE::NiLight* ProjectileLight(RE::Projectile* a_projectile)
    {
        return CheckedLight(a_projectile->GetProjectileRuntimeData().light.get(), "projectile");
    }

    void InstallProjectileHooks()
    {
        Handle3DLoadedHook<RE::MissileProjectile>::Install("missile");
        Handle3DLoadedHook<RE::FlameProjectile>::Install("flame");
        Handle3DLoadedHook<RE::ConeProjectile>::Install("cone");
        Handle3DLoadedHook<RE::BeamProjectile>::Install("beam");
        Handle3DLoadedHook<RE::GrenadeProjectile>::Install("grenade");
        Handle3DLoadedHook<RE::BarrierProjectile>::Install("barrier");
        Handle3DLoadedHook<RE::ArrowProjectile>::Install(kArrowKind);
        logger::info("BlackFlame: hooked Handle3DLoaded (0x{:X}) on the missile, flame, cone, beam, grenade and "
                     "barrier projectile vtables, and the arrow one (bound fire weapons' arrows)", kHandle3DLoadedIndex);
    }
}
