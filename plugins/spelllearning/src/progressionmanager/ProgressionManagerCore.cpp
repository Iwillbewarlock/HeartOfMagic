// =============================================================================
// ProgressionManagerCore.cpp — Singleton, utility helpers, progress reset
// =============================================================================

#include "ProgressionManager.h"
#include "EncodingUtils.h"
#include "SpellScanner.h"
#include "ProgressionNotify.h"
#include "SKSE/SKSE.h"

#include <algorithm>
#include <map>

ProgressionManager* ProgressionManager::GetSingleton()
{
    static ProgressionManager singleton;
    return &singleton;
}

// =============================================================================
// MOD EVENT HELPER
// =============================================================================

void ProgressionManager::SendModEvent(const char* eventName, const std::string& strArg, float numArg, RE::TESForm* sender)
{
    auto* eventSource = SKSE::GetModCallbackEventSource();
    if (!eventSource) {
        logger::warn("ProgressionManager: Cannot send ModEvent '{}' - event source not available", eventName);
        return;
    }
    SKSE::ModCallbackEvent modEvent(eventName, RE::BSFixedString(strArg.c_str()), numArg, sender);
    eventSource->SendEvent(&modEvent);
    logger::trace("ProgressionManager: Sent ModEvent '{}' (str={}, num={:.1f})", eventName, strArg, numArg);
}

// =============================================================================
// PROGRESS RESET
// =============================================================================

void ProgressionManager::ClearAllProgress()
{
    logger::info("ProgressionManager: Clearing all progress data");
    m_learningTargets.clear();
    m_spellProgress.clear();
    m_targetPrerequisites.clear();
    // This character's required XP numbers go; the tree's and the settings' overrides stay
    m_requiredXP.OnRevert();
    // The tree's prerequisites (m_prereqRequirements, m_requiredBy) stay: they
    // come from the tree, the same for every save, and the panel sends them only
    // when a tree is loaded or rebuilt (SetTreePrerequisites, which clears first).
    // Cleared here on every load, they were gone for the rest of the session -
    // no prerequisite checks, no spells opened by a known higher spell.
    m_dirty = false;
}

ProgressionManager::SpellProgress& ProgressionManager::EnsureProgress(RE::FormID formId)
{
    auto it = m_spellProgress.find(formId);
    if (it != m_spellProgress.end()) {
        return it->second;
    }
    // Before the entry exists, so GetRequiredXP does not find a placeholder
    SpellProgress progress;
    progress.requiredXP = GetRequiredXP(formId);
    return m_spellProgress.emplace(formId, std::move(progress)).first->second;
}

// =============================================================================
// ONE SCHOOL PER TARGET, XP LOG
// =============================================================================

std::string ProgressionManager::TargetSchoolOf(RE::SpellItem* spell)
{
    if (!spell) return {};
    auto school = SpellScanner::GetSpellSchool(spell);
    if (!SpellScanner::IsValidMagicSchool(school)) {
        auto* costliest = spell->GetCostliestEffectItem();
        school = (costliest && costliest->baseEffect) ? costliest->baseEffect->GetMagickSkill() : RE::ActorValue::kNone;
        if (!SpellScanner::IsValidMagicSchool(school)) return {};
    }
    return SpellScanner::GetSchoolName(school);
}

std::string ProgressionManager::EventSchoolOf(RE::FormID formId) const
{
    // The key the spell is a target under (what TargetChanged reported for it),
    // the smallest one should a save hold it twice; else the key it would get
    std::string held;
    for (const auto& [school, targetId] : m_learningTargets) {
        if (targetId == formId && (held.empty() || school < held)) held = school;
    }
    if (!held.empty()) return held;
    auto school = TargetSchoolOf(RE::TESForm::LookupByID<RE::SpellItem>(formId));
    return school.empty() ? std::string("Unknown") : school;
}

void ProgressionManager::DropTargetFromOtherSchools(const std::string& keepSchool, RE::FormID formId)
{
    if (formId == 0) return;
    for (auto it = m_learningTargets.begin(); it != m_learningTargets.end();) {
        if (it->first != keepSchool && it->second == formId) {
            logger::info("ProgressionManager: {:08X} was also the {} target - kept under {} only",
                formId, it->first, keepSchool.empty() ? "none" : keepSchool);
            // The panel holds its own copy of the keys: it forgets this one; and
            // Papyrus listeners hear the key cleared, as from ClearLearningTarget
            ProgressionNotify::TargetDropped(it->first, formId);
            SendModEvent("SpellLearning_TargetChanged", it->first, 0.0f, nullptr);
            it = m_learningTargets.erase(it);
            m_dirty = true;
        } else {
            ++it;
        }
    }
}

// Each target under one key, the key the panel uses (TargetSchoolOf) where it can be.
//
// Two phases so the result does not depend on the unordered_map's order: first
// collect every target with all its keys (by formId, keys sorted), then decide
// where each one goes, then apply. A single pass that moved targets one at a time
// left two spells saved under each other's school (A under B's key, B under A's)
// where they were: each found its key taken by the other, whichever came first.
//
// The rule: a target moves to its canonical key when no target that STAYS holds
// that key - a spell keeps a key it already has rather than lose it to one moving
// in. The movers are worked out to a fixed point: start with every target whose
// canonical key is not one of its own; drop a mover whose key a stayer holds, or
// that a mover with a smaller formId also wants; repeat until nothing drops (each
// pass only drops, so at most one pass per target). The swap resolves (both move,
// each key is freed by the other), and no target is ever lost: a stayer keeps its
// canonical key if it has it, else its first key, and no mover can be headed for
// a stayer's key. A mover dropped for a smaller one that is itself dropped later
// stays where it was - still one key, only not the canonical one (rare).
void ProgressionManager::CanonicalizeLearningTargets()
{
    ProgressionNotify::Batch notices;   // sent when this returns, after every change below
    struct Target
    {
        RE::FormID formId = 0;
        std::vector<std::string> keys;   // sorted
        std::string canonical;           // "" when the spell has no school
        bool moves = false;
    };

    // Phase 1: collect
    std::map<RE::FormID, std::vector<std::string>> keysOf;   // ordered by formId
    for (const auto& [school, formId] : m_learningTargets) {
        if (formId != 0) keysOf[formId].push_back(school);
    }
    std::vector<Target> targets;
    std::unordered_map<std::string, std::size_t> holderOf;   // saved key -> index in targets
    for (auto& [formId, keys] : keysOf) {
        std::sort(keys.begin(), keys.end());
        Target target;
        target.formId = formId;
        target.keys = keys;
        target.canonical = TargetSchoolOf(RE::TESForm::LookupByID<RE::SpellItem>(formId));
        target.moves = !target.canonical.empty() &&
                       std::find(keys.begin(), keys.end(), target.canonical) == keys.end();
        for (const auto& key : keys) holderOf[key] = targets.size();
        targets.push_back(std::move(target));
    }

    // Phase 2: who moves (fixed point, see above)
    for (bool changed = true; changed;) {
        changed = false;
        std::unordered_map<std::string, RE::FormID> claimedBy;   // canonical key -> first mover
        for (auto& target : targets) {
            if (!target.moves) continue;
            auto holder = holderOf.find(target.canonical);
            const bool stayerHolds = holder != holderOf.end() && !targets[holder->second].moves;
            const bool claimed = !claimedBy.emplace(target.canonical, target.formId).second;
            if (stayerHolds || claimed) {
                target.moves = false;
                changed = true;
            }
        }
    }

    // Phase 3: the new keys. A key with no spell (0) stays unless a target takes it
    std::unordered_map<std::string, RE::FormID> placed;
    for (const auto& [school, formId] : m_learningTargets) {
        if (formId == 0) placed[school] = 0;
    }
    for (const auto& target : targets) {
        std::string keep = target.keys.front();
        if (target.moves || std::find(target.keys.begin(), target.keys.end(), target.canonical) != target.keys.end()) {
            keep = target.canonical;
        }
        placed[keep] = target.formId;
        if (target.keys.size() > 1 || keep != target.keys.front()) {
            std::string held;
            for (const auto& key : target.keys) held += (held.empty() ? "" : ", ") + key;
            logger::info("ProgressionManager: save had {:08X} as the target of {} - kept under {}",
                target.formId, held, keep);
        }
    }

    // Phase 4: apply, and tell the panel and Papyrus listeners about each key that
    // changed, the way SetLearningTarget / ClearLearningTarget do (drops first, so
    // the panel never forgets a key just given). This runs from the save load
    // callback: the view gets the notices if it is up (it reloads the targets when
    // opened anyway) and the ModEvents are queued for scripts like any other.
    std::vector<std::pair<std::string, RE::FormID>> before(m_learningTargets.begin(), m_learningTargets.end());
    std::vector<std::pair<std::string, RE::FormID>> after(placed.begin(), placed.end());
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    if (before == after) return;
    for (const auto& [school, formId] : before) {
        auto now = placed.find(school);
        if (formId == 0 || (now != placed.end() && now->second == formId)) continue;
        ProgressionNotify::TargetDropped(school, formId);
        SendModEvent("SpellLearning_TargetChanged", school, 0.0f, nullptr);
    }
    const auto old = std::move(m_learningTargets);
    m_learningTargets = std::move(placed);
    m_dirty = true;
    for (const auto& [school, formId] : after) {
        auto was = old.find(school);
        if (formId == 0 || (was != old.end() && was->second == formId)) continue;
        auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
        ProgressionNotify::TargetSet(school, formId, spell ? spell->GetName() : "");
        SendModEvent("SpellLearning_TargetChanged", school, 1.0f, spell);
    }
}

void ProgressionManager::LogXPGrant(std::string_view a_path, RE::FormID targetId, float amount, std::string_view a_detail,
                                    bool a_debugLevel)
{
    auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(targetId);
    const auto line = std::format("XP: +{:.2f} to {} ({:08X}) from {}{}{}", amount, spell ? spell->GetName() : "?",
                                  targetId, a_path, a_detail.empty() ? "" : " - ", a_detail);
    if (a_debugLevel) {
        logger::debug("{}", line);
    } else {
        logger::info("{}", line);
    }
}
