#include "Common.h"
#include "JsonText.h"
#include "TreePrereqEntries.h"
#include "uimanager/UIManager.h"
#include "uimanager/UIManagerInternal.h"
#include "uimanager/StartupState.h"
#include "ProgressionManager.h"
#include "SpellScanner.h"
#include "SpellTomeHook.h"

#include <fstream>
#include <sstream>

// =============================================================================
// TREE STATE - the saved tree's prerequisites and requiredXP in ProgressionManager
// =============================================================================
//
// The panel path (OnSetTreePrerequisites, OnLoadSpellTree) and game start
// (LoadTreeStateAtStartup) share the two appliers below, and the saved tree's file
// read and validation after them.

// The entries' rules are the panel path's, moved here unchanged so game start
// applies a tree exactly as the panel's message does
int ApplyTreePrerequisiteEntries(const json& request)
{
    const auto started = std::chrono::steady_clock::now();
    auto* pm = ProgressionManager::GetSingleton();
    if (!pm) {
        logger::error("UIManager: SetTreePrerequisites - ProgressionManager not available");
        return 0;
    }
    int count = 0;

    for (const auto& entry : request) {
        if (!entry.is_object()) {
            logger::warn("UIManager: SetTreePrerequisites - non-object entry in array, skipping");
            continue;
        }

        // SafeJsonValue: an entry with a field of the wrong type is that entry's
        // problem (value() threw and ended the loop for every spell after it)
        std::string formIdStr = SafeJsonValue<std::string>(entry, "formId", "");
        if (formIdStr.empty()) continue;

        // The whole text must be the id (FormIdText::Parse, not ParseSpellOf): an edit
        // mode copy ("0x0001ABCD_dup_17...") is not its original, whose requirements the
        // copy's empty lists would then erase. Its prerequisite lists below name spells.
        const auto parsedId = FormIdText::Parse(formIdStr);
        if (!parsedId) {
            logger::warn("UIManager: SetTreePrerequisites - '{}' is not a formId - skipping", formIdStr);
            continue;
        }
        const RE::FormID formId = *parsedId;

        if (formId == 0) {
            logger::warn("UIManager: SetTreePrerequisites - formId resolved to 0 for '{}', skipping", formIdStr);
            continue;
        }

        // Parse hard/soft prerequisites (new unified system)
        ProgressionManager::PrereqRequirements reqs;

        // One list of formId strings. A spell is never its own prerequisite:
        // a self id would make it wait for itself and never open, so it is skipped.
        auto parsePrereqs = [&](const char* key, std::vector<RE::FormID>& out) {
            if (!entry.contains(key) || !entry[key].is_array()) return;
            for (const auto& prereqStr : entry[key]) {
                if (!prereqStr.is_string()) continue;
                const auto prereqId = ParsePanelFormId(prereqStr.get<std::string>(), key);
                if (!prereqId) continue;
                if (*prereqId == formId) {
                    logger::warn("UIManager: spell {:08X} listed as its own prerequisite ({}) - skipped",
                        formId, key);
                    continue;
                }
                out.push_back(*prereqId);
            }
        };

        parsePrereqs("hardPrereqs", reqs.hardPrereqs);  // must have ALL
        parsePrereqs("softPrereqs", reqs.softPrereqs);  // need softNeeded of these

        // Never more than the soft list holds, or the spell could never open
        reqs.softNeeded = (std::min)(SafeJsonValue<int>(entry, "softNeeded", 0), static_cast<int>(reqs.softPrereqs.size()));

        // Legacy fallback: parse old "prereqs" field as all hard
        if (reqs.hardPrereqs.empty() && reqs.softPrereqs.empty()) {
            parsePrereqs("prereqs", reqs.hardPrereqs);
        }

        // No line per spell here. This runs on the game thread for every
        // spell in the tree on every tree load - which is every game load -
        // and it was looking the form up and fetching its name for the sake
        // of the log alone: 1423 lookups and 1423 lines in 22 ms, two thirds
        // of the whole log file. The count is logged once, below.
        if (!reqs.hardPrereqs.empty() || !reqs.softPrereqs.empty()) {
            logger::trace("UIManager: prereqs {:08X}: {} hard, {} soft (need {})",
                formId, reqs.hardPrereqs.size(), reqs.softPrereqs.size(), reqs.softNeeded);
        }

        pm->SetPrereqRequirements(formId, reqs);
        count++;
    }

    // The new tree's nodes decide which spell a copy's tome counts for
    // (TreeSpellForTome): answers cached before they arrived start over
    if (auto* tomeHook = SpellTomeHook::GetSingleton()) tomeHook->InvalidateTomeInventoryCache();
    logger::info("UIManager: Set tree prerequisites for {} spells in {} ms (game thread)", count, MsSince(started));
    return count;
}

// OnLoadSpellTree's requiredXP sync, moved here unchanged
void SyncRequiredXPFromTree(const json& treeData)
{
    auto* pm = ProgressionManager::GetSingleton();
    int xpSyncCount = 0;

    if (treeData.contains("schools")) {
        for (auto& [schoolName, schoolData] : treeData["schools"].items()) {
            if (schoolData.contains("nodes")) {
                for (auto& node : schoolData["nodes"]) {
                    // Not text: that node skipped (get<std::string>() threw and ended the sync for every node after it)
                    if (node.contains("formId") && node["formId"].is_string()) {
                        std::string formIdStr = node["formId"].get<std::string>();
                        // Sync requiredXP from tree to ProgressionManager
                        if (node.contains("requiredXP") && node["requiredXP"].is_number()) {
                            float reqXP = node["requiredXP"].get<float>();
                            if (reqXP > 0) {
                                if (const auto formId = ParsePanelFormId(formIdStr, "LoadSpellTree XP sync")) {
                                    pm->SetRequiredXP(*formId, reqXP, RequiredXP::Kind::kTree);
                                    xpSyncCount++;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (xpSyncCount > 0) {
        logger::info("UIManager: Synced requiredXP for {} spells from tree to ProgressionManager", xpSyncCount);
    }
}

// =============================================================================
// THE SAVED TREE'S FILE - read and validated the same way by OnLoadSpellTree and game start
// =============================================================================

TreeFileText ReadTreeFileText(const std::filesystem::path& path, std::uintmax_t maxBytes)
{
    TreeFileText result;
    if (maxBytes != kNoTreeFileCap) {
        std::error_code sizeError;
        const auto size = std::filesystem::file_size(path, sizeError);
        // Only a size known to be over the cap is refused; an unknown one is read as before
        if (!sizeError && size > maxBytes) {
            result.outcome = TreeFileRead::TooLarge;
            return result;
        }
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        result.outcome = TreeFileRead::CouldNotOpen;
        return result;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    result.text = buffer.str();
    result.outcome = TreeFileRead::Ok;
    return result;
}

bool IsBlankTreeText(std::string_view text)
{
    constexpr std::string_view kBlank = " \t\r\n";
    return text.find_first_not_of(kBlank) == std::string_view::npos;
}

bool ValidateTreeForPanel(json& tree)
{
    const auto validation = SpellScanner::ValidateAndFixTree(tree);
    const int byPersistentId = validation.resolvedFromPersistent - validation.rematchedByEditorId;
    if (byPersistentId > 0) {
        logger::info("UIManager: Resolved {} spells from persistent IDs (load order changed)", byPersistentId);
    }
    if (validation.rematchedByEditorId > 0) {
        logger::info("UIManager: Found {} spells again by editor id (their plugin was renamed, merged or compacted)",
            validation.rematchedByEditorId);
    }
    if (validation.invalidNodes > 0) {
        logger::warn("UIManager: {} spells could not be resolved (plugins may be missing)", validation.invalidNodes);
    }
    return validation.resolvedFromPersistent > 0 || validation.invalidNodes > 0;
}

// =============================================================================
// GAME START
// =============================================================================

namespace
{
    // A saved tree of ~1,400 spells is under 1 MB. One far past that is not read in the SKSE
    // message handler, where its text, the parsed tree and the panel's copy would all be held
    constexpr std::uintmax_t kBytesPerMiB = 1024 * 1024;
    constexpr std::uintmax_t kMaxStartupTreeMiB = 64;
    constexpr std::uintmax_t kMaxStartupTreeBytes = kMaxStartupTreeMiB * kBytesPerMiB;

    // What the panel gets for the saved tree (OnLoadSpellTree): the file's text, or the
    // validated tree's dump when validation moved or dropped a spell
    struct PanelTree
    {
        json validated;          // after ValidateAndFixTree, for the requiredXP sync
        std::string panelText;   // the text the panel parses
        bool fromValidated = false;
    };

    void SkipStartupTree(std::string_view reason)
    {
        logger::info("Startup tree: {} - the tree's prerequisites wait for the panel", reason);
        TreeParity::Skip(reason);
    }

    // spell_tree.json read as OnLoadSpellTree reads it, or nullopt (skipped, logged)
    std::optional<std::string> ReadSavedTreeText()
    {
        const auto treePath = UIManager::GetTreeFilePath();
        std::error_code existsError;
        if (!std::filesystem::exists(treePath, existsError)) {
            SkipStartupTree(existsError ? "spell_tree.json could not be checked" : "no saved spell_tree.json");
            return std::nullopt;
        }
        auto file = ReadTreeFileText(treePath, kMaxStartupTreeBytes);
        switch (file.outcome) {
            case TreeFileRead::CouldNotOpen:
                SkipStartupTree("spell_tree.json could not be opened");
                return std::nullopt;
            case TreeFileRead::TooLarge:
                SkipStartupTree(std::format("spell_tree.json is over {} MB, not read at game start", kMaxStartupTreeMiB));
                return std::nullopt;
            case TreeFileRead::Ok:
                break;
        }
        if (IsBlankTreeText(file.text)) {
            SkipStartupTree("spell_tree.json is empty");
            return std::nullopt;
        }
        return std::move(file.text);
    }

    constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

    // Parse and validate as OnLoadSpellTree does (the file is never written). Throws on a
    // tree that does not parse or that validation cannot read, as the panel path's try does
    PanelTree ValidateAsPanelPath(std::string text)
    {
        PanelTree tree;
        tree.validated = json::parse(text);
        // The validator's lines below appear again when the panel's load validates the same file
        logger::info("Startup tree load: validating spell_tree.json (the panel validates it again when it loads)");
        tree.fromValidated = ValidateTreeForPanel(tree.validated);
        tree.panelText = tree.fromValidated ? JsonText::Dump(tree.validated) : std::move(text);
        return tree;
    }
}

void LoadTreeStateAtStartup(const StartupConfigResult& config)
{
    // The SKSE message handler must not throw: all of it under one try. `stage` names the
    // step for the log and the SKIP line (a throw after the baseline writes no SKIP)
    std::string_view stage = "spell_tree.json could not be read";
    try {
        const auto started = std::chrono::steady_clock::now();
        // The gate in ValidateAndFixTree reads config.json; the panel reads the tree only
        // after its own config load has replaced a broken file and applied the settings whole
        if (config.configBroken) {
            SkipStartupTree("config.json is broken (the panel replaces it before it reads the tree)");
            return;
        }
        if (config.applyThrew) {
            SkipStartupTree("applying config.json threw (the panel's load applies it before it reads the tree)");
            return;
        }

        auto text = ReadSavedTreeText();
        if (!text) return;

        const PanelTree tree = ValidateAsPanelPath(std::move(*text));
        // The panel gets the raw text when validation changed nothing, and its JSON.parse rejects
        // a UTF-8 BOM (nlohmann skips it): that panel sends nothing, so game start does not either
        if (!tree.fromValidated && tree.panelText.starts_with(kUtf8Bom)) {
            SkipStartupTree("spell_tree.json starts with a UTF-8 BOM, which the panel's JSON.parse rejects");
            return;
        }
        // Parsed in the panel's key order: the school order decides which school's node
        // of a spell in two schools is applied last
        const auto built = TreePrereqEntries::BuildPrerequisiteEntries(nlohmann::ordered_json::parse(tree.panelText));
        if (built.outcome != TreePrereqEntries::Outcome::Built) {
            SkipStartupTree(std::format("{}: {}", TreePrereqEntries::OutcomeName(built.outcome), built.reason));
            return;
        }

        stage = "applying the prerequisites threw";
        ProgressionManager::GetSingleton()->ClearAllTreePrerequisites();
        const int applied = ApplyTreePrerequisiteEntries(json(built.entries));
        logger::info("Startup tree: {} of {} entries applied from {} in {} ms", applied, built.entries.size(),
            tree.fromValidated ? "the validated tree" : "the file's text", MsSince(started));
        TreeParity::RecordBaseline();
        if (!UIManager::GetSingleton()->IsInitialized()) {
            TreeParity::Expire("no panel this session - startup state applied");
        }

        stage = "the requiredXP sync threw";
        SyncRequiredXPFromTree(tree.validated);
    } catch (const std::exception& e) {
        logger::error("Startup tree: {}: {}", stage, e.what());
        TreeParity::Skip(stage);
    }
}
