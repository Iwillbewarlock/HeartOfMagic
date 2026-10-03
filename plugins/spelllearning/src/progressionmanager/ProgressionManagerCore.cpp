// =============================================================================
// ProgressionManagerCore.cpp — Singleton, utility helpers, progress reset
// =============================================================================

#include "ProgressionManager.h"
#include "EncodingUtils.h"
#include "SpellScanner.h"
#include "SKSE/SKSE.h"

ProgressionManager* ProgressionManager::GetSingleton()
{
    static ProgressionManager singleton;
    return &singleton;
}

std::filesystem::path ProgressionManager::GetProgressFilePath() const
{
    if (m_currentSaveName.empty()) {
        logger::warn("ProgressionManager: Save name is empty, using fallback filename");
    }
    std::string safeName = EncodingUtils::SanitizeFilename(m_currentSaveName);
    // Defense-in-depth: reject if sanitized name still contains ".."
    if (safeName.find("..") != std::string::npos) {
        logger::error("ProgressionManager: Rejected suspicious save name: {}", m_currentSaveName);
        safeName = "_unnamed";
    }
    std::string filename = "progress_" + safeName + ".json";
    return std::filesystem::path("Data/SKSE/Plugins/SpellLearning") / filename;
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
    // The tree's prerequisites (m_prereqRequirements, m_requiredBy) stay: they
    // come from the tree, the same for every save, and the panel sends them only
    // when a tree is loaded or rebuilt (SetTreePrerequisites, which clears first).
    // Cleared here on every load, they were gone for the rest of the session -
    // no prerequisite checks, no spells opened by a known higher spell.
    m_dirty = false;
}

// =============================================================================
// ONE SCHOOL PER TARGET, XP LOG
// =============================================================================

void ProgressionManager::DropTargetFromOtherSchools(const std::string& keepSchool, RE::FormID formId)
{
    if (formId == 0) return;
    for (auto it = m_learningTargets.begin(); it != m_learningTargets.end();) {
        if (it->first != keepSchool && it->second == formId) {
            logger::info("ProgressionManager: {:08X} was also the {} target - kept under {} only",
                formId, it->first, keepSchool);
            it = m_learningTargets.erase(it);
            m_dirty = true;
        } else {
            ++it;
        }
    }
}

void ProgressionManager::DedupeLearningTargets()
{
    std::unordered_map<RE::FormID, std::vector<std::string>> schoolsOf;
    for (const auto& [school, formId] : m_learningTargets) {
        if (formId != 0) schoolsOf[formId].push_back(school);
    }
    for (auto& [formId, schools] : schoolsOf) {
        if (schools.size() < 2) continue;
        std::sort(schools.begin(), schools.end());
        std::string keep = schools.front();
        // Its own school when it is held there (the costliest effect's, as a tome sets it)
        if (auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId)) {
            auto* effect = spell->GetCostliestEffectItem();
            if (effect && effect->baseEffect) {
                const auto own = SpellScanner::GetSchoolName(effect->baseEffect->GetMagickSkill());
                if (std::find(schools.begin(), schools.end(), own) != schools.end()) keep = own;
            }
        }
        logger::info("ProgressionManager: save had {:08X} as the target of {} schools - kept under {}",
            formId, schools.size(), keep);
        DropTargetFromOtherSchools(keep, formId);
    }
}

void ProgressionManager::LogXPGrant(std::string_view a_path, RE::FormID targetId, float amount, std::string_view a_detail)
{
    auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(targetId);
    logger::info("XP: +{:.2f} to {} ({:08X}) from {}{}{}", amount, spell ? spell->GetName() : "?", targetId,
                 a_path, a_detail.empty() ? "" : " - ", a_detail);
}
