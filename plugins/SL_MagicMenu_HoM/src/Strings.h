#pragma once

#include "LangFiles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

// =============================================================================
// STRINGS - the Heart of Magic block's on-screen text: the HoM rows of the core's
// table at f83dc6f3 (Widget.cpp W:227-400, spec v5 Q2, HoM add-on plan Task 6),
// the five school names (the core keeps its own copy for its card) and the
// tree's empty line. Each row's text is the built-in English, the last
// fallback; the shown text comes from
// Data/SKSE/Plugins/SL_MagicMenu_HoM/lang/<code>.json, then en.json (the core's
// LangFiles.h, loaded once at kDataLoaded by Main.cpp). Wording: the reviewed
// drafts of 2026-10-31 (en.json / ko.json); the Korean, Gemini Argon's draft
// 2026-10-25 and the rows after it, lives in ko.json. Placeholders in braces
// are filled by Fill(); {key} already carries its brackets ("[U]"). Not passed
// to std::format: the braces are names, not format fields. No game types
// (tools/magic-menu-hom-block-check and magic-menu-lang-check include it).
// =============================================================================

namespace HomStrings
{
    enum class Str : std::size_t
    {
        kProgressTitle,
        kProgressTarget,
        kProgressNoneCandidates,
        kProgressNoneNothing,
        kProgressAllMastered,
        kProgressNotReady,
        kProgressNotInTree,
        kNodeHint,
        kNodeSetTarget,
        kNodeIsTarget,
        kNodeLocked,
        kNodeMastered,
        kLegendMastered,
        kLegendLearning,
        kLegendCanLearn,
        kTreeNotInTree,
        kTreeTitle,
        // build 5
        kProgressCandidates,
        kProgressCandidatesNoXY,
        kProgressReady,
        kProgressLearningEarly,
        kProgressUnknown,
        kProgressNoOther,
        kProgressNoTree,
        kProgressNoSchoolTree,
        kXp,
        kTreeNoTree,
        kTreeNoCentre,
        kTreeNoLinks,
        // spec v10 T7: an off-window badge (a drawn arrow, then this text)
        kTreeExit,
        kNodeCanLearn,
        kNodeTargetPct,
        kNodeLearningPct,
        kNodeReady,
        kNodeUnknown,
        kNodeSub,
        kReqAll,
        kReqSome,
        kReqNone,
        kReqMastered,
        kReqNotMastered,
        kReqChecking,
        kReqNotInGame,
        kReqMore,
        kSchoolAlteration,
        kSchoolConjuration,
        kSchoolDestruction,
        kSchoolIllusion,
        kSchoolRestoration,
        // review round 1 #3
        kProgressNoHom,
        kProgressNoData,
        // build 6 (spec v6 L4); since build 11 the tree legend's locked item (spec v11 T18)
        kTreeLocked,
        // user 2026-10-30 (the board): a block's line while nothing is chosen
        kEmptyChoose,
        kCount
    };

    struct StrEntry
    {
        Str id;
        const char* key;   // the name in logs ("SelfTest: Q2 state <key>") and in the spec's table
        const char* text;
    };

    inline constexpr auto kStrings = std::to_array<StrEntry>({
        { Str::kProgressTitle, "progress_title", "{school} progress" },
        { Str::kProgressTarget, "progress_target", "Learning target: {spell} ({pct}%)" },
        { Str::kProgressNoneCandidates, "progress_none_candidates", "No learning target · {n} can be learned · {key} Next spell" },
        { Str::kProgressNoneNothing, "progress_none_nothing", "No spells to learn (prerequisites unmet)" },
        { Str::kProgressAllMastered, "progress_all_mastered", "All spells mastered" },
        { Str::kProgressNotReady, "progress_not_ready", "Loading spell data..." },
        { Str::kProgressNotInTree, "progress_not_in_tree", "Selected spell not in spell tree" },
        { Str::kNodeHint, "node_hint", "Click a spell in the tree · {key} Next spell" },
        { Str::kNodeSetTarget, "node_set_target", "Set learning target {key}" },
        { Str::kNodeIsTarget, "node_is_target", "Current learning target" },
        { Str::kNodeLocked, "node_locked", "Prerequisites needed" },
        { Str::kNodeMastered, "node_mastered", "Mastered" },
        { Str::kLegendMastered, "legend_mastered", "Mastered" },
        { Str::kLegendLearning, "legend_learning", "Learning" },
        { Str::kLegendCanLearn, "legend_can_learn", "Can learn" },
        { Str::kTreeNotInTree, "tree_not_in_tree", "Spell not in spell tree" },
        { Str::kTreeTitle, "tree_title", "Spell tree" },
        // build 5
        { Str::kProgressCandidates, "progress_candidates", "{n} can be learned · {key} Next spell" },
        { Str::kProgressCandidatesNoXY, "progress_candidates_no_xy", "{n} can be learned · Not shown in tree" },
        { Str::kProgressReady, "progress_ready", "{n} at full XP · Ready to learn" },
        { Str::kProgressLearningEarly, "progress_learning_early", "{n} learned early · Mastery in progress" },
        { Str::kProgressUnknown, "progress_unknown", "{n} with unknown status" },
        { Str::kProgressNoOther, "progress_no_other", "No other spells to learn" },
        { Str::kProgressNoTree, "progress_no_tree", "No spell tree file" },
        { Str::kProgressNoSchoolTree, "progress_no_school_tree", "{school} not in spell tree" },
        { Str::kXp, "xp", "{cur} / {need} XP" },
        { Str::kTreeNoTree, "tree_no_tree", "No spell tree file (spell_tree.json)" },
        { Str::kTreeNoCentre, "tree_no_centre", "No spells in selected school" },
        { Str::kTreeNoLinks, "tree_no_links", "No linked spells" },
        // spec v10 T7: an off-window badge (a drawn arrow, then this text)
        { Str::kTreeExit, "tree_exit", "{n} more" },
        { Str::kNodeCanLearn, "node_can_learn", "Can learn" },
        { Str::kNodeTargetPct, "node_target_pct", "Learning target · {pct}%" },
        { Str::kNodeLearningPct, "node_learning_pct", "Learned early · {pct}% mastered" },
        { Str::kNodeReady, "node_ready", "XP full · Ready to learn" },
        { Str::kNodeUnknown, "node_unknown", "Checking status" },
        { Str::kNodeSub, "node_sub", "{school} · {level}" },
        { Str::kReqAll, "req_all", "Master all prerequisites" },
        { Str::kReqSome, "req_some", "Master {n} of the prerequisites" },
        { Str::kReqNone, "req_none", "No prerequisites" },
        { Str::kReqMastered, "req_mastered", "{name} · Mastered" },
        { Str::kReqNotMastered, "req_not_mastered", "{name} · Not mastered" },
        { Str::kReqChecking, "req_checking", "{name} · Checking" },
        { Str::kReqNotInGame, "req_not_in_game", "{name} · Not in game" },
        { Str::kReqMore, "req_more", "+{n} more" },
        { Str::kSchoolAlteration, "school_alteration", "Alteration" },
        { Str::kSchoolConjuration, "school_conjuration", "Conjuration" },
        { Str::kSchoolDestruction, "school_destruction", "Destruction" },
        { Str::kSchoolIllusion, "school_illusion", "Illusion" },
        { Str::kSchoolRestoration, "school_restoration", "Restoration" },
        // review round 1 #3
        { Str::kProgressNoHom, "progress_no_hom", "Heart of Magic not connected" },
        { Str::kProgressNoData, "progress_no_data", "No spell data received" },
        // build 6 (spec v6 L4); since build 11 the tree legend's locked item (spec v11 T18)
        { Str::kTreeLocked, "tree_locked", "Locked" },
        // user 2026-10-30 (the board): a block's line while nothing is chosen
        { Str::kEmptyChoose, "empty_choose", "Select a spell to view" },
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
    inline constexpr const char* kLangDir = "Data/SKSE/Plugins/SL_MagicMenu_HoM/lang";

    inline const char* Text(Str a_id)
    {
        return g_catalog.Text(static_cast<std::size_t>(a_id));
    }

    inline const char* Key(Str a_id)
    {
        return kStrings[static_cast<std::size_t>(a_id)].key;
    }

    /// The table's text with each {placeholder} replaced (every occurrence)
    inline std::string Fill(Str a_id, std::initializer_list<std::pair<std::string_view, std::string>> a_values)
    {
        return LangFiles::FillText(Text(a_id), a_values);
    }

    /// A tree school key ("Destruction") as the list's school column names it
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

    inline std::string KeyName(std::uint32_t a_scanCode)
    {
        // DirectInput scan codes of the letter rows
        constexpr std::string_view kRow1 = "QWERTYUIOP", kRow2 = "ASDFGHJKL", kRow3 = "ZXCVBNM";
        constexpr std::uint32_t kRow1Start = 16, kRow2Start = 30, kRow3Start = 44;
        if (a_scanCode >= kRow1Start && a_scanCode < kRow1Start + kRow1.size()) return std::string(1, kRow1[a_scanCode - kRow1Start]);
        if (a_scanCode >= kRow2Start && a_scanCode < kRow2Start + kRow2.size()) return std::string(1, kRow2[a_scanCode - kRow2Start]);
        if (a_scanCode >= kRow3Start && a_scanCode < kRow3Start + kRow3.size()) return std::string(1, kRow3[a_scanCode - kRow3Start]);
        return std::format("#{}", a_scanCode);
    }

    /// The key as the hints show it: its name in brackets ("[U]")
    inline std::string KeyLabel(std::uint32_t a_scanCode)
    {
        return "[" + KeyName(a_scanCode) + "]";
    }
}
