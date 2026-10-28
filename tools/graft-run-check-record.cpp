// graft-run-check, its record part (graft-run-check.cpp runs these): the scan dump the
// graft reads is the one the scan record names (ScanRecordRules.h), the seen list kept in
// the tree record before the catalog is replaced, and the journal that keeps the record
// with the tree (uimanager/TreeGraftRecord.h).

#include "Common.h"

#include "ScanRecordRules.h"
#include "uimanager/TreeGraftRecord.h"

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace
{
    using Json = nlohmann::json;
    using TreeRecordFile::TreeRecord;

    struct Checker
    {
        int failed = 0;
        void Check(bool ok, const std::string& what)
        {
            if (!ok) ++failed;
            std::printf("    [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
        }
    };
}

namespace GraftRunRecordCases
{
    int DumpCase()
    {
        std::printf("the scan dump the graft reads is the record's\n");
        Checker check;
        Json record = { { "version", 2 }, { "loadOrder", "aa" } };
        const std::string dump = R"({"spells":[]})";
        check.Check(!ScanRecordRules::DumpFits(record, dump), "a record without the dump's fields (a panel scan's): no");
        ScanRecordRules::MarkDump(record, dump);
        check.Check(ScanRecordRules::DumpFits(record, dump), "the dump the scan at game start wrote: yes");
        check.Check(!ScanRecordRules::DumpFits(record, R"({"spells":[1]})"), "another dump (an older or a newer one): no");
        check.Check(!ScanRecordRules::DumpFits(record, dump + " "), "the same dump one byte longer: no");
        return check.failed;
    }

    int SeedCase()
    {
        std::printf("the seen list kept before the catalog is replaced\n");
        Checker check;
        TreeRecord record;
        record.loadOrder = "old";
        const Json catalog = { { "spells", { { "B.esp|0x000801", Json::object() } } } };
        check.Check(TreeGraftRun::CatalogNeeded(record, "new"), "no seen list, another load order: needed");
        check.Check(!TreeGraftRun::CatalogNeeded(record, "old"), "the record is this load order's: not needed (no graft runs)");
        auto seen = record;
        seen.hasSeen = true;
        check.Check(!TreeGraftRun::CatalogNeeded(seen, "new"), "a seen list already: not needed");
        const auto seeded = TreeGraftRun::SeededRecord(record, catalog);
        check.Check(seeded && seeded->hasSeen && seeded->seen == std::vector<std::string>{ "B.esp|0x000801" } &&
                        seeded->loadOrder == "old",
            "seeded: the catalog's spells, the load order as it was");
        check.Check(TreeGraftRun::BaselineOf(*seeded, std::nullopt).source == TreeGraftRun::SeenSource::kRecord,
            "a graft after that takes them from the record (the catalog may be gone)");
        check.Check(!TreeGraftRun::SeededRecord(record, Json{ { "spells", Json::object() } }), "a catalog without spells: none");
        return check.failed;
    }

    int JournalCase()
    {
        std::printf("the graft's journal\n");
        Checker check;
        TreeRecord current;
        current.loadOrder = "old";
        current.extra = { { "later", 1 } };
        TreeRecord target = current;
        target.loadOrder = "new";
        target.hasSeen = true;
        target.seen = { "N.esp|0x000800" };
        target.pendingGrafted = 1;
        const std::string tree = R"({"schools":{}})";
        const auto journaled = TreeGraftRun::WithJournal(current, tree, target);
        const auto reread = TreeRecordFile::ParseTreeRecord(TreeRecordFile::ToJson(journaled));
        check.Check(reread.loadOrder == "old" && reread.extra.contains(TreeGraftRun::kJournalKey),
            "the journal record is the old record with the journal");

        const auto applied = TreeGraftRun::ResolveJournal(reread, tree);
        check.Check(applied.action == TreeGraftRun::JournalAction::kApply && applied.record.loadOrder == "new" &&
                        applied.record.seen == target.seen && applied.record.pendingGrafted == 1 &&
                        !applied.record.extra.contains(TreeGraftRun::kJournalKey) && applied.record.extra.contains("later"),
            "the tree on disk is the journal's: its record applied, without the journal");
        const auto dropped = TreeGraftRun::ResolveJournal(reread, std::string(R"({"schools":{"A":{}}})"));
        check.Check(dropped.action == TreeGraftRun::JournalAction::kDrop && dropped.record.loadOrder == "old" &&
                        !dropped.record.extra.contains(TreeGraftRun::kJournalKey) && dropped.record.extra.contains("later"),
            "another tree on disk: the journal dropped, the old record kept");
        check.Check(TreeGraftRun::ResolveJournal(reread, std::nullopt).action == TreeGraftRun::JournalAction::kDrop,
            "no tree file: dropped");
        check.Check(TreeGraftRun::ResolveJournal(current, tree).action == TreeGraftRun::JournalAction::kNone, "no journal: nothing");
        return check.failed;
    }
}
