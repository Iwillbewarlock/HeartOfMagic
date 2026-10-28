#pragma once

#include "AsciiText.h"
#include "ScanRecordRules.h"
#include "uimanager/TreeRecord.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

// =============================================================================
// TREE GRAFT RECORD - the seen list, the record a graft leaves, its journal
// (TreeGraftRun.h; no game types, tools/graft-run-check.cpp runs them offline)
// =============================================================================
namespace TreeGraftRun
{
    using Json = nlohmann::json;

    enum class SeenSource { kRecord, kCatalog, kNone };
    struct Baseline
    {
        std::vector<std::string> seen;
        SeenSource source = SeenSource::kNone;
    };

    /// The record's list (an empty one is a list); else the previous catalog's spells
    /// (spell_catalog.json "spells", keyed by persistentId); else none
    [[nodiscard]] inline Baseline BaselineOf(const TreeRecordFile::TreeRecord& record, const std::optional<Json>& catalog)
    {
        if (record.hasSeen) return { record.seen, SeenSource::kRecord };
        if (catalog && catalog->is_object()) {
            const auto spells = catalog->find("spells");
            if (spells != catalog->end() && spells->is_object() && !spells->empty()) {
                Baseline baseline{ {}, SeenSource::kCatalog };
                baseline.seen.reserve(spells->size());
                for (const auto& [key, value] : spells->items()) baseline.seen.push_back(key);
                return baseline;
            }
        }
        return {};
    }

    /// The record after a run (see the top): `complete` = nothing deferred or unplaced
    [[nodiscard]] inline TreeRecordFile::TreeRecord RecordAfterGraft(TreeRecordFile::TreeRecord record,
        const Baseline& baseline, const std::vector<std::string>& grafted, const std::vector<std::string>& leftOut,
        bool complete, const std::string& loadOrder, std::size_t plugins, std::size_t spells)
    {
        std::vector<std::string> seen = baseline.seen;
        std::unordered_set<std::string> folded;
        for (const auto& id : seen) folded.insert(AsciiText::ToLower(id));
        for (const auto* list : { &grafted, &leftOut }) {
            for (const auto& id : *list) {
                if (folded.insert(AsciiText::ToLower(id)).second) seen.push_back(id);
            }
        }
        record.seen = std::move(seen);
        record.hasSeen = true;
        record.pendingGrafted += grafted.size();
        if (complete) {
            record.loadOrder = loadOrder;
            record.plugins = plugins;
            record.spells = spells;
            record.noticedLoadOrder.clear();   // the tree is this load order's now
        }
        return record;
    }

    // -------------------------------------------------------------------------
    // The seen list kept before the catalog is replaced (Ruling 19)
    // -------------------------------------------------------------------------

    /// A tree record without a seen list whose load order is not `loadOrder`, and a catalog
    /// the scan record says was made under another known load order (`catalogLoadOrder`):
    /// the scan at game start is about to replace the previous load order's catalog, which a
    /// graft would take its seen list from. A catalog of unknown origin (no scan record, an
    /// incomplete scan's) or of this load order is no previous one: nothing is seeded.
    [[nodiscard]] inline bool CatalogNeeded(const TreeRecordFile::TreeRecord& record, const std::string& loadOrder,
        const std::string& catalogLoadOrder)
    {
        return !record.hasSeen && record.loadOrder != loadOrder && !catalogLoadOrder.empty() && catalogLoadOrder != loadOrder;
    }

    /// The record with the catalog's spells as its seen list; nothing when the catalog has none
    [[nodiscard]] inline std::optional<TreeRecordFile::TreeRecord> SeededRecord(TreeRecordFile::TreeRecord record,
        const Json& catalog)
    {
        record.hasSeen = false;
        auto baseline = BaselineOf(record, std::optional<Json>(catalog));
        if (baseline.source != SeenSource::kCatalog) return std::nullopt;
        record.seen = std::move(baseline.seen);
        record.hasSeen = true;
        return record;
    }

    // -------------------------------------------------------------------------
    // The graft's journal (Ruling 18)
    // -------------------------------------------------------------------------
    //
    // The record is written twice around the tree: first the record as it was with a
    // "graftJournal" { treeHash: FNV-1a 64 of the tree text about to be written, record:
    // the record that goes with it }, then that record alone. A start that finds a journal
    // whose treeHash is the tree file's writes its record (the tree was written, the final
    // record was not); any other journal is dropped (the tree was not written).

    inline constexpr const char* kJournalKey = "graftJournal";

    /// `current` with the journal for `treeText` and its `target` record
    [[nodiscard]] inline TreeRecordFile::TreeRecord WithJournal(TreeRecordFile::TreeRecord current,
        std::string_view treeText, const TreeRecordFile::TreeRecord& target)
    {
        auto clean = target;
        clean.extra.erase(kJournalKey);
        current.extra[kJournalKey] = { { "treeHash", ScanRecordRules::HashText(treeText) },
            { "record", TreeRecordFile::ToJson(clean) } };
        return current;
    }

    enum class JournalAction { kNone, kApply, kDrop };
    struct JournalResult
    {
        JournalAction action = JournalAction::kNone;
        TreeRecordFile::TreeRecord record;   ///< what to write (kApply, kDrop)
    };

    /// The journal's write may go: the record (and, for an apply, the tree) on disk are still
    /// what the journal was resolved from - a panel save or import in between wins, and the
    /// journal waits for the next start
    [[nodiscard]] inline bool JournalStillFits(JournalAction action, const std::optional<std::string>& recordRead,
        const std::optional<std::string>& recordNow, const std::optional<std::string>& treeRead,
        const std::optional<std::string>& treeNow)
    {
        if (action == JournalAction::kNone || recordNow != recordRead) return false;
        return action != JournalAction::kApply || treeNow == treeRead;
    }

    /// What a record's journal asks for, against the tree text on disk (nothing: no tree file)
    [[nodiscard]] inline JournalResult ResolveJournal(const TreeRecordFile::TreeRecord& record,
        const std::optional<std::string>& treeText)
    {
        JournalResult result;
        const auto journal = record.extra.find(kJournalKey);
        if (journal == record.extra.end()) return result;
        const bool written = treeText && journal->is_object() && journal->contains("treeHash") &&
                             (*journal)["treeHash"].is_string() &&
                             (*journal)["treeHash"].get<std::string>() == ScanRecordRules::HashText(*treeText) &&
                             journal->contains("record") && (*journal)["record"].is_object();
        if (written) {
            result.action = JournalAction::kApply;
            result.record = TreeRecordFile::ParseTreeRecord((*journal)["record"]);
            result.record.extra.erase(kJournalKey);
        } else {
            result.action = JournalAction::kDrop;
            result.record = record;
            result.record.extra.erase(kJournalKey);
        }
        return result;
    }
}
