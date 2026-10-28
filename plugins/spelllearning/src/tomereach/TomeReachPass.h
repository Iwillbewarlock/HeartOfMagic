#pragma once

// =============================================================================
// TOME REACH - ONE PASS OVER THE LOAD ORDER
// =============================================================================
//
// Which of the given tomes something hands out, from the plugin files and the
// distribution configs. Pure C++, shared by the plugin (TomeReach.cpp, on a
// worker thread) and tools/tome-reach-test.

#include "tomereach/TomeReachRecords.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TomeReach
{
    struct PassResult
    {
        std::unordered_set<Key> reached;         // tomes something hands out
        std::vector<std::string> unreadable;     // plugins that could not be opened: the pass cannot be trusted
        std::vector<std::string> damaged;        // plugins read only in part (see below)
        std::size_t byRecord = 0;                // reached through a record field that holds a form
        std::size_t byLooseOnly = 0;             // reached only through four bytes of a script property or alias
        std::size_t byConfig = 0;                // reached only through a distribution config
        std::size_t byDamaged = 0;               // counted as reached: a damaged plugin could refer to them
        std::size_t byDebug = 0;                 // referred to, but a debug tome (editor id): not reached
        std::size_t configFiles = 0;
        std::unordered_map<Key, std::uint32_t> looseFrom;  // byLooseOnly tomes: the plugin that named them
        // Reached tomes only a quest hands out: named by a quest's script or alias
        // (Evidence::foundQuest) and by nothing else (no other record's field or
        // script, no distribution config, no damaged plugin that could name them)
        std::unordered_set<Key> questOnly;
        // Each questOnly tome: the plugins (NameTable ids) whose quest records name it, in
        // load order (the quest-only log lines list them)
        std::unordered_map<Key, std::vector<std::uint32_t>> questFrom;
    };

    // A spell's tomes against a pass (TomeReach.cpp marks the spells by these).
    // Unreachable: nothing hands out any of them.
    inline bool NoTomeReached(const std::vector<Key>& tomes, const PassResult& pass)
    {
        for (const Key tome : tomes) {
            if (pass.reached.contains(tome)) return false;
        }
        return true;
    }

    // Quest-only: some tome of it is reached, and every reached one only a quest hands
    // out (a tome nothing hands out does not count against it)
    inline bool OnlyQuestsReach(const std::vector<Key>& tomes, const PassResult& pass)
    {
        bool reached = false;
        for (const Key tome : tomes) {
            if (!pass.reached.contains(tome)) continue;
            if (!pass.questOnly.contains(tome)) return false;
            reached = true;
        }
        return reached;
    }

    // `tomes` are the books to look for. Scans every plugin in load order order
    // and every distribution config. A damaged plugin only counts the tomes of
    // itself and its masters as reached (it could refer to no other); a plugin
    // that cannot be opened goes to `unreadable`. `books`, when given, receives
    // every BOOK record read (the last one of a tome is its winning override).
    PassResult FindReachedTomes(const std::vector<PluginFile>& loadOrder, NameTable& names,
        const std::unordered_set<Key>& tomes, const std::vector<std::filesystem::path>& configs,
        std::vector<BookRecord>* books = nullptr);
}
