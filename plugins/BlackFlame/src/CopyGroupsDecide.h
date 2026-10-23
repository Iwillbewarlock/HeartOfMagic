#pragma once

// =============================================================================
// COPY GROUPS DECIDE - the publish plan's decisions, without game types (CopyGroupsPlan.cpp; tested offline)
// =============================================================================
// (publication fix, 2026-10-19) C++17 and the standard library only, so tools/blackflame-cosave-check runs them:
//   DecideObject  one member object's state from what PlanObject found (the ledger's older generation, the progress line,
//                 the files, the window) - PlanObject's order of checks, unchanged
//   AtW1Of        what a member shows at W1: the generation its form already has (ApplyLastPublished) - shown -, nothing yet
//                 while its own object is still coming or not verified - fresh: gated until its group's turn (ShowOf),
//                 links to it go to the source, its object recorded once verified -, or left
//   OpensAtW1     a group opens at W1 when one member is shown; fresh members never keep it closed, and a member showing only
//                 its source never opens it. A group with no shown member (a first start) waits, is verified and opens in the
//                 drains as before
//   WorkOf        a member of an open group: newer objects to verify and record for the next start, or still to come
//   ShowsNow      (same-launch upgrades, 2026-10-23) an open group whose newer objects are all verified and recorded shows
//   ShowOf        them while the publish window is open (the main menu before the first load), whole: each member takes its
//                 recorded objects, a fresh member opens when its main object was recorded; the window closed first: they
//                 stay recorded for the next start as before

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace BlackFlame::CopyGroups::Decide
{
    enum class Member : std::uint8_t
    {
        kReady,      // shows a verified generation, or its source for this session (failed, terminal, keyless)
        kCandidate,  // this session's object is in Data: verify, then open (or, its group open, record for the next start)
        kPending,    // its object is still coming
        kLeft,       // never published this session: gated, links to it go to the source
    };

    enum class Found : std::uint8_t
    {
        kNoKey,        // no key this session (no builder)
        kTerminalKey,  // the ledger holds the key terminal "parse"
        kNoLine,       // the builder wrote no line for the key yet
        kTerminal,     // its line: too large or outside the dump caps
        kFailed,       // its line failed this session
        kStaging,      // its files are not all in Data yet
        kInData,       // its file and copy deps are in Data
    };

    struct ObjectFacts
    {
        bool older = false;           // the slot shows an older generation of this role
        Found found = Found::kNoKey;
        bool caughtUp = false;        // BuildProgress::CaughtUp: no more line or file comes this session
        // read only for kInData, in this order:
        bool replacedNow = false;     // a file of it replaced another at its path this session
        bool published = false;       // its record path is the one the slot shows
        bool canVerify = false;       // the drains can verify (MenuWatch::DrainsMayProve)
        bool enteredNoIndex = false;  // it entered Data this session and there is no file index
    };

    struct ObjectDecision
    {
        Member state = Member::kLeft;
        bool waiting = false;  // kReady on the older generation while the current key may still come
        const char* why = "";
    };

    [[nodiscard]] inline ObjectDecision DecideObject(const ObjectFacts& a_facts) noexcept
    {
        const auto fallback = [&](const char* a_why) {  // the older generation, or the source for this session
            return ObjectDecision{ a_facts.older ? Member::kReady : Member::kLeft, false, a_why };
        };
        // older: shown meanwhile, the ADDN queue keeps the slot (review Codex 1)
        const ObjectDecision stillComing = a_facts.older ? ObjectDecision{ Member::kReady, true, "" } : ObjectDecision{ Member::kPending, false, "" };
        switch (a_facts.found) {
        case Found::kNoKey:
            return fallback("no key this session (no builder)");
        case Found::kTerminalKey:
            return fallback("terminal: it failed to parse in two sessions");
        case Found::kNoLine:
            // the builder ended (or never started) and every line it wrote is read: none will come this session
            return a_facts.caughtUp ? fallback("the builder made no line for it this session") : stillComing;
        case Found::kTerminal:
            return fallback("terminal: too large or outside the dump caps");
        case Found::kFailed:
            return fallback("failed this session (tried again next start)");
        case Found::kStaging:
            // the worker's pass after the builder ended copied all it could: a file still missing will not come this session
            return a_facts.caughtUp ? fallback("its files did not all reach Data this session") : stillComing;
        case Found::kInData:
            break;
        }
        if (a_facts.replacedNow) {
            return fallback("a file of it replaced another at the same path this session: used from the next start");
        }
        if (a_facts.older && a_facts.published) {
            return { Member::kReady, false, "" };  // the ledger's generation is this key's
        }
        if (!a_facts.canVerify) {
            return fallback("no main-thread proof: verified at the next start");
        }
        if (a_facts.enteredNoIndex) {
            return fallback("in Data only since this session and no file index: the next start's index has it");
        }
        return { Member::kCandidate, false, "" };
    }

    enum class AtW1 : std::uint8_t
    {
        kShown,   // its form shows a verified generation: its gate opens with the group, and it opens the group at W1
        kFresh,   // nothing to show yet: gated until its group's turn (ShowOf; else this session), its object recorded once verified
        kSource,  // review round 1 (Codex P2 / B2): a slot without a source model whose muzzle is not grey (left, no older)
                  // shows only its source: its gate opens with the group, but it never opens one at W1
        kLeft,
    };

    /// a_member: the member's worst state; a_main / a_mainOlder: its main object's (the model, a TXST's set; a slot without a
    /// source model: its muzzle) state and whether an older generation of it is shown. A main object left with the member not
    /// left is only a slot without a source model (a model or a TXST set left leaves the member)
    [[nodiscard]] inline AtW1 AtW1Of(Member a_member, Member a_main, bool a_mainOlder) noexcept
    {
        if (a_member == Member::kLeft) {
            return AtW1::kLeft;
        }
        if ((a_main == Member::kPending || a_main == Member::kCandidate) && !a_mainOlder) {
            return AtW1::kFresh;
        }
        if (a_main == Member::kLeft && !a_mainOlder) {
            return AtW1::kSource;
        }
        return AtW1::kShown;
    }

    [[nodiscard]] inline bool OpensAtW1(const std::vector<AtW1>& a_members)
    {
        for (const auto member : a_members) {
            if (member == AtW1::kShown) {
                return true;
            }
        }
        return false;
    }

    struct PlanFacts
    {
        Member state = Member::kLeft;
        bool candidate = false;  // a candidate not verified (recorded or refused) yet this session
        bool waiting = false;
    };

    struct Work
    {
        bool verify = false;  // verify its candidates now
        bool wait = false;    // a newer object may still come (the group stays upgrading)
    };

    /// A member of an open group (its gates and links as W1 left them): what is left to verify and record for the next start
    [[nodiscard]] inline Work WorkOf(Member a_member, const PlanFacts& a_model, const PlanFacts& a_muzzle) noexcept
    {
        Work work;
        if (a_member == Member::kLeft) {
            return work;
        }
        if (a_member == Member::kPending) {
            work.wait = true;  // an object of it, or an ADDN slot it names, is still coming: verified together later
            return work;
        }
        for (const auto* plan : { &a_model, &a_muzzle }) {
            work.verify = work.verify || plan->candidate;
            work.wait = work.wait || plan->state == Member::kPending || plan->waiting;
        }
        return work;
    }

    // (same-launch upgrades, 2026-10-23) The group's turn: an open group (kOpen again after kUpgrading) whose verify list is
    // done with nothing more to wait for and at least one newer object recorded, while the publish window is open. Off
    // (showUpgradesNow false) or the window closed: the recorded objects are shown by the next start's ApplyLastPublished
    [[nodiscard]] inline bool ShowsNow(bool a_enabled, bool a_windowOpen, bool a_listDone, bool a_waits, std::size_t a_recorded) noexcept
    {
        return a_enabled && a_windowOpen && a_listDone && !a_waits && a_recorded > 0;
    }

    enum class Show : std::uint8_t
    {
        kNothing,  // left, nothing recorded for it, or a fresh member whose main object was not recorded (it stays gated)
        kSet,      // its gate is open already (shown or source at W1): its recorded objects set (model, muzzle, TXST set)
        kOpen,     // a fresh member: its recorded objects set, then its gate opened with the group's links
    };

    /// One member at its group's turn. a_atW1: what it showed at W1; a_mainRecorded: its main object (the model, a TXST's set;
    /// a slot without a source model: its muzzle) is recorded this session; a_anyRecorded: any of its objects is
    [[nodiscard]] inline Show ShowOf(AtW1 a_atW1, bool a_mainRecorded, bool a_anyRecorded) noexcept
    {
        switch (a_atW1) {
        case AtW1::kLeft:
            return Show::kNothing;
        case AtW1::kFresh:
            return a_mainRecorded ? Show::kOpen : Show::kNothing;  // a muzzle alone does not open a slot whose model is not grey
        case AtW1::kShown:
        case AtW1::kSource:
            break;
        }
        return a_anyRecorded ? Show::kSet : Show::kNothing;
    }
}
