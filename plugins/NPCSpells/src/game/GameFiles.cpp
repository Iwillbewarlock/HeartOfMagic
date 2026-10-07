#include "game/GameFiles.h"

#include "core/NpcSpellsSlots.h"
#include "tomereach/TomeReachRecords.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>

namespace
{
    constexpr std::string_view kSpidSuffix = "_distr.ini";
    constexpr const char* kInputsMissing = "spell_tree.json or spell_catalog.json not found (run a Heart of Magic scan and tree build first)";

    std::optional<std::string> ReadText(const char* path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return std::nullopt;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    // ParseRules skips groups that are not built in (NpcSpellsRules.cpp: "logged by the caller")
    void LogUnknownGroups(const std::string& rulesText)
    {
        const auto j = nlohmann::json::parse(rulesText, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            return;
        }
        const auto groups = j.find("groups");
        if (groups == j.end() || !groups->is_array()) {
            return;
        }
        for (const auto& g : *groups) {
            if (!g.is_object()) {
                continue;
            }
            const auto name = g.find("name");
            if (name == g.end() || !name->is_string()) {
                continue;
            }
            const auto& text = name->get_ref<const std::string&>();
            const bool builtIn = std::ranges::any_of(NpcSpells::kGroups, [&](const NpcSpells::GroupDef& d) { return d.name == text; });
            if (!builtIn) {
                logger::warn("NPCSpells: rules: group '{}' is not a built-in group, ignored", text);
            }
        }
    }

    void ReadRules(NpcSpellsGame::Inputs& in)
    {
        const auto text = ReadText(NpcSpellsGame::kRulesPath);
        if (!text) {
            in.rulesFromDefaults = true;
            in.rulesError = std::string(NpcSpellsGame::kRulesPath) + " not found";
            in.rules = NpcSpells::DefaultRules();
            logger::warn("NPCSpells: rules: {}, using the built-in rules", in.rulesError);
            return;
        }
        if (!NpcSpells::ParseRules(*text, in.rules, in.rulesError)) {
            // ParseRules may leave its out-param half written: start again from the defaults
            in.rulesFromDefaults = true;
            in.rules = NpcSpells::DefaultRules();
            logger::error("NPCSpells: rules: {} - using the built-in rules", in.rulesError);
            return;
        }
        if (!in.rulesError.empty()) {
            // Parsed, but an entry that is not a FormID (XXXXXX:Plugin.ext) was dropped
            logger::warn("NPCSpells: rules: {} (entry ignored)", in.rulesError);
        }
        LogUnknownGroups(*text);
    }

    // SPID files only: FindDistributionConfigs also returns KID, FLM, SWAP, CID,
    // CDF, LLI, SkyPatcher and LLOS files, whose mentions do not give spells.
    // ConfigText keeps each file's whole text, so a spell named only in a
    // _DISTR.ini filter is excluded too (accepted over-match).
    void ReadDistr(NpcSpellsGame::Inputs& in)
    {
        for (const auto& f : TomeReach::FindDistributionConfigs(NpcSpellsGame::kDataDir)) {
            if (TomeReach::LowerAscii(f.filename().string()).ends_with(kSpidSuffix)) {
                in.distr.Add(f);
            }
        }
    }
}

NpcSpellsGame::Inputs NpcSpellsGame::ReadInputs()
{
    Inputs in;
    const auto treeText = ReadText(kTreePath);
    const auto catalogText = ReadText(kCatalogPath);
    if (!treeText || !catalogText) {
        in.stopReason = kInputsMissing;
        return in;
    }
    in.tree = nlohmann::json::parse(*treeText, nullptr, false);
    if (in.tree.is_discarded()) {
        in.stopReason = std::string(kTreePath) + " is not valid JSON";
        return in;
    }
    in.catalog = nlohmann::json::parse(*catalogText, nullptr, false);
    if (in.catalog.is_discarded()) {
        in.stopReason = std::string(kCatalogPath) + " is not valid JSON";
        return in;
    }
    ReadRules(in);
    ReadDistr(in);
    logger::info("NPCSpells: inputs read ({} SPID config files)", in.distr.FileCount());
    in.ok = true;
    return in;
}
