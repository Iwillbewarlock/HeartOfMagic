#include "Config.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace BlackFlame
{
    namespace
    {
        constexpr float kMinLightChannel = 0.0f;
        constexpr float kMaxLightChannel = 1.0f;
        constexpr std::size_t kLightChannels = 3;
        constexpr float kMinSelfTestDelay = 0.0f;
        constexpr float kMaxSelfTestDelay = 600.0f;
        constexpr std::string_view kSaveExt = ".ess";
        constexpr std::size_t kMaxSaveNameLength = 200;
        constexpr const char* kSaveNameForbidden = "/\\:*?\"<>|";

        Config g_config;

        struct BoolKey
        {
            const char* name;
            bool Config::*field;
        };

        constexpr std::array kBoolKeys{
            BoolKey{ "burnShader", &Config::burnShader },
            BoolKey{ "hitArt", &Config::hitArt },
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
            BoolKey{ "copyModelProbeAlways", &Config::copyModelProbeAlways },
            BoolKey{ "copyCastingArt", &Config::copyCastingArt },
            BoolKey{ "refreshCastingArt", &Config::refreshCastingArt },
            BoolKey{ "copyPlaced", &Config::copyPlaced },
            BoolKey{ "copyHazardCreate", &Config::copyHazardCreate },
            BoolKey{ "writeRequest", &Config::writeRequest },
            BoolKey{ "selfTest", &Config::selfTest },
            BoolKey{ "testerMode", &Config::testerMode },
            BoolKey{ "devAutoE3", &Config::devAutoE3 },
            BoolKey{ "devAutoQuit", &Config::devAutoQuit },
        };

        // C6: keys of the retired runtime recolour (and burnMode before it). An old json keeps loading: each is logged
        // once as retired and ignored, not as an unknown key
        constexpr std::array<std::string_view, 7> kRetiredKeys{ "projectile", "explosion", "hazard", "forcePalette",
            "particleColors", "tintScale", "burnMode" };

        bool IsRetiredKey(std::string_view key)
        {
            return std::ranges::find(kRetiredKeys, key) != kRetiredKeys.end();
        }

        bool IsKnownKey(std::string_view key)
        {
            if (key == "lightColor" || key == "selfTestDelaySeconds" || key == "devAutoLoad" || key == "copyModelProbe") {
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

        std::string_view RetiredWhy(std::string_view key)
        {
            if (key == "burnMode") {
                return "the hit shader is swapped where the engine asks for it, so there is no overlay";
            }
            if (key == "forcePalette" || key == "particleColors" || key == "tintScale") {
                return "the runtime recolour is retired (C6); the builder bakes the copies' palette, colour keys and tint";
            }
            return "the runtime recolour of projectiles, explosions and hazards is retired (C6): their copies are grey";
        }

        // copyModelProbe: "off" / "log" / "drop" (false = "off"; true = "log", never "drop": dropping must be asked for by name)
        void ReadProbeMode(const nlohmann::json& j, Config& out)
        {
            const auto it = j.find("copyModelProbe");
            if (it == j.end()) {
                return;
            }
            if (it->is_boolean()) {
                out.copyModelProbe = it->get<bool>() ? ProbeMode::kLog : ProbeMode::kOff;
                return;
            }
            const std::string text = it->is_string() ? it->get<std::string>() : std::string();
            if (text == "off") {
                out.copyModelProbe = ProbeMode::kOff;
            } else if (text == "log") {
                out.copyModelProbe = ProbeMode::kLog;
            } else if (text == "drop") {
                out.copyModelProbe = ProbeMode::kDrop;
            } else {
                logger::warn("BlackFlame: config 'copyModelProbe' {} is not \"off\", \"log\" or \"drop\" - keeping {}", it->dump(),
                    ProbeModeName(out.copyModelProbe));
            }
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

        // Dev only: a save name in the game's save folder (no folder part); a trailing .ess is dropped
        void ReadDevAutoLoad(const nlohmann::json& j, Config& out)
        {
            const auto it = j.find("devAutoLoad");
            if (it == j.end()) {
                return;
            }
            if (!it->is_string()) {
                logger::warn("BlackFlame: config 'devAutoLoad' is not a string ({}) - no save is loaded", it->dump());
                return;
            }
            auto name = it->get<std::string>();
            if (name.size() > kSaveExt.size() && name.ends_with(kSaveExt)) {
                name.resize(name.size() - kSaveExt.size());
            }
            if (name.empty() || name.size() > kMaxSaveNameLength ||
                name.find_first_of(kSaveNameForbidden) != std::string::npos || name.find("..") != std::string::npos) {
                logger::warn("BlackFlame: config 'devAutoLoad' '{}' is not a save name (1..{} characters, no folder part, "
                             "none of {}) - no save is loaded", it->get<std::string>(), kMaxSaveNameLength, kSaveNameForbidden);
                return;
            }
            out.devAutoLoad = std::move(name);
        }

        // The dev keys run the self-test unattended: without the self-test there is nothing for them to do
        void GateDevKeys(Config& out)
        {
            if (out.selfTest || (out.devAutoLoad.empty() && !out.devAutoE3 && !out.devAutoQuit)) {
                return;
            }
            logger::warn("BlackFlame: config devAutoLoad/devAutoE3/devAutoQuit ignored: they work only with selfTest (or "
                         "testerMode) on");
            out.devAutoLoad.clear();
            out.devAutoE3 = false;
            out.devAutoQuit = false;
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
                "BlackFlame: config burnShader={} hitArt={} handArt={} boundWeapon={} scriptPlaced={} impactSwap={} hazardSwap={} light={} "
                "selfTest={} copyLaunch={} copyLaunchExplosion={} copyExplosionCreate={} "
                "copyImpacts={} copyHitArt={} copyModelProbe={} copyModelProbeAlways={} copyCastingArt={} refreshCastingArt={} copyHazardCreate={} copyPlaced={} writeRequest={} testerMode={} devAutoLoad='{}' devAutoE3={} devAutoQuit={} selfTestDelaySeconds={} lightColor=[{}, {}, {}]",
                g_config.burnShader, g_config.hitArt, g_config.handArt, g_config.boundWeapon, g_config.scriptPlaced, g_config.impactSwap,
                g_config.hazardSwap, g_config.light, g_config.selfTest,
                g_config.copyLaunch, g_config.copyLaunchExplosion, g_config.copyExplosionCreate, g_config.copyImpacts,
                g_config.copyHitArt, ProbeModeName(g_config.copyModelProbe), g_config.copyModelProbeAlways, g_config.copyCastingArt, g_config.refreshCastingArt, g_config.copyHazardCreate, g_config.copyPlaced, g_config.writeRequest, g_config.testerMode, g_config.devAutoLoad, g_config.devAutoE3, g_config.devAutoQuit, g_config.selfTestDelaySeconds,
                g_config.lightColor.red, g_config.lightColor.green, g_config.lightColor.blue);
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
            if (IsRetiredKey(key)) {
                logger::info("BlackFlame: config key '{}' ({}) is retired, ignored: {}", key, value.dump(), RetiredWhy(key));
            } else if (!IsKnownKey(key)) {
                logger::warn("BlackFlame: config key '{}' is not known - ignored", key);
            }
        }

        Config next;
        ReadBools(j, next);
        ReadProbeMode(j, next);
        ReadSelfTestDelay(j, next);
        ReadLightColor(j, next);
        if (next.testerMode) {
            // the tester package: the self-test and the copy request on; every info line reaches disk at once (a crash keeps it)
            next.selfTest = true;
            next.writeRequest = true;
            spdlog::flush_on(spdlog::level::info);
            logger::info("BlackFlame: testerMode on: selfTest and writeRequest forced on, every log line written through");
        }
        ReadDevAutoLoad(j, next);
        GateDevKeys(next);
        g_config = next;
        LogConfig();
    }

    void DisableEngineWrites(const char* a_why)
    {
        g_config.hitArt = false;
        g_config.handArt = false;
        g_config.boundWeapon = false;
        g_config.light = false;
        g_config.refreshCastingArt = false;  // Detach + SetCastingArt only from the main-thread queue (HandArtRefresh.h)
        logger::warn("BlackFlame: config overridden for this session ({}): hitArt, handArt, boundWeapon, light and refreshCastingArt false; the "
                     "copies and the per-call swaps stay on", a_why);
    }

    const char* ProbeModeName(ProbeMode a_mode) noexcept
    {
        switch (a_mode) {
        case ProbeMode::kOff:
            return "off";
        case ProbeMode::kDrop:
            return "drop";
        default:
            return "log";
        }
    }

    const Config& GetConfig() noexcept
    {
        return g_config;
    }
}
