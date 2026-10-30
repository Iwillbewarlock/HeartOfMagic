#pragma once

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
// table at f83dc6f3 (Widget.cpp W:227-400, spec v5 Q2), copied unchanged (HoM
// add-on plan Task 6), the five school names (the core keeps its own copy for its
// card) and the tree's empty line. Korean (the user plays in Korean).
// Placeholders in braces are filled by Fill(); {key} already carries its brackets
// ("[U]"). Wording: Gemini Argon's draft 2026-10-25 with two lines tightened by
// Claude (spec v5 table); the rows after "build 5" were written in the same
// short label style. Not passed to std::format: the braces are names, not format
// fields. No game types (tools/magic-menu-hom-block-check includes it).
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
        { Str::kProgressTitle, "progress_title", "{school} 학습" },
        { Str::kProgressTarget, "progress_target", "학습 목표: {spell} ({pct}%)" },
        { Str::kProgressNoneCandidates, "progress_none_candidates", "학습 목표 없음 · 배울 수 있는 주문 {n}개 · {key} 다음 주문" },
        { Str::kProgressNoneNothing, "progress_none_nothing", "배울 수 있는 주문 없음 (선행 조건 미충족)" },
        { Str::kProgressAllMastered, "progress_all_mastered", "모든 주문 숙달 완료" },
        { Str::kProgressNotReady, "progress_not_ready", "주문 데이터 불러오는 중..." },
        { Str::kProgressNotInTree, "progress_not_in_tree", "선택한 주문은 주문 트리에 없음" },
        { Str::kNodeHint, "node_hint", "트리에서 주문 클릭 · {key} 다음 주문" },
        { Str::kNodeSetTarget, "node_set_target", "학습 목표 지정 {key}" },
        { Str::kNodeIsTarget, "node_is_target", "현재 학습 목표" },
        { Str::kNodeLocked, "node_locked", "선행 조건 필요" },
        { Str::kNodeMastered, "node_mastered", "숙달 완료" },
        { Str::kLegendMastered, "legend_mastered", "숙달" },
        { Str::kLegendLearning, "legend_learning", "학습 중" },
        { Str::kLegendCanLearn, "legend_can_learn", "배울 수 있음" },
        { Str::kTreeNotInTree, "tree_not_in_tree", "주문 트리에 없는 주문" },
        { Str::kTreeTitle, "tree_title", "주문 트리" },
        // build 5
        { Str::kProgressCandidates, "progress_candidates", "배울 수 있는 주문 {n}개 · {key} 다음 주문" },
        { Str::kProgressCandidatesNoXY, "progress_candidates_no_xy", "배울 수 있는 주문 {n}개 · 트리 창에 표시 안 됨" },
        { Str::kProgressReady, "progress_ready", "경험치 충족된 주문 {n}개 · 습득 대기" },
        { Str::kProgressLearningEarly, "progress_learning_early", "미리 배운 주문 {n}개 · 숙달 진행 중" },
        { Str::kProgressUnknown, "progress_unknown", "상태를 알 수 없는 주문 {n}개" },
        { Str::kProgressNoOther, "progress_no_other", "배울 수 있는 다른 주문 없음" },
        { Str::kProgressNoTree, "progress_no_tree", "주문 트리 파일 없음" },
        { Str::kProgressNoSchoolTree, "progress_no_school_tree", "{school}: 주문 트리에 없는 학파" },
        { Str::kXp, "xp", "{cur} / {need} 경험치" },
        { Str::kTreeNoTree, "tree_no_tree", "주문 트리 파일 없음 (spell_tree.json)" },
        { Str::kTreeNoCentre, "tree_no_centre", "선택한 학파 주문 없음" },
        { Str::kTreeNoLinks, "tree_no_links", "연결된 주문 없음" },
        // spec v10 T7: an off-window badge (a drawn arrow, then this text)
        { Str::kTreeExit, "tree_exit", "{n}개 더" },
        { Str::kNodeCanLearn, "node_can_learn", "배울 수 있음" },
        { Str::kNodeTargetPct, "node_target_pct", "현재 학습 목표 · {pct}%" },
        { Str::kNodeLearningPct, "node_learning_pct", "미리 배움 · 숙달 {pct}%" },
        { Str::kNodeReady, "node_ready", "경험치 충족 · 습득 대기" },
        { Str::kNodeUnknown, "node_unknown", "상태 확인 중" },
        { Str::kNodeSub, "node_sub", "{school} · {level}" },
        { Str::kReqAll, "req_all", "선행 조건 모두 숙달 필요" },
        { Str::kReqSome, "req_some", "선행 조건 중 {n}개 숙달 필요" },
        { Str::kReqNone, "req_none", "선행 조건 없음" },
        { Str::kReqMastered, "req_mastered", "{name} · 숙달" },
        { Str::kReqNotMastered, "req_not_mastered", "{name} · 미숙달" },
        { Str::kReqChecking, "req_checking", "{name} · 확인 중" },
        { Str::kReqNotInGame, "req_not_in_game", "{name} · 게임에 없음" },
        { Str::kReqMore, "req_more", "외 {n}개" },
        { Str::kSchoolAlteration, "school_alteration", "변화마법" },
        { Str::kSchoolConjuration, "school_conjuration", "소환마법" },
        { Str::kSchoolDestruction, "school_destruction", "파괴마법" },
        { Str::kSchoolIllusion, "school_illusion", "환영마법" },
        { Str::kSchoolRestoration, "school_restoration", "회복마법" },
        // review round 1 #3
        { Str::kProgressNoHom, "progress_no_hom", "Heart of Magic 연결 안 됨" },
        { Str::kProgressNoData, "progress_no_data", "주문 데이터를 받지 못함" },
        // build 6 (spec v6 L4); since build 11 the tree legend's locked item (spec v11 T18)
        { Str::kTreeLocked, "tree_locked", "잠김" },
        // user 2026-10-30 (the board): a block's line while nothing is chosen
        { Str::kEmptyChoose, "empty_choose", "주문을 고르면 표시됩니다" },
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

    inline const char* Key(Str a_id)
    {
        return kStrings[static_cast<std::size_t>(a_id)].key;
    }

    /// The table's text with each {placeholder} replaced (every occurrence)
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
