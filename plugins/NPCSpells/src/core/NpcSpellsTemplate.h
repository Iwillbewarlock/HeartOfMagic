#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
namespace NpcSpells {
    // Template chain: link 0 is the NPC itself; link i+1 is link i's baseTemplateForm. Flags are TEMPLATE_USE_FLAG bits (RE/T/TESActorBaseData.h:46-62).
    // hasTemplate: the link names a template (baseTemplateForm set). The last link of a chain cut by a cycle or the depth
    // limit has one; a record that really ends the chain has none.
    struct ChainLink { std::uint16_t templateFlags; bool isLeveledCharacter; bool hasTemplate = false; };
    enum class Category : std::uint16_t { Traits = 1 << 0, Stats = 1 << 1, Factions = 1 << 2, Spells = 1 << 3, Keywords = 1 << 12 };
    // Index of the link that provides the category, or nullopt when a leveled character is met first (unknowable until spawn).
    // Spells only: a link with the flag and no template of its own (it ends the chain) keeps its own spell list and provides
    // it, as a record with the flag and no template does (AttachToNpcs, GameNpcs.cpp); a chain cut with the flag on is nullopt.
    std::optional<std::size_t> ProviderIndex(const std::vector<ChainLink>& chain, Category category);
    // The spell list's provider: ProviderIndex for Spells, and a chain of one is always the record itself (a record whose
    // template is itself, a cycle WalkChain cuts at once, keeps its own list too)
    std::optional<std::size_t> SpellListProviderIndex(const std::vector<ChainLink>& chain);
}
