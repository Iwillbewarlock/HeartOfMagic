// progression-check's practice and memory cases (librarian/SpellResearchMemoryMath.h), run by
// progression-check.cpp: the boundaries a cast's progress crosses, Spell Research's research gain
// table and global cap, the known share and the practice multiplier, the settings reader.

#include "progression-check.h"

#include "librarian/SpellResearchMemoryMath.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

namespace ProgressionCheck
{
    namespace
    {
        namespace MM = Librarian::SpellResearchMemoryMath;

        // The gains run up to ~830 at v = 6.25, where a float step is ~6e-5: compared relatively
        bool NearRelative(float a, double b)
        {
            constexpr double kRelative = 1e-6;
            return std::fabs(static_cast<double>(a) - b) <= kRelative * (std::fabs(b) > 1.0 ? std::fabs(b) : 1.0);
        }

        std::size_t PaidRanks(const std::array<float, MM::kRanks>& gains)
        {
            std::size_t paid = 0;
            for (const float gain : gains) paid += gain > 0.0f ? 1 : 0;
            return paid;
        }
    }

    void MemoryCases()
    {
        // ---- Normal --------------------------------------------------------------------------
        Check(MM::BoundariesCrossed(0.10f, 0.32f) == 2, "memory: 10% -> 32% crosses 15 and 30 (2 payments)");
        Check(MM::BoundariesCrossed(0.0f, 1.0f) == 7, "memory: 0 -> mastery crosses all 7 boundaries");
        Check(MM::BoundariesCrossed(0.16f, 0.29f) == 0, "memory: 16% -> 29% crosses none");

        const float v = MM::ResearchValue(15.0f, 25.0f);
        Check(Near(v, 6.25f), "memory: v = timeBase 15 x 25 / 60 = 6.25", std::to_string(v));

        const auto novice = MM::GainTable(0, v);
        Check(NearRelative(novice[2], std::pow(6.25, 1.0 / 3.0)) && NearRelative(novice[1], std::pow(6.25, 2.0 / 3.0)) &&
                  NearRelative(novice[0], 6.25),
              "memory: R=0 gains: Adept v^(1/3), Apprentice v^(2/3), Novice v^1",
              std::to_string(novice[0]) + " " + std::to_string(novice[1]) + " " + std::to_string(novice[2]));
        const auto master = MM::GainTable(4, v);
        bool masterOk = true;
        for (int t = 0; t <= 4; ++t) {
            const int slot = 4 + 2 - t;
            masterOk = masterOk && NearRelative(master[static_cast<std::size_t>(t)], std::pow(6.25, (slot + 4 + 1) / 3.0));
        }
        Check(masterOk, "memory: R=4 gains: rank t gets v^((6 - t + 5) / 3), Master v^(7/3) ... Novice v^(11/3)",
              std::to_string(master[4]) + " .. " + std::to_string(master[0]));

        // Known share: each archetype's global against SR's compose scale ((R + 1) x d)^2, averaged
        Check(Near(MM::KnownFraction(4.5f, 0, 3.0f), 0.5f) && Near(MM::KnownFraction(112.5f, 4, 3.0f), 0.5f),
              "memory: a global at half of KnownFull is known 0.5 (Novice 4.5 of 9, Master 112.5 of 225)");
        const float values[] = { 9.0f, 4.5f, 0.0f };
        const float share = MM::KnownShare(values, 3, 0, 3.0f);
        Check(Near(share, 0.5f) && Near(MM::PracticeMultiplier(25.0f, share), 1.125f),
              "memory: archetypes known 1, 0.5, 0 -> share 0.5, multiplier 1 + 0.25 x 0.5");
        Check(Near(MM::PracticeMultiplier(25.0f, 1.0f), 1.25f), "memory: every archetype fully known -> x1.25");
        // One payment of v = 6.25 into a Novice spell gives its rank 6.25 of 9: not yet all known
        Check(Near(MM::KnownFraction(MM::GainTable(0, v)[0], 0, 3.0f), 6.25f / 9.0f),
              "memory: one payment leaves a Novice spell's archetype 6.25 / 9 known (B does not saturate at once)");

        Check(Near(MM::ApplyGain(10.0f, 5.5f), 15.5f), "memory: a gain under the cap is added");

        // ---- Boundaries ----------------------------------------------------------------------
        Check(MM::BoundariesCrossed(0.15f, 0.15f) == 0, "memory: exactly 15% -> 15% crosses none");
        Check(MM::BoundariesCrossed(0.10f, 0.15f) == 1, "memory: reaching exactly 15% crosses it");
        Check(MM::BoundariesCrossed(0.15f, 0.20f) == 0, "memory: from exactly 15% upward does not cross 15 again");
        Check(MM::BoundariesCrossed(0.89f, 1.0f) == 2, "memory: 89% -> 100% crosses 90 and 100");
        Check(MM::BoundariesCrossed(0.95f, 1.0f) == 1, "memory: mastery from 95% pays the 100% boundary");
        Check(Near(MM::ApplyGain(9'999'990.0f, 100.0f), 10'000'000.0f), "memory: 9,999,990 + 100 clamps to 10,000,000");
        Check(Near(MM::ApplyGain(10'000'000.0f, 100.0f), 10'000'000.0f), "memory: a value at the cap is left alone");
        Check(Near(MM::ApplyGain(12'000'000.0f, 100.0f), 12'000'000.0f), "memory: a value over the cap is left alone (SR's rule)");
        Check(PaidRanks(novice) == 3 && novice[3] == 0.0f && novice[4] == 0.0f,
              "memory: R=0 pays Novice, Apprentice, Adept only (no slot below Novice)");
        Check(PaidRanks(master) == 5, "memory: R=4 pays all five ranks (no slot above Master)");
        const auto adept = MM::GainTable(2, v);
        Check(PaidRanks(adept) == 5 && NearRelative(adept[4], std::pow(6.25, 3.0 / 3.0)),
              "memory: R=2 pays all five ranks, Master by slot 0 (v^1)");
        Check(PaidRanks(MM::GainTable(1, v)) == 4 && MM::GainTable(1, v)[4] == 0.0f,
              "memory: R=1 pays Novice to Expert only");
        const auto apprentice = MM::GainTable(1, v);
        bool apprenticeOk = true;
        for (int t = 0; t <= 3; ++t) {
            const int slot = 1 + 2 - t;
            apprenticeOk = apprenticeOk && NearRelative(apprentice[static_cast<std::size_t>(t)], std::pow(6.25, (slot + 1 + 1) / 3.0));
        }
        Check(apprenticeOk, "memory: R=1 gains: Expert v^(2/3), Adept v^1, Apprentice v^(4/3), Novice v^(5/3)");
        const auto expert = MM::GainTable(3, v);
        bool expertOk = PaidRanks(expert) == 5;
        for (int t = 0; t <= 4; ++t) {
            const int slot = 3 + 2 - t;
            expertOk = expertOk && NearRelative(expert[static_cast<std::size_t>(t)], std::pow(6.25, (slot + 3 + 1) / 3.0));
        }
        Check(expertOk, "memory: R=3 gains: Master v^(5/3) ... Novice v^(9/3), all five ranks");
        Check(Near(MM::KnownFull(0, 3.0f), 9.0f) && Near(MM::KnownFull(4, 3.0f), 225.0f),
              "memory: KnownFull at d = 3 is 9 for Novice and 225 for Master");
        Check(Near(MM::KnownFraction(9.0f, 0, 3.0f), 1.0f) && Near(MM::KnownFraction(500.0f, 4, 3.0f), 1.0f) &&
                  Near(MM::KnownFraction(0.0f, 2, 3.0f), 0.0f),
              "memory: a global at or over KnownFull is known 1, at 0 known 0");

        // ---- Failure -------------------------------------------------------------------------
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Check(MM::BoundariesCrossed(0.50f, 0.50f) == 0 && MM::BoundariesCrossed(0.50f, 0.20f) == 0,
              "memory: progress that did not rise pays nothing");
        Check(MM::BoundariesCrossed(nan, 0.50f) == 0 && MM::BoundariesCrossed(0.10f, nan) == 0 &&
                  MM::BoundariesCrossed(-0.10f, 0.50f) == 0,
              "memory: NaN or negative progress pays nothing");
        Check(Near(MM::KnownShare(values, 0, 0, 3.0f), 0.0f) && Near(MM::KnownShare(nullptr, 3, 0, 3.0f), 0.0f) &&
                  Near(MM::PracticeMultiplier(25.0f, MM::KnownShare(values, 0, 0, 3.0f)), 1.0f),
              "memory: no archetypes -> share 0, multiplier 1");
        Check(Near(MM::KnownFull(0, 0.0f), 9.0f) && Near(MM::KnownFull(0, -2.0f), 9.0f) && Near(MM::KnownFull(0, nan), 9.0f) &&
                  Near(MM::KnownFull(4, std::numeric_limits<float>::infinity()), 225.0f),
              "memory: a difficulty <= 0, NaN or infinite falls back to 3.0");
        const float nanValues[] = { nan, 9.0f };
        Check(Near(MM::KnownFraction(nan, 0, 3.0f), 0.0f) && Near(MM::KnownFraction(-4.0f, 0, 3.0f), 0.0f) &&
                  Near(MM::KnownShare(nanValues, 2, 0, 3.0f), 0.5f),
              "memory: a NaN or negative global counts as 0 known");
        Check(Near(MM::KnownFull(5, 3.0f), 0.0f) && Near(MM::KnownFraction(9.0f, 5, 3.0f), 0.0f),
              "memory: a rank past Master has no scale and nothing known");
        Check(Near(MM::PracticeMultiplier(0.0f, 1.0f), 1.0f) && Near(MM::PracticeMultiplier(nan, 1.0f), 1.0f) &&
                  Near(MM::PracticeMultiplier(25.0f, nan), 1.0f),
              "memory: no boost, NaN boost or NaN share -> multiplier 1");
        const float none = MM::ResearchValue(15.0f, 0.0f);
        Check(Near(none, 0.0f) && PaidRanks(MM::GainTable(4, none)) == 0, "memory: memoryMinutes 0 -> no gain");
        Check(Near(MM::ResearchValue(nan, 25.0f), 0.0f) && Near(MM::ResearchValue(-15.0f, 25.0f), 0.0f),
              "memory: a NaN or negative time base -> no gain");
        Check(PaidRanks(MM::GainTable(5, v)) == 0, "memory: a rank past Master pays nothing");
        Check(Near(MM::ApplyGain(10.0f, nan), 10.0f) && Near(MM::ApplyGain(10.0f, -3.0f), 10.0f),
              "memory: a NaN or negative gain changes nothing");
        const float inf = std::numeric_limits<float>::infinity();
        Check(std::isnan(MM::ApplyGain(nan, 5.0f)) && MM::ApplyGain(inf, 5.0f) == inf,
              "memory: a NaN or infinite global is left alone (SR: GetValue() < max is false for both)");

        // ---- Settings ------------------------------------------------------------------------
        using json = nlohmann::json;
        const auto defaults = MM::ReadSettings(json::object());
        Check(defaults.practiceToMemory && defaults.memoryToPractice && Near(defaults.memoryMinutes, 25.0f) &&
                  Near(defaults.maxPracticeBoost, 25.0f),
              "memory settings: no section -> on, 25 minutes, 25%");
        const auto read = MM::ReadSettings(json::parse(R"({"spellResearch": {"enabled": false, "memoryExchange":
            {"practiceToMemory": false, "memoryMinutes": 300, "memoryToPractice": "yes", "maxPracticeBoost": -5}}})"));
        Check(!read.practiceToMemory && Near(read.memoryMinutes, 240.0f) && read.memoryToPractice &&
                  Near(read.maxPracticeBoost, 0.0f),
              "memory settings: off kept, minutes clamped to 240, a wrong type is the default, a negative boost is 0");
        const auto partial = MM::ReadSettings(json::parse(R"({"spellResearch": {"memoryExchange": {"maxPracticeBoost": 150}}})"));
        Check(partial.practiceToMemory && Near(partial.memoryMinutes, 25.0f) && Near(partial.maxPracticeBoost, 100.0f),
              "memory settings: missing keys are the defaults, the boost clamped to 100");
        Check(!defaults.cheatMode && !read.cheatMode, "memory settings: no cheatMode key -> cheat mode off");
        const auto cheat = MM::ReadSettings(json::parse(R"({"cheatMode": true})"));
        Check(cheat.cheatMode && cheat.practiceToMemory && cheat.memoryToPractice,
              "memory settings: top-level cheatMode true is read without a spellResearch section, switches kept");
        const auto cheatText = MM::ReadSettings(json::parse(R"({"cheatMode": "true", "spellResearch": {}})"));
        Check(!cheatText.cheatMode, "memory settings: a cheatMode that is not a boolean is off");
    }
}
