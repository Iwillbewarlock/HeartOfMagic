// graft-run-check, its record part (graft-run-check.cpp runs these): the scan dump the
// graft reads is the one the scan record names (ScanRecordRules.h), the seen list kept in
// the tree record before the catalog is replaced, and the journal that keeps the record
// with the tree, and the record after an edit save dropped grafted nodes (uimanager/TreeGraftRecord.h).

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

        // Only into the record its own scan wrote (a Papyrus or panel scan may have written another since)
        Json own = { { "version", 2 }, { ScanRecordRules::kWriteIdField, "aa-1-1" } };
        check.Check(ScanRecordRules::MarkDumpIfOwn(own, "aa-1-1", dump) && ScanRecordRules::DumpFits(own, dump),
            "the record this scan wrote: the dump marked");
        Json other = { { "version", 2 }, { ScanRecordRules::kWriteIdField, "aa-2-2" } };
        check.Check(!ScanRecordRules::MarkDumpIfOwn(other, "aa-1-1", dump) && !ScanRecordRules::DumpFits(other, dump),
            "another scan's record since: not marked, so no graft from this dump");
        Json old = { { "version", 2 } };
        check.Check(!ScanRecordRules::MarkDumpIfOwn(old, "aa-1-1", dump) && !ScanRecordRules::MarkDumpIfOwn(own, "", dump),
            "a record without a writeId, or no writeId given: not marked");
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
        check.Check(TreeGraftRun::CatalogNeeded(record, "new", "old"),
            "no seen list, another load order, the catalog the previous load order's: needed");
        check.Check(!TreeGraftRun::CatalogNeeded(record, "old", "older"), "the record is this load order's: not needed (no graft runs)");
        check.Check(!TreeGraftRun::CatalogNeeded(record, "new", ""),
            "the catalog's load order unknown (no scan record, an incomplete scan): not seeded");
        check.Check(!TreeGraftRun::CatalogNeeded(record, "new", "new"),
            "the catalog already this load order's (a panel scan made it): not seeded");
        auto seen = record;
        seen.hasSeen = true;
        check.Check(!TreeGraftRun::CatalogNeeded(seen, "new", "old"), "a seen list already: not needed");
        const auto seeded = TreeGraftRun::SeededRecord(record, catalog);
        check.Check(seeded && seeded->hasSeen && seeded->seen == std::vector<std::string>{ "B.esp|0x000801" } &&
                        seeded->loadOrder == "old",
            "seeded: the catalog's spells, the load order as it was");
        check.Check(TreeGraftRun::BaselineOf(*seeded, std::nullopt).source == TreeGraftRun::SeenSource::kRecord,
            "a graft after that takes them from the record (the catalog may be gone)");
        check.Check(!TreeGraftRun::SeededRecord(record, Json{ { "spells", Json::object() } }), "a catalog without spells: none");
        return check.failed;
    }

    int LostCase()
    {
        std::printf("an edit save that dropped grafted nodes (Ruling 23)\n");
        Checker check;
        TreeRecord record;   // as a complete graft left it
        record.loadOrder = "new";
        record.plugins = 210;
        record.spells = 4000;
        record.hasSeen = true;
        record.seen = { "Skyrim.esm|0x012FCD", "NewMod.esp|0x000801", "NewMod.esp|0x000802", "NewMod.esp|0x000803" };
        record.pendingGrafted = 2;
        record.extra = { { "later", 1 } };
        const auto after = TreeGraftRun::RecordAfterLostGraft(record, { "newmod.esp|0x000801" }, { "old", 200, 3900 });
        check.Check(after.seen == std::vector<std::string>{ "Skyrim.esm|0x012FCD", "NewMod.esp|0x000802", "NewMod.esp|0x000803" } &&
                        after.hasSeen,
            "the lost spell leaves the seen list (any case); the others stay");
        check.Check(after.loadOrder == "old" && after.plugins == 200 && after.spells == 3900,
            "the load order back to the record's before the graft: the next start grafts it again");
        check.Check(after.pendingGrafted == 1 && after.extra.contains("later"), "one fewer to tell about; other fields kept");
        const auto none = TreeGraftRun::RecordAfterLostGraft(record, { "A.esp|0x1", "B.esp|0x2", "C.esp|0x3" }, {});
        check.Check(none.pendingGrafted == 0 && none.seen.size() == 4 && none.loadOrder.empty(),
            "more lost than told: zero, not below; ids not in the list change nothing; no record before: no load order");
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
        check.Check(TreeGraftRun::ResolveJournal(reread, std::nullopt, true).action == TreeGraftRun::JournalAction::kWait,
            "a tree file there but unreadable (locked): left as it is, not dropped");
        check.Check(TreeGraftRun::ResolveJournal(reread, tree, true).action == TreeGraftRun::JournalAction::kApply,
            "the tree read after all: the unreadable flag does not matter");
        check.Check(!TreeGraftRun::JournalStillFits(TreeGraftRun::JournalAction::kWait, std::string("r"), std::string("r"),
                        std::nullopt, std::nullopt),
            "waiting: nothing is written");
        check.Check(TreeGraftRun::ResolveJournal(current, tree).action == TreeGraftRun::JournalAction::kNone, "no journal: nothing");

        // No tree file, its .tmp there (a crash while the old WriteAtomically had moved the file to .bak)
        const auto recovered = TreeGraftRun::ResolveJournal(reread, std::nullopt, false, tree);
        check.Check(recovered.action == TreeGraftRun::JournalAction::kRecover && recovered.record.loadOrder == "new" &&
                        !recovered.record.extra.contains(TreeGraftRun::kJournalKey),
            "no tree file, the .tmp is the journal's tree: moved into place and the journal applied");
        check.Check(TreeGraftRun::ResolveJournal(reread, std::nullopt, false, std::string("{\"schools\"")).action ==
                        TreeGraftRun::JournalAction::kDrop,
            "no tree file, a .tmp of another (a half-written) tree: dropped as before");
        check.Check(TreeGraftRun::ResolveJournal(reread, std::string("{}"), false, tree).action ==
                        TreeGraftRun::JournalAction::kDrop,
            "a tree file there (not the journal's): the .tmp does not count, dropped");
        check.Check(TreeGraftRun::ResolveJournal(reread, std::nullopt, true, tree).action == TreeGraftRun::JournalAction::kWait,
            "a tree file there but unreadable: waits, whatever the .tmp");

        // Its write only over the files as read
        using TreeGraftRun::JournalAction;
        using TreeGraftRun::JournalStillFits;
        const std::optional<std::string> rec = std::string("r1"), rec2 = std::string("r2");
        const std::optional<std::string> t1 = tree, t2 = std::string("{}");
        check.Check(JournalStillFits(JournalAction::kApply, rec, rec, t1, t1), "apply: record and tree as read - written");
        check.Check(!JournalStillFits(JournalAction::kApply, rec, rec2, t1, t1),
            "apply: the record changed (a panel build's Apply) - waits");
        check.Check(!JournalStillFits(JournalAction::kApply, rec, rec, t1, t2), "apply: the tree changed (a panel save) - waits");
        check.Check(JournalStillFits(JournalAction::kDrop, rec, rec, t1, t2), "drop: only the record must be as read");
        check.Check(!JournalStillFits(JournalAction::kDrop, rec, std::nullopt, t1, t1), "drop: the record gone - waits");
        check.Check(!JournalStillFits(JournalAction::kNone, rec, rec, t1, t1), "nothing to write: no");
        check.Check(JournalStillFits(JournalAction::kRecover, rec, rec, t1, t1) &&
                        !JournalStillFits(JournalAction::kRecover, rec, rec, t1, std::nullopt) &&
                        !JournalStillFits(JournalAction::kRecover, rec, rec2, t1, t1),
            "recover: the record and the .tmp as read - written; the .tmp gone or the record changed - waits");
        return check.failed;
    }
}
