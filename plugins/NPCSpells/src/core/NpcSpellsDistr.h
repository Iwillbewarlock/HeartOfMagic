#pragma once

// =============================================================================
// NPC SPELLS - SPELLS A SPID CONFIG ALREADY NAMES
// =============================================================================
// The same answers as TomeReach::ConfigText::Names(plugin, local, "") over the
// same text, but each plugin's mentions are found once, not once per spell:
// a tree of 1452 spells asked the old way scanned every config 1452 times.
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace NpcSpells
{
    class DistrIndex
    {
    public:
        // lowerText: ConfigText::Text(); it must outlive the index
        explicit DistrIndex(const std::string& lowerText) : m_text(lowerText) {}

        // Does a config name form `local` of the plugin (ASCII lower case) as "<hex>~plugin" or "plugin|<hex>"
        [[nodiscard]] bool Names(const std::string& pluginLower, std::uint32_t local);

    private:
        const std::string& m_text;
        std::unordered_map<std::string, std::vector<std::size_t>> m_mentions;  // plugin -> offsets, as Names' find loop
    };
}
