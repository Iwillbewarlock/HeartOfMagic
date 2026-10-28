#pragma once

#include "Common.h"
#include "SpellTomePairs.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// =============================================================================
// SPELL TOMES - which spell a book is the tome of
// =============================================================================
//
// One answer for every place that asks "is this book a spell tome, and for which
// spell": the spell the book teaches, and for a book that teaches nothing the
// spell Spell Research Synthesizer paired it with (SynthesizedPatch.json first,
// Spell Research's tier lists as an unverified fallback - SpellTomePairs.h has the
// rule), or, experimental, the spell a book still points at after a mod cleared its
// TeachesSpell flag at runtime (SpellScribe through SkyPatcher). The pairs are a snapshot, rebuilt on the game thread and read from any
// thread; each new set of pairs gets a new generation, and a new generation
// re-runs the tome reach pass, regroups the copies (SpellScannerDuplicates.cpp),
// drops the carried-tome cache and tells the panel (autoBuild.js retries a failed
// build at game start once). docs/ARCHITECTURE.md, "Spell tomes that teach nothing".
// =============================================================================

namespace SpellTomes
{
    // Rebuild reasons
    inline constexpr std::string_view kReasonSaveLoaded = "save loaded";
    inline constexpr std::string_view kReasonImport = "import";
    inline constexpr std::string_view kReasonScan = "scan";   // a full scan follows: it has these pairs

    // The spell `book` stands for: the one it teaches, else its pair; nullptr when
    // it is no spell tome. Any thread (the snapshot is read through an atomic).
    RE::SpellItem* SpellOf(RE::TESObjectBOOK* book);

    // A book that teaches nothing and has a pair. Any thread, touches only the
    // snapshot (the inventory sink's question).
    bool IsPairedTome(RE::FormID book);

    // Every pair of the snapshot (book that teaches nothing, its spell). Any thread.
    void ForEachPair(const std::function<void(RE::FormID book, RE::FormID spell)>& visit);

    // The pairs the installed files make (SynthesizedPatch.json, Spell Research's plugin part,
    // flags cleared at load), not the ones a loaded save brings (Spell Research's script part):
    // the main menu has the same ones every start. The scan record's key. From one snapshot;
    // false before the pairs were first read. Any thread.
    bool InstalledPairs(std::vector<std::pair<RE::FormID, RE::FormID>>& out);

    // Every spell tome with its spell, both kinds, in the BOOK array's order.
    // Game thread.
    void ForEachTome(const std::function<void(RE::TESObjectBOOK*, RE::SpellItem*)>& visit);

    // Reads the pairs again: kDataLoaded (before the tome reach pass), a new game, kPostLoadGame,
    // before a scan, after Spell Research's Import. A new generation only when the
    // pairs changed. Called off the game thread it posts itself there.
    void Rebuild(std::string_view reason);

    // The pairs' generation: 0 before the first Rebuild. Any thread.
    std::uint64_t Generation();

    // "SpellTomes: N tomes - T by the book's own spell, P by Spell Research pairs ..."
    std::string StatusLine();

    // What the last new generation counted, for the dev self-test. False before the first Rebuild.
    struct Status
    {
        std::uint64_t generation = 0;
        std::size_t ownSpell = 0;          // books that teach a spell themselves
        std::size_t paired = 0;            // books that teach nothing, with a pair
        SpellTomePairs::Counts counts;
        bool patchFound = false;           // SynthesizedPatch.json exists
        bool patchUsable = false;
        bool liveRead = false;             // Spell Research's lists were readable at least once
    };
    bool CurrentStatus(Status& out);

    // A full scan made with the pairs of `generation` (`paired` of them, read when it began) - a stepped scan, whose
    // pairs may have changed before it ended. Recorded as that scan's; when the pairs now hold more, the panel is
    // told at once that the scan is stale (game thread).
    void NoteFullScanOf(std::uint64_t generation, std::size_t paired);
}
