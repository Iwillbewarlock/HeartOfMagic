#pragma once

#include <cstdint>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "librarian/Librarian.h"

// Reporting for librarian-test: coverage, precision and recall against an
// answer set, and the four axes. Split out of librarian-test.cpp to keep both
// files inside the 600 line limit.

// A spell as both sides of the join name it: the plugin lowercased and the
// form's local id. Nothing else - falling back to the low 12 bits for ESL
// plugins mismatched every time it was tried.
struct JoinKey
{
    std::string plugin;
    std::uint32_t localId = 0;

    bool operator<(const JoinKey& other) const
    {
        if (plugin != other.plugin) {
            return plugin < other.plugin;
        }
        return localId < other.localId;
    }
};


// Indexes a catalog's "spells" object by join key.
std::map<JoinKey, nlohmann::json> IndexCatalog(const nlohmann::json& catalog);

// The tags out of an indexed catalog, for scoring.
std::map<JoinKey, Librarian::TagSet> TagsFromCatalog(const std::map<JoinKey, nlohmann::json>& entries);

// How much of the collection got tagged at all, and which tags were used.
void ReportCoverage(const std::map<JoinKey, nlohmann::json>& entries);

// Precision, recall and per tag hits against the answer set.
void ReportScore(const nlohmann::json& answers,
    const std::map<JoinKey, Librarian::TagSet>& tagged, bool verbose);

// The four axes, which come from scan fields rather than rules and so are
// scored apart from the tags.
void ReportAxes(const nlohmann::json& answers, const std::map<JoinKey, nlohmann::json>& entries);
