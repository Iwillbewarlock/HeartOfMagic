#include "core/NpcSpellsDistr.h"

#include "tomereach/TomeReachConfig.h"

#include <format>

namespace NpcSpells
{
    bool DistrIndex::Names(const std::string& pluginLower, std::uint32_t local)
    {
        if (pluginLower.empty() || !local) {
            return false;
        }
        auto it = m_mentions.find(pluginLower);
        if (it == m_mentions.end()) {
            std::vector<std::size_t> offsets;
            // overlapping mentions too (at + 1), as ConfigText::Names searches
            for (auto at = m_text.find(pluginLower); at != std::string::npos; at = m_text.find(pluginLower, at + 1)) {
                offsets.push_back(at);
            }
            it = m_mentions.emplace(pluginLower, std::move(offsets)).first;
        }
        const auto hex = std::format("{:x}", local);
        for (const auto at : it->second) {
            if (TomeReach::IdNextTo(m_text, at, pluginLower.size(), hex)) {
                return true;
            }
        }
        return false;
    }
}
