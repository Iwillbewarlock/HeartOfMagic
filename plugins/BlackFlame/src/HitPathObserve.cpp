#include "HitPathObserve.h"

#include "BuffState.h"
#include "CorpseGrey.h"
#include "CorpseMark.h"
#include "FireSet.h"
#include "HitEffectHook.h"
#include "HitPathAddress.h"
#include "HitPathAttribution.h"
#include "Patch.h"
#include "SehGuard.h"
#include "SreProbe.h"

#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#ifdef GetObject
#    undef GetObject
#endif

namespace BlackFlame::HitPath
{
    namespace
    {
        // Address Library ids of 1.6.1170 (ids.txt of the decrypted image, 2026-10-19)
        constexpr std::uint64_t kUpdateId = 34092;       // ActiveEffect::Update
        constexpr std::uint64_t kStartVisualsId = 34094;  // asks 0x0C, 0x0D, the gate, SetTarget, Start
        constexpr std::uint64_t kApplyShaderId = 19872;   // TESObjectREFR::ApplyEffectShader
        constexpr std::uint64_t kShaderEffectCtorId = 34910;  // ShaderReferenceEffect constructor
        constexpr std::uint64_t kOwnedVtableId = 190287;      // VTABLE_OwnedController (AE)
        constexpr std::uint64_t kCameraVtableId = 190289;     // VTABLE_OwnedCameraEffectController (AE)

        struct Watched
        {
            std::uint64_t id;
            const char* name;
        };
        constexpr std::array kWatched{
            Watched{ 40508, "the gate 34094 asks" },
            Watched{ kUpdateId, "ActiveEffect::Update" },
            Watched{ kStartVisualsId, "effect hit visuals start" },
            Watched{ 34761, "ReferenceEffectController::Start" },
            Watched{ kShaderEffectCtorId, "ShaderReferenceEffect ctor" },
            Watched{ kApplyShaderId, "TESObjectREFR::ApplyEffectShader" },
            Watched{ 34833, "SetTarget (34833)" },
            Watched{ 34154, "34154" },
        };

        // Call sites, as offsets into their functions (1.6.1170: 34092 at 5AE2B0, 19872 at 2F0E20)
        constexpr std::uint32_t kSiteAOffset = 0x5AE4EE - 0x5AE2B0;      // +0x23E
        constexpr std::uint32_t kSiteBOffset = 0x5AE77D - 0x5AE2B0;      // +0x4CD
        constexpr std::uint32_t kApplySiteOffset = 0x2F0FCC - 0x2F0E20;  // +0x1AC

        constexpr std::uint32_t kNoHitEffectBit = 1u << 27;  // EffectSettingData::Flag::kNoHitEffect (34092 at 5AE4B8)
        constexpr int kMaxStartLines = 40;       // sites A and B together, per stoke window
        constexpr int kMaxApplyLines = 40;       // ApplyEffectShader with a fire MGEF's shader or on the player
        constexpr int kMaxApplyOtherLines = 20;  // every other ApplyEffectShader call (Papyrus ambience), a cap of its own
        constexpr std::size_t kBytesPerCall = 14;  // an absolute jump in the trampoline per write_call<5>

        enum Site : std::size_t
        {
            kSiteA,
            kSiteB,
            kStartSiteCount
        };
        constexpr std::array<char, kStartSiteCount> kSiteNames{ 'A', 'B' };

        struct StartCounts
        {
            std::atomic<std::uint64_t> calls{ 0 }, playerShader{ 0 }, fire{ 0 }, noHitEffect{ 0 };
        };

        struct ApplyCounts
        {
            std::atomic<std::uint64_t> calls{ 0 }, owned{ 0 }, fireShader{ 0 }, onPlayer{ 0 }, camera{ 0 }, other{ 0 },
                null{ 0 };
        };

        // Window counts: zeroed when a stoke window starts (ResetWindow)
        std::array<StartCounts, kStartSiteCount> g_start;
        ApplyCounts g_apply;
        std::atomic<int> g_startLines{ 0 }, g_applyLines{ 0 }, g_applyOtherLines{ 0 };

        // Written once at kDataLoaded before any write_call; read-only afterwards
        std::array<bool, kStartSiteCount> g_startHooked{};
        bool g_applyHooked = false;
        std::uintptr_t g_ownedVtable = 0;
        std::uintptr_t g_cameraVtable = 0;
        std::uintptr_t g_effectControllerVtable = 0;
        std::atomic<bool> g_failureReported{ false };

        void ReportFailure()
        {
            if (!g_failureReported.exchange(true)) {
                logger::error("BlackFlame: hit path observation threw; the original was called; further failures are not logged");
            }
        }

        bool TakeLine(std::atomic<int>& a_counter, int a_cap, int& a_line)
        {
            if (a_counter.load(std::memory_order_relaxed) >= a_cap) {
                return false;
            }
            a_line = a_counter.fetch_add(1) + 1;
            return a_line <= a_cap;
        }

        void ReportForeignHooks()
        {
            for (const auto& watched : kWatched) {
                if (!GuardedKeep("hit path foreign hook report", [&]() { Detail::ReportJumpChain(watched.id, watched.name); })) {
                    logger::warn("BlackFlame: hit path foreign hooks: {} {}: the read faulted - skipped", watched.id, watched.name);
                }
            }
        }


        std::uint32_t PlayerHandle()
        {
            return PlayerHandleNative();
        }

        RE::FormID FormIdOfHandle(std::uint32_t a_handle)
        {
            if (a_handle == 0) {
                return 0;
            }
            const auto ref = RE::TESObjectREFR::LookupByHandle(a_handle);
            return ref ? ref->GetFormID() : 0;
        }


        void LogStart(Site a_site, int a_line, RE::ActiveEffect* a_effect, const RE::EffectSetting* a_base, bool a_casterPlayer,
            bool a_targetPlayer, bool a_fire, std::uint32_t a_flagsBefore)
        {
            const auto mgefFlags = a_base->data.flags.underlying();
            const auto* spell = a_effect->spell;
            const auto caster = a_effect->caster.get();
            auto* targetRef = a_effect->target ? a_effect->target->GetTargetStatsObject() : nullptr;
            const auto controllerVtable = *reinterpret_cast<const std::uintptr_t*>(&a_effect->hitEffectController);
            const std::string vtable = controllerVtable == g_effectControllerVtable ?
                                           std::string("ActiveEffectReferenceEffectController") :
                                           std::format("0x{:X} (not ActiveEffectReferenceEffectController)", controllerVtable);
            logger::info("BlackFlame: hit path effect start [{} {}]: MGEF {:08X} '{}' (MGEF flags 0x{:08X}, kNoHitEffect {}, "
                         "effectShader {:08X}, fire effect of a fire spell {}), spell {:08X} '{}', caster {} {:08X}, target {:08X}{}, "
                         "AE flags 0x{:08X} -> 0x{:08X}, controller vtable {}, stoked {}, thread {}",
                kSiteNames[a_site], a_line, a_base->GetFormID(), Detail::EditorIdOf(a_base), mgefFlags,
                (mgefFlags & kNoHitEffectBit) ? "set" : "clear", a_base->data.effectShader->GetFormID(), a_fire ? "yes" : "no",
                spell ? spell->GetFormID() : 0, spell ? spell->GetName() : "", a_casterPlayer ? "player" : "other",
                caster ? caster->GetFormID() : 0, targetRef ? targetRef->GetFormID() : 0, a_targetPlayer ? " (player)" : "",
                a_flagsBefore, a_effect->flags.underlying(), vtable, IsStoked() ? "yes" : "no", REX::W32::GetCurrentThreadId());
        }

        // After the original: counted always; the player's (caster or target) effects with a hit shader logged, capped
        void ObserveStart(Site a_site, RE::ActiveEffect* a_effect, std::uint32_t a_flagsBefore)
        {
            auto& counts = g_start[a_site];
            counts.calls.fetch_add(1, std::memory_order_relaxed);
            if (!a_effect) {
                return;
            }
            const auto playerHandle = PlayerHandle();
            const bool casterPlayer = playerHandle != 0 && a_effect->caster.native_handle() == playerHandle;
            auto* player = RE::PlayerCharacter::GetSingleton();
            const bool targetPlayer = player && a_effect->target && a_effect->target == player->AsMagicTarget();
            if (!casterPlayer && !targetPlayer) {
                return;
            }
            const auto* base = a_effect->GetBaseObject();
            if (!base || !base->data.effectShader) {
                return;
            }
            counts.playerShader.fetch_add(1, std::memory_order_relaxed);
            if (base->data.flags.underlying() & kNoHitEffectBit) {
                counts.noHitEffect.fetch_add(1, std::memory_order_relaxed);
            }
            const bool fire = IsBurnable(base, a_effect->spell);
            if (fire) {
                counts.fire.fetch_add(1, std::memory_order_relaxed);
            }
            int line = 0;
            if (TakeLine(g_startLines, kMaxStartLines, line)) {
                LogStart(a_site, line, a_effect, base, casterPlayer, targetPlayer, fire, a_flagsBefore);
            }
        }

        template <Site S>
        struct StartVisualsHook
        {
            // 34094 takes the ActiveEffect only; its callers ignore rax, which is handed back as it came
            static std::uintptr_t thunk(RE::ActiveEffect* a_effect)
            {
                const std::uint32_t flagsBefore = a_effect ? a_effect->flags.underlying() : 0;  // the caller just read +0x7C
                const auto result = func(a_effect);
                try {
                    (void)Guarded("hit path effect start", [&]() { ObserveStart(S, a_effect, flagsBefore); });
                } catch (const std::exception&) {  // not catch (...): /EHa would swallow SEH codes SehGuard passes on (review)
                    ReportFailure();
                }
                return result;
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        void ObserveApply(const RE::ReferenceEffectController* a_controller)
        {
            g_apply.calls.fetch_add(1, std::memory_order_relaxed);
            if (!a_controller) {
                g_apply.null.fetch_add(1, std::memory_order_relaxed);  // the controller allocation failed (2F0ED1)
                return;
            }
            const auto address = reinterpret_cast<std::uintptr_t>(a_controller);
            const auto vtable = *reinterpret_cast<const std::uintptr_t*>(address);
            int line = 0;
            if (vtable != g_ownedVtable) {
                const bool camera = vtable == g_cameraVtable;
                (camera ? g_apply.camera : g_apply.other).fetch_add(1, std::memory_order_relaxed);
                if (TakeLine(g_applyOtherLines, kMaxApplyOtherLines, line)) {
                    logger::info("BlackFlame: hit path ApplyEffectShader [other {}]: controller 0x{:X} is {} (no shader field "
                                 "read), stoked {}, thread {}", line, address,
                        camera ? "an OwnedCameraEffectController" : std::format("vtable 0x{:X}, not an OwnedController", vtable),
                        IsStoked() ? "yes" : "no", REX::W32::GetCurrentThreadId());
                }
                return;
            }
            g_apply.owned.fetch_add(1, std::memory_order_relaxed);
            const auto* shader = *reinterpret_cast<RE::TESEffectShader* const*>(address + Detail::kOwnedShader);
            const auto targetHandle = *reinterpret_cast<const std::uint32_t*>(address + Detail::kOwnedTargetHandle);
            const auto facingHandle = *reinterpret_cast<const std::uint32_t*>(address + Detail::kOwnedFacingHandle);
            const bool fire = Detail::IsFireShader(shader);
            const auto playerHandle = PlayerHandle();
            const bool onPlayer = playerHandle != 0 && targetHandle == playerHandle;
            if (fire) {
                g_apply.fireShader.fetch_add(1, std::memory_order_relaxed);
            }
            if (onPlayer) {
                g_apply.onPlayer.fetch_add(1, std::memory_order_relaxed);
            }
            const bool notable = fire || onPlayer;
            auto& usual = notable ? g_applyLines : g_applyOtherLines;
            const int usualCap = notable ? kMaxApplyLines : kMaxApplyOtherLines;
            const bool usualRoom = usual.load(std::memory_order_relaxed) < usualCap;
            // the session-wide actor pool takes it only when the usual pool is full and the target proves corpse-notable
            // (not alive or killed by the player, HitPathAttribution.h)
            const bool actorRoom = !usualRoom && !onPlayer && Detail::ActorPoolOpen();
            if (!usualRoom && !actorRoom) {
                if (!onPlayer) {
                    Detail::NoteActorClosed();
                }
                return;  // no pool that could take the line has room: the target is not resolved
            }
            // resolved once, as FormIdOfHandle always did for this line (the NiPointer holds the ref while it is read)
            const auto target = targetHandle ? RE::TESObjectREFR::LookupByHandle(targetHandle) : RE::NiPointer<RE::TESObjectREFR>{};
            auto* actor = target ? target->As<RE::Actor>() : nullptr;
            const auto targetId = target ? target->GetFormID() : 0;
            const auto facts = Detail::ReadActorFacts(actor, targetId, playerHandle);  // the defaults for a non-actor
            const bool corpse = actor && !onPlayer && facts.CorpseNotable();
            const char* pool = nullptr;
            if (usualRoom && TakeLine(usual, usualCap, line)) {
                pool = notable ? "fire/player" : "other";
            } else if (corpse && Detail::TakeActorLine(targetId, line)) {
                pool = "actor";  // the windowed pool is full: a corpse line from the session's own 48
            }
            if (!pool) {
                if (!usualRoom) {
                    Detail::NoteIdleResolve();  // resolved for the actor pool alone: not corpse-notable, or its target or the pool capped
                }
                return;
            }
            logger::info("BlackFlame: hit path ApplyEffectShader [{} {}]: OwnedController 0x{:X}, shader {:08X} '{}' (a fire "
                         "MGEF's hit shader {}{}), target {:08X}{}{}, facing {:08X}, stoked {}, thread {}",
                pool, line, address, shader ? shader->GetFormID() : 0, Detail::EditorIdOf(shader), fire ? "yes" : "no",
                Detail::ShaderColours(shader), targetId, onPlayer ? " (player)" : "",
                actor ? Detail::ActorAttribution(facts) : std::string(), FormIdOfHandle(facingHandle),
                IsStoked() ? "yes" : "no", REX::W32::GetCurrentThreadId());
        }

        struct ShaderEffectCtorHook
        {
            // 34910(this = the new ShaderReferenceEffect, controller); returns this (19872 keeps it in rdi)
            static void* thunk(void* a_effect, RE::ReferenceEffectController* a_controller)
            {
                auto* result = func(a_effect, a_controller);
                try {
                    (void)Guarded("hit path ApplyEffectShader", [&]() { ObserveApply(a_controller); });
                } catch (const std::exception&) {
                    ReportFailure();
                }
                // after the ctor (it zeroes +0x130) and before 34762 publishes the SRE; guarded on its own (CorpseMark.h)
                Detail::MarkCorpseEffect(result, a_controller, g_ownedVtable);
                return result;
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        void HookSites()
        {
            const auto update = Patch::AddressOf(kUpdateId);
            const auto startVisuals = Patch::AddressOf(kStartVisualsId);
            const auto apply = Patch::AddressOf(kApplyShaderId);
            const auto ctor = Patch::AddressOf(kShaderEffectCtorId);
            constexpr const char* kNameA = "hit path site A (34092 -> 34094)";
            constexpr const char* kNameB = "hit path site B (34092 -> 34094)";
            constexpr const char* kNameApply = "hit path ApplyEffectShader (19872 -> 34910)";
            const bool okA = Detail::VerifyCallSite(kNameA, update + kSiteAOffset, startVisuals);
            const bool okB = Detail::VerifyCallSite(kNameB, update + kSiteBOffset, startVisuals);
            const bool okApply = Detail::VerifyCallSite(kNameApply, apply + kApplySiteOffset, ctor);
            const std::size_t needed = kBytesPerCall * ((okA ? 1 : 0) + (okB ? 1 : 0) + (okApply ? 1 : 0));
            const auto freeBefore = SKSE::GetTrampoline().free_size();
            if (freeBefore < needed) {
                logger::error("BlackFlame: hit path observation: the SKSE trampoline has {} free bytes, {} needed - no site hooked",
                    freeBefore, needed);
                for (const auto& [ok, name, site] : { std::tuple{ okA, kNameA, update + kSiteAOffset },
                         std::tuple{ okB, kNameB, update + kSiteBOffset }, std::tuple{ okApply, kNameApply, apply + kApplySiteOffset } }) {
                    if (ok) {
                        Patch::RecordHook(name, false, site, Patch::HexBytes(site, Patch::kRel32Size), "trampoline short");
                    }
                }
                return;
            }
            if (okA) {
                // the original first, then the E8: a thread at the site never calls an unset func (review); VerifySite proved
                // the site's target is that callee
                StartVisualsHook<kSiteA>::func = decltype(StartVisualsHook<kSiteA>::func){ startVisuals };
                (void)Patch::WriteCall(kNameA, update + kSiteAOffset, StartVisualsHook<kSiteA>::thunk,
                    "log only: the original is called with the same argument");
                g_startHooked[kSiteA] = true;
            }
            if (okB) {
                StartVisualsHook<kSiteB>::func = decltype(StartVisualsHook<kSiteB>::func){ startVisuals };
                (void)Patch::WriteCall(kNameB, update + kSiteBOffset, StartVisualsHook<kSiteB>::thunk,
                    "log only: the original is called with the same argument");
                g_startHooked[kSiteB] = true;
            }
            if (okApply) {
                ShaderEffectCtorHook::func = decltype(ShaderEffectCtorHook::func){ ctor };
                (void)Patch::WriteCall(kNameApply, apply + kApplySiteOffset, ShaderEffectCtorHook::thunk,
                    "log only: the original is called with the same arguments");
                g_applyHooked = true;
            }
            logger::info("BlackFlame: hit path observation: site A {}, site B {}, ApplyEffectShader {}; trampoline {} bytes free "
                         "before, {} after; {} fire MGEF hit shaders",
                okA ? "hooked" : "skipped", okB ? "hooked" : "skipped", okApply ? "hooked" : "skipped", freeBefore,
                SKSE::GetTrampoline().free_size(), Detail::FireShaderCount());
        }
    }

    void Install()
    {
        const auto version = REL::Module::get().version();
        if (version != REL::Version(1, 6, 1170, 0)) {
            logger::info("BlackFlame: hit path observation off (runtime {}: the offsets are 1.6.1170's)", version.string("."));
            return;
        }
        if (!Patch::HasIds({ 40508, kUpdateId, kStartVisualsId, 34761, kShaderEffectCtorId, kApplyShaderId, 34833, 34154,
                               kOwnedVtableId, kCameraVtableId, 205809,  // 205809: the controller vtable read below (review)
                               34933, 34934, 17325, 206059, 105640, 34912, 34913, 34920, 34921 },  // CorpseGrey.h, SreProbe.h
                "hit path observation")) {
            logger::warn("BlackFlame: hit path observation off (Address Library ids missing)");
            return;
        }
        g_ownedVtable = Patch::AddressOf(kOwnedVtableId);
        g_cameraVtable = Patch::AddressOf(kCameraVtableId);
        g_effectControllerVtable = REL::Relocation<std::uintptr_t>{ RE::ActiveEffectReferenceEffectController::VTABLE[0] }.address();
        Detail::BuildFireShaderSet();
        ReportForeignHooks();
        HookSites();
        Detail::InstallCorpseGrey();   // hooks A and B (CorpseGrey.h): both sites checked, both hooked or neither
        Detail::ArmSreProbes();        // H8 now, H6 and H8 again at the main menu (SreProbe.h)
    }

    bool CorpseMarkInstalled() noexcept
    {
        return g_applyHooked;
    }

    void ResetWindow(bool a_capsReset) noexcept
    {
        for (auto& site : g_start) {
            site.calls.store(0);
            site.playerShader.store(0);
            site.fire.store(0);
            site.noHitEffect.store(0);
        }
        for (auto* counter : { &g_apply.calls, &g_apply.owned, &g_apply.fireShader, &g_apply.onPlayer, &g_apply.camera,
                 &g_apply.other, &g_apply.null }) {
            counter->store(0);
        }
        Detail::ResetIdleResolves();
        Detail::ClearPostLoad();  // the attach probe's "after a load" ends with the first stoke window
        if (a_capsReset) {
            g_startLines.store(0);
            g_applyLines.store(0);
            g_applyOtherLines.store(0);
        }
    }

    void LogStats(const char* a_when, double a_seconds, const char* a_reason)
    {
        if (!g_startHooked[kSiteA] && !g_startHooked[kSiteB] && !g_applyHooked) {
            return;
        }
        const auto site = [](Site a_site) {
            const auto& c = g_start[a_site];
            if (!g_startHooked[a_site]) {
                return std::string("not hooked");
            }
            return std::format("{} calls, {} the player's (caster or target) with a hit shader ({} fire effects of fire spells, "
                               "{} kNoHitEffect)", c.calls.load(), c.playerShader.load(), c.fire.load(), c.noHitEffect.load());
        };
        const std::string apply = !g_applyHooked ? std::string("not hooked") :
                                                   std::format("{} calls: OwnedController {} (a fire MGEF's shader {}, on the "
                                                               "player {}), camera {}, other {}, null {}",
                                                       g_apply.calls.load(), g_apply.owned.load(), g_apply.fireShader.load(),
                                                       g_apply.onPlayer.load(), g_apply.camera.load(), g_apply.other.load(),
                                                       g_apply.null.load()) + "; " + Detail::ActorPoolStats();
        logger::info("BlackFlame: hit path stats (stoke window {} {:.1f} s, {}): effect start site A {}; site B {}; "
                     "ApplyEffectShader {}; {}", a_when, a_seconds, a_reason, site(kSiteA), site(kSiteB), apply,
            Detail::CorpseStats());
    }

}
