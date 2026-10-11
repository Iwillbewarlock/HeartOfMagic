#include "Common.h"
#include "JsonText.h"
#include "PluginNames.h"
#include "SpellScanner.h"

#include <array>
#include <unordered_set>

namespace SpellScanner
{
    // =============================================================================
    // PERSISTENT FORMID FUNCTIONS (Load Order Resilient)
    // =============================================================================

    std::string GetPersistentFormId(RE::FormID formId)
    {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return "";

        uint8_t modIndex = (formId >> 24) & 0xFF;
        uint32_t localFormId = 0;
        const RE::TESFile* plugin = nullptr;

        if (modIndex == 0xFE) {
            // Light plugin (ESL)
            uint16_t lightIndex = (formId >> 12) & 0xFFF;
            plugin = dataHandler->LookupLoadedLightModByIndex(lightIndex);
            localFormId = formId & 0x00000FFF;  // Only 12 bits for light plugins
        } else {
            // Regular plugin
            plugin = dataHandler->LookupLoadedModByIndex(modIndex);
            localFormId = formId & 0x00FFFFFF;  // 24 bits for regular plugins
        }

        if (plugin && plugin->fileName && strlen(plugin->fileName) > 0) {
            // The file name is in the ANSI code page (a Korean or Japanese plugin
            // name): the key is its UTF-8 form, so it can go into JSON. A name that
            // is already UTF-8 (every ASCII one) is the same key as before.
            return PluginNames::MakePersistentId(plugin->fileName, localFormId);
        }

        return "";  // Unknown plugin
    }

    namespace
    {
        using PluginLookup = PluginNames::Lookup<const RE::TESFile*>;

        // The loaded plugin a persistentId names (PluginNames.h): the game's own
        // lookup by raw bytes, then, for a non-ASCII name, the converted names
        const RE::TESFile* FindPlugin(RE::TESDataHandler* dataHandler, PluginLookup& lookup,
                                      const std::string& pluginName)
        {
            return lookup.Find(
                pluginName,
                [dataHandler](const std::string& name) { return dataHandler->LookupModByName(name); },
                [dataHandler](auto&& visit) {
                    for (const auto* file : dataHandler->files) {
                        if (file) visit(file->fileName, file);
                    }
                });
        }

        // ResolvePersistentFormId with the caller's lookup (one per tree pass)
        RE::FormID ResolveWith(const std::string& persistentId, PluginLookup& lookup)
        {
            // Parse "PluginName.esp|0x123456" format
            auto pipePos = persistentId.find('|');
            if (pipePos == std::string::npos || pipePos == 0) {
                logger::trace("SpellScanner: Invalid persistent ID format (no pipe): {}", persistentId);
                return 0;
            }

            std::string pluginName = persistentId.substr(0, pipePos);
            std::string localIdStr = persistentId.substr(pipePos + 1);

            // Parse local FormID
            uint32_t localFormId = 0;
            try {
                if (localIdStr.length() >= 2 && (localIdStr.substr(0, 2) == "0x" || localIdStr.substr(0, 2) == "0X")) {
                    localIdStr = localIdStr.substr(2);
                }
                localFormId = std::stoul(localIdStr, nullptr, 16);
            } catch (const std::exception& e) {
                logger::warn("SpellScanner: Invalid local FormID in persistent ID: {} ({})", persistentId, e.what());
                return 0;
            }

            // Look up plugin by name
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) return 0;

            const RE::TESFile* plugin = FindPlugin(dataHandler, lookup, pluginName);
            if (!plugin) {
                logger::trace("SpellScanner: Plugin not loaded: {}", pluginName);
                return 0;
            }

            // Reconstruct full FormID with current mod index
            if (plugin->IsLight()) {
                // Light plugin: 0xFEXXX + 12-bit local ID
                std::uint32_t lightIndex = plugin->GetPartialIndex();
                return (0xFE000000 | ((lightIndex) << 12) | (localFormId & 0xFFF));
            } else {
                // Regular plugin: mod index + 24-bit local ID
                uint8_t modIndex = plugin->GetCompileIndex();
                return (static_cast<uint32_t>(modIndex) << 24) | (localFormId & 0x00FFFFFF);
            }
        }
    }

    RE::FormID ResolvePersistentFormId(const std::string& persistentId)
    {
        PluginLookup lookup;
        return ResolveWith(persistentId, lookup);
    }

    bool IsFormIdValid(RE::FormID formId)
    {
        if (formId == 0) return false;
        auto* form = RE::TESForm::LookupByID(formId);
        return form != nullptr;
    }

    bool IsFormIdValid(const std::string& formIdStr)
    {
        if (formIdStr.empty()) return false;

        RE::FormID formId = 0;
        try {
            std::string cleanId = formIdStr;
            if (cleanId.length() >= 2 && (cleanId.substr(0, 2) == "0x" || cleanId.substr(0, 2) == "0X")) {
                cleanId = cleanId.substr(2);
            }
            formId = std::stoul(cleanId, nullptr, 16);
        } catch (...) {
            return false;
        }

        return IsFormIdValid(formId);
    }

    namespace
    {
        // What a tree node must be to show: a spell with a name, that the spell gate does
        // not leave out (GateLeavesOut: free or dismiss-only, unless the player included it) and that is not in
        // the voice slot - what a scan lets into a new tree. A saved tree is checked again as it loads: a stale formId can name another
        // plugin's record (an unnamed Constant Effect ability), and a tree built before a
        // rule came in still holds spells the rule leaves out (2026-10-05).
        bool IsPlayerSpell(const std::string& formIdStr, const std::unordered_set<std::string>& gateKept)
        {
            RE::FormID formId = 0;
            try {
                formId = static_cast<RE::FormID>(std::stoul(formIdStr, nullptr, 16));
            } catch (...) {
                return false;
            }
            auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
            if (!spell) return false;
            const char* name = spell->GetFullName();
            return name && *name && !GateLeavesOut(spell, gateKept) && !IsEquippedInVoiceSlot(spell);
        }
    }

    TreeValidationResult ValidateAndFixTree(json& treeData)
    {
        const auto gateKept = ReadGateKept();
        TreeValidationResult result;
        std::set<std::string> missingPluginsSet;
        std::set<std::string> invalidFormIdsSet;
        std::unordered_map<std::string, std::string> formIdRemapping;
        // The formId every kept node has after the pass (kept or resolved)
        std::unordered_set<std::string> finalFormIds;
        std::size_t notPlayerSpells = 0;
        // One per pass: the file list is converted once, not once per node
        PluginLookup pluginLookup;

        if (!treeData.contains("schools")) {
            logger::warn("SpellScanner: Tree has no schools key");
            return result;
        }

        for (auto& [schoolName, schoolData] : treeData["schools"].items()) {
            if (!schoolData.contains("nodes") || !schoolData["nodes"].is_array()) {
                continue;
            }

            auto& nodes = schoolData["nodes"];
            std::vector<size_t> nodesToRemove;

            for (size_t i = 0; i < nodes.size(); ++i) {
                auto& node = nodes[i];
                result.totalNodes++;

                if (!node.contains("formId") || !node["formId"].is_string()) {
                    logger::warn("SpellScanner: Node missing string formId, skipping");
                    nodesToRemove.push_back(i);
                    result.invalidNodes++;
                    continue;
                }

                std::string formIdStr = node["formId"].get<std::string>();
                bool isValid = IsFormIdValid(formIdStr);

                // The persistent ID ("Plugin.esp|0x00080B") names the record whatever the load
                // order, so it wins whenever the node has one. A formId saved before a load order
                // change can still be a valid form - another plugin's record that took the old
                // index (an unnamed Constant Effect ability in place of a spell, 2026-10-05) - so
                // it is not trusted on its own.
                // Only a well-formed one ("Plugin|0x..."): an empty or broken field is no persistent
                // ID, and the node keeps the old rule (its formId if that is valid)
                if (node.contains("persistentId") && node["persistentId"].is_string() &&
                    node["persistentId"].get<std::string>().find('|') != std::string::npos) {
                    std::string persistentId = node["persistentId"].get<std::string>();
                    RE::FormID resolvedId = ResolveWith(persistentId, pluginLookup);

                    if (resolvedId != 0 && IsFormIdValid(resolvedId)) {
                        const std::string resolvedStr = std::format("0x{:08X}", resolvedId);
                        if (resolvedStr != formIdStr) {
                            // Update formId with resolved value, track old->new mapping
                            node["formId"] = resolvedStr;
                            formIdRemapping[formIdStr] = resolvedStr;
                            result.resolvedFromPersistent++;
                            logger::info("SpellScanner: Resolved {} -> {} from persistent ID {}",
                                formIdStr, resolvedStr, persistentId);
                        }
                        isValid = true;
                    } else {
                        // The plugin or the record is gone: the old formId, valid or not, is
                        // not this spell
                        isValid = false;
                        // Extract plugin name from persistent ID for error reporting
                        auto pipePos = persistentId.find('|');
                        if (pipePos != std::string::npos) {
                            missingPluginsSet.insert(persistentId.substr(0, pipePos));
                        }
                    }
                }

                // A node the tree shows is a player's spell (IsPlayerSpell). Anything else leaves
                // the tree as it loads, so the player never meets an unnamed or code-like node
                if (isValid && !IsPlayerSpell(node["formId"].get<std::string>(), gateKept)) {
                    isValid = false;
                    notPlayerSpells++;
                    logger::info("SpellScanner: {} ({}) is not a player's spell now - left out of the tree",
                        node["formId"].get<std::string>(), node.value("name", std::string("?")));
                }

                if (isValid) {
                    result.validNodes++;
                    finalFormIds.insert(node["formId"].get<std::string>());
                } else {
                    nodesToRemove.push_back(i);
                    result.invalidNodes++;
                    invalidFormIdsSet.insert(formIdStr);
                    logger::warn("SpellScanner: Invalid FormID in tree: {}", formIdStr);
                }
            }

            // Remove invalid nodes (reverse order to preserve indices)
            for (auto it = nodesToRemove.rbegin(); it != nodesToRemove.rend(); ++it) {
                nodes.erase(nodes.begin() + *it);
            }

            // Update root if it was invalid
            if (schoolData.contains("root") && schoolData["root"].is_string()) {
                std::string rootId = schoolData["root"].get<std::string>();
                if (invalidFormIdsSet.count(rootId) > 0) {
                    // Find first remaining node as new root
                    if (!nodes.empty() && nodes[0].contains("formId")) {
                        schoolData["root"] = nodes[0]["formId"];
                        logger::info("SpellScanner: Updated {} root to {}", schoolName, JsonText::Dump(nodes[0]["formId"]));
                    }
                }

                // Update root if its formId was remapped via persistent ID resolution
                auto remapIt = formIdRemapping.find(rootId);
                if (remapIt != formIdRemapping.end()) {
                    schoolData["root"] = remapIt->second;
                }
            }
        }

        // A formId some node holds after the pass is that node's, even when it is also the
        // old formId of another node that moved or went (two plugins swapped light indices:
        // Arclight's spell left 0xFE906838 and Bloodmoon's took it, 2026-10-05). A link to
        // it stays; mapping it to the other node's new formId would hand the kept node's
        // links to the moved one. The moved node loses links saved under the shared old
        // formId - they cannot be told apart - and the panel's orphan repair reattaches it.
        for (const auto& id : finalFormIds) {
            formIdRemapping.erase(id);
            invalidFormIdsSet.erase(id);
        }

        // The links, after every school is checked: a cross-school bridge puts a spell of
        // another school in a node's optional list, so a formId resolved in a later school
        // must reach it too
        for (auto& [schoolName, schoolData] : treeData["schools"].items()) {
            if (!schoolData.contains("nodes") || !schoolData["nodes"].is_array()) continue;
            auto& nodes = schoolData["nodes"];
            // Every link list of a node: the tree's own, and the required / optional lists C++
            // reads (left out before 2026-10-05, so a load order change kept old formIds there)
            static constexpr std::array<const char*, 4> kLinkLists = {
                "children", "prerequisites", "hardPrereqs", "softPrereqs"
            };

            // Clean up links that reference removed nodes. The saved links all hold old
            // formIds, so this and the rewrite below look each one up once: a new formId that
            // equals another node's old one is never mapped twice
            for (auto& node : nodes) {
                for (const char* key : kLinkLists) {
                    if (!node.contains(key) || !node[key].is_array()) continue;
                    auto& links = node[key];
                    links.erase(
                        std::remove_if(links.begin(), links.end(),
                            [&invalidFormIdsSet](const json& link) {
                                return link.is_string() && invalidFormIdsSet.count(link.get<std::string>()) > 0;
                            }),
                        links.end());
                }
            }

            // Rewrite links that referenced old formIds of resolved nodes
            if (!formIdRemapping.empty()) {
                for (auto& node : nodes) {
                    for (const char* key : kLinkLists) {
                        if (!node.contains(key) || !node[key].is_array()) continue;
                        for (auto& link : node[key]) {
                            if (!link.is_string()) continue;
                            auto it = formIdRemapping.find(link.get<std::string>());
                            if (it != formIdRemapping.end()) link = it->second;
                        }
                    }
                }
            }

            // Locks ({nodeId, ...}, Pre Req Master) name the node they wait on: the panel
            // reads nodeId as it loads (treeParser, PrereqEdit.repair, prereqMaster), so a
            // lock to a removed node goes and one to a moved node takes its new formId
            for (auto& node : nodes) {
                if (!node.contains("locks") || !node["locks"].is_array()) continue;
                auto& locks = node["locks"];
                locks.erase(
                    std::remove_if(locks.begin(), locks.end(),
                        [&invalidFormIdsSet](const json& lock) {
                            return lock.is_object() && lock.contains("nodeId") && lock["nodeId"].is_string() &&
                                   invalidFormIdsSet.count(lock["nodeId"].get<std::string>()) > 0;
                        }),
                    locks.end());
                for (auto& lock : locks) {
                    if (!lock.is_object() || !lock.contains("nodeId") || !lock["nodeId"].is_string()) continue;
                    auto it = formIdRemapping.find(lock["nodeId"].get<std::string>());
                    if (it != formIdRemapping.end()) lock["nodeId"] = it->second;
                }
            }
        }

        // The cross school bridges name spells of any school, so they are fixed after
        // every school is: an end whose spell moved takes its new formId, a bridge with
        // an end that was removed goes. Left as they were, a load order change turned
        // them into links to formIds the tree no longer has (the spell card listed them
        // by raw formId, and a click went nowhere).
        if (treeData.contains("bridges") && treeData["bridges"].is_array()) {
            auto& bridges = treeData["bridges"];
            const auto staleEnd = [&invalidFormIdsSet](const json& bridge) {
                for (const char* end : { "from", "to" }) {
                    if (!bridge.contains(end) || !bridge[end].is_string() ||
                        invalidFormIdsSet.count(bridge[end].get<std::string>()) > 0) {
                        return true;
                    }
                }
                return false;
            };
            const auto before = bridges.size();
            bridges.erase(std::remove_if(bridges.begin(), bridges.end(), staleEnd), bridges.end());
            for (auto& bridge : bridges) {
                for (const char* end : { "from", "to" }) {
                    auto it = formIdRemapping.find(bridge[end].get<std::string>());
                    if (it != formIdRemapping.end()) {
                        bridge[end] = it->second;
                    }
                }
            }
            if (bridges.size() != before) {
                logger::info("SpellScanner: Dropped {} bridges to removed spells", before - bridges.size());
            }
        }

        // Convert sets to vectors
        result.missingPlugins.assign(missingPluginsSet.begin(), missingPluginsSet.end());
        result.invalidFormIds.assign(invalidFormIdsSet.begin(), invalidFormIdsSet.end());

        logger::info("SpellScanner: Tree validation complete - {}/{} valid, {} resolved from persistent, {} invalid "
            "({} not a player's spell), {} references remapped",
            result.validNodes, result.totalNodes, result.resolvedFromPersistent, result.invalidNodes, notPlayerSpells,
            formIdRemapping.size());

        return result;
    }
}
