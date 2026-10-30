#pragma once

#include "LangFiles.h"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

// =============================================================================
// WIDGET STRINGS - the magic menu's on-screen text table (out of WidgetText.h so
// tools/magic-menu-lang-check can read it: no game types). Each row's text is
// the built-in English, the last fallback; the shown text comes from
// Data/SKSE/Plugins/SL_MagicMenu/lang/<code>.json, then en.json (LangFiles.h,
// loaded once at kDataLoaded by Main.cpp). Placeholders in braces are filled by
// Fill(); {key} already carries its brackets ("[U]"). Wording: the reviewed
// drafts of 2026-10-31 (en.json / ko.json); the Korean, Gemini Argon's draft
// 2026-10-25 and the rows after it, lives in ko.json. Not passed to
// std::format: the braces are names, not format fields.
// =============================================================================

namespace WidgetText
{
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
        // 2026-10-31: the layout key's hint in SkyUI's key-hint strip (KeyHints.cpp)
        kHintLayout,
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
        { Str::kPreviewTitle, "preview_title", "Preview" },
        // our card's school and level line (the node card's "node_sub" until Task 5)
        { Str::kCardSub, "card_sub", "{school} · {level}" },
        { Str::kSchoolAlteration, "school_alteration", "Alteration" },
        { Str::kSchoolConjuration, "school_conjuration", "Conjuration" },
        { Str::kSchoolDestruction, "school_destruction", "Destruction" },
        { Str::kSchoolIllusion, "school_illusion", "Illusion" },
        { Str::kSchoolRestoration, "school_restoration", "Restoration" },
        // build 7 (spec v7 C1): our info card. DRAFT wording by Claude in the short label style, for Argon's review
        { Str::kCardMagicka, "card_magicka", "{n} Magicka" },
        { Str::kCardMagickaPerSec, "card_magicka_per_sec", "{n} Magicka/s" },
        { Str::kCardCastFireForget, "card_cast_fire_forget", "Fire and Forget" },
        { Str::kCardCastConcentration, "card_cast_concentration", "Concentration" },
        { Str::kCardCastConstant, "card_cast_constant", "Constant Effect" },
        { Str::kCardCastScroll, "card_cast_scroll", "Scroll" },
        { Str::kCardShout, "card_shout", "Shout" },
        { Str::kCardShoutRecharge, "card_shout_recharge", "Recharge: {t}" },
        { Str::kCardPower, "card_power", "Power · Once a day" },
        { Str::kCardLesserPower, "card_lesser_power", "Lesser Power" },
        { Str::kCardAbility, "card_ability", "Ability" },
        { Str::kCardPowerAny, "card_power_any", "Power" },
        { Str::kCardActiveEffect, "card_active_effect", "Active Effects" },
        { Str::kCardTimeLeft, "card_time_left", "Time left: {t}" },
        { Str::kCardHours, "card_hours", "{n}h" },
        { Str::kCardMinutes, "card_minutes", "{n}m" },
        { Str::kCardSeconds, "card_seconds", "{n}s" },
        // the reset button right of the gear in the edit mode (user 2026-10-30), {key} the toggle key as the hints show keys
        { Str::kLayoutReset, "layout_reset", "Reset layout" },
        // user 2026-10-30 (the board): a block's line while nothing is chosen
        { Str::kEmptyChoose, "empty_choose", "Select a spell to view" },
        // user 2026-10-30: the gear's hover hint, out of the layout mode and in it
        { Str::kLayoutGearEnter, "layout_gear_enter", "Edit mode {key}" },
        { Str::kLayoutGearLeave, "layout_gear_leave", "View mode {key}" },
        // 2026-10-31: the layout key's hint in SkyUI's key-hint strip (the key is its icon there)
        { Str::kHintLayout, "hint_layout", "Edit mode" },
    });

    consteval bool StringsInOrder()
    {
        for (std::size_t i = 0; i < kStrings.size(); ++i) {
            if (static_cast<std::size_t>(kStrings[i].id) != i) return false;
        }
        return kStrings.size() == static_cast<std::size_t>(Str::kCount);
    }
    static_assert(StringsInOrder(), "kStrings must list every Str once, in enum order");

    /// The language files' texts once Main.cpp loaded them (kDataLoaded), the built-in English before
    inline LangFiles::Catalog<StrEntry, kStrings.size()> g_catalog{ kStrings };

    /// The language folder under the game folder
    inline constexpr const char* kLangDir = "Data/SKSE/Plugins/SL_MagicMenu/lang";

    inline const char* Text(Str a_id)
    {
        return g_catalog.Text(static_cast<std::size_t>(a_id));
    }

    // The table's text with each {placeholder} replaced (every occurrence)
    inline std::string Fill(Str a_id, std::initializer_list<std::pair<std::string_view, std::string>> a_values)
    {
        return LangFiles::FillText(Text(a_id), a_values);
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
}
