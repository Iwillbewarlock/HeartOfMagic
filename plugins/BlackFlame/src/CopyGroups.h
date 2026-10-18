#pragma once

// =============================================================================
// COPY GROUPS - closure groups (connected copy links) and the group gate: verify, then open
// =============================================================================
// A published copy may link only to an open copy or to an original (LinkFor goes through CopyOf), so slots open by
// groups: the connected components of the copy links of the slots' sources (PROJ explosion and decal, EXPL placed object
// and spawn projectile, IPCT hazard and both decals; design v3.1 G). Per member object (its model, a PROJ's muzzle):
//   ready      the slot already shows a generation that was verified in an earlier session (ApplyLastPublished); also a
//              member whose new object failed, is terminal, has no key, or got no line or not all its files once the
//              builder's lines are all read and copied (BuildProgress::CaughtUp): it shows that older generation
//   candidate  this session's key has an ok or fallback line and its files are in Data (BuildProgress kInData)
//   pending    built later (or retried) while the builder may still write it: the group waits
//   left       the cases of ready without an older generation, or a TXST
//              slot: the member leaves its group, stays gated, links to it go to its source. A terminal muzzle only keeps
//              the source's muzzle
// A NIF naming an ADDN slot index (a progress dep {addn}) is used only when that ADDN slot's MPS is published (its gate);
// an ADDN slot's new MPS is verified and published first, its DNAM flags left at 1 (copied from the source only at W1).
// Verify, then open (v3.2 §1): every candidate path registered (FileIndex), Demanded one at a time in 8 ms slices on the
// main thread (CopyModelProbeVerify.h); only when all parse: SetModel (model, muzzle), the gates of the group, its links
// (CopyFill::Refill pointers only), and the ledger's lastPublished (temp + rename). A parse failure counts that key in the
// ledger and the group is not opened this session; there is no re-close. Windows (MenuWatch.h): at kDataLoaded the groups
// whose members are all ready open at once (W1); the drains go on while the publish window is open and the main-thread
// proof holds (W1 drains and W2); each publish step re-checks the close flag right before SetModel and RegisterLoose. A
// group unfinished when the window closes stays as it was. A Demand that faults stops publication for the session. TXST
// slots (config slotTxst) are members like the others: their txst object's DDS deps are registered (no Demand), and the
// open writes TX00 / TX03 (SlotTxst.h) before the gates open; off, they are left and the decals keep the source's sets.

#include "Common.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace BlackFlame::CopyGroups
{
    /// kDataLoaded (singleLaunch), after ApplyLastPublished: the groups; the ready ones open now; the drain stepper queued
    void Start();

    struct Progress
    {
        std::size_t slots = 0;        // members that can still open this session (not left)
        std::size_t open = 0;         // of them, open
        std::size_t groups = 0;
        std::size_t groupsOpen = 0;
        std::size_t groupsFailed = 0;  // a NIF did not parse: not opened this session
        std::size_t left = 0;          // members that left their group
        std::size_t addnOpen = 0;      // ADDN slots whose MPS is published
        std::size_t verifying = 0;     // groups being verified now
        bool settled = false;          // nothing more can open this session (all open, failed or the window closed)
    };

    [[nodiscard]] Progress GetProgress();

    struct PublishLog
    {
        std::size_t steps = 0;           // publish steps (SetModel / RegisterLoose batches) run in the main-thread drains
        std::size_t mainMenuSteps = 0;   // of them, with the main menu open (W2: SetModel at the main menu)
        bool allOnMainThread = true;     // every drain step on RE::Main's thread
        bool loadDuringPublish = false;  // a load was under way during one (Watchdog::LoadUnderWay)
        std::uint32_t firstThread = 0;   // the first drain step's thread
        std::size_t syncSteps = 0;       // W1 steps at kDataLoaded itself (its thread, not Main's on 1.6.1170 under MO2: not judged)
        std::uint32_t syncThread = 0;    // the first W1 step's thread
    };

    /// T0-W2's evidence
    [[nodiscard]] PublishLog GetPublishLog();

    /// A slot's place in the publish groups: its group's state (or "left", "in no group", "not usable") and the reason its
    /// member (or the slot) states, "" when none
    struct SlotState
    {
        bool inGroup = false;
        bool groupOpen = false;
        bool left = false;  // the member never published this session
        std::string state;
        std::string why;
    };

    /// Main thread (CopyGroupsNotes.cpp)
    [[nodiscard]] SlotState SlotStateOf(std::uint16_t a_slot);

    /// Main thread: the slot's gate is closed inside an OPEN group, its member not left, and no reason is stated - a fault;
    /// every other closed slot has a state that says why (still building, failed, left, not usable, not written: why)
    [[nodiscard]] bool ClosedUnexplained(std::uint16_t a_slot);

    /// Main thread: a slot's group state and its member's reason in words ("waiting (still building)", "failed", "left: why",
    /// "open"), "in no group" when no group holds it - for the self-test's slot lines
    [[nodiscard]] std::string SlotNote(std::uint16_t a_slot);

    /// Main thread: "N of M assigned slots open; closed: <state> <count>, ..." (the self-test's summary line)
    [[nodiscard]] std::string SlotSummary();
}
