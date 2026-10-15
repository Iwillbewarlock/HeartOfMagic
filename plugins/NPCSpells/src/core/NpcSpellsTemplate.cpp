#include "core/NpcSpellsTemplate.h"
namespace NpcSpells {
std::optional<std::size_t> ProviderIndex(const std::vector<ChainLink>& chain, Category category)
{
    const auto bit = static_cast<std::uint16_t>(category);
    for (std::size_t i = 0; i < chain.size(); ++i) {
        if ((chain[i].templateFlags & bit) == 0) return i;
        // The Spells flag on a record with no template: the record's own spell list is the one the game uses
        if (category == Category::Spells && i + 1 >= chain.size() && !chain[i].hasTemplate) return i;
        // The flag says "take it from the template": a missing or leveled template cannot be read before spawn
        if (i + 1 >= chain.size() || chain[i + 1].isLeveledCharacter) return std::nullopt;
    }
    return std::nullopt;
}
}
namespace NpcSpells {
std::optional<std::size_t> SpellListProviderIndex(const std::vector<ChainLink>& chain)
{
    if (chain.size() == 1) return 0;
    return ProviderIndex(chain, Category::Spells);
}
}
