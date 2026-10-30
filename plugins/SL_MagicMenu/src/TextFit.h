#pragma once

#include "Common.h"

#include "Draw.h"
#include "LayoutMath.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>

// =============================================================================
// TEXT FIT - text fields that fit their room: reading sizes, field heights from
// the game font's measured line metrics, texts cut to "..." by bisection on the
// laid-out text, and the once-per-menu logs of what was cut (Q4) and which
// states were shown (Q2). Moved out of Widget.cpp (HoM add-on plan Task 3) so
// the core and the HoM add-on block share it: no menu state of its own, the
// caller passes its measured Metrics and its per-menu Log.
// =============================================================================

namespace TextFit
{
    // Reading sizes (spec v5 Q4): at least the left SkyUI list's row size, measured from the movie (MeasureListRow);
    // until it is measured kListRowFallback (UNVERIFIED: build 3 frame f_008 shows the list rows about as large as our 14).
    inline constexpr double kListRowFallback = 14.0;
    inline constexpr double kMinFitSize = 8.0;      // a text shrunk to its room goes no smaller (review round 1 #4)
    inline constexpr double kFitStep = 0.5;         // and is shrunk in these steps
    inline constexpr int kMaxCutLogs = 40;          // "Q4 text ... cut to" lines per menu
    inline constexpr double kGutter = 4.0;          // AS2 text fields keep 2 units free on each side of their text (textWidth leaves it out)

    /// The caller's measured text metrics: the left list's row text size (0 = not measured yet), the game font's first line
    /// height and its step per further line, per size unit (MeasureTextMetrics)
    struct Metrics
    {
        double rowSize{};
        double lineFirst{};
        double linePitch{};
    };

    /// The caller's once-per-menu logs: the cut texts and rooms (Q4), the Q2 states shown
    struct Log
    {
        std::set<std::string> cut;  // "Q4 text ... cut to" lines logged this menu (field and text)
        std::set<std::string> q2;   // Q2 states logged this menu
        const char* prefix = "Widget";  // the Q4 lines' first word: the core's "Widget", an add-on's own name (HoM add-on plan Task 6)
    };

    /// Where Q2State writes its "SelfTest: ..." line (the caller's self-test log)
    using SelfTestFn = void (*)(const std::string&);

    inline std::optional<double> Number(const RE::GFxValue& a_object, const char* a_member)
    {
        RE::GFxValue value;
        if (!a_object.IsObject() || !a_object.GetMember(a_member, &value) || !value.IsNumber()) return std::nullopt;
        return value.GetNumber();
    }

    inline std::string StringMember(const RE::GFxValue& a_object, const char* a_member)
    {
        RE::GFxValue value;
        if (!a_object.IsObject() || !a_object.GetMember(a_member, &value) || !value.IsString()) return {};
        const char* text = value.GetString();
        return text ? text : "";
    }

    // A reading size: at least the left list's row size (Q4)
    inline double Size(const Metrics& a_metrics, double a_base)
    {
        return std::max(a_base, a_metrics.rowSize > 0 ? a_metrics.rowSize : kListRowFallback);
    }

    // The step from one text line to the next at a size: the game font's, measured at attach (MeasureTextMetrics)
    inline double LineH(const Metrics& a_metrics, double a_size)
    {
        return a_size * a_metrics.linePitch;
    }

    // A field's height for a size and line count: the font's first line, a step per further line, and the gutter
    inline double FieldH(const Metrics& a_metrics, double a_size, int a_lines = 1)
    {
        return a_size * (a_metrics.lineFirst + a_metrics.linePitch * static_cast<double>(std::max(1, a_lines) - 1)) + kGutter;
    }

    // The same metrics for LayoutMath (its FieldH is this FieldH)
    inline LayoutMath::TextMetrics LayoutMetrics(const Metrics& a_metrics)
    {
        return { a_metrics.lineFirst, a_metrics.linePitch, kGutter };
    }

    inline std::size_t CodePoints(std::string_view a_text)
    {
        return static_cast<std::size_t>(std::ranges::count_if(a_text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
    }

    inline void SetText(RE::GFxMovieView* a_movie, RE::GFxValue& a_field, const std::string& a_text)
    {
        RE::GFxValue text;
        a_movie->CreateString(&text, a_text.c_str());
        a_field.SetMember("text", text);
    }

    inline double TextW(const RE::GFxValue& a_field)
    {
        return Number(a_field, "textWidth").value_or(0.0) + kGutter;
    }

    inline double TextH(const RE::GFxValue& a_field)
    {
        return Number(a_field, "textHeight").value_or(0.0) + kGutter;
    }

    // How many lines the field's text takes at a_size: its textHeight against the font's measured first line and step
    // (0 for no text)
    inline int TextLines(const Metrics& a_metrics, const RE::GFxValue& a_field, double a_size)
    {
        const double height = Number(a_field, "textHeight").value_or(0.0);
        if (height <= 0 || a_size <= 0) return 0;
        const double more = std::max(0.0, height - a_size * a_metrics.lineFirst);
        return 1 + static_cast<int>(std::lround(more / (a_size * a_metrics.linePitch)));
    }

    // No wider than a_w and no more than a_lines lines. Judged by the line count, not the height: cutting a one-line text
    // never lowers its height, so a font whose line is taller than our estimate cut every text to "..." (review round 1 #1)
    inline bool TextFits(const Metrics& a_metrics, const RE::GFxValue& a_field, double a_w, double a_size, int a_lines)
    {
        constexpr double kSlack = 0.5;
        return TextW(a_field) <= a_w + kSlack && TextLines(a_metrics, a_field, a_size) <= a_lines;
    }

    // A text that had to be cut, once per field and text per menu (up to kMaxCutLogs): what is shown now; WARN when only
    // "..." is left of the variable part
    inline void LogCut(Log& a_log, const RE::GFxValue& a_field, double a_w, double a_size, int a_lines, bool a_dotsOnly)
    {
        auto where = StringMember(a_field, "_target");
        if (where.empty()) where = StringMember(a_field, "_name");
        const auto shown = StringMember(a_field, "text");
        if (a_log.cut.size() >= static_cast<std::size_t>(kMaxCutLogs) || !a_log.cut.insert(where + "|" + shown).second) return;
        if (a_dotsOnly) {
            logger::warn("{}: Q4 text {} cut to '{}' - WARN only the ellipsis is left ({:.0f} wide, {} line(s) at size {:.1f}; "
                         "text {:.1f} x {:.1f})",
                a_log.prefix, where, shown, a_w, a_lines, a_size, TextW(a_field), TextH(a_field));
        } else {
            logger::info("{}: Q4 text {} cut to '{}' ({:.0f} wide, {} line(s) at size {:.1f})", a_log.prefix, where, shown, a_w, a_lines, a_size);
        }
    }

    // Puts a_compose(a_part) into the field when it fits (a_w wide, a_lines lines at a_size); else a_part cut to the most
    // code points that fit, with "..." (Draw::Cut), found by bisection on the text the field really lays out
    // (textWidth/textHeight read back), and logged (LogCut). False when even "..." alone does not fit (that text is kept).
    inline bool FitText(const Metrics& a_metrics, Log& a_log, RE::GFxMovieView* a_movie, RE::GFxValue& a_field, double a_w, double a_size,
        int a_lines, const std::string& a_part, const std::function<std::string(const std::string&)>& a_compose)
    {
        SetText(a_movie, a_field, a_compose(a_part));
        if (TextFits(a_metrics, a_field, a_w, a_size, a_lines)) return true;
        std::size_t lo = 0, hi = CodePoints(a_part);  // fits(lo) unknown, fits(hi) false
        bool any = false;
        while (lo < hi) {
            const std::size_t mid = (lo + hi) / 2;
            SetText(a_movie, a_field, a_compose(Draw::Cut(a_part, mid)));
            if (TextFits(a_metrics, a_field, a_w, a_size, a_lines)) {
                any = true;
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        // lo is the first count that does not fit: lo - 1 is the longest that does (0 when none does)
        const std::size_t best = lo > 0 ? lo - 1 : 0;
        SetText(a_movie, a_field, a_compose(Draw::Cut(a_part, best)));
        const bool fits = any || TextFits(a_metrics, a_field, a_w, a_size, a_lines);
        LogCut(a_log, a_field, a_w, a_size, a_lines, best == 0);
        return fits;
    }

    inline bool FitWhole(const Metrics& a_metrics, Log& a_log, RE::GFxMovieView* a_movie, RE::GFxValue& a_field, double a_w, double a_size,
        int a_lines, const std::string& a_text)
    {
        return FitText(a_metrics, a_log, a_movie, a_field, a_w, a_size, a_lines, a_text, [](const std::string& a_part) { return a_part; });
    }

    // A one-line text field whose text ends in "..." when it is wider than a_w
    inline RE::GFxValue FitLine(const Metrics& a_metrics, Log& a_log, RE::GFxMovieView* a_movie, RE::GFxValue& a_parent, const char* a_name,
        double a_x, double a_y, double a_w, double a_size, const std::string& a_text, std::uint32_t a_rgb, const char* a_align = "left")
    {
        auto field = Draw::Text(a_movie, a_parent, a_name, a_x, a_y, a_w, FieldH(a_metrics, a_size), "", a_rgb, a_size, a_align);
        if (field.IsDisplayObject()) FitWhole(a_metrics, a_log, a_movie, field, a_w, a_size, 1, a_text);
        return field;
    }

    // A text field wrapping to at most a_lines lines, the last ending in "..." when the text needs more
    inline RE::GFxValue FitLines(const Metrics& a_metrics, Log& a_log, RE::GFxMovieView* a_movie, RE::GFxValue& a_parent, const char* a_name,
        double a_x, double a_y, double a_w, double a_size, int a_lines, const std::string& a_text, std::uint32_t a_rgb,
        const char* a_align = "left")
    {
        auto field = Draw::Text(a_movie, a_parent, a_name, a_x, a_y, a_w, FieldH(a_metrics, a_size, a_lines), "", a_rgb, a_size, a_align);
        if (!field.IsDisplayObject()) return field;
        field.SetMember("wordWrap", true);
        FitWhole(a_metrics, a_log, a_movie, field, a_w, a_size, a_lines, a_text);
        return field;
    }

    // The largest size up to a_size (down to kMinFitSize) whose a_lines-line field fits a_room; logged once per menu when
    // even that runs over (review round 1 #4: at a list row size of 20 the progress card's last line ended at 104.75 of 102)
    inline double SizeFor(const Metrics& a_metrics, Log& a_log, double a_size, int a_lines, double a_room, const char* a_what)
    {
        const double size = LayoutMath::SizeFor(LayoutMetrics(a_metrics), a_size, a_lines, a_room, kMinFitSize, kFitStep);
        if (a_what && FieldH(a_metrics, size, a_lines) > a_room && a_log.cut.insert(std::string("room|") + a_what).second) {
            logger::info("{}: Q4 {} needs {:.1f} of {:.1f} stage units even at size {:.1f} - it runs over", a_log.prefix, a_what,
                FieldH(a_metrics, size, a_lines), a_room, size);
        }
        return size;
    }

    // "SelfTest: Q2 state <name>" the first time a state is shown in this menu, with the text the field shows (read back;
    // the intended text after it when the two differ), written through a_selfTest
    inline void Q2State(Log& a_log, SelfTestFn a_selfTest, std::string_view a_name, const std::string& a_intended,
        const RE::GFxValue* a_field = nullptr)
    {
        if (!a_log.q2.emplace(a_name).second) return;
        const auto shown = !a_field ? a_intended : a_field->IsDisplayObject() ? StringMember(*a_field, "text") : std::string("(no field)");
        a_selfTest(shown == a_intended ? std::format("Q2 state {} - \"{}\"", a_name, shown) :
                                         std::format("Q2 state {} - \"{}\" (intended \"{}\")", a_name, shown, a_intended));
    }
}
