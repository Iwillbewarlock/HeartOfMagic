#include "core/NpcSpellsReport.h"

#include <format>

namespace NpcSpells {
namespace {
    constexpr std::array<const char*, kClassTierCount> kClassTierLabels{ "non-mage", "hybrid", "mage" };
    constexpr std::array<const char*, kUndeadPolicyCount> kUndeadLabels{ "no undead", "conjurer", "necromancer" };

    void Section(std::string& out, const char* title) { out += std::format("\n== {} ==\n", title); }
}

std::string RenderReport(const ReportData& d)
{
    std::string out = std::format("Heart of Magic - NPC Spells report\nVersion: {}\nGenerated (UTC): {}\nRules: {}\n",
        d.version, d.generatedUtc, d.rulesFromDefaults ? "built-in defaults" : "HeartOfMagic_NPCSpells.json");
    if (!d.rulesError.empty()) out += std::format("Rules error: {}\n", d.rulesError);
    if (!d.stopReason.empty()) out += std::format("\nStopped: {}\n", d.stopReason);

    Section(out, "Spells");
    out += std::format("tree nodes {}, duplicate persistentId {}, not in catalog {}, no school or tier {}, excluded by tag {}, by list {}, by other distribution {}, unresolved {}, school mismatch {}\nkept {}\n",
        d.pool.treeNodes, d.pool.duplicates, d.pool.notInCatalog, d.pool.noSchoolOrTier, d.pool.excludedTag, d.pool.excludedList, d.pool.excludedDistr, d.pool.unresolved, d.pool.schoolMismatch, d.pool.kept);

    Section(out, "Lists");
    out += std::format("lists built {}, empty slots {}, dropped over capacity {}, variant lists topped up from All {}\n"
                       "ESL records {}, missing {}, wrong type {}, lists skipped for a missing record {}, skippedPrefilled {} (record already had entries, left as it was), "
                       "skippedNoMemory {} (the game heap gave no entry array)\n"
                       "per school (alteration, conjuration, destruction, illusion, restoration): {}/{}/{}/{}/{}\n",
        d.build.lists, d.build.emptySlots, d.build.droppedOverCapacity, d.build.variantsToppedUp, d.eslRecords, d.eslMissing, d.eslWrongType, d.listsSkippedNoRecord, d.listsSkippedPrefilled, d.listsSkippedNoMemory,
        d.build.perSchool[0], d.build.perSchool[1], d.build.perSchool[2], d.build.perSchool[3], d.build.perSchool[4]);
    for (const auto& [id, entries, flags] : d.listSizes) out += std::format("{}  {}  flag {}\n", id, entries, flags);

    Section(out, "NPCs");
    out += std::format("scanned {}, eligible {}, got lists {} ({} lists), unresolvable template {}, multi-group {}, unresolved rule refs {}\n",
        d.npcsScanned, d.npcsEligible, d.npcsAttached, d.listsAttached, d.unresolvableTemplate, d.multiGroup, d.unresolvedRefs);
    out += std::format("by tier (non-mage/hybrid/mage): {}/{}/{}\nby group:", d.byTier[0], d.byTier[1], d.byTier[2]);
    for (std::size_t g = 0; g < d.byGroup.size(); ++g) out += std::format(" {} {}{}", kGroups[g].name, d.byGroup[g], g + 1 < d.byGroup.size() ? "," : "\n");
    out += "skipped:\n";
    for (const auto& [reason, count] : d.skipped) out += std::format("  {}  {}\n", reason, count);

    Section(out, "NPC lists");
    for (const auto& n : d.npcs) {
        const auto& dec = n.decision;
        out += std::format("{}  {}  group {}  {}, {}:", n.persistentId, n.name, kGroups[dec.group].name,
            kClassTierLabels[static_cast<std::size_t>(dec.tier)], kUndeadLabels[static_cast<std::size_t>(dec.undead)]);
        bool first = true;
        for (const auto& a : dec.lists) {
            out += std::format("{} {}", first ? "" : ",", EditorIdFor(SlotTable()[a.slotIndex].key, a.chunk));
            first = false;
        }
        out += '\n';
    }

    Section(out, "Timing");
    for (const auto& [phase, ms] : d.timingsMs) out += std::format("{}  {:.1f} ms\n", phase, ms);
    return out;
}
}
