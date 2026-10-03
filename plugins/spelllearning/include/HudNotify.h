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
//   - Settings > "Show Notifications" (notifications.enabled) turns all of them off.
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

    /** Settings > Show Notifications. */
    [[nodiscard]] bool IsEnabled();

    /**
     * {{name}} in a_text replaced by its value. Pure; a value that itself holds
     * "{{" is not read again.
     */
    [[nodiscard]] std::string FillPlaceholders(std::string_view a_text, std::span<const Param> a_params);

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

    /** Show Text(...) in the corner. Nothing when notifications are off; returns whether it showed. */
    bool Show(std::string_view a_key, std::string_view a_english, std::span<const Param> a_params);
    inline bool Show(std::string_view a_key, std::string_view a_english, std::initializer_list<Param> a_params = {})
    {
        return Show(a_key, a_english, std::span<const Param>(a_params.begin(), a_params.size()));
    }

    /** "A", "A and B", "A, B and C" with the language's separators (hud.listSeparator, hud.listLastSeparator). */
    [[nodiscard]] std::string JoinList(const std::vector<std::string>& a_items);
}
