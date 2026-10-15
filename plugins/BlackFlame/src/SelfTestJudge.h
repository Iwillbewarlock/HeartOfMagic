#pragma once

#include "SelfTestCtx.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

// =============================================================================
// SELF-TEST JUDGE - shader identity and colour-key comparisons for the steps
// =============================================================================

namespace BlackFlame::SelfTest
{
    using ShaderFilter = std::function<bool(const SelfTestWorld::ShaderOn&)>;

    /// An engine hit shader the burn layer did not finish: unfinished, at least kEngineAgeMin old,
    /// one of a_engines, and not excluded by a_exclude
    [[nodiscard]] bool EngineLive(const std::vector<SelfTestWorld::ShaderOn>& a_shaders,
        const std::vector<RE::FormID>& a_engines, const ShaderFilter& a_exclude = {});

    /// a_now is the shader recorded as a_seen (not a new one at a reused address); see the .cpp
    [[nodiscard]] bool SameObject(const SelfTestWorld::ShaderOn& a_now, const SeenShader& a_seen);

    [[nodiscard]] bool SeenBefore(const SelfTestWorld::ShaderOn& a_now, const std::vector<SeenShader>& a_seen);

    /// The hit shaders the burn layer recorded from the player's effects since a mark
    [[nodiscard]] std::vector<SeenShader> PlayerHitShaders(std::uint64_t a_mark);

    /// A shader seen now on the target, as a record to compare later sightings with
    [[nodiscard]] SeenShader SeenNow(const SelfTestWorld::ShaderOn& a_shader);

    /// Each engine hit shader on the target with its age and whose hit it was (T3, T5-extend)
    [[nodiscard]] std::string DescribeEngineShaders(const Ctx& a_c, const std::vector<SelfTestWorld::ShaderOn>& a_shaders,
        const std::vector<RE::FormID>& a_engines);

    /// Some RGB key is not grey: desaturating these keys would change them
    [[nodiscard]] bool HasColour(const ColorKeys& a_keys);

    /// a_keys equals a_original desaturated the way ParticleColors does it (RGB to luminance,
    /// alpha kept), within a small tolerance; false for an already grey original
    [[nodiscard]] bool MatchesDesaturated(const ColorKeys& a_keys, const ColorKeys& a_original);

    /// The caster's colour keys at its hook vs now, per modifier pointer; empty when nothing matched
    [[nodiscard]] std::optional<bool> ModifiersChanged(const SelfTestProbe::Inspection& a_atHook,
        const SelfTestProbe::Inspection& a_now);
}
