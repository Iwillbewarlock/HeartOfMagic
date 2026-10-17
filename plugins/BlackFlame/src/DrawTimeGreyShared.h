#pragma once

#include "Common.h"
#include "DrawTimeGrey.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

// =============================================================================
// DRAW-TIME GREY - what the main-thread set and the render-side hook share (DrawTimeGrey.h)
// =============================================================================

namespace BlackFlame::DrawTime
{
    using Srv = REX::W32::ID3D11ShaderResourceView;

    /// What the palette of one player geometry is turned into
    enum class GreyKind : std::uint8_t
    {
        kDerived,    // the builder's derived palette for this source palette and blend (copies\palettes\{add,normal}\)
        kFixed,      // GradBlackFlame.dds (palette colour only, normal blend)
        kFixedAdd,   // GradBlackFlameAdd.dds (palette colour only, additive blend)
        kLeftAlone   // palette alpha on and no derived palette on disk: one universal palette would break its outline
    };

    /// One player geometry, decided on the main thread. The render side only compares these pointers with the pass's
    /// own and writes `grey` (a view of a session-long palette texture); it never dereferences them
    struct Target
    {
        const RE::BSShaderProperty* property = nullptr;  // the pass's shaderProperty must still be this one
        const RE::BSShaderMaterial* material = nullptr;  // for the logs
        Srv* source = nullptr;                           // the material's greyscale texture view (what the slot must hold)
        Srv* base = nullptr;                             // the material's source texture view (logged with the slot)
        Srv* grey = nullptr;                             // what is bound instead (null: kLeftAlone)
        GreyKind kind = GreyKind::kLeftAlone;
        RootKind root = RootKind::kProjectile;
    };

    /// Published by the main thread, read by the render hook. Immutable once published
    struct Snapshot
    {
        std::uint64_t generation = 0;
        std::unordered_map<const RE::BSGeometry*, Target> targets;
        std::unordered_set<Srv*> ours;  // every palette view we may bind (the NPC leak check; kept in an empty snapshot too)
        // The player's geometries skipped because their material already names one of our palettes (a copy): not in
        // targets, and not NPC leaks when the slot holds our view
        std::unordered_set<const RE::BSGeometry*> playerSkipped;
    };

    [[nodiscard]] std::shared_ptr<const Snapshot> LoadSnapshot() noexcept;
    void PublishSnapshot(std::shared_ptr<const Snapshot> a_snapshot) noexcept;

    /// Counters written by the hook (any thread) and read by the summary (main thread)
    struct Counters
    {
        std::atomic<std::uint64_t> passes{ 0 };          // SetupGeometry calls seen while stoked
        std::atomic<std::uint64_t> passesOffMain{ 0 };   // of those, not on the stamped main thread
        std::atomic<std::uint64_t> playerHits{ 0 };      // passes whose geometry is in the player set
        std::atomic<std::uint64_t> binds{ 0 };           // our palette written into the slot
        std::atomic<std::uint64_t> restores{ 0 };        // the saved view put back
        std::atomic<std::uint64_t> restoreChanged{ 0 };  // at restore the slot no longer held ours: left as it was
        std::atomic<std::uint64_t> unpaired{ 0 };        // a bind still open when the thread's next SetupGeometry came
        std::atomic<std::uint64_t> leftAlone{ 0 };       // player hits of kLeftAlone shapes
        std::atomic<std::uint64_t> unmeasured{ 0 };      // player hits before the slot was measured
        std::atomic<std::uint64_t> slotMismatch{ 0 };    // the measured slot did not hold the shape's own palette
        std::atomic<std::uint64_t> propMismatch{ 0 };    // the geometry's pass had another shaderProperty than walked
        std::atomic<std::uint64_t> npcLeaks{ 0 };        // a pass NOT the player's found one of our views in the slot
        std::atomic<std::uint64_t> playerOwnPalette{ 0 };  // a player pass skipped as already ours, with our view: no leak
        std::atomic<std::uint64_t> failures{ 0 };        // SEH-guarded bodies that faulted (or skipped after a fault)
        std::atomic<std::uint64_t> setupNs{ 0 };         // time inside our part of SetupGeometry
        std::atomic<std::uint64_t> restoreNs{ 0 };       // time inside our part of RestoreGeometry
        std::atomic<std::uint64_t> rewalkNs{ 0 };        // time of the main-thread rewalks (DrawTimeGreySet.cpp)
        std::atomic<std::uint64_t> seenGeneration{ 0 };  // the newest snapshot generation the hook loaded
        std::atomic<int> inside{ 0 };                    // hook bodies running now (snapshot reads in flight)
        std::atomic<int> slot{ -1 };                     // the measured palette slot, -1 until measured
        std::atomic<std::uint32_t> renderThread{ 0 };    // the thread of the last SetupGeometry seen while stoked
    };

    [[nodiscard]] Counters& GetCounters() noexcept;

    /// The hooks were written (both slots)
    [[nodiscard]] bool HookInstalled() noexcept;

    /// Main thread, every 5 s while stoked: the summary line (rates since the last call); a_setSize / a_roots from the set
    void LogSummary(std::size_t a_setSize, std::size_t a_roots, const std::string& a_kinds);

    /// Main thread, when the buff comes on: the next summary's rates count from now
    void StartSummaryWindow();

    /// Main thread, when the buff comes on: an unmeasured slot's vote and its examined-materials budget start again
    void ResetSlotVote();

    /// DrawTimeGreySet.cpp: starts the ticker (InstallDrawTimeGrey calls it after the hook)
    void StartSetTicker();

    /// DrawTimeGreyHook.cpp: writes the two vfuncs; false when an id is missing or the palettes did not load
    [[nodiscard]] bool InstallHook();
}
