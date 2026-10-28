#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <vector>

// =============================================================================
// TREE RECORD - spell_tree_loadorder.json, beside spell_tree.json
// =============================================================================
//
// The record a build's save keeps across game starts (UIManagerAutoBuild.cpp):
//
//   { "version": 2, "loadOrder": "<fingerprint the tree was built under>",
//     "plugins": n, "spells": n, "noticedLoadOrder": "<fingerprint the player was told about>",
//     "seenSpells": ["Plugin.esp|0x000800", ...], "pendingGrafted": n, ...fields of a later version }
//
// seenSpells: the persistentId of every spell of the scan the tree's build used,
// kept or left out (the panel sends it with a build's Apply, SpellTreeSave.send;
// a graft adds what it judged). Absent (a version 1 file, a tree imported since)
// is hasSeen false: no list, which is not an empty one. pendingGrafted: spells a
// graft added that the player has not been told about yet. Fields this version
// does not know are kept in `extra` and written back as they were.
//
// No game types: tools/tree-save-check.cpp compiles it as it is.
namespace TreeRecordFile
{
    inline constexpr int kVersion = 2;
    inline constexpr const char* kSeenKey = "seenSpells";

    struct TreeRecord
    {
        std::string loadOrder;
        std::string noticedLoadOrder;
        std::size_t plugins = 0;   // of the load order the tree was built under
        std::size_t spells = 0;
        std::vector<std::string> seen;
        bool hasSeen = false;
        std::size_t pendingGrafted = 0;
        nlohmann::json extra = nlohmann::json::object();   // fields this version does not know
    };

    namespace Detail
    {
        [[nodiscard]] inline bool IsKnownKey(const std::string& key)
        {
            return key == "version" || key == "loadOrder" || key == "noticedLoadOrder" || key == "plugins" ||
                   key == "spells" || key == kSeenKey || key == "pendingGrafted";
        }

        [[nodiscard]] inline std::string Text(const nlohmann::json& data, const char* key)
        {
            const auto it = data.find(key);
            return it != data.end() && it->is_string() ? it->get<std::string>() : std::string();
        }

        /// A count: a whole number not below 0, else 0
        [[nodiscard]] inline std::size_t Count(const nlohmann::json& data, const char* key)
        {
            const auto it = data.find(key);
            if (it == data.end()) return 0;
            if (it->is_number_unsigned()) return it->get<std::size_t>();
            if (it->is_number_integer() && it->get<long long>() >= 0) return static_cast<std::size_t>(it->get<long long>());
            return 0;
        }

        /// The string ids of a seen list, in its order (anything else in it left out)
        template <class Json>
        [[nodiscard]] std::vector<std::string> Ids(const Json& list)
        {
            std::vector<std::string> ids;
            ids.reserve(list.size());
            for (const auto& id : list) {
                if (id.is_string()) ids.push_back(id.template get<std::string>());
            }
            return ids;
        }
    }

    /// The record out of the file's JSON; a field of the wrong type reads as unset
    [[nodiscard]] inline TreeRecord ParseTreeRecord(const nlohmann::json& data)
    {
        TreeRecord record;
        if (!data.is_object()) return record;
        record.loadOrder = Detail::Text(data, "loadOrder");
        record.noticedLoadOrder = Detail::Text(data, "noticedLoadOrder");
        record.plugins = Detail::Count(data, "plugins");
        record.spells = Detail::Count(data, "spells");
        record.pendingGrafted = Detail::Count(data, "pendingGrafted");
        const auto seen = data.find(kSeenKey);
        if (seen != data.end() && seen->is_array()) {
            record.seen = Detail::Ids(*seen);
            record.hasSeen = true;
        }
        for (const auto& [key, value] : data.items()) {
            if (!Detail::IsKnownKey(key)) record.extra[key] = value;
        }
        return record;
    }

    /// The file's JSON: `extra` as it was, this version's fields over it
    [[nodiscard]] inline nlohmann::json ToJson(const TreeRecord& record)
    {
        nlohmann::json data = record.extra.is_object() ? record.extra : nlohmann::json::object();
        data["version"] = kVersion;
        data["loadOrder"] = record.loadOrder;
        data["plugins"] = record.plugins;
        data["spells"] = record.spells;
        data["noticedLoadOrder"] = record.noticedLoadOrder;
        if (record.hasSeen) data[kSeenKey] = record.seen;
        data["pendingGrafted"] = record.pendingGrafted;
        return data;
    }

    /// The seen list a built tree's payload carries, taken off the tree (the tree file
    /// is written without it); empty when absent. A template so the save keeps the
    /// panel's key order (nlohmann::ordered_json: the school order matters,
    /// TreePrereqEntries.h); nlohmann::json works too.
    template <class Json>
    [[nodiscard]] std::vector<std::string> TakeSeenSpells(Json& tree)
    {
        if (!tree.is_object()) return {};
        const auto it = tree.find(kSeenKey);
        if (it == tree.end()) return {};
        auto seen = it->is_array() ? Detail::Ids(*it) : std::vector<std::string>{};
        tree.erase(it);
        return seen;
    }
}
