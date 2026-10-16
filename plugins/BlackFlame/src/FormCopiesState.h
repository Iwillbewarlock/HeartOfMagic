#pragma once

// =============================================================================
// FORM COPIES STATE - the copies table FormCopies.cpp fills and FormCopiesRefresh.cpp checks (internal)
// =============================================================================
// Built once on the kDataLoaded thread and never changed afterwards (hooks read it from any thread). Only the two
// FormCopies translation units include this header.

#include "FormCopies.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BlackFlame::CopiesState
{
    struct Entry
    {
        std::string sourceText;  // "plugin|localId" as the map gives it
        RE::FormType type = RE::FormType::None;
        RE::TESForm* source = nullptr;
        RE::TESForm* copy = nullptr;
        bool ok = false;
        bool sourceMissing = false;
        bool copyMissing = false;
        bool sourceAbsent = false;  // H1: left out because the source's own mesh is in no mod or archive
        std::string detail;
    };

    struct Copies
    {
        std::unordered_map<RE::FormID, RE::TESForm*> bySource;
        std::unordered_map<RE::FormID, const RE::TESForm*> byCopy;
        std::vector<Entry> entries;
        CopyCounts counts;
        std::unordered_map<RE::FormID, std::string> muzzles;  // copy -> expected muzzle model (node-stable strings)
        std::unordered_set<RE::FormID> absentSources;          // H1: sources whose own mesh is absent (INFO, not FAIL)
    };

    /// The one table (an inline function's static: one instance across both translation units)
    inline Copies& TheCopies()
    {
        static auto* copies = std::make_unique<Copies>().release();
        return *copies;
    }
}
