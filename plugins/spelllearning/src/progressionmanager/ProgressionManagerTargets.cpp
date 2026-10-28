// =============================================================================
// ProgressionManagerTargets.cpp — Learning targets, prerequisites, unlocking
// =============================================================================

#include "ProgressionManager.h"
#include "FormIdText.h"
#include "ProgressionNotify.h"
#include "SpellEffectivenessHook.h"
#include "SpellScanner.h"
#include "SpellTomeHook.h"

// =============================================================================
// LEARNING TARGETS
// =============================================================================

void ProgressionManager::SetLearningTarget(const std::string& school, RE::FormID formId, const std::vector<RE::FormID>& prereqs)
{
    ProgressionNotify::Batch notices;   // sent when this returns, after every change below
    auto* effectivenessHook = SpellEffectivenessHook::GetSingleton();
    const auto& earlySettings = effectivenessHook->GetSettings();

    // Check if there's an existing learning target for this school
    auto it = m_learningTargets.find(school);
    if (it != m_learningTargets.end() && it->second != 0 && it->second != formId) {
        RE::FormID oldTargetId = it->second;

        // If old target was early-learned and not mastered, remove the spell from player
        if (earlySettings.enabled && effectivenessHook->IsEarlyLearnedSpell(oldTargetId)) {
            logger::info("ProgressionManager: Switching learning target in {} from {:08X} to {:08X}",
                school, oldTargetId, formId);

            // Remove the early spell from player (they can regain it by setting it as target again)
            SpellEffectivenessHook::RemoveEarlySpellFromPlayer(oldTargetId);
        }

        // Notify UI that old target is no longer being learned
        ProgressionNotify::TargetCleared(oldTargetId);

        // Clear old target's prerequisites
        m_targetPrerequisites.erase(oldTargetId);
    }

    DropTargetFromOtherSchools(school, formId);
    m_learningTargets[school] = formId;
    m_dirty = true;

    // Fire ModEvent: SpellLearning_TargetChanged (set)
    SendModEvent("SpellLearning_TargetChanged", school, 1.0f, RE::TESForm::LookupByID(formId));

    // Store the prerequisites for direct prereq detection
    if (!prereqs.empty()) {
        m_targetPrerequisites[formId] = prereqs;
        logger::info("ProgressionManager: Set {} direct prerequisites for {:08X}", prereqs.size(), formId);
    } else {
        m_targetPrerequisites.erase(formId);
    }

    logger::info("ProgressionManager: Set learning target for {} to {:08X}", school, formId);

    if (formId != 0) {
        EnsureProgress(formId);  // no entry for "no target" (the panel can send 0)
    }

    // If switching back to a spell with progress above early threshold, regrant it
    if (earlySettings.enabled && formId != 0) {
        effectivenessHook->CheckAndRegrantSpell(formId);
    }
}

RE::FormID ProgressionManager::GetLearningTarget(const std::string& school) const
{
    auto it = m_learningTargets.find(school);
    if (it != m_learningTargets.end()) {
        return it->second;
    }
    return 0;
}

void ProgressionManager::ClearLearningTarget(const std::string& school)
{
    ProgressionNotify::Batch notices;   // sent when this returns, after every change below
    // Check if there was an active target to clear
    RE::FormID oldTargetId = 0;
    auto it = m_learningTargets.find(school);
    if (it != m_learningTargets.end() && it->second != 0) {
        oldTargetId = it->second;

        // If old target was early-learned and not mastered, remove the spell from player
        auto* effectivenessHook = SpellEffectivenessHook::GetSingleton();
        const auto& earlySettings = effectivenessHook->GetSettings();

        if (earlySettings.enabled && effectivenessHook->IsEarlyLearnedSpell(oldTargetId)) {
            logger::info("ProgressionManager: Clearing learning target in {} - removing early spell {:08X}",
                school, oldTargetId);

            // Remove the early spell from player
            SpellEffectivenessHook::RemoveEarlySpellFromPlayer(oldTargetId);
        }

        // Clear prerequisites for this target
        m_targetPrerequisites.erase(oldTargetId);

        // Notify UI that learning target was cleared
        ProgressionNotify::TargetCleared(oldTargetId);
    }

    m_learningTargets.erase(school);
    m_dirty = true;
    // The spell under another school key as well (an older save): cleared there too
    DropTargetFromOtherSchools(school, oldTargetId);

    // Fire ModEvent: SpellLearning_TargetChanged (cleared)
    SendModEvent("SpellLearning_TargetChanged", school, 0.0f, nullptr);
}

void ProgressionManager::ClearLearningTargetForSpell(RE::FormID formId)
{
    // Clear every school whose target is this spell. The key is the one the
    // panel set (the tree node's school, from the spell's first effect), which
    // can differ from the costliest effect's school or be one the game calls
    // Unknown - so look the spell up by id instead of deriving its school.
    std::vector<std::string> schools;
    for (const auto& [school, targetId] : m_learningTargets) {
        if (targetId == formId) {
            schools.push_back(school);
        }
    }
    for (const auto& school : schools) {
        // The first clear drops the spell from its other keys too (DropTargetFromOtherSchools,
        // which sends their event): a key no longer holding it is not cleared a second time
        const auto now = m_learningTargets.find(school);
        if (now == m_learningTargets.end() || now->second != formId) continue;
        ClearLearningTarget(school);
        logger::info("ProgressionManager: Cleared learning target for {} (spell {:08X})", school, formId);
    }
}

// =============================================================================
// DIRECT PREREQUISITE CHECKING
// =============================================================================

bool ProgressionManager::IsDirectPrerequisite(RE::FormID targetSpellId, RE::FormID castSpellId) const
{
    auto it = m_targetPrerequisites.find(targetSpellId);
    if (it != m_targetPrerequisites.end() &&
        std::find(it->second.begin(), it->second.end(), castSpellId) != it->second.end()) {
        return true;
    }

    // The tree's own prerequisites too: a target set from a spell tome comes with
    // no prerequisite list (SetLearningTargetFromTome), but its tree links still count
    auto reqIt = m_prereqRequirements.find(targetSpellId);
    if (reqIt == m_prereqRequirements.end()) {
        return false;
    }
    const auto& reqs = reqIt->second;
    return std::find(reqs.hardPrereqs.begin(), reqs.hardPrereqs.end(), castSpellId) != reqs.hardPrereqs.end() ||
           std::find(reqs.softPrereqs.begin(), reqs.softPrereqs.end(), castSpellId) != reqs.softPrereqs.end();
}

bool ProgressionManager::IsDirectChild(RE::FormID targetSpellId, RE::FormID castSpellId) const
{
    auto it = m_prereqRequirements.find(castSpellId);
    if (it == m_prereqRequirements.end()) {
        return false;
    }
    const auto& reqs = it->second;
    return std::find(reqs.hardPrereqs.begin(), reqs.hardPrereqs.end(), targetSpellId) != reqs.hardPrereqs.end() ||
           std::find(reqs.softPrereqs.begin(), reqs.softPrereqs.end(), targetSpellId) != reqs.softPrereqs.end();
}

void ProgressionManager::SetTargetPrerequisites(RE::FormID targetSpellId, const std::vector<RE::FormID>& prereqs)
{
    if (prereqs.empty()) {
        m_targetPrerequisites.erase(targetSpellId);
    } else {
        m_targetPrerequisites[targetSpellId] = prereqs;
        logger::info("ProgressionManager: Set {} prerequisites for {:08X}", prereqs.size(), targetSpellId);
    }
}

// =============================================================================
// TREE PREREQUISITES - UNIFIED HARD/SOFT SYSTEM
// =============================================================================
// Hard prereqs: ALL must be mastered
// Soft prereqs: at least softNeeded must be mastered
// Single prereq = always hard (enforced by JS generation)

void ProgressionManager::SetPrereqRequirements(RE::FormID spellId, const PrereqRequirements& reqs)
{
    // The reverse index drops the old links and takes the new ones
    auto old = m_prereqRequirements.find(spellId);
    if (old != m_prereqRequirements.end()) {
        LinkRequiredBy(spellId, old->second, false);
    }
    LinkRequiredBy(spellId, reqs, true);
    m_treeSpells.insert(spellId);

    if (reqs.hardPrereqs.empty() && reqs.softPrereqs.empty()) {
        m_prereqRequirements.erase(spellId);
    } else {
        m_prereqRequirements[spellId] = reqs;
        logger::trace("ProgressionManager: Set prereqs for {:08X}: {} hard, {} soft (need {})",
            spellId, reqs.hardPrereqs.size(), reqs.softPrereqs.size(), reqs.softNeeded);
    }
}

// Legacy compatibility
void ProgressionManager::SetTreePrerequisites(RE::FormID spellId, const std::vector<RE::FormID>& prereqs)
{
    PrereqRequirements reqs;
    // Treat all as hard for legacy compatibility
    reqs.hardPrereqs = prereqs;
    reqs.softNeeded = 0;
    SetPrereqRequirements(spellId, reqs);
}

void ProgressionManager::ClearAllTreePrerequisites()
{
    m_prereqRequirements.clear();
    m_requiredBy.clear();
    m_treeSpells.clear();
    // Which spell a copy's tome counts for follows the tree (TreeSpellForTome):
    // a tome-in-inventory answer from the old tree must not stay
    if (auto* tomeHook = SpellTomeHook::GetSingleton()) tomeHook->InvalidateTomeInventoryCache();
    logger::info("ProgressionManager: Cleared all tree prerequisites");
}

bool ProgressionManager::IsTreeSpell(RE::FormID spellId) const
{
    return m_treeSpells.contains(spellId);
}

ProgressionManager::PrereqRequirements ProgressionManager::GetPrereqRequirements(RE::FormID spellId) const
{
    auto it = m_prereqRequirements.find(spellId);
    if (it != m_prereqRequirements.end()) {
        return it->second;
    }
    return PrereqRequirements{};
}

bool ProgressionManager::IsOwnSpellMastered(RE::FormID spellId) const
{
    // Check our progress tracking
    auto it = m_spellProgress.find(spellId);
    if (it != m_spellProgress.end()) {
        // Mastered if unlocked flag is set OR progress is at 100%
        if (it->second.unlocked || it->second.progressPercent >= 1.0f) {
            return true;
        }
    }

    // Also check if player knows the spell and it's NOT in our early-learned tracking
    // (meaning they learned it some other way, like vanilla)
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellId);
    if (player && spell && player->HasSpell(spell)) {
        auto* effectivenessHook = SpellEffectivenessHook::GetSingleton();
        if (!effectivenessHook || !effectivenessHook->IsEarlyLearnedSpell(spellId)) {
            // Player has the spell and it's not weakened = mastered
            return true;
        }
    }

    return false;
}

void ProgressionManager::LinkRequiredBy(RE::FormID spellId, const PrereqRequirements& reqs, bool link)
{
    // A spell both hard and soft counts once. The lists are a handful of ids,
    // so looking before adding beats a set allocated on every call.
    auto visit = [&](RE::FormID prereqId) {
        if (link) {
            auto& children = m_requiredBy[prereqId];
            if (std::ranges::find(children, spellId) == children.end()) {
                children.push_back(spellId);
            }
            return;
        }
        auto it = m_requiredBy.find(prereqId);
        if (it == m_requiredBy.end()) return;
        std::erase(it->second, spellId);
        if (it->second.empty()) m_requiredBy.erase(it);
    };
    for (const RE::FormID prereqId : reqs.hardPrereqs) visit(prereqId);
    for (const RE::FormID prereqId : reqs.softPrereqs) visit(prereqId);
}

const std::vector<RE::FormID>& ProgressionManager::GetRequiredBy(RE::FormID spellId) const
{
    static const std::vector<RE::FormID> kNone;
    auto it = m_requiredBy.find(spellId);
    return it != m_requiredBy.end() ? it->second : kNone;
}

bool ProgressionManager::IsUnlockedByKnownChild(RE::FormID spellId) const
{
    if (!m_xpSettings.reverseUnlock) return false;

    // Walk up from the spell: its children, and with reverseUnlockToRoot their
    // children too, until one is mastered. Visited set: a lock can point sideways.
    std::vector<RE::FormID> frontier{ spellId };
    std::unordered_set<RE::FormID> visited{ spellId };
    while (!frontier.empty()) {
        std::vector<RE::FormID> next;
        for (const RE::FormID current : frontier) {
            for (const RE::FormID childId : GetRequiredBy(current)) {
                if (visited.contains(childId)) continue;
                if (IsSpellMastered(childId)) return true;
                visited.insert(childId);
                next.push_back(childId);
            }
        }
        if (!m_xpSettings.reverseUnlockToRoot) break;  // direct children only
        frontier = std::move(next);
    }
    return false;
}

bool ProgressionManager::AreTreePrerequisitesMet(RE::FormID spellId) const
{
    auto reqs = GetPrereqRequirements(spellId);

    // No prerequisites = always available (root spell)
    if (reqs.hardPrereqs.empty() && reqs.softPrereqs.empty()) {
        return true;
    }

    // Check ALL hard prerequisites must be mastered
    bool ownMet = true;
    for (RE::FormID prereqId : reqs.hardPrereqs) {
        if (!IsSpellMastered(prereqId)) {
            ownMet = false;
            break;
        }
    }

    // Check soft prerequisites: need at least softNeeded mastered
    if (ownMet && reqs.softNeeded > 0 && !reqs.softPrereqs.empty()) {
        int masteredCount = 0;
        for (RE::FormID prereqId : reqs.softPrereqs) {
            if (IsSpellMastered(prereqId)) {
                masteredCount++;
            }
        }
        ownMet = masteredCount >= reqs.softNeeded;
    }

    // Or the player already knows a spell this one leads to: it is open whatever
    // its own prerequisites (a higher spell from a tome or a vendor opens the
    // step below it)
    return ownMet || IsUnlockedByKnownChild(spellId);
}

// =============================================================================
// SET LEARNING TARGET FROM TOME
// =============================================================================

void ProgressionManager::SetLearningTargetFromTome(const std::string& formIdStr, RE::SpellItem* spell)
{
    ProgressionNotify::Batch notices;   // sent when this returns, after every change below
    if (!spell) {
        // Try to look up by form ID
        const auto parsedId = FormIdText::Parse(formIdStr);
        if (!parsedId) {
            logger::error("ProgressionManager: '{}' is not a formId", formIdStr);
            return;
        }
        const RE::FormID formId = *parsedId;
        spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
        if (!spell) {
            logger::error("ProgressionManager: Could not find spell for formId {}", formIdStr);
            return;
        }
    }

    // The school key the panel uses for this spell (the tree node's school): a key
    // of its own here (the costliest effect's, before) left the panel unable to
    // see or clear the target
    const std::string schoolName = TargetSchoolOf(spell);
    if (schoolName.empty()) {
        logger::warn("ProgressionManager: Unknown school for spell {}", spell->GetName());
        return;
    }

    RE::FormID formId = spell->GetFormID();

    // =========================================================================
    // LEARNING MODE ENFORCEMENT
    // In "single" mode: clear ALL other learning targets before setting new one
    // In "perSchool" mode: only the same-school target is replaced (handled by SetLearningTarget)
    // =========================================================================
    if (m_xpSettings.learningMode == "single") {
        // Clear all learning targets in OTHER schools first
        std::vector<std::string> schoolsToClear;
        for (const auto& [existingSchool, existingTarget] : m_learningTargets) {
            if (existingSchool != schoolName && existingTarget != 0) {
                schoolsToClear.push_back(existingSchool);
            }
        }

        for (const auto& schoolToClear : schoolsToClear) {
            logger::info("ProgressionManager: Single mode - clearing {} target for new {} target",
                schoolToClear, schoolName);
            ClearLearningTarget(schoolToClear);
        }
    }

    EnsureProgress(formId);

    // Set as learning target (empty prereqs since tome provides direct learning)
    // This also handles clearing any existing target in the SAME school
    SetLearningTarget(schoolName, formId, {});

    logger::info("ProgressionManager: Set {} spell {} as learning target from tome",
        schoolName, spell->GetName());

    // Notify UI immediately so it knows this is a learning target BEFORE we grant the spell
    ProgressionNotify::TargetSet(schoolName, formId, spell->GetName());
}

// =============================================================================
// SPELL UNLOCKING
// =============================================================================

bool ProgressionManager::CanUnlock(RE::FormID formId) const
{
    auto it = m_spellProgress.find(formId);
    if (it == m_spellProgress.end()) {
        return false;
    }

    // Check XP requirement
    if (it->second.unlocked || it->second.progressPercent < 1.0f) {
        return false;
    }

    // The player knows a copy of it: granting this one too would put it in the book twice
    if (PlayerKnowsCopyOf(formId)) {
        logger::info("ProgressionManager: {:08X} not granted - the player knows a copy of it", formId);
        return false;
    }

    // Check tree prerequisites (hard/soft system)
    if (!AreTreePrerequisitesMet(formId)) {
        logger::trace("ProgressionManager: Cannot unlock {:08X} - prerequisites not met", formId);
        return false;
    }

    return true;
}

bool ProgressionManager::UnlockSpell(RE::FormID formId)
{
    ProgressionNotify::Batch notices;   // sent when this returns, after every change below
    if (!CanUnlock(formId)) {
        logger::warn("ProgressionManager: Cannot unlock {:08X} - not ready", formId);
        return false;
    }

    // Get player and spell
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);

    if (!player || !spell) {
        logger::error("ProgressionManager: Failed to get player or spell {:08X}", formId);
        return false;
    }

    // Add spell to player
    player->AddSpell(spell);

    // Mark as unlocked
    EnsureProgress(formId).unlocked = true;
    m_dirty = true;

    // Remove from early-learned tracking before clearing target, so ClearLearningTarget
    // doesn't call RemoveEarlySpellFromPlayer which would undo the AddSpell above.
    auto* effectivenessHook = SpellEffectivenessHook::GetSingleton();
    effectivenessHook->MarkMastered(formId);

    logger::info("ProgressionManager: Unlocked spell {} ({:08X})", spell->GetName(), formId);

    // The spell is learned: it is no longer any school's target
    ClearLearningTargetForSpell(formId);

    return true;
}

void ProgressionManager::RelockSpell(RE::FormID formId)
{
    ProgressionNotify::Batch notices;   // sent when this returns, after every change below
    // An early-learned spell: RemoveEarlySpellFromPlayer puts its own name and
    // descriptions back (the "(Learning - 40%)" ones) and drops the tracking the
    // co-save keeps. The caller already took the spell from the player, so it
    // finds nothing to remove. It does nothing for a spell not early-learned.
    SpellEffectivenessHook::RemoveEarlySpellFromPlayer(formId);
    // A relocked spell is no longer being learned either
    ClearLearningTargetForSpell(formId);
    // Back to nothing: its tome's XP too (the list is in the co-save, SLTX)
    SpellTomeHook::GetSingleton()->UnmarkTomeXPGranted(formId);

    auto it = m_spellProgress.find(formId);
    if (it == m_spellProgress.end()) {
        return;  // Nothing recorded (a spell the player simply knew): nothing to undo
    }

    // Undo what learning it set: the unlocked flag and the XP behind it. The
    // required XP stays (it comes from the tree). The co-save writes the entry
    // as it now is.
    SpellProgress& progress = it->second;
    progress.unlocked = false;
    progress.progressPercent = 0.0f;
    progress.xpFromAny = 0.0f;
    progress.xpFromSchool = 0.0f;
    progress.xpFromDirect = 0.0f;
    progress.xpFromSelf = 0.0f;
    progress.xpFromModded.clear();
    m_dirty = true;

    logger::info("ProgressionManager: Relocked {:08X} - progress cleared", formId);
}

bool ProgressionManager::IsUnlocked(RE::FormID formId) const
{
    auto it = m_spellProgress.find(formId);
    if (it != m_spellProgress.end()) {
        return it->second.unlocked;
    }
    return false;
}
