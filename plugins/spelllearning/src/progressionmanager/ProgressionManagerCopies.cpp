// =============================================================================
// ProgressionManagerCopies.cpp — Copies of a tree spell merged into it
// =============================================================================
//
// The panel merges exact copies of one spell (a mod installed twice, a copied
// spell) into one tree spell (SpellCopies, spellCopies.js) and sends each copy
// with copyOf. A copy's tome then teaches the tree spell (SpellTomeHook,
// DESTIntegration), a mastered or known copy counts as the tree spell
// (IsSpellMastered), and a known copy keeps the tree spell from being granted
// a second time (CanUnlock, the early grant).

#include "ProgressionManager.h"
#include "AsciiText.h"
#include "SpellScanner.h"

void ProgressionManager::SetTreeCopy(RE::FormID copyId, RE::FormID treeSpellId)
{
    if (copyId == 0 || treeSpellId == 0 || copyId == treeSpellId) return;
    // The panel saved the pair by formId; after a load order change an old id can
    // name another spell. Only a spell of the same name in the same school is a copy
    auto* copy = RE::TESForm::LookupByID<RE::SpellItem>(copyId);
    auto* treeSpell = RE::TESForm::LookupByID<RE::SpellItem>(treeSpellId);
    if (!copy || !treeSpell ||
        !AsciiText::EqualsIgnoreCase(copy->GetFullName(), treeSpell->GetFullName()) ||
        SpellScanner::GetSpellSchool(copy) != SpellScanner::GetSpellSchool(treeSpell)) {
        logger::warn("ProgressionManager: {:08X} is not a copy of tree spell {:08X} (other or missing spell) - ignored",
            copyId, treeSpellId);
        return;
    }
    m_treeCopyOf[copyId] = treeSpellId;
    auto& copies = m_treeCopies[treeSpellId];
    if (std::ranges::find(copies, copyId) == copies.end()) copies.push_back(copyId);
}

namespace
{
    bool PlayerKnows(RE::FormID spellId)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellId);
        return player && spell && player->HasSpell(spell);
    }
}

bool ProgressionManager::PlayerKnowsCopyOf(RE::FormID spellId) const
{
    auto it = m_treeCopies.find(spellId);
    return it != m_treeCopies.end() && std::ranges::any_of(it->second, PlayerKnows);
}

bool ProgressionManager::PlayerKnowsSpellOrCopy(RE::FormID spellId) const
{
    return PlayerKnows(spellId) || PlayerKnowsCopyOf(spellId);
}

RE::FormID ProgressionManager::TreeSpellFor(RE::FormID spellId) const
{
    auto it = m_treeCopyOf.find(spellId);
    return it != m_treeCopyOf.end() ? it->second : spellId;
}
