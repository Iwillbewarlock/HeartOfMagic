#pragma once

// =============================================================================
// COPY GROUPS STATE - the groups' plan and publication state (internal to CopyGroups.cpp and CopyGroupsPlan.cpp)
// =============================================================================
// Main thread only (kDataLoaded, then the drains). CopyGroupsPlan.cpp makes the plan (which object each member would
// show, the groups, the ADDN slots still to verify); CopyGroups.cpp verifies and opens.

#include "Common.h"

#include "SlotKeys.h"
#include "SlotLedgerModel.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace BlackFlame::CopyGroups::Detail
{
    namespace LM = SlotLedgerModel;
    using Clock = std::chrono::steady_clock;
    inline constexpr auto kSlice = std::chrono::milliseconds(8);  // the probe's measured no-hitch slice (README copyModelProbe)
    inline constexpr auto kLedgerWriteGap = std::chrono::seconds(1);  // review: ledger writes at most this often while publishing
    inline constexpr std::uint64_t kNoGeneration = UINT64_MAX;    // "not planned yet" (BuildProgress' counter never reaches it)

    enum class Member : std::uint8_t
    {
        kReady,      // shows a verified generation, or its source for this session (failed, terminal, keyless)
        kCandidate,  // this session's object is in Data: verify, then open
        kPending,    // its object is still coming: the group waits
        kLeft,       // never published this session: gated, links to it go to the source
    };

    struct Plan
    {
        Member state = Member::kLeft;         // kLeft: no object (no file to show)
        std::optional<LM::Object> candidate;  // kCandidate: the object this session's key gave
        std::string why;
        bool waiting = false;                 // kReady on an older generation while the current key may still come
    };

    struct Slot
    {
        std::uint16_t id = 0;
        Member state = Member::kPending;
        Plan model;
        Plan muzzle;
    };

    enum class GroupState : std::uint8_t
    {
        kWaiting,
        kVerifying,
        kOpen,
        kFailed,
        kGone,  // every member left
    };

    struct Verify
    {
        std::string path;  // record form (Demand's)
        std::uint64_t key = 0;
        LM::Object object;
        bool registered = false;
    };

    struct Group
    {
        std::vector<std::uint16_t> ids;
        GroupState state = GroupState::kWaiting;
        std::vector<Slot> members;
        std::vector<Verify> verify;
        std::size_t verifyNext = 0;
        std::uint64_t plannedAt = kNoGeneration;  // the progress generation its plan was made at
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

    /// Plans the waiting groups from the cursor until the slice ends; true when every group is planned for a_state.generation
    [[nodiscard]] bool Replan(State& a_state, Clock::time_point a_start, bool a_unbounded = false);
}
