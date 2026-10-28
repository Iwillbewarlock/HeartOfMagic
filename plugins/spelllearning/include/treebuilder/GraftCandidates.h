#pragma once

#include "AsciiText.h"
#include "FormIdText.h"
#include "TreeRemap.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

// =============================================================================
// GRAFT CANDIDATES - which scan spells a graft looks at, and which the tree takes
// (pure rules, no game types: tools/graft-check.cpp runs them offline)
// =============================================================================
//
// NewSpells: the spells of a scan the tree has never met - not in the seen list
// (the build's scan and every earlier graft's candidates), not in the tree, with a
// persistentId, and no node of their duplicate group (the design, section 1).
//
// IsTreeCandidate: the panel's rule for the spells a build takes, ported line by
// line so a graft takes what a rebuild would - proceduralTreeBuilder.js
// isTaughtByTome (isTreeCandidate, isTomeCandidate), filterBlacklistedSpells and
// filterWhitelistedSpells. Change one, change the other.
namespace TreeGraft
{
    /**
     * The player's choices, as the panel keeps them in config.json.
     *
     * Every key is the text the panel compares, built the panel's way:
     * - blacklistKeys: per settings.spellBlacklist entry with plugin and localFormId,
     *   foldPluginName(plugin) + ":" + localFormId in lower case ("mymod.esp:00080b");
     *   an older entry with only a formId goes in as that formId text exactly as saved
     *   ("0x0500080B"; the panel compares it with the spell's formId as is). A formId has
     *   no ':', so the two kinds never collide.
     * - gateKept: settings.gateReviewed as it is (GateReview.key: "mymod.esp:00080b").
     * - pluginsOff: foldPluginName(plugin) of each settings.pluginWhitelist entry with
     *   enabled false.
     * foldPluginName lowers A-Z only (AsciiText::ToLower); every other byte stays.
     */
    struct PlayerRules
    {
        std::unordered_set<std::string> blacklistKeys, gateKept, pluginsOff;
    };

    namespace Detail
    {
        /// What JavaScript's `if (value)` says: absent, null, false, 0, "" are false
        [[nodiscard]] inline bool Truthy(const nlohmann::json& spell, const char* key)
        {
            const auto it = spell.find(key);
            if (it == spell.end() || it->is_null()) return false;
            if (it->is_boolean()) return it->get<bool>();
            if (it->is_string()) return !it->get_ref<const std::string&>().empty();
            if (it->is_number()) return it->get<double>() != 0.0;
            return true;   // an object or array
        }

        /// The field is the boolean `value` (JavaScript's `=== value`)
        [[nodiscard]] inline bool Is(const nlohmann::json& spell, const char* key, bool value)
        {
            const auto it = spell.find(key);
            return it != spell.end() && it->is_boolean() && it->get<bool>() == value;
        }

        [[nodiscard]] inline std::string Text(const nlohmann::json& spell, const char* key)
        {
            const auto it = spell.find(key);
            return (it != spell.end() && it->is_string()) ? it->get<std::string>() : std::string();
        }

        /// uiHelpers.js getLocalFormId: "0x" off, lower case, padded to 8; an ESL (fe..) keeps
        /// its last 3 hex digits, any other its last 6
        [[nodiscard]] inline std::string LocalFormId(std::string_view formId)
        {
            if (formId.empty()) return {};
            if (formId.size() >= 2 && formId[0] == '0' && (formId[1] == 'x' || formId[1] == 'X')) formId.remove_prefix(2);
            std::string hex = AsciiText::ToLower(formId);
            if (hex.size() < FormIdText::kMaxHexDigits) hex.insert(0, FormIdText::kMaxHexDigits - hex.size(), '0');
            constexpr std::size_t kEslLocalStart = 5;   // "fe" + 3 digits of the light plugin's index
            constexpr std::size_t kLocalStart = 2;      // the plugin's index byte
            return hex.starts_with("fe") ? hex.substr(kEslLocalStart) : hex.substr(kLocalStart);
        }

        /// proceduralTreeBuilder.js spellPluginName: the persistentId's plugin, else plugin, else source
        [[nodiscard]] inline std::string SpellPluginName(const nlohmann::json& spell)
        {
            const std::string persistentId = Text(spell, "persistentId");
            if (const auto pipe = persistentId.find('|'); pipe != std::string::npos) return persistentId.substr(0, pipe);
            if (Truthy(spell, "plugin")) return Text(spell, "plugin");
            return Text(spell, "source");
        }

        /// GateReview.key: spell.plugin (not the persistentId's) folded, ':', the local id
        [[nodiscard]] inline std::string GateKey(const nlohmann::json& spell)
        {
            return AsciiText::ToLower(Text(spell, "plugin")) + ":" + LocalFormId(Text(spell, "formId"));
        }

        /// proceduralTreeBuilder.js isTomeCandidate
        [[nodiscard]] inline bool IsTomeCandidate(const nlohmann::json& spell)
        {
            return !Is(spell, "taughtByTome", false) && !Is(spell, "tomeUnreachable", true) &&
                   !Is(spell, "voiceSlot", true) && !Truthy(spell, "duplicateOf");
        }

        /// filterBlacklistedSpells: by folded plugin and local id, else by the formId text
        [[nodiscard]] inline bool Blacklisted(const nlohmann::json& spell, const PlayerRules& rules)
        {
            if (rules.blacklistKeys.empty()) return false;
            const std::string formId = Truthy(spell, "formId") ? Text(spell, "formId") : Text(spell, "id");
            const std::string plugin = SpellPluginName(spell);
            if (!plugin.empty() && !formId.empty() &&
                rules.blacklistKeys.contains(AsciiText::ToLower(plugin) + ":" + LocalFormId(formId))) {
                return true;
            }
            return rules.blacklistKeys.contains(formId);
        }

        /// filterWhitelistedSpells: a spell whose plugin cannot be told stays in.
        /// Known limit: the panel also matches an entry an older panel lowered with
        /// toLowerCase (a Cyrillic or accented name); A-Z folding cannot, so such an entry
        /// keeps its plugin's spells in here until the plugin list renames it.
        [[nodiscard]] inline bool PluginOff(const nlohmann::json& spell, const PlayerRules& rules)
        {
            const std::string plugin = SpellPluginName(spell);
            return !plugin.empty() && rules.pluginsOff.contains(AsciiText::ToLower(plugin));
        }

        /// The one form tree and scan formIds are compared in: "0x%08X"; an edit mode
        /// copy's node ("<formId>_dup_<n>") stands for its spell
        [[nodiscard]] inline std::string SpellIdOf(std::string_view nodeOrSpellId)
        {
            const auto id = FormIdText::ParseSpellOf(nodeOrSpellId);
            return id ? FormIdText::Format(*id) : std::string(nodeOrSpellId);
        }

        /// A persistentId that names a record ("Plugin.esp|0x..."), A-Z folded; empty otherwise
        [[nodiscard]] inline std::string PersistentKey(const nlohmann::json& entry)
        {
            const std::string persistentId = Text(entry, "persistentId");
            return persistentId.find('|') != std::string::npos ? AsciiText::ToLower(persistentId) : std::string();
        }
    }

    /// The panel's candidate rule: isTaughtByTome, the blacklist and the plugin whitelist.
    /// gateKept brings back a spell the gate ("out") or the quest-only rule leaves out,
    /// nothing else. A scan from before the gate has only freeSpell.
    [[nodiscard]] inline bool IsTreeCandidate(const nlohmann::json& spell, const PlayerRules& rules)
    {
        if (!spell.is_object()) return false;
        if (!Detail::IsTomeCandidate(spell)) return false;
        const bool kept = rules.gateKept.contains(Detail::GateKey(spell));
        if (Detail::Is(spell, "questOnly", true) && !kept) return false;
        const auto gate = spell.find("gate");
        const bool hasGate = Detail::Truthy(spell, "gate");
        if (hasGate ? (gate->is_object() && Detail::Text(*gate, "band") == "out" && !kept) : Detail::Is(spell, "freeSpell", true)) {
            return false;
        }
        return !Detail::Blacklisted(spell, rules) && !Detail::PluginOff(spell, rules);
    }

    /**
     * The scan spells a graft judges: not seen (`seen` holds persistentIds, compared
     * A-Z folded), with a persistentId, not in the tree, and no node of their
     * duplicate group (the kept spell and the spells marked duplicateOf it).
     *
     * In the tree: a node with a persistentId the scan has is that spell; a node
     * without one (a tree from before 2026-10), or whose persistentId the scan lacks
     * (a renamed plugin, which the editor id rematch may still find), holds the spell
     * of its formId (an edit mode copy's "<formId>_dup_<n>" its spell's). So a node
     * whose saved formId went stale in a load order shift does not hide the new
     * spell that took that formId.
     *
     * The pointers point into `scanSpells`, which must outlive them. TreeJson is
     * nlohmann::json or nlohmann::ordered_json (the file read in its key order).
     */
    template <class TreeJson>
    [[nodiscard]] std::vector<const nlohmann::json*> NewSpells(const nlohmann::json& scanSpells, const TreeJson& tree,
        const std::unordered_set<std::string>& seen)
    {
        std::vector<const nlohmann::json*> out;
        if (!scanSpells.is_array()) return out;

        std::unordered_map<std::string, const nlohmann::json*> scanByPersistent;
        for (const auto& spell : scanSpells) {
            if (!spell.is_object()) continue;
            if (const auto key = Detail::PersistentKey(spell); !key.empty()) scanByPersistent.emplace(key, &spell);
        }

        // What the tree holds, by formId and by persistentId
        std::unordered_set<std::string> treeFormIds, treePersistent;
        const auto schools = tree.find("schools");
        if (schools != tree.end() && schools->is_object()) {
            for (const auto& [name, school] : schools->items()) {
                const auto nodes = school.find("nodes");
                if (!school.is_object() || nodes == school.end() || !nodes->is_array()) continue;
                for (const auto& node : *nodes) {
                    if (!node.is_object()) continue;
                    const std::string persistent = node.contains("persistentId") && node["persistentId"].is_string()
                                                       ? AsciiText::ToLower(node["persistentId"].template get<std::string>())
                                                       : std::string();
                    if (!persistent.empty()) treePersistent.insert(persistent);
                    const auto found = persistent.find('|') != std::string::npos ? scanByPersistent.find(persistent)
                                                                                 : scanByPersistent.end();
                    if (found != scanByPersistent.end()) {
                        treeFormIds.insert(Detail::SpellIdOf(Detail::Text(*found->second, "formId")));
                    } else if (node.contains("formId") && node["formId"].is_string()) {
                        treeFormIds.insert(Detail::SpellIdOf(node["formId"].template get<std::string>()));
                    }
                }
            }
        }
        const auto inTree = [&](const nlohmann::json& spell) {
            const auto key = Detail::PersistentKey(spell);
            return (!key.empty() && treePersistent.contains(key)) ||
                   treeFormIds.contains(Detail::SpellIdOf(Detail::Text(spell, "formId")));
        };

        // Duplicate groups (SpellScannerDuplicates.cpp): the kept spell's formId -> a member has a node
        const auto groupOf = [](const nlohmann::json& spell) {
            return Detail::SpellIdOf(Detail::Truthy(spell, "duplicateOf") ? Detail::Text(spell, "duplicateOf")
                                                                           : Detail::Text(spell, "formId"));
        };
        std::unordered_set<std::string> groupsWithNode;
        for (const auto& spell : scanSpells) {
            if (spell.is_object() && inTree(spell)) groupsWithNode.insert(groupOf(spell));
        }

        std::unordered_set<std::string> seenFolded;
        for (const auto& id : seen) seenFolded.insert(AsciiText::ToLower(id));

        std::unordered_set<std::string> taken;
        for (const auto& spell : scanSpells) {
            if (!spell.is_object()) continue;
            const auto key = Detail::PersistentKey(spell);
            if (key.empty() || seenFolded.contains(key) || inTree(spell)) continue;
            const auto group = groupOf(spell);
            if (groupsWithNode.contains(group) || treeFormIds.contains(group)) continue;
            if (!taken.insert(key).second) continue;   // the same record twice in one scan
            out.push_back(&spell);
        }
        return out;
    }
}
