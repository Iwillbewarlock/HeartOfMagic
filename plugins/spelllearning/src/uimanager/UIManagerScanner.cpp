#include "Common.h"
#include "JsonText.h"
#include "EncodingUtils.h"
#include "uimanager/UIManager.h"
#include "SpellScanner.h"
#include "ThreadUtils.h"
#include "librarian/Librarian.h"
#include "librarian/PerkAdapterPatch.h"
#include "librarian/SpellResearchPatch.h"
#include "tomereach/TomeReach.h"

#include <thread>

using json = nlohmann::json;

// =============================================================================
// SCANNER TAB CALLBACKS
// =============================================================================

namespace
{
    // How long a full scan waits for the tome reach pass (a few seconds on a large load order)
    constexpr std::chrono::milliseconds kTomeReachWait{ 30000 };

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


// =============================================================================
// THE SCAN, OFF THE GAME THREAD WHERE IT CAN BE (docs/ARCHITECTURE.md, "Where a scan runs")
//
//   1. CollectScan    - game thread task: every form read (SpellScanner::Collect*)
//   2. PrepareScanText - worker thread: the 10-20 MB dump, the librarian, the debug
//                        dump file. Strings and files only, no game form is read
//   3. FinishScan     - game thread task: perk adapters and Spell Research (they
//                        write forms), then the panel gets the spells
//
// One scan runs at a time. A request while one runs waits for it, one per mode
// (the latest wins); EndScan starts it, the full scan first.
// =============================================================================

void UIManager::OnScanSpells(const char* argument)
{
    logger::info("UIManager: ScanSpells callback triggered");
    if (auto* instance = GetSingleton()) {
        instance->RequestScan(std::string(argument ? argument : ""));
    }
}

void UIManager::RequestScan(std::string argStr)
{
    {
        std::lock_guard lock(m_scanMutex);
        if (m_scanRunning) {
            const bool tomeScan = IsTomeScan(argStr);
            logger::info("UIManager: a scan is running - the {} scan starts after it", tomeScan ? "tome" : "full");
            (tomeScan ? m_queuedTomeScan : m_queuedFullScan) = std::move(argStr);
            return;
        }
        m_scanRunning = true;
    }
    StartScan(std::move(argStr));
}

void UIManager::StartScan(std::string argStr)
{
    // The tome reach pass runs for a few seconds after the game starts; a full scan
    // asked for in that time waits for it on its own thread (the game goes on), so
    // the spells whose tomes nothing hands out are marked (TomeReach.h). A tome scan
    // does not use the pass and goes at once, as before.
    const auto collect = [](const std::string& arg) {
        AddTaskToGameThread("ScanSpells", [arg]() {
            if (auto* instance = GetSingleton()) instance->CollectScan(arg);
        });
    };
    if (IsTomeScan(argStr)) {
        collect(argStr);
        return;
    }
    try {
        std::thread([argStr, collect]() {
            if (!TomeReach::WaitUntilDone(kTomeReachWait)) {
                logger::info("UIManager: the scan did not wait for the tome reach pass (not started or still running)");
            }
            collect(argStr);
        }).detach();
    } catch (const std::system_error& e) {
        logger::warn("UIManager: no thread to wait for the tome reach pass ({}) - scanning now", e.what());
        collect(argStr);
    }
}

void UIManager::CollectScan(const std::string& argStr)
{
    const bool tomeScan = IsTomeScan(argStr);

    // A scan that throws must still end for the player: the panel disables the Scan
    // button until the spells come back, so without this report it stayed on
    // "Scanning..." for good and the only trace was one line in the log.
    try {
        SpellScanner::ScanConfig scanConfig;
        if (!argStr.empty()) {
            try {
                scanConfig = SpellScanner::ParseScanConfig(argStr.c_str());
            } catch (...) {
                // If parsing fails, use defaults
            }
        }

        UpdateStatus(tomeScan ? "Scanning spell tomes..." : "Scanning all spells...");
        auto output = std::make_shared<json>(
            tomeScan ? SpellScanner::CollectSpellTomes(scanConfig) : SpellScanner::CollectAllSpells(scanConfig));

        // A shared_ptr: std::function (the thread and the task) needs a copyable callable
        const auto finish = [tomeScan](std::shared_ptr<std::string> text) {
            AddTaskToGameThread("ScanSpellsDone", [text, tomeScan]() {
                if (auto* instance = GetSingleton()) instance->FinishScan(std::move(*text), tomeScan);
            });
        };
        const auto fail = [tomeScan](std::string what) {
            AddTaskToGameThread("ScanSpellsFailed", [what = std::move(what), tomeScan]() {
                auto* instance = GetSingleton();
                if (!instance) return;
                instance->ReportScanFailure(what.c_str(), tomeScan);
                instance->EndScan();
            });
        };
        try {
            std::thread([output, tomeScan, finish, fail]() {
                try {
                    finish(std::make_shared<std::string>(PrepareScanText(std::move(*output), tomeScan)));
                } catch (const std::exception& e) {
                    fail(e.what());
                } catch (...) {
                    fail("");
                }
            }).detach();
        } catch (const std::system_error& e) {
            // No worker: the text work runs here, on the game thread, as it always did before
            logger::warn("UIManager: no thread for the scan's text work ({}) - doing it now", e.what());
            FinishScan(PrepareScanText(std::move(*output), tomeScan), tomeScan);
        }
    } catch (const std::exception& e) {
        ReportScanFailure(e.what(), tomeScan);
        EndScan();
    } catch (...) {
        ReportScanFailure("", tomeScan);
        EndScan();
    }
}

std::string UIManager::PrepareScanText(json&& output, bool tomeScan)
{
    // Worker thread: what is in here takes strings and files, never a game form.
    // Librarian::ClassifyScan is the code the offline harness links without the
    // game (tools/CMakeLists.txt); it serializes itself against a Papyrus RunScan.
    std::string result = JsonText::Dump(output, 2);
    output = json();  // the parsed copy is 50-100 MB on a large load order

    // Classify what was just scanned and hand the catalog's elements on to
    // the traits the tree builder groups by and the chips the card shows.
    // Same call the Papyrus path makes, so the Scan button and the Papyrus
    // RunScan leave the same catalog behind. It logs its own failures and leaves
    // the result as it was rather than interrupting the scan.
    Librarian::ClassifyScan(result);

    // Debug installs keep the full scan for offline analysis, as Papyrus RunScan does
    if (!tomeScan && SpellScanner::DebugScanDumpEnabled()) SpellScanner::WriteScanOutput(result);
    return result;
}

void UIManager::FinishScan(std::string result, bool tomeScan)
{
    try {
        // The perk adapters read the catalog the full scan just rebuilt, and with
        // Spell Research they write forms - so here, on the game thread. Not after
        // the tome scan the panel runs in the background: that leaves the catalog
        // as it was, and the work would land on the frames the panel opens in.
        if (!tomeScan) {
            Librarian::PerkAdapters::Apply("scan");
            Librarian::SpellResearchPatch::OnCatalogChanged();
        }

        if (tomeScan) {
            // A tome scan is a filter list, not the spells a tree is built from,
            // so it is sent and not kept.
            SendSpellData(result);
        } else {
            StoreAndSendFullScan(std::move(result));
        }
    } catch (const std::exception& e) {
        ReportScanFailure(e.what(), tomeScan);
    } catch (...) {
        ReportScanFailure("", tomeScan);
    }
    EndScan();
}

void UIManager::StoreAndSendFullScan(std::string result)
{
    // Keep the full scan for tree builds (see m_scanText). Everything that can
    // throw happens before the panel has the data (the allocation here), and
    // what follows the send cannot report a failure: a throw that reached
    // FinishScan's catch then would put "Scan failed" over a good scan.
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

void UIManager::EndScan()
{
    std::optional<std::string> next;
    {
        std::lock_guard lock(m_scanMutex);
        // The full scan first: the player is waiting on it, the tome list is a filter
        if (m_queuedFullScan) {
            next = std::move(m_queuedFullScan);
            m_queuedFullScan.reset();
        } else if (m_queuedTomeScan) {
            next = std::move(m_queuedTomeScan);
            m_queuedTomeScan.reset();
        } else {
            m_scanRunning = false;  // with a next scan it stays set
        }
    }
    if (next) StartScan(std::move(*next));
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
