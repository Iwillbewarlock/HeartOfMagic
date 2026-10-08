#pragma once

#include <array>
#include <initializer_list>
#include <string>
#include <vector>

// =============================================================================
// AS2 calls on a MovieClip: the drawing API (beginFill, lineTo ...), text fields
// in the menu's own font (a Korean font mod applies), and a glow filter
// =============================================================================

namespace MagicMenuDraw
{
    inline constexpr const char* kFont = "$EverywhereMediumFont";  // the game's fontconfig name; font mods remap it

    inline void Call(RE::GFxValue& a_clip, const char* a_name, std::initializer_list<RE::GFxValue> a_args)
    {
        std::vector<RE::GFxValue> args(a_args);
        a_clip.Invoke(a_name, nullptr, args.data(), args.size());
    }
    inline void Clear(RE::GFxValue& c) { Call(c, "clear", {}); }
    inline void LineStyle(RE::GFxValue& c, double t, std::uint32_t rgb) { Call(c, "lineStyle", { t, static_cast<double>(rgb), 100.0 }); }
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

    // A child clip above every child a_parent already has
    inline RE::GFxValue Clip(RE::GFxValue& a_parent, const char* a_name)
    {
        RE::GFxValue clip;
        a_parent.CreateEmptyMovieClip(&clip, a_name, static_cast<std::int32_t>(NextDepth(a_parent)));
        return clip;
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

    // flash.filters.GlowFilter on a clip or text field; false when the movie has no filters
    inline bool Glow(RE::GFxMovieView* a_movie, RE::GFxValue& a_target, std::uint32_t a_rgb, double a_blur, double a_strength)
    {
        RE::GFxValue filter, filters, check;
        const std::array<RE::GFxValue, 6> args{ static_cast<double>(a_rgb), 1.0, a_blur, a_blur, a_strength, 2.0 };
        a_movie->CreateObject(&filter, "flash.filters.GlowFilter", args.data(), static_cast<std::uint32_t>(args.size()));
        if (!filter.IsObject()) return false;
        a_movie->CreateArray(&filters);
        filters.PushBack(filter);
        a_target.SetMember("filters", filters);
        return a_target.GetMember("filters", &check) && check.IsArray() && check.GetArraySize() == 1;
    }
}
