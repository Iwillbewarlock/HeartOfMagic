#pragma once

#include "AsciiText.h"
#include "FormIdText.h"

#include <cstddef>
#include <string>
#include <string_view>
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

        /// A string field, empty when absent or not a string (Json: json or ordered_json)
        template <class Json>
        [[nodiscard]] std::string Text(const Json& object, const char* key)
        {
            const auto it = object.find(key);
            return (it != object.end() && it->is_string()) ? it->template get<std::string>() : std::string();
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

        /// The lower case of one code point as JavaScript's toLowerCase gives it, for the
        /// scripts plugin names are written in: Latin-1, Latin Extended-A, Greek, Cyrillic.
        /// Any other code point (Hangul, CJK, kana have no case) stays. Not covered: the
        /// context rule that makes a word-final capital sigma "ς", and the rarer Cyrillic
        /// and Latin blocks (U+0460 on, U+0180 on). Every result is below U+0800.
        [[nodiscard]] inline std::u32string_view LowerOf(char32_t c, char32_t& buffer) noexcept
        {
            static constexpr char32_t kDottedI[] = { U'i', 0x0307 };   // "İ" lowers to "i" + combining dot
            const auto one = [&buffer](char32_t lower) {
                buffer = lower;
                return std::u32string_view(&buffer, 1);
            };
            const bool even = (c % 2) == 0;
            if (c >= U'A' && c <= U'Z') return one(c + 0x20);
            if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return one(c + 0x20);
            if (c == 0x130) return { kDottedI, 2 };
            if (even && ((c >= 0x100 && c <= 0x12F) || (c >= 0x132 && c <= 0x137) || (c >= 0x14A && c <= 0x177))) {
                return one(c + 1);
            }
            if (!even && ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))) return one(c + 1);
            if (c == 0x178) return one(0xFF);
            if (c == 0x386) return one(0x3AC);
            if (c >= 0x388 && c <= 0x38A) return one(c + 0x25);
            if (c == 0x38C) return one(0x3CC);
            if (c == 0x38E || c == 0x38F) return one(c + 0x3F);
            if ((c >= 0x391 && c <= 0x3A1) || (c >= 0x3A3 && c <= 0x3AB)) return one(c + 0x20);
            if (c >= 0x400 && c <= 0x40F) return one(c + 0x50);
            if (c >= 0x410 && c <= 0x42F) return one(c + 0x20);
            return one(c);
        }

        /// JavaScript's toLowerCase on UTF-8 text, as far as LowerOf goes; longer sequences
        /// (no case in LowerOf's range) and bytes that are not well-formed UTF-8 stay as they are.
        /// Where it differs from JavaScript: the code points LowerOf leaves out (U+0460 and up in
        /// Cyrillic, U+0180 and up in Latin) and every three-byte one (U+0800 on: Latin Extended
        /// Additional, Greek Extended...) stay, which JavaScript lowers (U+1E9E to U+00DF,
        /// U+0460 to U+0461). Only the legacy whitelist lookup reads it (PluginIsOff: a key an
        /// older panel lowered with toLowerCase), so a plugin named with such a capital is not
        /// turned off by an entry saved that way - the A-Z folded key, which panels since save, still is.
        [[nodiscard]] inline std::string JsLowerCase(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (std::size_t i = 0; i < text.size();) {
                const auto lead = static_cast<unsigned char>(text[i]);
                const bool twoBytes = lead >= 0xC2 && lead <= 0xDF && i + 1 < text.size() &&
                                      (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80;
                if (lead >= 0x80 && !twoBytes) {
                    out += text[i++];
                    continue;
                }
                const char32_t c = lead < 0x80 ? static_cast<char32_t>(lead)
                                               : static_cast<char32_t>(((lead & 0x1F) << 6) |
                                                                       (static_cast<unsigned char>(text[i + 1]) & 0x3F));
                i += lead < 0x80 ? 1 : 2;
                char32_t buffer = 0;
                for (const char32_t lower : LowerOf(c, buffer)) {
                    if (lower < 0x80) {
                        out += static_cast<char>(lower);
                    } else {
                        out += static_cast<char>(0xC0 | (lower >> 6));
                        out += static_cast<char>(0x80 | (lower & 0x3F));
                    }
                }
            }
            return out;
        }

        /// The one form tree and scan formIds are compared in: "0x%08X"; an edit mode
        /// copy's node ("<formId>_dup_<n>") stands for its spell
        [[nodiscard]] inline std::string SpellIdOf(std::string_view nodeOrSpellId)
        {
            const auto id = FormIdText::ParseSpellOf(nodeOrSpellId);
            return id ? FormIdText::Format(*id) : std::string(nodeOrSpellId);
        }

        /// A persistentId that names a record ("Plugin.esp|0x..."), A-Z folded; empty otherwise
        template <class Json>
        [[nodiscard]] std::string PersistentKey(const Json& entry)
        {
            const std::string persistentId = Text(entry, "persistentId");
            return persistentId.find('|') != std::string::npos ? AsciiText::ToLower(persistentId) : std::string();
        }

        /// What a tree holds (node identity, Ruling 11: see NewSpells): by persistentId, and
        /// by formId for the nodes without one
        struct TreeHolds
        {
            std::unordered_set<std::string> formIds, persistent;

            template <class TreeJson>
            explicit TreeHolds(const TreeJson& tree)
            {
                const auto schools = tree.find("schools");
                if (schools == tree.end() || !schools->is_object()) return;
                for (const auto& [name, school] : schools->items()) {
                    const auto nodes = school.find("nodes");
                    if (!school.is_object() || nodes == school.end() || !nodes->is_array()) continue;
                    for (const auto& node : *nodes) {
                        if (!node.is_object()) continue;
                        if (const auto key = PersistentKey(node); !key.empty()) {
                            persistent.insert(key);
                        } else if (node.contains("formId") && node["formId"].is_string()) {
                            formIds.insert(SpellIdOf(node["formId"].template get<std::string>()));
                        }
                    }
                }
            }

            [[nodiscard]] bool Holds(const nlohmann::json& spell) const
            {
                const auto key = PersistentKey(spell);
                return (!key.empty() && persistent.contains(key)) || formIds.contains(SpellIdOf(Text(spell, "formId")));
            }
        };
    }

    /// A plugin the whitelist turns off (`off`: PluginsOffOf), by both of the panel's lookups
    /// (filterWhitelistedSpells): the name folded A-Z, and lowered with toLowerCase (an entry
    /// an older panel saved that way, "заклинания.esp", turns "Заклинания.esp" off). No plugin
    /// name: not off. The graft and Spell Research's filter (SpellResearchPlayerFilter.h) both ask this
    [[nodiscard]] inline bool PluginIsOff(std::string_view plugin, const std::unordered_set<std::string>& off)
    {
        return !plugin.empty() && (off.contains(AsciiText::ToLower(plugin)) || off.contains(Detail::JsLowerCase(plugin)));
    }

    /// settings.pluginWhitelist's plugins turned off (enabled false), A-Z folded - the one
    /// reader of that list (Spell Research's filter reads it too, SpellResearchPlayerFilter.h)
    [[nodiscard]] inline std::unordered_set<std::string> PluginsOffOf(const nlohmann::json& config)
    {
        std::unordered_set<std::string> off;
        if (!config.is_object()) return off;
        const auto list = config.find("pluginWhitelist");
        if (list == config.end() || !list->is_array()) return off;
        for (const auto& entry : *list) {
            if (!entry.is_object()) continue;
            const auto enabled = entry.find("enabled");
            if (enabled != entry.end() && enabled->is_boolean() && !enabled->get<bool>()) {
                off.insert(AsciiText::ToLower(Detail::Text(entry, "plugin")));
            }
        }
        return off;
    }

    /// PlayerRules out of config.json's settings, in the keys described at PlayerRules.
    /// Only string fields count (the panel saves strings); anything else in a list is passed over
    [[nodiscard]] inline PlayerRules PlayerRulesOf(const nlohmann::json& config)
    {
        PlayerRules rules;
        if (!config.is_object()) return rules;
        // filterBlacklistedSpells: `entry.plugin && entry.localFormId` makes a key, else `entry.formId` as it is
        if (const auto list = config.find("spellBlacklist"); list != config.end() && list->is_array()) {
            for (const auto& entry : *list) {
                if (!entry.is_object()) continue;
                const auto plugin = Detail::Text(entry, "plugin");
                const auto local = Detail::Text(entry, "localFormId");
                if (!plugin.empty() && !local.empty()) {
                    rules.blacklistKeys.insert(AsciiText::ToLower(plugin) + ":" + AsciiText::ToLower(local));
                } else if (auto formId = Detail::Text(entry, "formId"); !formId.empty()) {
                    rules.blacklistKeys.insert(std::move(formId));
                }
            }
        }
        if (const auto list = config.find("gateReviewed"); list != config.end() && list->is_array()) {
            for (const auto& key : *list) {
                if (key.is_string()) rules.gateKept.insert(key.get<std::string>());
            }
        }
        rules.pluginsOff = PluginsOffOf(config);
        return rules;
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
        return !Detail::Blacklisted(spell, rules) && !PluginIsOff(Detail::SpellPluginName(spell), rules.pluginsOff);
    }

    /**
     * The scan spells a graft judges: not seen (`seen` holds persistentIds, compared
     * A-Z folded), with a persistentId, not in the tree, and no node of their
     * duplicate group (the kept spell and the spells marked duplicateOf it).
     *
     * In the tree (node identity, Ruling 11): a node with a persistentId is that
     * record, matched with the scan's persistentId; only a node without one (a tree
     * from before 2026-10) is matched by its formId (an edit mode copy's
     * "<formId>_dup_<n>" by its spell's). So neither a node whose saved formId went
     * stale in a load order shift nor a removed plugin's node hides the new spell
     * that now has that formId (Graft defers such a spell, TreeGraft.h).
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

        const Detail::TreeHolds holds(tree);
        const auto inTree = [&holds](const nlohmann::json& spell) { return holds.Holds(spell); };

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
            if (groupsWithNode.contains(group) || holds.formIds.contains(group)) continue;
            if (!taken.insert(key).second) continue;   // the same record twice in one scan
            out.push_back(&spell);
        }
        return out;
    }

    /// The persistentIds of the scan's spells the tree holds (as NewSpells counts it) that
    /// `seen` lacks (A-Z folded), each once: nodes an edit save brought back after a stale
    /// save took their spells off the list (Ruling 23). Seen again, so a node of theirs
    /// deleted later is not grafted again
    template <class TreeJson>
    [[nodiscard]] std::vector<std::string> HeldUnseen(const nlohmann::json& scanSpells, const TreeJson& tree,
        const std::unordered_set<std::string>& seen)
    {
        std::vector<std::string> out;
        if (!scanSpells.is_array()) return out;
        const Detail::TreeHolds holds(tree);
        std::unordered_set<std::string> folded;
        for (const auto& id : seen) folded.insert(AsciiText::ToLower(id));
        for (const auto& spell : scanSpells) {
            if (!spell.is_object()) continue;
            const auto key = Detail::PersistentKey(spell);
            if (key.empty() || !holds.Holds(spell) || !folded.insert(key).second) continue;
            out.push_back(Detail::Text(spell, "persistentId"));
        }
        return out;
    }
}
