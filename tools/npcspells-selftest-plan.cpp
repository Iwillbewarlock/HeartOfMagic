#include "NpcSpellsCheck.h"
#include "core/NpcSpellsNpc.h"
#include "core/NpcSpellsPlan.h"
#include "core/NpcSpellsRules.h"
#include <string_view>
#include <vector>
using namespace NpcSpells; using NpcSpellsCheck::Check;
namespace {
    PlanRecord Holder(bool lists = true) { PlanRecord r; r.hasLists = lists; r.hasSpellData = true; return r; }
    PlanRecord Child(std::size_t holder) { PlanRecord r; r.inheriting = true; r.holderResolved = true; r.holder = holder; r.hasSpellData = true; r.copiedTemplate = true; return r; }
    bool Is(const PlanStep& s, PlanAction a, std::size_t listsFrom) { return s.action == a && s.listsFrom == listsFrom && s.skipReason == nullptr; }
    bool SameReason(const char* a, const char* b) { return a && b && std::string_view(a) == b; }   // by text: a literal may have one address per TU
    bool Skipped(const PlanStep& s, const char* reason) { return s.action == PlanAction::kNone && SameReason(s.skipReason, reason); }
    ClassWeights MageWeights() { ClassWeights c; c.w[12] = 1; c.w[13] = 3; c.w[14] = 2; c.w[15] = 1; c.w[16] = 3; c.w[0] = 1; return c; }
    ClassWeights WarriorWeights() { ClassWeights c; c.w[0] = 3; c.w[3] = 3; c.w[5] = 3; return c; }
}
int RunPlanSelfTest()
{
    std::printf("plan\n");
    {   // the form array is in load order: a child met before its holder still gets the holder's lists
        const auto s = PlanWrites({ Child(1), Holder() });
        Check(Is(s[1], PlanAction::kWriteOwn, 1) && Is(s[0], PlanAction::kWriteInherited, 1), "child before its holder in the array: the holder is written, the child gets its lists");
    }
    {   // a holder whose leveled-list array another spell list data shares is never written; its child says why
        auto h = Holder(); h.blocked = true;
        const auto s = PlanWrites({ h, Child(0) });
        Check(Skipped(s[0], kSkipSharedLevArray) && s[0].blockedWrite, "a blocked holder is not written, counted as a blocked write");
        Check(Skipped(s[1], kSkipHolderBlocked) && !s[1].blockedWrite, "its child: 'holder not written (shared leveled-list array)', not 'holder got no lists'");
        auto c = Child(0); c.blocked = true;
        const auto t = PlanWrites({ Holder(), c });
        Check(Is(t[0], PlanAction::kWriteOwn, 0) && Skipped(t[1], kSkipSharedLevArray) && t[1].blockedWrite, "a blocked child is not written; its holder is");
    }
    {   // spell list data shared with the holder: the holder's write reaches the child, nothing is written to it
        auto c = Child(0); c.sharesSpellData = 0;
        const auto s = PlanWrites({ Holder(), c });
        Check(s[1].action == PlanAction::kViaShared && s[1].listsFrom == 0 && s[1].via == 0 && !IsWrite(s[1].action), "SpellData shared with the holder: via shared, the holder's lists, no write of its own");
        const auto none = PlanWrites({ Holder(false), c });
        Check(Skipped(none[1], kSkipHolderGotNone), "SpellData shared with a holder that gets no lists: nothing, 'holder got no lists'");
        auto blocked = Holder(); blocked.blocked = true;
        Check(Skipped(PlanWrites({ blocked, c })[1], kSkipHolderBlocked), "SpellData shared with a blocked holder: 'holder not written'");
        // Shared with a middle record of the chain that inherits too: it gets what that record gets
        auto mid = Child(0); auto low = Child(0); low.sharesSpellData = 1;
        const auto m = PlanWrites({ Holder(), mid, low });
        Check(Is(m[1], PlanAction::kWriteInherited, 0) && m[2].action == PlanAction::kViaShared && m[2].listsFrom == 0 && m[2].via == 1, "shared with an inheriting chain record: through that record's write, the holder's lists");
        // ...listed before it and shared in turn (a chain of shared data settles in rounds)
        auto a = Child(3); a.sharesSpellData = 1; auto b = Child(3); b.sharesSpellData = 2;
        const auto r = PlanWrites({ a, b, Child(3), Holder() });
        Check(r[0].action == PlanAction::kViaShared && r[1].action == PlanAction::kViaShared && r[0].listsFrom == 3, "a chain of shared spell list data settles whatever the order");
        auto x = Child(2); x.sharesSpellData = 1; auto y = Child(2); y.sharesSpellData = 0;
        const auto cyc = PlanWrites({ x, y, Holder() });
        Check(Skipped(cyc[0], kSkipSharedSpellData) && Skipped(cyc[1], kSkipSharedSpellData), "a cycle of shared spell list data settles nothing: both skipped");
        auto own = Holder(false); auto z = Child(2); z.sharesSpellData = 0;
        Check(Skipped(PlanWrites({ own, z, Holder() })[1], kSkipSharedSpellData), "shared with a chain record that is not its holder and gets nothing: skipped");
    }
    {   // a chain that cannot tell its holder before spawn: never decided for itself
        PlanRecord u; u.inheriting = true; u.holderResolved = false; u.hasSpellData = true;
        Check(Skipped(PlanWrites({ u })[0], kSkipTemplateUnresolvable), "unresolvable chain: 'holder not resolvable'");
    }
    {   // no spell list data of its own: made only when the engine has copied (an empty) holder into it
        auto copied = Child(0); copied.hasSpellData = false;
        auto notCopied = Child(0); notCopied.hasSpellData = false; notCopied.copiedTemplate = false;
        const auto s = PlanWrites({ Holder(), copied, notCopied });
        Check(Is(s[1], PlanAction::kCreateInherited, 0) && IsWrite(s[1].action), "empty holder copied (kCopiedTemplate, no spell list data): data made, the holder's lists added");
        Check(Skipped(s[2], kSkipNoOwnSpellList), "no spell list data and no kCopiedTemplate: skipped");
        auto withData = Child(0); withData.copiedTemplate = false;
        Check(Is(PlanWrites({ Holder(), withData })[1], PlanAction::kWriteInherited, 0), "spell list data of its own without kCopiedTemplate: written");
    }
    {   // the child's own race rules
        auto beast = Child(0); beast.ownSkip = kSkipInheritedBeast;
        auto creature = Child(0); creature.ownSkip = kSkipInheritedCreature;
        const auto s = PlanWrites({ Holder(), beast, creature });
        Check(Skipped(s[1], kSkipInheritedBeast) && Skipped(s[2], kSkipInheritedCreature), "a beast-race child and a non-mage creature child get nothing");
        const Rules rules = DefaultRules();
        NpcFacts f; f.hasClass = true; f.weights = MageWeights(); f.isBeast = true;
        Check(SameReason(InheritedRaceSkip(f, rules), kSkipInheritedBeast), "InheritedRaceSkip: a beast race, whatever the class");
        f.isBeast = false; f.isCreature = true;
        Check(InheritedRaceSkip(f, rules) == nullptr, "InheritedRaceSkip: a creature with a mage class keeps its holder's lists");
        f.weights = WarriorWeights();
        Check(SameReason(InheritedRaceSkip(f, rules), kSkipInheritedCreature), "InheritedRaceSkip: a creature without a mage class gets nothing");
        f.isCreature = false;
        Check(InheritedRaceSkip(f, rules) == nullptr, "InheritedRaceSkip: a person of a plain race keeps them");
        NpcFacts own = f; own.isBeast = true;
        Check(std::string_view(DecideNpc(own, rules, ListIndex{}).skipReason) == "beast race", "DecideNpc keeps its own reason for the same rule");
    }
    {   // a holder that gets nothing gives nothing
        const auto s = PlanWrites({ Holder(false), Child(0), Child(kNoRecord) });
        Check(s[0].action == PlanAction::kNone && s[0].skipReason == nullptr, "an own decision without lists: nothing written, no plan reason (DecideNpc's counts)");
        Check(Skipped(s[1], kSkipHolderGotNone), "holder without lists -> child nothing");
        Check(Skipped(s[2], kSkipHolderGotNone), "holder skipped before its decision -> child nothing");
    }
    return NpcSpellsCheck::g_failed;
}
