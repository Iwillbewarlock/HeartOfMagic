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
#include <cstdint>
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
    // [Debug] bDebugFillerBlock's test add-on (blocks plan Task 6): placed in the left column after the spell card, so the
    // preview's column scrolls (the RF3 test path; final review ruling)
    inline constexpr const char* kDebugFillerId = "SL_MagicMenu.filler";

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
    /// Hidden blocks and blocks with no entry in a_heights (absent add-ons) get no rect and no gap; so does a block whose
    /// height is not finite or negative (it cannot move the others). A block in a_fill
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

    /// The insertion line's y for a_slot (unscrolled): half a gap above the block it goes before, else half a gap below the
    /// column's lowest arranged block (a_dragged left out), else the column's top. A column out of range: frames[0].top.
    double SlotLineY(const State& a_state, const Slot& a_slot, const BlockId& a_dragged, const std::map<BlockId, Settings::Rect>& a_arranged,
        const std::array<Frame, kColumns>& a_frames);

    /// The scroll that keeps a_top..a_bottom (unscrolled) in a column whose viewport starts at a_viewTop and is a_visible high:
    /// unchanged when it is in view already (a non-finite scroll counts as 0); a span taller than the viewport shows its top; clamped
    /// by ClampScroll.
    double ScrollToShow(double a_scroll, double a_top, double a_bottom, double a_viewTop, double a_visible, double a_content);

    // -------------------------------------------------------------------------
    // The gamepad / keyboard focus model of the layout mode (blocks plan Task 5)
    // -------------------------------------------------------------------------

    /// LB / RB, [ / ] (PageUp / PageDown) move the column border by this (stage units), through ClampBorder
    inline constexpr double kBorderStep = 10.0;

    /// One key of the layout mode: D-pad / arrows, A / Enter, B / Esc, LB / RB
    enum class Nav
    {
        kUp,
        kDown,
        kLeft,
        kRight,
        kAccept,       // pick the focused block up / drop the picked one
        kCancel,       // drop the pick where it was / leave the mode (cancel)
        kBorderLeft,   // the border kBorderStep to the left
        kBorderRight,
    };

    /// The focused block, and while it is picked up the slot it would be dropped at (the block stays in its place until then:
    /// the insertion line shows the target)
    struct Focus
    {
        std::optional<BlockId> focus;
        std::optional<Slot> target;  // set = the focused block is picked

        [[nodiscard]] bool Picked() const { return focus.has_value() && target.has_value(); }
        bool operator==(const Focus&) const = default;
    };

    enum class NavEffect
    {
        kIgnored,         // not in the mode: nothing changes
        kNone,            // at an edge, nothing to focus or move: nothing changes
        kFocused,         // the focus moved (or was given to the first block)
        kPicked,          // the focused block picked up (target = its own place)
        kTargeted,        // the picked block's target slot moved
        kDropped,         // dropped: the layout changed (Move)
        kDroppedInPlace,  // dropped at its own place: the layout is the same
        kPickCancelled,   // B / Esc with a pick: nothing moved
        kLeave,           // B / Esc with no pick: the caller leaves the mode as Cancel
        kBorder,          // the border moved
    };

    struct NavResult
    {
        Focus focus;
        State state;
        NavEffect effect{};
    };

    /// The first visible block (column 0 top to bottom, then column 1); nullopt when nothing is visible.
    std::optional<BlockId> FirstFocus(const State& a_state, const std::set<BlockId>& a_visible);

    /// The block's own place as a slot: its column, before the next visible block below it (nullopt: none). nullopt when the
    /// block is not in the layout.
    std::optional<Slot> SlotOf(const State& a_state, const BlockId& a_block, const std::set<BlockId>& a_visible);

    /// The focus made valid: an unknown block (not in the layout) clears it; a block no longer visible moves the focus to the
    /// nearest visible one in its column (below first, then above), else clears it - and a pick ends then; a target column out
    /// of range ends the pick; a target before a block that is not visible (or the picked one) aims at the column's end.
    Focus Normalize(const State& a_state, const Focus& a_focus, const std::set<BlockId>& a_visible);

    /// One key in the layout mode (a_modeOn false: kIgnored, nothing changes). The focus is normalized first; with no focus a
    /// direction or A gives it to FirstFocus. Up / Down: the visible block above / below in its column (no wrap). Left / Right:
    /// the visible block of the other column nearest by centre y (the upper on a tie). Picked: Up / Down move the target one
    /// slot in its column, Left / Right to the other column before its first visible block whose centre is below the target
    /// line (SlotLineY); A drops (Move; the own place is kDroppedInPlace, no Move), B cancels the pick. LB / RB: the border
    /// (picked or not). a_arranged: this tab's unscrolled rects (Arrange). O(blocks) per key.
    NavResult Navigate(const State& a_state, const Focus& a_focus, Nav a_nav, bool a_modeOn, const std::set<BlockId>& a_visible,
        const std::map<BlockId, Settings::Rect>& a_arranged, const std::array<Frame, kColumns>& a_frames);

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

    /// a_ids appended in order to the end of a_column (the right column by default: add-on blocks, blocks plan Task 6), each
    /// one only when it is in no column yet (a saved place wins) and not empty. A column out of range: unchanged.
    State AddBlocks(State a_state, const std::vector<BlockId>& a_ids, int a_column = 1);

    /// The add-on blocks put into a default layout (Settings::AddBlocks): each at the end of the right column, except
    /// kDebugFillerId, which goes to the end of the left one (after the spell card: the preview's column then scrolls)
    State AddAddons(State a_default, const std::vector<BlockId>& a_ids);

    // -------------------------------------------------------------------------
    // The layout file's safety (blocks plan D10, Task 4): which files a save writes and keeps
    // -------------------------------------------------------------------------

    /// What the layout file was when it was last read or written
    enum class FileState
    {
        kMissing,  // no file (the first save creates it)
        kRead,     // read as a layout (or written by this build)
        kBroken,   // there but not readable as a layout (not JSON, bad fields, too big, unreadable): kept as .bad before a save
        kNewer,    // from a newer build: never overwritten
    };

    /// What a save does. Order: refuse -> copy the file to .bad -> write .tmp -> replace (the old file kept as .bak) or move
    struct SavePlan
    {
        bool refuse = false;        // nothing written (a newer build's file)
        bool copyBadFirst = false;  // the unreadable file copied to <file>.bad first; the save fails if that copy fails
        bool replace = false;       // the file exists: ReplaceFileW keeping <file>.bak (fallback: copy to .bak + MoveFileExW);
                                    // false: no file yet, MoveFileExW of the .tmp alone (ReplaceFileW needs an existing target)

        bool operator==(const SavePlan&) const = default;
    };

    /// The plan for a save: a_state as last read/written, a_badKept = the .bad copy was made already (a retry does not copy it
    /// again), a_fileExists = the layout file is there now.
    SavePlan PlanSave(FileState a_state, bool a_badKept, bool a_fileExists);

    /// The file's state after a save attempt: kRead after a successful write; unchanged otherwise (a failed save leaves the
    /// file as it was and the next try plans the same).
    FileState AfterSave(FileState a_state, bool a_written);

    /// After the .tmp could not be put in the file's place (final review): what the save does with what is on disk. A replace
    /// can move the old file to .bak and then fail to move the new one in (ReplaceFileW's ERROR_UNABLE_TO_MOVE_REPLACEMENT_2,
    /// 1177), and the MoveFileExW fallback can fail too: the layout file is then gone, the user's layout only in .bak
    enum class FailedPut
    {
        kFileThere,   // the layout file is in place (as it was): the .tmp is removed
        kRestoreBak,  // no file, the replace left the old one as .bak: .bak moved back to the file's name
        kNoFile,      // no file and nothing to restore (a first save): the .tmp is removed
    };

    /// a_replace: the save planned a replace (there was a file); a_fileThere / a_bakThere: the layout file and its .bak exist now
    FailedPut AfterFailedPut(bool a_replace, bool a_fileThere, bool a_bakThere);

    /// The .tmp is kept (not removed) after a failed save: only when .bak had to be moved back and that failed too - the .tmp
    /// then holds the edited layout and .bak the old one, and the log names both
    bool KeepTmp(FailedPut a_put, bool a_restored);

    /// LoadLayout reads <file>.bak instead: the layout file is missing and a .bak is there (a save that lost the file between
    /// its replace and its move; final review)
    bool ReadBackup(FileState a_file, bool a_bakThere);

    // -------------------------------------------------------------------------
    // The layout mode's on/off as the input sees it (final review: a quick toggle off/on queued Save then Enter, and the
    // Save's leave set the flag off after the Enter's toggle had set it on)
    // -------------------------------------------------------------------------

    /// Every change of intent (a toggle, a cancel, a frame-side leave or entry) takes the next sequence number and sets the flag
    /// at once (later events of the hook's batch see it); the frame handler's result for a request is the source of truth, but
    /// it is written only while no newer request was made, so an older request never overwrites a newer state. The caller
    /// keeps it under one lock
    struct ModeLedger
    {
        std::uint64_t latest = 0;
        bool on = false;

        /// A new intent: the flag set to a_on; the request's number returned
        std::uint64_t Request(bool a_on)
        {
            on = a_on;
            return ++latest;
        }

        /// The frame handler's result for request a_seq (whether the mode is on after it); ignored when a newer one was made
        void Settle(std::uint64_t a_seq, bool a_result)
        {
            if (a_seq == latest) on = a_result;
        }
    };
}
