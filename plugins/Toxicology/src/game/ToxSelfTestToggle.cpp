// =============================================================================
// Toxicology - dev self test: step 4b, the power that turns the feature on and off
// =============================================================================
// See game/ToxSelfTestState.h for the split between the self test's files.
// Spec 2026-10-20 (game/ToxToggle.h). Runs after step 4a and before step 1, on
// the save's own ability state, whatever "enabled" says (the power must work
// while the feature is off). Every wait is counted in ticks; nothing blocks.
//   phase 0: the records (the power's effect, the power, the two messages). The
//            ability is noted, then the power is cast on the player through the
//            player's instant caster (CastFromPlayer, as the hit steps cast).
//   phase 1: up to kToggleWaitMs for the ability to flip. No flip: FAIL, with
//            which event paths fired (ToxToggle.h PowerTrace).
//   phase 2: kToggleGapMs (longer than the repeat window): one cast made one
//            toggle, and the ability is still flipped. CastSpellImmediate sends
//            no TESSpellCastEvent (plugins/spelllearning/src/
//            SpellCastImmediateHook.cpp:8), so usually only the effect apply
//            event arrives and the debounce is not exercised (an INFO line; a
//            real Z cast is checked from the log). When both events arrive,
//            the second must have been dropped as a repeat. Then the power is
//            cast again.
//   phase 3: up to kToggleWaitMs for the ability to flip back.
//   phase 4: kToggleGapMs again, the second cast's toggle count, then the 1.0
//            save migration (ToxToggle.h MigratePower) on three states: the
//            ability without the power (the power is given), both (nothing),
//            the power without the ability (nothing, it stays off).
// The ability and the power are put back as the save had them (UndoToggle),
// also when the test ends early; a load ends it through the run's abort.
// The real Z-key use of the power is not tested here: whether a hand cast of a
// lesser power raises the same events is an in-game check (README).

#include "game/ToxSelfTestState.h"

#include "Common.h"
#include "game/ToxData.h"
#include "game/ToxSelection.h"
#include "game/ToxToggle.h"

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace SelfTest = ToxGame::SelfTest;
using SelfTest::State;

namespace
{
    // =========================================================================
    // Constants
    // =========================================================================

    constexpr std::string_view kStep = "4b power";
    constexpr double kToggleWaitMs = 5000.0;  // a cast's toggle task, from the cast
    constexpr double kToggleGapMs = ToxGame::kRepeatToggleSeconds * 1000.0 + 500.0;  // between the casts: past the repeat window
    constexpr RE::FormID kVoiceEquipSlotId = 0x025BEE;  // VoiceEquip, Skyrim.esm: a power's equip slot
    constexpr std::uint32_t kOneToggle = 1;

    enum Phase
    {
        kPhaseStart = 0,
        kPhaseWaitOn = 1,
        kPhaseGap = 2,
        kPhaseWaitBack = 3,
        kPhaseMigration = 4,
    };

    // =========================================================================
    // Helpers
    // =========================================================================

    const char* Held(bool a_held)
    {
        return a_held ? "held" : "not held";
    }

    // What the power's sinks and task did since a_before
    std::string TraceSince(const ToxGame::PowerTrace& a_before)
    {
        const auto now = ToxGame::PowerTraceNow();
        return std::format("spell cast events {}, effect apply events {}, toggles {}, repeats dropped {}, failed toggles {}",
            now.castEvents - a_before.castEvents, now.applyEvents - a_before.applyEvents, now.toggles - a_before.toggles,
            now.repeats - a_before.repeats, now.failures - a_before.failures);
    }

    // The player's own spell list entry for a_spell set to a_want (AddSpell / RemoveSpell, RE/A/Actor.h:518,678)
    void SetKnown(RE::SpellItem* a_spell, bool a_want)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !a_spell || player->HasSpell(a_spell) == a_want) return;  // RE/A/Actor.h:620
        if (a_want) {
            player->AddSpell(a_spell);
        } else {
            player->RemoveSpell(a_spell);
        }
    }

    void EndToggle(State& s)
    {
        SelfTest::UndoToggle(s);
        SelfTest::NextStep(s, SelfTest::Step::kSetup);
    }

    // The power's records (spec 2026-10-20 section 3); false when one is missing (nothing further can run)
    bool CheckRecords(State& s)
    {
        auto* effect = ToxGame::PowerEffect();
        auto* power = ToxGame::Power();
        auto* on = ToxGame::MessageOn();
        auto* off = ToxGame::MessageOff();
        if (!effect || !power || !on || !off || !ToxGame::Ability()) {
            SelfTest::Fail(s, kStep, std::format("records missing: effect {}, power {}, on message {}, off message {}, ability {} "
                "(a 1.0 ESL has none of the first four) - step skipped", SelfTest::FormText(effect), SelfTest::FormText(power),
                SelfTest::FormText(on), SelfTest::FormText(off), SelfTest::FormText(ToxGame::Ability())));
            return false;
        }
        using Archetype = RE::EffectSetting::Archetype;
        using CastingType = RE::MagicSystem::CastingType;
        using Delivery = RE::MagicSystem::Delivery;
        // RE/E/EffectSetting.h:92-93,138; RE/E/EffectArchetypes.h:118-121 (to_string)
        SelfTest::Check(s, kStep, effect->GetArchetype() == Archetype::kScript &&
            effect->data.castingType == CastingType::kFireAndForget && effect->data.delivery == Delivery::kSelf,
            std::format("effect {}: archetype {}, casting type {}, delivery {} (Script, Fire and Forget 1, Self 0 expected)",
                SelfTest::FormText(effect), std::to_string(effect->GetArchetype()), std::to_underlying(effect->data.castingType),
                std::to_underlying(effect->data.delivery)));

        const auto* only = power->effects.size() == 1 ? power->effects[0] : nullptr;  // RE/M/MagicItem.h:129
        const auto* slot = power->GetEquipSlot();  // RE/B/BGSEquipType.h:31
        auto* dh = RE::TESDataHandler::GetSingleton();
        const auto* voice = dh ? dh->LookupForm<RE::BGSEquipSlot>(kVoiceEquipSlotId, ToxGame::kSkyrimEsm) : nullptr;
        // RE/S/SpellItem.h:67,69,71 (spellType, castingType, delivery)
        SelfTest::Check(s, kStep, power->GetSpellType() == RE::MagicSystem::SpellType::kLesserPower &&
            power->GetCastingType() == CastingType::kFireAndForget && power->GetDelivery() == Delivery::kSelf && only &&
            only->baseEffect == effect && voice && slot == voice,
            std::format("power {}: spell type {}, casting type {}, delivery {}, {} effect(s), the first {}, equip slot {} (Lesser "
                "Power 3, Fire and Forget 1, Self 0, one effect {}, VoiceEquip {} expected)", SelfTest::FormText(power),
                std::to_underlying(power->GetSpellType()), std::to_underlying(power->GetCastingType()),
                std::to_underlying(power->GetDelivery()), power->effects.size(), SelfTest::FormText(only ? only->baseEffect : nullptr),
                SelfTest::FormText(slot), SelfTest::FormText(effect), SelfTest::FormText(voice)));

        for (const auto* message : { on, off }) {
            const char* text = message->GetFullName();  // RE/T/TESFullName.h:23 (FULL)
            SelfTest::Check(s, kStep, text && *text, std::format("message {} ({}): notification text '{}' (not empty expected)",
                SelfTest::FormText(message), message == on ? "on" : "off", text ? text : ""));
        }
        return true;
    }

    // false (FAIL logged) when there is no instant caster to cast with
    bool CastPower(State& s, std::string_view a_which)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        s.powerTraceBefore = ToxGame::PowerTraceNow();
        if (!SelfTest::CastFromPlayer(ToxGame::Power(), player)) {
            SelfTest::Fail(s, kStep, std::format("{} cast: the player has no instant caster - step skipped", a_which));
            return false;
        }
        SelfTest::Info(kStep, std::format("{} cast of {} on the player (instant caster, CastSpellImmediate); ability {} before it",
            a_which, SelfTest::FormText(ToxGame::Power()), Held(ToxGame::PlayerHasAbility())));
        return true;
    }

    // Phases 1 and 3: true once the ability reads a_want or the wait is over (PASS or FAIL logged)
    bool WaitFlip(State& s, bool a_want, std::string_view a_which)
    {
        const bool now = ToxGame::PlayerHasAbility();
        if (now != a_want && !SelfTest::WaitOver(s.phaseTicks, kToggleWaitMs)) return false;
        const auto trace = TraceSince(s.powerTraceBefore);
        if (now == a_want) {
            SelfTest::Pass(s, kStep, std::format("{} cast: ability {} after {} ticks ({})", a_which, Held(now), s.phaseTicks, trace));
            if (!now && !ToxGame::GetSettings().enabled) {
                // "enabled" off: CurrentPoison is none whatever the ability, so the check would prove nothing
                SelfTest::Info(kStep, std::format("{} cast, off: CurrentPoison check skipped: enabled off", a_which));
            } else if (!now) {
                // Off: the gates follow at once (no poison to copy, so no hit, icon or Magicka count)
                const auto* poison = ToxGame::CurrentPoison(true);
                SelfTest::Check(s, kStep, poison == nullptr, std::format("{} cast, off: CurrentPoison {} (none expected)", a_which,
                    SelfTest::FormText(poison)));
            }
            return true;
        }
        const auto seen = ToxGame::PowerTraceNow();
        const bool noEvent = seen.castEvents == s.powerTraceBefore.castEvents && seen.applyEvents == s.powerTraceBefore.applyEvents;
        SelfTest::Fail(s, kStep, std::format("{} cast: {} after {:.0f} ms - ability {}, {} expected ({})", a_which,
            noEvent ? "no toggle (neither event arrived)" : "an event arrived but the ability did not change", kToggleWaitMs,
            Held(now), Held(a_want), trace));
        return true;
    }

    // Phases 2 and 4: after the gap, one cast must have made exactly one toggle. When both event paths fired, the
    // second was dropped by the debounce; the instant caster sends no TESSpellCastEvent, so usually only one fires
    void CheckOneToggle(State& s, std::string_view a_which)
    {
        const auto& before = s.powerTraceBefore;
        const auto now = ToxGame::PowerTraceNow();
        const auto toggles = now.toggles - before.toggles;
        const auto repeats = now.repeats - before.repeats;
        const bool bothPaths = now.castEvents != before.castEvents && now.applyEvents != before.applyEvents;
        if (bothPaths) {
            SelfTest::Check(s, kStep, toggles == kOneToggle && repeats >= 1, std::format("{} cast, {:.0f} ms later, both events: "
                "{} toggle(s), {} repeat(s) dropped ({} toggle and at least 1 repeat expected; {})", a_which, kToggleGapMs,
                toggles, repeats, kOneToggle, TraceSince(before)));
            return;
        }
        SelfTest::Check(s, kStep, toggles == kOneToggle, std::format("{} cast, {:.0f} ms later: {} toggle(s) ({} expected; {})",
            a_which, kToggleGapMs, toggles, kOneToggle, TraceSince(before)));
        SelfTest::Info(kStep, std::format("{} cast: debounce not exercised (instant caster sends no TESSpellCastEvent); a real "
            "Z cast is checked from the log", a_which));
    }

    // The 1.0 save migration on three states of the save (ToxToggle.h MigratePower)
    void CheckMigration(State& s)
    {
        s.migrationHadPower = ToxGame::PlayerHasPower();
        s.migrationTaken = true;
        SetKnown(ToxGame::Ability(), true);
        SetKnown(ToxGame::Power(), false);
        if (!ToxGame::PlayerHasAbility() || ToxGame::PlayerHasPower()) {
            SelfTest::Fail(s, kStep, std::format("migration: could not set up a 1.0 save (ability {}, power {}; held and not "
                "known wanted) - skipped", Held(ToxGame::PlayerHasAbility()), ToxGame::PlayerHasPower() ? "known" : "not known"));
            return;
        }
        const auto grant = ToxGame::MigratePower();
        SelfTest::Check(s, kStep, grant == Toxicology::MigrationAction::kGrantPower && ToxGame::PlayerHasPower(),
            std::format("migration, ability without the power: {}, power {} (GrantPower and known expected)",
                Toxicology::MigrationActionName(grant), ToxGame::PlayerHasPower() ? "known" : "not known"));

        SetKnown(ToxGame::Power(), true);
        const auto both = ToxGame::MigratePower();
        SelfTest::Check(s, kStep, both == Toxicology::MigrationAction::kNothing && ToxGame::PlayerHasPower() &&
            ToxGame::PlayerHasAbility(), std::format("migration, ability and power: {}, ability {}, power {} (Nothing, held, "
            "known expected)", Toxicology::MigrationActionName(both), Held(ToxGame::PlayerHasAbility()),
            ToxGame::PlayerHasPower() ? "known" : "not known"));

        SetKnown(ToxGame::Ability(), false);
        const auto powerOnly = ToxGame::MigratePower();
        SelfTest::Check(s, kStep, powerOnly == Toxicology::MigrationAction::kNothing && ToxGame::PlayerHasPower() &&
            !ToxGame::PlayerHasAbility(), std::format("migration, power without the ability: {}, ability {}, power {} (Nothing, "
            "not held - it stays off - and known expected)", Toxicology::MigrationActionName(powerOnly),
            Held(ToxGame::PlayerHasAbility()), ToxGame::PlayerHasPower() ? "known" : "not known"));
    }

    void StartPhase(State& s)
    {
        if (!RE::PlayerCharacter::GetSingleton()) {
            SelfTest::Fail(s, kStep, "no player - step skipped");
            EndToggle(s);
            return;
        }
        if (!CheckRecords(s)) {
            EndToggle(s);
            return;
        }
        s.toggleHadAbility = ToxGame::PlayerHasAbility();
        s.toggleTaken = true;
        s.toggleNoted = true;
        SelfTest::Info(kStep, std::format("before: ability {}, power {}, setting \"enabled\" {}", Held(s.toggleHadAbility),
            ToxGame::PlayerHasPower() ? "known" : "not known", ToxGame::GetSettings().enabled));
        if (!CastPower(s, "first")) {
            EndToggle(s);
            return;
        }
        SelfTest::NextPhase(s, kPhaseWaitOn);
    }
}

// =============================================================================
// ToxSelfTestState.h
// =============================================================================

void SelfTest::TickToggle(State& s)
{
    switch (s.phase) {
    case kPhaseStart:
        StartPhase(s);
        return;
    case kPhaseWaitOn:
        if (!WaitFlip(s, !s.toggleHadAbility, "first")) return;
        if (ToxGame::PlayerHasAbility() == s.toggleHadAbility) {  // no toggle: the second cast would test nothing
            CheckMigration(s);
            EndToggle(s);
            return;
        }
        NextPhase(s, kPhaseGap);
        return;
    case kPhaseGap:
        if (!WaitOver(s.phaseTicks, kToggleGapMs)) return;
        CheckOneToggle(s, "first");
        if (!CastPower(s, "second")) {
            EndToggle(s);
            return;
        }
        NextPhase(s, kPhaseWaitBack);
        return;
    case kPhaseWaitBack:
        if (!WaitFlip(s, s.toggleHadAbility, "second")) return;
        NextPhase(s, kPhaseMigration);
        return;
    case kPhaseMigration:
        if (!WaitOver(s.phaseTicks, kToggleGapMs)) return;
        CheckOneToggle(s, "second");
        CheckMigration(s);
        EndToggle(s);
        return;
    default:
        Fail(s, kStep, std::format("unknown phase {}", s.phase));
        EndToggle(s);
        return;
    }
}

void SelfTest::UndoToggle(State& s)
{
    if (!s.toggleTaken && !s.migrationTaken) return;
    if (s.migrationTaken) SetKnown(ToxGame::Power(), s.migrationHadPower);
    if (s.toggleTaken) SetKnown(ToxGame::Ability(), s.toggleHadAbility);
    const bool hadPower = s.migrationTaken ? s.migrationHadPower : ToxGame::PlayerHasPower();
    s.toggleTaken = false;
    s.migrationTaken = false;
    ToxGame::OnSelectionMaybeChanged();  // the caster hook's flag and the icon keyword for the state put back
    Info(kStep, std::format("put back: ability {} (the save had it {}), power {} (the save had it {})",
        Held(ToxGame::PlayerHasAbility()), Held(s.toggleHadAbility), ToxGame::PlayerHasPower() ? "known" : "not known",
        hadPower ? "known" : "not known"));
}

void SelfTest::NoteAbilityBeforeSetup(const State& s)
{
    if (!s.toggleNoted) return;
    const bool now = ToxGame::PlayerHasAbility();
    Info(kStep, std::format("before step 1: ability {}, step 4b started with it {} ({})", Held(now), Held(s.toggleHadAbility),
        now == s.toggleHadAbility ? "the same" : "differs: a toggle task that ran after the put back flipped it"));
}
