#include "CorpseMark.h"

#include "Config.h"
#include "CorpseGrey.h"
#include "GameClock.h"
#include "HitEffectHook.h"
#include "HitPathAddress.h"
#include "HitPathAttribution.h"
#include "MagicApplySink.h"
#include "SehGuard.h"

#include <atomic>
#include <chrono>
#include <format>
#include <string>

namespace BlackFlame::HitPath::Detail
{
    namespace
    {
        using SRE = RE::ShaderReferenceEffect;
        using Steady = std::chrono::steady_clock;

        constexpr int kMaxMarkLines = 48;
        constexpr int kMaxNotMarkedLines = 32;
        constexpr auto kAfterLoadFor = std::chrono::seconds(60);

        struct Counts
        {
            std::atomic<std::uint64_t> owned{ 0 }, skipped{ 0 }, judged{ 0 }, marked{ 0 }, decidedOff{ 0 }, notMarked{ 0 };
        };

        Counts g_counts;  // the session's; never reset
        std::atomic<int> g_markLines{ 0 }, g_notMarkedLines{ 0 };
        std::atomic<std::int64_t> g_postLoadAt{ 0 };  // steady ticks of the last kPostLoadGame, 0: not after a load
        std::atomic<bool> g_markFailureReported{ false };

        bool TakeLine(std::atomic<int>& a_counter, int a_cap, int& a_line)
        {
            if (a_counter.load(std::memory_order_relaxed) >= a_cap) {
                return false;
            }
            a_line = a_counter.fetch_add(1) + 1;
            return a_line <= a_cap;
        }

        double SteadySecondsSince(std::int64_t a_ticks)
        {
            return std::chrono::duration<double>(Steady::now().time_since_epoch() - Steady::duration(a_ticks)).count();
        }

        // The reason the corpse is not marked, empty when it qualifies (ElementalHitRules.h DecideMark); a_open: the last hit
        // is past the window but a mark kept it open
        std::string WhyNotMarked(const std::optional<FireHit>& a_hit, double a_now, double a_window, bool& a_open)
        {
            const auto decision = DecideMark(a_hit ? &*a_hit : nullptr, GameClock::Advancing(), a_now, a_window);
            a_open = decision.reason == MarkReason::kMarkedKeptOpen;
            switch (decision.reason) {
            case MarkReason::kMarked:
            case MarkReason::kMarkedKeptOpen:
                return {};
            case MarkReason::kNoGameClock:
                return "no game clock (the main-thread queue has not run two frames)";
            case MarkReason::kNoHit:
                return "no elemental hit remembered on it";
            case MarkReason::kNotStokedFire:
                return std::format("the last elemental hit on it was {} {} ({:.1f} game s ago)",
                    a_hit->source == FireSource::kNpc ? "an NPC's" : "the player's, not stoked", ElementName(a_hit->element),
                    decision.age);
            case MarkReason::kOverWindow:
            default:
                return std::format("the last fire hit (player stoked) is {:.1f} game s old, over the {} s window{}", decision.age,
                    a_window, a_hit->markedUntilGame > 0.0 ?
                                  std::format(" (an earlier mark kept it open until {:.1f})", a_hit->markedUntilGame) :
                                  std::string());
            }
        }

        void Judge(SRE* a_sre, const RE::ReferenceEffectController* a_controller, std::uintptr_t a_ownedVtable)
        {
            if (!a_sre || !a_controller || a_ownedVtable == 0) {
                return;
            }
            const auto address = reinterpret_cast<std::uintptr_t>(a_controller);
            if (*reinterpret_cast<const std::uintptr_t*>(address) != a_ownedVtable) {
                return;  // the corpse effects are Papyrus Play: OwnedController only
            }
            g_counts.owned.fetch_add(1, std::memory_order_relaxed);
            const auto targetHandle = *reinterpret_cast<const std::uint32_t*>(address + kOwnedTargetHandle);
            const auto playerHandle = PlayerHandleNative();
            if (targetHandle == 0 || playerHandle == 0 || targetHandle == playerHandle) {
                return;
            }
            const double now = GameClock::Seconds();
            const double window = GetConfig().corpseWindowGameSeconds;
            // while "not marked" lines are left every candidate is resolved (a kill whose fire hits were never recorded shows
            // as "no fire hit remembered"); after that only while some remembered fire hit is in its window
            if (g_notMarkedLines.load(std::memory_order_relaxed) >= kMaxNotMarkedLines && GameClock::Advancing() &&
                !AnyFireHitOpen(now, window)) {
                g_counts.skipped.fetch_add(1, std::memory_order_relaxed);  // no target could qualify: not resolved
                return;
            }
            const auto target = RE::TESObjectREFR::LookupByHandle(targetHandle);  // held while it is read
            auto* actor = target ? target->As<RE::Actor>() : nullptr;
            if (!actor) {
                return;
            }
            const auto targetId = target->GetFormID();
            const auto facts = ReadActorFacts(actor, targetId, playerHandle);  // plain field reads and the fire hit copy
            const bool dying = facts.life == RE::ACTOR_LIFE_STATE::kDying;
            if ((!dying && facts.life != RE::ACTOR_LIFE_STATE::kDead) || facts.killer != playerHandle) {
                return;  // not a corpse, or not the player's kill: never marked, no line
            }
            g_counts.judged.fetch_add(1, std::memory_order_relaxed);
            const auto* shader = *reinterpret_cast<RE::TESEffectShader* const*>(address + kOwnedShader);
            const auto shaderId = shader ? shader->GetFormID() : 0;
            const auto thread = REX::W32::GetCurrentThreadId();
            bool keptOpen = false;
            const auto why = WhyNotMarked(facts.hit, now, window, keptOpen);
            int line = 0;
            if (!why.empty()) {
                g_counts.notMarked.fetch_add(1, std::memory_order_relaxed);
                if (TakeLine(g_notMarkedLines, kMaxNotMarkedLines, line)) {
                    logger::info("BlackFlame: corpse not marked [{}]: SRE 0x{:X}, shader {:08X} '{}', target {:08X} ({}), killed by "
                                 "the player: {}, thread {}", line, reinterpret_cast<std::uintptr_t>(a_sre), shaderId,
                        EditorIdOf(shader), targetId, dying ? "dying" : "dead", why, thread);
                }
                return;
            }
            const bool write = GetConfig().greyCorpseShaders;
            const auto before = a_sre->flags.underlying();
            if (write) {
                // the SRE is this thread's alone until 34762 adds it to ProcessLists (after this thunk returns)
                a_sre->flags.set(static_cast<SRE::Flag>(kCorpseGreyBit));
                g_counts.marked.fetch_add(1, std::memory_order_relaxed);
            } else {
                g_counts.decidedOff.fetch_add(1, std::memory_order_relaxed);
            }
            ExtendCorpseWindow(targetId, now + window);  // the chain's later Plays qualify too (design v2 §1)
            if (TakeLine(g_markLines, kMaxMarkLines, line)) {
                const auto& hit = *facts.hit;
                logger::info("BlackFlame: corpse mark [{}]: SRE 0x{:X}, shader {:08X} '{}', target {:08X} ({}), killed by the "
                             "player, last elemental hit {} {} {:.1f} game s / {:.1f} s ago (by the {}; window {} game s{}), "
                             "flags 0x{:08X} -> 0x{:08X}{}, game {:.1f} s, thread {}", line, reinterpret_cast<std::uintptr_t>(a_sre),
                    shaderId, EditorIdOf(shader), targetId, dying ? "dying" : "dead", FireSourceName(hit.source),
                    ElementName(hit.element), now - hit.lastGameAt, SteadySecondsSince(hit.lastAt),
                    hit.origin == HitOrigin::kApplyEvent ? "apply event" : "effect start", window,
                    keptOpen ? std::format(", kept open by an earlier mark until {:.1f}", hit.markedUntilGame) : std::string(),
                    before, a_sre->flags.underlying(), write ? "" : " (greyCorpseShaders false: bit not set)", now, thread);
            }
        }
    }

    void MarkCorpseEffect(void* a_effect, const RE::ReferenceEffectController* a_controller, std::uintptr_t a_ownedVtable)
    {
        try {
            if (!GuardedKeep("corpse mark", [&]() { Judge(static_cast<SRE*>(a_effect), a_controller, a_ownedVtable); }) &&
                !g_markFailureReported.exchange(true)) {
                logger::warn("BlackFlame: corpse mark: a read faulted (logged above); this effect was not marked; further faults "
                             "are counted by the guard");
            }
        } catch (const std::exception& e) {
            if (!g_markFailureReported.exchange(true)) {
                logger::error("BlackFlame: corpse mark threw ({}); this effect was not marked; further failures are not logged",
                    e.what());
            }
        }
    }

    void NotePostLoad() noexcept
    {
        g_postLoadAt.store(Steady::now().time_since_epoch().count());
    }

    void ClearPostLoad() noexcept
    {
        g_postLoadAt.store(0);
    }

    bool IsAfterLoad() noexcept
    {
        const auto at = g_postLoadAt.load();
        return at != 0 && Steady::now().time_since_epoch() - Steady::duration(at) <
                              std::chrono::duration_cast<Steady::duration>(kAfterLoadFor);
    }

    std::string CorpseStats()
    {
        const auto& c = g_counts;
        return std::format("corpse mark (session): OwnedController {}, not resolved with no fire hit in its window {}, the "
                           "player's dying or dead kills {}: marked {}, decided with greyCorpseShaders false {}, not marked {}; "
                           "{}; {}; {}", c.owned.load(), c.skipped.load(), c.judged.load(), c.marked.load(), c.decidedOff.load(),
            c.notMarked.load(), CorpseGreyStats(), FireHitStats(), MagicApplyStats());
    }
}
