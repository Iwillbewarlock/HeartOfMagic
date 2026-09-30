#include "treebuilder/TreeBuilderInternal.h"
#include "treebuilder/TreeNLP.h"

#include <unordered_set>

// =============================================================================
// TreeBuilderPlugins - which plugin a spell comes from (mod themes, Classic's
// same-mod parent bonus)
// =============================================================================

namespace
{
    // The game and its DLC: their ids carry no author prefix, so their words are words
    const std::unordered_set<std::string> kBaseGamePlugins = {
        "skyrim.esm", "update.esm", "dawnguard.esm", "hearthfires.esm", "dragonborn.esm"
    };
}

std::string TreeBuilder::Internal::SpellPlugin(const json& spell)
{
    auto plugin = spell.value("plugin", std::string(""));
    if (plugin.empty()) {
        const auto persistentId = spell.value("persistentId", std::string(""));
        plugin = persistentId.substr(0, persistentId.find('|'));
    }
    return TreeNLP::ToLower(plugin);
}

bool TreeBuilder::Internal::IsBaseGamePlugin(const std::string& lowerPlugin)
{
    return kBaseGamePlugins.contains(lowerPlugin);
}

bool TreeBuilder::Internal::SharesModPlugin(const TreeNode& a, const TreeNode& b)
{
    return !a.modPlugin.empty() && a.modPlugin == b.modPlugin;
}
