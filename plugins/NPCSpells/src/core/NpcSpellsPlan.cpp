#include "core/NpcSpellsPlan.h"
#include "core/NpcSpellsNpc.h"
namespace NpcSpells {
namespace {
    // Why an inheriting record gets nothing from `target`, the record its lists would come through (its holder, or the record
    // whose spell list data it shares); target kNoRecord: the holder was skipped before its decision
    const char* NotWrittenReason(const std::vector<PlanStep>& steps, std::size_t target, bool isHolder)
    {
        if (target < steps.size() && steps[target].blockedWrite) return kSkipHolderBlocked;
        return isHolder ? kSkipHolderGotNone : kSkipSharedSpellData;
    }
    void Skip(PlanStep& s, const char* reason, bool blockedWrite = false) { s.action = PlanAction::kNone; s.skipReason = reason; s.blockedWrite = blockedWrite; }
}
std::vector<PlanStep> PlanWrites(const std::vector<PlanRecord>& records)
{
    const auto n = records.size();
    std::vector<PlanStep> steps(n);
    // The records that decide for themselves first: an inheriting record's step reads its holder's
    for (std::size_t i = 0; i < n; ++i) {
        const auto& r = records[i];
        if (r.inheriting || !r.hasLists) continue;
        if (r.blocked) { Skip(steps[i], kSkipSharedLevArray, true); continue; }
        steps[i].action = PlanAction::kWriteOwn;
        steps[i].listsFrom = i;
    }
    std::vector<std::size_t> shared;   // inheriting records whose spell list data is a chain record's: settled last
    for (std::size_t i = 0; i < n; ++i) {
        const auto& r = records[i];
        if (!r.inheriting) continue;
        if (!r.holderResolved) { Skip(steps[i], kSkipTemplateUnresolvable); continue; }
        if (r.sharesSpellData < n && r.sharesSpellData != i) { shared.push_back(i); continue; }
        if (r.ownSkip) { Skip(steps[i], r.ownSkip); continue; }
        const auto h = r.holder;
        if (h >= n || steps[h].action != PlanAction::kWriteOwn) { Skip(steps[i], NotWrittenReason(steps, h, true)); continue; }
        if (r.blocked) { Skip(steps[i], kSkipSharedLevArray, true); continue; }
        if (!r.hasSpellData && !r.copiedTemplate) { Skip(steps[i], kSkipNoOwnSpellList); continue; }
        steps[i].action = r.hasSpellData ? PlanAction::kWriteInherited : PlanAction::kCreateInherited;
        steps[i].listsFrom = h;
    }
    // A record sharing spell list data gets what the record it shares with gets (written or itself reached the same way).
    // Rounds settle a chain of such records; one that never settles (a cycle) gets nothing.
    std::vector<bool> settled(n, true);
    for (const auto i : shared) settled[i] = false;
    for (std::size_t round = 0; round < n && !shared.empty(); ++round) {
        std::vector<std::size_t> open;
        for (const auto i : shared) {
            const auto t = records[i].sharesSpellData;
            if (!settled[t]) { open.push_back(i); continue; }
            if (IsWrite(steps[t].action) || steps[t].action == PlanAction::kViaShared) {
                steps[i].action = PlanAction::kViaShared;
                steps[i].listsFrom = steps[t].listsFrom;
                steps[i].via = t;
            } else {
                Skip(steps[i], NotWrittenReason(steps, t, t == records[i].holder));
            }
            settled[i] = true;
        }
        if (open.size() == shared.size()) break;
        shared.swap(open);
    }
    for (const auto i : shared)
        if (!settled[i]) Skip(steps[i], kSkipSharedSpellData);
    return steps;
}
}
