#pragma once

// =============================================================================
// TINT - the one luminance / desaturation rule (Recolor.cpp: the kept runtime recolours)
// =============================================================================

namespace BlackFlame
{
    // Rec. 709 luma weights
    inline constexpr float kLumaRed = 0.2126f;
    inline constexpr float kLumaGreen = 0.7152f;
    inline constexpr float kLumaBlue = 0.0722f;

    template <class Colour>
    [[nodiscard]] inline float Luminance(const Colour& a_color) noexcept
    {
        return a_color.red * kLumaRed + a_color.green * kLumaGreen + a_color.blue * kLumaBlue;
    }

    /// RGB set to luminance * a_scale, alpha kept. Returns whether any channel changed.
    template <class Colour>
    inline bool Desaturate(Colour& a_color, float a_scale = 1.0f) noexcept
    {
        const float grey = Luminance(a_color) * a_scale;
        const bool changed = a_color.red != grey || a_color.green != grey || a_color.blue != grey;
        a_color.red = grey;
        a_color.green = grey;
        a_color.blue = grey;
        return changed;
    }
}
