#include "Common.h"
#include "CombatConfig.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace SLCombat
{
    namespace
    {
        constexpr const char* kConfigPath = "Data/SKSE/Plugins/SL_Combat.json";

        template <class T>
        void Read(const nlohmann::json& a_json, const char* a_key, T& a_out)
        {
            auto it = a_json.find(a_key);
            if (it == a_json.end()) {
                return;
            }
            try {
                a_out = it->get<T>();
            } catch (const std::exception& e) {
                logger::warn("SL_Combat: config key '{}' ignored ({})", a_key, e.what());
            }
        }
    }

    bool CombatConfig::Save() const
    {
        nlohmann::ordered_json json;
        json["enableCounter"] = enableCounter;
        json["counterWindow"] = counterWindow;
        json["wardBonusWindow"] = wardBonusWindow;
        json["frontAngle"] = frontAngle;
        json["skipIfOtherParryMod"] = skipIfOtherParryMod;
        json["filterMode"] = filterMode == FilterMode::kShift ? "shift" : "systemGroup";
        json["hookReturnOnCounter"] = hookReturnOnCounter;
        json["requireKnownWard"] = requireKnownWard;
        json["counterResult"] = counterResult == CounterResult::kAbsorb  ? "absorb" :
                                counterResult == CounterResult::kReflect ? "reflect" :
                                                                           "ward";
        json["dualCastPerk"] = dualCastPerk;
        json["warnLeadSeconds"] = warnLeadSeconds;
        json["warnRadius"] = warnRadius;
        json["promptText"] = promptText;
        json["wardLingerSeconds"] = wardLingerSeconds;
        json["holdWard"] = holdWard;
        json["holdRefreshSeconds"] = holdRefreshSeconds;
        json["sphereWardAllAngles"] = sphereWardAllAngles;
        json["slowMoScale"] = slowMoScale;
        json["slowMoDuration"] = slowMoDuration;
        json["freeNextCast"] = freeNextCast;
        json["freeCastWindow"] = freeCastWindow;
        json["xpPerCounter"] = xpPerCounter;
        json["hudText"] = hudText;
        json["testTools"] = testTools;

        std::ofstream file(kConfigPath, std::ios::trunc);
        if (!file.is_open()) {
            logger::warn("SL_Combat: could not write {}", kConfigPath);
            return false;
        }
        file << json.dump(4) << '\n';
        return static_cast<bool>(file);
    }

    CombatConfig& CombatConfig::Get()
    {
        static CombatConfig instance;
        return instance;
    }

    void CombatConfig::Load()
    {
        std::ifstream file(kConfigPath);
        if (!file.is_open()) {
            logger::info("SL_Combat: no {} - using defaults", kConfigPath);
            return;
        }

        nlohmann::json json;
        try {
            json = nlohmann::json::parse(file, nullptr, true, true);
        } catch (const std::exception& e) {
            logger::warn("SL_Combat: {} could not be read ({}) - using defaults", kConfigPath, e.what());
            return;
        }

        Read(json, "enableCounter", enableCounter);
        Read(json, "counterWindow", counterWindow);
        Read(json, "wardBonusWindow", wardBonusWindow);
        Read(json, "frontAngle", frontAngle);
        Read(json, "skipIfOtherParryMod", skipIfOtherParryMod);
        Read(json, "hookReturnOnCounter", hookReturnOnCounter);
        Read(json, "requireKnownWard", requireKnownWard);

        Read(json, "wardLingerSeconds", wardLingerSeconds);
        Read(json, "holdWard", holdWard);
        Read(json, "holdRefreshSeconds", holdRefreshSeconds);
        Read(json, "sphereWardAllAngles", sphereWardAllAngles);
        Read(json, "warnLeadSeconds", warnLeadSeconds);
        Read(json, "warnRadius", warnRadius);
        Read(json, "promptText", promptText);
        Read(json, "slowMoScale", slowMoScale);
        Read(json, "slowMoDuration", slowMoDuration);
        Read(json, "freeNextCast", freeNextCast);
        Read(json, "freeCastWindow", freeCastWindow);
        Read(json, "xpPerCounter", xpPerCounter);
        Read(json, "hudText", hudText);
        Read(json, "testTools", testTools);

        Read(json, "dualCastPerk", dualCastPerk);

        std::string result;
        Read(json, "counterResult", result);
        if (result == "absorb") {
            counterResult = CounterResult::kAbsorb;
        } else if (result == "reflect") {
            counterResult = CounterResult::kReflect;
        } else if (!result.empty() && result != "ward") {
            logger::warn("SL_Combat: counterResult '{}' unknown - using 'ward'", result);
        }

        std::string mode;
        Read(json, "filterMode", mode);
        if (mode == "systemGroup") {
            filterMode = FilterMode::kSystemGroup;
        } else if (!mode.empty() && mode != "shift") {
            logger::warn("SL_Combat: filterMode '{}' unknown - using 'shift'", mode);
        }

        logger::info("SL_Combat: config loaded - counter {} window {:.2f}s (+{:.2f}s ward) angle {:.0f} slowmo {:.2f}x/{:.2f}s freeCast {} xp {:.1f} filter {}",
            enableCounter, counterWindow, wardBonusWindow, frontAngle, slowMoScale, slowMoDuration,
            freeNextCast, xpPerCounter, filterMode == FilterMode::kShift ? "shift" : "systemGroup");
    }
}
