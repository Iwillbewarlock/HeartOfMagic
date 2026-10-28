#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
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
//   - The tome reach pass's outcome joined to it (WithTomeReach): a pass that
//     fails the same way every start (the same unreadable plugins) keeps the
//     record, a pass that reads them again makes a scan due.
//   - What a scan could not mark ("incomplete" in the scan output), and that a
//     scan missing a mark leaves no record: the next game start scans again.
//   - When the collection of the spells vampires cast counts as failed.
// =============================================================================

namespace ScanRecordRules
{
    // Raise when the scan output or its marks change in a way the catalog follows:
    // every record made before counts as not matching once
    inline constexpr int kScannerFormatVersion = 1;

    // spell_catalog_loadorder.json's "version". 2: "scan" (the scan fingerprint with the
    // tome reach outcome) is what matches; a record of any other version never does
    inline constexpr int kRecordVersion = 2;

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

    // The scan fingerprint with the tome reach pass's outcome (TomeReach::OutcomeKey: "ready",
    // "failed:...", "not started"), or empty when either is unknown (the pass still runs)
    inline std::string WithTomeReach(std::string_view scanHash, std::string_view tomeReachOutcome)
    {
        if (scanHash.empty() || tomeReachOutcome.empty()) return {};
        std::uint64_t hash = kFnvOffsetBasis;
        Fnv(hash, scanHash);
        Fnv(hash, "|tomereach|");
        Fnv(hash, tomeReachOutcome);
        return Hex(hash);
    }

    // The tome pairs a scan read (SpellTomes: books that teach nothing, each with the spell it
    // stands for) as one key: the same pairs in any order give the same key, "none" for no pair.
    // The load order fingerprint does not see them: Spell Research Synthesizer run again over
    // the same plugins rewrites SynthesizedPatch.json alone. O(n log n) in the pairs.
    inline std::string TomePairsKey(std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs)
    {
        if (pairs.empty()) return "none";
        std::ranges::sort(pairs);
        std::string text;
        text.reserve(pairs.size() * 18);
        for (const auto& [book, spell] : pairs) text += std::format("{:08X}:{:08X}\n", book, spell);
        return HashText(text);
    }

    // The scan output's tome pairs key (FullScanCollector::Finish puts the one its marks read),
    // empty when it has none (a tome scan, or the pairs not read yet)
    inline constexpr const char* kTomePairsField = "tomePairs";
    inline std::string TomePairsOf(const nlohmann::json& output)
    {
        if (!output.is_object()) return {};
        const auto found = output.find(kTomePairsField);
        return found != output.end() && found->is_string() ? found->get<std::string>() : std::string();
    }

    // The record's fingerprint with the tome pairs' key joined, or empty when either is unknown
    // (no fingerprint, or the pairs not read yet)
    inline std::string WithTomePairs(std::string_view fingerprint, std::string_view pairsKey)
    {
        if (fingerprint.empty() || pairsKey.empty()) return {};
        std::uint64_t hash = kFnvOffsetBasis;
        Fnv(hash, fingerprint);
        Fnv(hash, "|tomepairs|");
        Fnv(hash, pairsKey);
        return Hex(hash);
    }

    // How this session's collection of the spells vampires cast went (SpellScannerCasters.cpp)
    enum class Casters
    {
        kNotRun,     // no scan asked yet: nothing was marked from it, nothing is missing
        kComplete,   // read to the end (an NPC or a SPID file that could not be read is skipped and logged)
        kFailed,     // stopped part way, or most NPCs skipped: castByVampires marks are missing
    };

    // The walk over the NPCs failed: more than half of them could not be read (a few
    // broken NPCs are skipped; that many means the walk itself is broken)
    inline bool CasterWalkFailed(std::size_t npcs, std::size_t skipped) noexcept
    {
        return npcs > 0 && skipped * 2 > npcs;
    }

    // What a full scan could not mark, in words; empty when nothing is missing.
    // `tomeReachAnswered`: the pass had ended when the marks were made (TomeReach::OutcomeKey
    // not empty). A pass that failed is an answer - the next start's fails the same way, and
    // its outcome is in the fingerprint (WithTomeReach) - one still running is not.
    inline std::vector<std::string> IncompleteReasons(bool tomeReachAnswered, Casters casters)
    {
        std::vector<std::string> reasons;
        if (!tomeReachAnswered) {
            reasons.emplace_back("the tome reach pass was not done - no tomeUnreachable marks");
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

    // The record on disk was made under `scanFingerprint` by this version's rules. The
    // catalog's own hash and the librarian rules are checked apart (ScanRecord.cpp).
    inline bool RecordFits(const nlohmann::json& record, std::string_view scanFingerprint)
    {
        if (scanFingerprint.empty() || !record.is_object()) return false;
        const auto version = record.find("version");
        if (version == record.end() || !version->is_number_integer() || version->get<long long>() != kRecordVersion) {
            return false;
        }
        const auto scan = record.find("scan");
        return scan != record.end() && scan->is_string() && scan->get<std::string>() == scanFingerprint;
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
    // `loadOrderHash`: LoadOrderFingerprint::Value::hash (empty: not worked out);
    // `scanFingerprint`: WithTomeReach of its `scan` (empty: gate/rules.json unreadable or
    // the tome reach pass still running).
    inline RecordDecision DecideRecord(bool fullScan, std::string_view loadOrderHash, std::string_view scanFingerprint,
        std::string_view incomplete)
    {
        if (!fullScan) return { RecordAction::kRemove, "the catalog was written by a tome scan" };
        if (!incomplete.empty()) return { RecordAction::kRemove, std::format("the scan was incomplete: {}", incomplete) };
        if (loadOrderHash.empty()) return { RecordAction::kRemove, "the load order is unknown" };
        if (scanFingerprint.empty()) {
            return { RecordAction::kRemove,
                "the scan fingerprint is unknown (gate/rules.json could not be read, or the tome reach pass runs)" };
        }
        return { RecordAction::kWrite, {} };
    }
}
