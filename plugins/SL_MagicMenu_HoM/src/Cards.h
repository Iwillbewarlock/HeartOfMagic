#pragma once

#include "AddonState.h"
#include "Strings.h"

#include <cstdint>
#include <string>

// =============================================================================
// CARDS - the Heart of Magic block's callbacks: the learning progress card and node card
// (ProgressCard.cpp, NodeCard.cpp: the core's W:7119-7337 and W:7414-7603 at
// f83dc6f3, ported as unchanged as they could be), drawn by the host's
// drawSelection into the content clip it lends; the node card's set-target
// button has its own AS2 handler (kFlagOwnMouse).
// =============================================================================

namespace HomAddon
{
    /// A line of text with the table entry it came from (its key names the Q2 state in the log)
    struct Line
    {
        HomStrings::Str id;
        std::string text;
    };

    /// The progress card's block: drawSelection (Block API v2/v3)
    void DrawProgressBlock(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float a_height, std::uint32_t a_shown, void* a_user);

    /// The node card's block: drawSelection
    void DrawNodeCardBlock(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float a_height, std::uint32_t a_shown, void* a_user);

    /// The node card's frame: SelfTest L2 node button bottom, once per menu, read back in the lent content clip
    void NodeCardFrame(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, RE::GFxValue* a_overlay, const BlockApi::Context& a_context, void* a_user);

    /// What to do next in the active school (Q2): the progress card's line, also named in the U key's log
    Line NextStepLine(const SchoolInfo& a_info, bool a_hasTarget);

    /// The node card's state words (NodeCard.cpp); on the button a target says only that it is the target. The tree's tooltip too
    Line StateLine(Spells::State a_state, float a_percent, bool a_button);

    // ---- the tree block: its window (TreeDraw.cpp); its frame (Frame.cpp) drives the three blocks' per-frame work, its key
    // callback records U and G ----

    void DrawTreeBlock(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float a_height, std::uint32_t a_shown, void* a_user);
    void TreeFrame(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, RE::GFxValue* a_overlay, const BlockApi::Context& a_context, void* a_user);
    bool TreeKey(std::uint32_t a_scanCode, void* a_user);
}
