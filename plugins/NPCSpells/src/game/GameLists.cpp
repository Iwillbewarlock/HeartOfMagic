#include "game/GameLists.h"

#include "core/NpcSpellsEsl.h"
#include "core/NpcSpellsSlots.h"

NpcSpellsGame::EslRecords NpcSpellsGame::LookupEslRecords()
{
    EslRecords out;
    auto* dh = RE::TESDataHandler::GetSingleton();  // RE/T/TESDataHandler.h:46
    if (!dh) {
        return out;
    }
    // RE/T/TESDataHandler.h:78 finds light plugins only; :74 tells "present but not light"
    if (!dh->LookupLoadedLightModByName(NpcSpells::kEslFileName)) {
        if (dh->LookupLoadedModByName(NpcSpells::kEslFileName)) {
            logger::error("NPCSpells: {} is loaded without the ESL flag - reinstall the mod", NpcSpells::kEslFileName);
        } else {
            logger::error("NPCSpells: {} is not loaded - enable the plugin in your mod manager", NpcSpells::kEslFileName);
        }
        return out;
    }
    out.loaded = true;
    const auto& table = NpcSpells::SlotTable();
    out.byRecord.assign(table.size() * NpcSpells::kMaxChunks, nullptr);
    for (std::size_t slot = 0; slot < table.size(); ++slot) {
        for (std::uint8_t c = 0; c < NpcSpells::kMaxChunks; ++c) {
            // RE/T/TESDataHandler.h:62. LookupFormID (src/RE/T/TESDataHandler.cpp:125-141) builds the
            // id at :136-138 without checking the form exists, so a null form is the existence test.
            auto* form = dh->LookupForm(NpcSpells::LocalFormId(slot, c), NpcSpells::kEslFileName);
            if (!form) {
                ++out.missing;
                continue;
            }
            auto* lev = form->As<RE::TESLevSpell>();  // RE/T/TESForm.h:259, RE/T/TESLevSpell.h:16
            if (!lev) {
                ++out.wrongType;
                logger::warn("NPCSpells: record 0x{:03X} is not a leveled spell list", NpcSpells::LocalFormId(slot, c));
                continue;
            }
            out.byRecord[slot * NpcSpells::kMaxChunks + c] = lev;
        }
    }
    return out;
}

NpcSpells::ListIndex NpcSpellsGame::FillLists(const std::vector<NpcSpells::BuiltList>& lists, const ResolvedPool& pool,
    const EslRecords& records, std::size_t& skippedNoRecord)
{
    NpcSpells::ListIndex index;
    index.present.assign(NpcSpells::SlotTable().size(), {});
    for (const auto& list : lists) {
        const std::size_t record = list.slotIndex * NpcSpells::kMaxChunks + list.chunk;
        auto* lev = record < records.byRecord.size() ? records.byRecord[record] : nullptr;
        if (!lev || list.slotIndex >= index.present.size()) {
            ++skippedNoRecord;
            continue;
        }
        // RE/T/TESLeveledList.h:70-73. The shipped records hold no entries (LLCT 0), so resize allocates a fresh
        // game-heap array (RE/M/MemoryManager.h:368) and frees nothing. numEntries is uint8: BuildLists caps a
        // list at kMaxEntries (255).
        lev->entries.resize(list.entries.size());
        for (std::size_t i = 0; i < list.entries.size(); ++i) {
            auto& e = lev->entries[i];
            e.form = pool.forms[list.entries[i].spellIndex];
            e.count = 1;
            e.level = list.entries[i].level;
            e.pad0C = 0;
            e.itemExtra = nullptr;
        }
        lev->numEntries = static_cast<std::uint8_t>(list.entries.size());
        lev->chanceNone = 0;
        lev->llFlags = static_cast<RE::TESLeveledList::Flag>(list.flags);  // kAllLevelsFlag = kCalculateFromAllLevelsLTOrEqPCLevel
        index.present[list.slotIndex][list.chunk] = true;
    }
    return index;
}
