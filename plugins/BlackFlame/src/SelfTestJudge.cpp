#include "SelfTestJudge.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace BlackFlame::SelfTest
{
    namespace
    {
        constexpr float kSlackSeconds = 0.2f;
    }

    GreyVerdict JudgeGrey(RE::FormID a_copyBase, const SelfTestProbe::Inspection& a_after)
    {
        GreyVerdict out;
        if (a_copyBase != 0) {
            out.judged = true;
            out.copy = true;
            // v2i: palette shaders carry one of our palettes, every other shader a grey base colour
            out.pass = a_after.effectProperties > 0 && a_after.greyByRule == a_after.effectProperties;
            out.how = std::format("copy {:08X}: grey by the copies' rule on {} of {} effect shapes in its 3D ({} palette shaders, "
                "{} with our palette, {} of them the additive one){}", a_copyBase, a_after.greyByRule, a_after.effectProperties,
                a_after.paletteShaders, a_after.withOurPalette, a_after.withAddPalette,
                a_after.effectProperties == 0 ? " (NOTHING OBSERVED)" : "");
            return out;
        }
        out.how = "no copy (C6: no runtime recolour; this form stays orange): nothing to judge";
        return out;
    }

    bool EngineLive(const std::vector<SelfTestWorld::ShaderOn>& a_shaders, const std::vector<RE::FormID>& a_engines,
        const ShaderFilter& a_exclude)
    {
        return std::ranges::any_of(a_shaders, [&](const auto& s) {
            return !s.finished && s.age >= kEngineAgeMin && std::ranges::find(a_engines, s.effectData) != a_engines.end() &&
                   !(a_exclude && a_exclude(s));
        });
    }

    // Same address, and the shader's age grew by the unpaused time since the record (a shader's age
    // does not grow while the game is paused, so the self-test's paused seconds are taken out).
    // The slack (frame timing between the record and now) is at most the recorded age: a new shader
    // born at a reused address after the record is younger than the elapsed time, so it cannot
    // reach `recorded age + elapsed - slack` once the slack cannot exceed the recorded age. And a
    // record taken finished never matches an unfinished sighting: a finished effect never comes back.
    bool SameObject(const SelfTestWorld::ShaderOn& a_now, const SeenShader& a_seen)
    {
        if (a_now.ptr != a_seen.ptr || (a_seen.finished && !a_now.finished)) {
            return false;
        }
        const float wall = std::chrono::duration<float>(Clock::now() - a_seen.at).count();
        const float elapsed = (std::max)(wall - (SelfTestProbe::PausedSeconds() - a_seen.paused), 0.0f);
        const float slack = (std::min)(kSlackSeconds, a_seen.age);
        return a_now.age >= a_seen.age + elapsed - slack;
    }

    bool SeenBefore(const SelfTestWorld::ShaderOn& a_now, const std::vector<SeenShader>& a_seen)
    {
        return std::ranges::any_of(a_seen, [&](const auto& s) { return SameObject(a_now, s); });
    }

    SeenShader SeenNow(const SelfTestWorld::ShaderOn& a_shader)
    {
        return { a_shader.ptr, a_shader.age, Clock::now(), SelfTestProbe::PausedSeconds(), a_shader.finished };
    }

    std::string DescribeEngineShaders(const std::vector<SelfTestWorld::ShaderOn>& a_shaders,
        const std::vector<RE::FormID>& a_engines)
    {
        std::string out;
        for (const auto& s : a_shaders) {
            if (std::ranges::find(a_engines, s.effectData) == a_engines.end()) {
                continue;
            }
            out += std::format("{}[{:08X} age {:.2f}{}]", out.empty() ? "" : " ", s.effectData, s.age,
                s.finished ? " finished" : "");
        }
        return out.empty() ? "none" : out;
    }
}
