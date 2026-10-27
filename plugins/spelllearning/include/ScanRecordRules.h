#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

// =============================================================================
// ScanRecordRules - the pure half of the scan record (ScanRecord.h)
//
// No game types: the plugin (LoadOrderFingerprint.cpp, ScanRecord.cpp, the
// scanner) and the offline harness (librarian-test --self-test,
// tools/scanrecord-selftest.cpp) compile the same code.
//
//   - The scan's fingerprint: the load order fingerprint (plugin names in order
//     and the spell form count) with each plugin's file size and last-write
//     time, the gate rules file's hash and the scanner's format version. A
//     plugin updated under the same name, a changed gate/rules.json or a
//     scanner whose output changed make a scan due.
//   - What a scan could not mark ("incomplete" in the scan output), and that a
//     scan missing a mark leaves no record: the next game start scans again.
// =============================================================================

namespace ScanRecordRules
{
    // Raise when the scan output or its marks change in a way the catalog follows:
    // every record made before counts as not matching once
    inline constexpr int kScannerFormatVersion = 1;

    // Top-level list in the scan output: what this scan could not mark, in words.
    // Absent when the scan marked everything.
    inline constexpr const char* kIncompleteField = "incomplete";

    inline constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
    inline constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;

    inline void Fnv(std::uint64_t& hash, std::string_view bytes) noexcept
    {
        for (const unsigned char c : bytes) {
            hash ^= c;
            hash *= kFnvPrime;
        }
    }

    inline std::string Hex(std::uint64_t hash) { return std::format("{:016x}", hash); }

    // FNV-1a 64 of a whole text (a small file's bytes), 16 hex digits
    inline std::string HashText(std::string_view bytes)
    {
        std::uint64_t hash = kFnvOffsetBasis;
        Fnv(hash, bytes);
        return Hex(hash);
    }

    // The gate rules file's part of the fingerprint when there is no such file
    inline constexpr std::string_view kNoGateRules = "none";

    // One loaded plugin as the game found it: its file size and last-write time
    // (FILETIME ticks), 0 when not known
    struct PluginStamp
    {
        std::string name;
        std::uint64_t bytes = 0;
        std::uint64_t writeTime = 0;
    };

    // The scan's fingerprint (16 hex digits), or empty when a part is unknown (no
    // load order yet, a gate rules file that could not be read): an empty one
    // matches nothing, so the scan runs and leaves no record.
    inline std::string ScanHash(std::string_view loadOrderHash, const std::vector<PluginStamp>& plugins,
        std::string_view gateRulesHash, int formatVersion)
    {
        if (loadOrderHash.empty() || gateRulesHash.empty()) return {};
        std::uint64_t hash = kFnvOffsetBasis;
        Fnv(hash, loadOrderHash);
        Fnv(hash, "\n");
        for (const auto& plugin : plugins) {
            Fnv(hash, std::format("{}|{}|{}\n", plugin.name, plugin.bytes, plugin.writeTime));
        }
        Fnv(hash, "|gate|");
        Fnv(hash, gateRulesHash);
        Fnv(hash, std::format("|format|{}", formatVersion));
        return Hex(hash);
    }

    // How this session's collection of the spells vampires cast went (SpellScannerCasters.cpp)
    enum class Casters
    {
        kNotRun,     // no scan asked yet: nothing was marked from it, nothing is missing
        kComplete,   // read to the end (an NPC or a SPID file that could not be read is skipped and logged)
        kFailed,     // stopped part way: castByVampires marks are missing
    };

    // What a full scan could not mark, in words; empty when nothing is missing
    inline std::vector<std::string> IncompleteReasons(bool tomeReachMarked, Casters casters)
    {
        std::vector<std::string> reasons;
        if (!tomeReachMarked) {
            reasons.emplace_back("the tome reach pass was not done or failed - no tomeUnreachable marks");
        }
        if (casters == Casters::kFailed) {
            reasons.emplace_back("the spells vampires cast could not be collected - no castByVampires marks");
        }
        return reasons;
    }

    // Puts the reasons into the scan output (nothing when there are none)
    inline void MarkIncomplete(nlohmann::json& output, const std::vector<std::string>& reasons)
    {
        if (!reasons.empty()) output[kIncompleteField] = reasons;
    }

    // The scan output's reasons joined by "; ", empty when it marked everything.
    // A field that is not a list of texts still counts as incomplete.
    inline std::string IncompleteOf(const nlohmann::json& output)
    {
        if (!output.is_object()) return {};
        const auto found = output.find(kIncompleteField);
        if (found == output.end()) return {};
        if (!found->is_array()) return "the scan output's incomplete field is not a list";
        std::string joined;
        for (const auto& reason : *found) {
            if (!joined.empty()) joined += "; ";
            joined += reason.is_string() ? reason.get<std::string>() : std::string("(not a text)");
        }
        return joined.empty() ? std::string("the scan output's incomplete field is empty") : joined;
    }

    enum class RecordAction
    {
        kWrite,    // the catalog is a full scan's of this load order: keep the record
        kRemove,   // anything else: no record, the next game start scans again
    };

    struct RecordDecision
    {
        RecordAction action = RecordAction::kRemove;
        std::string why;   // for the log when removed
    };

    // After a scan wrote spell_catalog.json: whether the record says which load order it is
    // from. Only a scan of every spell that marked everything, under a known fingerprint.
    inline RecordDecision DecideRecord(bool fullScan, std::string_view scanFingerprint, std::string_view incomplete)
    {
        if (!fullScan) return { RecordAction::kRemove, "the catalog was written by a tome scan" };
        if (scanFingerprint.empty()) return { RecordAction::kRemove, "the load order is unknown" };
        if (!incomplete.empty()) return { RecordAction::kRemove, std::format("the scan was incomplete: {}", incomplete) };
        return { RecordAction::kWrite, {} };
    }
}
