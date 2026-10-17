// =============================================================================
// Toxicology - dev self test: poison spell hits (steps 2, 3, 4 and 6)
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
#include <format>
#include <string>
#include <unordered_set>

namespace SelfTest = ToxGame::SelfTest;
using SelfTest::State;
using ToxGame::ApplyOutcome;

namespace
{
    // =========================================================================
    // Constants
    // =========================================================================

    // Phases shared by the three steps
    constexpr int kSpawn = 0;
    constexpr int kReady = 1;
    constexpr int kFirstOwn = 2;  // a step's own phases start here

    constexpr float kResetGapSeconds = 0.5f;  // "elapsed went back": by at least this much
    constexpr float kHalf = 0.5f;
    constexpr float kIntervalMarginSeconds = 0.5f;
    constexpr float kEndMarginSeconds = 2.0f;  // the second hit comes at least this long before the effect ends
    constexpr double kMsPerSecond = 1000.0;

    // Steps 3 and 4 use the first delivery that landed in step 2, in this order
    constexpr std::array<std::string_view, 5> kRefreshOrder{ "touch", "target actor", "aimed", "concentration", "target location" };

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
        std::optional<ApplyOutcome> outcome;  // the last one, set only when a task reached ApplyOrRefresh since the cast
        std::array<std::uint32_t, ToxGame::kApplyOutcomeKinds> outcomes{};  // since the cast, by kind

        // How many ApplyOrRefresh calls since the cast gave a_kind
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
        return std::format("TESHitEvents with this source {}, kept by the sink {}, hit tasks run {}, reached ApplyOrRefresh {} "
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

    float LongestElapsed(RE::Actor* a_actor, const std::vector<SelfTest::EffectRef>& a_refs)
    {
        float out = -1.0f;
        for (const auto& ref : a_refs) {
            if (const auto* e = SelfTest::FindAgain(a_actor, ref)) out = std::max(out, e->elapsedSeconds);
        }
        return out;
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
            SelfTest::Fail(s, step + " apply", std::format("no hit task reached ApplyOrRefresh; the last stopped at: {}", SelfTest::StageName(r.last)));
        } else {
            SelfTest::Check(s, step + " apply", r.Count(ApplyOutcome::kCast) > 0,
                std::format("{} (expected a kCast on a fresh target)", HitText(r)));
        }
        auto detail = EffectsText(onTarget);
        if (r.Count(ApplyOutcome::kCastNoEffect) > 0 && !onTarget.empty()) {
            detail += " - present now but not right after ApplyOrRefresh's cast: the effect is added later, so nothing was "
                      "recorded and a refresh can never happen";
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
        for (const auto label : kRefreshOrder) {
            for (std::size_t i = 0; i < s.deliveries.size() && !s.refreshDelivery; ++i) {
                if (s.deliveries[i].label == label && s.deliveries[i].landed) s.refreshDelivery = i;
            }
        }
        SelfTest::NextStep(s, SelfTest::Step::kRefresh);
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
// Step 3: a second hit refreshes our effect and lengthens it
// =============================================================================

void SelfTest::TickRefresh(State& s)
{
    enum : int { kCast1 = kFirstOwn, kWait1, kWaitElapsed, kCast2, kWait2, kWaitEnd };
    if (!s.refreshDelivery) {
        Fail(s, "3 refresh", "no delivery put the poison on a target in step 2 - nothing to refresh");
        EndStep(s, Step::kForeign);
        return;
    }
    const auto& d = s.deliveries[*s.refreshDelivery];
    bool failed = false;
    if (s.phase < kFirstOwn) {
        BringTarget(s, "3 refresh", kCast1, failed);
        if (failed) NextStep(s, Step::kForeign);
        return;
    }
    const auto actor = TargetActor(s);
    if (!actor) {
        Fail(s, "3 refresh", "the target is gone");
        EndStep(s, Step::kForeign);
        return;
    }
    const float minInterval = ToxGame::GetSettings().minIntervalSeconds;
    switch (s.phase) {
    case kCast1:
    case kCast2:
        if (s.phase == kCast2) s.elapsedBefore = LongestElapsed(actor.get(), { s.owned[s.longest] });
        if (!CastDelivery(s, d, actor.get())) {
            Fail(s, "3 cast", "not cast: the player has no instant magic caster (GetMagicCaster(kInstant) is null)");
            EndStep(s, Step::kForeign);
            return;
        }
        NextPhase(s, s.phase + 1);
        return;
    case kWait1:
        {
            if (!HitSettled(s, d.hitSpell->GetFormID())) return;
            const auto r = Since(s, d.hitSpell->GetFormID());
            std::vector<RE::ActiveEffect*> ours;
            const auto playerHandle = RE::PlayerCharacter::GetSingleton()->GetHandle();
            for (auto* e : LiveEffectsOf(actor.get(), s.poisonA)) {
                if (e->caster == playerHandle) ours.push_back(e);
            }
            if (r.Count(ApplyOutcome::kCast) == 0 || ours.empty()) {
                Fail(s, "3 first hit", std::format("{}; our effects on the target: {}", HitText(r), ours.size()));
                EndStep(s, Step::kForeign);
                return;
            }
            s.owned = Refs(ours);
            s.longest = static_cast<std::size_t>(std::max_element(ours.begin(), ours.end(),
                [](const RE::ActiveEffect* l, const RE::ActiveEffect* r2) { return l->duration < r2->duration; }) - ours.begin());
            s.duration = ours[s.longest]->duration;
            s.waitElapsed = std::max(s.duration * kHalf, minInterval + kIntervalMarginSeconds);
            if (s.waitElapsed > s.duration - kEndMarginSeconds) {
                Fail(s, "3 refresh", std::format("minIntervalSeconds {:.1f} leaves no room for a second hit inside the {:.1f} s effect",
                    minInterval, s.duration));
                EndStep(s, Step::kForeign);
                return;
            }
            Info("3 first hit", std::format("{} of ours: {}; the second hit comes at elapsed {:.1f} s", s.owned.size(),
                EffectsText(ours), s.waitElapsed));
            NextPhase(s, kWaitElapsed);
            return;
        }
    case kWaitElapsed:
        {
            const auto* e = FindAgain(actor.get(), s.owned[s.longest]);
            if (!e || !IsLiveEffect(*e)) {
                Fail(s, "3 refresh", "our effect ended before the second hit");
                EndStep(s, Step::kForeign);
            } else if (e->elapsedSeconds >= s.waitElapsed) {
                NextPhase(s, kCast2);
            } else if (WaitOver(s.phaseTicks, (s.waitElapsed + kElapsedSlackSeconds) * kMsPerSecond)) {
                Fail(s, "3 elapsed", std::format("our effect's elapsedSeconds is still {:.2f} after {} ticks: effects do not run on "
                    "the target{}", e->elapsedSeconds, s.phaseTicks, s.config.disableTargetAI ? " (AI is off: try disableTargetAI false)" : ""));
                EndStep(s, Step::kForeign);
            }
            return;
        }
    case kWait2:
        {
            if (!HitSettled(s, d.hitSpell->GetFormID())) return;
            const auto r = Since(s, d.hitSpell->GetFormID());
            Check(s, "3 refresh outcome", r.Count(ApplyOutcome::kRefreshed) > 0 && r.Count(ApplyOutcome::kCast) == 0,
                HitText(r) + " (expected a kRefreshed and no kCast)");
            const auto live = LiveEffectsOf(actor.get(), s.poisonA);
            std::size_t found = 0;
            for (const auto& ref : s.owned) {
                if (const auto* e = FindAgain(actor.get(), ref); e && IsLiveEffect(*e)) ++found;
            }
            std::unordered_set<const RE::EffectSetting*> bases;
            for (const auto* e : live) bases.insert(e->GetBaseObject());
            Check(s, "3 one instance", live.size() == s.owned.size() && found == s.owned.size() && bases.size() == live.size(),
                std::format("{} live effect(s) of {} on the target ({} distinct base effects), {} of them ours from the first hit "
                    "(expected {} each): {}", live.size(), FormText(s.poisonA), bases.size(), found, s.owned.size(), EffectsText(live)));
            const float after = LongestElapsed(actor.get(), { s.owned[s.longest] });
            Check(s, "3 elapsed reset", after >= 0.0f && after <= s.elapsedBefore - kResetGapSeconds && after <= kNearZeroSeconds,
                std::format("longest effect's elapsedSeconds {:.2f} before the second hit, {:.2f} after (expected <= {:.1f})",
                    s.elapsedBefore, after, kNearZeroSeconds));
            s.elapsedAfter = after;
            NextPhase(s, kWaitEnd);
            return;
        }
    case kWaitEnd:
        {
            const auto* e = FindAgain(actor.get(), s.owned[s.longest]);
            if (!e || !IsLiveEffect(*e)) {
                Fail(s, "3 extended", std::format("our effect ended about {:.1f} s after the first hit; its duration is {:.1f} s: "
                    "elapsedSeconds = 0 does not lengthen it (kRefreshByRecast, ToxApply.cpp)", s.sinceFirstHit, s.duration));
                EndStep(s, Step::kForeign);
                return;
            }
            // Time since the first hit, counted low: elapsed at the second hit plus what the effect ran since the
            // reading just after it. Without a real reset this tops out below the duration, so no false PASS.
            s.sinceFirstHit = s.elapsedBefore + (e->elapsedSeconds - s.elapsedAfter);
            if (s.sinceFirstHit >= s.duration + kExtendMarginSeconds) {
                Pass(s, "3 extended", std::format("still on {:.1f} s after the first hit (its duration {:.1f} s, elapsed {:.1f}/{:.1f})",
                    s.sinceFirstHit, s.duration, e->elapsedSeconds, e->duration));
                EndStep(s, Step::kForeign);
            } else if (WaitOver(s.phaseTicks, (s.duration + kElapsedSlackSeconds) * kMsPerSecond)) {
                Fail(s, "3 extended", std::format("elapsedSeconds stuck at {:.2f} after {} ticks", e->elapsedSeconds, s.phaseTicks));
                EndStep(s, Step::kForeign);
            }
            return;
        }
    default:
        EndStep(s, Step::kForeign);
        return;
    }
}

// =============================================================================
// Step 4: the same poison from another source is neither extended nor stacked
// =============================================================================

void SelfTest::TickForeign(State& s)
{
    enum : int { kDirectCast = kFirstOwn, kHit, kWaitHit };
    if (!s.refreshDelivery) {
        Fail(s, "4 foreign", "no delivery put the poison on a target in step 2");
        EndStep(s, Step::kDoses);
        return;
    }
    const auto& d = s.deliveries[*s.refreshDelivery];
    bool failed = false;
    if (s.phase < kFirstOwn) {
        BringTarget(s, "4 foreign", kDirectCast, failed);
        if (failed) NextStep(s, Step::kDoses);
        return;
    }
    const auto actor = TargetActor(s);
    if (!actor) {
        Fail(s, "4 foreign", "the target is gone");
        EndStep(s, Step::kDoses);
        return;
    }
    switch (s.phase) {
    case kDirectCast:
        // The poison put on directly, as a coated weapon would: not through ApplyOrRefresh, so no record
        if (!CastFromPlayer(s.poisonA, actor.get())) {
            Fail(s, "4 cast", "the poison was not cast: the player has no instant magic caster (GetMagicCaster(kInstant) is null)");
            EndStep(s, Step::kDoses);
            return;
        }
        Info("4 sync", std::format("effects of {} in the target's list right after CastSpellImmediate returned: {} (ApplyOrRefresh "
            "records what is there at that moment)", FormText(s.poisonA), LiveEffectsOf(actor.get(), s.poisonA).size()));
        NextPhase(s, kHit);
        return;
    case kHit:
        {
            if (s.phaseTicks < static_cast<std::size_t>(kSettleTicks)) return;
            const auto foreign = LiveEffectsOf(actor.get(), s.poisonA);
            if (foreign.empty()) {
                Fail(s, "4 setup", "the direct CastSpellImmediate of the poison left no effect on the target");
                EndStep(s, Step::kDoses);
                return;
            }
            s.owned = Refs(foreign);
            s.elapsedBefore = LongestElapsed(actor.get(), s.owned);
            Info("4 foreign", "before the hit: " + EffectsText(foreign));
            if (!CastDelivery(s, d, actor.get())) {
                Fail(s, "4 cast", "not cast: the player has no instant magic caster (GetMagicCaster(kInstant) is null)");
                EndStep(s, Step::kDoses);
                return;
            }
            NextPhase(s, kWaitHit);
            return;
        }
    case kWaitHit:
        {
            if (!HitSettled(s, d.hitSpell->GetFormID())) return;
            const auto r = Since(s, d.hitSpell->GetFormID());
            Check(s, "4 foreign outcome", r.Count(ApplyOutcome::kForeignPresent) > 0 && r.Count(ApplyOutcome::kCast) == 0 &&
                r.Count(ApplyOutcome::kRefreshed) == 0, HitText(r) + " (expected a kForeignPresent, no kCast or kRefreshed)");
            const auto live = LiveEffectsOf(actor.get(), s.poisonA);
            Check(s, "4 not stacked", live.size() == s.owned.size(), std::format("{} effect(s) of {} after the hit, {} before: {}",
                live.size(), FormText(s.poisonA), s.owned.size(), EffectsText(live)));
            const float after = LongestElapsed(actor.get(), s.owned);
            Check(s, "4 not extended", after > s.elapsedBefore, std::format("longest elapsedSeconds {:.2f} before the hit, {:.2f} "
                "after (expected it kept running: larger; -1 = gone)", s.elapsedBefore, after));
            EndStep(s, Step::kDoses);
            return;
        }
    default:
        EndStep(s, Step::kDoses);
        return;
    }
}
