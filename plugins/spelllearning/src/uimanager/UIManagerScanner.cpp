#include "Common.h"
#include "JsonText.h"
#include "EncodingUtils.h"
#include "uimanager/UIManager.h"
#include "SpellScanner.h"
#include "ThreadUtils.h"
#include "librarian/Librarian.h"

using json = nlohmann::json;

// =============================================================================
// SCANNER TAB CALLBACKS
// =============================================================================

namespace
{
    // The scan mode the panel asked for ({"scanMode":"tomes"}); anything else, or text
    // that is not JSON, is the full scan.
    bool IsTomeScan(const std::string& argStr)
    {
        if (argStr.empty()) return false;
        try {
            const json j = json::parse(argStr);
            return j.contains("scanMode") && j["scanMode"].is_string() &&
                   j["scanMode"].get<std::string>() == "tomes";
        } catch (...) {
            return false;
        }
    }
}

void UIManager::OnScanSpells(const char* argument)
{
    logger::info("UIManager: ScanSpells callback triggered");

    std::string argStr(argument ? argument : "");

    AddTaskToGameThread("ScanSpells", [argStr]() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        // A scan that throws must still end for the player: the panel disables the Scan
        // button until the spells come back, so without this report it stayed on
        // "Scanning..." for good and the only trace was one line in the log.
        try {
            instance->RunScan(argStr);
        } catch (const std::exception& e) {
            instance->ReportScanFailure(e.what(), IsTomeScan(argStr));
        } catch (...) {
            instance->ReportScanFailure("", IsTomeScan(argStr));
        }
    });
}

void UIManager::ReportScanFailure(const char* what, bool tomeScan)
{
    // what() comes from the system (in its own code page), so make it valid UTF-8 before it is JSON
    const std::string reason = EncodingUtils::SanitizeToUTF8(what ? what : "");
    logger::error("UIManager: the {} scan failed: {}", tomeScan ? "tome" : "full",
        reason.empty() ? std::string("unknown error") : reason);
    // The mode tells the panel whether this is the scan the player is waiting on ("all") or a
    // background tome scan that must not touch the status bar ("tomes").
    // An empty reason (a throw that is not a std::exception) is "unknown error" in the panel's language
    const json message = {{"mode", tomeScan ? "tomes" : "all"}, {"reason", reason}};
    CallView("onScanFailed",
        JsonText::Dump(message).c_str());
}

void UIManager::RunScan(const std::string& argStr)
{
    // Parse the scan configuration
    SpellScanner::ScanConfig scanConfig;
    const bool useTomeMode = IsTomeScan(argStr);

    if (!argStr.empty()) {
        try {
            scanConfig = SpellScanner::ParseScanConfig(argStr.c_str());
        } catch (...) {
            // If parsing fails, use defaults
        }
    }

    std::string result;
    if (useTomeMode) {
        UpdateStatus("Scanning spell tomes...");
        result = SpellScanner::ScanSpellTomes(scanConfig);
    } else {
        UpdateStatus("Scanning all spells...");
        result = SpellScanner::ScanAllSpells(scanConfig);
    }

    // Classify what was just scanned and hand the catalog's elements on to
    // the traits the tree builder groups by and the chips the card shows.
    // Same call the Papyrus path makes, so the Scan button and the Papyrus
    // RunScan leave the same catalog behind. It logs its own failures and leaves
    // the result as it was rather than interrupting the scan.
    Librarian::ClassifyScan(result);

    // A tome scan is a filter list, not the spells a tree is built from, so
    // it is sent and not kept.
    if (useTomeMode) {
        SendSpellData(result);
        return;
    }

    // Keep the full scan for tree builds (see m_scanText). Everything that can
    // throw happens before the panel has the data (the allocation here), and
    // what follows the send cannot report a failure: a throw that reached
    // OnScanSpells' catch then would put "Scan failed" over a good scan.
    auto stored = std::make_shared<const std::string>(std::move(result));
    const std::string storedId = std::to_string(m_scanId + 1);
    SendSpellData(*stored);
    m_scanText = std::move(stored);
    ++m_scanId;
    try {
        CallView("onScanStored", storedId.c_str());
    } catch (const std::exception& e) {
        // The panel then sends the spells with its next build instead of the scan id
        logger::error("UIManager: could not tell the panel the scan id: {}", e.what());
    } catch (...) {
        logger::error("UIManager: could not tell the panel the scan id");
    }
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
