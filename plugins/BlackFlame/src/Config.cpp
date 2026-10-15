#include "Config.h"

#include <nlohmann/json.hpp>

#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace BlackFlame
{
    namespace
    {
        constexpr float kMinTintScale = 0.0f;
        constexpr float kMaxTintScale = 2.0f;
        constexpr float kMinLightChannel = 0.0f;
        constexpr float kMaxLightChannel = 1.0f;
        constexpr std::size_t kLightChannels = 3;

        Config g_config;

        struct BoolKey
        {
            const char* name;
            bool Config::*field;
        };

        constexpr std::array kBoolKeys{
            BoolKey{ "burnShader", &Config::burnShader },
            BoolKey{ "projectile", &Config::projectile },
            BoolKey{ "explosion", &Config::explosion },
            BoolKey{ "handArt", &Config::handArt },
            BoolKey{ "light", &Config::light },
            BoolKey{ "particleColors", &Config::particleColors },
        };

        bool IsKnownKey(std::string_view key)
        {
            if (key == "tintScale" || key == "lightColor") {
                return true;
            }
            for (const auto& b : kBoolKeys) {
                if (key == b.name) {
                    return true;
                }
            }
            return false;
        }

        void ReadBools(const nlohmann::json& j, Config& out)
        {
            for (const auto& b : kBoolKeys) {
                const auto it = j.find(b.name);
                if (it == j.end()) {
                    continue;
                }
                if (!it->is_boolean()) {
                    logger::warn("BlackFlame: config '{}' is not true/false ({}) - keeping {}", b.name, it->dump(),
                        out.*b.field);
                    continue;
                }
                out.*b.field = it->get<bool>();
            }
        }

        void ReadTintScale(const nlohmann::json& j, Config& out)
        {
            const auto it = j.find("tintScale");
            if (it == j.end()) {
                return;
            }
            if (!it->is_number()) {
                logger::warn("BlackFlame: config 'tintScale' is not a number ({}) - keeping {}", it->dump(), out.tintScale);
                return;
            }
            const float value = it->get<float>();
            if (value < kMinTintScale || value > kMaxTintScale) {
                logger::warn("BlackFlame: config 'tintScale' {} is outside {}..{} - keeping {}", value, kMinTintScale,
                    kMaxTintScale, out.tintScale);
                return;
            }
            out.tintScale = value;
        }

        void ReadLightColor(const nlohmann::json& j, Config& out)
        {
            const auto it = j.find("lightColor");
            if (it == j.end()) {
                return;
            }
            const auto keep = [&](const char* why) {
                logger::warn("BlackFlame: config 'lightColor' {} ({}) - keeping [{}, {}, {}]", why, it->dump(),
                    out.lightColor.red, out.lightColor.green, out.lightColor.blue);
            };
            if (!it->is_array() || it->size() != kLightChannels) {
                keep("is not an array of 3 numbers");
                return;
            }
            std::array<float, kLightChannels> rgb{};
            for (std::size_t i = 0; i < kLightChannels; ++i) {
                const auto& v = (*it)[i];
                if (!v.is_number()) {
                    keep("has a value that is not a number");
                    return;
                }
                rgb[i] = v.get<float>();
                if (rgb[i] < kMinLightChannel || rgb[i] > kMaxLightChannel) {
                    keep("has a value outside 0..1");
                    return;
                }
            }
            out.lightColor = RE::NiColor(rgb[0], rgb[1], rgb[2]);
        }
    }

    void LoadConfig()
    {
        std::ifstream file(kConfigPath, std::ios::binary);
        if (!file) {
            logger::warn("BlackFlame: no config at '{}' - using the defaults", kConfigPath);
            return;
        }
        std::stringstream text;
        text << file.rdbuf();
        const auto j = nlohmann::json::parse(text.str(), nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            logger::warn("BlackFlame: '{}' is not a JSON object - using the defaults", kConfigPath);
            return;
        }
        for (const auto& [key, value] : j.items()) {
            if (!IsKnownKey(key)) {
                logger::warn("BlackFlame: config key '{}' is not known - ignored", key);
            }
        }

        Config next;
        ReadBools(j, next);
        ReadTintScale(j, next);
        ReadLightColor(j, next);
        g_config = next;

        logger::info(
            "BlackFlame: config burnShader={} projectile={} explosion={} handArt={} light={} particleColors={} "
            "tintScale={} lightColor=[{}, {}, {}]",
            g_config.burnShader, g_config.projectile, g_config.explosion, g_config.handArt, g_config.light,
            g_config.particleColors, g_config.tintScale, g_config.lightColor.red, g_config.lightColor.green,
            g_config.lightColor.blue);
        if (g_config.particleColors) {
            logger::warn("BlackFlame: 'particleColors' is not implemented in this prototype - it changes nothing");
        }
    }

    const Config& GetConfig() noexcept
    {
        return g_config;
    }
}
