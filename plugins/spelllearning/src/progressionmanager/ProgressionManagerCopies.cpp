// =============================================================================
// ProgressionManagerCopies.cpp - another plugin's copy of a tree spell
// =============================================================================
//
// The scan keeps one record of a spell two plugins carry (SpellScannerDuplicates.cpp)
// and the tome hook reads a copy's tome as the tree spell's. A save can still hold
// the copy itself: learned before the merge, or from outside the tree. That copy
// is the tree spell to the player, so it counts as mastered for the tree spell's
// children, and the tree spell is not granted on top of it.

#include "ProgressionManager.h"
#include "SpellEffectivenessHook.h"
#include "SpellScanner.h"

#include <algorithm>

namespace
{
    // The other members of the spell's duplicate group (empty when it has none)
    std::vector<RE::FormID> CopiesOf(RE::FormID spellId)
    {
        auto group = SpellScanner::DuplicateGroupOf(spellId);
        std::erase(group, spellId);
        return group;
    }

    bool PlayerKnows(RE::FormID spellId)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellId);
        return player && spell && player->HasSpell(spell);
    }
}

bool ProgressionManager::IsSpellMastered(RE::FormID spellId) const
{
    if (IsOwnSpellMastered(spellId)) return true;
    const auto copies = CopiesOf(spellId);
    return std::ranges::any_of(copies, [this](RE::FormID copyId) { return IsOwnSpellMastered(copyId); });
}

bool ProgressionManager::PlayerKnowsCopyOf(RE::FormID spellId) const
{
    return std::ranges::any_of(CopiesOf(spellId), PlayerKnows);
}
