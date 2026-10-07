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
        PoolStats pool; BuildStats build; std::size_t eslRecords = 0, eslMissing = 0, eslWrongType = 0, listsSkippedNoRecord = 0, listsSkippedPrefilled = 0;   // prefilled: the record already held entries (left as it was)
        std::vector<std::tuple<std::string, std::size_t, std::uint8_t>> listSizes;   // editorId, entries, flags
        std::size_t npcsScanned = 0, npcsEligible = 0, npcsAttached = 0, listsAttached = 0, unresolvableTemplate = 0, multiGroup = 0, unresolvedRefs = 0;
        std::vector<std::pair<std::string, std::size_t>> skipped;                     // reason, count (copied from NpcStats::skipped)
        std::array<std::size_t, 3> byTier{}; std::array<std::size_t, 6> byGroup{};
        std::vector<NpcLine> npcs;                                                    // every NPC that got lists, in scan order (no cap)
        std::vector<std::pair<std::string, double>> timingsMs;
    };
    std::string RenderReport(const ReportData& d);
}
