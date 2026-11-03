// =============================================================================
// ProgressionManagerSerialization.cpp — SKSE co-save, legacy save/load, JSON
// =============================================================================

#include "ProgressionManager.h"
#include "FormIdText.h"
#include "ProgressionCoSave.h"
#include "ProgressionMath.h"
#include "SpellEffectivenessHook.h"
#include "JsonText.h"
#include "SKSE/SKSE.h"
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// =============================================================================
// SKSE CO-SAVE SERIALIZATION
// =============================================================================

void ProgressionManager::OnGameSaved(SKSE::SerializationInterface* a_intfc)
{
    logger::info("ProgressionManager: Saving to co-save...");

    // Write learning targets record
    if (!a_intfc->OpenRecord(kTargetsRecord, kTargetsVersion)) {
        logger::error("ProgressionManager: Failed to open targets record for writing");
        return;
    }

    std::vector<ProgressionCoSave::TargetEntry> targets;
    targets.reserve(m_learningTargets.size());
    for (const auto& [school, formId] : m_learningTargets) {
        targets.push_back({ school, formId });
    }
    const auto numTargets = targets.size();
    if (!ProgressionCoSave::WriteTargets(*a_intfc, targets)) {
        logger::error("ProgressionManager: Failed to write the learning targets record");
    }

    logger::info("ProgressionManager: Saved {} learning targets", numTargets);

    // Write progress record
    if (!a_intfc->OpenRecord(kProgressRecord, ProgressionCoSave::kProgressVersion)) {
        logger::error("ProgressionManager: Failed to open progress record for writing");
        return;
    }

    // Write number of progress entries
    uint32_t numProgress = static_cast<uint32_t>(m_spellProgress.size());
    a_intfc->WriteRecordData(&numProgress, sizeof(numProgress));

    // Each entry in the v2 layout 1.5.0 reads (ProgressionCoSave.h): percent,
    // unlocked, the modded sources' XP (share x required XP, as 1.7.0 reads it; the shares go in SLPS).
    // The built-in sources' XP goes in SLPX below.
    uint32_t skippedSources = 0;
    for (auto& [formId, progress] : m_spellProgress) {
        ProgressionCoSave::Entry entry;
        entry.formId = formId;
        entry.progressPercent = progress.progressPercent;
        entry.unlocked = progress.unlocked;
        for (const auto& [name, share] : progress.shareModded) entry.modded.emplace_back(name, progress.XPOfShare(share));
        uint32_t skipped = 0;
        if (!ProgressionCoSave::WriteEntry(*a_intfc, entry, &skipped)) {
            logger::error("ProgressionManager: Failed to write progress entry {:08X}", formId);
            return;
        }
        skippedSources += skipped;
    }
    if (skippedSources > 0) {
        logger::warn("ProgressionManager: {} modded XP source counts not saved (name too long or too many sources)",
                     skippedSources);
    }

    logger::info("ProgressionManager: Saved {} spell progress entries to co-save (version {})", numProgress,
                 ProgressionCoSave::kProgressVersion);
    WriteCounterRecords(a_intfc);
    m_dirty = false;
}

void ProgressionManager::WriteCounterRecords(SKSE::SerializationInterface* a_intfc) const
{
    // SLPX in 1.7.0's layout (absolute XP = share x required XP), so going back to 1.7.0 keeps the counts;
    // SLPS with the shares themselves, which this version reads. Only spells with something counted: a
    // zero entry reads the same as none
    std::vector<ProgressionCoSave::CapEntry> absolute;
    std::vector<ProgressionCoSave::ShareEntry> shares;
    for (const auto& [formId, progress] : m_spellProgress) {
        const ProgressionCoSave::CapEntry e{ formId, progress.XPOfShare(progress.shareAny), progress.XPOfShare(progress.shareSchool),
                                             progress.XPOfShare(progress.shareDirect), progress.XPOfShare(progress.shareSelf) };
        if (!e.IsEmpty()) absolute.push_back(e);
        ProgressionCoSave::ShareEntry share{ formId, progress.shareAny, progress.shareSchool, progress.shareDirect,
                                             progress.shareSelf, {} };
        for (const auto& [name, value] : progress.shareModded) {
            if (value != 0.0f) share.modded.emplace_back(name, value);
        }
        const bool any = share.shareAny != 0.0f || share.shareSchool != 0.0f || share.shareDirect != 0.0f ||
                         share.shareSelf != 0.0f || !share.modded.empty();
        if (any) shares.push_back(std::move(share));
    }
    if (!a_intfc->OpenRecord(kCapTrackersRecord, ProgressionCoSave::kCapTrackersVersion) ||
        !ProgressionCoSave::WriteCapEntries(*a_intfc, absolute)) {
        logger::error("ProgressionManager: Failed to write the cap tracker record ({} spells)", absolute.size());
    }
    if (!a_intfc->OpenRecord(kSharesRecord, ProgressionCoSave::kSharesVersion) ||
        !ProgressionCoSave::WriteShareEntries(*a_intfc, shares)) {
        logger::error("ProgressionManager: Failed to write the per-source shares record ({} spells)", shares.size());
        return;
    }
    logger::info("ProgressionManager: Saved per-source counts for {} spells (SLPS; SLPX for 1.7.0: {})", shares.size(),
                 absolute.size());
}

bool ProgressionManager::ReadCapTrackers(SKSE::SerializationInterface* a_intfc, uint32_t length)
{
    std::vector<ProgressionCoSave::CapEntry> saved;
    const auto result = ProgressionCoSave::ReadCapEntries(*a_intfc, length, saved);
    if (result != ProgressionCoSave::ReadResult::Ok) {
        // Whole entries read before the failure are good
        logger::error("ProgressionManager: cap tracker record unreadable ({}), {} entries read before it kept",
                      ProgressionCoSave::Describe(result), saved.size());
    }
    for (auto& e : saved) {
        RE::FormID resolvedId = 0;
        if (a_intfc->ResolveFormID(e.formId, resolvedId)) {
            e.formId = resolvedId;
            m_pendingCapTrackers.push_back(e);
        }
    }
    return true;
}

bool ProgressionManager::ReadShares(SKSE::SerializationInterface* a_intfc, uint32_t length)
{
    std::vector<ProgressionCoSave::ShareEntry> saved;
    const auto result = ProgressionCoSave::ReadShareEntries(*a_intfc, length, saved);
    if (result != ProgressionCoSave::ReadResult::Ok) {
        logger::error("ProgressionManager: per-source shares record unreadable ({}), {} entries read before it kept",
                      ProgressionCoSave::Describe(result), saved.size());
    }
    for (auto& e : saved) {
        RE::FormID resolvedId = 0;
        if (a_intfc->ResolveFormID(e.formId, resolvedId)) {
            e.formId = resolvedId;
            m_pendingShares.insert_or_assign(resolvedId, std::move(e));
        }
    }
    return true;
}

void ProgressionManager::BeginLoad()
{
    logger::info("ProgressionManager: Loading from co-save...");
    ClearAllProgress();
    m_pendingCapTrackers.clear();
    m_pendingAbsolute.clear();
    m_pendingShares.clear();
}

void ProgressionManager::EndLoad()
{
    // A 1.7 save's absolute counts: SLPX onto SLPR's entries (a spell without one gets none)
    ProgressionCoSave::ApplyCapEntries(m_pendingCapTrackers, m_pendingAbsolute);
    m_pendingCapTrackers.clear();
    EndLoadMastery();

    // Every entry's required XP now that the whole save is in (ReadRecord's was worked out before the
    // records after SLPR), then its per-source counts: the save's shares (SLPS) as they are; a save without
    // them (1.7 and before) has absolute XP, converted once against this required XP and held to the
    // progress (ProgressionMath::SharesFromAbsolute) - an approximation, the save does not say what required
    // XP they were counted against (docs/ARCHITECTURE.md, "Per-source counts")
    std::size_t fromShares = 0;
    std::size_t converted = 0;
    for (auto& [formId, progress] : m_spellProgress) {
        progress.requiredXP = BaseRequiredXP(formId);
        if (const auto shares = m_pendingShares.find(formId); shares != m_pendingShares.end()) {
            progress.shareAny = ProgressionMath::CleanShare(shares->second.shareAny);
            progress.shareSchool = ProgressionMath::CleanShare(shares->second.shareSchool);
            progress.shareDirect = ProgressionMath::CleanShare(shares->second.shareDirect);
            progress.shareSelf = ProgressionMath::CleanShare(shares->second.shareSelf);
            progress.shareModded.clear();
            for (const auto& [name, value] : shares->second.modded) {
                if (const float share = ProgressionMath::CleanShare(value); share > 0.0f) progress.shareModded[name] = share;
            }
            ++fromShares;
        } else if (m_pendingShares.empty()) {
            if (const auto absolute = m_pendingAbsolute.find(formId); absolute != m_pendingAbsolute.end()) {
                ProgressionMath::SharesFromAbsolute(progress, absolute->second, progress.requiredXP, progress.progressPercent);
                ++converted;
            }
        }
    }
    m_pendingAbsolute.clear();
    m_pendingShares.clear();
    logger::info("ProgressionManager: Co-save load complete ({} entries, {} tree required XP values, per-source counts: "
                 "{} from shares, {} converted from a 1.7 save's XP)",
                 m_spellProgress.size(), m_requiredXP.TreeCount(), fromShares, converted);
}

bool ProgressionManager::ReadRecord(SKSE::SerializationInterface* a_intfc,
                                    uint32_t type, uint32_t version, uint32_t length)
{
    // Not ours: say so before looking at the version, or a record belonging to
    // another owner would be rejected here for a version it never claimed.
    if (type == kMasteryRecord) return ReadMasteryRecord(a_intfc, version, length);
    if (type != kTargetsRecord && type != kProgressRecord && type != kCapTrackersRecord && type != kSharesRecord) {
        return false;
    }

    // Every version written so far is read: SLTR 1-2 (one layout), SLPR 1-3 (v3
    // from a 2026-10-17 development build only), SLPX 1, SLPS 1
    bool readable = false;
    switch (type) {
        case kTargetsRecord: readable = version >= 1 && version <= kTargetsVersion; break;
        case kProgressRecord: readable = ProgressionCoSave::IsReadableProgressVersion(version); break;
        case kSharesRecord: readable = version >= 1 && version <= ProgressionCoSave::kSharesVersion; break;
        default: readable = version >= 1 && version <= ProgressionCoSave::kCapTrackersVersion; break;
    }
    if (!readable) {
        logger::warn("ProgressionManager: Skipping record {:08X} with unsupported version {} (written by a newer version?)",
            type, version);
        return true;
    }

    if (type == kCapTrackersRecord) return ReadCapTrackers(a_intfc, length);
    if (type == kSharesRecord) return ReadShares(a_intfc, length);

    switch (type) {
        case kTargetsRecord: {
            // Every field read in full: a cut-short form id read as a whole one
            // named another spell (ProgressionCoSave::ReadTargets)
            std::vector<ProgressionCoSave::TargetEntry> targets;
            const auto result = ProgressionCoSave::ReadTargets(*a_intfc, length, targets);
            if (result != ProgressionCoSave::ReadResult::Ok) {
                logger::error("ProgressionManager: learning targets record unreadable ({}), {} targets read before it kept",
                              ProgressionCoSave::Describe(result), targets.size());
            }

            for (const auto& [school, formId] : targets) {
                // Resolve formId (handles load order changes)
                RE::FormID resolvedId = 0;
                if (!a_intfc->ResolveFormID(formId, resolvedId)) {
                    logger::warn("ProgressionManager: Failed to resolve target formId {:08X}", formId);
                } else if (!SpellEffectivenessHook::CanLearnEarly(RE::TESForm::LookupByID<RE::SpellItem>(resolvedId))) {
                    // A power a tome made the target before 2026-10-07: free the school's slot
                    logger::info("ProgressionManager: Dropped target {} -> {:08X}, a power, ability or schoolless spell", school, resolvedId);
                } else {
                    m_learningTargets[school] = resolvedId;
                    logger::info("ProgressionManager: Loaded target {} -> {:08X}", school, resolvedId);
                }
            }

            CanonicalizeLearningTargets();
            logger::info("ProgressionManager: Loaded {} learning targets", m_learningTargets.size());
            break;
        }

        case kProgressRecord: {
            // Read spell progress
            uint32_t numProgress = 0;
            if (a_intfc->ReadRecordData(&numProgress, sizeof(numProgress)) != sizeof(numProgress)) {
                logger::error("ProgressionManager: Failed to read numProgress");
                break;
            }

            for (uint32_t i = 0; i < numProgress; ++i) {
                // A failed read anywhere in an entry ends the record: the rest of
                // it would be read from the wrong offset (ProgressionCoSave.h)
                ProgressionCoSave::Entry entry;
                const auto result = ProgressionCoSave::ReadEntry(*a_intfc, version, entry);
                if (result != ProgressionCoSave::ReadResult::Ok) {
                    logger::error("ProgressionManager: progress entry {} of {} unreadable ({}), the {} after it are "
                                  "skipped; {} kept", i, numProgress, ProgressionCoSave::Describe(result),
                                  numProgress - i - 1, m_spellProgress.size());
                    break;
                }

                // Resolve formId (handles load order changes)
                RE::FormID resolvedId = 0;
                if (!a_intfc->ResolveFormID(entry.formId, resolvedId)) {
                    logger::warn("ProgressionManager: Failed to resolve progress formId {:08X}", entry.formId);
                    continue;
                }
                SpellProgress progress;
                progress.progressPercent = entry.progressPercent;
                progress.unlocked = entry.unlocked;
                // The per-source counts as absolute XP (the modded sources here, the built-in ones from a v3
                // record or SLPX): kept for EndLoad, which takes SLPS's shares instead when the save has them
                m_pendingAbsolute.insert_or_assign(resolvedId, entry);
                // The co-save keeps only the percent: the required XP is the panel's
                // number, else the tier's - worked out again in EndLoad, once every
                // record is in (the reverse unlock share reads the other spells'
                // progress and the early-learned list, which may come later)
                progress.requiredXP = BaseRequiredXP(resolvedId);
                m_spellProgress.insert_or_assign(resolvedId, std::move(progress));

                logger::info("ProgressionManager: Loaded progress {:08X} -> {:.1f}% {} ({} modded sources)",
                    resolvedId, entry.progressPercent * 100.0f, entry.unlocked ? "(unlocked)" : "",
                    entry.modded.size());
            }

            logger::info("ProgressionManager: Loaded {} spell progress entries (record version {})",
                         m_spellProgress.size(), version);
            break;
        }

        default:
            break;
    }
    return true;
}

void ProgressionManager::OnRevert(SKSE::SerializationInterface*)
{
    logger::info("ProgressionManager: Reverting (new game or load)");
    ClearAllProgress();
}

// =============================================================================
// LEGACY SAVE/LOAD (JSON files - kept for backwards compatibility)
// =============================================================================

void ProgressionManager::SetCurrentSave(const std::string& saveName)
{
    if (m_currentSaveName != saveName) {
        m_currentSaveName = saveName;
        logger::info("ProgressionManager: Save name set to '{}'", saveName);
    }
}

void ProgressionManager::LoadProgress(const std::string& saveName)
{
    // This is now a no-op - progress is loaded from co-save
    // Kept for backwards compatibility
    SetCurrentSave(saveName);
    logger::info("ProgressionManager: LoadProgress called (legacy) - using co-save data");
}

void ProgressionManager::SaveProgress()
{
    // This is now a no-op - progress is saved to co-save automatically
    // Kept for backwards compatibility
    logger::trace("ProgressionManager: SaveProgress called (legacy) - using co-save");
}

std::string ProgressionManager::GetProgressJSON() const
{
    json j;

    // Learning targets
    json targets = json::object();
    for (auto& [school, formId] : m_learningTargets) {
        targets[school] = FormIdText::Format(formId);
    }
    j["learningTargets"] = targets;

    // Spell progress
    json progress = json::object();
    for (auto& [formId, data] : m_spellProgress) {
        const std::string formIdStr = FormIdText::Format(formId);

        float currentXP = data.GetCurrentXP();
        progress[formIdStr] = {
            {"xp", currentXP},
            {"required", data.requiredXP},
            {"progress", data.progressPercent},
            {"unlocked", data.unlocked},
            {"ready", !data.unlocked && data.progressPercent >= 1.0f}
        };
    }
    j["spellProgress"] = progress;

    // Mastery ranks: the settings the card needs and every spell with mastery XP, whether it has a
    // progress entry or not (a spell known from outside the tree has none). Apart from spellProgress,
    // whose entries the panel reads as learning progress
    j["mastery"] = { {"enabled", m_mastery.enabled}, {"ranks", m_mastery.ranks} };
    json mastery = json::object();
    for (const auto& [formId, xp] : m_masteryXP) {
        const auto where = MasteryMath::ProgressFor(xp, m_mastery, MasteryTierOf(formId));
        mastery[FormIdText::Format(formId)] = {
            {"xp", xp}, {"rank", where.rank}, {"ranks", where.ranks}, {"floor", where.floor}, {"next", where.next}
        };
    }
    j["spellMastery"] = mastery;

    return JsonText::Dump(j);
}
