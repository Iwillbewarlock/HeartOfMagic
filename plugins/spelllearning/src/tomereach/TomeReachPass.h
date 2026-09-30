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
        std::size_t configFiles = 0;
        std::unordered_map<Key, std::uint32_t> looseFrom;  // byLooseOnly tomes: the plugin that named them
    };

    // `tomes` are the books to look for. Scans every plugin in load order order
    // and every distribution config. A damaged plugin only counts the tomes of
    // itself and its masters as reached (it could refer to no other); a plugin
    // that cannot be opened goes to `unreadable`. `books`, when given, receives
    // every BOOK record read (the last one of a tome is its winning override).
    PassResult FindReachedTomes(const std::vector<PluginFile>& loadOrder, NameTable& names,
        const std::unordered_set<Key>& tomes, const std::vector<std::filesystem::path>& configs,
        std::vector<BookRecord>* books = nullptr);
}
