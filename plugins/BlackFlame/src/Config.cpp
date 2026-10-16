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
        constexpr float kMinSelfTestDelay = 0.0f;
        constexpr float kMaxSelfTestDelay = 600.0f;

        Config g_config;

        struct BoolKey
        {
            const char* name;
            bool Config::*field;
        };

        constexpr std::array kBoolKeys{
            BoolKey{ "burnShader", &Config::burnShader },
            BoolKey{ "hitArt", &Config::hitArt },
            BoolKey{ "hazard", &Config::hazard },
            BoolKey{ "projectile", &Config::projectile },
            BoolKey{ "explosion", &Config::explosion },
            BoolKey{ "handArt", &Config::handArt },
            BoolKey{ "boundWeapon", &Config::boundWeapon },
            BoolKey{ "scriptPlaced", &Config::scriptPlaced },
            BoolKey{ "impactSwap", &Config::impactSwap },
            BoolKey{ "hazardSwap", &Config::hazardSwap },
            BoolKey{ "light", &Config::light },
            BoolKey{ "copyLaunch", &Config::copyLaunch },
            BoolKey{ "copyLaunchExplosion", &Config::copyLaunchExplosion },
            BoolKey{ "copyExplosionCreate", &Config::copyExplosionCreate },
            BoolKey{ "copyImpacts", &Config::copyImpacts },
            BoolKey{ "copyHitArt", &Config::copyHitArt },
            BoolKey{ "writeRequest", &Config::writeRequest },
            BoolKey{ "forcePalette", &Config::forcePalette },
            BoolKey{ "selfTest", &Config::selfTest },
            BoolKey{ "testerMode", &Config::testerMode },
            BoolKey{ "particleColors", &Config::particleColors },
        };

        bool IsKnownKey(std::string_view key)
        {
            if (key == "tintScale" || key == "lightColor" || key == "burnMode" || key == "selfTestDelaySeconds") {
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

        // burnMode is gone: the hit shader is swapped where the engine asks for it, so there is one
        // shader per hit and nothing to lay on top. An old config keeps loading; the key is ignored.
        void ReadBurnMode(const nlohmann::json& j)
        {
            const auto it = j.find("burnMode");
            if (it != j.end()) {
                logger::info("BlackFlame: config 'burnMode' {} is ignored (no longer used: the engine now makes the hit "
                             "shader from ours, so there is no overlay)", it->dump());
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

        void ReadSelfTestDelay(const nlohmann::json& j, Config& out)
        {
            const auto it = j.find("selfTestDelaySeconds");
            if (it == j.end()) {
                return;
            }
            if (!it->is_number() || it->get<float>() < kMinSelfTestDelay || it->get<float>() > kMaxSelfTestDelay) {
                logger::warn("BlackFlame: config 'selfTestDelaySeconds' {} is not a number in {}..{} - keeping {}", it->dump(),
                    kMinSelfTestDelay, kMaxSelfTestDelay, out.selfTestDelaySeconds);
                return;
            }
            out.selfTestDelaySeconds = it->get<float>();
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

        void LogConfig()
        {
            logger::info(
                "BlackFlame: config burnShader={} hitArt={} projectile={} explosion={} hazard={} handArt={} boundWeapon={} scriptPlaced={} impactSwap={} hazardSwap={} light={} "
                "forcePalette={} selfTest={} particleColors={} copyLaunch={} copyLaunchExplosion={} copyExplosionCreate={} "
                "copyImpacts={} copyHitArt={} writeRequest={} testerMode={} selfTestDelaySeconds={} tintScale={} lightColor=[{}, {}, {}]",
                g_config.burnShader, g_config.hitArt, g_config.projectile, g_config.explosion, g_config.hazard,
                g_config.handArt, g_config.boundWeapon, g_config.scriptPlaced, g_config.impactSwap, g_config.hazardSwap, g_config.light, g_config.forcePalette, g_config.selfTest, g_config.particleColors,
                g_config.copyLaunch, g_config.copyLaunchExplosion, g_config.copyExplosionCreate, g_config.copyImpacts,
                g_config.copyHitArt, g_config.writeRequest, g_config.testerMode, g_config.selfTestDelaySeconds,
                g_config.tintScale, g_config.lightColor.red, g_config.lightColor.green, g_config.lightColor.blue);
            if (g_config.particleColors) {
                logger::warn("BlackFlame: 'particleColors' is on - particle colour modifiers in the player's recoloured "
                             "3D are desaturated through a layout CommonLib does not have (unverified on AE)");
            }
        }
    }

    void LoadConfig()
    {
        std::ifstream file(kConfigPath, std::ios::binary);
        if (!file) {
            logger::warn("BlackFlame: no config at '{}' - using the defaults", kConfigPath);
            LogConfig();
            return;
        }
        std::stringstream text;
        text << file.rdbuf();
        const auto j = nlohmann::json::parse(text.str(), nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            logger::warn("BlackFlame: '{}' is not a JSON object - using the defaults", kConfigPath);
            LogConfig();
            return;
        }
        for (const auto& [key, value] : j.items()) {
            if (!IsKnownKey(key)) {
                logger::warn("BlackFlame: config key '{}' is not known - ignored", key);
            }
        }

        Config next;
        ReadBools(j, next);
        ReadBurnMode(j);
        ReadTintScale(j, next);
        ReadSelfTestDelay(j, next);
        ReadLightColor(j, next);
        if (next.testerMode) {
            // the tester package: the self-test and the copy request on; every info line reaches disk at once (a crash keeps it)
            next.selfTest = true;
            next.writeRequest = true;
            spdlog::flush_on(spdlog::level::info);
            logger::info("BlackFlame: testerMode on: selfTest and writeRequest forced on, every log line written through");
        }
        g_config = next;
        LogConfig();
    }

    void DisableEngineWrites(const char* a_why)
    {
        g_config.hitArt = false;
        g_config.projectile = false;
        g_config.explosion = false;
        g_config.hazard = false;
        g_config.handArt = false;
        g_config.boundWeapon = false;
        g_config.light = false;
        g_config.particleColors = false;
        logger::warn("BlackFlame: config overridden for this session ({}): hitArt, projectile, explosion, hazard, handArt, "
                     "boundWeapon, light and particleColors false; the copies and the per-call swaps stay on", a_why);
    }

    const Config& GetConfig() noexcept
    {
        return g_config;
    }
}
