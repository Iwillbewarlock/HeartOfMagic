// =============================================================================
// ProgressionManagerStudy.cpp — ISL study XP (no early grant, capped per spell)
// =============================================================================

#include "ProgressionManager.h"
#include "ProgressionMath.h"
#include "SpellEffectivenessHook.h"

float ProgressionManager::AddXPNoGrant(const std::string& formIdStr, float amount)
{
    // ISL compatibility: Record XP progress without triggering early spell grant.
    // ISL's scripts call AddSpell when study is complete — we must not AddSpell
    // before that or ISL thinks the player already knows the spell.
    RE::FormID formId = 0;
    try {
        formId = std::stoul(formIdStr, nullptr, 16);
    } catch (const std::exception& e) {
        logger::error("ProgressionManager: Failed to parse formId '{}': {}", formIdStr, e.what());
        return 0.0f;
    }

    auto& progress = EnsureProgress(formId);

    if (progress.unlocked && progress.progressPercent >= 1.0f) {
        return 0.0f;
    }
    // requiredXP == 0 means XP requirements not initialized — treat as no progress, not instant mastery
    if (progress.requiredXP <= 0.0f) {
        logger::warn("ProgressionManager: [ISL-NoGrant] AddXP for {:08X} but requiredXP is 0 — XP update ignored", formId);
        return 0.0f;
    }

    // All study of a spell is worth its unlock-threshold share, however often ISL
    // starts its own count again (the player swaps books mid-study)
    const float threshold = SpellEffectivenessHook::GetSingleton()->GetSettings().unlockThreshold / 100.0f;
    // A spell studied before 2026-10-17 has no count yet and starts from 0: its
    // study so far is not told apart from its cast XP
    const auto counted = progress.xpFromModded.find(kStudySource);
    float studied = counted != progress.xpFromModded.end() ? counted->second : 0.0f;
    const float granted = ProgressionMath::StudyGrant(amount, progress.requiredXP, threshold, studied);

    const float oldXP = progress.GetCurrentXP();
    progress.progressPercent = ProgressionMath::NoGrantProgress(progress.progressPercent, granted, progress.requiredXP);
    const float added = (std::max)(0.0f, progress.GetCurrentXP() - oldXP);
    if (added > 0.0f) {
        studied += added;
        progress.xpFromModded[kStudySource] = studied;  // only once something was studied
    }
    m_dirty = true;
    SpellEffectivenessHook::GetSingleton()->SyncProgress(formId, progress.progressPercent);

    logger::info("ProgressionManager: [ISL-NoGrant] Spell {:08X} XP: {:.1f} -> {:.1f} / {:.1f} ({:.1f}%), "
                 "{:.1f} asked, study XP {:.1f} of {:.1f}",
        formId, oldXP, progress.GetCurrentXP(), progress.requiredXP, progress.progressPercent * 100.0f,
        amount, studied, progress.requiredXP * threshold);

    // NOTE: Deliberately skipping GrantEarlySpell and power step updates.
    // ISL will call AddSpell when study completes, at which point our
    // SpellEffectivenessHook will apply the appropriate power scaling.
    return added;
}
