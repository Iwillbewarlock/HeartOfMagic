#include "game/GameLists.h"

#include "core/NpcSpellsEsl.h"
#include "core/NpcSpellsSlots.h"

#include <memory>
#include <type_traits>

namespace
{
    // =========================================================================
    // A NEW ENTRY ARRAY FOR AN EMPTY RECORD
    // =========================================================================
    // On an LLCT-0 record the engine leaves entries.data() non-null while numEntries is 0 (first in-game run,
    // 1.6.1170): what that pointer holds is unknown, so it is never freed, resized or read through. A new block in
    // SimpleArray's layout (RE/M/MemoryManager.h:368-402, resize's else branch: one size_t count, then the elements,
    // from the game heap) replaces the pointer. The old block is left where it is: a deliberate leak of one small
    // block per filled list (about 338), once per game start, because freeing memory of unknown origin could
    // corrupt the heap. The engine reads only the pointer (TESLevSpell+0x38) and numEntries (+0x42).
    using EntryArray = RE::SimpleArray<RE::LEVELED_OBJECT>;
    static_assert(sizeof(EntryArray) == sizeof(void*), "SimpleArray is its element pointer alone");
    static_assert(alignof(RE::LEVELED_OBJECT) <= alignof(std::size_t), "the head is one size_t (resize's else branch)");
    static_assert(std::is_trivially_copyable_v<RE::LEVELED_OBJECT>);

    // SimpleArray::_data (RE/M/MemoryManager.h, protected) reached through an explicit instantiation, which may
    // name a non-public member: a defined way to store the pointer, where a memcpy into a class with a user
    // destructor is not
    struct EntryDataTag
    {
        using Member = RE::LEVELED_OBJECT* EntryArray::*;
        friend Member DataMember(EntryDataTag);
    };
    template <class Tag, typename Tag::Member kMember>
    struct ExposeMember
    {
        friend typename Tag::Member DataMember(Tag) { return kMember; }
    };
    template struct ExposeMember<EntryDataTag, &EntryArray::_data>;

    // A zeroed array of `count` entries set into `entries`, or null when the game heap gives nothing (entries untouched)
    RE::LEVELED_OBJECT* AttachNewEntries(EntryArray& entries, std::size_t count)
    {
        auto* head = RE::malloc<std::size_t>(sizeof(std::size_t) + sizeof(RE::LEVELED_OBJECT) * count);  // RE/M/MemoryManager.h:98-110
        if (!head) {
            return nullptr;
        }
        *head = count;
        auto* first = reinterpret_cast<RE::LEVELED_OBJECT*>(head + 1);
        std::uninitialized_value_construct_n(first, count);
        entries.*DataMember(EntryDataTag{}) = first;
        return first;
    }
}


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
    const EslRecords& records, std::size_t& skippedNoRecord, std::size_t& skippedPrefilled)
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
        // RE/T/TESLeveledList.h:70-73. A record whose count is not 0 holds another plugin's override: it is left as it
        // was (its array is not ours to replace). A count of 0 gets a new array (AttachNewEntries: the pointer the
        // engine left there is not touched). numEntries is uint8: BuildLists caps a list at kMaxEntries (255).
        if (lev->numEntries != 0) {
            if (skippedPrefilled == 0) {
                logger::warn("NPCSpells: {} already has entries ({}); it and any other such list are left as they are (count in the report)",
                    NpcSpells::EditorIdFor(NpcSpells::SlotTable()[list.slotIndex].key, list.chunk), lev->numEntries);
            }
            ++skippedPrefilled;
            continue;
        }
        RE::LEVELED_OBJECT* entries = nullptr;
        if (!list.entries.empty()) {
            entries = AttachNewEntries(lev->entries, list.entries.size());
            if (!entries) {
                logger::error("NPCSpells: the game heap gave no memory for {}; list left empty",
                    NpcSpells::EditorIdFor(NpcSpells::SlotTable()[list.slotIndex].key, list.chunk));
                ++skippedNoRecord;
                continue;
            }
        }
        for (std::size_t i = 0; i < list.entries.size(); ++i) {
            auto& e = entries[i];
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
