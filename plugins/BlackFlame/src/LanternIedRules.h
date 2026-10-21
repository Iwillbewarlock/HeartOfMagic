#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

// =============================================================================
// LANTERN IED RULES - where Immersive Equipment Displays puts the worn lantern (LanternIed.h): the four hip presets, the
// optional transform override and the choice between them, with no engine type and no JSON (C++17:
// tools/blackflame-cosave-check tests them, LanternIedRulesTests.h)
// =============================================================================
// Config.cpp reduces lanternIedPreset / lanternIedTransform to an Input and logs each warning of the Choice with its
// "BlackFlame: config " prefix. The presets are the IED profiles of GiraPomba's Lights On - Wearable Lanterns for
// Immersive Equipment Displays (Nexus SSE 85575, asset use allowed with credit; README "Lantern on IED"): node NPC Pelvis
// [Pelv], position in game units, rotation in degrees (IED's SetItemRotation takes degrees), scale 0.6.

namespace BlackFlame::LanternIedRules
{
    inline constexpr const char* kNode = "NPC Pelvis [Pelv]";
    inline constexpr const char* kItemName = "Lantern";  // the IED entry's name under the plugin's key
    // The override's limits, compared as doubles before the values are narrowed to float (0.05 itself passes)
    inline constexpr double kMaxPosition = 1000.0;  // game units from the node, each axis
    inline constexpr double kMaxRotationDeg = 360.0;
    inline constexpr double kMinScale = 0.05;
    inline constexpr double kMaxScale = 5.0;
    inline constexpr std::size_t kTransformLength = 7;  // [px, py, pz, rx, ry, rz, scale]
    inline constexpr float kPresetScale = 0.6f;

    struct Transform
    {
        std::array<float, 3> position{};
        std::array<float, 3> rotationDeg{};
        float scale = 1.0f;
    };

    struct Preset
    {
        const char* name;
        Transform transform;
    };

    inline constexpr std::array<Preset, 4> kPresets{ {
        { "frontLeft", { { -18.0f, 13.0f, -8.5f }, { 15.0f, 31.0f, 23.5f }, kPresetScale } },
        { "frontRight", { { 13.5f, 13.5f, -8.5f }, { -14.5f, -29.5f, 10.0f }, kPresetScale } },
        { "backLeft", { { -14.5f, -10.0f, -12.0f }, { -7.5f, 136.5f, -8.5f }, kPresetScale } },
        { "backRight", { { 13.0f, -14.0f, -12.0f }, { 4.5f, 215.0f, -5.0f }, kPresetScale } },
    } };
    inline constexpr std::size_t kDefaultPreset = 0;  // frontLeft

    /// What the config file holds for the two keys
    struct Input
    {
        bool presetGiven = false;
        bool presetIsString = false;
        std::string preset;
        bool transformGiven = false;
        bool transformIsArray = false;
        std::vector<double> transform;  // a value that is not a number as NaN
    };

    struct Choice
    {
        Transform transform = kPresets[kDefaultPreset].transform;
        std::string preset = kPresets[kDefaultPreset].name;  // the preset chosen (the override replaces its transform)
        bool overridden = false;
        std::vector<std::string> warnings;  // without the "BlackFlame: config " prefix
    };

    /// The preset of that exact name (case matters), or nullptr
    [[nodiscard]] inline const Preset* FindPreset(const std::string& a_name) noexcept
    {
        for (const auto& preset : kPresets) {
            if (a_name == preset.name) {
                return &preset;
            }
        }
        return nullptr;
    }

    [[nodiscard]] inline std::string NumberText(double a_value)
    {
        std::ostringstream out;
        out << a_value;
        return out.str();
    }

    /// Empty when a_values is a transform within the limits (a_out set), else why not (a_out untouched)
    [[nodiscard]] inline std::string ParseTransform(const std::vector<double>& a_values, Transform& a_out)
    {
        if (a_values.size() != kTransformLength) {
            return "has " + std::to_string(a_values.size()) + " values, expected 7 [px, py, pz, rx, ry, rz, scale]";
        }
        static constexpr std::array<const char*, kTransformLength> kNames{ "px", "py", "pz", "rx", "ry", "rz", "scale" };
        Transform parsed;
        for (std::size_t i = 0; i < kTransformLength; ++i) {
            const double value = a_values[i];
            const bool position = i < 3;
            const bool scale = i == kTransformLength - 1;
            const double low = scale ? kMinScale : position ? -kMaxPosition : -kMaxRotationDeg;
            const double high = scale ? kMaxScale : position ? kMaxPosition : kMaxRotationDeg;
            // NaN fails both comparisons
            if (!(value >= low && value <= high)) {
                return std::string("has ") + kNames[i] + " " + (std::isnan(value) ? std::string("not a number") : NumberText(value)) +
                       ", expected a number in " + NumberText(low) + ".." + NumberText(high) +
                       (scale ? "" : position ? " game units" : " degrees");
            }
            const auto narrowed = static_cast<float>(value);
            if (position) {
                parsed.position[i] = narrowed;
            } else if (scale) {
                parsed.scale = narrowed;
            } else {
                parsed.rotationDeg[i - 3] = narrowed;
            }
        }
        a_out = parsed;
        return {};
    }

    /// The preset (frontLeft when absent, not a string or unknown: a warning for the last two), then the override when it
    /// is given and valid (a warning and the preset's transform when it is not)
    [[nodiscard]] inline Choice Choose(const Input& a_in)
    {
        Choice choice;
        if (a_in.presetGiven) {
            const Preset* preset = a_in.presetIsString ? FindPreset(a_in.preset) : nullptr;
            if (preset) {
                choice.transform = preset->transform;
                choice.preset = preset->name;
            } else {
                choice.warnings.push_back(std::string("'lanternIedPreset' ") +
                                          (a_in.presetIsString ? "'" + a_in.preset + "' is not" : std::string("is not a string, expected")) +
                                          " \"frontLeft\", \"frontRight\", \"backLeft\" or \"backRight\" - using frontLeft");
            }
        }
        if (!a_in.transformGiven) {
            return choice;
        }
        const std::string why = a_in.transformIsArray ? ParseTransform(a_in.transform, choice.transform) :
                                                        std::string("is not an array of 7 numbers [px, py, pz, rx, ry, rz, scale]");
        if (why.empty()) {
            choice.overridden = true;
        } else {
            choice.warnings.push_back("'lanternIedTransform' " + why + " - using the preset " + choice.preset);
        }
        return choice;
    }
}
