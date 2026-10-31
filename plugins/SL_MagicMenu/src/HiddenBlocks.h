#pragma once

// =============================================================================
// HIDDEN BLOCKS - [Blocks] sHidden of MagicMenuRewrite.ini (2026-10-31, the settings
// tab: "활성화할 블록창을 고를 수 있게"): the block ids the player turned off, as
// one comma-separated value. The host treats a hidden block like an add-on that is
// not installed: no place (its place in the layout file kept, no gap), no panel, no
// draw, no frame or key call, no key hint, not in the layout mode. Read at plugin
// load and at each magic menu open (BlockHiding.cpp); the settings tab writes it.
// Pure: no game types, no logger, so tools/magic-menu-layout-check runs it.
// =============================================================================

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace HiddenBlocks
{
    inline constexpr std::string_view kSection = "Blocks", kKey = "sHidden";
    inline constexpr char kSeparator = ',';
    inline constexpr std::size_t kMaxChars = 2048;  // the value read at most (a block id is a few dozen characters)

    namespace Detail
    {
        inline std::string_view Trim(std::string_view a_text)
        {
            const auto first = a_text.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) return {};
            const auto last = a_text.find_last_not_of(" \t\r\n");
            return a_text.substr(first, last - first + 1);
        }
    }

    /// The ids as written: split at ',', each trimmed (spaces, tabs), empty items and repeats dropped (the first kept), the order
    /// kept. Unknown ids are kept too (an add-on not installed now is hidden again when it comes back). O(ids^2), a handful
    inline std::vector<std::string> Parse(std::string_view a_value)
    {
        std::vector<std::string> ids;
        while (true) {
            const auto comma = a_value.find(kSeparator);
            const auto item = Detail::Trim(a_value.substr(0, comma));
            if (!item.empty() && std::ranges::find(ids, item) == ids.end()) ids.emplace_back(item);
            if (comma == std::string_view::npos) break;
            a_value.remove_prefix(comma + 1);
        }
        return ids;
    }

    /// The value written back: the ids joined by ',' (no spaces); "" for none
    inline std::string Join(const std::vector<std::string>& a_ids)
    {
        std::string value;
        for (const auto& id : a_ids) value += (value.empty() ? "" : std::string(1, kSeparator)) + id;
        return value;
    }

    /// a_ids with a_id hidden (appended when not there) or shown (removed); every other id, unknown ones too, kept in order
    inline std::vector<std::string> With(std::vector<std::string> a_ids, std::string_view a_id, bool a_hidden)
    {
        const auto at = std::ranges::find(a_ids, a_id);
        if (a_hidden && at == a_ids.end() && !a_id.empty()) a_ids.emplace_back(a_id);
        if (!a_hidden && at != a_ids.end()) a_ids.erase(at);
        return a_ids;
    }

    /// What the host hides: the ids of a_ids that are blocks (a_known: final ids), a retired id (a key of a_legacyToFinal, the
    /// layout file's old name of an add-on's block) read as the final id that took it over
    inline std::set<std::string> Effective(const std::vector<std::string>& a_ids, const std::set<std::string>& a_known,
        const std::map<std::string, std::string>& a_legacyToFinal)
    {
        std::set<std::string> hidden;
        for (const auto& id : a_ids) {
            const auto alias = a_legacyToFinal.find(id);
            const auto& finalId = alias != a_legacyToFinal.end() ? alias->second : id;
            if (a_known.contains(finalId)) hidden.insert(finalId);
        }
        return hidden;
    }

    /// The ids of a_ids that name no block here (the log names them; they stay in the ini)
    inline std::vector<std::string> Unknown(const std::vector<std::string>& a_ids, const std::set<std::string>& a_known,
        const std::map<std::string, std::string>& a_legacyToFinal)
    {
        std::vector<std::string> unknown;
        for (const auto& id : a_ids) {
            const auto alias = a_legacyToFinal.find(id);
            if (!a_known.contains(alias != a_legacyToFinal.end() ? alias->second : id)) unknown.push_back(id);
        }
        return unknown;
    }

    /// a_heights (the blocks Layout::Arrange may place) without the hidden ids: they are absent there, as an add-on not installed
    template <class Heights>
    void Drop(Heights& a_heights, const std::set<std::string>& a_hidden)
    {
        std::erase_if(a_heights, [&](const auto& a_entry) { return a_hidden.contains(a_entry.first); });
    }
}
