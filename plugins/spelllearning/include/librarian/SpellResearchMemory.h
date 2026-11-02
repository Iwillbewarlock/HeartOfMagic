#pragma once

#include "Common.h"
#include "librarian/SpellResearchMemoryMath.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// =============================================================================
// SpellResearchMemory - practice and memory: HoM casting XP <-> Spell Research
// archetype XP (the game side; the numbers are SpellResearchMemoryMath.h)
//
// A. Practice becomes memory: every 15% boundary (and 100%) a learning target's
//    progress crosses on a cast pays one short research session (memoryMinutes)
//    into the spell's SR archetype globals, through SR's research gain table.
// B. Memory guides practice: a cast's XP for a target x (1 + maxPracticeBoost x the
//    share of the spell's archetypes remembered at the spell's rank, each archetype's
//    global measured against SR's compose scale ((R + 1) x d)^2).
//
// Both do nothing unless SpellResearch.esp is loaded, SR's Import finished and
// none runs (_SR_GlobalIsSetup 1, _SR_GlobalImporting 0), the half's switch is on
// and the spell is in one of SR's tier lists and at least one archetype list.
// Rank and archetypes are read on every call (no cache: an Import can change
// membership with equal list sizes). The lists and globals are resolved once at
// kDataLoaded from compat/spellresearch.json's persistent ids.
//
// Game thread only: the globals and lists are the ones SR's scripts read, and
// the settings are written by ApplyConfig on the game thread without a lock.
// Design: docs/superpowers/specs/2026-11-02-sr-memory-exchange-design.md.
// =============================================================================

namespace Librarian::SpellResearchMemory
{
    // kDataLoaded: with SpellResearch.esp loaded, resolves the tier lists, the 60
    // archetype spell lists, the 60 x 5 memory globals and SR's research time base.
    void Install();

    // config.json's spellResearch.memoryExchange (ApplySettingsFromConfig: game start,
    // the panel's load and every save). Missing keys are the defaults.
    void ApplyConfig(const nlohmann::json& config);

    // kPostLoadGame: one info line with the sum of the memory globals and how many are above 0.
    void OnGameLoaded();

    // A, from ProgressionManager::OnSpellCast: the target's progress before and after
    // the cast's XP. Pays one payment per boundary crossed.
    void OnPracticeProgress(RE::FormID spellId, float progressBefore, float progressAfter);

    // B, from ProgressionManager::OnSpellCast: the factor for the target's casting XP (1.0 when skipped).
    [[nodiscard]] float PracticeMultiplier(RE::FormID spellId);

    // ---- The raw parts, also used by the dev self-test (EnvironmentSelfTestMemory.cpp) ----

    // A spell as SR files it: its rank (the lowest tier list holding it) and the archetype
    // indices (0..59) whose list has it.
    struct SpellMemory
    {
        std::size_t rank = 0;
        std::vector<std::size_t> archetypes;
    };

    // SR is installed here and its lists resolved, an Import finished and none runs.
    // `why` says what is missing when false.
    [[nodiscard]] bool Ready(std::string& why);
    // The spell's rank and archetypes, or nullopt (not in a tier list, or in no archetype list).
    [[nodiscard]] std::optional<SpellMemory> Lookup(const RE::SpellItem* spell);
    // The memory global for an archetype and a rank, or nullptr.
    [[nodiscard]] RE::TESGlobal* Global(std::size_t archetype, std::size_t rank);
    // v for one payment now: SR's research time base x memoryMinutes / 60.
    [[nodiscard]] float PaymentValue();
    // `payments` payments of v into the spell's archetypes (SR's cap rule each time). The
    // switches are not checked here. Returns how many globals changed.
    std::size_t Pay(const SpellMemory& memory, int payments, float value);
    // The share of the spell's archetypes remembered at its rank: the average of
    // min(1, global[a][R] / ((R + 1) x d)^2), d = SR's compose difficulty (SpellResearchMemoryMath::KnownShare).
    [[nodiscard]] float KnownShare(const SpellMemory& memory);
    // d now: _SR_GlobalCraftSpellDifficulty, or 3.0 when missing, not positive or not finite.
    [[nodiscard]] float CraftDifficulty();
    // The lists and globals were found at kDataLoaded (the panel shows the memory rows only then).
    [[nodiscard]] bool Installed();
    // The settings ApplyConfig took last.
    [[nodiscard]] SpellResearchMemoryMath::Settings CurrentSettings();
    // The first spell in SR's tier lists that has an archetype, or nullptr.
    [[nodiscard]] RE::SpellItem* SampleSpell();
}
