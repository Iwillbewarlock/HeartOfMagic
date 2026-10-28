#include "librarian/SpellResearchPlayerFilter.h"
#include "AsciiText.h"
#include "FormIdText.h"
#include "JsonText.h"
#include "SpellScanner.h"
#include "librarian/LibrarianInternal.h"
#include "treebuilder/GraftCandidates.h"

// =============================================================================
// LibrarianSpellResearchFilter - SpellResearchPlayerFilter.h (moved out of
// LibrarianSpellResearchPatch.cpp, at the 600 line limit)
// =============================================================================

namespace Librarian::SpellResearchPatch
{
    using namespace Librarian::Detail;

    namespace
    {
        // "plugin|<local id as a number>" for a plugin name and a hex local id ("0x00080B" or "00080b");
        // empty when the id is missing or not hex (the entry is then read by its formId)
        std::string LocalKey(const std::string& plugin, const std::string& localHex)
        {
            const auto localId = FormIdText::Parse(localHex);
            return localId ? AsciiText::ToLower(plugin) + "|" + std::to_string(*localId) : std::string();
        }
    }

    PlayerFilter ReadPlayerFilter()
    {
        PlayerFilter filter;
        json config;
        if (!ReadJsonFile(DataPath("config.json"), config) || !config.is_object()) return filter;
        const auto text = [](const json& entry, const char* key) {
            const auto found = entry.find(key);
            return (found != entry.end() && found->is_string()) ? found->get<std::string>() : std::string();
        };
        if (const auto list = config.find("spellBlacklist"); list != config.end() && list->is_array()) {
            for (const auto& entry : *list) {
                if (!entry.is_object()) continue;
                const auto key = LocalKey(text(entry, "plugin"), text(entry, "localFormId"));
                if (!text(entry, "plugin").empty() && !key.empty()) {
                    filter.blacklisted.insert(key);
                } else if (const auto id = SpellScanner::ResolvePersistentFormId(text(entry, "formId")); id) {
                    filter.blacklistedIds.insert(id);
                } else if (const auto formId = FormIdText::ParseSpellOf(text(entry, "formId"))) {  // a copy's "_dup_" id (Shift+Eraser) names its spell
                    filter.blacklistedIds.insert(*formId);
                } else {
                    logger::warn("SpellResearchPatch: spellBlacklist entry {} has no usable plugin, persistentId "
                                 "or formId - ignored", JsonText::Dump(entry));
                }
            }
        }
        // The tree graft reads the same list (GraftCandidates.h, PlayerRulesOf)
        filter.pluginsOff = TreeGraft::PluginsOffOf(config);
        return filter;
    }

    bool PlayerLeftOut(const std::string& persistentId, RE::FormID formId, const PlayerFilter& filter)
    {
        const auto pipe = persistentId.find('|');
        if (pipe == std::string::npos) return filter.blacklistedIds.contains(formId);
        const std::string plugin = persistentId.substr(0, pipe);
        return filter.pluginsOff.contains(AsciiText::ToLower(plugin)) || filter.blacklistedIds.contains(formId) ||
            filter.blacklisted.contains(LocalKey(plugin, persistentId.substr(pipe + 1)));
    }
}
