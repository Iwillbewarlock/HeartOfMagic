#pragma once

#include "LoadOrderFingerprint.h"

#include <string>
#include <string_view>

// =============================================================================
// SCAN RECORD (docs/ARCHITECTURE.md, "Scan at game start")
// =============================================================================
//
//   spell_catalog_loadorder.json, beside spell_catalog.json
//     { "version": 2, "loadOrder": "<fingerprint>",
//       "scan": "<scan fingerprint + tome reach outcome + tome pairs key>",
//       "tomeReach": "<TomeReach::OutcomeKey>", "tomePairs": "<the installed tome pairs' key>",
//       "plugins": n, "spells": n,
//       "catalogBytes": n, "catalogHash": "<FNV-1a 64 of spell_catalog.json>",
//       "rules": "<FNV-1a 64 of CATALOG_VERSION and the librarian/ rule files>",
//       "writeId": "<this write's own mark>",
//       "dumpBytes": n, "dumpHash": "<FNV-1a 64 of spell_scan_output.json>" }
//     (the dump's two fields only from the scan at game start, DumpWritten, into its own write)
//
// Which load order (and which librarian rules) the catalog on disk was made
// from, by a scan of every spell that marked everything. "scan" is what matches
// (LoadOrderFingerprint::Value::scan: plugin names, sizes and write times, the
// spell count, gate/rules.json, the scanner's format version); a record of
// another version never matches. A scan that could not mark everything (its
// output's "incomplete" list, ScanRecordRules.h) writes the catalog but removes
// the record, so the next game start scans again.
// Kept in step with the catalog file inside Librarian::ClassifyScan's lock (its
// onWritten): a full scan that wrote the catalog writes the record, any other
// scan that wrote it (a tome scan with effects) removes it. A catalog changed
// any other way no longer matches the record's hash, so it counts as unknown.

namespace ScanRecord
{
    // After a scan wrote spell_catalog.json: whether it scanned every spell, the
    // load order it scanned under (nullptr when not worked out), and what it
    // could not mark (ScanRecordRules::IncompleteOf of its output; empty:
    // nothing). The record - the load order's scan fingerprint joined to the tome
    // reach pass's outcome (WithTomeReach) - is written only for a full scan that
    // marked everything under a known fingerprint (DecideRecord), and removed
    // otherwise, with the reason logged. Any thread; logs its own failures.
    // `scanPairs`: the tome pairs key the scan's marks read (ScanRecordRules::TomePairsOf of its
    // output); recorded only when the installed pairs still give it. Returns the record's
    // writeId (empty: no record written), for DumpWritten.
    std::string CatalogWritten(bool fullScan, const LoadOrderFingerprint::Value* loadOrder, std::string_view incomplete = {},
        std::string_view scanPairs = {}) noexcept;

    // The record removed (the scan at game start could not write its dump): the
    // next game start scans again. Any thread; logs its own failures.
    void Forget(const char* why) noexcept;

    // The scan at game start wrote `dumpText` to spell_scan_output.json: its size and hash go
    // into the record its CatalogWritten wrote (`writeId`), under the lock the record's writers
    // share; nothing when another scan has written the record since (ScanRecordRules::
    // MarkDumpIfOwn) or there is none. Any thread; logs its own failures.
    void DumpWritten(std::string_view writeId, std::string_view dumpText) noexcept;

    // The load order the record says the catalog was made under (empty: no readable record).
    // Any thread.
    [[nodiscard]] std::string RecordedLoadOrder() noexcept;

    // `dumpText` (spell_scan_output.json as read) is the dump of the scan the record is for
    // (ScanRecordRules::DumpFits). Any thread; false when the record is missing or unreadable.
    [[nodiscard]] bool DumpMatches(std::string_view dumpText) noexcept;

    // The catalog on disk is the one a full scan made under `now`. Any thread
    // (reads two files); false when either is missing or unreadable.
    [[nodiscard]] bool CatalogMatches(const LoadOrderFingerprint::Value& now) noexcept;
}
