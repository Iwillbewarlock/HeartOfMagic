#include "game/GameFiles.h"

#include "core/NpcSpellsSlots.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
    constexpr std::string_view kSpidSuffix = "_distr.ini";
    constexpr const char* kInputsMissing = "spell_tree.json or spell_catalog.json not found (run a Heart of Magic scan and tree build first)";
    constexpr std::size_t kBytesPerKb = 1024;

    // The whole file in one read (its size first), not through a stream buffer
    std::optional<std::string> ReadText(const char* path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            return std::nullopt;
        }
        const auto size = file.tellg();
        if (size < 0) {
            return std::nullopt;
        }
        std::string text(static_cast<std::size_t>(size), '\0');
        file.seekg(0);
        if (!file.read(text.data(), size)) {
            return std::nullopt;
        }
        return text;
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

    using Clock = std::chrono::steady_clock;

    double MsSince(Clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    // FindDistributionConfigs' walk (the data folder's top level, then SKSE/Plugins recursively), keeping the same
    // files it keeps that end in _distr.ini, but the file name is tested first: IsDistributionConfig (which lowers the
    // whole parent path) runs only on the few SPID names, not on every file under SKSE/Plugins
    void AddIfSpid(const std::filesystem::directory_entry& entry, std::vector<std::filesystem::path>& out)
    {
        try {
            if (!TomeReach::LowerUtf8(entry.path().filename()).ends_with(kSpidSuffix)) {
                return;
            }
            std::error_code ec;
            if (entry.is_regular_file(ec) && !ec && TomeReach::IsDistributionConfig(entry.path())) {
                out.push_back(entry.path());
            }
        } catch (...) {
            // a name the path code cannot handle: skipped alone, as FindDistributionConfigs does
        }
    }

    struct SpidFiles
    {
        std::vector<std::filesystem::path> files;
        std::size_t atTop = 0;  // in the data folder itself (where SPID reads them); the rest are under SKSE/Plugins
    };

    SpidFiles FindSpidConfigs(const std::filesystem::path& dataDir)
    {
        namespace fs = std::filesystem;
        SpidFiles out;
        std::error_code ec;
        for (fs::directory_iterator it(dataDir, ec), end; !ec && it != end; it.increment(ec)) {
            AddIfSpid(*it, out.files);
        }
        out.atTop = out.files.size();
        const auto plugins = dataDir / "SKSE" / "Plugins";
        ec.clear();
        for (fs::recursive_directory_iterator it(plugins, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            AddIfSpid(*it, out.files);
        }
        return out;
    }

    // SPID files only: FindDistributionConfigs also returns KID, FLM, SWAP, CID,
    // CDF, LLI, SkyPatcher and LLOS files, whose mentions do not give spells.
    // ConfigText keeps each file's whole text, so a spell named only in a
    // _DISTR.ini filter is excluded too (accepted over-match).
    void ReadDistr(NpcSpellsGame::Inputs& in, std::string& timings)
    {
        auto start = Clock::now();
        const auto found = FindSpidConfigs(NpcSpellsGame::kDataDir);
        timings += std::format(", SPID config search {:.0f} ms ({} in Data, {} under SKSE/Plugins)", MsSince(start),
            found.atTop, found.files.size() - found.atTop);
        start = Clock::now();
        for (const auto& f : found.files) {
            in.distr.Add(f);
        }
        timings += std::format(", SPID config reading {:.0f} ms ({} KB)", MsSince(start), in.distr.Text().size() / kBytesPerKb);
    }
}

NpcSpellsGame::Inputs NpcSpellsGame::ReadInputs()
{
    Inputs in;
    std::string timings;
    // One timed step: read a JSON file, then parse it; false (stopReason set) when it is missing or not JSON
    const auto readJson = [&](const char* path, const char* label, nlohmann::json& out) {
        auto start = Clock::now();
        const auto text = ReadText(path);
        timings += std::format("{}{} read {:.0f} ms", timings.empty() ? "" : ", ", label, MsSince(start));
        if (!text) {
            in.stopReason = kInputsMissing;
            return false;
        }
        start = Clock::now();
        out = nlohmann::json::parse(*text, nullptr, false);
        timings += std::format(", parse {:.0f} ms ({} KB)", MsSince(start), text->size() / kBytesPerKb);
        if (out.is_discarded()) {
            in.stopReason = std::string(path) + " is not valid JSON";
            return false;
        }
        return true;
    };
    const bool jsonOk = readJson(kTreePath, "tree", in.tree) && readJson(kCatalogPath, "catalog", in.catalog);
    if (jsonOk) {
        auto start = Clock::now();
        ReadRules(in);
        timings += std::format(", rules {:.0f} ms", MsSince(start));
        ReadDistr(in, timings);
    }
    logger::info("NPCSpells: input steps: {}", timings);
    if (!jsonOk) {
        return in;
    }
    logger::info("NPCSpells: inputs read ({} SPID config files)", in.distr.FileCount());
    in.ok = true;
    return in;
}
