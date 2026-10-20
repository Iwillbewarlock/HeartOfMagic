#include "Config.h"

#include "OldCopiesRule.h"
#include "CopyContract.h"

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
            BoolKey{ "singleLaunch", &Config::singleLaunch },
            BoolKey{ "launchBuilder", &Config::launchBuilder },
            BoolKey{ "slotPreflight", &Config::slotPreflight },
            BoolKey{ "selfTestSlots", &Config::selfTestSlots },
            BoolKey{ "slotTxst", &Config::slotTxst },
            BoolKey{ "cleanOldCopies", &Config::cleanOldCopies },
            BoolKey{ "greyCorpseShaders", &Config::greyCorpseShaders },
        };

        // Single launch: the tints the builder bakes (GreyBuild.h --tint / --tint-add take 0..2) and the job memory cap
        constexpr double kMinTint = 0.0;
        constexpr double kMaxTint = 2.0;
        constexpr std::uint32_t kMinJobMemoryMiB = 256;
        constexpr std::uint32_t kMaxJobMemoryMiB = 16384;
        constexpr double kMinCorpseWindow = 1.0;  // corpseWindowGameSeconds (CorpseMark.h)
        constexpr double kMaxCorpseWindow = 3600.0;

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
            if (key == "lightColor" || key == "selfTestDelaySeconds" || key == "devAutoLoad" || key == "copyModelProbe" ||
                key == "tint" || key == "tintAdd" || key == "tintAddHands" || key == "paletteMode" || key == "builderJobMemoryMiB" ||
                key == "handMarbleOpacity" || key == "handMarbleScale" ||
                key == "corpseWindowGameSeconds" || key == "oldCopiesGraceDays") {
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

        void ReadTint(const nlohmann::json& j, const char* a_key, double& a_out)
        {
            const auto it = j.find(a_key);
            if (it == j.end()) {
                return;
            }
            if (!it->is_number() || !(it->get<double>() >= kMinTint && it->get<double>() <= kMaxTint)) {
                logger::warn("BlackFlame: config '{}' {} is not a number in {}..{} - keeping {}", a_key, it->dump(), kMinTint,
                    kMaxTint, a_out);
                return;
            }
            a_out = it->get<double>();
        }

        // Single launch: tint, tintAdd, paletteMode, builderJobMemoryMiB, oldCopiesGraceDays
        void ReadSingleLaunch(const nlohmann::json& j, Config& out)
        {
            ReadTint(j, "tint", out.tint);
            ReadTint(j, "tintAdd", out.tintAdd);
            if (const auto it = j.find("tintAddHands"); it != j.end()) {
                if (it->is_number()) {
                    out.tintAddHands = CopyContract::ClampTintAddHands(it->get<double>());
                    if (out.tintAddHands != it->get<double>()) {
                        logger::warn("BlackFlame: config tintAddHands {} clamped/quantised to {}", it->dump(), out.tintAddHands);
                    }
                } else {
                    logger::warn("BlackFlame: config tintAddHands {} is not a number - keeping {}", it->dump(), out.tintAddHands);
                }
            }
            for (const auto* key : { "handMarbleOpacity", "handMarbleScale" }) {
                const auto it = j.find(key);
                if (it == j.end()) {
                    continue;
                }
                const bool opacity = std::string_view(key) == "handMarbleOpacity";
                auto& value = opacity ? out.handMarbleOpacity : out.handMarbleScale;
                if (!it->is_number()) {
                    logger::warn("BlackFlame: config {} {} is not a number - keeping {}", key, it->dump(), value);
                    continue;
                }
                value = opacity ? CopyContract::ClampHandMarbleOpacity(it->get<double>()) :
                    CopyContract::ClampHandMarbleScale(it->get<double>());
                if (value != it->get<double>()) {
                    logger::warn("BlackFlame: config {} {} clamped/quantised to {}", key, it->dump(), value);
                }
            }
            if (const auto it = j.find("paletteMode"); it != j.end()) {
                const std::string text = it->is_string() ? it->get<std::string>() : std::string();
                if (text == "derived" || text == "godskin") {
                    out.paletteGodskin = text == "godskin";
                } else {
                    logger::warn("BlackFlame: config 'paletteMode' {} is not \"derived\" or \"godskin\" - keeping {}", it->dump(),
                        out.paletteGodskin ? "godskin" : "derived");
                }
            }
            if (const auto it = j.find("builderJobMemoryMiB"); it != j.end()) {
                if (!it->is_number_unsigned() || it->get<std::uint64_t>() < kMinJobMemoryMiB ||
                    it->get<std::uint64_t>() > kMaxJobMemoryMiB) {
                    logger::warn("BlackFlame: config 'builderJobMemoryMiB' {} is not a whole number in {}..{} - keeping {}",
                        it->dump(), kMinJobMemoryMiB, kMaxJobMemoryMiB, out.builderJobMemoryMiB);
                } else {
                    out.builderJobMemoryMiB = static_cast<std::uint32_t>(it->get<std::uint64_t>());
                }
            }
            if (const auto it = j.find("oldCopiesGraceDays"); it != j.end()) {
                if (!it->is_number_unsigned() || !OldCopiesRule::GraceDaysValid(it->get<std::uint64_t>())) {
                    logger::warn("BlackFlame: config 'oldCopiesGraceDays' {} is not a whole number in {}..{} - keeping {}", it->dump(),
                        OldCopiesRule::kMinGraceDays, OldCopiesRule::kMaxGraceDays, out.oldCopiesGraceDays);
                } else {
                    out.oldCopiesGraceDays = static_cast<std::uint32_t>(it->get<std::uint64_t>());
                }
            }
        }

        void ReadCorpseWindow(const nlohmann::json& j, Config& out)
        {
            const auto it = j.find("corpseWindowGameSeconds");
            if (it == j.end()) {
                return;
            }
            if (!it->is_number() || !(it->get<double>() >= kMinCorpseWindow && it->get<double>() <= kMaxCorpseWindow)) {
                logger::warn("BlackFlame: config 'corpseWindowGameSeconds' {} is not a number in {}..{} - keeping {}", it->dump(),
                    kMinCorpseWindow, kMaxCorpseWindow, out.corpseWindowGameSeconds);
                return;
            }
            out.corpseWindowGameSeconds = it->get<double>();
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
            logger::info("BlackFlame: config singleLaunch={} launchBuilder={} slotPreflight={} selfTestSlots={} slotTxst={} tint={} "
                         "tintAdd={} tintAddHands={} handMarbleOpacity={} handMarbleScale={} paletteMode={} builderJobMemoryMiB={} cleanOldCopies={} oldCopiesGraceDays={}", g_config.singleLaunch,
                g_config.launchBuilder, g_config.slotPreflight, g_config.selfTestSlots, g_config.slotTxst, g_config.tint,
                g_config.tintAdd, g_config.tintAddHands, g_config.handMarbleOpacity, g_config.handMarbleScale,
                g_config.paletteGodskin ? "godskin" : "derived", g_config.builderJobMemoryMiB, g_config.cleanOldCopies,
                g_config.oldCopiesGraceDays);
            logger::info("BlackFlame: config greyCorpseShaders={} corpseWindowGameSeconds={}", g_config.greyCorpseShaders,
                g_config.corpseWindowGameSeconds);
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
        ReadSingleLaunch(j, next);
        ReadCorpseWindow(j, next);
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
