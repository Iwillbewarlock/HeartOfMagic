#pragma once

#include "BlockApi.h"
#include "Layout.h"
#include "Settings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

// =============================================================================
// BLOCK RULES - the pure parts of the Block API v2 host side (Spell Research
// add-on design 2026-10-27, "Changes / 1"): which SkyUI category tab is which
// BlockApi tab bit, whether a block shows on a tab, whether a press and its
// release in an add-on block become a click call, and the redraw / height
// requests (kMessageRedraw, kMessageHeight): reading, judging, queueing. No game types: Widget.cpp
// feeds it the values it reads; tools/magic-menu-block-api-check runs these
// same functions on made-up cases. Stage units, y down.
// =============================================================================

namespace BlockRules
{
    // SkyUI's magic menu category flags (the category entry's flag, FILTERFLAG_MAGIC_*): the five schools, Shouts, Powers and
    // Active Effects. All (-257), Favorites (1) and any other value are none of these
    inline constexpr int kFlagAlteration = 2, kFlagIllusion = 4, kFlagDestruction = 8, kFlagConjuration = 16, kFlagRestoration = 32,
                         kFlagShouts = 64, kFlagPowers = 128, kFlagActiveEffects = 256;

    /// The BlockApi tab bit of a category flag: Shouts, Powers and Active Effects their own; every other tab (All, Favorites,
    /// the school tabs, a category SL_MagicMenu does not know) is a spell tab - the tabs it draws its spell tree on
    constexpr std::uint32_t TabBit(int a_flag)
    {
        switch (a_flag) {
        case kFlagShouts: return BlockApi::kTabShouts;
        case kFlagPowers: return BlockApi::kTabPowers;
        case kFlagActiveEffects: return BlockApi::kTabActiveEffects;
        default: return BlockApi::kTabSpells;
        }
    }

    // The longest final id the registry can make: an unprefixed id of kMaxIdLength gets "<sender>." in front, and an SKSE
    // plugin's name is at most 255 characters (CommonLibSSE-NG SKSE/Interfaces.h PluginDeclaration::pluginName[256]; a
    // legacy SKSEPlugin_Query name has no such bound - such a plugin with a longer name cannot ask for a redraw)
    inline constexpr std::size_t kMaxPluginNameLength = 255;
    inline constexpr std::size_t kMaxFinalIdLength = kMaxPluginNameLength + 1 + BlockApi::kMaxIdLength;

    /// The block id of a kMessageRedraw message: its NUL-terminated text read no further than a_dataLen; nullopt when there
    /// is none (null data, empty) or it runs past kMaxFinalIdLength. Whether it names a block is the registry's to say
    inline std::optional<std::string> RedrawId(const void* a_data, std::uint32_t a_dataLen)
    {
        const auto* text = static_cast<const char*>(a_data);
        if (!text) return std::nullopt;
        const std::size_t length = strnlen(text, std::min<std::size_t>(a_dataLen, kMaxFinalIdLength + 1));
        if (length == 0 || length > kMaxFinalIdLength) return std::nullopt;
        return std::string(text, length);
    }

    // ---------------------------------------------------------------------
    // kMessageHeight ('SLMH'): an add-on's height for its block this menu (BlockApi.h "kMessageHeight")
    // ---------------------------------------------------------------------

    struct HeightRead
    {
        std::string id;
        double height = 0.0;
    };

    inline constexpr std::size_t kHeightNeeded = offsetof(BlockApi::HeightRequest, height) + sizeof(float);  // the fields read

    /// The id and height of a kMessageHeight message, or nullopt with the reason in a_why: the message must cover the fields
    /// up to height (by dataLen and structSize), have apiVersion kVersionV2 or kVersion (2 or 3: the message is v2's, unchanged in
    /// v3; a later one is a header this host does not know), an id of 1..kMaxFinalIdLength characters and a finite height > 0 (whether it fits the
    /// block is JudgeHeight's)
    inline std::optional<HeightRead> ReadHeight(const void* a_data, std::uint32_t a_dataLen, std::string& a_why)
    {
        if (!a_data || a_dataLen < kHeightNeeded) {
            a_why = "the message is " + std::to_string(a_data ? a_dataLen : 0) + " bytes, a HeightRequest needs " + std::to_string(kHeightNeeded);
            return std::nullopt;
        }
        BlockApi::HeightRequest request{};
        std::memcpy(&request, a_data, std::min<std::size_t>(a_dataLen, sizeof(request)));
        if (request.apiVersion != BlockApi::kVersionV2 && request.apiVersion != BlockApi::kVersion) {
            a_why = "apiVersion " + std::to_string(request.apiVersion) + " (this SL_MagicMenu reads " + std::to_string(BlockApi::kVersionV2) + " or " + std::to_string(BlockApi::kVersion) + ")";
            return std::nullopt;
        }
        if (request.structSize < kHeightNeeded) {
            a_why = "structSize " + std::to_string(request.structSize) + " does not cover the height";
            return std::nullopt;
        }
        if (!request.id) {
            a_why = "no id";
            return std::nullopt;
        }
        const std::size_t length = strnlen(request.id, kMaxFinalIdLength + 1);
        if (length == 0 || length > kMaxFinalIdLength) {
            a_why = "the id is not 1.." + std::to_string(kMaxFinalIdLength) + " characters";
            return std::nullopt;
        }
        const double height = request.height;
        if (!std::isfinite(height) || height <= 0.0) {
            a_why = "the height " + std::to_string(height) + " is not a finite number > 0";
            return std::nullopt;
        }
        return HeightRead{ std::string(request.id, length), height };
    }

    enum class HeightVerdict
    {
        kApply,      // a new height: the columns are laid out again and the block drawn into it
        kUnchanged,  // the block has that height already
        kTooTall,    // more than the registered height (the block's maximum): refused
    };

    inline constexpr double kHeightTolerance = 0.01;  // stage units: two heights this close are the same

    /// a_requested against the block's registered height and its current one (nullopt: still the registered one)
    inline HeightVerdict JudgeHeight(double a_requested, double a_registered, const std::optional<double>& a_current)
    {
        if (a_requested > a_registered + kHeightTolerance) return HeightVerdict::kTooTall;
        const double now = a_current.value_or(a_registered);
        return std::abs(a_requested - now) <= kHeightTolerance ? HeightVerdict::kUnchanged : HeightVerdict::kApply;
    }

    /// The height a block is laid out with: its requested one this menu, but the registered one in the layout mode (the mode
    /// shows every block at its full size) or when none was requested
    inline double LaidOutHeight(double a_registered, const std::optional<double>& a_current, bool a_layoutMode)
    {
        return a_layoutMode || !a_current ? a_registered : *a_current;
    }

    // ---------------------------------------------------------------------
    // Blocks that fit their content (user 2026-10-30): the built-in cards and the tree window set their height this menu to
    // what their content needs, up to their full (ini / fill / registered) height - the same per-block height an add-on asks
    // for with kMessageHeight. One map for both (Widget.cpp g_menu.fitHeight)
    // ---------------------------------------------------------------------

    inline constexpr double kFitSlack = 0.5;  // stage units over a block's needed height: a room exactly as high as its content
                                              // is not left to float rounding in the fits' floor() and <= comparisons

    /// A block's height for content that needs a_needed, at most a_max (a needed height that is not a finite number > 0: a_max)
    inline double FittedHeight(double a_needed, double a_max)
    {
        if (!std::isfinite(a_needed) || a_needed <= 0.0) return a_max;
        return std::min(a_needed + kFitSlack, a_max);
    }

    /// The fit heights put into the heights Arrange is given: each fitted block at LaidOutHeight (its full height in the layout
    /// mode), and outside the mode taken out of the fill set - a fill block grows to the rest of its column (Layout::Arrange),
    /// which would undo its fit. Ids not in a_heights (not laid out) are skipped
    inline void ApplyFits(std::map<std::string, double>& a_heights, std::set<std::string>& a_fill, const std::map<std::string, double>& a_fits,
        bool a_layoutMode)
    {
        for (const auto& [id, fit] : a_fits) {
            const auto it = a_heights.find(id);
            if (it == a_heights.end()) continue;
            it->second = LaidOutHeight(it->second, fit, a_layoutMode);
            if (!a_layoutMode) a_fill.erase(id);
        }
    }

    // ---------------------------------------------------------------------
    // The layout mode's controls (user 2026-10-30): the gear toggles the mode (view <-> edit, as the K key); in the edit mode
    // one button, "처음 배치로" (reset), slides out right of the gear on its row. No control row under the columns
    // ---------------------------------------------------------------------

    inline constexpr double kResetOpenMs = 180.0, kResetCloseMs = 140.0;  // the reset button's unfold and fold (steady clock)

    /// Ease-out (cubic) of a_t in 0..1: fast at first, slowing to the end
    constexpr double EaseOut(double a_t)
    {
        const double t = a_t < 0.0 ? 0.0 : a_t > 1.0 ? 1.0 : a_t;
        const double rest = 1.0 - t;
        return 1.0 - rest * rest * rest;
    }

    /// An eased value from a_from to a_to, started at a_startMs and lasting a_durationMs, at a_nowMs (a_to from the end on)
    constexpr double AnimWidth(double a_from, double a_to, double a_startMs, double a_nowMs, double a_durationMs)
    {
        if (!(a_durationMs > 0.0)) return a_to;
        return a_from + (a_to - a_from) * EaseOut((a_nowMs - a_startMs) / a_durationMs);
    }

    /// The reset button's rect (stage): a_gap right of the gear, centred on the gear's height, a_w x a_h; kept inside a_visible
    inline Settings::Rect ResetButtonRect(const Settings::Rect& a_gear, double a_w, double a_h, double a_gap, const Settings::Rect& a_visible)
    {
        double left = a_gear.right + a_gap, top = a_gear.CenterY() - a_h / 2;
        left = std::clamp(left, a_visible.left, std::max(a_visible.left, a_visible.right - a_w));
        top = std::clamp(top, a_visible.top, std::max(a_visible.top, a_visible.bottom - a_h));
        return { left, top, left + a_w, top + a_h };
    }

    /// The reset button takes a click only in the edit mode and only once fully out (a_shownW at its full width a_fullW)
    constexpr bool ResetClickable(double a_shownW, double a_fullW, bool a_layoutMode)
    {
        return a_layoutMode && a_fullW > 0.0 && a_shownW >= a_fullW - kHeightTolerance;
    }

    /// The gear's hover hint beside the gear (stage rect a_gear): right of it when a hint a_w wide fits on screen there, else left
    /// of it; vertically centred on it. Returns its left edge in the gear's own coordinates (the gear's left = 0)
    /// a_preferLeft (the edit mode: the reset button sits right of the gear): left of it when it fits there
    inline double GearHintX(const Settings::Rect& a_gear, double a_w, double a_gap, const Settings::Rect& a_visible, bool a_preferLeft = false)
    {
        const bool leftFits = a_gear.left - a_gap - a_w >= a_visible.left;
        const bool rightFits = a_gear.right + a_gap + a_w <= a_visible.right;
        const bool left = a_preferLeft ? leftFits || !rightFits : !rightFits;
        return left ? -a_gap - a_w : a_gear.Width() + a_gap;
    }

    // ---------------------------------------------------------------------
    // The key-hint strip and the gear under our columns (user 2026-10-30, sketch on the layout map): SkyUI's bottom-bar button
    // hints move to one row right under the lower of our two columns, from the left column's left edge; the gear goes under
    // that row at the left column's left edge
    // ---------------------------------------------------------------------

    // Under the columns (user 2026-10-30: "단축키 시작이랑 톱니바퀴랑 Y축이 같게" - one vertical line, and "톱니바퀴가 한칸 아래로"): the
    // key-hint strip's row, then the gear one row below it, the gear's left edge on the strip's start. The gear's row keeps
    // the room right of it free (the gear's flyout will open there)
    struct HintRow
    {
        Settings::Rect strip, gear;  // the strip's rect is empty (0 x 0 at the columns' bottom + a_stripGap) when there is no strip
    };

    /// The strip (a_w x a_h, its own size) from the left column's left edge a_left, a_stripGap under the columns' bottom
    /// a_columnsBottom; the gear (a_gearSize) a_gearGap under the strip with its left edge on the strip's left edge (no strip:
    /// a_stripGap under the columns). Strip and gear are kept inside a_visible together: the pair moves up from the stage's
    /// bottom, and a strip wider than the room right of a_left moves left (the gear with it, so the two keep one line)
    inline HintRow HintRowRects(double a_left, double a_columnsBottom, double a_w, double a_h, double a_gearSize, double a_stripGap, double a_gearGap,
        const Settings::Rect& a_visible)
    {
        const bool hasStrip = a_w > 0.0 && a_h > 0.0;
        const double gapBelow = hasStrip ? a_gearGap : 0.0, stripH = hasStrip ? a_h : 0.0;
        const double pairH = stripH + gapBelow + a_gearSize;
        const double top = std::clamp(a_columnsBottom + a_stripGap, a_visible.top, std::max(a_visible.top, a_visible.bottom - pairH));
        const double left = std::clamp(a_left, a_visible.left, std::max(a_visible.left, a_visible.right - std::max(a_w, a_gearSize)));
        HintRow row;
        row.strip = { left, top, left + (hasStrip ? a_w : 0.0), top + stripH };
        const double gearTop = row.strip.bottom + gapBelow;
        row.gear = { left, gearTop, left + a_gearSize, gearTop + a_gearSize };
        return row;
    }

    enum class Queued
    {
        kAdded,
        kReplaced,  // the id was queued already: its value is the new one (the last request counts)
        kFull,      // a_cap ids queued already: dropped
    };

    /// The per-frame queue rule of kMessageRedraw and kMessageHeight: one entry per id, the last value wins, at most a_cap ids
    template <class T>
    Queued Enqueue(std::vector<std::pair<std::string, T>>& a_queue, const std::string& a_id, const T& a_value, std::size_t a_cap)
    {
        for (auto& [id, value] : a_queue) {
            if (id == a_id) {
                value = a_value;
                return Queued::kReplaced;
            }
        }
        if (a_queue.size() >= a_cap) return Queued::kFull;
        a_queue.emplace_back(a_id, a_value);
        return Queued::kAdded;
    }

    /// A block with this tabs mask shows on the tab of a_flag (kTabsAll: on every tab)
    constexpr bool ShownOn(std::uint32_t a_tabs, int a_flag)
    {
        return a_tabs == BlockApi::kTabsAll || (a_tabs & TabBit(a_flag)) != 0;
    }

    /// The host's state a press is judged by, read at the press and again at the release
    struct Moment
    {
        std::uint64_t scrollMoves{};  // the block's column: times its scroll changed this menu
        std::uint64_t draws{};        // the block: times it was drawn this menu
        std::uint64_t modeChanges{};  // the layout mode entered or left this menu
        bool layoutMode{};            // the layout mode is on
        Settings::Rect content;       // the block's content area on screen (stage, scrolled)
        Settings::Rect viewport;      // its column's visible part on screen
    };

    struct Point
    {
        double x{}, y{};
    };

    struct Press
    {
        Moment at;
        Point point;  // stage
    };

    struct Release
    {
        Moment at;
        Point point;           // stage
        bool overClip = true;  // onRelease (true) or onReleaseOutside (false: the mouse came up off the hit area)
    };

    /// A press and release that make a click call, or why not (the first rule that applies, in this order)
    enum class Verdict
    {
        kClick,
        kLayoutMode,       // the layout mode is on at the press or the release (it owns the mouse)
        kModeChanged,      // the layout mode was entered or left between them
        kScrolled,         // the column scrolled between them
        kRedrawn,          // the block was drawn again between them
        kMoved,            // the block's content area moved or changed size between them (a tab change, a new column width)
        kOutsideBlock,     // the release is off the hit area, or the press or the release outside the content area
        kOutsideViewport,  // the press or the release is outside the column's visible part
    };

    constexpr const char* VerdictText(Verdict a_verdict)
    {
        switch (a_verdict) {
        case Verdict::kClick: return "click";
        case Verdict::kLayoutMode: return "the layout mode is on";
        case Verdict::kModeChanged: return "the layout mode was entered or left between press and release";
        case Verdict::kScrolled: return "the column scrolled between press and release";
        case Verdict::kRedrawn: return "the block was drawn again between press and release";
        case Verdict::kMoved: return "the block moved or changed size between press and release";
        case Verdict::kOutsideBlock: return "released outside the block";
        case Verdict::kOutsideViewport: return "pressed or released outside the column's visible part";
        default: return "unknown";
        }
    }

    // Exact, not Widget.cpp's kRectTolerance: both rects come from the same stored arrangement (g_menu.blocks and the column
    // scroll) by the same arithmetic, so an unmoved block gives the same doubles; any re-arrangement that moves it differs
    inline bool SameRect(const Settings::Rect& a_a, const Settings::Rect& a_b)
    {
        return a_a.left == a_b.left && a_a.top == a_b.top && a_a.right == a_b.right && a_a.bottom == a_b.bottom;
    }

    inline Verdict Judge(const Press& a_press, const Release& a_release)
    {
        if (a_press.at.layoutMode || a_release.at.layoutMode) return Verdict::kLayoutMode;
        if (a_press.at.modeChanges != a_release.at.modeChanges) return Verdict::kModeChanged;
        if (a_press.at.scrollMoves != a_release.at.scrollMoves) return Verdict::kScrolled;
        if (a_press.at.draws != a_release.at.draws) return Verdict::kRedrawn;
        if (!SameRect(a_press.at.content, a_release.at.content)) return Verdict::kMoved;
        if (!a_release.overClip || !a_release.at.content.Contains(a_release.point.x, a_release.point.y) ||
            !a_press.at.content.Contains(a_press.point.x, a_press.point.y)) {
            return Verdict::kOutsideBlock;
        }
        if (!a_press.at.viewport.Contains(a_press.point.x, a_press.point.y) || !a_release.at.viewport.Contains(a_release.point.x, a_release.point.y)) {
            return Verdict::kOutsideViewport;
        }
        return Verdict::kClick;
    }

    /// A stage point in the content clip's coordinates (its top-left is a_content's)
    constexpr Point ToContent(const Point& a_stage, const Settings::Rect& a_content)
    {
        return { a_stage.x - a_content.left, a_stage.y - a_content.top };
    }

    // The gear's outline (user 2026-10-30 picked shape "B", scratchpad gear/gear2.py flat()): kGearTeeth square teeth with flat
    // tops on a root circle, a little wider at the root, flat valleys along the root circle between them, and a round hole.
    // Both outlines are closed: the last point is the first again, so the line has no start tick
    inline constexpr int kGearTeeth = 6;
    inline constexpr double kGearRootShare = 0.72;   // the root circle, of the outer radius
    inline constexpr double kGearTipShare = 0.42;    // a tooth's flat top, of its angular step
    inline constexpr double kGearRootWiden = 1.25;   // a tooth at the root, of its top's width
    inline constexpr int kGearValleySteps = 4;       // a valley in this many pieces along the root circle
    inline constexpr double kGearHoleShare = 0.30;   // the hole, of the outer radius
    inline constexpr int kGearHoleSegments = 32;

    struct GearShape
    {
        std::vector<Point> rim, hole;
    };

    /// The gear centred on a_cx, a_cy with the outer radius a_r
    inline GearShape GearOutline(double a_cx, double a_cy, double a_r)
    {
        const double twoPi = 2.0 * std::numbers::pi, step = twoPi / kGearTeeth, inner = a_r * kGearRootShare;
        const double tip = step * kGearTipShare / 2, root = tip * kGearRootWiden;
        const auto at = [&](double a_radius, double a_angle) { return Point{ a_cx + a_radius * std::cos(a_angle), a_cy + a_radius * std::sin(a_angle) }; };
        GearShape shape;
        for (int i = 0; i < kGearTeeth; ++i) {
            const double mid = step * i;
            shape.rim.push_back(at(inner, mid - root));
            shape.rim.push_back(at(a_r, mid - tip));
            shape.rim.push_back(at(a_r, mid + tip));
            shape.rim.push_back(at(inner, mid + root));
            const double from = mid + root, to = mid + step - root;  // the flat valley to the next tooth
            for (int k = 1; k < kGearValleySteps; ++k) shape.rim.push_back(at(inner, from + (to - from) * k / kGearValleySteps));
        }
        shape.rim.push_back(shape.rim.front());
        const double hole = a_r * kGearHoleShare;
        for (int k = 0; k < kGearHoleSegments; ++k) shape.hole.push_back(at(hole, twoPi * k / kGearHoleSegments));
        shape.hole.push_back(shape.hole.front());
        return shape;
    }

    /// The tree tooltip's top in its panel (review round 2): a_y as placed, kept within a_minY..a_maxY (under the title, inside
    /// the tree's full height), then inside the column's visible part a_viewTop..a_viewBottom (the same panel coordinates; the
    /// column mask cuts anything past it). One that runs past the visible bottom goes above its node (a_above) when that
    /// place is visible and under the title, else it is shifted up; it never starts above the visible top
    inline double TooltipY(double a_y, double a_h, double a_minY, double a_maxY, double a_above, double a_viewTop, double a_viewBottom)
    {
        double y = std::clamp(a_y, a_minY, std::max(a_minY, a_maxY));
        if (y + a_h > a_viewBottom) y = a_above >= std::max(a_viewTop, a_minY) ? a_above : a_viewBottom - a_h;
        return std::max(y, a_viewTop);
    }

    // ---------------------------------------------------------------------
    // Less jumping (user 2026-10-30): a fitted block grows at once (never cut) but shrinks only after its wanted height has held
    // for a delay - arrowing through the list does not make the columns jump on every row
    // ---------------------------------------------------------------------

    inline constexpr double kShrinkDelayMs = 150.0;

    /// A shrink waiting for its delay: the height and when it was first wanted (milliseconds of the host's steady clock)
    struct HeldShrink
    {
        double height{}, sinceMs{};
    };

    enum class FitStep
    {
        kApply,      // the wanted height now (a grow, or a shrink that held long enough); the hold is cleared
        kHold,       // a shrink, not yet: the block keeps its current height; a_held records it
        kUnchanged,  // already that height; a waiting shrink is dropped
    };

    /// The step for a block whose height is a_current and whose content wants a_wanted at a_nowMs. a_held is this block's waiting
    /// shrink (updated: started for a new shrink height, kept while the same height is wanted, cleared otherwise)
    inline FitStep DecideFit(double a_current, double a_wanted, double a_nowMs, std::optional<HeldShrink>& a_held, double a_delayMs = kShrinkDelayMs)
    {
        if (std::abs(a_wanted - a_current) <= kHeightTolerance) {
            a_held.reset();
            return FitStep::kUnchanged;
        }
        if (a_wanted > a_current) {
            a_held.reset();
            return FitStep::kApply;
        }
        if (!a_held || std::abs(a_held->height - a_wanted) > kHeightTolerance) a_held = HeldShrink{ a_wanted, a_nowMs };
        if (a_nowMs - a_held->sinceMs >= a_delayMs) {
            a_held.reset();
            return FitStep::kApply;
        }
        return FitStep::kHold;
    }

    /// A fitted block's height this menu and its waiting shrink, as the host keeps them (g_menu.fitHeight / heldShrink)
    struct FitState
    {
        std::optional<double> current;  // nullopt: no fit yet this menu (its full height)
        std::optional<HeldShrink> held;
    };

    /// One height wanted (a card's content, an add-on's 'SLMH'): true when it applies - a_state.current takes it and ANY waiting
    /// shrink is dropped (GPT review of 4b83efd3: a height applied in the layout mode left the old shrink waiting, and the
    /// mode's end put it back over the newer height); false when it is a shrink still waiting its delay. The menu's first fit
    /// and every height in the layout mode apply at once
    inline bool AskHeight(FitState& a_state, double a_wanted, double a_nowMs, bool a_layoutMode, double a_delayMs = kShrinkDelayMs)
    {
        if (!a_layoutMode && a_state.current && DecideFit(*a_state.current, a_wanted, a_nowMs, a_state.held, a_delayMs) == FitStep::kHold) return false;
        a_state.current = a_wanted;
        a_state.held.reset();
        return true;
    }

    /// The waiting shrink that is due at a_nowMs (its height, to be asked for again), nullopt when none is (or in the layout mode)
    inline std::optional<double> DueShrink(const FitState& a_state, double a_nowMs, bool a_layoutMode, double a_delayMs = kShrinkDelayMs)
    {
        if (a_layoutMode || !a_state.held || a_nowMs - a_state.held->sinceMs < a_delayMs) return std::nullopt;
        return a_state.held->height;
    }

    /// The bounds of a clip's four corners after they were taken into another clip's space (controller finding 2026-10-30:
    /// SkyUI's clips inside Inventory Reforged's sized list window - each corner goes through every parent's transform, then
    /// the box round them): nullopt when a corner is not a finite number or the box is empty
    inline std::optional<Settings::Rect> CornerBounds(const std::array<Point, 4>& a_corners)
    {
        Settings::Rect box{ a_corners[0].x, a_corners[0].y, a_corners[0].x, a_corners[0].y };
        for (const auto& corner : a_corners) {
            if (!std::isfinite(corner.x) || !std::isfinite(corner.y)) return std::nullopt;
            box.left = std::min(box.left, corner.x);
            box.top = std::min(box.top, corner.y);
            box.right = std::max(box.right, corner.x);
            box.bottom = std::max(box.bottom, corner.y);
        }
        if (box.right <= box.left || box.bottom <= box.top) return std::nullopt;
        return box;
    }

    // ---------------------------------------------------------------------
    // The key-hint strip's move (SkyUI's BottomBar.as / ButtonPanel.as): ONE clip, bottomBar.buttonPanel (_x set only by
    // positionElements), moved with its buttons - SkyUI's and STB Hotkey System's, laid out inside it, hidden with _visible
    // ---------------------------------------------------------------------

    /// One child of the panel: its bounds on the stage, whether it shows, and whether its bounds read (a shown child only)
    struct ChildBox
    {
        Settings::Rect stage;
        bool visible = true;
        bool read = true;
    };

    /// Every shown child's bounds read, so the content may be cached; a failed read is read again next frame (GPT on 4846c6a5)
    inline bool AllShownRead(const std::vector<ChildBox>& a_children)
    {
        return std::none_of(a_children.begin(), a_children.end(), [](const ChildBox& a_child) { return a_child.visible && !a_child.read; });
    }

    /// The union of the visible children's bounds (a hidden button is left out: it would widen the strip); nullopt when none shows
    /// or a shown child's bounds did not read (no part-union: it would put the strip off by the missing button)
    inline std::optional<Settings::Rect> VisibleUnion(const std::vector<ChildBox>& a_children)
    {
        if (!AllShownRead(a_children)) return std::nullopt;
        std::optional<Settings::Rect> united;
        for (const auto& child : a_children) {
            if (!child.visible) continue;
            if (!united) united = child.stage;
            united->left = std::min(united->left, child.stage.left);
            united->top = std::min(united->top, child.stage.top);
            united->right = std::max(united->right, child.stage.right);
            united->bottom = std::max(united->bottom, child.stage.bottom);
        }
        return united;
    }

    inline bool NearPoint(const Point& a_a, const Point& a_b, double a_tolerance)
    {
        return std::abs(a_a.x - a_b.x) <= a_tolerance && std::abs(a_a.y - a_b.y) <= a_tolerance;
    }

    struct PanelPlace
    {
        Point want;             // the origin (parent space) that puts the visible content's top-left on the target
        bool set = false;       // want is off the present origin by more than the tolerance: _x / _y are to be set
        bool rePlaced = false;  // the panel is off the origin last known in place (or none is known yet): SkyUI placed it
    };

    /// Where the panel goes. a_now is its origin (parent space), a_nowStage the same point on the stage, a_visibleNow its visible
    /// content on the stage; a_toParent (-> optional<Point>) maps a stage point into the parent through every parent (Inventory
    /// Reforged scales the menu 0.85). want = the stage origin moved by the step from the content's top-left to a_target, taken
    /// into the parent; a step of the origin moves the content by the mapped step (the chain is affine). a_inPlace is the origin
    /// last known in place (set by the host, or found right): the panel off it by more than a_tolerance (parent space; Flash
    /// keeps _x in twips) was placed by SkyUI, which writes only its _x (BottomBar.as:51). Landing exactly on it is harmless:
    /// the panel is then where it belongs. nullopt when the mapping fails
    template <class ToParent>
    std::optional<PanelPlace> PlacePanel(const Point& a_now, const Point& a_nowStage, const std::optional<Point>& a_inPlace, const Settings::Rect& a_visibleNow,
        const Point& a_target, ToParent&& a_toParent, double a_tolerance)
    {
        const std::optional<Point> want = a_toParent(Point{ a_nowStage.x + a_target.x - a_visibleNow.left, a_nowStage.y + a_target.y - a_visibleNow.top });
        if (!want) return std::nullopt;
        PanelPlace place;
        place.want = *want;
        place.set = !NearPoint(place.want, a_now, a_tolerance);
        place.rePlaced = !a_inPlace || !NearPoint(a_now, *a_inPlace, a_tolerance);
        return place;
    }

    // ---------------------------------------------------------------------
    // The board (user 2026-10-30, mock board.png, "이대로 만들기"): a fixed panel behind both columns, and every block's look
    // while nothing is chosen
    // ---------------------------------------------------------------------

    inline constexpr double kBoardPad = 16.0;  // the board round the two columns' full areas, each side

    /// The board's rect: the two columns' full areas (a_columns: their union - the frames, not the blocks, so it never changes
    /// when cards fit) a_pad larger on each side, cut to a_visible
    inline Settings::Rect BoardRect(const Settings::Rect& a_columns, double a_pad, const Settings::Rect& a_visible)
    {
        return { std::max(a_columns.left - a_pad, a_visible.left), std::max(a_columns.top - a_pad, a_visible.top), std::min(a_columns.right + a_pad, a_visible.right),
            std::min(a_columns.bottom + a_pad, a_visible.bottom) };
    }

    /// A block's look while nothing is chosen (the columns stay up then)
    enum class EmptyLook
    {
        kAsUsual,      // a spell is chosen, or the block has its own content without one (progress card, node card, a v1 add-on)
        kLineCentred,  // keeps its size, one dim line in its middle (the 3D preview, the tree window)
        kLineOnly,     // collapses to one dim line (our spell card)
        kClosed,       // not drawn, closed up (an add-on that closes when empty, BlockRulesV3.h ClosesWhenEmpty; not in the
                       // layout mode, where every block shows so it can be moved - review R2)
    };

    inline EmptyLook EmptyLookOf(std::string_view a_id, bool a_nothingShown, bool a_closesWhenEmpty, bool a_layoutMode = false)
    {
        if (!a_nothingShown) return EmptyLook::kAsUsual;
        if (a_id == Layout::kPreview || a_id == Layout::kTree) return EmptyLook::kLineCentred;
        if (a_id == Layout::kSpellCard) return EmptyLook::kLineOnly;
        if (a_closesWhenEmpty) return a_layoutMode ? EmptyLook::kAsUsual : EmptyLook::kClosed;
        return EmptyLook::kAsUsual;
    }
}
