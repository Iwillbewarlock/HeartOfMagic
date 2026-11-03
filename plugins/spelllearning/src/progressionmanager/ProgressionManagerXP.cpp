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
#include "librarian/SpellResearchMemory.h"

// =============================================================================
// XP SETTINGS
// =============================================================================

void ProgressionManager::SetXPSettings(const XPSettings& settings)
{
    const bool toSingle = settings.learningMode == "single" && m_xpSettings.learningMode != "single";
    m_xpSettings = settings;
    // Known limit: the other schools' targets are kept, not dropped; each cast credits the closest one
    // (OnSpellCast's single mode order). A tome or the panel's next target in single mode clears them.
    const auto held = std::ranges::count_if(m_learningTargets, [](const auto& t) { return t.second != 0; });
    if (toSingle && held > 1) {
        logger::warn("ProgressionManager: learning mode is now single with {} learning targets held - kept; each "
                     "cast credits the closest one (self, direct, school, any)", held);
    }
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
    // The per-source counts follow a lowered XP down (a source at its cap gives again)
    ProgressionMath::FitTrackersToXP(progress, progress.GetCurrentXP());
    // A spell no longer mastered (not unlocked, below 100%, no copy mastered) loses its mastery XP; one
    // that stays unlocked keeps its full power and its ranks
    if (!IsMasteredForRanks(MasteryKeyOf(formId))) ClearMastery(formId);
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
    // Mastery XP: decided before the learning XP below, so the cast that masters a spell earns none
    const RE::FormID masteryKey = MasteryKeyForCast(castSpellId);

    // Iterate through all learning targets and grant XP based on settings. A copy:
    // AddXP clears targets when a spell is mastered (every school targeting it),
    // which would erase from the map under the loop. A target cleared or changed
    // meanwhile is skipped, so a mastered spell gets no more XP from this cast.
    // A spell held under two school keys (a save from before DropTargetFromOtherSchools)
    // gets XP from one cast once
    using ProgressionMath::CastSource;
    struct Candidate
    {
        std::string school;
        RE::FormID id = 0;
        CastSource source = CastSource::kAny;
    };
    std::vector<Candidate> targets;
    std::vector<CastSource> sources;
    targets.reserve(m_learningTargets.size());
    sources.reserve(m_learningTargets.size());
    for (const auto& [targetSchool, targetId] : m_learningTargets) {
        targets.push_back({ targetSchool, targetId, ClassifyCast(targetSchool, targetId, school, castSpellId) });
        sources.push_back(targets.back().source);
    }
    // Single mode credits one target: the closest link first (self, direct, school, any), not
    // whichever school the map happened to hold first. Within a source the map order stays
    const bool single = m_xpSettings.learningMode == "single";
    if (single) ProgressionMath::OrderBySource(targets, sources);

    std::vector<RE::FormID> credited;
    credited.reserve(targets.size());
    float totalXP = 0.0f;
    int duplicateKeys = 0;   // the same spell met again under a second school key
    const auto* castSpell = RE::TESForm::LookupByID<RE::SpellItem>(castSpellId);
    const char* castName = castSpell ? castSpell->GetName() : "?";
    for (const auto& [targetSchool, targetId, source] : targets) {
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

        // After selfCastRequiredAt, only casting the target itself grants XP
        const float currentProgress = progress.progressPercent * 100.0f;
        if (earlySettings.enabled && currentProgress >= earlySettings.selfCastRequiredAt && source != CastSource::kSelf) {
            logger::trace("ProgressionManager: Progress {:.0f}% >= selfCastRequiredAt {:.0f}% - "
                "only self-casting grants XP (cast spell {:08X} != target {:08X})",
                currentProgress, earlySettings.selfCastRequiredAt, castSpellId, targetId);
            continue;
        }

        // The source's multiplier and cap; self: the direct multiplier (times the early-learned bonus), no cap
        float multiplier = 0.0f;
        float capPercent = ProgressionMath::kNoCapPercent;
        auto& progRef = EnsureProgress(targetId);
        float* tracked = &progRef.xpFromSelf;
        switch (source) {
            case CastSource::kSelf:
                multiplier = rates.multiplierDirect;
                if (earlySettings.enabled && effectivenessHook->IsEarlyLearnedSpell(targetId)) {
                    multiplier *= earlySettings.selfCastXPMultiplier;
                }
                break;
            case CastSource::kDirect:
                multiplier = rates.multiplierDirect;
                capPercent = rates.capDirect;
                tracked = &progRef.xpFromDirect;
                break;
            case CastSource::kSchool:
                multiplier = rates.multiplierSchool;
                capPercent = rates.capSchool;
                tracked = &progRef.xpFromSchool;
                break;
            case CastSource::kAny:
                multiplier = rates.multiplierAny;
                capPercent = rates.capAny;
                tracked = &progRef.xpFromAny;
                break;
        }
        const char* sourceName = ProgressionMath::CastSourceName(source);
        if (multiplier <= 0.0f) continue;

        float xpGain = adjustedBaseXP * multiplier;
        // Tome inventory boost: bonus XP while the player carries the spell's tome
        auto* tomeHook = SpellTomeHook::GetSingleton();
        if (tomeHook && tomeHook->GetSettings().tomeInventoryBoost) {
            const float tomeBoost = tomeHook->GetXPMultiplier(targetId);
            if (tomeBoost > 1.0f) xpGain *= tomeBoost;
        }
        // Memory guides practice: Spell Research archetypes remembered at the spell's rank (1.0 when skipped)
        xpGain *= Librarian::SpellResearchMemory::PracticeMultiplier(targetId);

        // The source's cap: at most capPercent of the required XP from this kind of cast
        const float remainingCap = progRef.requiredXP * (capPercent / 100.0f) - *tracked;
        if (remainingCap <= 0.0f) {
            logger::trace("ProgressionManager: Source cap reached for {:08X} (source: {}, cap: {:.1f}%)", targetId,
                          sourceName, capPercent);
            continue;
        }
        const float grant = (std::min)(xpGain, remainingCap);

        // In "single" mode, only the first learning target gets XP
        // In "perSchool" mode, each school's target gets XP independently.
        // Per target at debug level: the cast's one info line follows the loop
        const float progressBefore = progRef.progressPercent;   // by value: AddXP may clear the target
        const bool wasUnlocked = progRef.unlocked;
        // What the cast really added (less than the grant at the top, 0 on a spell already at 100%):
        // counted against the source's cap, logged, and in single mode a cast that changed nothing
        // (a target at 100% waiting for Unlock) does not use up the one target it may credit
        const float added = AddXPMeasured(targetId, grant);
        if (!(added > 0.0f) && wasUnlocked == progRef.unlocked) continue;
        credited.push_back(targetId);
        *tracked += added;
        // Practice becomes memory: each 15% boundary (and 100%) crossed pays Spell Research archetype XP
        Librarian::SpellResearchMemory::OnPracticeProgress(targetId, progressBefore, progRef.progressPercent);
        totalXP += added;
        LogXPGrant(a_path, targetId, added,
                   single ? std::format("cast {} ({:08X}), {} rate, single mode", castName, castSpellId, sourceName)
                          : std::format("cast {} ({:08X}), {} rate, {} target", castName, castSpellId, sourceName,
                                        targetSchool),
                   true);
        if (single) break;  // Only one target in single mode
    }

    if (masteryKey != 0) OnMasteredCast(masteryKey, castSpellId, baseXP);

    // One info line per cast that earned something (a cast earning nothing stays at debug)
    if (!credited.empty()) {
        logger::info("XP: cast {} ({:08X}) from {}: +{:.2f} to {} target(s), {} duplicate key(s) skipped",
                     castName, castSpellId, a_path, totalXP, credited.size(), duplicateKeys);
    } else {
        logger::debug("XP: cast {} ({:08X}) from {}: no target credited", castName, castSpellId, a_path);
    }
}

ProgressionMath::CastSource ProgressionManager::ClassifyCast(const std::string& targetSchool, RE::FormID targetId,
                                                            const std::string& castSchool, RE::FormID castSpellId) const
{
    using ProgressionMath::CastSource;
    if (castSpellId == targetId) return CastSource::kSelf;
    if (targetSchool != castSchool) return CastSource::kAny;
    // Direct runs both ways: a spell leading to the target, or - learning downward - the
    // known spell above it that opened it (a copy of either counts, ProgressionManagerCopies.cpp)
    if (IsDirectPrerequisite(targetId, castSpellId) ||
        (m_xpSettings.reverseUnlock && IsDirectChild(targetId, castSpellId))) {
        return CastSource::kDirect;
    }
    return CastSource::kSchool;
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
        const float newProgress = progress.progressPercent;
        TryEarlyGrant(targetSpellId, oldProgress, newProgress, earlySettings.unlockThreshold / 100.0f);

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
                HudNotify::Show("hud.mastered", "{{spell}} MASTERED. Full power unlocked.",
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

bool ProgressionManager::TryEarlyGrant(RE::FormID spellId, float oldProgress, float newProgress, float thresholdFraction)
{
    auto* effectivenessHook = SpellEffectivenessHook::GetSingleton();
    const bool isTarget = std::ranges::any_of(m_learningTargets, [spellId](const auto& t) { return t.second == spellId; });
    if (!ProgressionMath::ShouldTryEarlyGrant(oldProgress, newProgress, thresholdFraction, isTarget,
                                              effectivenessHook->IsEarlyLearnedSpell(spellId))) {
        return false;
    }
    // Grant the spell early (nerfed) by the one grant rule: not when the player
    // knows a copy of it (CanUnlock), not while ISL holds it for its study
    auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellId);
    effectivenessHook->ReleaseHeldIfOwned(spell);
    if (!effectivenessHook->ShouldGrantEarly(spell)) return false;
    SpellEffectivenessHook::GrantEarlySpell(spell);
    logger::info("ProgressionManager: Early granted spell {:08X} at {:.0f}% progress (threshold {:.0f}%)",
        spellId, newProgress * 100.0f, thresholdFraction * 100.0f);

    // Fire ModEvent: SpellLearning_SpellEarlyGranted, naming the school the way
    // TargetChanged did for this spell (its target key), not the costliest effect's
    SendModEvent("SpellLearning_SpellEarlyGranted", EventSchoolOf(spellId), newProgress * 100.0f, spell);
    return true;
}

float ProgressionManager::AddXPMeasured(RE::FormID targetSpellId, float amount)
{
    const float before = GetProgressPercent(targetSpellId);
    AddXP(targetSpellId, amount);
    const auto it = m_spellProgress.find(targetSpellId);
    if (it == m_spellProgress.end()) return 0.0f;
    return ProgressionMath::ActualRise(before, it->second.progressPercent, it->second.requiredXP);
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
