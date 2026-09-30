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
        for (const auto& plugin : loadOrder) {
            if (!ScanPlugin(plugin, names, tomes, result.reached, &read)) result.unreadable.push_back(plugin.name);
        }
        result.byRecord = result.reached.size();

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
        if (books) *books = std::move(read);
        return result;
    }
}
