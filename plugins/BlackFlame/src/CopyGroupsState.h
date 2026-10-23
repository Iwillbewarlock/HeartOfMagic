#pragma once

// =============================================================================
// COPY GROUPS STATE - the groups' plan and publication state (internal to CopyGroups.cpp and CopyGroupsPlan.cpp)
// =============================================================================
// Main thread only (kDataLoaded, then the drains). CopyGroupsPlan.cpp makes the plan (which object each member would
// show, the groups, the ADDN slots still to verify); CopyGroups.cpp verifies and opens.

#include "Common.h"

#include "CopyGroups.h"
#include "CopyGroupsDecide.h"
#include "SlotKeys.h"
#include "SlotLedgerModel.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace BlackFlame::CopyGroups::Detail
{
    namespace LM = SlotLedgerModel;
    using Clock = std::chrono::steady_clock;
    inline constexpr auto kSlice = std::chrono::milliseconds(8);  // the probe's measured no-hitch slice (README copyModelProbe)
    inline constexpr auto kLedgerWriteGap = std::chrono::seconds(1);  // review: ledger writes at most this often while publishing
    inline constexpr std::uint64_t kNoGeneration = UINT64_MAX;    // "not planned yet" (BuildProgress' counter never reaches it)

    using Decide::Member;  // CopyGroupsDecide.h (no game types: tested offline)

    struct Plan
    {
        Member state = Member::kLeft;         // kLeft: no object (no file to show)
        std::optional<LM::Object> candidate;  // kCandidate: the object this session's key gave
        std::string why;
        bool waiting = false;                 // kReady on an older generation while the current key may still come
        bool older = false;                   // the slot shows an older generation of this role (publication fix, 2026-10-19)
    };

    struct Slot
    {
        std::uint16_t id = 0;
        Member state = Member::kPending;
        Plan model;
        Plan muzzle;
        // Decide::AtW1Of: kFresh (nothing to show yet) is, in a group opened at W1, gated for this session; kSource opens with
        // the group but does not open it (review round 1 B2)
        Decide::AtW1 atW1 = Decide::AtW1::kShown;
        [[nodiscard]] bool Fresh() const noexcept { return atW1 == Decide::AtW1::kFresh; }
    };

    enum class GroupState : std::uint8_t
    {
        kWaiting,
        kVerifying,
        kOpen,
        kUpgrading,  // open (W1, on the generations its forms show); newer objects verified and recorded (then shown: showDue)
        kFailed,
        kGone,  // every member left
    };

    struct Verify
    {
        std::string path;  // record form (Demand's); "" for a TXST object (its textures are deps)
        std::uint64_t key = 0;
        LM::Object object;
        bool registered = false;
        bool demand = true;  // false: a TXST object, registered only (a DDS is not Demanded through the model database)
        std::uint16_t slot = 0;  // kUpgrading: the member the object is recorded for
    };

    struct Upgrade  // kUpgrading: one object of a member verified this session (not verified again)
    {
        bool recorded = false;  // the ledger holds it for the next start; else refused (why)
        std::string why;
        // (same-launch upgrades, 2026-10-23) the object recorded (a TXST set with its strings), shown at the group's turn
        std::optional<LM::Object> object;
        bool shown = false;
    };

    struct Group
    {
        std::vector<std::uint16_t> ids;
        GroupState state = GroupState::kWaiting;
        std::vector<Slot> members;
        std::vector<Verify> verify;
        std::size_t verifyNext = 0;
        std::uint64_t plannedAt = kNoGeneration;    // the progress generation its plan was made at (or looked at)
        std::uint64_t plannedFrom = kNoGeneration;  // kUpgrading: the generation PlanOpenGroup last planned it from
        // (publication fix, 2026-10-19) opened at W1 on what its forms show: its fresh members (gates closed this session),
        // the objects verified for the next start by (slot, key), and whether a newer object may still come
        std::vector<std::uint16_t> leftForNow;
        std::map<std::pair<std::uint16_t, std::uint64_t>, Upgrade> upgrades;
        bool upgradeWaits = false;
        bool showDue = false;  // (same-launch upgrades) its upgrades are done and recorded: shown at the next drain (ShowUpgrades)
    };

    struct AddnPass  // one pass over State::addns, carried over the slices (StepAddn)
    {
        std::size_t next = 0;
        std::size_t pending = 0;
        std::size_t waiting = 0;
        bool changed = false;
    };

    struct State
    {
        std::vector<Group> groups;
        std::vector<std::uint16_t> addns;      // usable ADDN slots whose newest MPS may still be published
        bool addnDirty = true;
        AddnPass addnPass;
        std::uint64_t generation = kNoGeneration;
        std::size_t replanNext = 0;             // Replan's place (it goes on in the next slice)
        LM::Ledger ledger;                      // a copy, taken when the generation moved
        bool canVerify = false;
        bool stopped = false;
        bool proofWaitLogged = false;           // "waits for the main-thread proof", once
        bool ledgerDirty = false;               // something to save (written at most every kLedgerWriteGap, and at the end)
        std::optional<Clock::time_point> lastLedgerWrite;
    };

    [[nodiscard]] State& TheState();

    enum class AddnDep : std::uint8_t
    {
        kReady,  // every ADDN slot it names is open, or will not open this session (it shows that slot's model as it is)
        kWait,   // one may still open
        kLeft,   // one is collided: the index names another record
    };

    [[nodiscard]] AddnDep AddnDeps(const State& a_state, const LM::Object& a_object);

    /// a_object's file or one of its copy deps entered Data this session (BuildProgress::CopiedNow): the startup index does
    /// not know it, so it is verified only through the file index (review N1)
    [[nodiscard]] bool EnteredDataNow(const LM::Object& a_object);

    [[nodiscard]] Plan PlanObject(const State& a_state, LM::Role a_role, const std::optional<SlotKeys::Key>& a_key,
        const std::string& a_published, const LM::Entry* a_entry);

    void BuildGroups(State& a_state);

    /// Plans the waiting groups (and the upgrading ones not verifying now) from the cursor until the slice ends; true when
    /// every group is planned for a_state.generation
    [[nodiscard]] bool Replan(State& a_state, Clock::time_point a_start, bool a_unbounded = false);

    /// W1: one member is shown (Decide::OpensAtW1)
    [[nodiscard]] bool OpensAtW1(const Group& a_group);

    /// A member opened at W1 left for this session (fresh then)
    [[nodiscard]] bool LeftForNow(const Group& a_group, std::uint16_t a_id);

    /// (publication fix, 2026-10-19) An open group's verify list: every member's candidates not verified yet this session
    /// (Decide::WorkOf); kUpgrading while one is listed or may still come, else kOpen. a_replan: its members planned again first
    void PlanOpenGroup(const State& a_state, Group& a_group, bool a_replan);

    /// (publication fix, 2026-10-19) kUpgrading: a verified object goes into the ledger only (the shown one becomes its
    /// previous); the object recorded (a TXST set with its strings), else nullopt and a_why (CopyGroupsUpgrade.cpp)
    [[nodiscard]] std::optional<LM::Object> RecordUpgrade(State& a_state, const Verify& a_item, std::string& a_why);

    /// The verified object's outcome, kept by (slot, key) so it is not verified again this session; a recorded one is kept
    /// for the group's turn (ShowUpgrades)
    void NoteUpgrade(Group& a_group, const Verify& a_item, std::optional<LM::Object> a_recorded, const std::string& a_why);

    /// (same-launch upgrades, 2026-10-23) A group whose upgrades are done (showDue), drain only, the window checked by the
    /// caller right before: every member's recorded objects set, its fresh members opened, then every open member's links
    /// (CopyGroupsDecide.h ShowOf). Returns the objects shown
    std::size_t ShowUpgrades(Group& a_group);

    /// The counts the HUD-less progress lines and the dev auto-load read (CopyGroups::Progress)
    [[nodiscard]] Progress CountProgress(const State& a_state);

    /// Trace (Fire Rune glyph): kDataLoaded, after the W1 openings, one line per TXST slot - its plan, group, write and gate
    /// (CopyGroupsNotes.cpp)
    void LogTxstSlotsAtW1(const State& a_state);
}
