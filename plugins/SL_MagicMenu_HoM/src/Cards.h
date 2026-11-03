#pragma once

#include "AddonState.h"
#include "Strings.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

// =============================================================================
// CARDS - the Heart of Magic block's callbacks: the learning progress card and node card
// (ProgressCard.cpp, NodeCard.cpp: the core's W:7119-7337 and W:7414-7603 at
// f83dc6f3, ported as unchanged as they could be), drawn by the host's
// drawSelection into the content clip it lends; the node card's set-target
// button and the progress card's next-spell rows (ProgressChoices.cpp, 2026-11-03)
// have their own AS2 handlers (kFlagOwnMouse).
// =============================================================================

namespace HomAddon
{
    /// A line of text with the table entry it came from (its key names the Q2 state in the log)
    struct Line
    {
        HomStrings::Str id;
        std::string text;
    };

    /// The progress card's base text sizes (the reading size is at least the list's row size): the school title, the hint lines,
    /// the next-spell rows
    inline constexpr double kProgressSchoolSize = 11.0, kProgressHintSize = 13.0;

    /// The progress block's registered height: the ini rect's a_iniH, or more for a_maxChoices next-spell rows and their "more" line at
    /// the fallback metrics (ChoiceRules::RegisteredHeight; ProgressChoices.cpp)
    double ProgressRegisteredHeight(double a_iniH, int a_maxChoices);

    /// Where the next-spell rows go in the progress card (card coordinates): its width, the top under the school title, its full
    /// height; the title shown, and how to drop it (it returns the new top)
    struct ChoicesArea
    {
        double w{}, y{}, fullH{};
        bool titleShown{};
        std::function<double(double)> dropTitle;
    };

    /// The next-spell rows in place of the "no learning target" line (ProgressChoices.cpp): a heading, the rows, an "N more" line;
    /// the card's needed height
    std::optional<double> DrawChoices(RE::GFxMovieView* a_movie, RE::GFxValue& a_card, const ChoicesArea& a_area, const SchoolInfo& a_info);

    /// The progress block's frame part of the rows (ProgressChoices.cpp): the column's room (the rows drawn again when it changed), the
    /// hover dropped in the layout mode, a row release judged against the visible part - the node to pick and set, -1 for none
    int ProgressChoicesFrame(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, const BlockApi::Context& a_context);

    /// The progress card's block: drawSelection (Block API v2/v3)
    void DrawProgressBlock(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float a_height, std::uint32_t a_shown, void* a_user);

    /// The node card's block: drawSelection
    void DrawNodeCardBlock(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float a_height, std::uint32_t a_shown, void* a_user);

    /// The node card's frame: the shared per-frame work when it is this frame's first HoM call (CardSharedFrame), then SelfTest L2
    /// node button bottom, once per menu, read back in the lent content clip
    void NodeCardFrame(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, RE::GFxValue* a_overlay, const BlockApi::Context& a_context, void* a_user);

    /// Fix round 1 I1: the progress card's frame - only the shared per-frame work, when it is this frame's first HoM call
    void ProgressFrame(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, RE::GFxValue* a_overlay, const BlockApi::Context& a_context, void* a_user);

    /// Fix round 1 I1 (Frame.cpp): a card's frame call does the three blocks' shared per-frame work (the tab, the selection, the
    /// button, the polling, the redraws) when it is the first HoM call of this host frame - the tree hidden by [Blocks] sHidden
    void CardSharedFrame(BlockKind a_kind, const BlockApi::Context& a_context);

    /// What to do next in the active school (Q2): the progress card's line, also named in the U key's log
    Line NextStepLine(const SchoolInfo& a_info, bool a_hasTarget);

    /// The node card's state words (NodeCard.cpp); on the button a target says only that it is the target. The tree's tooltip too
    Line StateLine(Spells::State a_state, float a_percent, bool a_button);

    // ---- the tree block: its window (TreeDraw.cpp); its frame (Frame.cpp) drives the three blocks' per-frame work while it shows
    // (a card's frame does without it), its key callback records U and G (the keys and their hints stay with the tree) ----

    void DrawTreeBlock(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float a_height, std::uint32_t a_shown, void* a_user);
    void TreeFrame(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, RE::GFxValue* a_overlay, const BlockApi::Context& a_context, void* a_user);
    bool TreeKey(std::uint32_t a_scanCode, void* a_user);
}
