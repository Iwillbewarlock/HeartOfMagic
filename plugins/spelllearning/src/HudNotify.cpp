#include "HudNotify.h"
#include <algorithm>
#include <array>
#include "PathText.h"

#include <atomic>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <nlohmann/json.hpp>

// =============================================================================
// HUD NOTIFICATIONS - see HudNotify.h
// =============================================================================

namespace HudNotify
{
    namespace
    {
        constexpr std::string_view kKeyPrefix = "hud.";
        constexpr std::string_view kOpen = "{{";
        constexpr std::string_view kClose = "}}";

        using Table = std::unordered_map<std::string, std::string>;

        std::atomic<bool> g_enabled{ true };

        // The table is swapped whole under the lock; readers copy the pointer and
        // read without it
        std::mutex g_tableMutex;
        std::shared_ptr<const Table> g_table = std::make_shared<const Table>();
        std::string g_loadedLocale;
        bool g_loadedOnce = false;

        std::shared_ptr<const Table> CurrentTable()
        {
            std::lock_guard<std::mutex> lock(g_tableMutex);
            return g_table;
        }

        constexpr std::string_view kChipPrefix = "chips.";  // tag chip names, for the magic menu (MagicMenuSpellInfo.cpp)
        constexpr std::string_view kLanguageMissing = "corner messages stay in English";
        constexpr std::string_view kEnglishMissing = "tag names the language lacks show their ids";

        // The hud.* strings and the chips.* names: nothing else of the file is read here.
        // a_consequence ends the warning when the file is missing or broken
        Table ReadTable(const std::filesystem::path& a_file, std::string_view a_consequence)
        {
            Table table;
            std::ifstream file(a_file, std::ios::binary);
            if (!file.is_open()) {
                logger::warn("HudNotify: {} not found - {}", PathText::Utf8(a_file), a_consequence);
                return table;
            }
            std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            auto json = nlohmann::json::parse(text, nullptr, false);
            if (json.is_discarded() || !json.is_object()) {
                logger::warn("HudNotify: {} could not be read - {}", PathText::Utf8(a_file), a_consequence);
                return table;
            }
            for (auto& [key, value] : json.items()) {
                if ((key.starts_with(kKeyPrefix) || key.starts_with(kChipPrefix)) && value.is_string()) {
                    table.emplace(key, value.get<std::string>());
                }
            }
            return table;
        }
    }

    void Configure(bool enabled, const std::string& localeCode, const std::filesystem::path& langFile,
                   const std::filesystem::path& englishFile)
    {
        g_enabled.store(enabled);

        {
            std::lock_guard<std::mutex> lock(g_tableMutex);
            if (g_loadedOnce && g_loadedLocale == localeCode) return;
        }
        auto read = ReadTable(langFile, kLanguageMissing);
        // A tag name the language lacks comes from English, as the panel's card does: the chip
        // ids themselves (element.fire) are not for players. The hud.* lines fall back to the
        // English written at each call instead
        if (langFile != englishFile) {
            for (auto& [key, value] : ReadTable(englishFile, kEnglishMissing)) {
                if (key.starts_with(kChipPrefix)) read.try_emplace(key, std::move(value));
            }
        }
        auto table = std::make_shared<const Table>(std::move(read));
        logger::info("HudNotify: {} corner message strings for '{}' (notifications {})",
                     table->size(), localeCode, enabled ? "on" : "off");
        std::lock_guard<std::mutex> lock(g_tableMutex);
        g_table = std::move(table);
        g_loadedLocale = localeCode;
        g_loadedOnce = true;
    }

    bool IsEnabled()
    {
        return g_enabled.load();
    }

    bool IsXPAlert(std::string_view a_key)
    {
        // How XP and power grow: casting a weakened spell, a power step, studying a
        // tome (the hook, the ISL patch's hours), book XP. Not learned, mastered,
        // blocked or the Spell Research line - those the player needs to see
        static constexpr std::array<std::string_view, 11> kXPAlertKeys = {
            "hud.castPowerXP", "hud.castPower", "hud.powerIncreased",
            "hud.beginStudy", "hud.review", "hud.beginGrasp",
            "hud.studiedHour", "hud.studiedHours",
            "hud.bookXpGranted", "hud.bookXpNoTargets", "hud.bookXpInitialized"
        };
        return std::ranges::find(kXPAlertKeys, a_key) != kXPAlertKeys.end();
    }

    bool IsShown(std::string_view a_key)
    {
        return IsEnabled() || !IsXPAlert(a_key);
    }

    std::string FillPlaceholders(std::string_view a_text, std::span<const Param> a_params)
    {
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
                if (param.first == name) {
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

    std::string Text(std::string_view a_key, std::string_view a_english, std::span<const Param> a_params)
    {
        if (!a_key.empty()) {
            auto table = CurrentTable();
            auto it = table->find(std::string(a_key));
            if (it != table->end() && !it->second.empty()) {
                return FillPlaceholders(it->second, a_params);
            }
        }
        return FillPlaceholders(a_english, a_params);
    }

    bool Show(std::string_view a_key, std::string_view a_english, std::span<const Param> a_params)
    {
        if (!IsShown(a_key)) return false;
        auto text = Text(a_key, a_english, a_params);
        if (text.empty()) return false;
        RE::SendHUDMessage::ShowHUDMessage(text.c_str());
        return true;
    }

    std::string JoinList(const std::vector<std::string>& a_items)
    {
        auto separator = Text("hud.listSeparator", ", ");
        auto lastSeparator = Text("hud.listLastSeparator", " and ");
        std::string out;
        for (std::size_t i = 0; i < a_items.size(); ++i) {
            if (i > 0) out += (i + 1 == a_items.size()) ? lastSeparator : separator;
            out += a_items[i];
        }
        return out;
    }
}
