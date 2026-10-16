#include "SelfTestJudge.h"

#include "Tint.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace BlackFlame::SelfTest
{
    namespace
    {
        constexpr float kColorTolerance = 0.002f;
        constexpr float kSlackSeconds = 0.2f;
        constexpr std::size_t kRgba = 4;

        bool Near(float a_lhs, float a_rhs) { return std::fabs(a_lhs - a_rhs) <= kColorTolerance; }
    }

    GreyVerdict JudgeGrey(RE::FormID a_copyBase, bool a_recolored, int a_recoloredShapes,
        const SelfTestProbe::Inspection& a_after, bool a_recolourOn)
    {
        GreyVerdict out;
        if (a_copyBase != 0) {
            out.judged = true;
            out.copy = true;
            out.pass = a_after.effectProperties > 0 && a_after.withPalettePath == a_after.effectProperties;
            out.how = std::format("copy {:08X}: grey palette path on {} of {} effect shapes in its 3D{}", a_copyBase,
                a_after.withPalettePath, a_after.effectProperties, a_after.effectProperties == 0 ? " (NOTHING OBSERVED)" : "");
            return out;
        }
        if (a_recolourOn) {
            out.judged = true;
            out.pass = a_recolored && a_recoloredShapes > 0 && a_after.withPalettePath >= a_recoloredShapes;
            out.how = std::format("runtime recolour (no copy): {} shapes recoloured, palette path on {} of {}", a_recoloredShapes,
                a_after.withPalettePath, a_after.effectProperties);
            return out;
        }
        out.how = "no copy and the runtime recolour off: nothing to judge";
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

    bool HasColour(const ColorKeys& a_keys)
    {
        for (std::size_t i = 0; i + kRgba <= a_keys.size(); i += kRgba) {
            if (a_keys[i] != a_keys[i + 1] || a_keys[i] != a_keys[i + 2]) {
                return true;
            }
        }
        return false;
    }

    bool MatchesDesaturated(const ColorKeys& a_keys, const ColorKeys& a_original)
    {
        if (!HasColour(a_original)) {
            return false;
        }
        for (std::size_t i = 0; i + kRgba <= a_original.size(); i += kRgba) {
            RE::NiColorA c{ a_original[i], a_original[i + 1], a_original[i + 2], a_original[i + 3] };
            (void)Desaturate(c);
            if (!Near(a_keys[i], c.red) || !Near(a_keys[i + 1], c.green) || !Near(a_keys[i + 2], c.blue) ||
                !Near(a_keys[i + 3], c.alpha)) {
                return false;
            }
        }
        return true;
    }

    std::optional<bool> ModifiersChanged(const SelfTestProbe::Inspection& a_atHook, const SelfTestProbe::Inspection& a_now)
    {
        if (a_atHook.colorModifiers.empty() || a_now.colorModifiers.empty()) {
            return std::nullopt;
        }
        bool matched = false;
        for (std::size_t i = 0; i < a_atHook.colorModifiers.size(); ++i) {
            const auto it = std::ranges::find(a_now.colorModifiers, a_atHook.colorModifiers[i]);
            if (it == a_now.colorModifiers.end()) {
                continue;
            }
            matched = true;
            if (a_now.modifierKeys[static_cast<std::size_t>(it - a_now.colorModifiers.begin())] !=
                a_atHook.modifierKeys[i]) {
                return true;
            }
        }
        return matched ? std::optional<bool>(false) : std::nullopt;  // nothing compared: unknown
    }
}
