#pragma once

#include "Common.h"

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
// WIDGET TEXT - the magic menu's on-screen text table and the spell card's text
// helpers (moved out of Widget.cpp, Task 8 fix round 1). Header only, game types
// through Common.h; Widget.cpp takes it with `using namespace WidgetText`.
// =============================================================================

namespace WidgetText
{
    inline constexpr double kSecondsPerMinute = 60.0, kSecondsPerHour = 3600.0;

    // ---------------------------------------------------------------------
    // On-screen text: one table (spec v5 Q2), Korean (the user plays in Korean). Placeholders in braces are filled by
    // Fill(); {key} already carries its brackets ("[U]"). Wording: Gemini Argon's draft 2026-10-25 with two lines tightened
    // by Claude (spec v5 table); the rows after "build 5" were written in the same short label style for states and lines
    // the spec's table does not have. Not passed to std::format: the braces are names, not format fields.
    // ---------------------------------------------------------------------

    enum class Str : std::size_t
    {
        kPreviewTitle,
        kCardSub,
        kSchoolAlteration,
        kSchoolConjuration,
        kSchoolDestruction,
        kSchoolIllusion,
        kSchoolRestoration,
        // build 7 (C1, drafts for Argon's review)
        kCardMagicka,
        kCardMagickaPerSec,
        kCardCastFireForget,
        kCardCastConcentration,
        kCardCastConstant,
        kCardCastScroll,
        kCardShout,
        kCardShoutRecharge,
        kCardPower,
        kCardLesserPower,
        kCardAbility,
        kCardPowerAny,
        kCardActiveEffect,
        kCardTimeLeft,
        kCardHours,
        kCardMinutes,
        kCardSeconds,
        // blocks plan Task 4 (D5): the layout buttons
        kLayoutReset,
        kEmptyChoose,
        kLayoutGearEnter,
        kLayoutGearLeave,
        kCount
    };

    struct StrEntry
    {
        Str id;
        const char* key;   // the name in the spec's table
        const char* text;
    };

    // HoM add-on plan Task 5: the Heart of Magic rows (progress card, node card, tree, legend) left with the HoM block; the
    // add-on keeps them as they were at f83dc6f3
    inline constexpr auto kStrings = std::to_array<StrEntry>({
        { Str::kPreviewTitle, "preview_title", "미리보기" },
        // our card's school and level line (the node card's "node_sub" until Task 5)
        { Str::kCardSub, "card_sub", "{school} · {level}" },
        { Str::kSchoolAlteration, "school_alteration", "변화마법" },
        { Str::kSchoolConjuration, "school_conjuration", "소환마법" },
        { Str::kSchoolDestruction, "school_destruction", "파괴마법" },
        { Str::kSchoolIllusion, "school_illusion", "환영마법" },
        { Str::kSchoolRestoration, "school_restoration", "회복마법" },
        // build 7 (spec v7 C1): our info card. DRAFT wording by Claude in the short label style, for Argon's review
        { Str::kCardMagicka, "card_magicka", "매지카 {n}" },
        { Str::kCardMagickaPerSec, "card_magicka_per_sec", "매지카 {n}/초" },
        { Str::kCardCastFireForget, "card_cast_fire_forget", "단발" },
        { Str::kCardCastConcentration, "card_cast_concentration", "집중" },
        { Str::kCardCastConstant, "card_cast_constant", "지속 효과" },
        { Str::kCardCastScroll, "card_cast_scroll", "마법 주문서" },
        { Str::kCardShout, "card_shout", "함성" },
        { Str::kCardShoutRecharge, "card_shout_recharge", "충전 {t}" },
        { Str::kCardPower, "card_power", "특수능력 · 하루 1회" },
        { Str::kCardLesserPower, "card_lesser_power", "하급 특수능력" },
        { Str::kCardAbility, "card_ability", "능력" },
        { Str::kCardPowerAny, "card_power_any", "특수능력" },
        { Str::kCardActiveEffect, "card_active_effect", "적용중인 효과" },
        { Str::kCardTimeLeft, "card_time_left", "남은 시간 {t}" },
        { Str::kCardHours, "card_hours", "{n}시간" },
        { Str::kCardMinutes, "card_minutes", "{n}분" },
        { Str::kCardSeconds, "card_seconds", "{n}초" },
        // the reset button right of the gear in the edit mode (user 2026-10-30), {key} the toggle key as the hints show keys
        { Str::kLayoutReset, "layout_reset", "처음 배치로" },
        // user 2026-10-30 (the board): a block's line while nothing is chosen
        { Str::kEmptyChoose, "empty_choose", "주문을 고르면 표시됩니다" },
        // user 2026-10-30: the gear's hover hint, out of the layout mode and in it
        { Str::kLayoutGearEnter, "layout_gear_enter", "편집 모드 {key}" },
        { Str::kLayoutGearLeave, "layout_gear_leave", "보기 모드로 {key}" },
    });

    consteval bool StringsInOrder()
    {
        for (std::size_t i = 0; i < kStrings.size(); ++i) {
            if (static_cast<std::size_t>(kStrings[i].id) != i) return false;
        }
        return kStrings.size() == static_cast<std::size_t>(Str::kCount);
    }
    static_assert(StringsInOrder(), "kStrings must list every Str once, in enum order");

    inline const char* Text(Str a_id)
    {
        return kStrings[static_cast<std::size_t>(a_id)].text;
    }

    // The table's text with each {placeholder} replaced (every occurrence)
    inline std::string Fill(Str a_id, std::initializer_list<std::pair<std::string_view, std::string>> a_values)
    {
        std::string text = Text(a_id);
        for (const auto& [name, value] : a_values) {
            for (auto at = text.find(name); at != std::string::npos; at = text.find(name, at + value.size())) {
                text.replace(at, name.size(), value);
            }
        }
        return text;
    }

    // A tree school key ("Destruction") as the list's school column names it
    inline std::string SchoolText(const std::string& a_school)
    {
        constexpr std::array<std::pair<std::string_view, Str>, 5> kSchools{ { { "Alteration", Str::kSchoolAlteration },
            { "Conjuration", Str::kSchoolConjuration }, { "Destruction", Str::kSchoolDestruction }, { "Illusion", Str::kSchoolIllusion },
            { "Restoration", Str::kSchoolRestoration } } };
        for (const auto& [name, id] : kSchools) {
            if (name == a_school) return Text(id);
        }
        return a_school;
    }

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
