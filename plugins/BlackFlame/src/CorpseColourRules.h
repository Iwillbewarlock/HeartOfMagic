#pragma once

#include "Tint.h"

// The engine's factory reads the live EFSH keys (another plugin may have patched them). Grey only the returned private
// property, even with no palette/base copy; the shared EFSH and its cache must retain their colours for other targets.
namespace BlackFlame::CorpseColours
{
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
