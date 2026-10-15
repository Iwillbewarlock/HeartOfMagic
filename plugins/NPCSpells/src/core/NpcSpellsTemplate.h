#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
namespace NpcSpells {
    // Template chain: link 0 is the NPC itself; link i+1 is link i's baseTemplateForm. Flags are TEMPLATE_USE_FLAG bits (RE/T/TESActorBaseData.h:46-62).
    struct ChainLink { std::uint16_t templateFlags; bool isLeveledCharacter; };
    enum class Category : std::uint16_t { Traits = 1 << 0, Stats = 1 << 1, Factions = 1 << 2, Spells = 1 << 3, Keywords = 1 << 12 };
    // Index of the link that provides the category, or nullopt when a leveled character is met first (unknowable until spawn).
    std::optional<std::size_t> ProviderIndex(const std::vector<ChainLink>& chain, Category category);
}
