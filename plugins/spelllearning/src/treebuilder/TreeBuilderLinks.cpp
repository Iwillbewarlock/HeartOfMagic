#include "treebuilder/TreeBuilderInternal.h"

#include <array>
#include <unordered_set>

// =============================================================================
// LINK CLEANUP
// =============================================================================
//
// A spell is never its own child or prerequisite, and a node lists each link
// once. AddChild, AddPrerequisite and LinkNodes already refuse both, so a tree
// the builder grows has none; this pass is the net under the whole tree before
// it is validated and saved, in case a later step writes the arrays directly.
// It works on the JSON in place, so every other node field stays as it was.

namespace
{
    constexpr std::array<const char*, 2> kLinkKeys = { "children", "prerequisites" };

    int CleanLinkList(json& list, const std::string& ownId)
    {
        if (!list.is_array()) return 0;

        std::unordered_set<std::string> seen;
        json kept = json::array();
        int removed = 0;
        for (auto& id : list) {
            if (id.is_string()) {
                const auto& s = id.get_ref<const std::string&>();
                if (s == ownId || !seen.insert(s).second) {
                    ++removed;
                    continue;
                }
            }
            kept.push_back(std::move(id));
        }
        if (removed > 0) list = std::move(kept);
        return removed;
    }
}

int TreeBuilder::Internal::RemoveBadLinks(json& treeData)
{
    if (!treeData.contains("schools") || !treeData["schools"].is_object()) return 0;

    int removed = 0;
    for (auto& [schoolName, schoolData] : treeData["schools"].items()) {
        if (!schoolData.contains("nodes") || !schoolData["nodes"].is_array()) continue;
        for (auto& node : schoolData["nodes"]) {
            if (!node.is_object()) continue;
            const auto ownId = node.value("formId", std::string(""));
            for (const char* key : kLinkKeys) {
                if (node.contains(key)) removed += CleanLinkList(node[key], ownId);
            }
        }
    }
    return removed;
}
