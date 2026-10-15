#include "NpcSpellsCheck.h"
#include "core/NpcSpellsNpc.h"
#include "core/NpcSpellsTemplate.h"
#include "core/NpcSpellsSlots.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>
#include <utility>
using namespace NpcSpells; using NpcSpellsCheck::Check;
namespace {
    ListIndex AllPresent() { ListIndex li; li.present.assign(SlotTable().size(), { true, true, true }); return li; }
    ClassWeights Mage() { ClassWeights c; c.w[12] = 1; c.w[13] = 3; c.w[14] = 2; c.w[15] = 1; c.w[16] = 3; c.w[0] = 1; return c; }       // CombatMageConjurer
    ClassWeights Elemental() { ClassWeights c; c.w[12] = 2; c.w[13] = 1; c.w[14] = 4; c.w[15] = 1; c.w[16] = 2; c.w[0] = 1; return c; }  // destruction-led mage
    ClassWeights Warrior() { ClassWeights c; c.w[0] = 3; c.w[3] = 3; c.w[5] = 3; return c; }
    ClassWeights Spellsword() { ClassWeights c; c.w[0] = 3; c.w[5] = 3; c.w[12] = 1; c.w[14] = 2; c.w[16] = 2; return c; }               // .455 hybrid, no conjuration weight
    ClassWeights BattleConjurer() { ClassWeights c; c.w[0] = 3; c.w[5] = 3; c.w[13] = 2; c.w[14] = 1; return c; }                         // 3 of 9 = .333 hybrid, conjuration and destruction weighted
    NpcFacts Npc(std::uint32_t id, ClassWeights w) { NpcFacts n; n.pluginLower = "test.esp"; n.localId = id; n.hasClass = true; n.weights = w; return n; }
    std::size_t CountSchool(const NpcDecision& d, School s) { std::size_t n = 0; for (const auto& a : d.lists) if (SlotTable()[a.slotIndex].key.school == s && !SlotTable()[a.slotIndex].key.nonMage) ++n; return n; }
    bool AnyVariant(const NpcDecision& d, School s, Variant v) { for (const auto& a : d.lists) if (SlotTable()[a.slotIndex].key.school == s && SlotTable()[a.slotIndex].key.variant == v) return true; return false; }
    // Conjuration attachments of one variant over many local ids: {variant hits, all conjuration hits}
    std::pair<std::size_t, std::size_t> ConjVariantShare(NpcFacts n, const Rules& rules, const ListIndex& lists, Variant v, std::uint32_t firstId, int count) {
        std::size_t hits = 0, total = 0;
        for (int i = 0; i < count; ++i) { n.localId = firstId + i;
            for (const auto& a : DecideNpc(n, rules, lists).lists) { const auto& k = SlotTable()[a.slotIndex].key; if (k.school != School::Conjuration || k.nonMage) continue; ++total; hits += k.variant == v ? 1 : 0; } }
        return { hits, total };
    }
    bool ShareNear(std::pair<std::size_t, std::size_t> p, double want) { return p.second > 0 && std::abs(double(p.first) / p.second - want) < 0.02; }
}
int RunNpcSelfTest()
{
    std::printf("template\n");
    Check(ProviderIndex({ { 0, false } }, Category::Stats) == 0, "no flags: the record provides its own stats");
    Check(ProviderIndex({ { 0x2, false }, { 0, false } }, Category::Stats) == 1, "kStats: the template provides");
    Check(ProviderIndex({ { 0x2, false }, { 0x2, false }, { 0, false } }, Category::Stats) == 2, "chain of two templates");
    Check(!ProviderIndex({ { 0x2, false }, { 0, true } }, Category::Stats).has_value(), "a leveled character provider is unresolvable");
    Check(ProviderIndex({ { 0x2, false }, { 0, true } }, Category::Factions) == 0, "a category without the flag stays on the record even when the template is leveled");
    Check(SpellListProviderIndex({ { 0x8, false } }) == 0 && !ProviderIndex({ { 0x8, false } }, Category::Spells).has_value(), "spell list: the Spells flag with no template link keeps the record's own list");
    Check(SpellListProviderIndex({ { 0x8, false }, { 0x8, false }, { 0, false } }) == 2 && SpellListProviderIndex({ { 0, false }, { 0, false } }) == 0, "spell list: a template chain is followed as ProviderIndex does; no flag stays on the record");
    Check(!SpellListProviderIndex({ { 0x8, false }, { 0, true } }).has_value(), "spell list: a leveled template before the provider is unresolvable");

    std::printf("npc\n");
    const Rules rules = DefaultRules(); const ListIndex all = AllPresent();
    NpcFacts inherits = Npc(1, Mage()); inherits.inheritsSpellList = true;
    Check(!DecideNpc(inherits, rules, all).eligible, "template spell list -> skipped");
    {   // inheritance: an NPC whose spell list comes from a template takes its holder's decision as it is
        NpcFacts holderFacts = Npc(500, Mage()); holderFacts.groupsMatched = { 4 };
        const auto holder = DecideNpc(holderFacts, rules, all);
        const char* why = "unset";
        const auto copy = InheritDecision(&holder, why);
        Check(!holder.lists.empty() && copy && why == nullptr && copy->lists.size() == holder.lists.size() &&
              std::equal(copy->lists.begin(), copy->lists.end(), holder.lists.begin(), [](const Attachment& x, const Attachment& y) { return x.slotIndex == y.slotIndex && x.chunk == y.chunk; }) &&
              copy->tier == holder.tier && copy->group == 4 && copy->undead == holder.undead, "inherited: the holder's lists, tier, group and undead policy, copied");
        // No dice of its own: two children of one holder get the same lists, though their own ids would roll differently
        Check(InheritDecision(&holder, why)->lists.size() == copy->lists.size(), "inherited: every child of one holder gets the same copy");
        NpcDecision skippedHolder; skippedHolder.skipReason = "beast race";
        Check(!InheritDecision(&skippedHolder, why) && std::string_view(why) == kSkipHolderGotNone, "inherited: a holder skipped (no lists) gives nothing");
        NpcDecision eligibleEmpty; eligibleEmpty.eligible = true; eligibleEmpty.slotsWithoutList = 3;
        Check(!InheritDecision(&eligibleEmpty, why) && std::string_view(why) == kSkipHolderGotNone, "inherited: an eligible holder whose dice gave no list gives nothing");
        Check(!InheritDecision(nullptr, why) && std::string_view(why) == kSkipHolderGotNone, "inherited: a holder with no row (skipped before the decision) gives nothing");
        // A chain with a leveled link before the holder cannot be resolved: such an NPC reaches DecideNpc and stays skipped
        Check(!SpellListProviderIndex({ { 0x8, false }, { 0x8, true }, { 0, false } }).has_value(), "inherited: a leveled link before the holder is unresolvable");
        NpcFacts unresolvable = Npc(501, Mage()); unresolvable.inheritsSpellList = true;
        const auto u = DecideNpc(unresolvable, rules, all);
        Check(!u.eligible && u.lists.empty() && u.skipReason && std::string_view(u.skipReason) == kSkipTemplateUnresolvable, "inherited: an unresolvable chain is skipped as 'holder not resolvable'");
    }
    NpcFacts beast = Npc(2, Mage()); beast.isBeast = true;
    Check(!DecideNpc(beast, rules, all).eligible, "beast race -> skipped");
    NpcFacts creatureWarrior = Npc(3, Warrior()); creatureWarrior.isCreature = true;
    Check(!DecideNpc(creatureWarrior, rules, all).eligible, "creature without a mage class -> skipped");
    NpcFacts hagraven = Npc(4, Mage()); hagraven.isCreature = true;
    Check(DecideNpc(hagraven, rules, all).eligible && DecideNpc(hagraven, rules, all).tier == ClassTier::Mage, "creature with a mage class receives the mage share");
    NpcFacts redWarrior = Npc(5, Warrior()); redWarrior.isRedguard = true;
    Check(!DecideNpc(redWarrior, rules, all).eligible, "Redguard non-mage -> nothing");
    NpcFacts redMage = Npc(6, Mage()); redMage.isRedguard = true;   // a conjuration class: undead rule 1 skips Redguards
    { bool noUndead = true, gotNoUndeadAll = false; for (int i = 0; i < 200; ++i) { redMage.localId = 1000 + i; const auto d = DecideNpc(redMage, rules, all);
        noUndead = noUndead && d.eligible && d.undead == UndeadPolicy::NoUndead && !AnyVariant(d, School::Conjuration, Variant::All) && !AnyVariant(d, School::Conjuration, Variant::NecroPrefer);
        gotNoUndeadAll = gotNoUndeadAll || AnyVariant(d, School::Conjuration, Variant::NoUndeadAll); }
      Check(noUndead && gotNoUndeadAll, "Redguard conjuration-class mage: conjuration only from NoUndeadAll"); }
    NpcFacts plainConjurer = Npc(13, Mage());
    Check(DecideNpc(plainConjurer, rules, all).undead == UndeadPolicy::Conjurer, "non-Redguard conjuration-class mage is a conjurer");
    NpcFacts redNecroClass = Npc(14, Mage()); redNecroClass.isRedguard = true; redNecroClass.isNecromancerClass = true;
    { bool noUndead = true, gotNoUndeadAll = false; for (int i = 0; i < 200; ++i) { redNecroClass.localId = 5000 + i; const auto d = DecideNpc(redNecroClass, rules, all);
        noUndead = noUndead && d.undead == UndeadPolicy::NoUndead && !AnyVariant(d, School::Conjuration, Variant::All) && !AnyVariant(d, School::Conjuration, Variant::NecroPrefer);
        gotNoUndeadAll = gotNoUndeadAll || AnyVariant(d, School::Conjuration, Variant::NoUndeadAll); }
      Check(noUndead && gotNoUndeadAll, "Redguard CombatMageNecro outside Vampire/Necromancer: NoUndead, conjuration only from NoUndeadAll"); }
    NpcFacts redSpellswordVampire = Npc(15, Spellsword()); redSpellswordVampire.isRedguard = true; redSpellswordVampire.groupsMatched = { 2 };
    { const auto d = DecideNpc(redSpellswordVampire, rules, all);
      Check(!d.eligible && d.lists.empty() && d.skipReason && std::string_view(d.skipReason) == "redguard non-mage", "Redguard vampire hybrid without conjuration weight: skipped as redguard non-mage"); }
    NpcFacts falion = Npc(7, Mage()); falion.isRedguard = true; falion.isUndeadException = true;
    Check(DecideNpc(falion, rules, all).undead == UndeadPolicy::Conjurer, "Falion is a conjurer despite being Redguard");
    NpcFacts redVampire = Npc(8, BattleConjurer()); redVampire.isRedguard = true; redVampire.groupsMatched = { 2 };
    { const auto d = DecideNpc(redVampire, rules, all); Check(d.eligible && d.group == 2 && d.undead == UndeadPolicy::Necromancer, "Redguard vampire hybrid is eligible via the Vampire group");
      std::size_t conjLists = 0; bool onlyConj = true;
      for (int i = 0; i < 200; ++i) { redVampire.localId = 3000 + i;
          for (const auto& a : DecideNpc(redVampire, rules, all).lists) { const bool conj = SlotTable()[a.slotIndex].key.school == School::Conjuration && !SlotTable()[a.slotIndex].key.nonMage; onlyConj = onlyConj && conj; conjLists += conj ? 1 : 0; } }
      Check(onlyConj && conjLists > 0, "...and only for Conjuration (destruction is weighted too but dropped)"); }
    NpcFacts redWarriorVampire = Npc(12, Warrior()); redWarriorVampire.isRedguard = true; redWarriorVampire.groupsMatched = { 2 };
    { bool none = true; for (int i = 0; i < 200; ++i) { redWarriorVampire.localId = 4000 + i; const auto d = DecideNpc(redWarriorVampire, rules, all); none = none && !d.eligible && d.lists.empty(); }
      Check(none, "Redguard non-mage vampire: nothing, not even the 5% curing/armor list"); }
    NpcFacts vampNecro = Npc(9, Mage()); vampNecro.groupsMatched = { 3, 2 };
    Check(DecideNpc(vampNecro, rules, all).group == 2, "Vampire precedes Necromancer (built-in order)");
    NpcFacts necroClass = Npc(10, Elemental()); necroClass.isNecromancerClass = true;
    Check(DecideNpc(necroClass, rules, all).undead == UndeadPolicy::Necromancer, "CombatMageNecro is a necromancer without a faction");
    NpcFacts thalmorNecro = Npc(11, Mage()); thalmorNecro.isNecromancerClass = true; thalmorNecro.groupsMatched = { 4 };
    // NecroPrefer/NoUndeadAll slots do not exist for NeverUndead groups, so check the split that does: Prefer ~75%
    const int kShareNpcs = 5000;
    Check(ShareNear(ConjVariantShare(thalmorNecro, rules, all, Variant::Prefer, 20000, kShareNpcs), 0.75), "Thalmor group: necromancer class gets Prefer ~75% of conjuration (never undead overrides the class)");
    NpcFacts vampire = Npc(16, Mage()); vampire.groupsMatched = { 2 };
    Check(ShareNear(ConjVariantShare(vampire, rules, all, Variant::NecroPrefer, 30000, kShareNpcs), 0.75), "Vampire: NecroPrefer ~75% of conjuration");
    Check(ShareNear(ConjVariantShare(vampire, rules, all, Variant::All, 30000, kShareNpcs), 0.25), "Vampire: All ~25% of conjuration");
    NpcFacts necroNoGroup = Npc(17, Elemental()); necroNoGroup.isNecromancerClass = true;
    Check(ShareNear(ConjVariantShare(necroNoGroup, rules, all, Variant::NecroPrefer, 40000, kShareNpcs), 0.75), "necromancer class, no group: NecroPrefer ~75% of conjuration");
    Check(ShareNear(ConjVariantShare(necroNoGroup, rules, all, Variant::All, 40000, kShareNpcs), 0.25), "necromancer class, no group: All ~25% of conjuration");
    Check(ShareNear(ConjVariantShare(plainConjurer, rules, all, Variant::NoUndeadAll, 50000, kShareNpcs), 0.75), "conjurer, no group: NoUndeadAll ~75% of conjuration");
    Check(ShareNear(ConjVariantShare(plainConjurer, rules, all, Variant::All, 50000, kShareNpcs), 0.25), "conjurer, no group: All ~25% of conjuration");

    { const auto d = DecideNpc(thalmorNecro, rules, all); Check(d.undead == UndeadPolicy::NoUndead, "Thalmor necromancer class reports undead policy NoUndead (the group's never undead rules)"); }

    // Fallback chain when a variant list is missing (AttachRung): Prefer/NecroPrefer -> All; a NoUndead NPC's Prefer -> NoUndeadAll, never All
    auto without = [](std::initializer_list<Variant> gone) { ListIndex li = AllPresent();
        for (std::size_t s = 0; s < SlotTable().size(); ++s) for (Variant v : gone) if (SlotTable()[s].key.variant == v && !SlotTable()[s].key.nonMage) li.present[s] = { false, false, false };
        return li; };
    struct Tally { std::size_t schoolHits = 0, wrongVariant = 0, withoutList = 0; };
    auto tally = [&](NpcFacts n, const ListIndex& li, School school, std::initializer_list<Variant> allowed, std::uint32_t firstId) { Tally t;
        for (int i = 0; i < 200; ++i) { n.localId = firstId + i; const auto d = DecideNpc(n, rules, li); t.withoutList += d.slotsWithoutList;
            for (const auto& at : d.lists) { const auto& k = SlotTable()[at.slotIndex].key; if (k.school != school || k.nonMage) continue; ++t.schoolHits;
                if (std::find(allowed.begin(), allowed.end(), k.variant) == allowed.end()) ++t.wrongVariant; } }
        return t; };
    const ListIndex noPrefer = without({ Variant::Prefer }), noNecro = without({ Variant::NecroPrefer }), noUndeadAll = without({ Variant::NoUndeadAll });
    const ListIndex noPreferNoUndeadAll = without({ Variant::Prefer, Variant::NoUndeadAll });
    NpcFacts forswornMage = Npc(18, Elemental()); forswornMage.groupsMatched = { 1 };
    { const auto t = tally(forswornMage, noPrefer, School::Destruction, { Variant::All }, 60000);
      Check(t.schoolHits > 0 && t.wrongVariant == 0 && t.withoutList == 0, "Prefer missing -> All (Forsworn destruction)"); }
    for (std::uint8_t g : { std::uint8_t{ 4 }, std::uint8_t{ 5 } }) { NpcFacts n = Npc(19, Mage()); n.groupsMatched = { g };
      const auto dest = tally(n, noPrefer, School::Destruction, { Variant::All }, 61000), conj = tally(n, noPrefer, School::Conjuration, { Variant::All }, 61000);
      Check(dest.schoolHits > 0 && conj.schoolHits > 0 && dest.wrongVariant == 0 && conj.wrongVariant == 0 && dest.withoutList == 0,
            std::format("{} Prefer missing -> All (destruction and conjuration)", kGroups[g].name).c_str()); }
    { const auto t = tally(vampire, noNecro, School::Conjuration, { Variant::All }, 62000);
      Check(t.schoolHits > 0 && t.wrongVariant == 0 && t.withoutList == 0, "NecroPrefer missing -> All (Vampire)"); }
    { const auto t = tally(necroNoGroup, noNecro, School::Conjuration, { Variant::All }, 63000);
      Check(t.schoolHits > 0 && t.wrongVariant == 0 && t.withoutList == 0, "NecroPrefer missing -> All (necromancer class, no group)"); }
    { const auto t = tally(plainConjurer, noUndeadAll, School::Conjuration, { Variant::All }, 64000);
      Check(t.schoolHits > 0 && t.wrongVariant == 0 && t.withoutList == 0, "conjurer, no group: NoUndeadAll missing -> All"); }
    { const auto t = tally(forswornMage, noPrefer, School::Conjuration, { Variant::NoUndeadAll }, 65000);
      Check(t.schoolHits > 0 && t.wrongVariant == 0 && t.withoutList == 0, "NoUndead Forsworn: Prefer missing -> NoUndeadAll, never All"); }
    { const auto t = tally(forswornMage, noPreferNoUndeadAll, School::Conjuration, {}, 66000);
      Check(t.schoolHits == 0 && t.withoutList > 0, "NoUndead Forsworn: Prefer and NoUndeadAll missing -> no conjuration list (All is never used), counted"); }
    { const auto t = tally(Npc(20, Elemental()), noUndeadAll, School::Conjuration, {}, 67000);
      Check(t.schoolHits == 0 && t.withoutList > 0, "NoUndead, no group: NoUndeadAll missing -> no conjuration list, counted"); }

    // Expected counts: 20,000 mage NPCs; rung attach frequency equals chance; per-tier expectation equals the ladder sum
    std::array<double, kRungCount> rungHits{}; std::size_t preferHits = 0, preferTotal = 0; const int kNpcs = 20000;
    std::size_t bothR1R2 = 0;   // rungs 1 and 2 share a chance; their dice must be independent, not one coin
    for (int i = 0; i < kNpcs; ++i) { NpcFacts m = Npc(100000 + i, Mage()); m.groupsMatched = { 4 }; const auto d = DecideNpc(m, rules, all);
        bool r1 = false, r2 = false;
        for (const auto& a : d.lists) { const auto& k = SlotTable()[a.slotIndex].key; if (k.school != School::Destruction || k.nonMage) continue; rungHits[k.rung] += 1; ++preferTotal; if (k.variant == Variant::Prefer) ++preferHits; r1 = r1 || k.rung == 1; r2 = r2 || k.rung == 2; }
        bothR1R2 += (r1 && r2) ? 1 : 0; }
    bool freq = true; for (std::size_t r = 0; r < kRungCount; ++r) freq = freq && std::abs(rungHits[r] / kNpcs - rules.ladder[r].chance) < 0.02;
    Check(freq, "rung attach frequency within 0.02 of its chance over 20,000 NPCs");
    Check(std::abs(double(bothR1R2) / kNpcs - rules.ladder[1].chance * rules.ladder[2].chance) < 0.01, "rungs 1 and 2 attach independently (both ~0.0625, not 0.25)");
    Check(std::abs(double(preferHits) / preferTotal - 0.75) < 0.02, "Prefer variant chosen ~75% of the time");
    std::array<double, kTierCount> expectedPerTier{}; for (std::size_t r = 0; r < kRungCount; ++r) for (std::size_t t = static_cast<std::size_t>(rules.ladder[r].startTier); t < kTierCount; ++t) expectedPerTier[t] += rungHits[r] / kNpcs;
    std::array<double, kTierCount> ladderPerTier{}; for (const auto& rung : rules.ladder) for (std::size_t t = static_cast<std::size_t>(rung.startTier); t < kTierCount; ++t) ladderPerTier[t] += rung.chance;
    bool targets = true; for (std::size_t t = 0; t < kTierCount; ++t) targets = targets && std::abs(expectedPerTier[t] - ladderPerTier[t]) < 0.05;
    Check(targets, "expected spells per tier match the ladder (sum of chances of the rungs reaching the tier) within 0.05");
    // Hybrid: half, weighted schools only
    double hybridDest = 0; std::size_t hybridConj = 0;
    for (int i = 0; i < kNpcs; ++i) { const auto d = DecideNpc(Npc(200000 + i, Spellsword()), rules, all); hybridDest += CountSchool(d, School::Destruction); hybridConj += CountSchool(d, School::Conjuration); }
    Check(hybridConj == 0, "hybrid gets nothing in an unweighted school");
    Check(std::abs(hybridDest / kNpcs - 0.5 * (1.0 + 0.25 + 0.25 + 0.5 + 0.5)) < 0.05, "hybrid attach expectation is half the mage's");
    // Non-mage: 5% one novice curing/armor list
    std::size_t nonMageLists = 0; for (int i = 0; i < kNpcs; ++i) { const auto d = DecideNpc(Npc(300000 + i, Warrior()), rules, all); nonMageLists += d.lists.size(); }
    Check(std::abs(double(nonMageLists) / kNpcs - 0.05) < 0.01, "non-mage: about 5% get one list");
    // Determinism and missing lists
    const auto a = DecideNpc(Npc(42, Mage()), rules, all), b = DecideNpc(Npc(42, Mage()), rules, all);
    Check(a.lists.size() == b.lists.size() && std::equal(a.lists.begin(), a.lists.end(), b.lists.begin(), [](const Attachment& x, const Attachment& y) { return x.slotIndex == y.slotIndex && x.chunk == y.chunk; }), "same NPC, same decision");
    ListIndex none; none.present.assign(SlotTable().size(), { false, false, false });
    const auto empty = DecideNpc(Npc(43, Mage()), rules, none);
    Check(empty.eligible && empty.lists.empty() && empty.slotsWithoutList > 0, "no lists present: nothing attached, counted");
    ListIndex onlyChunk2; onlyChunk2.present.assign(SlotTable().size(), { false, false, true });
    { bool ok = true; for (const auto& at : DecideNpc(Npc(44, Mage()), rules, onlyChunk2).lists) ok = ok && at.chunk == 2; Check(ok, "only present chunks are attached"); }
    Check(Dice("a.esp", 1, "x") != Dice("a.esp", 2, "x") && Dice("a.esp", 1, "x") >= 0.0 && Dice("a.esp", 1, "x") < 1.0, "dice differ per NPC and stay in [0,1)");
    return NpcSpellsCheck::g_failed;
}
