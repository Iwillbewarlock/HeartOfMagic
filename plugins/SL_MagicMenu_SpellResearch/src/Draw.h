#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// =============================================================================
// DRAW - AS2 calls on a MovieClip: the drawing API (beginFill, lineTo ...) and
// text fields in the menu's own font (a Korean font mod applies). Re-port of the
// bookmark branch's MagicMenuDraw.h plus rect, diamond, ring, arc, dashed line
// and padlock shapes for the tree window.
// =============================================================================

namespace Draw
{
    inline constexpr const char* kFont = "$EverywhereMediumFont";  // the game's fontconfig name; font mods remap it
    inline constexpr double kAlphaFull = 100.0;  // AS2 _alpha and the drawing API's alpha are percents
    inline constexpr int kCircleSegments = 32;

    inline void Call(RE::GFxValue& a_clip, const char* a_name, std::initializer_list<RE::GFxValue> a_args)
    {
        std::vector<RE::GFxValue> args(a_args);
        a_clip.Invoke(a_name, nullptr, args.data(), args.size());
    }
    inline void LineStyle(RE::GFxValue& c, double t, std::uint32_t rgb, double alpha = kAlphaFull) { Call(c, "lineStyle", { t, static_cast<double>(rgb), alpha }); }
    inline void NoLine(RE::GFxValue& c) { Call(c, "lineStyle", {}); }
    inline void BeginFill(RE::GFxValue& c, std::uint32_t rgb, double alpha) { Call(c, "beginFill", { static_cast<double>(rgb), alpha }); }
    inline void EndFill(RE::GFxValue& c) { Call(c, "endFill", {}); }
    inline void MoveTo(RE::GFxValue& c, double x, double y) { Call(c, "moveTo", { x, y }); }
    inline void LineTo(RE::GFxValue& c, double x, double y) { Call(c, "lineTo", { x, y }); }

    inline double NextDepth(RE::GFxValue& a_parent)
    {
        RE::GFxValue depth;
        a_parent.Invoke("getNextHighestDepth", &depth);
        return depth.IsNumber() ? depth.GetNumber() : 0.0;
    }

    // A child clip above every child a_parent already has (not a display object when that failed)
    inline RE::GFxValue Clip(RE::GFxValue& a_parent, const char* a_name)
    {
        RE::GFxValue clip;
        a_parent.CreateEmptyMovieClip(&clip, a_name, static_cast<std::int32_t>(NextDepth(a_parent)));
        return clip;
    }

    // a_parent's child a_name, removed (no-op when there is none)
    inline void Remove(RE::GFxValue& a_parent, const char* a_name)
    {
        RE::GFxValue old;
        if (a_parent.GetMember(a_name, &old) && old.IsDisplayObject()) old.Invoke("removeMovieClip");
    }

    // A not selectable text field (lines split by a CR); a_align "left", "center" or "right"
    inline RE::GFxValue Text(RE::GFxMovieView* a_movie, RE::GFxValue& a_parent, const char* a_name, double x, double y,
        double w, double h, const std::string& a_text, std::uint32_t a_rgb, double a_size, const char* a_align = "left")
    {
        RE::GFxValue field, format, text;
        Call(a_parent, "createTextField", { a_name, NextDepth(a_parent), x, y, w, h });
        if (!a_parent.GetMember(a_name, &field) || !field.IsDisplayObject()) return field;
        field.Invoke("getNewTextFormat", &format);
        if (format.IsObject()) {
            format.SetMember("font", kFont);
            format.SetMember("color", static_cast<double>(a_rgb));
            format.SetMember("size", a_size);
            format.SetMember("align", a_align);
            field.Invoke("setNewTextFormat", nullptr, &format, 1);
        }
        field.SetMember("embedFonts", true);
        field.SetMember("selectable", false);
        field.SetMember("multiline", true);
        a_movie->CreateString(&text, a_text.c_str());
        field.SetMember("text", text);
        return field;
    }

    // The first a_maxChars code points of a UTF-8 text, "..." appended when cut (tree names are Korean: 3 bytes a letter)
    inline std::string Cut(std::string_view a_text, std::size_t a_maxChars)
    {
        std::size_t chars = 0, i = 0;
        while (i < a_text.size()) {
            if (chars == a_maxChars) return std::string(a_text.substr(0, i)) + "...";
            const auto lead = static_cast<unsigned char>(a_text[i]);
            const std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
            i += length;
            ++chars;
        }
        return std::string(a_text);
    }

    // Shapes. Outlines use the current lineStyle; a_fillAlpha 0 still makes the shape take the mouse.

    inline void Rect(RE::GFxValue& c, double x, double y, double w, double h, std::uint32_t a_fill, double a_fillAlpha)
    {
        BeginFill(c, a_fill, a_fillAlpha);
        MoveTo(c, x, y);
        LineTo(c, x + w, y);
        LineTo(c, x + w, y + h);
        LineTo(c, x, y + h);
        LineTo(c, x, y);
        EndFill(c);
    }

    // A box: a filled rect with a thin border
    inline void Box(RE::GFxValue& c, double w, double h, std::uint32_t a_fill, double a_fillAlpha, std::uint32_t a_edge, double a_edgeWidth)
    {
        LineStyle(c, a_edgeWidth, a_edge);
        Rect(c, 0, 0, w, h, a_fill, a_fillAlpha);
    }

    inline void Diamond(RE::GFxValue& c, double cx, double cy, double r)
    {
        MoveTo(c, cx, cy - r);
        LineTo(c, cx + r, cy);
        LineTo(c, cx, cy + r);
        LineTo(c, cx - r, cy);
        LineTo(c, cx, cy - r);
    }

    // Part of a circle, clockwise (stage y down) from the top, a_fraction 0..1 of the way round
    inline void Arc(RE::GFxValue& c, double cx, double cy, double r, double a_fraction)
    {
        a_fraction = std::clamp(a_fraction, 0.0, 1.0);
        const int steps = std::max(1, static_cast<int>(std::ceil(kCircleSegments * a_fraction)));
        const double start = -std::numbers::pi / 2;
        MoveTo(c, cx, cy - r);
        for (int i = 1; i <= steps; ++i) {
            const double t = start + 2 * std::numbers::pi * a_fraction * i / steps;
            LineTo(c, cx + r * std::cos(t), cy + r * std::sin(t));
        }
    }

    inline void Ring(RE::GFxValue& c, double cx, double cy, double r) { Arc(c, cx, cy, r, 1.0); }

    // A dashed line: a_dash drawn, a_gap left out, from a to b
    inline void DashedLine(RE::GFxValue& c, double ax, double ay, double bx, double by, double a_dash, double a_gap)
    {
        const double length = std::hypot(bx - ax, by - ay);
        if (length <= 0 || a_dash <= 0) return;
        const double ux = (bx - ax) / length, uy = (by - ay) / length;
        for (double s = 0; s < length; s += a_dash + a_gap) {
            const double e = std::min(s + a_dash, length);
            MoveTo(c, ax + ux * s, ay + uy * s);
            LineTo(c, ax + ux * e, ay + uy * e);
        }
    }

    // A closed polygon through points with .x / .y (HomStyle::ShapePoints), back to the first: the current lineStyle and fill
    template <class Points>
    void Polygon(RE::GFxValue& c, const Points& a_points)
    {
        if (a_points.empty()) return;
        MoveTo(c, a_points.front().x, a_points.front().y);
        for (const auto& p : a_points) LineTo(c, p.x, p.y);
        LineTo(c, a_points.front().x, a_points.front().y);
    }

    // Separate strokes with .a / .b ends (HomStyle::DashOutline: a dashed outline in one phase round its shape, spec v11 R10)
    template <class Strokes>
    void Segments(RE::GFxValue& c, const Strokes& a_strokes)
    {
        for (const auto& s : a_strokes) {
            MoveTo(c, s.a.x, s.a.y);
            LineTo(c, s.b.x, s.b.y);
        }
    }

    // A small padlock centred on (cx, cy), a_size tall: a filled body and a shackle drawn as half a ring
    inline void Padlock(RE::GFxValue& c, double cx, double cy, double a_size, std::uint32_t a_rgb)
    {
        constexpr double kBodyW = 0.8, kBodyH = 0.5, kShackleR = 0.25;
        constexpr int kHalfSegments = 8;
        const double bw = kBodyW * a_size, bh = kBodyH * a_size, r = kShackleR * a_size;
        const double bodyTop = cy + a_size / 2 - bh;
        NoLine(c);
        Rect(c, cx - bw / 2, bodyTop, bw, bh, a_rgb, kAlphaFull);
        LineStyle(c, std::max(1.0, a_size / 8), a_rgb);
        MoveTo(c, cx - r, bodyTop);
        for (int i = 1; i <= kHalfSegments; ++i) {
            const double t = std::numbers::pi + std::numbers::pi * i / kHalfSegments;
            LineTo(c, cx + r * std::cos(t), bodyTop + r * std::sin(t));
        }
    }

    // Mark strokes: at least kMarkMinWidth wide, a_size / kMarkWidthShare otherwise
    inline constexpr double kMarkMinWidth = 1.5, kMarkWidthShare = 6.0;

    // A polyline of points given as shares of a box of a_size at (x, y) top-left
    inline void Marks(RE::GFxValue& c, double x, double y, double a_size, std::initializer_list<std::pair<double, double>> a_points)
    {
        bool first = true;
        for (const auto& [px, py] : a_points) {
            if (first) {
                MoveTo(c, x + a_size * px, y + a_size * py);
            } else {
                LineTo(c, x + a_size * px, y + a_size * py);
            }
            first = false;
        }
    }

    // A check mark in a box of a_size at (x, y) top-left
    inline void Check(RE::GFxValue& c, double x, double y, double a_size, std::uint32_t a_rgb)
    {
        LineStyle(c, std::max(kMarkMinWidth, a_size / kMarkWidthShare), a_rgb);
        Marks(c, x, y, a_size, { { 0.1, 0.55 }, { 0.4, 0.85 }, { 0.9, 0.15 } });
    }

    // A cross in a box of a_size at (x, y) top-left
    inline void Cross(RE::GFxValue& c, double x, double y, double a_size, std::uint32_t a_rgb)
    {
        constexpr double kNear = 0.15, kFar = 0.85;
        LineStyle(c, std::max(kMarkMinWidth, a_size / kMarkWidthShare), a_rgb);
        Marks(c, x, y, a_size, { { kNear, kNear }, { kFar, kFar } });
        Marks(c, x, y, a_size, { { kFar, kNear }, { kNear, kFar } });
    }
}
