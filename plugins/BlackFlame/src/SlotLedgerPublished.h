#pragma once

// =============================================================================
// SLOT LEDGER PUBLISHED - the files the ledger's published (and previous) objects stand on (included by SlotLedgerModel.h)
// =============================================================================
// Split out of SlotLedgerModel.h for the 600-line limit (publication fix, 2026-10-19). No game types.

#include "SlotLedgerModel.h"

#include <cstdint>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace BlackFlame::SlotLedgerModel
{
    /// The files an object stands on: its own (path, fnv64) when it has one, then each dep's that is a file (not an ADDN
    /// slot), in order. The checks that a file entered or was replaced in Data this session look at every one
    [[nodiscard]] inline std::vector<std::pair<std::string, std::uint64_t>> FilesOf(const Object& a_object)
    {
        std::vector<std::pair<std::string, std::uint64_t>> files;
        if (!a_object.path.empty()) {
            files.emplace_back(a_object.path, a_object.fnv64);
        }
        for (const auto& dep : a_object.deps) {
            if (!dep.isAddn && !dep.path.empty()) {
                files.emplace_back(dep.path, dep.fnv64);
            }
        }
        return files;
    }

    /// review round 1 D1: some published object of a_ledger stands on a_path with a_fnv64 (FilesOf: its own file or a file
    /// dep). A copy path names its key (the key16 folder), so that file is a verified build of the same key. A previous
    /// object counts too: a later start may show it (publication fix, 2026-10-19)
    [[nodiscard]] inline bool PublishedHolds(const Ledger& a_ledger, const std::string& a_path, std::uint64_t a_fnv64)
    {
        for (const auto& entry : a_ledger.entries) {
            for (const auto* objects : { &entry.published, &entry.previous }) {
                for (const auto& object : *objects) {
                    for (const auto& [path, fnv] : FilesOf(object)) {
                        if (fnv == a_fnv64 && path == a_path) {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    /// Every file the published objects of a ledger stand on (FilesOf): by path, the fnv64s recorded for it
    using PublishedFileMap = std::map<std::string, std::set<std::uint64_t>>;

    /// (reviewer round 2) PublishedHolds for many paths: one walk of a_ledger, then a path is looked up before the file
    /// there is hashed (a path no published object names needs no hash)
    [[nodiscard]] inline PublishedFileMap PublishedFiles(const Ledger& a_ledger)
    {
        PublishedFileMap files;
        for (const auto& entry : a_ledger.entries) {
            for (const auto* objects : { &entry.published, &entry.previous }) {
                for (const auto& object : *objects) {
                    for (auto& [path, fnv] : FilesOf(object)) {
                        files[std::move(path)].insert(fnv);
                    }
                }
            }
        }
        return files;
    }
}
