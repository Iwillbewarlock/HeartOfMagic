#pragma once
#include <cstddef>
#include <vector>
// The write plan of AttachToNpcs (game/GameNpcs.cpp) without game types, so it is tested offline (npcspells-test): which
// NPC records get our lists and how, and why the others get none. Pass 1 of AttachToNpcs reads every record into a
// PlanRecord; nothing is written before the plan is made.
namespace NpcSpells {
    inline constexpr std::size_t kNoRecord = static_cast<std::size_t>(-1);
    // Skip reasons of the plan (DecideNpc's own reasons are counted apart). kSkipTemplateUnresolvable and kSkipHolderGotNone
    // are in NpcSpellsNpc.h.
    inline constexpr const char* kSkipHolderBlocked = "template spell list, holder not written (shared leveled-list array)";
    inline constexpr const char* kSkipInheritedBeast = "template spell list, beast race";
    inline constexpr const char* kSkipInheritedCreature = "template spell list, creature not a mage";
    // No spell list data and no kCopiedTemplate: the engine has not copied a list into it, so one made here would hold only
    // our lists, in place of whatever the engine gives it later
    inline constexpr const char* kSkipNoOwnSpellList = "template spell list, no spell list data and no kCopiedTemplate";
    inline constexpr const char* kSkipSharedSpellData = "template spell list, spell list data shared with a chain record that gets no lists";
    // Adding lists frees the record's old array (RE/T/TESSpellList.cpp CopySpellList): another spell list data pointing at
    // the same array would be left pointing at freed memory
    inline constexpr const char* kSkipSharedLevArray = "leveled-list array shared with another record's spell list data (not written)";

    // One NPC record as pass 1 read it: it decides for itself (DecideNpc) or inherits its spell list from a template
    struct PlanRecord {
        bool inheriting = false;                 // the Spells flag with a template link (not the player)
        bool holderResolved = false;             // inheriting: the chain names its spell list holder before spawn
        std::size_t holder = kNoRecord;          // inheriting, resolved: the holder's index; kNoRecord when the holder was skipped before its decision
        std::size_t sharesSpellData = kNoRecord; // inheriting: a record of its template chain (index) whose spell list data it is, the holder first
        bool hasLists = false;                   // decides for itself: its decision gave lists to add
        bool hasSpellData = false;               // has spell list data (actorEffects)
        bool copiedTemplate = false;             // kCopiedTemplate: the engine copied its holder's spell list into it while loading
        bool blocked = false;                    // its leveled-list array is also another spell list data's: never written
        const char* ownSkip = nullptr;           // inheriting: the skip its own race gives (kSkipInheritedBeast, kSkipInheritedCreature)
    };
    enum class PlanAction {
        kNone,             // nothing written: skipReason says why (null for an own decision without lists, counted by DecideNpc's reason)
        kWriteOwn,         // its own decision's lists
        kWriteInherited,   // its holder's lists, into its own spell list data
        kCreateInherited,  // its holder's lists, into spell list data made for it (kCopiedTemplate and none: the engine copied an empty holder)
        kViaShared,        // nothing written to it: the record whose spell list data it shares is written, so it gets those lists
    };
    struct PlanStep {
        PlanAction action = PlanAction::kNone;
        const char* skipReason = nullptr;
        std::size_t listsFrom = kNoRecord;  // a write or kViaShared: the record whose decision supplies the lists (itself for kWriteOwn)
        std::size_t via = kNoRecord;        // kViaShared: the record whose write reaches it
        bool blockedWrite = false;          // it would have been written but its array is shared (kSkipSharedLevArray)
    };
    // One step per record, in record order. O(n), plus O(n) per round for chains of shared spell list data (at most n rounds).
    std::vector<PlanStep> PlanWrites(const std::vector<PlanRecord>& records);
    inline bool IsWrite(PlanAction a) { return a == PlanAction::kWriteOwn || a == PlanAction::kWriteInherited || a == PlanAction::kCreateInherited; }
}
