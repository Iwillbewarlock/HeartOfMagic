#pragma once

#include <filesystem>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// =============================================================================
// HUD NOTIFICATIONS (top-left corner messages)
// =============================================================================
//
// Every corner message Heart of Magic shows goes through here, so that
//   - the text comes from the panel's language file (lang/<locale>.json, the
//     "hud.*" keys), with the English text written at the call as the fallback
//     when the file or the key is missing, and
//   - Settings > "XP Alerts" (notifications.enabled, the key keeps its old name)
//     turns the XP messages off (IsXPAlert: casting, power steps, studying, book
//     XP). Learned, mastered, blocked tomes and the Spell Research line always show.
//
// The strings use the panel's {{name}} placeholders (modules/i18n.js). The table
// is read once per language: Configure() runs whenever the settings are applied
// (config load and every save, UIManagerConfig.cpp) and reads the file only when
// the language changed. Show/Text are safe from any thread (Papyrus, other DLLs).

namespace HudNotify
{
    /** One {{name}} placeholder and the text that replaces it. */
    using Param = std::pair<std::string_view, std::string>;

    /**
     * Turn notifications on or off and load the language's hud.* strings.
     * @param localeCode  the panel's language ("en", "ko", "pt-br")
     * @param langFile    lang/<localeCode>.json of the panel
     */
    void Configure(bool enabled, const std::string& localeCode, const std::filesystem::path& langFile);

    /** Settings > XP Alerts. */
    [[nodiscard]] bool IsEnabled();

    /** The key is an XP message, which the XP Alerts switch turns off. */
    [[nodiscard]] bool IsXPAlert(std::string_view a_key);

    /** Whether a message of this key shows now: anything but an XP alert, or every one with XP Alerts on. */
    [[nodiscard]] bool IsShown(std::string_view a_key);

    /**
     * Whether a placeholder name matches a parameter name, ignoring ASCII case.
     * A name from Papyrus arrives as a BSFixedString, which the game pools without
     * case: "spell" comes back as "Spell" when the pool already holds that word,
     * so an exact compare left {{spell}} unfilled in the ISL study messages (1.9.1).
     */
    [[nodiscard]] inline bool PlaceholderNameEquals(std::string_view a_lhs, std::string_view a_rhs)
    {
        if (a_lhs.size() != a_rhs.size()) return false;
        auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
        for (std::size_t i = 0; i < a_lhs.size(); ++i) {
            if (lower(a_lhs[i]) != lower(a_rhs[i])) return false;
        }
        return true;
    }

    /**
     * {{name}} in a_text replaced by its value (names compared without case,
     * PlaceholderNameEquals). Pure and header-only for tools/hud-placeholders-check;
     * a value that itself holds "{{" is not read again.
     */
    [[nodiscard]] inline std::string FillPlaceholders(std::string_view a_text, std::span<const Param> a_params)
    {
        constexpr std::string_view kOpen = "{{";
        constexpr std::string_view kClose = "}}";
        std::string out;
        out.reserve(a_text.size());
        std::size_t pos = 0;
        while (pos < a_text.size()) {
            auto open = a_text.find(kOpen, pos);
            if (open == std::string_view::npos) break;
            auto close = a_text.find(kClose, open + kOpen.size());
            if (close == std::string_view::npos) break;
            auto name = a_text.substr(open + kOpen.size(), close - open - kOpen.size());
            const Param* match = nullptr;
            for (const auto& param : a_params) {
                if (PlaceholderNameEquals(param.first, name)) {
                    match = &param;
                    break;
                }
            }
            out.append(a_text.substr(pos, open - pos));
            if (match) {
                out.append(match->second);   // not read again: a spell name with {{ stays as it is
            } else {
                out.append(a_text.substr(open, close + kClose.size() - open));
            }
            pos = close + kClose.size();
        }
        if (pos < a_text.size()) out.append(a_text.substr(pos));
        return out;
    }

    /**
     * The translated text of a key with its placeholders filled, or a_english
     * filled the same way when the language has no such key. An empty key looks
     * nothing up (a_english is used as it is, placeholders filled).
     */
    [[nodiscard]] std::string Text(std::string_view a_key, std::string_view a_english, std::span<const Param> a_params);
    [[nodiscard]] inline std::string Text(std::string_view a_key, std::string_view a_english,
                                          std::initializer_list<Param> a_params = {})
    {
        return Text(a_key, a_english, std::span<const Param>(a_params.begin(), a_params.size()));
    }

    /** Show Text(...) in the corner. Nothing for an XP alert with XP Alerts off; returns whether it showed. */
    bool Show(std::string_view a_key, std::string_view a_english, std::span<const Param> a_params);
    inline bool Show(std::string_view a_key, std::string_view a_english, std::initializer_list<Param> a_params = {})
    {
        return Show(a_key, a_english, std::span<const Param>(a_params.begin(), a_params.size()));
    }

    /** "A", "A and B", "A, B and C" with the language's separators (hud.listSeparator, hud.listLastSeparator). */
    [[nodiscard]] std::string JoinList(const std::vector<std::string>& a_items);
}
