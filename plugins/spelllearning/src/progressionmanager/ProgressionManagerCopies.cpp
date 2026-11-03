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
#include "ProgressionMath.h"
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

// =============================================================================
// DIRECT LINKS OF A CAST (a copy counts as the spell it copies)
// =============================================================================
// A cast of a copy the player learned before the merge, or from outside the tree, used to count
// as a same-school cast only: the tree's lists name the kept record, never the copy.

bool ProgressionManager::IsDirectPrerequisite(RE::FormID targetSpellId, RE::FormID castSpellId) const
{
    const auto targetList = m_targetPrerequisites.find(targetSpellId);
    // The tree's own prerequisites too: a target set from a spell tome comes with
    // no prerequisite list (SetLearningTargetFromTome), but its tree links still count
    const auto reqIt = m_prereqRequirements.find(targetSpellId);
    if (targetList == m_targetPrerequisites.end() && reqIt == m_prereqRequirements.end()) return false;
    const auto listed = [&](RE::FormID id) {
        if (targetList != m_targetPrerequisites.end() && std::ranges::find(targetList->second, id) != targetList->second.end()) {
            return true;
        }
        if (reqIt == m_prereqRequirements.end()) return false;
        return std::ranges::find(reqIt->second.hardPrereqs, id) != reqIt->second.hardPrereqs.end() ||
               std::ranges::find(reqIt->second.softPrereqs, id) != reqIt->second.softPrereqs.end();
    };
    return ProgressionMath::SpellOrCopyMatches(castSpellId, CopiesOf(castSpellId), listed);
}

bool ProgressionManager::IsDirectChild(RE::FormID targetSpellId, RE::FormID castSpellId) const
{
    const auto listsTarget = [&](RE::FormID id) {
        const auto it = m_prereqRequirements.find(id);
        if (it == m_prereqRequirements.end()) return false;
        return std::ranges::find(it->second.hardPrereqs, targetSpellId) != it->second.hardPrereqs.end() ||
               std::ranges::find(it->second.softPrereqs, targetSpellId) != it->second.softPrereqs.end();
    };
    return ProgressionMath::SpellOrCopyMatches(castSpellId, CopiesOf(castSpellId), listsTarget);
}
