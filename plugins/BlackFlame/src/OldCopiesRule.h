#pragma once

// =============================================================================
// OLD COPIES RULE - which copies\<key16>\ folders an old-copies cleanup keeps, and which it may remove (no game types)
// =============================================================================
// Every builder identity change gives every object a new key, so a whole generation of copies\<key16>\ folders stays behind
// in Data and in staging. OldCopies.h removes them once a session; this header is its decision, C++17 only, so
// tools/blackflame-cosave-check tests it offline (OldCopiesTests.h):
//   keep     every key a ledger object stands on (published and previous: its key, its path, its file deps, a TXST's
//            texture strings), every key of every keep set file (OldCopiesKeepsets.h: one per ledger epoch, so MO2 profiles
//            whose save folders share one virtual path, and one mirror name, still keep theirs), and each of those keys and
//            this session's request keys followed through their progress lines (their files and deps, and the deps' own
//            lines). A progress line nothing reaches is not kept: the builder's compaction keeps every line whose staged
//            file is still there (GreyProgress.cpp Compact), so "every key progress.jsonl lists" would keep every old
//            generation for good
//   name     a folder is ours only when its name is exactly 16 lower-case hex digits (DataPathRule::IsCopyKey16); the older
//            copies\palettes\ and copies\_tint\ folders, and anything else, are never touched
//   root     a candidate is removed only when it is a direct child of its root (lexically normal, no "..", absolute)
//   grace    a key whose newest file (Data or staging) was written within oldCopiesGraceDays is kept (WithinGrace)
//   empty    an empty keep set removes nothing (a ledger or request that did not read is no licence to clear the folder)

#include "CopyContractProgress.h"
#include "DataPathRule.h"
#include "SlotLedgerModel.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace BlackFlame::OldCopiesRule
{
    namespace CC = CopyContract;
    namespace LM = SlotLedgerModel;
    using KeySet = std::set<std::uint64_t>;

    inline constexpr std::size_t kMaxFoldersPerSession = 2000;  // removals a session at most (the rest wait for the next)

    /// A folder name that is one of ours: exactly 16 lower-case hex digits; its key, else nullopt
    [[nodiscard]] inline std::optional<std::uint64_t> FolderKey(std::string_view a_name)
    {
        return CC::ParseHex16(a_name);
    }

    /// Every key16 folder a_text names (a copy path, a dep, a TXST texture string with or without "textures\", any case or
    /// separator), added to a_keep. Lenient on purpose: a text that names a folder keeps it
    inline void AddKeysOfText(KeySet& a_keep, std::string_view a_text)
    {
        const auto text = DataPathRule::LowerSlashes(a_text);
        const auto folder = DataPathRule::kCopiesFolder;
        for (auto at = text.find(folder); at != std::string::npos; at = text.find(folder, at + 1)) {
            const auto start = at + folder.size();
            if (start + DataPathRule::kKey16Length > text.size()) {
                break;
            }
            const auto end = start + DataPathRule::kKey16Length;
            if (end != text.size() && text[end] != '\\') {
                continue;  // a longer name: not a key folder
            }
            if (const auto key = FolderKey(std::string_view(text).substr(start, DataPathRule::kKey16Length))) {
                a_keep.insert(*key);
            }
        }
    }

    inline void AddObject(KeySet& a_keep, const LM::Object& a_object)
    {
        a_keep.insert(a_object.key);
        AddKeysOfText(a_keep, a_object.path);
        for (const auto& dep : a_object.deps) {
            if (!dep.isAddn) {
                AddKeysOfText(a_keep, dep.path);
            }
        }
        for (const auto& texture : a_object.textures) {
            AddKeysOfText(a_keep, texture);
        }
    }

    /// Every object of every entry (any state), published and previous
    inline void AddLedger(KeySet& a_keep, const LM::Ledger& a_ledger)
    {
        for (const auto& entry : a_ledger.entries) {
            for (const auto* objects : { &entry.published, &entry.previous }) {
                for (const auto& object : *objects) {
                    AddObject(a_keep, object);
                }
            }
        }
    }

    /// The ledger has at least one object (published or previous) to stand on
    [[nodiscard]] inline bool HasObjects(const LM::Ledger& a_ledger)
    {
        for (const auto& entry : a_ledger.entries) {
            if (!entry.published.empty() || !entry.previous.empty()) {
                return true;
            }
        }
        return false;
    }

    /// a_seeds (every key the keep set already holds, and this session's request keys) kept whether or not they have a line
    /// yet, then each key's line (a_lookup(key):
    /// std::optional<CC::ProgressLine>) followed: its file's and its file deps' key folders kept, and each of those keys'
    /// own lines followed too (an MPS or a NIF names textures, which may name a palette). Each key looked up once
    template <class Lookup>
    void AddRequestClosure(KeySet& a_keep, const std::vector<std::uint64_t>& a_seeds, Lookup&& a_lookup)
    {
        std::vector<std::uint64_t> todo(a_seeds.begin(), a_seeds.end());
        KeySet seen;
        while (!todo.empty()) {
            const auto key = todo.back();
            todo.pop_back();
            a_keep.insert(key);
            if (!seen.insert(key).second) {
                continue;
            }
            const std::optional<CC::ProgressLine> line = a_lookup(key);
            if (!line) {
                continue;
            }
            KeySet found;
            AddKeysOfText(found, line->path);
            for (const auto& dep : line->deps) {
                if (!dep.isAddn) {
                    AddKeysOfText(found, dep.path);
                }
            }
            for (const auto next : found) {
                a_keep.insert(next);
                if (seen.count(next) == 0) {
                    todo.push_back(next);
                }
            }
        }
    }

    /// a_candidate is a direct child of a_root: both absolute, lexically normal, no ".." or "." left, and the child's
    /// name is not empty. The cleanup's last check before it touches a folder
    [[nodiscard]] inline bool ContainedIn(const std::filesystem::path& a_root, const std::filesystem::path& a_candidate)
    {
        const auto root = a_root.lexically_normal();
        const auto candidate = a_candidate.lexically_normal();
        if (!root.is_absolute() || !candidate.is_absolute() || root != a_root || candidate != a_candidate) {
            return false;  // a path that normalising changes ("..", ".", doubled separators) is refused, not repaired
        }
        for (const auto& part : candidate) {
            if (part == ".." || part == ".") {
                return false;
            }
        }
        const auto name = candidate.filename();
        return !name.empty() && candidate.parent_path() == root;
    }

    /// HoMBF_slots.<8 lower-case hex digits>.json: a ledger mirror's file name (SlotLedger.cpp MirrorName), nothing else
    [[nodiscard]] inline bool IsMirrorName(std::string_view a_name)
    {
        constexpr std::string_view kPrefix = "HoMBF_slots.";
        constexpr std::string_view kSuffix = ".json";
        constexpr std::size_t kHashDigits = 8;
        if (a_name.size() != kPrefix.size() + kHashDigits + kSuffix.size() || a_name.substr(0, kPrefix.size()) != kPrefix ||
            a_name.substr(a_name.size() - kSuffix.size()) != kSuffix) {
            return false;
        }
        for (const char c : a_name.substr(kPrefix.size(), kHashDigits)) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                return false;
            }
        }
        return true;
    }

    struct Candidate
    {
        std::uint64_t key = 0;
        std::string name;
        bool inData = false;     // a folder of that name under the Data root
        bool inStaging = false;  // and under the staging root
    };

    struct Candidates
    {
        std::vector<Candidate> remove;  // by key, each once with where its folders are
        std::size_t kept = 0;           // key folders the keep set holds (Data and staging counted apart)
        std::size_t notOurs = 0;        // names that are not 16 lower-case hex digits (never touched)
        bool refusedEmptyKeep = false;  // a_keep was empty: nothing removed
    };

    /// One subtree (meshes or textures): which key folders under its Data root (a_data) and its staging root (a_staging)
    /// the keep set lacks. Not ours -> left; in a_keep -> kept; else a candidate, once per key. An empty a_keep removes nothing
    [[nodiscard]] inline Candidates PlanKeys(const std::vector<std::string>& a_data, const std::vector<std::string>& a_staging,
        const KeySet& a_keep)
    {
        Candidates out;
        if (a_keep.empty()) {
            out.refusedEmptyKeep = true;
            return out;
        }
        std::map<std::uint64_t, Candidate> found;
        for (const auto* names : { &a_data, &a_staging }) {
            for (const auto& name : *names) {
                const auto key = FolderKey(name);
                if (!key) {
                    ++out.notOurs;
                } else if (a_keep.count(*key) != 0) {
                    ++out.kept;
                } else {
                    auto& candidate = found[*key];
                    candidate.key = *key;
                    candidate.name = name;
                    (names == &a_data ? candidate.inData : candidate.inStaging) = true;
                }
            }
        }
        for (auto& [key, candidate] : found) {
            out.remove.push_back(std::move(candidate));
        }
        return out;
    }

    /// a_folders taken from a_left when they fit (true); else nothing taken (false: the key waits for the next session)
    [[nodiscard]] inline bool TakeBudget(std::size_t& a_left, std::size_t a_folders)
    {
        if (a_folders > a_left) {
            return false;
        }
        a_left -= a_folders;
        return true;
    }

    inline constexpr std::uint64_t kFileTimeTicksPerDay = 24ull * 60 * 60 * 10'000'000;  // FILETIME: 100 ns ticks
    // oldCopiesGraceDays' range (Config.cpp). At least a day (review round 2, reviewer 3): a folder the builder or another
    // game is writing right now is never a candidate, whatever else the keep set missed
    inline constexpr std::uint32_t kMinGraceDays = 1;
    inline constexpr std::uint32_t kMaxGraceDays = 3650;

    /// A config value oldCopiesGraceDays may take (kMinGraceDays..kMaxGraceDays); anything else keeps the default
    [[nodiscard]] inline bool GraceDaysValid(std::uint64_t a_days)
    {
        return a_days >= kMinGraceDays && a_days <= kMaxGraceDays;
    }

    /// A key folder whose newest file was written at a_newest (FILETIME ticks) is still in its grace at a_now: within
    /// a_days days, or written after a_now (a clock moved back). 0 days: no grace (the config never gives 0: GraceDaysValid)
    [[nodiscard]] inline bool WithinGrace(std::uint64_t a_newest, std::uint64_t a_now, std::uint32_t a_days)
    {
        if (a_days == 0) {
            return false;
        }
        return a_newest > a_now || a_now - a_newest < static_cast<std::uint64_t>(a_days) * kFileTimeTicksPerDay;
    }
}
