#pragma once

#include "Common.h"
#include "SpellTomePairs.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

// =============================================================================
// SPELL TOMES - which spell a book is the tome of
// =============================================================================
//
// One answer for every place that asks "is this book a spell tome, and for which
// spell": the spell the book teaches, and for a book that teaches nothing the
// spell Spell Research Synthesizer paired it with (SynthesizedPatch.json first,
// Spell Research's tier lists as an unverified fallback - SpellTomePairs.h has the
// rule). The pairs are a snapshot, rebuilt on the game thread and read from any
// thread; each new set of pairs gets a new generation, and a new generation
// re-runs the tome reach pass, regroups the copies (SpellScannerDuplicates.cpp),
// drops the carried-tome cache and tells the panel (autoBuild.js retries a failed
// build at game start once). docs/ARCHITECTURE.md, "Spell tomes that teach nothing".
// =============================================================================

namespace SpellTomes
{
    // The spell `book` stands for: the one it teaches, else its pair; nullptr when
    // it is no spell tome. Any thread (the snapshot is read through an atomic).
    RE::SpellItem* SpellOf(RE::TESObjectBOOK* book);

    // A book that teaches nothing and has a pair. Any thread, touches only the
    // snapshot (the inventory sink's question).
    bool IsPairedTome(RE::FormID book);

    // Every spell tome with its spell, both kinds, in the BOOK array's order.
    // Game thread.
    void ForEachTome(const std::function<void(RE::TESObjectBOOK*, RE::SpellItem*)>& visit);

    // Reads the pairs again: kDataLoaded (before the tome reach pass), kPostLoadGame,
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

    // A full scan was collected now (game thread): a later generation with more
    // pairs marks that scan stale for the panel.
    void NoteFullScan();
}
