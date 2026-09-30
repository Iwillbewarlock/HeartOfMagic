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
        for (const Key tome : tomes) {
            if (result.reached.contains(tome)) continue;
            const auto plugin = PluginOf(tome);
            const auto local = LocalOf(tome);
            const auto edid = editorIds.find(tome);
            if (text.Names(names.Name(plugin), local, edid != editorIds.end() ? edid->second : std::string())) {
                result.reached.insert(tome);
                ++result.byConfig;
            }
        }
        // Last: a damaged plugin could have named any tome of itself or its masters
        for (const Key tome : tomes) {
            if (damagedScope.contains(PluginOf(tome)) && result.reached.insert(tome).second) ++result.byDamaged;
        }
        if (books) *books = std::move(read);
        return result;
    }
}
