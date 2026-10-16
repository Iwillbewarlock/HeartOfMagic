// =============================================================================
// SpellTomeHookSave.cpp — the tome "XP once" list in the SKSE co-save (SLTX)
// =============================================================================

#include "SpellTomeHook.h"
#include "ProgressionCoSave.h"

void SpellTomeHook::OnGameSaved(SKSE::SerializationInterface* a_intfc) const
{
    std::vector<std::uint32_t> formIds;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        formIds.assign(m_tomeXPGranted.begin(), m_tomeXPGranted.end());
    }

    if (!a_intfc->OpenRecord(kTomeXPRecord, ProgressionCoSave::kTomeXPVersion)) {
        logger::error("SpellTomeHook: Failed to open the tome XP record for writing");
        return;
    }
    if (!ProgressionCoSave::WriteFormIds(*a_intfc, formIds)) {
        logger::error("SpellTomeHook: Failed to write the tome XP record ({} spells)", formIds.size());
        return;
    }
    logger::info("SpellTomeHook: Saved {} spells that already had tome XP", formIds.size());
}

bool SpellTomeHook::ReadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t type, std::uint32_t version,
                               std::uint32_t length)
{
    if (type != kTomeXPRecord) return false;

    if (version < 1 || version > ProgressionCoSave::kTomeXPVersion) {
        logger::warn("SpellTomeHook: Skipping tome XP record with unsupported version {} (written by a newer version?)",
                     version);
        return true;
    }

    std::vector<std::uint32_t> saved;
    const auto result = ProgressionCoSave::ReadFormIds(*a_intfc, length, saved);
    if (result != ProgressionCoSave::ReadResult::Ok) {
        // The ids read before the failure are still good: each is a whole field
        logger::error("SpellTomeHook: tome XP record unreadable ({}), {} spells read before it kept",
                      ProgressionCoSave::Describe(result), saved.size());
    }

    std::unordered_set<RE::FormID> resolved;
    std::size_t unresolved = 0;
    for (const auto formId : saved) {
        RE::FormID resolvedId = 0;
        if (a_intfc->ResolveFormID(formId, resolvedId)) {
            resolved.insert(resolvedId);
        } else {
            ++unresolved;  // its plugin is gone
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tomeXPGranted = std::move(resolved);
    }
    logger::info("SpellTomeHook: Loaded {} spells that already had tome XP ({} from removed plugins dropped)",
                 saved.size() - unresolved, unresolved);
    return true;
}
