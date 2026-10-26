#pragma once

// =============================================================================
// LAYOUT - the block layout of the magic menu's panels (spec 2026-10-26 blocks):
// two snapping columns, the saved order and column border, their JSON. Pure:
// no GFx, no game calls, no logger (Settings.h only for Rect / Values), so the
// offline check tool tools/magic-menu-layout-check links it too.
//
// Coordinates are stage units (1280 x 720), UNSCROLLED (plan D4): a column's
// scroll is applied once, by the caller, on that column's clip. A pointer y
// handed to SnapSlot must be in the same unscrolled space (stage y + scroll).
// =============================================================================

#include "Settings.h"

#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Layout
{
    using BlockId = std::string;
    inline constexpr const char* kPreview = "preview";
    inline constexpr const char* kTree = "tree";
    inline constexpr const char* kProgress = "progress";
    inline constexpr const char* kSpellCard = "spellcard";
    inline constexpr const char* kNodeCard = "nodecard";

    inline constexpr int kColumns = 2;
    inline constexpr int kJsonVersion = 1;
    // the narrowest a column may get: the tree legend and the cards' text stay readable (spec "Column border limits")
    inline constexpr double kMinColumn = 240.0;

    /// The saved layout. Each column lists block ids top to bottom; it may hold hidden blocks and ids of absent add-ons
    /// (kept in place, skipped when arranged). border = the left column's right edge; the right column starts at
    /// border + gap.
    struct State
    {
        std::array<std::vector<BlockId>, kColumns> columns;
        double border{};

        bool operator==(const State&) const = default;
    };

    /// One column's area. Both columns use the same frame today (plan D5: 648..1238, 67..623); left / right are the
    /// outer edges of the whole two-column area: column 0 spans left..border, column 1 border + gap..right.
    /// The horizontal gap between the columns is frames[0].gap; each frame's gap is its vertical gap between blocks.
    struct Frame
    {
        double left{}, right{}, top{}, bottom{}, gap{}, minColumn{};

        [[nodiscard]] double Height() const { return bottom - top; }
    };

    /// Where a dropped block goes: the column, and the visible block it goes before (nullopt = the end of the column).
    struct Slot
    {
        int column{};
        std::optional<BlockId> before;

        bool operator==(const Slot&) const = default;
    };

    /// The default layout (= layout E2): {preview, progress, spellcard} / {tree, nodecard}, border = the preview's right edge.
    State Default(const Settings::Values& a_values);

    /// Both columns' frames from the ini rects: preview.left .. tree.right, preview.top .. itemCard.bottom, panelGap, kMinColumn.
    std::array<Frame, kColumns> DefaultFrames(const Settings::Values& a_values);

    /// The five blocks' heights from the ini rects (block heights never change, only widths).
    std::map<BlockId, double> DefaultHeights(const Settings::Values& a_values);

    /// The border clamped so both columns are at least minColumn wide. If the frames cannot fit two such columns,
    /// the midpoint of the impossible range (Settings clamps panelGap so this does not happen with 240-wide columns).
    double ClampBorder(double a_x, const Frame& a_left, const Frame& a_right);

    /// Block rects (unscrolled), stacked from each frame's top with the gap between them; the border is clamped first.
    /// Hidden blocks and blocks with no entry in a_heights (absent add-ons) get no rect and no gap. A block in a_fill
    /// takes the rest of its column (frame height minus the other shown blocks and gaps), never less than its own
    /// height; with two fill blocks in one column only the first one grows.
    std::map<BlockId, Settings::Rect> Arrange(const State& a_state, const std::map<BlockId, double>& a_heights, const std::set<BlockId>& a_hidden,
        const std::set<BlockId>& a_fill, const std::array<Frame, kColumns>& a_frames);

    /// The column's area (unscrolled stage units): its x span as Arrange gives its blocks (column 0 frame.left .. border,
    /// column 1 border + gap .. frame.right, border clamped as Arrange does) and the frame's top .. bottom. This is the
    /// column's viewport: what of the column shows, its mask. A column out of range gets an empty rect.
    Settings::Rect ColumnArea(const State& a_state, int a_column, const std::array<Frame, kColumns>& a_frames);

    /// The part of a_box inside a_viewport; nullopt when nothing is (no overlap, only an edge or a corner in common, or a
    /// non-finite edge). Blocks plan Task 3: the 3D preview's scissor is its box cut to its column's viewport.
    std::optional<Settings::Rect> VisibleBox(const Settings::Rect& a_box, const Settings::Rect& a_viewport);

    /// Each column's content height: the lowest arranged bottom minus the frame top (0 for an empty column).
    std::array<double, kColumns> ContentHeight(const State& a_state, const std::map<BlockId, Settings::Rect>& a_arranged,
        const std::array<Frame, kColumns>& a_frames);

    /// A scroll offset kept in 0 .. content - visible (0 when everything fits, or for a non-finite offset).
    double ClampScroll(double a_offset, double a_content, double a_visible);

    /// The slot under the pointer (unscrolled stage units): column = x < border + gap / 2 ? 0 : 1 (border clamped as
    /// Arrange does); before = the first arranged block of that column, the dragged one left out, whose centre is below y.
    Slot SnapSlot(const State& a_state, const BlockId& a_dragged, double a_x, double a_y, const std::map<BlockId, Settings::Rect>& a_arranged,
        const std::array<Frame, kColumns>& a_frames);

    /// The block taken out of its place and put right before a_slot.before in the saved array of a_slot.column (after any
    /// hidden or absent ids standing in front of it), or at the column's end when before is nullopt or not in that column.
    /// Unchanged when the block is not in the layout, before is the block itself, or the column is out of range.
    State Move(State a_state, const BlockId& a_block, const Slot& a_slot);

    /// The layout as JSON: {"version":1,"columns":[[...],[...]],"border":969}.
    std::string ToJson(const State& a_state);

    /// Parses ToJson's text. nullopt with a reason in a_why when it is not JSON, a field has the wrong type or the
    /// version is not one this build reads. A missing "version" counts as 1; a missing "border" comes back as NaN
    /// (Repair puts the fallback's in). *a_newerVersion is set true when the file is from a newer build
    /// ("unsupported version"): the caller must then leave the file alone instead of writing over it.
    std::optional<State> FromJson(std::string_view a_text, std::string* a_why, bool* a_newerVersion = nullptr);

    /// Drops empty and repeated ids (the first place wins), appends each known block that is missing to its column in
    /// a_fallback (column 0 when the fallback lacks it too), keeps unknown ids in place (absent add-ons), and takes the
    /// fallback's border when the border is not finite. Border range clamping is ClampBorder's (Arrange applies it).
    State Repair(State a_state, const std::set<BlockId>& a_known, const State& a_fallback);
}
