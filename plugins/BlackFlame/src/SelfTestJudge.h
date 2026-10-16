#pragma once

#include "SelfTestCtx.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

// =============================================================================
// SELF-TEST JUDGE - shader identity and the copies' grey verdict for the steps
// =============================================================================

namespace BlackFlame::SelfTest
{
    using ShaderFilter = std::function<bool(const SelfTestWorld::ShaderOn&)>;

    /// An engine hit shader still alight: unfinished, at least kEngineAgeMin old, one of a_engines,
    /// and not excluded by a_exclude
    [[nodiscard]] bool EngineLive(const std::vector<SelfTestWorld::ShaderOn>& a_shaders,
        const std::vector<RE::FormID>& a_engines, const ShaderFilter& a_exclude = {});

    /// a_now is the shader recorded as a_seen (not a new one at a reused address); see the .cpp
    [[nodiscard]] bool SameObject(const SelfTestWorld::ShaderOn& a_now, const SeenShader& a_seen);

    [[nodiscard]] bool SeenBefore(const SelfTestWorld::ShaderOn& a_now, const std::vector<SeenShader>& a_seen);

    /// A shader seen now on the target, as a record to compare later sightings with
    [[nodiscard]] SeenShader SeenNow(const SelfTestWorld::ShaderOn& a_shader);

    /// Each engine hit shader on the target with its age
    [[nodiscard]] std::string DescribeEngineShaders(const std::vector<SelfTestWorld::ShaderOn>& a_shaders,
        const std::vector<RE::FormID>& a_engines);

    /// C2: whether the player's projectile or explosion is grey. A copy base is grey by construction, so it is
    /// judged by the copy and the copies' rule on every effect shape of its 3D (already grey is the expected
    /// state); a non-copy is not judged (C6: no runtime recolour; a form without a copy stays orange).
    struct GreyVerdict
    {
        bool judged = false;
        bool pass = false;
        bool copy = false;
        std::string how;
    };

    [[nodiscard]] GreyVerdict JudgeGrey(RE::FormID a_copyBase, const SelfTestProbe::Inspection& a_after);
}
