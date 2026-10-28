#include "tomereach/TomeReachPass.h"
#include "tomereach/TomeReachConfig.h"

#include <unordered_map>

namespace TomeReach
{
    PassResult FindReachedTomes(const std::vector<PluginFile>& loadOrder, NameTable& names,
        const std::unordered_set<Key>& tomes, const std::vector<std::filesystem::path>& configs,
        std::vector<BookRecord>* books)
    {
        PassResult result;
        std::vector<BookRecord> read;
        Evidence evidence;
        std::unordered_set<std::uint32_t> damagedScope;
        for (const auto& plugin : loadOrder) {
            const auto scan = ScanPlugin(plugin, names, tomes, evidence, &read);
            if (scan.status == ScanStatus::kUnreadable) {
                result.unreadable.push_back(plugin.name);
            } else if (scan.status == ScanStatus::kDamaged) {
                result.damaged.push_back(plugin.name);
                damagedScope.insert(scan.scope.begin(), scan.scope.end());
            }
        }
        result.reached = evidence.found;
        result.byRecord = result.reached.size();
        for (const Key tome : evidence.foundLoose) {
            if (result.reached.insert(tome).second) {
                ++result.byLooseOnly;
                result.looseFrom.emplace(tome, evidence.looseFrom[tome]);
            }
        }

        // The rest: named by a distribution config, by editor id or by plugin and id
        std::unordered_map<Key, std::string> editorIds;
        for (const auto& book : read) editorIds[book.book] = book.editorId;
        ConfigText text;
        for (const auto& file : configs) text.Add(file);
        result.configFiles = text.FileCount();
        // Quest-only so far: a quest's script or alias names it, no record field or other script does
        for (const Key tome : evidence.foundQuest) {
            if (!evidence.found.contains(tome) && !evidence.foundOtherLoose.contains(tome)) {
                result.questOnly.insert(tome);
            }
        }
        for (const Key tome : tomes) {
            // A quest-only tome is read too: a config that names it hands it out
            if (result.reached.contains(tome) && !result.questOnly.contains(tome)) continue;
            const auto plugin = PluginOf(tome);
            const auto local = LocalOf(tome);
            const auto edid = editorIds.find(tome);
            if (text.Names(names.Name(plugin), local, edid != editorIds.end() ? edid->second : std::string())) {
                if (result.reached.insert(tome).second) ++result.byConfig;
                result.questOnly.erase(tome);
            }
        }
        // A damaged plugin could have named any tome of itself or its masters
        for (const Key tome : tomes) {
            if (!damagedScope.contains(PluginOf(tome))) continue;
            if (result.reached.insert(tome).second) ++result.byDamaged;
            result.questOnly.erase(tome);
        }
        // Last: a debug tome counts as handed out by nothing, wherever it is placed. Mods
        // keep them in a test cell (Sa'chil's SU04SpellTomeDebug: "place Sa'chil", in
        // the SachilDebug cell); on the author's load order every BOOK with "debug" in
        // its editor id is one (3 of them)
        for (const auto& [book, editorId] : editorIds) {  // the winning override's editor id
            if (tomes.contains(book) && AsciiText::ToLower(editorId).find("debug") != std::string::npos &&
                result.reached.erase(book)) {
                ++result.byDebug;
                result.questOnly.erase(book);
            }
        }
        if (books) *books = std::move(read);
        return result;
    }
}
