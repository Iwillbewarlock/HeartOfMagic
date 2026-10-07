#include "core/NpcSpellsBuild.h"
#include "core/NpcSpellsSlots.h"
#include <algorithm>
namespace NpcSpells {
namespace {
    const GroupRule* GroupFor(const Rules& rules, std::uint8_t group) {
        if (group == kGroupNone) return nullptr;
        for (const auto& g : rules.groups) if (g.name == kGroups[group].name) return &g;
        return nullptr;
    }
}
namespace {
    // Flag 0: the spell's own tier floor. Flag 1 (below): the floor of the tier above it, but at least the rung's start tier, so an NPC of tier S draws only lower tiers.
    std::uint16_t EntryLevel(Tier t, const Rung* below) {
        const auto idx = static_cast<std::size_t>(t);
        if (!below) return kTierFloor[idx];
        return kTierFloor[std::max(idx + 1, static_cast<std::size_t>(below->startTier))];
    }
}
bool InSlot(const SpellFacts& s, const SlotKey& k, const Rules& rules)
{
    if (s.school != k.school) return false;
    const GroupRule* g = GroupFor(rules, k.group);
    if (g && s.HasAnyTag(g->never)) return false;
    if (k.nonMage) return s.tier == Tier::Novice && s.HasAnyTag(rules.nonMageTags);
    const Rung& rung = rules.ladder[k.rung];
    if (rung.below) { if (s.tier == Tier::Master) return false; }   // a below list gives the tiers under the NPC's own, so it also holds the lower ones (no level exists past master)
    else if (s.tier < rung.startTier) return false;
    const bool prefer = g && s.HasAnyTag(g->prefer);
    switch (k.variant) {
    case Variant::All: return true;
    case Variant::Prefer: return prefer && !(k.school == School::Conjuration && kGroups[k.group].policy == UndeadGroupPolicy::Mixed && s.undeadRule);
    case Variant::NecroPrefer: return s.undeadRule;
    case Variant::NoUndeadAll: return !s.undeadRule;
    }
    return false;
}
std::vector<BuiltList> BuildLists(const std::vector<SpellFacts>& pool, const Rules& rules, BuildStats& stats)
{
    std::vector<BuiltList> out;
    const auto& table = SlotTable();
    for (std::size_t slot = 0; slot < table.size(); ++slot) {
        const SlotKey& key = table[slot].key;
        std::array<std::vector<std::uint32_t>, kTierCount> byTier;
        for (std::uint32_t i = 0; i < pool.size(); ++i) if (InSlot(pool[i], key, rules)) byTier[static_cast<std::size_t>(pool[i].tier)].push_back(i);
        std::size_t total = 0; for (const auto& v : byTier) total += v.size();
        if (total == 0) { ++stats.emptySlots; continue; }
        // Split only over 255 (§5.4): a small slot stays one list, so a flag-0 list never misses a tier it holds
        const std::size_t chunkCount = std::min<std::size_t>(kMaxChunks, (total + kMaxEntries - 1) / kMaxEntries);
        const Rung* belowRung = (!key.nonMage && rules.ladder[key.rung].below) ? &rules.ladder[key.rung] : nullptr;
        std::array<std::vector<Entry>, kMaxChunks> chunks;
        const auto add = [&](std::vector<Entry>& target, std::uint32_t spell, std::size_t t) {
            if (target.size() >= kMaxEntries) { ++stats.droppedOverCapacity; return; }
            target.push_back({ spell, EntryLevel(static_cast<Tier>(t), belowRung) });
        };
        std::size_t next = 0;
        for (std::size_t t = 0; t < kTierCount; ++t) {
            if (chunkCount > 1 && byTier[t].size() < chunkCount) {   // a thin tier goes into every chunk so each chunk keeps every tier the slot holds
                for (std::uint32_t spell : byTier[t]) for (std::size_t c = 0; c < chunkCount; ++c) add(chunks[c], spell, t);
                continue;
            }
            for (std::uint32_t spell : byTier[t]) add(chunks[next++ % chunkCount], spell, t);
        }
        const std::uint8_t flags = belowRung ? kAllLevelsFlag : 0;
        for (std::uint8_t c = 0; c < kMaxChunks; ++c) {
            if (chunks[c].empty()) continue;
            std::stable_sort(chunks[c].begin(), chunks[c].end(), [](const Entry& a, const Entry& b) { return a.level < b.level; });
            out.push_back({ slot, c, flags, std::move(chunks[c]) });
            ++stats.lists; ++stats.perSchool[static_cast<std::size_t>(key.school)];
        }
    }
    return out;
}
}
