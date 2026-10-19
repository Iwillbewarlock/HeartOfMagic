#include "Common.h"
#include "FormIdText.h"
#include "FormIdBits.h"
#include "PluginNames.h"
#include "SpellScanner.h"
#include "TreeRemap.h"

#include <unordered_set>

namespace SpellScanner
{
    // =============================================================================
    // PERSISTENT FORMID FUNCTIONS (Load Order Resilient)
    // =============================================================================

    std::string GetPersistentFormId(RE::FormID formId)
    {
        using namespace FormIdBits;
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return "";

        const auto modIndex = static_cast<std::uint8_t>(formId >> kFileIndexShift);
        uint32_t localFormId = 0;
        const RE::TESFile* plugin = nullptr;

        if (modIndex == kLightIndex) {
            const auto lightIndex = static_cast<std::uint16_t>((formId >> kLightFileShift) & kLightFileMask);
            plugin = dataHandler->LookupLoadedLightModByIndex(lightIndex);
            localFormId = formId & kLightLocalMask;
        } else {
            plugin = dataHandler->LookupLoadedModByIndex(modIndex);
            localFormId = formId & kFullLocalMask;
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

        // A file the game read but did not load (unchecked in the load order) keeps this
        // compile index; CommonLib's TESDataHandler::LookupFormID refuses it the same way
        constexpr std::uint8_t kNotLoadedIndex = 0xFF;

        // Why a persistentId gave no formId (the tree load logs it)
        enum class ResolveFailure
        {
            kNone,
            kBadFormat,
            kPluginNotLoaded,
            kLocalIdTooBig,
        };

        const RE::TESFile* IfLoaded(const RE::TESFile* file)
        {
            return file && file->GetCompileIndex() != kNotLoadedIndex ? file : nullptr;
        }

        // The loaded plugin a persistentId names (PluginNames.h): the game's own
        // lookup by raw bytes, then, for a non-ASCII name, the converted names.
        // TESDataHandler::files holds inactive plugins too, so both ask IfLoaded
        const RE::TESFile* FindPlugin(RE::TESDataHandler* dataHandler, PluginLookup& lookup,
                                      const std::string& pluginName)
        {
            return lookup.Find(
                pluginName,
                [dataHandler](const std::string& name) { return IfLoaded(dataHandler->LookupModByName(name)); },
                [dataHandler](auto&& visit) {
                    for (const auto* file : dataHandler->files) {
                        if (IfLoaded(file)) visit(file->fileName, file);
                    }
                });
        }

        // ResolvePersistentFormId with the caller's lookup (one per tree pass). Logs at trace:
        // the librarian asks for many ids of plugins the player does not have
        RE::FormID ResolveWith(const std::string& persistentId, PluginLookup& lookup, ResolveFailure& failure)
        {
            using namespace FormIdBits;
            failure = ResolveFailure::kBadFormat;
            // Parse "PluginName.esp|0x123456" format
            auto pipePos = persistentId.find('|');
            if (pipePos == std::string::npos || pipePos == 0) {
                logger::trace("SpellScanner: Invalid persistent ID format (no pipe): {}", persistentId);
                return 0;
            }

            std::string pluginName = persistentId.substr(0, pipePos);
            std::string localIdStr = persistentId.substr(pipePos + 1);

            const auto parsedLocal = FormIdText::Parse(localIdStr);
            if (!parsedLocal) {
                logger::warn("SpellScanner: Invalid local FormID in persistent ID: {}", persistentId);
                return 0;
            }
            const std::uint32_t localFormId = *parsedLocal;

            // Look up plugin by name
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) return 0;

            const RE::TESFile* plugin = FindPlugin(dataHandler, lookup, pluginName);
            if (!plugin) {
                failure = ResolveFailure::kPluginNotLoaded;
                logger::trace("SpellScanner: Plugin not loaded: {}", pluginName);
                return 0;
            }

            // A local id the plugin has no room for (saved while it was a full plugin, then
            // ESL-flagged without compacting) is no record of it: cut to fit, it would name
            // another one
            const std::uint32_t localMask = plugin->IsLight() ? kLightLocalMask : kFullLocalMask;
            if (localFormId > localMask) {
                failure = ResolveFailure::kLocalIdTooBig;
                logger::trace("SpellScanner: {} does not fit a {} plugin's local ids", persistentId,
                    plugin->IsLight() ? "light" : "full");
                return 0;
            }

            failure = ResolveFailure::kNone;
            if (plugin->IsLight()) {
                return kLightPrefix | (static_cast<std::uint32_t>(plugin->GetSmallFileCompileIndex()) << kLightFileShift) |
                       localFormId;
            }
            return (static_cast<std::uint32_t>(plugin->GetCompileIndex()) << kFileIndexShift) | localFormId;
        }
    }

    RE::FormID ResolvePersistentFormId(const std::string& persistentId)
    {
        PluginLookup lookup;
        ResolveFailure failure = ResolveFailure::kNone;
        return ResolveWith(persistentId, lookup, failure);
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

        const auto formId = FormIdText::Parse(formIdStr);
        return formId && IsFormIdValid(*formId);
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
            const auto formId = FormIdText::Parse(formIdStr);
            if (!formId) return false;
            auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(*formId);
            if (!spell) return false;
            const char* name = spell->GetFullName();
            return name && *name && !GateLeavesOut(spell, gateKept) && !IsEquippedInVoiceSlot(spell);
        }

        // A persistentId that resolved to nothing: why, in the log the player can read, and which
        // report list it goes to (a plugin not loaded, or a loaded plugin without that record)
        void NoteUnresolved(TreeRemap::NodeVerdict& verdict, const std::string& persistentId, ResolveFailure failure)
        {
            const std::string plugin = persistentId.substr(0, persistentId.find('|'));
            switch (failure) {
            case ResolveFailure::kPluginNotLoaded:
                verdict.missingPlugin = plugin;
                logger::info("SpellScanner: {} - plugin {} is not loaded", persistentId, plugin);
                break;
            case ResolveFailure::kLocalIdTooBig:
                verdict.missingRecord = persistentId;
                logger::info("SpellScanner: {} - the local id does not fit {} (ESL-flagged after it was saved?)",
                    persistentId, plugin);
                break;
            default:
                verdict.missingRecord = persistentId;
                logger::info("SpellScanner: {} - {} has no such record", persistentId, plugin);
                break;
            }
        }

        // What the game says about one saved node: its formId now, and whether it stays
        TreeRemap::NodeVerdict JudgeNode(const json& node, PluginLookup& lookup,
                                         const std::unordered_set<std::string>& gateKept, std::size_t& notPlayerSpells)
        {
            TreeRemap::NodeVerdict verdict;
            const std::string saved = node["formId"].get<std::string>();
            verdict.formId = saved;
            bool isValid = IsFormIdValid(saved);

            // The persistent ID ("Plugin.esp|0x00080B") names the record whatever the load
            // order, so it wins whenever the node has one. A formId saved before a load order
            // change can still be a valid form - another plugin's record that took the old
            // index (an unnamed Constant Effect ability in place of a spell, 2026-10-05) - so
            // it is not trusted on its own.
            // Only a well-formed one ("Plugin|0x..."): an empty or broken field is no persistent
            // ID, and the node keeps the old rule (its formId if that is valid)
            if (node.contains("persistentId") && node["persistentId"].is_string() &&
                node["persistentId"].get<std::string>().find('|') != std::string::npos) {
                const std::string persistentId = node["persistentId"].get<std::string>();
                verdict.hasPersistentId = true;
                ResolveFailure failure = ResolveFailure::kNone;
                const RE::FormID resolvedId = ResolveWith(persistentId, lookup, failure);
                if (resolvedId != 0 && IsFormIdValid(resolvedId)) {
                    verdict.formId = FormIdText::Format(resolvedId);
                    isValid = true;
                    if (TreeRemap::NormalizeFormId(saved) != verdict.formId) {
                        logger::info("SpellScanner: Resolved {} -> {} from persistent ID {}", saved, verdict.formId,
                            persistentId);
                    }
                } else {
                    // The plugin or the record is gone: the old formId, valid or not, is not this spell
                    isValid = false;
                    NoteUnresolved(verdict, persistentId, failure);
                }
            }

            // A node the tree shows is a player's spell (IsPlayerSpell). Anything else leaves
            // the tree as it loads, so the player never meets an unnamed or code-like node
            if (isValid && !IsPlayerSpell(verdict.formId, gateKept)) {
                isValid = false;
                notPlayerSpells++;
                logger::info("SpellScanner: {} ({}) is not a player's spell now - left out of the tree",
                    verdict.formId, node.value("name", std::string("?")));
            }
            if (!isValid) logger::warn("SpellScanner: Invalid FormID in tree: {}", saved);
            verdict.keep = isValid;
            return verdict;
        }

        void LogReport(const TreeRemap::Report& report)
        {
            if (report.nodesWithoutFormId > 0) {
                logger::warn("SpellScanner: {} nodes without a string formId - left out of the tree",
                    report.nodesWithoutFormId);
            }
            for (const auto& saved : report.collisions) {
                logger::warn("SpellScanner: {} has no persistent ID and a resolved spell now holds that formId - "
                    "left out of the tree", saved);
            }
            for (const auto& change : report.rootChanges) {
                logger::info("SpellScanner: Updated {} root {} -> {}", change.school, change.from, change.to);
            }
            if (report.bridgesDropped > 0) {
                logger::info("SpellScanner: Dropped {} bridges to removed spells", report.bridgesDropped);
            }
        }
    }

    TreeValidationResult ValidateAndFixTree(json& treeData)
    {
        TreeValidationResult result;
        if (!treeData.contains("schools") || !treeData["schools"].is_object()) {
            logger::warn("SpellScanner: Tree has no schools object - nothing checked");
            return result;
        }

        const auto gateKept = ReadGateKept();
        // One per pass: the file list is converted once, not once per node
        PluginLookup pluginLookup;
        std::size_t notPlayerSpells = 0;

        // The node verdicts come from the game; the formIds and every link to them are fixed
        // by TreeRemap (no game types, tools/tree-remap-check runs the same code). Saved links
        // hold the formIds the file was saved with, so each is looked up once by that value
        const auto report = TreeRemap::FixTree(treeData, [&](const json& node) {
            return JudgeNode(node, pluginLookup, gateKept, notPlayerSpells);
        });
        LogReport(report);

        result.totalNodes = report.totalNodes;
        result.validNodes = report.validNodes;
        result.invalidNodes = report.invalidNodes;
        result.resolvedFromPersistent = report.resolvedFromPersistent;
        result.missingPlugins.assign(report.missingPlugins.begin(), report.missingPlugins.end());
        result.missingRecords.assign(report.missingRecords.begin(), report.missingRecords.end());
        result.invalidFormIds.assign(report.removedFormIds.begin(), report.removedFormIds.end());

        logger::info("SpellScanner: Tree validation complete - {}/{} valid, {} resolved from persistent, {} invalid "
            "({} not a player's spell, {} legacy nodes on a resolved spell's formId, {} legacy nodes following a "
            "copy with a persistent ID), {} nodes moved to a new formId",
            result.validNodes, result.totalNodes, result.resolvedFromPersistent, result.invalidNodes, notPlayerSpells,
            report.legacyCollisions, report.legacyFollowed, report.moved.size());

        return result;
    }
}
