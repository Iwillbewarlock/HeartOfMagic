#pragma once

#include "Common.h"

#include "WidgetStrings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <utility>

// =============================================================================
// WIDGET TEXT - the spell card's text helpers (moved out of Widget.cpp, Task 8
// fix round 1) over the on-screen text table of WidgetStrings.h. Header only,
// game types through Common.h; Widget.cpp takes it with `using namespace WidgetText`.
// =============================================================================

namespace WidgetText
{
    inline constexpr double kSecondsPerMinute = 60.0, kSecondsPerHour = 3600.0;

    // A string game setting, empty when there is none
    inline std::string GameString(const char* a_setting)
    {
        auto* settings = RE::GameSettingCollection::GetSingleton();
        auto* setting = settings ? settings->GetSetting(a_setting) : nullptr;
        const char* text = setting && setting->GetType() == RE::Setting::Type::kString ? setting->GetString() : nullptr;
        return text ? text : "";
    }

    inline std::string HtmlEscape(std::string_view a_text)
    {
        std::string out;
        for (const char c : a_text) {
            switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c; break;
            }
        }
        return out;
    }

    inline std::string ColorHex(std::uint32_t a_rgb)
    {
        return std::format("#{:06X}", a_rgb);
    }

    // A time left in SkyUI's way (ItemCard.as:330-354: whole hours, else whole minutes, else seconds) in our words
    inline std::string TimeText(double a_seconds)
    {
        const double whole = std::floor(a_seconds);
        if (whole >= kSecondsPerHour) return Fill(Str::kCardHours, { { "{n}", std::format("{:.0f}", std::floor(whole / kSecondsPerHour)) } });
        if (whole >= kSecondsPerMinute) return Fill(Str::kCardMinutes, { { "{n}", std::format("{:.0f}", std::floor(whole / kSecondsPerMinute)) } });
        return Fill(Str::kCardSeconds, { { "{n}", std::format("{:.0f}", whole) } });
    }

    inline std::string CastTypeText(const RE::SpellItem* a_spell)
    {
        switch (a_spell->GetCastingType()) {
        case RE::MagicSystem::CastingType::kConcentration: return Text(Str::kCardCastConcentration);
        case RE::MagicSystem::CastingType::kConstantEffect: return Text(Str::kCardCastConstant);
        case RE::MagicSystem::CastingType::kScroll: return Text(Str::kCardCastScroll);
        default: return Text(Str::kCardCastFireForget);
        }
    }

    inline std::string PowerText(const RE::SpellItem* a_spell)
    {
        if (!a_spell) return Text(Str::kCardPowerAny);
        switch (a_spell->GetSpellType()) {
        case RE::MagicSystem::SpellType::kPower: return Text(Str::kCardPower);
        case RE::MagicSystem::SpellType::kLesserPower: return Text(Str::kCardLesserPower);
        case RE::MagicSystem::SpellType::kAbility: return Text(Str::kCardAbility);
        default: return Text(Str::kCardPowerAny);
        }
    }

    inline std::string Upper(std::string a_text)
    {
        std::ranges::transform(a_text, a_text.begin(), [](char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; });
        return a_text;
    }

    // The colours an HTML text sets ("color=" values, upper case), for the colour check
    inline std::set<std::string> HtmlColours(const std::string& a_html)
    {
        std::string lower = a_html;
        std::ranges::transform(lower, lower.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
        std::set<std::string> colours;
        constexpr std::size_t kHexDigits = 6;
        for (auto at = lower.find("color="); at != std::string::npos; at = lower.find("color=", at + 1)) {
            const auto hash = lower.find('#', at);
            if (hash == std::string::npos || hash > at + 8 || hash + 1 + kHexDigits > lower.size()) continue;
            colours.insert(Upper(a_html.substr(hash + 1, kHexDigits)));
        }
        return colours;
    }
}
