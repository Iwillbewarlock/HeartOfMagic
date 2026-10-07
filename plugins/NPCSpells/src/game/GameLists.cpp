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
