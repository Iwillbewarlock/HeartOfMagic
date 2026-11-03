// =============================================================================
// ProgressionManagerRequired.cpp — required XP per spell (tier, tree, overrides,
// reverse unlock share) and the XP gain rates
// =============================================================================
// Moved out of ProgressionManagerXP.cpp (600-line limit) unchanged.

#include "ProgressionManager.h"
#include "AsciiText.h"
#include "FormIdText.h"
#include "ProgressionMath.h"
#include "SpellScanner.h"

// =============================================================================
// REQUIRED XP LOOKUP
// =============================================================================

float ProgressionManager::GetRequiredXP(const std::string& formIdStr) const
{
    // Parse hex string to FormID (supports "0x" prefix)
    const auto parsedId = FormIdText::Parse(formIdStr);
    if (!parsedId) {
        logger::error("ProgressionManager: '{}' is not a formId", formIdStr);
        return 0.0f;
    }
    const RE::FormID formId = *parsedId;

    return GetRequiredXP(formId);
}

float ProgressionManager::GetRequiredXP(RE::FormID formId) const
{
    auto it = m_spellProgress.find(formId);
    if (it != m_spellProgress.end() && it->second.requiredXP > 0) {
        return it->second.requiredXP;
    }
    return BaseRequiredXP(formId);
}

float ProgressionManager::BaseRequiredXP(RE::FormID formId) const
{
    if (m_requiredXP.HasCharacterValue(formId)) return m_requiredXP.For(formId, 0.0f, false, 1.0f);

    // The panel's getRequiredXPForNode: override, else the tree's, else the tier's,
    // times the reverse unlock share - the same number for a spell the panel has
    // not sent (a tome read before the spell was ever a target, a save just loaded).
    float tierXP = m_xpSettings.xpNovice;  // Default to novice
    float share = 1.0f;
    bool opened = false;
    auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
    if (spell) {
        // Use perk-based tier detection (fixes modded master spells with minimumSkill=0)
        const std::string tier = SpellScanner::DetermineSpellTier(spell);
        tierXP = GetXPForTier(tier);
        opened = IsUnlockedByKnownChild(formId);
        if (opened) share = GetReverseUnlockXPShare(tier);
    }
    const float required = m_requiredXP.For(formId, tierXP, opened, share);
    return required;
}

ProgressionManager::XPGainRates ProgressionManager::GetGainRates(RE::FormID targetId) const
{
    if (m_xpSettings.reverseUnlock && m_xpSettings.reverseXPSeparate && IsUnlockedByKnownChild(targetId)) {
        return m_xpSettings.reverseGain;
    }
    XPGainRates rates;
    rates.globalMultiplier = m_xpSettings.globalMultiplier;
    rates.multiplierDirect = m_xpSettings.multiplierDirect;
    rates.multiplierSchool = m_xpSettings.multiplierSchool;
    rates.multiplierAny = m_xpSettings.multiplierAny;
    rates.capAny = m_xpSettings.capAny;
    rates.capSchool = m_xpSettings.capSchool;
    rates.capDirect = m_xpSettings.capDirect;
    return rates;
}

float ProgressionManager::GetReverseUnlockXPShare(const std::string& tier) const
{
    const std::string t = AsciiText::ToLower(tier);
    if (t == "apprentice") return m_xpSettings.reverseUnlockXPApprentice;
    if (t == "adept") return m_xpSettings.reverseUnlockXPAdept;
    if (t == "expert") return m_xpSettings.reverseUnlockXPExpert;
    if (t == "master") return m_xpSettings.reverseUnlockXPMaster;
    return m_xpSettings.reverseUnlockXPNovice;
}

// =============================================================================
// REQUIRED XP BY KIND (tree, settings overrides, this character)
// =============================================================================

void ProgressionManager::SetRequiredXP(RE::FormID formId, float required, RequiredXP::Kind kind)
{
    if (required <= 0.0f) {
        logger::warn("ProgressionManager: SetRequiredXP called with non-positive value {:.1f} for {:08X}, clamping to 1.0", required, formId);
        required = 1.0f;
    }
    m_requiredXP.Set(kind, formId, required);
    // A tree number counts only where this character and the overrides have none
    auto& progress = EnsureProgress(formId);
    const float before = progress.requiredXP;
    progress.requiredXP = kind == RequiredXP::Kind::kCharacter ? required : BaseRequiredXP(formId);
    // The per-source counts keep their share of the required XP, both ways (ProgressionMath::RescaleTrackers)
    ProgressionMath::RescaleSettlingFirst(progress, before, progress.requiredXP, m_treeRequiredApplied);
}

void ProgressionManager::SetRequiredXPOverrides(RequiredXP::Table::Map overrides)
{
    m_requiredXP.SetOverrides(std::move(overrides));
    std::size_t updated = 0;
    for (auto& [formId, progress] : m_spellProgress) {
        if (m_requiredXP.HasCharacterValue(formId)) continue;
        const float before = progress.requiredXP;
        progress.requiredXP = BaseRequiredXP(formId);
        ProgressionMath::RescaleSettlingFirst(progress, before, progress.requiredXP, m_treeRequiredApplied);
        ++updated;
    }
    logger::info("ProgressionManager: {} per-spell XP overrides from the settings ({} entries sized again)",
                 m_requiredXP.OverrideCount(), updated);
}

void ProgressionManager::SettlePendingTrackerFits()
{
    // From now on this session's loads settle at their end (EndLoad): no tree read follows them
    m_treeRequiredApplied = true;
    std::size_t fitted = 0;
    for (auto& [formId, progress] : m_spellProgress) {
        if (ProgressionMath::SettlePendingFit(progress)) ++fitted;
    }
    if (fitted > 0) {
        m_dirty = true;
        logger::info("ProgressionManager: per-source XP counts of {} spells from an older save fitted to the XP they hold",
                     fitted);
    }
}
