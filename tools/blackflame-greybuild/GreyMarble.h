#pragma once

#include "GreyTexStats.h"
#include <cstddef>

namespace nifly { class NifFile; }
namespace GreyBuild
{
    struct MarbleTotals
    {
        std::size_t systems = 0;
        std::size_t emberSystems = 0;
        std::size_t sourceParticles = 0;
        std::size_t addedParticles = 0;
    };

    // Casting-art only. Main fire = additive, palette-coloured, multi-cell particle atlas,
    // and >=20% fire-coloured lit source-palette texels (the shared EfshFireShare rule).
    // Clone the source-owned graph, retaining external emitter/gravity/mesh references.
    // Small single-cell additive fire particles also gain x2 emission brightness and a dark
    // companion at 1/3 birth rate. All clones keep the source pool: selected pool budget <= x2.
    [[nodiscard]] MarbleTotals AddHandMarble(nifly::NifFile& a_copy, nifly::NifFile& a_source,
        TextureSource& a_textures, double a_opacity, double a_scale);
    void ValidateMarbleRefs(nifly::NifFile& a_nif);
}
