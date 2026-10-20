#pragma once

#include "GreyParticleGraph.h"

namespace nifly { class NifFile; class NiParticleSystem; class NiPSysData; }
namespace GreyBuild
{
    // Casting-art output recipe, versioned by handfx in the model key, not new user settings.
    inline constexpr float kHandMarbleRadiansPerSecond = 1.8849555922f; // 108 degrees/s
    inline constexpr float kHandMarbleSpeedVariation = kHandMarbleRadiansPerSecond * 0.5f;
    inline constexpr float kHandMarbleInitialAngle = 0.384f;
    inline constexpr float kHandMarbleAngleVariation = 3.1415926536f;
    inline constexpr float kHandMarbleCellsPerSecond = 8.4f;
    inline constexpr float kHandMarbleFrameCountFudge = 1.0f; // Unbiased stochastic integer rounding.
    // Birth cell = trunc(U * variation), U in [0, 1] (1.0 only by float rounding of the RNG):
    // variation = cells - this keeps the last cell reachable and never yields cell == count.
    inline constexpr float kHandMarbleBirthCellMargin = 1.0e-3f;
    inline constexpr std::uint32_t kHandParticleMotionOrder = 3000;
    inline constexpr float kHandEmberMaxRadius = 4.0f;
    inline constexpr float kHandEmberBrightness = 2.0f;
    inline constexpr float kHandEmberMaxBrightness = 8.0f;
    inline constexpr float kHandDarkEmberBirthFraction = 1.0f / 3.0f; // Add 2 dark for 6 bright.
    inline constexpr float kHandDarkEmberOpacity = 0.9f;

    bool IsHandEmber(nifly::NiHeader& hdr, nifly::NiParticleSystem& system, const nifly::NiPSysData& data);
    // Nonempty result explains a local motion skip; the cloned source motion stays intact.
    std::string SetHandMarbleMotion(nifly::NiHeader& hdr, std::uint32_t id);
    void OrderHandParticleUpdate(nifly::NiHeader& hdr, std::uint32_t id);
    void BrightenHandEmber(nifly::NifFile& nif, std::uint32_t id);
    void ReduceDarkEmberRate(nifly::NiHeader& hdr, const HandGraph::Remap& map, std::uint32_t id);
}
