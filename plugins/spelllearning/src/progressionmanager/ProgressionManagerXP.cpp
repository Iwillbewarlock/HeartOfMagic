// =============================================================================
// ProgressionManagerXP.cpp — XP settings, spell cast XP, progress tracking
// =============================================================================

#include "ProgressionManager.h"
#include "AsciiText.h"
#include "FormIdText.h"
#include "HudNotify.h"
#include "ProgressionNotify.h"
#include "SpellEffectivenessHook.h"
#include "SpellTomeHook.h"
#include "SpellScanner.h"
#include "ProgressionMath.h"

// =============================================================================
// XP SETTINGS
// =============================================================================

void ProgressionManager::SetXPSettings(const XPSettings& settings)
{
    m_xpSettings = settings;
    logger::info("ProgressionManager: XP settings updated - mode: {}, global: x{:.0f}, direct: {:.0f}%, school: {:.0f}%, any: {:.0f}%",
        m_xpSettings.learningMode,
        m_xpSettings.globalMultiplier,
        m_xpSettings.multiplierDirect * 100,
        m_xpSettings.multiplierSchool * 100,
        m_xpSettings.multiplierAny * 100);
    logger::debug("ProgressionManager: XP caps - any: {:.0f}%, school: {:.0f}%, direct: {:.0f}%",
        m_xpSettings.capAny,
        m_xpSettings.capSchool,
        m_xpSettings.capDirect);
    logger::debug("ProgressionManager: Tier XP - Novice: {:.0f}, Apprentice: {:.0f}, Adept: {:.0f}, Expert: {:.0f}, Master: {:.0f}",
        m_xpSettings.xpNovice,
        m_xpSettings.xpApprentice,
        m_xpSettings.xpAdept,
        m_xpSettings.xpExpert,
        m_xpSettings.xpMaster);
}

float ProgressionManager::GetXPForTier(const std::string& tier) const
{
    const std::string tierLower = AsciiText::ToLower(tier);

    if (tierLower == "novice") return m_xpSettings.xpNovice;
    if (tierLower == "apprentice") return m_xpSettings.xpApprentice;
    if (tierLower == "adept") return m_xpSettings.xpAdept;
    if (tierLower == "expert") return m_xpSettings.xpExpert;
    if (tierLower == "master") return m_xpSettings.xpMaster;

    return m_xpSettings.xpNovice;  // Default to novice
}

void ProgressionManager::SetSpellXP(RE::FormID formId, float xp)
{
    // Direct XP manipulation (cheat mode, API, Papyrus). Clamped to 0..required:
    // a progress over 1.0 read as 200% in the panel and made the next XP's maths
    // (current XP = progress x required) start above the requirement.
    auto& progress = EnsureProgress(formId);
    progress.progressPercent = ProgressionMath::ProgressForXP(xp, progress.requiredXP);
    m_dirty = true;
    SpellEffectivenessHook::GetSingleton()->SyncProgress(formId, progress.progressPercent);

    logger::info("ProgressionManager: SetSpellXP {:08X} to {:.0f} XP asked, now {:.0f} / {:.0f} ({:.1f}%)",
        formId, xp, progress.GetCurrentXP(), progress.requiredXP, progress.progressPercent * 100.0f);
}

// =============================================================================
// XP TRACKING (spell cast events)
// =============================================================================

void ProgressionManager::OnSpellCast(const std::string& school, RE::FormID castSpellId, float baseXP,
                                     std::string_view a_path)
{
    // Get early learning settings
    auto* effectivenessHook = SpellEffectivenessHook::GetSingleton();
    const auto& earlySettings = effectivenessHook->GetSettings();

    // Iterate through all learning targets and grant XP based on settings. A copy:
    // AddXP clears targets when a spell is mastered (every school targeting it),
    // which would erase from the map under the loop. A target cleared or changed
    // meanwhile is skipped, so a mastered spell gets no more XP from this cast.
    // A spell held under two school keys (a save from before DropTargetFromOtherSchools)
    // gets XP from one cast once
    const std::vector<std::pair<std::string, RE::FormID>> targets(m_learningTargets.begin(), m_learningTargets.end());
    std::vector<RE::FormID> credited;
    credited.reserve(targets.size());
    float totalXP = 0.0f;
    int duplicateKeys = 0;   // the same spell met again under a second school key
    const auto* castSpell = RE::TESForm::LookupByID<RE::SpellItem>(castSpellId);
    const char* castName = castSpell ? castSpell->GetName() : "?";
    for (const auto& [targetSchool, targetId] : targets) {
        auto current = m_learningTargets.find(targetSchool);
        if (current == m_learningTargets.end() || current->second != targetId) continue;
        if (targetId == 0) continue;
        if (std::find(credited.begin(), credited.end(), targetId) != credited.end()) {
            ++duplicateKeys;
            continue;
        }

        // Check if target is already fully mastered
        auto progress = GetProgress(targetId);
        if (progress.unlocked && progress.progressPercent >= 1.0f) continue;

        // Spells learned downward can have their own rates (GetGainRates)
        const XPGainRates rates = GetGainRates(targetId);
        const float adjustedBaseXP = baseXP * rates.globalMultiplier;

        // =========================================================================
        // SELF-CAST REQUIREMENT CHECK
        // =========================================================================
        bool isCastingLearningTarget = (castSpellId == targetId);
        float currentProgress = progress.progressPercent * 100.0f;  // Convert to percentage

        if (earlySettings.enabled && currentProgress >= earlySettings.selfCastRequiredAt) {
            // After selfCastRequiredAt threshold, ONLY self-casting grants XP
            if (!isCastingLearningTarget) {
                logger::trace("ProgressionManager: Progress {:.0f}% >= selfCastRequiredAt {:.0f}% - "
                    "only self-casting grants XP (cast spell {:08X} != target {:08X})",
                    currentProgress, earlySettings.selfCastRequiredAt, castSpellId, targetId);
                continue;  // Skip this target, no XP from non-self casts
            }
        }

        // =========================================================================
        // DETERMINE XP SOURCE AND CHECK CAPS
        // =========================================================================
        float multiplier = 0.0f;
        enum class XPSource { Any, School, Direct, Self };
        XPSource source = XPSource::Any;

        if (isCastingLearningTarget) {
            // Casting the learning target itself - SELF source
            source = XPSource::Self;
            if (earlySettings.enabled) {
                // After early unlock, casting the spell itself grants bonus XP
                if (effectivenessHook->IsEarlyLearnedSpell(targetId)) {
                    multiplier = rates.multiplierDirect * earlySettings.selfCastXPMultiplier;
                    logger::trace("ProgressionManager: Self-casting early-learned spell - multiplier {:.0f}% x {:.1f} = {:.0f}%",
                        rates.multiplierDirect * 100, earlySettings.selfCastXPMultiplier, multiplier * 100);
                } else {
                    // Spell not yet early-unlocked - use direct multiplier
                    multiplier = rates.multiplierDirect;
                }
            } else {
                multiplier = rates.multiplierDirect;
            }
        } else if (targetSchool == school) {
            // Same school as cast spell - check if DIRECT prereq or just same SCHOOL.
            // Direct runs both ways: a spell leading to the target, or - learning
            // downward - the known spell above it that opened it
            if (IsDirectPrerequisite(targetId, castSpellId) ||
                (m_xpSettings.reverseUnlock && IsDirectChild(targetId, castSpellId))) {
                // Cast spell is a direct link of the target
                source = XPSource::Direct;
                multiplier = rates.multiplierDirect;
                logger::trace("ProgressionManager: Direct prereq cast {:08X} for target {:08X} - using direct multiplier {:.0f}%",
                    castSpellId, targetId, multiplier * 100);
            } else {
                // Same school but not a direct prereq
                source = XPSource::School;
                multiplier = rates.multiplierSchool;
                logger::trace("ProgressionManager: Same school cast - using school multiplier {:.0f}%", multiplier * 100);
            }
        } else {
            // Different school - ANY source
            source = XPSource::Any;
            multiplier = rates.multiplierAny;
            logger::trace("ProgressionManager: Different school cast - using any multiplier {:.0f}%", multiplier * 100);
        }

        // Skip if multiplier is 0
        if (multiplier <= 0.0f) continue;

        // Calculate XP gain
        float xpGain = adjustedBaseXP * multiplier;

        // =========================================================================
        // TOME INVENTORY BOOST - bonus XP if player has the spell tome in inventory
        // =========================================================================
        auto* tomeHook = SpellTomeHook::GetSingleton();
        if (tomeHook && tomeHook->GetSettings().tomeInventoryBoost) {
            float tomeBoost = tomeHook->GetXPMultiplier(targetId);
            if (tomeBoost > 1.0f) {
                xpGain *= tomeBoost;
                logger::trace("ProgressionManager: Tome inventory boost applied to {:08X}, xpGain = {:.1f}",
                    targetId, xpGain);
            }
        }

        // =========================================================================
        // CHECK XP CAPS - limit contribution from each source type
        // =========================================================================
        auto& progRef = EnsureProgress(targetId);
        float maxXPFromSource = 0.0f;
        float currentXPFromSource = 0.0f;

        switch (source) {
            case XPSource::Any:
                maxXPFromSource = progRef.requiredXP * (rates.capAny / 100.0f);
                currentXPFromSource = progRef.xpFromAny;
                break;
            case XPSource::School:
                maxXPFromSource = progRef.requiredXP * (rates.capSchool / 100.0f);
                currentXPFromSource = progRef.xpFromSchool;
                break;
            case XPSource::Direct:
                maxXPFromSource = progRef.requiredXP * (rates.capDirect / 100.0f);
                currentXPFromSource = progRef.xpFromDirect;
                break;
            case XPSource::Self:
                // Self-casting has no cap - can go to 100%
                maxXPFromSource = progRef.requiredXP;
                currentXPFromSource = progRef.xpFromSelf;
                break;
        }

        // Clamp XP gain to not exceed cap
        float remainingCap = maxXPFromSource - currentXPFromSource;
        if (remainingCap <= 0.0f) {
            logger::trace("ProgressionManager: Source cap reached for {:08X} (source: {}, cap: {:.1f}%)",
                targetId,
                source == XPSource::Any ? "any" : source == XPSource::School ? "school" : source == XPSource::Direct ? "direct" : "self",
                source == XPSource::Any ? rates.capAny : source == XPSource::School ? rates.capSchool : source == XPSource::Direct ? rates.capDirect : 100.0f);
            continue;  // Skip this target, cap reached
        }

        float actualXPGain = (std::min)(xpGain, remainingCap);

        credited.push_back(targetId);
        const char* sourceName = source == XPSource::Any ? "any" : source == XPSource::School ? "school" :
                                 source == XPSource::Direct ? "direct" : "self";

        // Track XP by source
        switch (source) {
            case XPSource::Any:    progRef.xpFromAny += actualXPGain; break;
            case XPSource::School: progRef.xpFromSchool += actualXPGain; break;
            case XPSource::Direct: progRef.xpFromDirect += actualXPGain; break;
            case XPSource::Self:   progRef.xpFromSelf += actualXPGain; break;
        }

        // In "single" mode, only the first learning target gets XP
        // In "perSchool" mode, each school's target gets XP independently.
        // Per target at debug level: the cast's one info line follows the loop
        AddXP(targetId, actualXPGain);
        totalXP += actualXPGain;
        const bool single = m_xpSettings.learningMode == "single";
        LogXPGrant(a_path, targetId, actualXPGain,
                   single ? std::format("cast {} ({:08X}), {} rate, single mode", castName, castSpellId, sourceName)
                          : std::format("cast {} ({:08X}), {} rate, {} target", castName, castSpellId, sourceName,
                                        targetSchool),
                   true);
        if (single) break;  // Only one target in single mode
    }

    // One info line per cast that earned something (a cast earning nothing stays at debug)
    if (!credited.empty()) {
        logger::info("XP: cast {} ({:08X}) from {}: +{:.2f} to {} target(s), {} duplicate key(s) skipped",
                     castName, castSpellId, a_path, totalXP, credited.size(), duplicateKeys);
    } else {
        logger::debug("XP: cast {} ({:08X}) from {}: no target credited", castName, castSpellId, a_path);
    }
}

// =============================================================================
// XP GRANTING (core AddXP logic with early learning and mastery)
// =============================================================================

void ProgressionManager::AddXP(RE::FormID targetSpellId, float amount)
{
    ProgressionNotify::Batch notices;   // sent when this returns, after every change below
    auto& progress = EnsureProgress(targetSpellId);

    // If already fully mastered (unlocked in old system OR 100% in new system), no more XP needed
    if (progress.unlocked && progress.progressPercent >= 1.0f) {
        return;
    }

    // requiredXP == 0 means XP requirements not initialized: no progress and no
    // instant mastery, and the progress the save holds stays as it is
    if (!(progress.requiredXP > 0.0f)) {
        logger::warn("ProgressionManager: AddXP for {:08X} but requiredXP is 0 — XP update ignored", targetSpellId);
        return;
    }

    auto* effectivenessHook = SpellEffectivenessHook::GetSingleton();
    float oldXP = progress.GetCurrentXP();
    float oldProgress = progress.progressPercent;
    progress.progressPercent = ProgressionMath::ProgressAfterXP(oldProgress, amount, progress.requiredXP);
    const float newXP = progress.GetCurrentXP();
    m_dirty = true;
    // The effect hook's copy, before the power step below reads it
    effectivenessHook->SyncProgress(targetSpellId, progress.progressPercent);

    // PERFORMANCE: Use trace for frequent XP updates (only visible with verbose logging)
    logger::trace("ProgressionManager: Spell {:08X} XP: {:.1f} -> {:.1f} / {:.1f} ({:.1f}%)",
        targetSpellId, oldXP, newXP, progress.requiredXP, progress.progressPercent * 100.0f);

    // =========================================================================
    // EARLY SPELL LEARNING - Grant spell at threshold, master at 100%
    // Power steps: 25%, 40%, 55%, 70%, 85%, 100%
    // =========================================================================
    const auto& earlySettings = effectivenessHook->GetSettings();

    if (earlySettings.enabled) {
        float unlockThreshold = earlySettings.unlockThreshold / 100.0f;  // Convert to 0-1
        float newProgress = progress.progressPercent;

        // Check if we just crossed the unlock threshold (first grant)
        if (oldProgress < unlockThreshold && newProgress >= unlockThreshold) {
            // Grant the spell early (nerfed) by the one grant rule: not when the player
            // knows a copy of it (CanUnlock), not while ISL holds it for its study
            auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(targetSpellId);
            effectivenessHook->ReleaseHeldIfOwned(spell);
            if (effectivenessHook->ShouldGrantEarly(spell)) {
                SpellEffectivenessHook::GrantEarlySpell(spell);
                logger::info("ProgressionManager: Early granted spell {:08X} at {:.0f}% progress",
                    targetSpellId, newProgress * 100.0f);

                // Fire ModEvent: SpellLearning_SpellEarlyGranted, naming the school the way
                // TargetChanged did for this spell (its target key), not the costliest effect's
                SendModEvent("SpellLearning_SpellEarlyGranted", EventSchoolOf(targetSpellId), newProgress * 100.0f, spell);
            }
        }

        // Read before the step check: reaching the last step there masters the
        // spell's effect and drops it from the early-learned set. A spell held for
        // ISL (in the set, not yet taught) is neither renamed nor mastered until ISL
        // teaches it - the same rule as OnStudyProgress (ProgressionMath::ShouldMaster)
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* targetSpell = RE::TESForm::LookupByID<RE::SpellItem>(targetSpellId);
        // ISL taught it without telling us (no OnStudyComplete): no longer held, on any
        // cast, not only at the threshold - or its steps and name stopped for good
        effectivenessHook->ReleaseHeldIfOwned(targetSpell);
        const bool earlyLearned = effectivenessHook->IsEarlyLearnedSpell(targetSpellId);
        const bool playerHasSpell = player && targetSpell && player->HasSpell(targetSpell);
        const bool heldForISL = (earlyLearned && !playerHasSpell) || effectivenessHook->IsHeldForISL(targetSpellId);

        // Check if power step changed (for display updates)
        // This only updates name/description when crossing step thresholds
        bool stepChanged = !heldForISL && effectivenessHook->CheckAndUpdatePowerStep(targetSpellId);
        if (stepChanged) {
            int currentStep = effectivenessHook->GetCurrentPowerStep(targetSpellId);
            float effectiveness = effectivenessHook->GetSteppedEffectiveness(targetSpellId);
            logger::info("ProgressionManager: Spell {:08X} power step changed to {} ({}%)",
                targetSpellId, effectivenessHook->GetPowerStepLabel(currentStep),
                static_cast<int>(effectiveness * 100));

            // Show notification for power step increase
            auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(targetSpellId);
            if (spell) {
                HudNotify::Show("hud.powerIncreased", "{{spell}} power increased to {{percent}}%",
                    { { "spell", spell->GetName() },
                      { "percent", std::to_string(static_cast<int>(effectiveness * 100)) } });

                // Fire ModEvent: SpellLearning_ProgressMilestone
                SendModEvent("SpellLearning_ProgressMilestone",
                    effectivenessHook->GetPowerStepLabel(currentStep),
                    effectiveness * 100.0f, spell);
            }
        }

        // 100% and not yet unlocked: just reached, or reached before without
        // mastering (ProgressionMath::ShouldMaster). A held spell counts as early
        // learned there even out of the early set (a target switch): never mastered
        // before ISL teaches it
        if (ProgressionMath::ShouldMaster(oldProgress, newProgress, progress.unlocked,
                                          earlyLearned || heldForISL, playerHasSpell)) {
            // Mark as mastered - this removes the nerf
            effectivenessHook->MarkMastered(targetSpellId);
            progress.unlocked = true;  // Also mark as unlocked in progress system

            logger::info("ProgressionManager: Spell {:08X} MASTERED - nerf removed!", targetSpellId);

            // Show mastery notification
            auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(targetSpellId);
            if (spell) {
                HudNotify::Show("hud.mastered", "{{spell}} MASTERED! Full power unlocked.",
                    { { "spell", spell->GetName() } });

                // Fire ModEvent: SpellLearning_SpellMastered, naming the school the way
                // TargetChanged did for this spell (its target key), not the costliest effect's
                SendModEvent("SpellLearning_SpellMastered", EventSchoolOf(targetSpellId), 0.0f, spell);
            }

            // Clear learning target for this school (spell is mastered)
            ClearLearningTargetForSpell(targetSpellId);

            // Notify UI that spell is fully unlocked/mastered (not just "ready")
            ProgressionNotify::SpellUnlocked(targetSpellId, true);
        }
    } else {
        // Old behavior: notify when ready to unlock (100%)
        if (progress.progressPercent >= 1.0f) {
            logger::info("ProgressionManager: Spell {:08X} is ready to unlock!", targetSpellId);
            ProgressionNotify::SpellReady(targetSpellId);
        }
    }

    // Notify UI of progress update
    ProgressionNotify::Progress(targetSpellId, newXP, progress.requiredXP);
}

void ProgressionManager::AddXP(const std::string& formIdStr, float amount)
{
    // Parse hex string to FormID (supports "0x" prefix)
    const auto parsedId = FormIdText::Parse(formIdStr);
    if (!parsedId) {
        logger::error("ProgressionManager: '{}' is not a formId", formIdStr);
        return;
    }
    const RE::FormID formId = *parsedId;

    AddXP(formId, amount);
}

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
// SPELL AVAILABILITY & PROGRESS QUERIES
// =============================================================================

bool ProgressionManager::IsSpellAvailableToLearn(const std::string& formIdStr) const
{
    const auto parsedId = FormIdText::Parse(formIdStr);
    if (!parsedId) {
        logger::error("ProgressionManager: '{}' is not a formId", formIdStr);
        return false;
    }
    const RE::FormID formId = *parsedId;

    return IsSpellAvailableToLearn(formId);
}

bool ProgressionManager::IsSpellAvailableToLearn(RE::FormID formId) const
{
    // A spell is available to learn if:
    // 1. It exists in our progress tracking (meaning it's in the tree)
    // 2. It's not yet unlocked
    // 3. All prerequisites are met (hard/soft system)

    auto it = m_spellProgress.find(formId);
    if (it == m_spellProgress.end()) {
        return false;  // Not in our tree
    }

    if (it->second.unlocked) {
        return false;  // Already unlocked
    }

    // Check tree prerequisites (hard/soft system)
    return AreTreePrerequisitesMet(formId);
}

ProgressionManager::SpellProgress ProgressionManager::GetProgress(RE::FormID formId) const
{
    auto it = m_spellProgress.find(formId);
    if (it != m_spellProgress.end()) {
        return it->second;
    }
    return SpellProgress{};
}

float ProgressionManager::GetProgressPercent(RE::FormID formId) const
{
    auto it = m_spellProgress.find(formId);
    return it != m_spellProgress.end() ? it->second.progressPercent : 0.0f;
}

void ProgressionManager::SetRequiredXP(RE::FormID formId, float required, RequiredXP::Kind kind)
{
    if (required <= 0.0f) {
        logger::warn("ProgressionManager: SetRequiredXP called with non-positive value {:.1f} for {:08X}, clamping to 1.0", required, formId);
        required = 1.0f;
    }
    m_requiredXP.Set(kind, formId, required);
    // A tree number counts only where this character and the overrides have none
    EnsureProgress(formId).requiredXP = kind == RequiredXP::Kind::kCharacter ? required : BaseRequiredXP(formId);
}

void ProgressionManager::SetRequiredXPOverrides(RequiredXP::Table::Map overrides)
{
    m_requiredXP.SetOverrides(std::move(overrides));
    std::size_t updated = 0;
    for (auto& [formId, progress] : m_spellProgress) {
        if (m_requiredXP.HasCharacterValue(formId)) continue;
        progress.requiredXP = BaseRequiredXP(formId);
        ++updated;
    }
    logger::info("ProgressionManager: {} per-spell XP overrides from the settings ({} entries sized again)",
                 m_requiredXP.OverrideCount(), updated);
}
