#pragma once

#include "Settings.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

// =============================================================================
// HOM SETTINGS - the Heart of Magic block's values from SL_MagicMenu.ini, with
// the defaults and the accepted ranges they had in the core's Settings.cpp at
// f83dc6f3 (a value outside its range, or not a number, keeps the default and is
// warned about). Moved here in HoM add-on plan Task 5; the reader is Task 6's:
// Read() is the pure part (no game types: tools/magic-menu-hom-block-check runs
// it on a sample ini), HomSettings.cpp hands it the ini's raw values through
// GetPrivateProfileString. tools/magic-menu-tree-check uses the defaults and
// TreeContent, so its window is the one the block draws.
// =============================================================================

namespace HomSettings
{
    using Rect = Settings::Rect;

    struct Values
    {
        // [Keys] DirectInput scan codes. Chosen by the build-5 key scan of the installed mods
        // (docs/superpowers/specs/v5-key-scan.md): R (19) is Spell Organizer's remove-spell key in the magic menu and
        // N (49) Creature Framework's puppet key with no menu check; U and G are used by nothing live in the magic menu.
        std::uint32_t keyNext = 22;       // U: next learnable spell of the school (iNext)
        std::uint32_t keySetTarget = 34;  // G: what the node card's button does (iSetTarget)

        // [Layout] stage units, layout E2 (spec v6 L1): the tree window and the node card in the right column, the progress
        // card under the preview. Each block's height is its rect's (bottom - top); the core places it (Layout::Arrange)
        Rect tree{ 978, 67, 1238, 297 };      // fTree*
        Rect progress{ 648, 306, 969, 408 };  // fProgress*
        Rect nodeCard{ 978, 306, 1238, 623 }; // fNodeCard*
        // stage units per tree (world) unit, TreeData::View::scale (fTreeScale, 0.01..10). Spec v10 T2: one fixed scale; every
        // school's closest two nodes are >= 37.9 tree units apart (spell_tree.json), 28.4 at 0.75
        double treeScale = 0.75;
        // a multiplier on Heart of Magic's own mark sizes (fTreeMarkScale, 0.5..3; spec v11 T12: radii in HoM world units x
        // treeScale x this; 1 = HoM's). The largest drawn reach at 1 (a selected learnable spell's ring, 14.5 + its half line)
        // x 0.75 = 11.5 < 14.2, half the closest pair (CheckSpacing, tools/magic-menu-tree-check)
        double treeMarkScale = 1.0;
        int maxSteps = 1;   // iMaxSteps (1..6): link steps that count as linked to the centre: drawn in full and clickable even when locked
        int maxNodes = 60;  // iMaxNodes (1..200)

        // [Debug] fDebugExtraHeight (0..2000): stage units added to the node card block's height so its column overflows and the
        // column scroll can be tried in game (0 = no effect)
        double debugExtraHeight = 0.0;

        // [General] bSelfTest: the host's switch (SL_MagicMenu reads the same key, its default 0 since Task 8 fix round 1): the add-on's "SelfTest:" lines
        // in SL_MagicMenu_HoM.log
        bool selfTest = false;
    };

    // The accepted ranges (Settings.cpp at f83dc6f3)
    inline constexpr std::uint32_t kMaxScanCode = 255;
    inline constexpr double kMinTreeScale = 0.01, kMaxTreeScale = 10.0;
    inline constexpr double kMinMarkScale = 0.5, kMaxMarkScale = 3.0;
    inline constexpr int kMaxSteps = 6, kMaxNodes = 200;
    inline constexpr double kMaxDebugExtraHeight = 2000.0;  // a few screens of overflow at most
    inline constexpr double kStageMin = -4000.0, kStageMax = 4000.0;

    inline constexpr double kTreeLegendH = 18.0;  // the legend strip at the bottom of the tree window

    /// An ini value as GetPrivateProfileString gives it, cleaned as the core's Settings.cpp Raw() does: trailing blanks and a
    /// ';' or '#' comment cut off; nullopt when nothing is left
    inline std::optional<std::string> CleanValue(std::string a_text)
    {
        const auto trim = [&] {
            while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t')) a_text.pop_back();
        };
        trim();
        if (const auto comment = a_text.find_first_of(";#"); comment != std::string::npos) a_text.resize(comment);
        trim();
        if (a_text.empty()) return std::nullopt;
        return a_text;
    }

    /// The cleaned text of [a_section] a_key (CleanValue), nullopt when the key is missing or empty
    using RawFn = std::function<std::optional<std::string>(std::string_view a_section, std::string_view a_key)>;

    struct ReadResult
    {
        Values values;
        std::vector<std::string> warnings;  // one per value kept at its default (logged by the caller)
    };

    namespace detail
    {
        template <class T>
        std::optional<T> Parse(const std::string& a_text)
        {
            T value{};
            const auto* end = a_text.data() + a_text.size();
            const auto [ptr, ec] = std::from_chars(a_text.data(), end, value);
            if (ec != std::errc{} || ptr != end) return std::nullopt;
            // from_chars reads "nan" and "inf"; a NaN would pass every range check (all comparisons false)
            if constexpr (std::is_floating_point_v<T>) {
                if (!std::isfinite(value)) return std::nullopt;
            }
            return value;
        }

        template <class T>
        void Number(const RawFn& a_raw, ReadResult& a_out, std::string_view a_section, std::string_view a_key, T& a_value, T a_min, T a_max)
        {
            const auto raw = a_raw(a_section, a_key);
            if (!raw) return;
            const auto value = Parse<T>(*raw);
            if (!value || *value < a_min || *value > a_max) {
                a_out.warnings.push_back(std::format("[{}] {} = '{}' is not a number in {}..{} - keeping {}", a_section, a_key, *raw, a_min, a_max, a_value));
                return;
            }
            a_value = *value;
        }

        inline void Bool(const RawFn& a_raw, ReadResult& a_out, std::string_view a_section, std::string_view a_key, bool& a_value)
        {
            const auto raw = a_raw(a_section, a_key);
            if (!raw) return;
            const auto value = Parse<int>(*raw);
            if (!value || (*value != 0 && *value != 1)) {
                a_out.warnings.push_back(std::format("[{}] {} = '{}' is not 0 or 1 - keeping {}", a_section, a_key, *raw, a_value));
                return;
            }
            a_value = *value == 1;
        }

        // Four keys <prefix>Left/Top/Right/Bottom in [Layout]; a rect with no area keeps the default
        inline void ReadRect(const RawFn& a_raw, ReadResult& a_out, std::string_view a_prefix, Rect& a_value)
        {
            auto rect = a_value;
            const std::string prefix(a_prefix);
            Number(a_raw, a_out, "Layout", prefix + "Left", rect.left, kStageMin, kStageMax);
            Number(a_raw, a_out, "Layout", prefix + "Top", rect.top, kStageMin, kStageMax);
            Number(a_raw, a_out, "Layout", prefix + "Right", rect.right, kStageMin, kStageMax);
            Number(a_raw, a_out, "Layout", prefix + "Bottom", rect.bottom, kStageMin, kStageMax);
            if (rect.Width() <= 0 || rect.Height() <= 0) {
                a_out.warnings.push_back(std::format("[Layout] {} rect {},{} .. {},{} has no area - keeping the default", a_prefix, rect.left, rect.top,
                    rect.right, rect.bottom));
                return;
            }
            a_value = rect;
        }
    }

    /// The block's keys of SL_MagicMenu.ini (sections and names as the core read them at f83dc6f3)
    inline ReadResult Read(const RawFn& a_raw)
    {
        ReadResult read;
        auto& v = read.values;
        detail::Bool(a_raw, read, "General", "bSelfTest", v.selfTest);
        detail::Number(a_raw, read, "Keys", "iNext", v.keyNext, 1u, kMaxScanCode);
        detail::Number(a_raw, read, "Keys", "iSetTarget", v.keySetTarget, 1u, kMaxScanCode);
        detail::ReadRect(a_raw, read, "fTree", v.tree);
        detail::ReadRect(a_raw, read, "fProgress", v.progress);
        detail::ReadRect(a_raw, read, "fNodeCard", v.nodeCard);
        detail::Number(a_raw, read, "Layout", "fTreeScale", v.treeScale, kMinTreeScale, kMaxTreeScale);
        detail::Number(a_raw, read, "Layout", "fTreeMarkScale", v.treeMarkScale, kMinMarkScale, kMaxMarkScale);
        detail::Number(a_raw, read, "Layout", "iMaxSteps", v.maxSteps, 1, kMaxSteps);
        detail::Number(a_raw, read, "Layout", "iMaxNodes", v.maxNodes, 1, kMaxNodes);
        detail::Number(a_raw, read, "Debug", "fDebugExtraHeight", v.debugExtraHeight, 0.0, kMaxDebugExtraHeight);
        return read;
    }

    /// The three blocks' registered heights (each rect's bottom - top, as the core's DefaultHeights at f83dc6f3; the node card
    /// plus fDebugExtraHeight, as its BlockHeights)
    struct Heights
    {
        double tree{}, progress{}, nodeCard{};
    };

    inline Heights BlockHeights(const Values& a_values)
    {
        return { a_values.tree.Height(), a_values.progress.Height(), a_values.nodeCard.Height() + a_values.debugExtraHeight };
    }

    /// Reads the block's keys from Data/SKSE/Plugins/SL_MagicMenu.ini (HomSettings.cpp; call once, at plugin load). Logs them
    void Load();

    /// The values Load() read (the defaults before it)
    const Values& Get();

    struct Area
    {
        double cx{}, cy{};        // centre, in the panel's own coordinates
        double halfW{}, halfH{};  // half size
    };

    /// The tree window's content area: below the title, above the legend, inside the padding (Settings::kPanelTitleH / kPanelPad:
    /// every panel's, the host draws the title strip).
    /// a_titleH / a_pad: the box's title strip and padding (the block reads them from its content clip's place; the host's own by
    /// default)
    inline Area TreeContent(const Rect& a_tree, double a_titleH = Settings::kPanelTitleH, double a_pad = Settings::kPanelPad)
    {
        const double bodyH = a_tree.Height() - a_titleH - kTreeLegendH;
        return { a_tree.Width() / 2, a_titleH + bodyH / 2, a_tree.Width() / 2 - a_pad, bodyH / 2 - a_pad };
    }
}
