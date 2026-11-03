#pragma once

#include "MasteryMath.h"

#include <cmath>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// =============================================================================
// MasteryConfig - config.json's "mastery" section read into MasteryMath::Settings
// (UIManagerConfig.cpp at game start and on each settings save; tools/progression-check)
// =============================================================================

namespace MasteryMath
{
    // config.json's "mastery" section. A missing section or key is the default; a value of the
    // wrong type, out of range, not finite or not positive is the default too, and is described in
    // `problems` (the caller logs them). ranks: a whole number 1..kMaxRanks.
    [[nodiscard]] inline Settings ReadSettings(const nlohmann::json& config, std::vector<std::string>* problems = nullptr)
    {
        Settings s;
        const auto complain = [&](std::string text) {
            if (problems) problems->push_back(std::move(text));
        };
        if (!config.is_object()) return s;
        const auto section = config.find("mastery");
        if (section == config.end()) return s;
        if (!section->is_object()) {
            complain("mastery is not an object");
            return s;
        }
        // A positive finite number up to `most`, else the fallback
        const auto positive = [&](const nlohmann::json& value, const std::string& name, float fallback, float most) {
            if (value.is_number()) {
                const double v = value.get<double>();
                if (v > 0.0 && v <= most) return static_cast<float>(v);
            }
            complain(name + " " + value.dump() + " is not a number above 0 and up to " + std::to_string(most));
            return fallback;
        };

        if (const auto it = section->find("enabled"); it != section->end()) {
            if (it->is_boolean()) {
                s.enabled = it->get<bool>();
            } else {
                complain("mastery.enabled " + it->dump() + " is not true or false");
            }
        }
        if (const auto it = section->find("ranks"); it != section->end()) {
            const double v = it->is_number() ? it->get<double>() : 0.0;
            if (it->is_number() && v >= kMinRanks && v <= kMaxRanks && v == std::floor(v)) {
                s.ranks = static_cast<int>(v);
            } else {
                complain("mastery.ranks " + it->dump() + " is not a whole number from " + std::to_string(kMinRanks) +
                         " to " + std::to_string(kMaxRanks));
            }
        }
        if (const auto it = section->find("tierXP"); it != section->end()) {
            if (!it->is_object()) {
                complain("mastery.tierXP is not an object");
            } else {
                for (std::size_t i = 0; i < kTierCount; ++i) {
                    const auto entry = it->find(std::string(kTierKeys[i]));
                    if (entry == it->end()) continue;
                    s.tierXP[i] = positive(*entry, "mastery.tierXP." + std::string(kTierKeys[i]), kDefaultTierXP[i],
                                           kMaxTierXP);
                }
            }
        }
        if (const auto it = section->find("rankMultipliers"); it != section->end()) {
            std::vector<float> read;
            bool ok = it->is_array() && !it->empty();
            if (ok) {
                for (const auto& value : *it) {
                    const bool good = value.is_number() && value.get<double>() > 0.0 &&
                                      value.get<double>() <= kMaxRankMultiplier;
                    if (!good) {
                        ok = false;
                        break;
                    }
                    read.push_back(value.get<float>());
                }
            }
            if (ok) {
                s.rankMultipliers = std::move(read);
            } else {
                complain("mastery.rankMultipliers " + it->dump() + " is not a list of numbers above 0 and up to " +
                         std::to_string(kMaxRankMultiplier) + " - the default list is used");
            }
        }
        if (const auto it = section->find("skillFactor"); it != section->end()) {
            if (!it->is_object()) {
                complain("mastery.skillFactor is not an object");
            } else {
                if (const auto v = it->find("atMin"); v != it->end()) {
                    s.skillAtMin = positive(*v, "mastery.skillFactor.atMin", kDefaultSkillAtMin, kMaxSkillFactor);
                }
                if (const auto v = it->find("at100"); v != it->end()) {
                    s.skillAt100 = positive(*v, "mastery.skillFactor.at100", kDefaultSkillAt100, kMaxSkillFactor);
                }
            }
        }
        if (const auto it = section->find("xpRate"); it != section->end()) {
            s.xpRate = positive(*it, "mastery.xpRate", kDefaultXPRate, kMaxXPRate);
        }
        return s;
    }
}
