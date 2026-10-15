#pragma once
#include "core/NpcSpellsRules.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>
// The dev-only in-game self test's pure parts (game/GameSelfTest.cpp does the spawning): its config file and the
// witness spells of one attached list. Never in the release: the test runs only when the config file is present.
namespace NpcSpells {
    inline constexpr int kSelfTestDefaultStartDelayMs = 2000;   // after the player's 3D is in, before the first spawn
    inline constexpr int kSelfTestDefaultMaxWaitMs = 8000;      // per spawned actor: its 3D and high process must come in this time
    inline constexpr int kSelfTestMaxMs = 60000;                // either value above this (or below 0) goes back to its default
    struct SelfTestConfig {
        std::vector<FormRef> samples;            // base NPC records, "XXXXXX:Plugin.ext" like the rules file
        std::vector<std::string> sampleTexts;    // as written, parallel to samples (for the log)
        int startDelayMs = kSelfTestDefaultStartDelayMs;
        int maxWaitMs = kSelfTestDefaultMaxWaitMs;
    };
    // {"samples": ["039D37:Skyrim.esm", ...], "startDelayMs": 2000, "maxWaitMs": 8000}. false + error when the text is not a JSON
    // object or no sample is usable; true otherwise, error naming each dropped sample or reset value ("; " between).
    bool ParseSelfTestConfig(const std::string& jsonText, SelfTestConfig& out, std::string& error);
    // A list's spells that prove the list worked: each once, in list order, without those the NPC can have another way
    // (excluded: its static spells, its race's, its other leveled lists' entries, the actor's added spells)
    std::vector<std::uint32_t> WitnessSpells(const std::vector<std::uint32_t>& listSpells, const std::unordered_set<std::uint32_t>& excluded);
    // How many witnesses another of our lists on the same NPC also holds (a hit on one of them may have come from that list)
    std::size_t CountShared(const std::vector<std::uint32_t>& witnesses, const std::unordered_set<std::uint32_t>& otherLists);
}
