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
bool InSlot(const SpellFacts& s, const SlotKey& k, const Rules& rules)
{
    if (s.school != k.school) return false;
    const GroupRule* g = GroupFor(rules, k.group);
    if (g && s.HasAnyTag(g->never)) return false;
    if (k.nonMage) return s.tier == Tier::Novice && s.HasAnyTag(rules.nonMageTags);
    if (s.tier < rules.ladder[k.rung].startTier) return false;
    const bool prefer = g && s.HasAnyTag(g->prefer);
    switch (k.variant) {
    case Variant::All: return true;
    case Variant::Prefer: return prefer && !(k.school == School::Conjuration && kGroups[k.group].policy == UndeadGroupPolicy::Mixed && s.undeadRule);
    case Variant::NecroPrefer: return s.undeadRule || prefer;
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
        std::array<std::vector<Entry>, kMaxChunks> chunks;
        std::size_t next = 0;
        for (std::size_t t = 0; t < kTierCount; ++t)
            for (std::uint32_t spell : byTier[t]) {
                auto& target = chunks[next % chunkCount]; ++next;
                if (target.size() >= kMaxEntries) { ++stats.droppedOverCapacity; continue; }
                target.push_back({ spell, kTierFloor[t] });
            }
        const std::uint8_t flags = (!key.nonMage && rules.ladder[key.rung].below) ? 1 : 0;
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
