#pragma once

#include "LoadOrderFingerprint.h"

#include <string_view>

// =============================================================================
// SCAN RECORD (docs/ARCHITECTURE.md, "Scan at game start")
// =============================================================================
//
//   spell_catalog_loadorder.json, beside spell_catalog.json
//     { "version": 2, "loadOrder": "<fingerprint>", "scan": "<scan fingerprint>",
//       "plugins": n, "spells": n,
//       "catalogBytes": n, "catalogHash": "<FNV-1a 64 of spell_catalog.json>",
//       "rules": "<FNV-1a 64 of CATALOG_VERSION and the librarian/ rule files>" }
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
    // After a scan wrote spell_catalog.json: the load order of a scan of every
    // spell, or nullptr for any other scan, and what that scan could not mark
    // (ScanRecordRules::IncompleteOf of its output; empty: nothing). The record is
    // written only for a full scan that marked everything (DecideRecord), and
    // removed otherwise. Any thread; logs its own failures.
    void CatalogWritten(const LoadOrderFingerprint::Value* fullScanLoadOrder, std::string_view incomplete = {}) noexcept;

    // The record removed (the scan at game start could not write its dump): the
    // next game start scans again. Any thread; logs its own failures.
    void Forget(const char* why) noexcept;

    // The catalog on disk is the one a full scan made under `now`. Any thread
    // (reads two files); false when either is missing or unreadable.
    [[nodiscard]] bool CatalogMatches(const LoadOrderFingerprint::Value& now) noexcept;
}
