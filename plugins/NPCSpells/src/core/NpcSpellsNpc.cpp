#include "core/NpcSpellsNpc.h"
#include <format>
// Decision rules: spec sections 5.1-5.2 and 5.4. Every random choice is a Dice roll keyed by (plugin, local id, salt),
// so one NPC gets the same lists on every load. Deterministic per machine: the plugin name is hashed as the game gives
// its bytes, which follow the system code page for a non-ASCII name (an ASCII name hashes the same everywhere).
namespace NpcSpells {
namespace {
    constexpr std::uint64_t kFnvOffset = 14695981039346656037ull, kFnvPrime = 1099511628211ull;
    constexpr int kBitsPerByte = 8, kLocalIdBytes = 4;
    constexpr int kChunkHashShift = 8;   // chunk pick reads bits above the low byte
    std::uint64_t Hash(std::string_view pluginLower, std::uint32_t localId, std::string_view salt)
    {
        std::uint64_t h = kFnvOffset;
        auto mix = [&](unsigned char c) { h ^= c; h *= kFnvPrime; };
        for (unsigned char c : pluginLower) mix(c);
        mix('|');
        for (int i = 0; i < kLocalIdBytes; ++i) mix(static_cast<unsigned char>(localId >> (kBitsPerByte * i)));
        mix('|');
        for (unsigned char c : salt) mix(c);
        // MurmurHash3 fmix64. Plain FNV-1a barely moves its top bits when only the last byte differs (the rung
        // digit), which made rungs of one school a single coin; the finalizer spreads every bit over all 64.
        constexpr int kShift = 33;
        constexpr std::uint64_t kMul1 = 0xff51afd7ed558ccdull, kMul2 = 0xc4ceb9fe1a85ec53ull;
        h ^= h >> kShift; h *= kMul1; h ^= h >> kShift; h *= kMul2; h ^= h >> kShift;
        return h;
    }
    std::string RungSalt(School school, std::uint8_t rung) { return std::format("{}:R{}", kSchoolNames[static_cast<std::size_t>(school)], static_cast<int>(rung)); }
    // First present chunk scanning forward from the hashed start; nullopt when no chunk of the slot exists
    std::optional<std::uint8_t> PickChunk(const ListIndex& lists, std::size_t slot, std::uint64_t hash)
    {
        const auto start = static_cast<std::uint8_t>((hash >> kChunkHashShift) % kMaxChunks);
        for (std::uint8_t i = 0; i < kMaxChunks; ++i) {
            const auto c = static_cast<std::uint8_t>((start + i) % kMaxChunks);
            if (lists.present[slot][c]) return c;
        }
        return std::nullopt;
    }
    bool Attach(NpcDecision& d, const ListIndex& lists, const SlotKey& key, std::uint64_t hash)
    {
        const auto slot = FindSlot(key);
        if (!slot || *slot >= lists.present.size()) return false;
        const auto chunk = PickChunk(lists, *slot, hash);
        if (chunk) d.lists.push_back({ *slot, *chunk });
        return chunk.has_value();
    }
    double GroupPreferShare(const Rules& rules, std::uint8_t group)
    {
        for (const auto& gr : rules.groups) if (gr.name == kGroups[group].name) return gr.preferShare;
        return rules.conjurationShare;   // the None group has no GroupRule
    }
    // Variant for one school rung (5.2). Fallbacks: Prefer/NecroPrefer -> All; a NoUndead NPC never falls to All,
    // its Prefer falls to NoUndeadAll and a missing NoUndeadAll leaves the slot without a list.
    void AttachRung(NpcDecision& d, const NpcFacts& npc, const Rules& rules, const ListIndex& lists, School school, std::uint8_t rung)
    {
        const std::string salt = RungSalt(school, rung) + ":prefer";
        const double u = Dice(npc.pluginLower, npc.localId, salt);
        const std::uint64_t h = Hash(npc.pluginLower, npc.localId, salt);
        const auto policy = kGroups[d.group].policy;
        const bool none = d.group == kGroupNone;
        Variant first = Variant::All, second = Variant::All; bool noUndeadOnly = false;
        if (school != School::Conjuration || policy == UndeadGroupPolicy::NeverUndead) { if (!none) first = Variant::Prefer; }
        else if (d.undead == UndeadPolicy::Necromancer) first = Variant::NecroPrefer;
        else if (d.undead == UndeadPolicy::Conjurer) first = none ? Variant::NoUndeadAll : Variant::Prefer;
        else { noUndeadOnly = true; first = none ? Variant::NoUndeadAll : Variant::Prefer; second = Variant::NoUndeadAll; }
        const double share = first == Variant::NecroPrefer ? rules.conjurationShare : GroupPreferShare(rules, d.group);
        const Variant chosen = (first != Variant::All && u < share) ? first : second;
        if (Attach(d, lists, { school, d.group, chosen, rung, false }, h)) return;
        if (chosen != Variant::All && !noUndeadOnly && Attach(d, lists, { school, d.group, Variant::All, rung, false }, h)) return;
        if (noUndeadOnly && chosen != Variant::NoUndeadAll && Attach(d, lists, { school, d.group, Variant::NoUndeadAll, rung, false }, h)) return;
        ++d.slotsWithoutList;
    }
}
double Dice(std::string_view pluginLower, std::uint32_t localId, std::string_view salt)
{
    constexpr int kHashBits = 64, kMantissaBits = 53;                       // a double holds 53 bits exactly
    constexpr double kDiceScale = 1.0 / static_cast<double>(1ull << kMantissaBits);
    return static_cast<double>(Hash(pluginLower, localId, salt) >> (kHashBits - kMantissaBits)) * kDiceScale;   // top 53 bits -> [0,1)
}
NpcDecision DecideNpc(const NpcFacts& npc, const Rules& rules, const ListIndex& lists)
{
    NpcDecision d;
    if (npc.isPlayer) { d.skipReason = "player"; return d; }
    if (npc.inheritsSpellList) { d.skipReason = kSkipTemplateUnresolvable; return d; }
    if (npc.isBeast) { d.skipReason = "beast race"; return d; }
    d.tier = npc.hasClass ? ClassifyClass(npc.weights, rules) : ClassTier::NonMage;
    if (npc.isCreature && d.tier != ClassTier::Mage) { d.skipReason = "creature not a mage"; return d; }
    // Lowest built-in index wins (Forsworn < Vampire < Necromancer < Thalmor < Vigilant)
    for (std::uint8_t g : npc.groupsMatched)
        if (g != kGroupNone && g < kGroups.size() && (d.group == kGroupNone || g < d.group)) d.group = g;
    const auto policy = kGroups[d.group].policy;
    // Redguards are left out of the class-based undead rules (5.1): only a Vampire/Necromancer group makes one a necromancer
    // A never-undead group (Thalmor, Vigilant) takes no undead-rule spell whatever the class: report it as NoUndead
    if (policy == UndeadGroupPolicy::NeverUndead) d.undead = UndeadPolicy::NoUndead;
    else if (policy == UndeadGroupPolicy::AlwaysNecro || (npc.isNecromancerClass && !npc.isRedguard)) d.undead = UndeadPolicy::Necromancer;
    else if (npc.isUndeadException || (npc.hasClass && IsConjurationClass(npc.weights) && !npc.isRedguard)) d.undead = UndeadPolicy::Conjurer;
    std::array<bool, kSchoolCount> schools{};
    if (d.tier == ClassTier::Mage) schools.fill(true);
    else if (d.tier == ClassTier::Hybrid) schools = SchoolsWeighted(npc.weights);
    if (npc.isRedguard && d.tier != ClassTier::Mage) {
        // Only a hybrid vampire/necromancer keeps its conjuration share; a NonMage-tier Redguard never gets the 5% list (5.1)
        if (policy != UndeadGroupPolicy::AlwaysNecro || d.tier == ClassTier::NonMage) { d.skipReason = "redguard non-mage"; return d; }
        const auto conj = static_cast<std::size_t>(School::Conjuration);
        const bool keep = schools[conj];
        if (!keep) { d.skipReason = "redguard non-mage"; return d; }   // no conjuration weight: nothing left to give
        schools.fill(false);
        schools[conj] = keep;
    }
    d.eligible = true;
    const double scale = d.tier == ClassTier::Hybrid ? rules.hybridScale : 1.0;
    for (std::size_t s = 0; s < kSchoolCount; ++s) {
        if (!schools[s]) continue;
        const auto school = static_cast<School>(s);
        for (std::uint8_t r = 0; r < rules.ladder.size() && r < kRungCount; ++r)
            if (Dice(npc.pluginLower, npc.localId, RungSalt(school, r)) < rules.ladder[r].chance * scale) AttachRung(d, npc, rules, lists, school, r);
    }
    if (d.tier == ClassTier::NonMage) {
        constexpr std::string_view kNonMageSalt = "nonmage";
        constexpr double kHalf = 0.5;   // Restoration and Alteration split the non-mage chance evenly
        const double u = Dice(npc.pluginLower, npc.localId, kNonMageSalt);
        const std::uint64_t h = Hash(npc.pluginLower, npc.localId, kNonMageSalt);
        std::optional<School> school;
        if (u < rules.nonMageChance * kHalf) school = School::Restoration;
        else if (u < rules.nonMageChance) school = School::Alteration;
        if (school && !Attach(d, lists, { *school, d.group, Variant::All, 0, true }, h)) ++d.slotsWithoutList;
    }
    return d;
}
std::optional<NpcDecision> InheritDecision(const NpcDecision* holder, const char*& skipReason)
{
    if (!holder || holder->lists.empty()) { skipReason = kSkipHolderGotNone; return std::nullopt; }
    skipReason = nullptr;
    return *holder;
}
}
