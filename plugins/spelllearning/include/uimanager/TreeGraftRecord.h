#pragma once

#include "AsciiText.h"
#include "ScanRecordRules.h"
#include "uimanager/TreeRecord.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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

    /// A catalog's spells (spell_catalog.json "spells", keyed by persistentId); none when it has none
    [[nodiscard]] inline std::optional<std::vector<std::string>> CatalogSeen(const Json& catalog)
    {
        if (!catalog.is_object()) return std::nullopt;
        const auto spells = catalog.find("spells");
        if (spells == catalog.end() || !spells->is_object() || spells->empty()) return std::nullopt;
        std::vector<std::string> seen;
        seen.reserve(spells->size());
        for (const auto& [key, value] : spells->items()) seen.push_back(key);
        return seen;
    }

    /// The record's list (an empty one is a list); else the previous catalog's spells
    /// (CatalogSeen); else none
    [[nodiscard]] inline Baseline BaselineOf(const TreeRecordFile::TreeRecord& record, const std::optional<Json>& catalog)
    {
        if (record.hasSeen) return { record.seen, SeenSource::kRecord };
        if (catalog) {
            if (auto seen = CatalogSeen(*catalog)) return { std::move(*seen), SeenSource::kCatalog };
        }
        return {};
    }

    /// The record after a run (see the top): `complete` = nothing deferred or unplaced.
    /// `inTree`: spells in the tree the list may lack - the new persistentIds of nodes found
    /// again by editor id, and the scan's spells the tree holds (HeldUnseen) - seen, so
    /// deleting such a node does not graft it again
    [[nodiscard]] inline TreeRecordFile::TreeRecord RecordAfterGraft(TreeRecordFile::TreeRecord record,
        const Baseline& baseline, const std::vector<std::string>& grafted, const std::vector<std::string>& leftOut,
        bool complete, const std::string& loadOrder, std::size_t plugins, std::size_t spells,
        const std::vector<std::string>& inTree = {})
    {
        std::vector<std::string> seen = baseline.seen;
        std::unordered_set<std::string> folded;
        for (const auto& id : seen) folded.insert(AsciiText::ToLower(id));
        for (const auto* list : { &grafted, &leftOut, &inTree }) {
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

    /// An edit save over the tree the session's graft wrote that lacks some of its nodes
    /// (`carried`: how many of the grafted formIds it still has) is stale - its panel held
    /// the tree from before the graft - only when it was asked for before the graft's write
    /// (`stampAtEntry` lower than `stampNow`, TreeGraftWriteStamp) and carries none of them.
    /// Asked for after the write, or carrying some: the player deleted those nodes in edit
    /// mode, and they stay out (Ruling 24)
    [[nodiscard]] inline bool StaleGraftSave(std::uint64_t stampAtEntry, std::uint64_t stampNow, std::size_t carried)
    {
        return stampAtEntry < stampNow && carried == 0;
    }

    /// The places in `ids` (the graft's node formIds) of those that are no JSON string anywhere
    /// in `text` (an edit save's tree): one pass collects the strings no longer than the longest
    /// id, then one lookup each (a search of the whole text per id was O(ids x text) on the game
    /// thread). A string's escapes are skipped over, not decoded: an id is plain hex
    [[nodiscard]] inline std::vector<std::size_t> IdsNotIn(std::string_view text, const std::vector<std::string>& ids)
    {
        std::size_t longest = 0;
        for (const auto& id : ids) longest = (std::max)(longest, id.size());
        std::unordered_set<std::string_view> strings;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '"') continue;
            std::size_t end = i + 1;
            while (end < text.size() && text[end] != '"') end += text[end] == '\\' ? 2 : 1;
            if (end >= text.size()) break;
            if (end - i - 1 <= longest) strings.insert(text.substr(i + 1, end - i - 1));
            i = end;
        }
        std::vector<std::size_t> missing;
        for (std::size_t k = 0; k < ids.size(); ++k) {
            if (!strings.contains(ids[k])) missing.push_back(k);
        }
        return missing;
    }

    /// SaveSpellTree's "the same tree as the last save - not rewritten": the text is the last
    /// save's (`hash` == `lastHash`) and no graft wrote the tree file since (the write stamp,
    /// TreeGraftWriteStamp, when that save wrote is the one now). A graft's write in between
    /// replaced the file: Import A, the graft writes B, Import A again must write A
    [[nodiscard]] inline bool SameAsLastSave(std::size_t hash, std::size_t lastHash, std::uint64_t stampAtLastSave,
        std::uint64_t stampNow)
    {
        return hash == lastHash && stampAtLastSave == stampNow;
    }

    /// A stale edit save (StaleGraftSave) dropped grafted nodes (Ruling 23): their spells
    /// (`lost`, persistentIds) leave the seen list (A-Z folded) and pendingGrafted, and the
    /// load order and the notice told go back to the record's before the graft (`held`), so
    /// the next start grafts them again and the "rebuild" notice is as it was
    struct HeldLoadOrder
    {
        std::string loadOrder;
        std::size_t plugins = 0, spells = 0;
        std::string noticedLoadOrder;
    };
    [[nodiscard]] inline TreeRecordFile::TreeRecord RecordAfterLostGraft(TreeRecordFile::TreeRecord record,
        const std::vector<std::string>& lost, const HeldLoadOrder& held)
    {
        std::unordered_set<std::string> gone;
        for (const auto& id : lost) gone.insert(AsciiText::ToLower(id));
        std::erase_if(record.seen, [&gone](const std::string& id) { return gone.contains(AsciiText::ToLower(id)); });
        record.pendingGrafted -= (std::min)(record.pendingGrafted, lost.size());
        record.loadOrder = held.loadOrder;
        record.plugins = held.plugins;
        record.spells = held.spells;
        record.noticedLoadOrder = held.noticedLoadOrder;
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
        auto seen = CatalogSeen(catalog);
        if (!seen) return std::nullopt;
        record.seen = std::move(*seen);
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
    // record was not); with no tree file but a spell_tree.json.tmp of that hash, the .tmp is
    // moved into place first (Ruling 21); any other journal is dropped (the tree was not written).

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

    /// kWait: the tree file is there but could not be read - the journal is left as it is.
    /// kRecover: no tree file, but its .tmp is the tree the journal was for (a crash while an
    /// older WriteAtomically had the file moved to .bak): moved into place, then applied
    enum class JournalAction { kNone, kApply, kDrop, kWait, kRecover };
    struct JournalResult
    {
        JournalAction action = JournalAction::kNone;
        TreeRecordFile::TreeRecord record;   ///< what to write (kApply, kDrop)
    };

    /// The journal's write may go: the record (and, for an apply, the tree; for a recovery,
    /// the .tmp - pass its texts as the tree's) on disk are still what the journal was
    /// resolved from - a panel save or import in between wins, and the journal waits for
    /// the next start
    [[nodiscard]] inline bool JournalStillFits(JournalAction action, const std::optional<std::string>& recordRead,
        const std::optional<std::string>& recordNow, const std::optional<std::string>& treeRead,
        const std::optional<std::string>& treeNow)
    {
        if (action == JournalAction::kNone || action == JournalAction::kWait || recordNow != recordRead) return false;
        return (action != JournalAction::kApply && action != JournalAction::kRecover) || treeNow == treeRead;
    }

    /// What a record's journal asks for, against the tree text on disk (nothing: no tree file;
    /// `treeUnreadable`: a tree file there that could not be read, which says nothing - kWait).
    /// `tempText`: spell_tree.json.tmp, read when there is no tree file (kRecover when it is
    /// the journal's tree)
    [[nodiscard]] inline JournalResult ResolveJournal(const TreeRecordFile::TreeRecord& record,
        const std::optional<std::string>& treeText, bool treeUnreadable = false,
        const std::optional<std::string>& tempText = std::nullopt)
    {
        JournalResult result;
        const auto journal = record.extra.find(kJournalKey);
        if (journal == record.extra.end()) return result;
        if (!treeText && treeUnreadable) {
            result.action = JournalAction::kWait;
            return result;
        }
        const auto isJournalsTree = [&](const std::optional<std::string>& text) {
            return text && journal->is_object() && journal->contains("treeHash") && (*journal)["treeHash"].is_string() &&
                   (*journal)["treeHash"].get<std::string>() == ScanRecordRules::HashText(*text) &&
                   journal->contains("record") && (*journal)["record"].is_object();
        };
        const bool written = isJournalsTree(treeText);
        const bool recover = !treeText && isJournalsTree(tempText);
        if (written || recover) {
            result.action = recover ? JournalAction::kRecover : JournalAction::kApply;
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
