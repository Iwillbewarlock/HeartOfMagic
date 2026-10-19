// =============================================================================
// Toxicology - dev self test: poison spell hits (steps 2, 3 and 6; step 3 the immunity window)
// =============================================================================
// Each case spawns a fresh target (game/ToxSelfTestSetup.cpp), casts with the
// player's instant caster, waits for the hit task (the hit trace, ToxHits.h)
// and reads the target's active effects. See game/ToxSelfTestState.h.

#include "game/ToxSelfTestState.h"

#include "Common.h"
#include "game/ToxData.h"
#include "game/ToxMagicka.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>

namespace SelfTest = ToxGame::SelfTest;
using SelfTest::State;
using ToxGame::ApplyOutcome;

namespace
{
    // =========================================================================
    // Constants
    // =========================================================================

    // Phases shared by the two steps
    constexpr int kSpawn = 0;
    constexpr int kReady = 1;
    constexpr int kFirstOwn = 2;  // a step's own phases start here

    // Step 3 uses the first delivery that landed in step 2, in this order
    constexpr std::array<std::string_view, 5> kSecondHitOrder{ "touch", "target actor", "aimed", "concentration", "target location" };

    // =========================================================================
    // Hit trace
    // =========================================================================

    struct HitResult
    {
        std::uint32_t raw = 0;
        std::uint32_t kept = 0;
        std::uint32_t tasks = 0;
        std::uint32_t applied = 0;
        ToxGame::HitStage last = ToxGame::HitStage::kNone;
        std::optional<ApplyOutcome> outcome;  // the last one, set only when a task reached ApplyPoison since the cast
        std::array<std::uint32_t, ToxGame::kApplyOutcomeKinds> outcomes{};  // since the cast, by kind

        // How many ApplyPoison calls since the cast gave a_kind
        [[nodiscard]] std::uint32_t Count(ApplyOutcome a_kind) const { return outcomes[static_cast<std::size_t>(a_kind)]; }
    };

    HitResult Since(const State& s, RE::FormID a_spell)
    {
        const auto now = ToxGame::HitTraceFor(a_spell);
        const auto& was = s.traceBefore;
        HitResult r{ now.rawEvents - was.rawEvents, now.sinkHits - was.sinkHits, now.tasks - was.tasks,
            now.applied - was.applied, now.last, std::nullopt, {} };
        if (r.applied > 0) r.outcome = now.outcome;
        for (std::size_t i = 0; i < r.outcomes.size(); ++i) r.outcomes[i] = now.outcomes[i] - was.outcomes[i];
        return r;
    }

    std::string HitText(const HitResult& a_r)
    {
        std::string kinds;
        for (std::size_t i = 0; i < a_r.outcomes.size(); ++i) {
            if (a_r.outcomes[i] == 0) continue;
            kinds += std::format("{}{} x{}", kinds.empty() ? "" : ", ", SelfTest::OutcomeName(static_cast<ApplyOutcome>(i)), a_r.outcomes[i]);
        }
        return std::format("TESHitEvents with this source {}, kept by the sink {}, hit tasks run {}, reached ApplyPoison {} "
            "({}), last task: {}, last outcome {}", a_r.raw, a_r.kept, a_r.tasks, a_r.applied, kinds.empty() ? "no outcome" : kinds,
            SelfTest::StageName(a_r.last), SelfTest::OutcomeName(a_r.outcome));
    }

    // True once the hit has had kSettleTicks since its first sign and every kept event's task ran,
    // or hitWaitMs passed since the cast
    bool HitSettled(State& s, RE::FormID a_spell)
    {
        const auto r = Since(s, a_spell);
        if ((r.raw > 0 || r.tasks > 0) && !s.hitSeenTick) s.hitSeenTick = s.phaseTicks;
        if (s.hitSeenTick && s.phaseTicks >= *s.hitSeenTick + SelfTest::kSettleTicks && r.tasks >= r.kept) return true;
        return SelfTest::WaitOver(s.phaseTicks, s.config.hitWaitMs);
    }

    // =========================================================================
    // Effects
    // =========================================================================

    std::string EffectsText(const std::vector<RE::ActiveEffect*>& a_effects)
    {
        std::string out = std::format("{} effect(s)", a_effects.size());
        for (const auto* e : a_effects) {
            const auto* base = e->GetBaseObject();  // RE/A/ActiveEffect.h:93
            const float recorded = e->effect ? e->effect->effectItem.magnitude : 0.0f;  // RE/E/Effect.h:20
            out += std::format("; {} elapsed {:.2f}/{:.2f} s, magnitude {:.2f} (record {:.2f})", SelfTest::FormText(base),
                e->elapsedSeconds, e->duration, e->magnitude, recorded);
        }
        return out;
    }

    std::size_t NewSince(const std::vector<RE::ActiveEffect*>& a_now, const std::vector<SelfTest::EffectRef>& a_before)
    {
        return static_cast<std::size_t>(std::count_if(a_now.begin(), a_now.end(), [&](const RE::ActiveEffect* e) {
            return std::none_of(a_before.begin(), a_before.end(),
                [e](const SelfTest::EffectRef& b) { return b.effect == e && b.uniqueId == e->usUniqueID; });
        }));
    }

    // =========================================================================
    // Shared phases
    // =========================================================================

    // kSpawn / kReady. Returns true when the target is ready (the phase is then a_next); false while
    // waiting or after a failure (a_failed set, target deleted).
    bool BringTarget(State& s, std::string_view a_step, int a_next, bool& a_failed)
    {
        a_failed = false;
        if (s.phase == kSpawn) {
            if (!SelfTest::SpawnTarget(s, a_step)) {
                a_failed = true;
                return false;
            }
            SelfTest::NextPhase(s, kReady);
            return false;
        }
        switch (SelfTest::TargetReady(s)) {
        case SelfTest::Readiness::kReady:
            SelfTest::NextPhase(s, a_next);
            return true;
        case SelfTest::Readiness::kTimedOut:
            SelfTest::Fail(s, std::format("{} target", a_step), std::format("no 3D or high process after {} ms",
                s.config.spawnWaitMs));
            SelfTest::DeleteTarget(s);
            a_failed = true;
            return false;
        case SelfTest::Readiness::kWaiting:
            break;
        }
        return false;
    }

    // Before each delivery cast: the count toward the next bottle to 0, what the last cast left added to
    // chargedByTestCasts (the first time it holds the player's own count, put back by Undo)
    void ZeroCountBeforeCast(State& s)
    {
        if (s.countZeroed) s.chargedByTestCasts += ToxGame::Accumulated();
        s.countZeroed = true;
        ToxGame::SetAccumulated(0.0f);
    }

    // Casts d at the target (a cloak on the player) after noting what the checks compare against
    bool CastDelivery(State& s, const SelfTest::Delivery& d, RE::Actor* a_target)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        ZeroCountBeforeCast(s);
        s.traceBefore = ToxGame::HitTraceFor(d.hitSpell->GetFormID());
        s.playerBefore = SelfTest::Refs(SelfTest::LiveEffectsOf(player, s.poisonA));
        s.hitSeenTick.reset();
        if (d.cloak) s.cloakOnPlayer = d.spell;
        if (!SelfTest::CastFromPlayer(d.spell, d.cloak ? static_cast<RE::TESObjectREFR*>(player) : a_target)) return false;
        s.castSync = !SelfTest::LiveEffectsOf(a_target, d.hitSpell).empty();
        return true;
    }

    // =========================================================================
    // Step 2 (and 6): one delivery
    // =========================================================================

    void CheckDelivery(State& s, SelfTest::Delivery& d)
    {
        const auto step = std::format("2/{}", d.label);
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto actor = SelfTest::TargetActor(s);
        const auto r = Since(s, d.hitSpell->GetFormID());
        const auto onTarget = SelfTest::LiveEffectsOf(actor.get(), s.poisonA);
        SelfTest::Info(step, std::format("cast {}{}; its own effects on the target right after CastSpellImmediate: {}; {}",
            SelfTest::FormText(d.spell), d.cloak ? " on the player (cloak)" : " at the target", s.castSync ? "yes (synchronous)" : "no",
            SelfTest::TargetText(s, actor.get())));
        SelfTest::Check(s, step + " hit event", r.kept > 0, HitText(r) +
            (r.kept > 0 ? "" : (r.raw > 0 ? " - events came but the cause was not the player or the target not another actor" :
                                            " - no TESHitEvent for this spell: this delivery needs another hook (spec 5.3, MagicTarget::AddTarget)")));
        if (r.kept == 0) {
            if (!onTarget.empty()) SelfTest::Info(step, "the poison is on the target anyway: " + EffectsText(onTarget));
            if (!d.cloak && d.spell->GetDelivery() == RE::MagicSystem::Delivery::kTargetLocation) {
                SelfTest::Info(step, "no hit from the target-location spell: its rune or area may still be in the world here "
                    "(run the test in a throwaway cell)");
            }
            return;
        }
        if (r.applied == 0) {
            SelfTest::Fail(s, step + " apply", std::format("no hit task reached ApplyPoison; the last stopped at: {}", SelfTest::StageName(r.last)));
        } else {
            SelfTest::Check(s, step + " apply", r.Count(ApplyOutcome::kCast) > 0,
                std::format("{} (expected a kCast on a fresh target)", HitText(r)));
        }
        auto detail = EffectsText(onTarget);
        if (r.Count(ApplyOutcome::kCastNoEffect) > 0 && !onTarget.empty()) {
            detail += " - present now but not right after ApplyPoison's cast: the effect is added later (the pair is "
                      "recorded all the same; only the cast's log line says no effect)";
        }
        SelfTest::Check(s, step + " poison on target", !onTarget.empty(), std::format("{} on the target: {}", SelfTest::FormText(s.poisonA), detail));
        const auto onPlayer = NewSince(SelfTest::LiveEffectsOf(player, s.poisonA), s.playerBefore);
        SelfTest::Check(s, step + " not on player", onPlayer == 0, onPlayer == 0 ?
            std::format("no new effect of {} on the player", SelfTest::FormText(s.poisonA)) :
            std::format("{} new effect(s) of {} on the player: the Self delivery wins - set kCastFromTarget (ToxApply.cpp)", onPlayer,
                SelfTest::FormText(s.poisonA)));
        if (!onTarget.empty()) {
            const auto playerHandle = player->GetHandle();
            const auto byPlayer = std::count_if(onTarget.begin(), onTarget.end(), [&](const RE::ActiveEffect* e) { return e->caster == playerHandle; });
            std::string casters;
            for (const auto* e : onTarget) {
                const auto caster = e->caster.get();  // RE/A/ActiveEffect.h:96 (ActorHandle)
                casters += std::format(" [{:08X}]", caster ? caster->GetFormID() : 0);
            }
            SelfTest::Check(s, std::format("6/{} caster", d.label), byPlayer == static_cast<std::ptrdiff_t>(onTarget.size()),
                std::format("{} of {} effect(s) have the player [{:08X}] as caster; casters:{}", byPlayer, onTarget.size(),
                    player->GetFormID(), casters));
        }
        d.landed = r.Count(ApplyOutcome::kCast) > 0 && !onTarget.empty();
    }

    void NextDelivery(State& s)
    {
        SelfTest::DispelCloak(s);
        SelfTest::DeleteTarget(s);
        ++s.deliveryIndex;
        SelfTest::NextPhase(s, kSpawn);
    }

    void EndDeliveries(State& s)
    {
        SelfTest::Info("2 magicka", std::format("step 2's casts were charged {:.2f} Magicka in all (the count is set to 0 before "
            "each cast) and {} went {} -> {} bottles: anything but 0 and no change means CastSpellImmediate raised player cast "
            "events that were charged", s.chargedByTestCasts + ToxGame::Accumulated(), SelfTest::FormText(s.poisonA),
            s.bottlesAtHits, SelfTest::BottleCount(s.poisonA)));
        for (const auto label : kSecondHitOrder) {
            for (std::size_t i = 0; i < s.deliveries.size() && !s.secondHitDelivery; ++i) {
                if (s.deliveries[i].label == label && s.deliveries[i].landed) s.secondHitDelivery = i;
            }
        }
        SelfTest::NextStep(s, SelfTest::Step::kSecondHit);
    }

    void EndStep(State& s, SelfTest::Step a_next)
    {
        SelfTest::DeleteTarget(s);
        SelfTest::NextStep(s, a_next);
    }
}

// =============================================================================
// Step 2
// =============================================================================

void SelfTest::TickDeliveries(State& s)
{
    if (s.deliveryIndex >= s.deliveries.size()) {
        EndDeliveries(s);
        return;
    }
    auto& d = s.deliveries[s.deliveryIndex];
    const auto step = std::format("2/{}", d.label);
    bool failed = false;
    if (s.phase < kFirstOwn) {
        BringTarget(s, step, kFirstOwn, failed);
        if (failed) NextDelivery(s);
        return;
    }
    const auto actor = TargetActor(s);
    if (!actor) {
        Fail(s, step + " target", "the target is gone");
        NextDelivery(s);
        return;
    }
    if (s.phase == kFirstOwn) {
        if (!CastDelivery(s, d, actor.get())) {
            Fail(s, step + " cast", "not cast: the player has no instant magic caster (GetMagicCaster(kInstant) is null)");
            NextDelivery(s);
            return;
        }
        NextPhase(s, kFirstOwn + 1);
        return;
    }
    if (!HitSettled(s, d.hitSpell->GetFormID())) return;
    CheckDelivery(s, d);
    NextDelivery(s);
}


// =============================================================================
// Step 3: immunity (spec 5.4) - a second hit inside the window does nothing,
// a third hit after it casts again
// =============================================================================

namespace
{
    enum : int
    {
        kCast1 = kFirstOwn,
        kWait1,
        kCast2,
        kWait2,
        kWaitWindow,
        kCast3,
        kWait3
    };

    // The first hit's effect with the longest duration (the clock check follows it)
    std::optional<SelfTest::EffectRef> LongestRef(const std::vector<RE::ActiveEffect*>& a_live)
    {
        const auto it = std::max_element(a_live.begin(), a_live.end(),
            [](const RE::ActiveEffect* a, const RE::ActiveEffect* b) { return a->duration < b->duration; });
        if (it == a_live.end() || !((*it)->duration > 0.0f)) return std::nullopt;
        return SelfTest::EffectRef{ *it, (*it)->usUniqueID };
    }

    // The game clock and s.clockRef's elapsedSeconds now, or nullopt once that effect has ended or the clock is missing
    std::optional<SelfTest::ClockSample> SampleClock(State& s, RE::Actor* a_actor)
    {
        if (!s.clockRef) return std::nullopt;
        const auto clock = ToxGame::GameClockSeconds();
        const auto* effect = SelfTest::FindAgain(a_actor, *s.clockRef);
        if (!clock || !effect || !SelfTest::IsLiveEffect(*effect)) return std::nullopt;
        return SelfTest::ClockSample{ *clock, effect->elapsedSeconds };  // RE/A/ActiveEffect.h:110
    }

    // The game clock against the effect's own timer over the samples taken while it ran
    void CheckClock(State& s)
    {
        if (!s.clockFirst || !s.clockLast) {
            SelfTest::Info("3 clock", "no two samples of the first hit's effect and the game clock - not compared");
            return;
        }
        const double clockSpan = s.clockLast->clockSeconds - s.clockFirst->clockSeconds;
        const double effectSpan = s.clockLast->effectSeconds - s.clockFirst->effectSeconds;
        const auto detail = std::format("game clock advanced {:.2f} s, the effect's elapsedSeconds {:.2f} s (tolerance {:.2f} s)",
            clockSpan, effectSpan, SelfTest::kClockToleranceSeconds);
        if (effectSpan < SelfTest::kMinClockSpanSeconds) {
            SelfTest::Info("3 clock", detail + " - too short a span to judge");
            return;
        }
        SelfTest::Check(s, "3 clock", std::fabs(clockSpan - effectSpan) <= SelfTest::kClockToleranceSeconds, detail);
    }

    // kWait1: the first hit cast, its effects and its window
    bool CheckFirstHit(State& s, RE::Actor* a_actor, const SelfTest::Delivery& d)
    {
        const auto r = Since(s, d.hitSpell->GetFormID());
        const auto live = SelfTest::LiveEffectsOf(a_actor, s.poisonA);
        if (r.Count(ApplyOutcome::kCast) == 0 || live.empty()) {
            SelfTest::Fail(s, "3 first hit", std::format("{}; effects of {} on the target: {} (expected a kCast and an effect)",
                HitText(r), SelfTest::FormText(s.poisonA), live.size()));
            return false;
        }
        s.owned = SelfTest::Refs(live);
        SelfTest::Info("3 first hit", EffectsText(live));
        s.window = ToxGame::ImmunityOf(a_actor->GetFormID(), s.poisonA->GetFormID());
        if (!s.window) {
            SelfTest::Fail(s, "3 window", "no immunity window recorded for the pair (no game clock?)");
            return false;
        }
        SelfTest::Info("3 window", std::format("D {:.2f} s, immunity {:.2f} s, reapply in {:.2f} s", s.window->durationSeconds,
            s.window->immunitySeconds, s.window->reapplyInSeconds));
        s.clockRef = LongestRef(live);
        s.clockFirst = SampleClock(s, a_actor);
        s.clockLast.reset();
        return true;
    }

    // kWait2: the second hit was immune and left the first hit's effects alone
    void CheckSecondHit(State& s, RE::Actor* a_actor, const HitResult& r)
    {
        SelfTest::Check(s, "3 second hit outcome", r.Count(ApplyOutcome::kImmune) > 0 && r.Count(ApplyOutcome::kCast) == 0 &&
            r.Count(ApplyOutcome::kCastNoEffect) == 0, HitText(r) + " (expected a kImmune and no cast)");
        const auto live = SelfTest::LiveEffectsOf(a_actor, s.poisonA);
        std::size_t found = 0;
        for (const auto& ref : s.owned) {
            if (const auto* e = SelfTest::FindAgain(a_actor, ref); e && SelfTest::IsLiveEffect(*e)) ++found;
        }
        SelfTest::Check(s, "3 one instance", live.size() == s.owned.size() && found == s.owned.size(),
            std::format("{} live effect(s) of {} on the target, {} of them from the first hit (expected {} each): {}",
                live.size(), SelfTest::FormText(s.poisonA), found, s.owned.size(), EffectsText(live)));
    }

    // kWait3: the hit after the window cast again, with a new effect
    void CheckThirdHit(State& s, RE::Actor* a_actor, const HitResult& r)
    {
        SelfTest::Check(s, "3 third hit outcome", r.Count(ApplyOutcome::kCast) > 0 && r.Count(ApplyOutcome::kImmune) == 0,
            HitText(r) + " (expected a kCast after the immunity window)");
        const auto live = SelfTest::LiveEffectsOf(a_actor, s.poisonA);
        const auto fresh = NewSince(live, s.owned);
        SelfTest::Check(s, "3 third hit effect", fresh > 0, std::format("{} new effect(s) of {} on the target: {}", fresh,
            SelfTest::FormText(s.poisonA), EffectsText(live)));
    }

    // kWaitWindow, one tick: true when the window has ended (or the step is over, a_done set)
    bool WindowEnded(State& s, RE::Actor* a_actor, bool& a_done)
    {
        a_done = false;
        if (const auto sample = SampleClock(s, a_actor)) s.clockLast = sample;
        const auto window = ToxGame::ImmunityOf(a_actor->GetFormID(), s.poisonA->GetFormID());
        if (!window) {
            SelfTest::Fail(s, "3 window", "the pair's window is gone while waiting (cleared, or no game clock)");
            a_done = true;
            return false;
        }
        if (window->reapplyInSeconds <= 0.0) {
            CheckClock(s);
            return true;
        }
        constexpr double kMsPerSecond = 1000.0;
        constexpr double kWaitFactor = 1.5;
        constexpr double kWaitSlackMs = 10000.0;
        const double capMs = (s.window->durationSeconds + s.window->immunitySeconds) * kMsPerSecond * kWaitFactor + kWaitSlackMs;
        if (SelfTest::WaitOver(s.phaseTicks, capMs)) {
            SelfTest::Fail(s, "3 window", std::format("the game clock did not reach the end of the window: {:.2f} s left after "
                "{:.0f} ms of ticks (the clock does not advance?)", window->reapplyInSeconds, capMs));
            a_done = true;
        }
        return false;
    }
}

void SelfTest::TickSecondHit(State& s)
{
    if (!s.secondHitDelivery) {
        Fail(s, "3 second hit", "no delivery put the poison on a target in step 2 - nothing to hit twice");
        EndStep(s, Step::kDoses);
        return;
    }
    const auto& d = s.deliveries[*s.secondHitDelivery];
    bool failed = false;
    if (s.phase < kFirstOwn) {
        BringTarget(s, "3 second hit", kCast1, failed);
        if (failed) NextStep(s, Step::kDoses);
        return;
    }
    const auto actor = TargetActor(s);
    if (!actor) {
        Fail(s, "3 second hit", "the target is gone");
        EndStep(s, Step::kDoses);
        return;
    }
    switch (s.phase) {
    case kCast2:
        {
            const bool stillOn = std::any_of(s.owned.begin(), s.owned.end(), [&](const EffectRef& ref) {
                const auto* e = FindAgain(actor.get(), ref);
                return e && IsLiveEffect(*e);
            });
            if (!stillOn) {
                Fail(s, "3 second hit", std::format("the effect of {} from the first hit ended before the second hit",
                    FormText(s.poisonA)));
                EndStep(s, Step::kDoses);
                return;
            }
        }
        [[fallthrough]];
    case kCast1:
    case kCast3:
        if (!CastDelivery(s, d, actor.get())) {
            Fail(s, "3 cast", "not cast: the player has no instant magic caster (GetMagicCaster(kInstant) is null)");
            EndStep(s, Step::kDoses);
            return;
        }
        NextPhase(s, s.phase + 1);
        return;
    case kWait1:
        if (!HitSettled(s, d.hitSpell->GetFormID())) return;
        if (!CheckFirstHit(s, actor.get(), d)) {
            EndStep(s, Step::kDoses);
            return;
        }
        NextPhase(s, kCast2);
        return;
    case kWait2:
        {
            if (!HitSettled(s, d.hitSpell->GetFormID())) return;
            const auto r = Since(s, d.hitSpell->GetFormID());
            if (r.kept == 0 || r.applied == 0) {
                Fail(s, "3 second hit", HitText(r) + " - the second hit never reached the apply step, so nothing was judged");
                EndStep(s, Step::kDoses);
                return;
            }
            CheckSecondHit(s, actor.get(), r);
            const double total = s.window->durationSeconds + s.window->immunitySeconds;
            if (total > kMaxImmunityWaitSeconds) {
                Info("3 third hit", std::format("skipped: D + immunity is {:.1f} s, over {:.0f} s (lower immunityMultiplier or "
                    "maxPoisonSeconds to test it)", total, kMaxImmunityWaitSeconds));
                EndStep(s, Step::kDoses);
                return;
            }
            NextPhase(s, kWaitWindow);
            return;
        }
    case kWaitWindow:
        {
            bool done = false;
            if (WindowEnded(s, actor.get(), done)) {
                NextPhase(s, kCast3);
            } else if (done) {
                EndStep(s, Step::kDoses);
            }
            return;
        }
    case kWait3:
        {
            if (!HitSettled(s, d.hitSpell->GetFormID())) return;
            const auto r = Since(s, d.hitSpell->GetFormID());
            if (r.kept == 0 || r.applied == 0) {
                Fail(s, "3 third hit", HitText(r) + " - the third hit never reached the apply step, so nothing was judged");
            } else {
                CheckThirdHit(s, actor.get(), r);
            }
            EndStep(s, Step::kDoses);
            return;
        }
    default:
        EndStep(s, Step::kDoses);
        return;
    }
}
