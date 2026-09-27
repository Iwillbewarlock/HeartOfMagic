#include "Common.h"
#include "uimanager/UIManager.h"
#include "SpellScanner.h"
#include "ThreadUtils.h"
#include "librarian/Librarian.h"

using json = nlohmann::json;

// =============================================================================
// SCANNER TAB CALLBACKS
// =============================================================================

void UIManager::OnScanSpells(const char* argument)
{
    logger::info("UIManager: ScanSpells callback triggered");

    std::string argStr(argument ? argument : "");

    AddTaskToGameThread("ScanSpells", [argStr]() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        // Parse the scan configuration
        SpellScanner::ScanConfig scanConfig;
        bool useTomeMode = false;

        if (!argStr.empty()) {
            try {
                json j = json::parse(argStr);
                scanConfig = SpellScanner::ParseScanConfig(argStr.c_str());

                // Check for scan mode
                if (j.contains("scanMode") && j["scanMode"].get<std::string>() == "tomes") {
                    useTomeMode = true;
                }
            } catch (...) {
                // If parsing fails, use defaults
            }
        }

        std::string result;
        if (useTomeMode) {
            instance->UpdateStatus("Scanning spell tomes...");
            result = SpellScanner::ScanSpellTomes(scanConfig);
        } else {
            instance->UpdateStatus("Scanning all spells...");
            result = SpellScanner::ScanAllSpells(scanConfig);
        }

        // Classify what was just scanned and hand the catalog's elements on to
        // the traits the tree builder groups by and the chips the card shows.
        // Same call the Papyrus path makes, so the Scan button and RunScan
        // leave the same catalog behind. It logs its own failures and leaves
        // the result as it was rather than interrupting the scan.
        Librarian::ClassifyScan(result);

        // Send result back to UI
        instance->SendSpellData(result);

        // Keep the full scan for tree builds (see m_scanText). A tome scan is
        // a filter list, not the spells a tree is built from, so it does not
        // replace it.
        if (!useTomeMode) {
            instance->m_scanText = std::make_shared<const std::string>(std::move(result));
            ++instance->m_scanId;
            instance->CallView("onScanStored", std::to_string(instance->m_scanId).c_str());
        }
    });
}

void UIManager::OnSaveOutput(const char* argument)
{
    logger::info("UIManager: SaveOutput callback triggered");

    if (!argument || strlen(argument) == 0) {
        logger::warn("UIManager: SaveOutput - no content to save");
        return;
    }

    std::string argStr(argument);

    AddTaskToGameThread("SaveOutput", [argStr]() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        if (SpellScanner::WriteScanOutput(argStr).empty()) {
            instance->UpdateStatus("Failed to save file");
        } else {
            instance->UpdateStatus("Saved to spell_scan_output.json");
        }
    });
}

void UIManager::OnLoadPrompt([[maybe_unused]] const char* argument)
{
    logger::info("UIManager: LoadPrompt callback triggered");

    AddTaskToGameThread("LoadPrompt", []() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        auto promptPath = GetPromptFilePath();

        // Check if saved prompt exists
        if (!std::filesystem::exists(promptPath)) {
            logger::info("UIManager: No saved prompt file found, using default");
            return;
        }

        try {
            std::ifstream file(promptPath);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                file.close();

                std::string promptContent = buffer.str();
                logger::info("UIManager: Loaded prompt from file ({} bytes)", promptContent.size());

                instance->SendPrompt(promptContent);
            } else {
                logger::warn("UIManager: Could not open prompt file");
            }
        } catch (const std::exception& e) {
            logger::error("UIManager: Exception while loading prompt: {}", e.what());
        }
    });
}
