// toxicology-test - offline checks of the Toxicology addon's pure core
// (plugins/Toxicology/src/core/ToxCore.h).
//
//   toxicology-test --self-test    every check, "PASS <name>" / "FAIL <name>: <detail>"
//
// Exit 0 when every check passes, 1 on any FAIL, 2 on a bad argument.

#include "core/ToxCore.h"

#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr int kExitOk = 0;
    constexpr int kExitFail = 1;
    constexpr int kExitUsage = 2;
    constexpr float kFloatTolerance = 1e-4f;

    // UTF-8 for "독약" (poison), escaped: the build has no /utf-8, so a literal would go through the code page
    constexpr const char* kKoreanPoison = "\xEB\x8F\x85\xEC\x95\xBD";

    using nlohmann::json;
    using Toxicology::DoseResult;
    using Toxicology::PoisonCandidate;
    using Toxicology::Settings;

    // A check returns "" on success, otherwise what went wrong
    using CheckFn = std::string (*)();

    struct Check
    {
        const char* name;
        CheckFn run;
    };

    bool Near(float a_a, float a_b)
    {
        return std::fabs(a_a - a_b) <= kFloatTolerance;
    }

    std::string Describe(const Settings& a_s)
    {
        return "{enabled=" + std::to_string(a_s.enabled) + ", showIcon=" + std::to_string(a_s.showIcon) +
               ", minIntervalSeconds=" + std::to_string(a_s.minIntervalSeconds) +
               ", magickaPerDose=" + std::to_string(a_s.magickaPerDose) + "}";
    }

    std::string Describe(const DoseResult& a_r)
    {
        return "{doses=" + std::to_string(a_r.doses) + ", remainder=" + std::to_string(a_r.remainder) + "}";
    }

    std::string Describe(const std::vector<std::string>& a_items)
    {
        std::string out = "[";
        for (std::size_t i = 0; i < a_items.size(); ++i) {
            out += (i ? ", " : "") + a_items[i];
        }
        return out + "]";
    }

    std::string Describe(const std::optional<std::size_t>& a_index)
    {
        return a_index ? std::to_string(*a_index) : std::string("nullopt");
    }

    bool SameSettings(const Settings& a_a, const Settings& a_b)
    {
        return a_a.enabled == a_b.enabled && a_a.showIcon == a_b.showIcon &&
               Near(a_a.minIntervalSeconds, a_b.minIntervalSeconds) && Near(a_a.magickaPerDose, a_b.magickaPerDose);
    }

    std::string ExpectDose(float a_accumulated, float a_spent, float a_perDose, DoseResult a_expected)
    {
        const auto got = Toxicology::ConsumeMagicka(a_accumulated, a_spent, a_perDose);
        if (got.doses == a_expected.doses && Near(got.remainder, a_expected.remainder)) return {};
        return "(" + std::to_string(a_accumulated) + ", " + std::to_string(a_spent) + ", " + std::to_string(a_perDose) +
               ") gave " + Describe(got) + ", expected " + Describe(a_expected);
    }

    std::string ExpectPick(const std::vector<PoisonCandidate>& a_candidates, std::optional<std::size_t> a_expected)
    {
        const auto got = Toxicology::PickTopPoison(a_candidates);
        if (got == a_expected) return {};
        return "picked " + Describe(got) + ", expected " + Describe(a_expected);
    }

    std::string ExpectInterval(double a_last, double a_now, float a_min, bool a_expected)
    {
        const bool got = Toxicology::IntervalElapsed(a_last, a_now, a_min);
        if (got == a_expected) return {};
        return "(" + std::to_string(a_last) + ", " + std::to_string(a_now) + ", " + std::to_string(a_min) + ") gave " +
               (got ? "true" : "false");
    }

    // ---------------------------------------------------------------- settings

    std::string ParseSettingsDefaults()
    {
        std::vector<std::string> warnings;
        const auto s = Toxicology::ParseSettings(json::object(), warnings);
        if (!SameSettings(s, Settings{})) return "got " + Describe(s);
        if (!warnings.empty()) return "warnings " + Describe(warnings);
        return {};
    }

    std::string ParseSettingsValues()
    {
        std::vector<std::string> warnings;
        const auto input = json::parse(R"({"enabled":false,"showIcon":false,"minIntervalSeconds":2.5,"magickaPerDose":40})");
        const auto s = Toxicology::ParseSettings(input, warnings);
        const Settings expected{ false, false, 2.5f, 40.0f };
        if (!SameSettings(s, expected)) return "got " + Describe(s) + ", expected " + Describe(expected);
        if (!warnings.empty()) return "warnings " + Describe(warnings);
        return {};
    }

    std::string ParseSettingsBadTypes()
    {
        std::vector<std::string> warnings;
        const auto input = json::parse(R"({"enabled":"yes","minIntervalSeconds":-1,"magickaPerDose":0})");
        const auto s = Toxicology::ParseSettings(input, warnings);
        if (!SameSettings(s, Settings{})) return "got " + Describe(s);
        if (warnings.size() != 3) return "expected 3 warnings, got " + Describe(warnings);
        for (const char* key : { "enabled", "minIntervalSeconds", "magickaPerDose" }) {
            bool named = false;
            for (const auto& w : warnings) named = named || w.find(key) != std::string::npos;
            if (!named) return std::string("no warning names ") + key + ": " + Describe(warnings);
        }

        // A non-finite number (only reachable from code, JSON text has none) keeps the default too
        std::vector<std::string> nanWarnings;
        json nanInput = json::object();
        nanInput["magickaPerDose"] = std::numeric_limits<double>::quiet_NaN();
        const auto n = Toxicology::ParseSettings(nanInput, nanWarnings);
        if (!SameSettings(n, Settings{})) return "NaN magickaPerDose: got " + Describe(n);
        if (nanWarnings.size() != 1) return "NaN magickaPerDose: expected 1 warning, got " + Describe(nanWarnings);
        return {};
    }

    // ------------------------------------------------------------ poison keys

    std::string PoisonSpellKeysBasic()
    {
        std::string error;
        const auto catalog = json::parse(
            R"({"version":1,"spells":{"A.esp|0x000801":{"elements":["poison"]},"A.esp|0x000802":{"elements":["fire"]}}})");
        const auto keys = Toxicology::PoisonSpellKeys(catalog, error);
        const std::vector<std::string> expected{ "A.esp|0x000801" };
        if (keys != expected) return "got " + Describe(keys);
        if (!error.empty()) return "error set: " + error;
        return {};
    }

    std::string PoisonSpellKeysWrongVersion()
    {
        for (const char* text : { R"({"version":2,"spells":{}})", R"({"version":"1","spells":{}})",
                                  R"({"spells":{}})", R"({"version":1,"spells":[]})" }) {
            std::string error;
            const auto keys = Toxicology::PoisonSpellKeys(json::parse(text), error);
            if (!keys.empty()) return std::string(text) + ": got " + Describe(keys);
            if (error.empty()) return std::string(text) + ": no error";
        }
        return {};
    }

    std::string PoisonSpellKeysNoElements()
    {
        std::string error;
        const auto catalog = json::parse(R"({"version":1,"spells":{
            "A.esp|0x000805":{"elements":[3,"poison"]},
            "A.esp|0x000801":{},
            "A.esp|0x000802":{"elements":"poison"},
            "A.esp|0x000803":5,
            "A.esp|0x000804":{"elements":["frost","poison"]}}})");
        const auto keys = Toxicology::PoisonSpellKeys(catalog, error);
        const std::vector<std::string> expected{ "A.esp|0x000804", "A.esp|0x000805" };
        if (keys != expected) return "got " + Describe(keys) + ", expected " + Describe(expected);
        if (!error.empty()) return "error set: " + error;
        return {};
    }

    // ------------------------------------------------------------- top poison

    std::string PickTopPoisonBasic()
    {
        return ExpectPick({ { "Weak Poison", 0x1, true, 1 }, { "Deadly Poison", 0x2, true, 1 } }, 1);
    }

    std::string PickTopPoisonIgnoresUnfavorited()
    {
        return ExpectPick({ { "A", 0x1, false, 1 }, { "B", 0x2, true, 1 } }, 1);
    }

    std::string PickTopPoisonSkipsZeroCount()
    {
        return ExpectPick({ { "A", 0x1, true, 0 }, { "B", 0x2, true, 2 } }, 1);
    }

    std::string PickTopPoisonCaseInsensitive()
    {
        return ExpectPick({ { "b poison", 0x1, true, 1 }, { "A poison", 0x2, true, 1 } }, 1);
    }

    std::string PickTopPoisonKoreanAfterAscii()
    {
        return ExpectPick({ { kKoreanPoison, 0x1, true, 1 }, { "Zebra", 0x2, true, 1 } }, 1);
    }

    std::string PickTopPoisonTieByFormId()
    {
        return ExpectPick({ { "Poison", 0x20, true, 1 }, { "Poison", 0x10, true, 1 } }, 1);
    }

    std::string PickTopPoisonEmpty()
    {
        return ExpectPick({}, std::nullopt);
    }

    // ---------------------------------------------------------------- magicka

    std::string ConsumeMagickaUnder()
    {
        return ExpectDose(0.0f, 60.0f, 100.0f, { 0, 60.0f });
    }

    std::string ConsumeMagickaExact()
    {
        return ExpectDose(40.0f, 60.0f, 100.0f, { 1, 0.0f });
    }

    std::string ConsumeMagickaMulti()
    {
        return ExpectDose(90.0f, 250.0f, 100.0f, { 3, 40.0f });
    }

    std::string ConsumeMagickaBadSpent()
    {
        auto detail = ExpectDose(10.0f, -5.0f, 100.0f, { 0, 10.0f });
        if (detail.empty()) detail = ExpectDose(10.0f, std::numeric_limits<float>::quiet_NaN(), 100.0f, { 0, 10.0f });
        return detail;
    }

    // --------------------------------------------------------------- interval

    std::string IntervalFirstTime()
    {
        return ExpectInterval(-1.0, 5.0, 1.0f, true);
    }

    std::string IntervalTooSoon()
    {
        return ExpectInterval(5.0, 5.5, 1.0f, false);
    }

    std::string IntervalElapsedExactly()
    {
        return ExpectInterval(5.0, 6.0, 1.0f, true);
    }

    // ---------------------------------------------------------- catalog keys

    std::string ExpectKey(std::string_view a_key, const char* a_plugin, std::uint32_t a_local)
    {
        const auto parts = Toxicology::SplitCatalogKey(a_key);
        if (!parts) return std::string(a_key) + ": got nullopt";
        if (parts->plugin != a_plugin || parts->local != a_local) {
            return std::string(a_key) + ": got {" + parts->plugin + ", " + std::to_string(parts->local) + "}";
        }
        return {};
    }

    std::string ExpectRejected(std::string_view a_key)
    {
        const auto parts = Toxicology::SplitCatalogKey(a_key);
        if (parts) return std::string(a_key) + ": accepted as {" + parts->plugin + ", " + std::to_string(parts->local) + "}";
        return {};
    }

    std::string SplitKeyBasic()
    {
        auto detail = ExpectKey("A.esp|0x000D62", "A.esp", 0xD62);
        if (detail.empty()) detail = ExpectKey("A.esp|0X000d62", "A.esp", 0xD62);
        return detail;
    }

    std::string SplitKeyNoPrefix()
    {
        return ExpectKey("A.esp|FFFFFF", "A.esp", 0xFFFFFF);
    }

    // Past a full plugin's 24 bits: rejected (the light plugin's 12-bit limit is ToxData's check)
    std::string SplitKeyTooLarge()
    {
        return ExpectRejected("A.esp|1000000");
    }

    std::string SplitKeyBad()
    {
        for (const auto key : { "|0x1", "A.esp|", "A.esp|0x", "A.esp|0xZZ", "A.esp|0x12G", "A.esp" }) {
            if (auto detail = ExpectRejected(key); !detail.empty()) return detail;
        }
        return {};
    }

    constexpr Check kChecks[] = {
        { "ParseSettingsDefaults", ParseSettingsDefaults },
        { "ParseSettingsValues", ParseSettingsValues },
        { "ParseSettingsBadTypes", ParseSettingsBadTypes },
        { "PoisonSpellKeysBasic", PoisonSpellKeysBasic },
        { "PoisonSpellKeysWrongVersion", PoisonSpellKeysWrongVersion },
        { "PoisonSpellKeysNoElements", PoisonSpellKeysNoElements },
        { "PickTopPoisonBasic", PickTopPoisonBasic },
        { "PickTopPoisonIgnoresUnfavorited", PickTopPoisonIgnoresUnfavorited },
        { "PickTopPoisonSkipsZeroCount", PickTopPoisonSkipsZeroCount },
        { "PickTopPoisonCaseInsensitive", PickTopPoisonCaseInsensitive },
        { "PickTopPoisonKoreanAfterAscii", PickTopPoisonKoreanAfterAscii },
        { "PickTopPoisonTieByFormId", PickTopPoisonTieByFormId },
        { "PickTopPoisonEmpty", PickTopPoisonEmpty },
        { "ConsumeMagickaUnder", ConsumeMagickaUnder },
        { "ConsumeMagickaExact", ConsumeMagickaExact },
        { "ConsumeMagickaMulti", ConsumeMagickaMulti },
        { "ConsumeMagickaBadSpent", ConsumeMagickaBadSpent },
        { "IntervalFirstTime", IntervalFirstTime },
        { "IntervalTooSoon", IntervalTooSoon },
        { "IntervalElapsedExactly", IntervalElapsedExactly },
        { "SplitKeyBasic", SplitKeyBasic },
        { "SplitKeyNoPrefix", SplitKeyNoPrefix },
        { "SplitKeyTooLarge", SplitKeyTooLarge },
        { "SplitKeyBad", SplitKeyBad },
    };

    int SelfTest()
    {
        int failed = 0;
        for (const auto& check : kChecks) {
            std::string detail;
            try {
                detail = check.run();
            } catch (const std::exception& e) {
                detail = std::string("threw ") + e.what();
            }
            if (detail.empty()) {
                std::printf("PASS %s\n", check.name);
            } else {
                std::printf("FAIL %s: %s\n", check.name, detail.c_str());
                ++failed;
            }
        }
        std::printf("%d of %zu checks failed\n", failed, std::size(kChecks));
        return failed ? kExitFail : kExitOk;
    }
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") return SelfTest();
    std::fprintf(stderr, "usage: toxicology-test --self-test\n");
    return kExitUsage;
}
