// =============================================================================
// librarian-test --self-test, scan record part  -  ScanRecordRules.h
// =============================================================================
//
// The pure half of the scan record (docs/ARCHITECTURE.md, "Scan at Game Start"):
// the scan fingerprint moves with a plugin's size or write time, the gate rules
// file, the scanner's format version and the tome reach outcome, and stays the
// same when none of them changes; a scan that could not mark everything leaves
// no record; a record of another version never matches; when the vampire caster
// walk counts as failed. Needs no dump and no game.

#include "LibrarianSelfTest.h"

#include "ScanRecordRules.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
    using ScanRecordRules::Casters;
    using ScanRecordRules::PluginStamp;
    using ScanRecordRules::RecordAction;

    int* g_passed = nullptr;
    int* g_failed = nullptr;

    void Check(bool ok, const char* what)
    {
        (ok ? *g_passed : *g_failed)++;
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    }

    constexpr const char* kLoadOrder = "0123456789abcdef";
    constexpr int kVersion = ScanRecordRules::kScannerFormatVersion;

    std::vector<PluginStamp> Plugins()
    {
        return { { "Skyrim.esm", 249380772, 133000000000000000ULL }, { "Mod.esp", 4096, 133100000000000000ULL } };
    }

    std::string Hash(const std::vector<PluginStamp>& plugins, const std::string& gate = "none",
        const std::string& loadOrder = kLoadOrder, int version = kVersion)
    {
        return ScanRecordRules::ScanHash(loadOrder, plugins, gate, version);
    }

    void Fingerprint()
    {
        const std::string base = Hash(Plugins());
        Check(base.size() == 16 && base == Hash(Plugins()), "scan fingerprint: the same inputs give the same 16 hex digits");

        auto bigger = Plugins();
        bigger[1].bytes += 1;
        Check(Hash(bigger) != base, "scan fingerprint: a plugin updated under the same name (one byte more) changes it");

        auto touched = Plugins();
        touched[1].writeTime += 1;
        Check(Hash(touched) != base, "scan fingerprint: a plugin's write time alone changes it");

        Check(Hash(Plugins(), ScanRecordRules::HashText("{\"rules\": []}")) != base &&
                  Hash(Plugins(), ScanRecordRules::HashText("{\"rules\": []}")) !=
                      Hash(Plugins(), ScanRecordRules::HashText("{\"rules\": [{}]}")),
            "scan fingerprint: a gate rules file, and a change to it, change it");

        Check(Hash(Plugins(), "none", kLoadOrder, kVersion + 1) != base,
            "scan fingerprint: a new scanner format version changes it");
        Check(Hash(Plugins(), "none", "fedcba9876543210") != base,
            "scan fingerprint: another load order fingerprint (names, order, spell count) changes it");

        auto swapped = Plugins();
        std::swap(swapped[0].bytes, swapped[1].bytes);
        std::swap(swapped[0].writeTime, swapped[1].writeTime);
        Check(Hash(swapped) != base, "scan fingerprint: the same stamps on other plugins change it");

        Check(Hash(Plugins(), "").empty() && Hash(Plugins(), "none", "").empty(),
            "scan fingerprint edge: an unreadable gate rules file or no load order gives none (matches nothing)");
        Check(Hash({}).size() == 16, "scan fingerprint edge: no plugin stamps still gives a fingerprint");
    }

    void Incomplete()
    {
        Check(ScanRecordRules::IncompleteReasons(true, Casters::kComplete).empty() &&
                  ScanRecordRules::IncompleteReasons(true, Casters::kNotRun).empty(),
            "incomplete: the tome reach pass ended (ready or failed) and the casters collected (or never asked) - nothing missing");
        Check(ScanRecordRules::IncompleteReasons(false, Casters::kComplete).size() == 1 &&
                  ScanRecordRules::IncompleteReasons(true, Casters::kFailed).size() == 1 &&
                  ScanRecordRules::IncompleteReasons(false, Casters::kFailed).size() == 2,
            "incomplete: the tome reach pass still running, a failed caster collection, or both - one reason each");

        nlohmann::json output = { { "spells", nlohmann::json::array() } };
        ScanRecordRules::MarkIncomplete(output, {});
        Check(!output.contains(ScanRecordRules::kIncompleteField) && ScanRecordRules::IncompleteOf(output).empty(),
            "incomplete: a complete scan's output has no field and reads as complete");
        ScanRecordRules::MarkIncomplete(output, ScanRecordRules::IncompleteReasons(false, Casters::kFailed));
        const std::string both = ScanRecordRules::IncompleteOf(output);
        Check(both.find("tomeUnreachable") != std::string::npos && both.find("castByVampires") != std::string::npos &&
                  both.find("; ") != std::string::npos,
            "incomplete: both reasons read back from the output, joined");

        Check(!ScanRecordRules::IncompleteOf(nlohmann::json{ { "incomplete", "yes" } }).empty() &&
                  !ScanRecordRules::IncompleteOf(nlohmann::json{ { "incomplete", nlohmann::json::array() } }).empty() &&
                  ScanRecordRules::IncompleteOf(nlohmann::json::array()).empty(),
            "incomplete edge: a field that is not a list of reasons, or an empty list, still reads as incomplete");
    }

    void Decision()
    {
        constexpr const char* kScan = "0123456789abcdef";
        const auto write = ScanRecordRules::DecideRecord(true, kLoadOrder, kScan, "");
        Check(write.action == RecordAction::kWrite, "record: a complete full scan under a known fingerprint is recorded");

        const auto incomplete = ScanRecordRules::DecideRecord(true, kLoadOrder, kScan, "no castByVampires marks");
        Check(incomplete.action == RecordAction::kRemove && incomplete.why.find("incomplete") != std::string::npos &&
                  incomplete.why.find("castByVampires") != std::string::npos,
            "record: an incomplete full scan is not recorded, and says why");

        const auto tome = ScanRecordRules::DecideRecord(false, kLoadOrder, kScan, "");
        const auto noLoadOrder = ScanRecordRules::DecideRecord(true, "", "", "");
        const auto noScan = ScanRecordRules::DecideRecord(true, kLoadOrder, "", "");
        Check(tome.action == RecordAction::kRemove && tome.why.find("tome scan") != std::string::npos &&
                  noLoadOrder.action == RecordAction::kRemove && noLoadOrder.why.find("load order is unknown") != std::string::npos &&
                  noScan.action == RecordAction::kRemove && noScan.why.find("gate/rules.json") != std::string::npos,
            "record: a tome scan, an unknown load order and an unknown scan fingerprint each remove it with their own reason");
    }

    void TomeReachOutcome()
    {
        const std::string scan = Hash(Plugins());
        const std::string ready = ScanRecordRules::WithTomeReach(scan, "ready");
        const std::string failedA = ScanRecordRules::WithTomeReach(scan, "failed:unreadable:" + ScanRecordRules::HashText("A.esp\n"));
        const std::string failedB = ScanRecordRules::WithTomeReach(scan, "failed:unreadable:" + ScanRecordRules::HashText("B.esp\n"));
        Check(ready.size() == 16 && ready == ScanRecordRules::WithTomeReach(scan, "ready") &&
                  failedA == ScanRecordRules::WithTomeReach(scan, "failed:unreadable:" + ScanRecordRules::HashText("A.esp\n")),
            "tome reach outcome: the same outcome gives the same fingerprint (a pass failing the same way keeps the record)");
        Check(ready != failedA && failedA != failedB,
            "tome reach outcome: ready against failed, and other unreadable plugins, change it");
        Check(ScanRecordRules::WithTomeReach(scan, "").empty() && ScanRecordRules::WithTomeReach("", "ready").empty(),
            "tome reach outcome edge: a pass still running, or no scan fingerprint, gives none");
    }

    void RecordVersion()
    {
        const std::string scan = "0123456789abcdef";
        const nlohmann::json current = { { "version", ScanRecordRules::kRecordVersion }, { "scan", scan } };
        Check(ScanRecordRules::RecordFits(current, scan), "record match: this version's record under the same fingerprint fits");
        Check(!ScanRecordRules::RecordFits({ { "version", 1 }, { "scan", scan }, { "loadOrder", scan } }, scan) &&
                  !ScanRecordRules::RecordFits({ { "scan", scan } }, scan) &&
                  !ScanRecordRules::RecordFits({ { "version", "2" }, { "scan", scan } }, scan),
            "record match: a version 1 record, one with no version or a version that is not a number never fits");
        Check(!ScanRecordRules::RecordFits(current, "fedcba9876543210") && !ScanRecordRules::RecordFits(current, "") &&
                  !ScanRecordRules::RecordFits({ { "version", ScanRecordRules::kRecordVersion } }, scan) &&
                  !ScanRecordRules::RecordFits(nlohmann::json::array(), scan),
            "record match: another fingerprint, an empty one, no scan field or not an object - no fit");
    }

    void CasterWalk()
    {
        Check(!ScanRecordRules::CasterWalkFailed(1000, 0) && !ScanRecordRules::CasterWalkFailed(1000, 1) &&
                  !ScanRecordRules::CasterWalkFailed(1000, 500) && !ScanRecordRules::CasterWalkFailed(0, 0),
            "caster walk: none, one, or half the NPCs skipped (or no NPC) - not failed");
        Check(ScanRecordRules::CasterWalkFailed(1000, 501) && ScanRecordRules::CasterWalkFailed(3, 3) &&
                  ScanRecordRules::CasterWalkFailed(1, 1),
            "caster walk: more than half, or every NPC, skipped - failed");
    }
}

void RunScanRecordSelfTest(int& passed, int& failed)
{
    g_passed = &passed;
    g_failed = &failed;
    Fingerprint();
    Incomplete();
    Decision();
    TomeReachOutcome();
    RecordVersion();
    CasterWalk();
}
