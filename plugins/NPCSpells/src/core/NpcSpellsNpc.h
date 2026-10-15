#pragma once
#include "core/NpcSpellsClassTier.h"
#include "core/NpcSpellsSlots.h"
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace NpcSpells {
    struct NpcFacts {
        std::string pluginLower; std::uint32_t localId = 0;   // dice key
        bool inheritsSpellList = false;   // own kSpells flag and a template link (the flag without TPLT inherits nothing)
        bool unresolvableTraits = false, unresolvableStats = false, unresolvableFactions = false;
        bool isPlayer = false, isBeast = false, isCreature = false, isRedguard = false, isUndeadException = false, isNecromancerClass = false;
        bool hasClass = false; ClassWeights weights;
        std::vector<std::uint8_t> groupsMatched;   // built-in group indices, any order
    };
    struct ListIndex { std::vector<std::array<bool, kMaxChunks>> present; };   // [slot][chunk] exists and is non-empty
    struct Attachment { std::size_t slotIndex; std::uint8_t chunk; };
    struct NpcDecision { bool eligible = false; const char* skipReason = nullptr; ClassTier tier = ClassTier::NonMage; std::uint8_t group = kGroupNone; UndeadPolicy undead = UndeadPolicy::NoUndead; std::vector<Attachment> lists; std::size_t slotsWithoutList = 0; };
    // FNV-1a 64 of (plugin, local id, salt), run through MurmurHash3's fmix64 so salts that differ only in the last
    // byte (the rung digit) give independent values; top 53 bits -> [0,1)
    double Dice(std::string_view pluginLower, std::uint32_t localId, std::string_view salt);
    NpcDecision DecideNpc(const NpcFacts& npc, const Rules& rules, const ListIndex& lists);
    // One report row per NPC that got lists (Task 9 fills it, Task 10 renders it): persistentId "Plugin.esp|0x00XXXX" (catalog form),
    // display name; inheritedFrom: the spell list holder's persistentId for an NPC that got its holder's lists (empty for its own decision)
    struct NpcLine { std::string persistentId; std::string name; NpcDecision decision; std::string inheritedFrom; };
    // Skip reasons of NPCs whose spell list comes from a template (the plan's others: NpcSpellsPlan.h). The first is for a chain
    // whose holder cannot be told before spawn (a leveled link first, a malformed chain): PlanWrites gives it, and DecideNpc
    // to any NPC that reaches it with inheritsSpellList set (AttachToNpcs sends none there).
    inline constexpr const char* kSkipTemplateUnresolvable = "template spell list, holder not resolvable";
    inline constexpr const char* kSkipHolderGotNone = "template spell list, holder got no lists";
    // An NPC whose spell list comes from a template: the engine copies the holder's list into the NPC while loading, before the
    // addon runs, so lists put on the holder never reach it. It gets the holder's decision as it is (the same lists, tier, group
    // and undead policy, no dice of its own), so the template and its children behave alike. holder: the decision of the
    // record its spell list comes from, null when that record got no row (skipped, or no list). nullopt with skipReason
    // kSkipHolderGotNone when the holder has no list to copy.
    std::optional<NpcDecision> InheritDecision(const NpcDecision* holder, const char*& skipReason);
    // The race rules (README "Who gets what"): a beast race gets nothing, a creature only with a mage class. DecideNpc applies
    // them to an NPC that decides for itself; InheritedRaceSkip to one that takes its holder's decision, with its own race and
    // class (a child of another race than its holder), giving the plan's reason (kSkipInheritedBeast, kSkipInheritedCreature)
    // or null.
    enum class RaceRule { kNone, kBeast, kCreatureNotMage };
    ClassTier OwnClassTier(const NpcFacts& npc, const Rules& rules);
    RaceRule RaceRuleFor(const NpcFacts& npc, ClassTier tier);
    const char* InheritedRaceSkip(const NpcFacts& child, const Rules& rules);
}
