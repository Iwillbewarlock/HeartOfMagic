#pragma once
#include "core/NpcSpellsBuild.h"
#include "core/NpcSpellsNpc.h"
#include "core/NpcSpellsPool.h"
#include <array>
#include <cstdint>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
namespace NpcSpells {
    // Everything one run knows, rendered as the developer report (UTF-8 text; persistent ids are allowed here, never in player text)
    struct ReportData {
        std::string version, generatedUtc; bool rulesFromDefaults = false; std::string rulesError; std::string stopReason;
        PoolStats pool; BuildStats build; std::size_t eslRecords = 0, eslMissing = 0, eslWrongType = 0, listsSkippedNoRecord = 0, listsSkippedPrefilled = 0, listsSkippedNoMemory = 0;   // prefilled: the record already held entries (left as it was); no memory: the game heap gave no entry array
        std::vector<std::tuple<std::string, std::size_t, std::uint8_t>> listSizes;   // editorId, entries, flags: the filled lists only (a skipped one is only counted)
        std::size_t npcsScanned = 0, npcsEligible = 0, npcsAttached = 0, listsAttached = 0, unresolvableTemplate = 0, multiGroup = 0, unresolvedRefs = 0;
        // NPCs written with their spell list holder's lists (InheritDecision), those lists, of them the ones given new spell list
        // data (kCopiedTemplate and none), and NPCs that got them through spell list data shared with a written record, of them
        // the ones their own race rule would have given nothing (NpcStats)
        std::size_t inheritedAttached = 0, inheritedLists = 0, inheritedCreated = 0, inheritedViaShared = 0, viaSharedDespiteOwnSkip = 0;
        // NPCs that inherit their spell list (NpcStats, GameNpcs.h): all; without kCopiedTemplate with / without a leveled
        // chain link; met before their holder; class, race or factions from another record; spell list data shared with a chain
        // record; a holder at the chain's end with the Spells flag and no template; the memory audit: records whose leveled-list
        // array another spell list data shares, the writes left out, spell list data shared outside one template chain
        std::size_t inheriting = 0, notCopiedLeveledChain = 0, notCopiedPlainChain = 0, beforeHolder = 0, otherProvider = 0, sharedSpellData = 0, holderFlagNoTemplate = 0;
        std::size_t sharedLevSpells = 0, blockedWrites = 0, sharedSpellDataOutsideChain = 0;
        std::vector<std::pair<std::string, std::size_t>> skipped;                     // reason, count (copied from NpcStats::skipped)
        std::array<std::size_t, kClassTierCount> byTier{}; std::array<std::size_t, kGroupCount> byGroup{};
        std::vector<NpcLine> npcs;                                                    // every NPC that got lists (own decisions in scan order, then inherited ones; no cap)
        std::vector<std::pair<std::string, double>> timingsMs;
    };
    std::string RenderReport(const ReportData& d);
}
