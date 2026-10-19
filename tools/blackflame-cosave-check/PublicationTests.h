#pragma once

// =============================================================================
// PUBLICATION TESTS - the publication fix (2026-10-19): W1 opens on the shown generation, upgrades are recorded
// =============================================================================
// plugins/BlackFlame/src/CopyGroupsDecide.h (the plan's decisions without game types: PlanObject's order of checks, what a
// member shows at W1, when a group opens at W1, an open group's work) and the ledger's previous generation
// (SlotLedgerModel.h SetPublished / RestorePrevious / PreviousOf, SlotLedgerJson.h "previous"). Included by main.cpp.

#include "../../plugins/BlackFlame/src/CopyGroupsDecide.h"
#include "../../plugins/BlackFlame/src/SlotLedgerModel.h"

#include <string>
#include <vector>

namespace PublicationTests
{
    namespace D = BlackFlame::CopyGroups::Decide;
    namespace LM = BlackFlame::SlotLedgerModel;
    namespace CC = BlackFlame::CopyContract;
    using M = D::Member;

    inline D::ObjectFacts Facts(bool a_older, D::Found a_found, bool a_caughtUp = false, bool a_published = false, bool a_canVerify = true)
    {
        D::ObjectFacts facts;
        facts.older = a_older;
        facts.found = a_found;
        facts.caughtUp = a_caughtUp;
        facts.published = a_published;
        facts.canVerify = a_canVerify;
        return facts;
    }

    // A one-object member (a model slot): its W1 state and its work in an open group
    inline D::AtW1 AtW1(const D::ObjectFacts& a_facts)
    {
        const auto decision = D::DecideObject(a_facts);
        return D::AtW1Of(decision.state, decision.state, a_facts.older);
    }

    inline D::Work WorkOf(const D::ObjectFacts& a_facts, bool a_verifiedAlready = false)
    {
        const auto decision = D::DecideObject(a_facts);
        // PlanOpenGroup: a candidate verified this session already (recorded or refused) counts as ready
        const bool candidate = decision.state == M::kCandidate && !a_verifiedAlready;
        const auto state = decision.state == M::kCandidate && a_verifiedAlready ? M::kReady : decision.state;
        return D::WorkOf(state, { state, candidate, decision.waiting }, {});
    }

    template <class Check>
    void RunDecide(Check&& a_check)
    {
        // old + new in Data (a key change, its file copied in): the old generation opens at W1, the new one is verified
        const auto inData = Facts(true, D::Found::kInData);
        const auto inDataDecision = D::DecideObject(inData);
        const auto inDataWork = WorkOf(inData);
        a_check(inDataDecision.state == M::kCandidate && AtW1(inData) == D::AtW1::kShown && D::OpensAtW1({ AtW1(inData) }) &&
                    inDataWork.verify && !inDataWork.wait,
            "publication: an older generation shown and the new key's object in Data: shown at W1 (its group opens), the new one "
            "verified in the drains");
        const auto recorded = WorkOf(inData, true);
        a_check(!recorded.verify && !recorded.wait, "publication: once the new object is recorded for the next start nothing is left to do");
        // old + new pending (no line yet, the builder runs)
        const auto pending = Facts(true, D::Found::kNoLine);
        const auto pendingDecision = D::DecideObject(pending);
        const auto pendingWork = WorkOf(pending);
        a_check(pendingDecision.state == M::kReady && pendingDecision.waiting && AtW1(pending) == D::AtW1::kShown && !pendingWork.verify &&
                    pendingWork.wait,
            "publication: an older generation shown and the new key still coming: ready on the older one at W1, the group "
            "upgrading (waits for it)");
        const auto staging = Facts(true, D::Found::kStaging);
        a_check(D::DecideObject(staging).waiting && WorkOf(staging).wait && !WorkOf(Facts(true, D::Found::kStaging, true)).wait,
            "publication: the new object's files still coming keep the upgrade waiting until the builder's lines are all read");
        const auto caughtUp = Facts(true, D::Found::kNoLine, true);
        a_check(D::DecideObject(caughtUp).state == M::kReady && !D::DecideObject(caughtUp).waiting && !WorkOf(caughtUp).wait,
            "publication: no line once the builder is caught up: the older generation stays, nothing more to wait for");
        // old + failed candidate: the line failed, or the Demand of the new NIF failed (refused: verified already)
        const auto failed = Facts(true, D::Found::kFailed);
        a_check(D::DecideObject(failed).state == M::kReady && AtW1(failed) == D::AtW1::kShown && !WorkOf(failed).verify &&
                    !WorkOf(failed).wait,
            "publication: an older generation shown and the new object failed this session: ready on the older one, no work");
        const auto refused = WorkOf(inData, true);
        a_check(!refused.verify && !refused.wait,
            "publication: a new object whose Demand failed (refused) is not verified again; the older generation stays open");
        // old neighbour + unpublished TXST (the TXST rule fix: 26 TXST slots, none published)
        const auto neighbour = Facts(true, D::Found::kInData, false, true);
        const auto txst = Facts(false, D::Found::kNoLine);
        const auto txstDecision = D::DecideObject(txst);
        a_check(D::DecideObject(neighbour).state == M::kReady && AtW1(neighbour) == D::AtW1::kShown && txstDecision.state == M::kPending &&
                    AtW1(txst) == D::AtW1::kFresh && D::OpensAtW1({ AtW1(neighbour), AtW1(txst) }),
            "publication: a neighbour showing its generation and a TXST with none published, still building: the group opens at "
            "W1, the TXST left for this session (it does not keep the group closed)");
        const auto txstCandidate = Facts(false, D::Found::kInData);
        a_check(AtW1(txstCandidate) == D::AtW1::kFresh && D::OpensAtW1({ AtW1(neighbour), AtW1(txstCandidate) }),
            "publication: a TXST with nothing published whose set is in Data is fresh too (verified and recorded, not shown)");
        a_check(!D::OpensAtW1({ AtW1(txst), AtW1(txstCandidate) }) && !D::OpensAtW1({ D::AtW1::kLeft, AtW1(txst) }) && !D::OpensAtW1({}),
            "publication: a group with no member shown (a first start) does not open at W1: it waits and opens in the drains");
        // progress arriving after the group opened: the fresh TXST's line, then its record
        const auto beforeLine = WorkOf(txst);
        const auto afterLine = WorkOf(txstCandidate);
        const auto afterRecord = WorkOf(txstCandidate, true);
        a_check(!beforeLine.verify && beforeLine.wait && afterLine.verify && !afterLine.wait && !afterRecord.verify && !afterRecord.wait,
            "publication: progress after the group opened: the fresh member waits, then is verified, then the group settles");
        // the rest of PlanObject's order
        a_check(D::DecideObject(Facts(true, D::Found::kInData, false, false, false)).state == M::kReady &&
                    D::DecideObject(Facts(false, D::Found::kInData, false, false, false)).state == M::kLeft,
            "publication: no main-thread proof: the older generation (ready) or the source (left), never a candidate");
        auto replaced = Facts(true, D::Found::kInData, false, true);
        replaced.replacedNow = true;
        auto noIndex = Facts(false, D::Found::kInData);
        noIndex.enteredNoIndex = true;
        a_check(D::DecideObject(replaced).state == M::kReady && D::DecideObject(noIndex).state == M::kLeft &&
                    D::DecideObject(Facts(false, D::Found::kNoKey)).state == M::kLeft &&
                    D::DecideObject(Facts(true, D::Found::kTerminalKey)).state == M::kReady &&
                    D::DecideObject(Facts(false, D::Found::kStaging, true)).state == M::kLeft,
            "publication: a file replaced this session, no index for a new file, no key, a terminal key, files that did not reach "
            "Data: the older generation or the source");
        a_check(D::AtW1Of(M::kReady, M::kLeft, false) == D::AtW1::kShown && D::AtW1Of(M::kLeft, M::kReady, true) == D::AtW1::kLeft &&
                    D::AtW1Of(M::kPending, M::kCandidate, true) == D::AtW1::kShown,
            "publication: a muzzle-only slot is shown as before; a left member stays left; a member waiting for an ADDN slot "
            "with an older model shown is shown");
        a_check(D::WorkOf(M::kPending, { M::kCandidate, true, false }, {}).wait && !D::WorkOf(M::kPending, { M::kCandidate, true, false }, {}).verify &&
                    !D::WorkOf(M::kLeft, { M::kCandidate, true, false }, {}).verify,
            "publication: a member still pending (an ADDN slot it names) is not verified yet; a left member has no work");
    }

    inline LM::Object Model(std::uint64_t a_key)
    {
        return { LM::Role::kModel, a_key, "format=BFK4|kind=model", "meshes\\heartofmagic\\blackflame\\copies\\" + CC::Hex16(a_key) + "\\copy.nif",
            10, a_key + 1, {} };
    }

    template <class Check>
    void RunPrevious(Check&& a_check)
    {
        LM::Entry entry;
        entry.slot = 0x800;
        entry.type = CC::SlotType::kPROJ;
        entry.plugin = "Skyrim.esm";
        entry.localId = 0x10FBED;
        LM::SetPublished(entry, Model(0x11));
        a_check(LM::PublishedOf(entry, LM::Role::kModel) && !LM::PreviousOf(entry, LM::Role::kModel),
            "publication ledger: a first object has no previous");
        LM::SetPublished(entry, Model(0x22));
        const auto* previous = LM::PreviousOf(entry, LM::Role::kModel);
        a_check(LM::PublishedOf(entry, LM::Role::kModel)->key == 0x22 && previous && previous->key == 0x11,
            "publication ledger: an upgrade recorded keeps the generation shown as the previous one");
        auto rebuilt = Model(0x22);
        rebuilt.fnv64 = 0x99;  // the same key and path (a GPU rebuild kept): no new previous
        LM::SetPublished(entry, rebuilt);
        a_check(LM::PreviousOf(entry, LM::Role::kModel) && LM::PreviousOf(entry, LM::Role::kModel)->key == 0x11 &&
                    LM::PublishedOf(entry, LM::Role::kModel)->fnv64 == 0x99,
            "publication ledger: the same generation recorded again keeps the previous one");
        LM::SetPublished(entry, { LM::Role::kMuzzle, 0x33, "", Model(0x33).path, 5, 6, {} });
        a_check(!LM::PreviousOf(entry, LM::Role::kMuzzle) && LM::PreviousOf(entry, LM::Role::kModel)->key == 0x11,
            "publication ledger: previous objects are per role");
        const bool swapped = LM::RestorePrevious(entry, LM::Role::kModel);
        a_check(swapped && LM::PublishedOf(entry, LM::Role::kModel)->key == 0x11 && LM::PreviousOf(entry, LM::Role::kModel)->key == 0x22 &&
                    !LM::RestorePrevious(entry, LM::Role::kMuzzle),
            "publication ledger: RestorePrevious swaps a role's published and previous objects (none: false, nothing changes)");

        LM::Ledger ledger;
        ledger.epoch = 0x1234;
        ledger.version = 3;
        ledger.sessions = 2;
        LM::Insert(ledger, entry);
        std::string why;
        std::optional<LM::Ledger> back;
        try {
            back = LM::FromJson(LM::Json::parse(LM::ToJson(ledger).dump()), why);
        } catch (const LM::Json::exception& e) {
            why = e.what();
        }
        const auto* read = back ? LM::FindSlot(*back, 0x800) : nullptr;
        const auto* readPrevious = read ? LM::PreviousOf(*read, LM::Role::kModel) : nullptr;
        a_check(read && readPrevious && readPrevious->key == 0x22 && readPrevious->path == Model(0x22).path && read->previous.size() == 1 &&
                    read->published.size() == 2,
            "publication ledger: previous objects survive the JSON round trip " + why);
        auto older = LM::ToJson(ledger);
        older["slots"][0].erase("previous");
        const auto oldRead = LM::FromJson(older, why);
        a_check(oldRead && LM::FindSlot(*oldRead, 0x800) && LM::FindSlot(*oldRead, 0x800)->previous.empty(),
            "publication ledger: a ledger without previous (an older build's) reads");
        auto twice = LM::ToJson(ledger);
        twice["slots"][0]["previous"].push_back(twice["slots"][0]["previous"][0]);
        auto notArray = LM::ToJson(ledger);
        notArray["slots"][0]["previous"] = 5;
        a_check(!LM::FromJson(twice, why) && !LM::FromJson(notArray, why),
            "publication ledger: two previous objects of one role, or a previous that is not an array, refuse the file");
        // the previous object is the rebuilt 0x22 (fnv64 0x99) after the swap
        a_check(LM::PublishedHolds(ledger, Model(0x22).path, 0x99) && !LM::PublishedHolds(ledger, Model(0x22).path, Model(0x22).fnv64) &&
                    LM::PublishedFiles(ledger).count(Model(0x22).path) == 1,
            "publication ledger: a previous object's files count as published (kept, never replaced by a deferred copy)");

        LM::Ledger other;
        other.epoch = 0x5678;  // another epoch, another source: a conflict
        auto stranger = entry;
        stranger.plugin = "Other.esp";
        LM::Insert(other, stranger);
        std::vector<std::string> conflicts;
        auto merged = ledger;
        (void)LM::Merge(merged, other, conflicts, "a test");
        LM::ApplySave(ledger, { { 0x800, { 0x800, CC::SlotType::kPROJ, "Other.esp", 0x10FBED }, false, "" } }, 3);
        a_check(LM::FindSlot(merged, 0x800)->previous.empty() && LM::FindSlot(merged, 0x800)->published.empty() &&
                    LM::FindSlot(ledger, 0x800)->previous.empty(),
            "publication ledger: a conflict and a save's rebind drop the previous objects with the published ones");
    }

    template <class Check>
    void Run(Check&& a_check)
    {
        RunDecide(a_check);
        RunPrevious(a_check);
    }
}
