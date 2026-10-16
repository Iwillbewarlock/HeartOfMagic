// =============================================================================
// ProgressionManagerSerialization.cpp — SKSE co-save, legacy save/load, JSON
// =============================================================================

#include "ProgressionManager.h"
#include "ProgressionCoSave.h"
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

    // Write number of targets
    uint32_t numTargets = static_cast<uint32_t>(m_learningTargets.size());
    a_intfc->WriteRecordData(&numTargets, sizeof(numTargets));

    // Write each target: school string length, school string, formId
    for (auto& [school, formId] : m_learningTargets) {
        uint32_t schoolLen = static_cast<uint32_t>(school.length());
        a_intfc->WriteRecordData(&schoolLen, sizeof(schoolLen));
        a_intfc->WriteRecordData(school.c_str(), schoolLen);
        a_intfc->WriteRecordData(&formId, sizeof(formId));
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
    // unlocked, the modded sources' XP. The built-in sources' XP goes in SLPX below.
    uint32_t skippedSources = 0;
    for (auto& [formId, progress] : m_spellProgress) {
        ProgressionCoSave::Entry entry;
        entry.formId = formId;
        entry.progressPercent = progress.progressPercent;
        entry.unlocked = progress.unlocked;
        entry.modded.assign(progress.xpFromModded.begin(), progress.xpFromModded.end());
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
    WriteCapTrackers(a_intfc);
    m_dirty = false;
}

void ProgressionManager::WriteCapTrackers(SKSE::SerializationInterface* a_intfc) const
{
    // Only spells with something counted: a zero entry reads the same as none
    std::vector<ProgressionCoSave::CapEntry> entries;
    for (const auto& [formId, progress] : m_spellProgress) {
        const ProgressionCoSave::CapEntry e{ formId, progress.xpFromAny, progress.xpFromSchool, progress.xpFromDirect,
                                             progress.xpFromSelf };
        if (!e.IsEmpty()) entries.push_back(e);
    }
    if (!a_intfc->OpenRecord(kCapTrackersRecord, ProgressionCoSave::kCapTrackersVersion) ||
        !ProgressionCoSave::WriteCapEntries(*a_intfc, entries)) {
        logger::error("ProgressionManager: Failed to write the cap tracker record ({} spells)", entries.size());
        return;
    }
    logger::info("ProgressionManager: Saved cap trackers for {} spells", entries.size());
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

void ProgressionManager::BeginLoad()
{
    logger::info("ProgressionManager: Loading from co-save...");
    ClearAllProgress();
    m_pendingCapTrackers.clear();
}

void ProgressionManager::EndLoad()
{
    // SLPX onto the entries SLPR made; a save without SLPX (1.5.0 and before)
    // keeps the counters at 0, a spell without an entry gets none made
    const auto applied = ProgressionCoSave::ApplyCapEntries(m_pendingCapTrackers, m_spellProgress);
    if (!m_pendingCapTrackers.empty()) {
        logger::info("ProgressionManager: Applied cap trackers to {} of {} spells ({} without a progress entry)",
                     applied, m_pendingCapTrackers.size(), m_pendingCapTrackers.size() - applied);
    }
    m_pendingCapTrackers.clear();
    logger::info("ProgressionManager: Co-save load complete");
}

bool ProgressionManager::ReadRecord(SKSE::SerializationInterface* a_intfc,
                                    uint32_t type, uint32_t version, uint32_t length)
{
    // Not ours: say so before looking at the version, or a record belonging to
    // another owner would be rejected here for a version it never claimed.
    if (type != kTargetsRecord && type != kProgressRecord && type != kCapTrackersRecord) return false;

    // Every version written so far is read: SLTR 1-2 (one layout), SLPR 1-3 (v3
    // from a 2026-10-17 development build only), SLPX 1
    bool readable = false;
    switch (type) {
        case kTargetsRecord: readable = version >= 1 && version <= kTargetsVersion; break;
        case kProgressRecord: readable = ProgressionCoSave::IsReadableProgressVersion(version); break;
        default: readable = version >= 1 && version <= ProgressionCoSave::kCapTrackersVersion; break;
    }
    if (!readable) {
        logger::warn("ProgressionManager: Skipping record {:08X} with unsupported version {} (written by a newer version?)",
            type, version);
        return true;
    }

    if (type == kCapTrackersRecord) return ReadCapTrackers(a_intfc, length);

    switch (type) {
        case kTargetsRecord: {
            // Read learning targets
            uint32_t numTargets = 0;
            if (!a_intfc->ReadRecordData(&numTargets, sizeof(numTargets))) {
                logger::error("ProgressionManager: Failed to read numTargets");
                break;
            }

            for (uint32_t i = 0; i < numTargets; ++i) {
                uint32_t schoolLen = 0;
                if (!a_intfc->ReadRecordData(&schoolLen, sizeof(schoolLen))) {
                    logger::error("ProgressionManager: Failed to read schoolLen at target {}", i);
                    break;
                }
                if (schoolLen > ProgressionCoSave::kMaxNameLength) {
                    logger::error("ProgressionManager: schoolLen {} exceeds limit at target {}", schoolLen, i);
                    break;
                }

                std::string school(schoolLen, '\0');
                if (!a_intfc->ReadRecordData(school.data(), schoolLen)) {
                    logger::error("ProgressionManager: Failed to read school string at target {}", i);
                    break;
                }

                RE::FormID formId = 0;
                if (!a_intfc->ReadRecordData(&formId, sizeof(formId))) {
                    logger::error("ProgressionManager: Failed to read formId at target {}", i);
                    break;
                }

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
            if (!a_intfc->ReadRecordData(&numProgress, sizeof(numProgress))) {
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
                progress.xpFromModded.insert(entry.modded.begin(), entry.modded.end());
                // What the built-in caps count against: from a v3 record only; a
                // v2 one has them in SLPX, put on in EndLoad (0 from an older save)
                progress.xpFromAny = entry.xpFromAny;
                progress.xpFromSchool = entry.xpFromSchool;
                progress.xpFromDirect = entry.xpFromDirect;
                progress.xpFromSelf = entry.xpFromSelf;
                // The co-save keeps only the percent. Start from the spell's tier
                // XP (GetRequiredXP with no stored value, so before the entry is
                // in the map) rather than the struct's 100; the panel then sends its
                // own number - per-spell override, known-higher-spell share - through SetRequiredXP.
                m_spellProgress.erase(resolvedId);
                progress.requiredXP = GetRequiredXP(resolvedId);
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
        std::stringstream ss;
        ss << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << formId;
        targets[school] = ss.str();
    }
    j["learningTargets"] = targets;

    // Spell progress
    json progress = json::object();
    for (auto& [formId, data] : m_spellProgress) {
        std::stringstream ss;
        ss << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << formId;
        std::string formIdStr = ss.str();

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

    return JsonText::Dump(j);
}
