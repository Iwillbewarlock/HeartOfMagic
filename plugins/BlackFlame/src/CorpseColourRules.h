#pragma once

#include "Tint.h"

#include <algorithm>

// The engine's factory reads the live EFSH keys (another plugin may have patched them). Grey only the returned private
// property of a fire shader, even with no palette/base copy; the shared EFSH and its cache keep their colours for others.
namespace BlackFlame::CorpseColours
{
    // The builder's EFSH fire test on one colour (tools/blackflame-greybuild/GreyKeyed.h kEfshFireHueMax, kEfshFireHueWrap,
    // kEfshFireMinSat, kEfshFireMinValue, kEfshBaseOrangeHueMin, kEfshBaseRedMinValue: keep them equal): orange to yellow
    // at HSV saturation 0.45 and value 0.30 or more, or red at value 0.55 or more. Grey, white, blue and green are not fire.
    inline constexpr float kFireHueMin = 15.0f / 360.0f;
    inline constexpr float kFireHueMax = 55.0f / 360.0f;
    inline constexpr float kFireHueWrap = 345.0f / 360.0f;
    inline constexpr float kFireMinSat = 0.45f;
    inline constexpr float kFireMinValue = 0.30f;
    inline constexpr float kFireRedMinValue = 0.55f;

    template <class Colour>  // channels 0..255 (RE::Color, the EFSH record's keys)
    [[nodiscard]] bool FireColoured(const Colour& a_colour)
    {
        constexpr float kByte = 255.0f;
        const float r = a_colour.red / kByte, g = a_colour.green / kByte, b = a_colour.blue / kByte;
        const float max = std::max({ r, g, b });
        const float span = max - std::min({ r, g, b });
        if (max < kFireMinValue || span / max < kFireMinSat) {
            return false;
        }
        constexpr float kSextants = 6.0f;
        float sextant = max == r ? (g - b) / span : max == g ? (b - r) / span + 2.0f : (r - g) / span + 4.0f;
        if (sextant < 0.0f) {
            sextant += kSextants;
        }
        const float hue = sextant / kSextants;
        return (hue >= kFireHueMin && hue <= kFireHueMax) || ((hue < kFireHueMin || hue >= kFireHueWrap) && max >= kFireRedMinValue);
    }

    // review R1 (2026-10-23): a marked corpse's shader is greyed only when it is fire - the builder gave one of its textures
    // a grey copy, or a stage it draws has a fire-coloured key (live values: another plugin may have patched them). A
    // non-fire shader played on the same corpse (a highlight, a soul trap) keeps its colours.
    template <class Data>  // TESEffectShader::Data
    [[nodiscard]] bool FireKeys(const Data& a_data, bool a_membraneDrawn, bool a_particlesDrawn)
    {
        // review Codex: the rim's colour is edgeEffectColor (vanilla FireFXShader: grey/white fill keys, edge 146,65,5)
        const bool membrane = a_membraneDrawn && (FireColoured(a_data.fillTextureEffectColorKey1) ||
                                                     FireColoured(a_data.fillTextureEffectColorKey2) ||
                                                     FireColoured(a_data.fillTextureEffectColorKey3) ||
                                                     FireColoured(a_data.edgeEffectColor));
        const bool particles = a_particlesDrawn && (FireColoured(a_data.colorKey1) || FireColoured(a_data.colorKey2) ||
                                                       FireColoured(a_data.colorKey3));
        return membrane || particles;
    }

    template <class Factory>
    auto MakeParticles(bool a_grey, bool a_share, Factory&& a_factory)
    {
        auto* property = a_factory(a_grey ? false : a_share);
        if (a_grey && property) {
            Desaturate(property->colorKey1);
            Desaturate(property->colorKey2);
            Desaturate(property->colorKey3);
        }
        return property;
    }

    // Attach and Pop initialise these colours; Update interpolates them again. Called after each of those writes.
    template <class Membrane>
    bool GreyMembrane(bool a_grey, Membrane* a_data)
    {
        if (!a_grey || !a_data) {
            return false;
        }
        const bool fill = Desaturate(a_data->fillColor);
        const bool rim = Desaturate(a_data->rimColor);
        return fill || rim;
    }
}
