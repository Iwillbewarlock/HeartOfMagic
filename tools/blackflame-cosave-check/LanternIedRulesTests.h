#pragma once

// =============================================================================
// LANTERN IED RULES TESTS - plugins/BlackFlame/src/LanternIedRules.h (the worn lantern on Immersive Equipment Displays):
// the preset table as GiraPomba's Lights On profiles give it, the default, each preset by name, an unknown or non-string
// preset, the transform override winning, a wrong length, values out of range or not numbers, and the limits' exact edges
// =============================================================================
// Included by main.cpp.

#include "../../plugins/BlackFlame/src/LanternIedRules.h"

#include <cmath>
#include <string>
#include <vector>

namespace LanternIedRulesTests
{
    namespace R = BlackFlame::LanternIedRules;

    inline bool Same(const R::Transform& a_left, const R::Transform& a_right)
    {
        return a_left.position == a_right.position && a_left.rotationDeg == a_right.rotationDeg && a_left.scale == a_right.scale;
    }

    inline bool Is(const R::Transform& a_t, float px, float py, float pz, float rx, float ry, float rz, float scale)
    {
        return a_t.position == std::array<float, 3>{ px, py, pz } && a_t.rotationDeg == std::array<float, 3>{ rx, ry, rz } &&
               a_t.scale == scale;
    }

    inline R::Input Preset(const std::string& a_name)
    {
        R::Input in;
        in.presetGiven = true;
        in.presetIsString = true;
        in.preset = a_name;
        return in;
    }

    inline R::Input Override(std::vector<double> a_values, const std::string& a_preset = "")
    {
        R::Input in = a_preset.empty() ? R::Input{} : Preset(a_preset);
        in.transformGiven = true;
        in.transformIsArray = true;
        in.transform = std::move(a_values);
        return in;
    }

    inline bool Mentions(const std::vector<std::string>& a_warnings, const std::string& a_text)
    {
        for (const auto& w : a_warnings) {
            if (w.find(a_text) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    template <class Check>
    void RunPresets(Check&& a_check)
    {
        a_check(Is(R::kPresets[0].transform, -18.0f, 13.0f, -8.5f, 15.0f, 31.0f, 23.5f, 0.6f) &&
                    Is(R::kPresets[1].transform, 13.5f, 13.5f, -8.5f, -14.5f, -29.5f, 10.0f, 0.6f) &&
                    Is(R::kPresets[2].transform, -14.5f, -10.0f, -12.0f, -7.5f, 136.5f, -8.5f, 0.6f) &&
                    Is(R::kPresets[3].transform, 13.0f, -14.0f, -12.0f, 4.5f, 215.0f, -5.0f, 0.6f) &&
                    std::string(R::kPresets[R::kDefaultPreset].name) == "frontLeft",
            "lantern ied: the four presets hold the Lights On profile values, frontLeft the default");

        const auto none = R::Choose(R::Input{});
        a_check(none.preset == "frontLeft" && !none.overridden && none.warnings.empty() && Same(none.transform, R::kPresets[0].transform),
            "lantern ied: no keys give frontLeft with no warning");

        bool each = true;
        for (const auto& preset : R::kPresets) {
            const auto chosen = R::Choose(Preset(preset.name));
            each = each && chosen.preset == preset.name && !chosen.overridden && chosen.warnings.empty() &&
                   Same(chosen.transform, preset.transform);
        }
        a_check(each, "lantern ied: each preset by name gives its own transform, no warning");

        const auto unknown = R::Choose(Preset("FrontLeft"));
        a_check(unknown.preset == "frontLeft" && Same(unknown.transform, R::kPresets[0].transform) && unknown.warnings.size() == 1 &&
                    Mentions(unknown.warnings, "'FrontLeft' is not"),
            "lantern ied: an unknown preset (case matters: 'FrontLeft') gives frontLeft and one warning naming it");

        R::Input notString;
        notString.presetGiven = true;
        notString.preset = "3";
        const auto number = R::Choose(notString);
        a_check(number.preset == "frontLeft" && number.warnings.size() == 1 && Mentions(number.warnings, "is not a string"),
            "lantern ied: a preset that is not a string gives frontLeft and one warning");
    }

    template <class Check>
    void RunOverride(Check&& a_check)
    {
        const auto wins = R::Choose(Override({ 1, -2, 3.5, 90, -45, 360, 1.25 }, "backRight"));
        a_check(wins.overridden && wins.preset == "backRight" && wins.warnings.empty() && Is(wins.transform, 1, -2, 3.5f, 90, -45, 360, 1.25f),
            "lantern ied: a valid lanternIedTransform wins over the preset (the preset still named)");

        const auto shortOne = R::Choose(Override({ 1, 2, 3, 4, 5, 6 }, "backLeft"));
        const auto longOne = R::Choose(Override({ 1, 2, 3, 4, 5, 6, 1, 8 }));
        a_check(!shortOne.overridden && Same(shortOne.transform, R::kPresets[2].transform) && Mentions(shortOne.warnings, "has 6 values") &&
                    Mentions(shortOne.warnings, "using the preset backLeft") && !longOne.overridden &&
                    Same(longOne.transform, R::kPresets[0].transform) && Mentions(longOne.warnings, "has 8 values"),
            "lantern ied: 6 or 8 values are refused with a warning, the preset's transform kept");

        R::Input notArray;
        notArray.transformGiven = true;
        const auto object = R::Choose(notArray);
        a_check(!object.overridden && object.warnings.size() == 1 && Mentions(object.warnings, "is not an array of 7 numbers"),
            "lantern ied: a lanternIedTransform that is not an array is refused with a warning");

        const std::vector<std::pair<std::vector<double>, std::string>> bad{
            { { 1000.5, 0, 0, 0, 0, 0, 1 }, "has px 1000.5" },
            { { 0, -1001, 0, 0, 0, 0, 1 }, "has py -1001" },
            { { 0, 0, 0, 0, 361, 0, 1 }, "has ry 361" },
            { { 0, 0, 0, 0, 0, -360.5, 1 }, "has rz -360.5" },
            { { 0, 0, 0, 0, 0, 0, 0.04 }, "has scale 0.04" },
            { { 0, 0, 0, 0, 0, 0, 5.01 }, "has scale 5.01" },
            { { 0, 0, std::nan(""), 0, 0, 0, 1 }, "has pz not a number" },
        };
        bool refused = true;
        std::string missed;
        for (const auto& [values, text] : bad) {
            const auto chosen = R::Choose(Override(values, "frontRight"));
            const bool ok = !chosen.overridden && Same(chosen.transform, R::kPresets[1].transform) && chosen.warnings.size() == 1 &&
                            Mentions(chosen.warnings, text);
            refused = refused && ok;
            if (!ok) {
                missed += " '" + text + "'";
            }
        }
        a_check(refused, "lantern ied: a value out of range or not a number is refused, named, and the preset kept" +
                             (missed.empty() ? std::string() : " (missed:" + missed + ")"));

        const auto edges = R::Choose(Override({ -1000, 1000, 0, -360, 360, 0, 0.05 }));
        const auto top = R::Choose(Override({ 0, 0, 0, 0, 0, 0, 5 }));
        a_check(edges.overridden && top.overridden && Is(edges.transform, -1000, 1000, 0, -360, 360, 0, 0.05f) && top.transform.scale == 5.0f,
            "lantern ied: the limits themselves pass (+-1000 units, +-360 degrees, scale 0.05 and 5)");

        const auto both = R::Choose(Override({ 1, 2, 3 }, "sideways"));
        a_check(both.warnings.size() == 2 && both.preset == "frontLeft" && Same(both.transform, R::kPresets[0].transform),
            "lantern ied: an unknown preset and a bad transform give two warnings and frontLeft");
    }

    template <class Check>
    void Run(Check&& a_check)
    {
        RunPresets(a_check);
        RunOverride(a_check);
    }
}
