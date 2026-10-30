#pragma once

// =============================================================================
// SPELLS - the tree's spells for the magic menu, and HoM's state of them.
// The tree is spell_tree.json read
// at kDataLoaded, the state comes from HoM's API v2.
//
// Threads (game run 2026-10-25): the menu's frame handler runs on several
// threads, HoM's API is main-thread only. So the frame handler never asks HoM:
// it asks for a refresh (RequestRefresh), an SKSE task on the main thread reads
// HoM for the schools asked for and swaps in a new immutable Snapshot, and the
// frame handler draws from Current(). The tree data itself is read-only after
// Load().
// =============================================================================

#include "TreeData.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Spells
{
    struct Data
    {
        TreeData::Tree tree;
        std::vector<RE::FormID> gameIds;                             // per node: the runtime FormID, 0 = unresolved
        std::unordered_map<RE::FormID, std::vector<int>> byGameId;  // runtime FormID -> node indexes
    };

    /// Reads spell_tree.json and resolves its FormIDs (kDataLoaded). False when there is no tree.
    bool Load();

    /// The loaded tree, nullptr when there is none.
    const Data* Get();

    bool IsDup(const TreeData::Node& a_node);
    int FindNode(RE::FormID a_id, const std::string& a_school);  // the school's non-dup node first; -1 if none
    int PrimaryNode(RE::FormID a_id);                           // the non-dup node whatever the school; -1 if none
    std::pair<int, RE::FormID> FindSaved(std::uint32_t a_saved, const std::string& a_school);  // (node, runtime id)
    const TreeData::School* SchoolNamed(const std::string& a_name);
    std::string NodeName(int a_index);

    // ---- HoM state ----

    struct SpellState
    {
        bool mastered{};    // HoM's IsSpellMastered
        float percent{};    // HoM's GetProgress, 0..100
        float required{};   // HoM's GetRequiredXP
        bool known{};       // the player has the spell
    };

    struct SchoolState
    {
        RE::FormID target{};
        float percent{};
        float required{};
    };

    struct Snapshot
    {
        std::uint64_t epoch{};  // grows whenever the content changes
        std::unordered_map<std::string, SchoolState> schools;
        std::unordered_map<RE::FormID, SpellState> spells;
        std::size_t contentHash{};  // order-free hash of schools and spells (a refresh with the same content keeps the epoch)

        [[nodiscard]] const SpellState* Spell(RE::FormID a_id) const;
        [[nodiscard]] RE::FormID Target(const std::string& a_school) const;
        [[nodiscard]] bool Covers(const std::string& a_school) const { return schools.contains(a_school); }
    };

    /// The newest snapshot (never null).
    std::shared_ptr<const Snapshot> Current();

    /// Asks for HoM's state of these schools' spells (and their prerequisites), read on the main thread by an
    /// SKSE task; requests made before it runs are merged into one task.
    void RequestRefresh(const std::vector<std::string>& a_schools);

    // ---- looks, from a snapshot only ----

    enum class State
    {
        kUnknown,  // unresolved, or no state yet for its school
        kLocked,
        kAvailable,
        kReady,  // XP full, not learned yet
        kTarget,
        kLearning,  // known early while HoM still counts its XP (not HoM-mastered): drawn like the target
        kMastered,
    };

    struct Look
    {
        State state = State::kUnknown;
        float percent{};  // 0..100
    };

    Look LookOf(const Snapshot& a_snapshot, int a_index);

    enum class Prereq
    {
        kMastered,
        kNotMastered,
        kUnknown,   // its state is not in the snapshot yet (never reported as not mastered)
        kNotInGame  // the prerequisite does not resolve to a spell
    };

    /// A prerequisite (saved FormID) by HoM's IsSpellMastered, as the snapshot has it
    Prereq PrereqOf(const Snapshot& a_snapshot, std::uint32_t a_saved, const std::string& a_school);

    bool PrereqMastered(const Snapshot& a_snapshot, std::uint32_t a_saved, const std::string& a_school);

    /// The school's non-dup nodes in the "available" state, ordered by tier, name, FormID (R key)
    std::vector<int> Learnable(const Snapshot& a_snapshot, const std::string& a_school);

    const char* StateText(State a_state);
}
