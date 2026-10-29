#pragma once

#include "BlockApi.h"
#include "Settings.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

// =============================================================================
// BLOCK RULES - the pure parts of the Block API v2 host side (Spell Research
// add-on design 2026-10-27, "Changes / 1"): which SkyUI category tab is which
// BlockApi tab bit, whether a block shows on a tab, and whether a press and its
// release in an add-on block become a click call. No game types: Widget.cpp
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
}
