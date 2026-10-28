#include "Common.h"
#include "JsonText.h"
#include "EncodingUtils.h"
#include "LoadOrderFingerprint.h"
#include "ScanRecord.h"
#include "ScanRecordRules.h"
#include "uimanager/UIManager.h"
#include "uimanager/UIManagerInternal.h"
#include "SpellScanner.h"
#include "SpellTomes.h"
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
// One scan runs at a time, from RequestScan until its ScanTicket is let go. A
// request meanwhile waits, one per mode (the latest wins) - a tome scan too, also
// while a full scan is still waiting for the tome reach pass; EndScan starts it,
// the full scan first.
// =============================================================================

// Held by every step of one scan: the tasks and threads it hands itself on to
// take it along by move. The last one let go ends the scan however it ended -
// sent, failed in any step, or a task SKSE dropped - so m_scanRunning cannot
// stay set. Normally that is FinishScan's or the failure report's game thread
// task; EndScan does not call into the panel, so any thread is safe.
struct UIManager::ScanTicket
{
    ScanTicket() = default;
    ScanTicket(const ScanTicket&) = delete;
    ScanTicket& operator=(const ScanTicket&) = delete;
    ~ScanTicket()
    {
        if (auto* instance = GetSingleton()) instance->EndScan();
    }
};

void UIManager::OnScanSpells(const char* argument)
{
    logger::info("UIManager: ScanSpells callback triggered");
    // Deferred: a scan that cannot start reports it to the panel (ReportScanFailure's
    // CallView), and a call back into JS from inside a listener does not work in
    // Ultralight (re-entrant, see OnProceduralTreeGenerate)
    AddTaskToGameThread("ScanSpells", [argStr = std::string(argument ? argument : "")]() mutable {
        if (auto* instance = GetSingleton()) {
            instance->RequestScan(std::move(argStr));
        }
    });
}

void UIManager::RequestScan(std::string argStr)
{
    const bool tomeScan = IsTomeScan(argStr);
    {
        std::lock_guard lock(m_scanMutex);
        if (m_scanRunning) {
            logger::info("UIManager: a scan is running - the {} scan starts after it", tomeScan ? "tome" : "full");
            (tomeScan ? m_queuedTomeScan : m_queuedFullScan) = std::move(argStr);
            return;
        }
        m_scanRunning = true;
    }
    // The panel is waiting on this one; a queued scan that cannot start is only logged
    if (!LaunchScan(std::move(argStr))) {
        ReportScanFailure("", tomeScan);
    }
}

bool UIManager::LaunchScan(std::string argStr) noexcept
{
    const bool tomeScan = IsTomeScan(argStr);
    std::shared_ptr<ScanTicket> ticket;
    try {
        ticket = std::make_shared<ScanTicket>();
    } catch (...) {
        logger::error("UIManager: the {} scan could not start (no memory)", tomeScan ? "tome" : "full");
        std::lock_guard lock(m_scanMutex);
        m_scanRunning = false;
        return false;
    }
    try {
        StartScan(std::move(argStr), std::move(ticket));
        return true;
    } catch (const std::exception& e) {
        // The ticket went with the throw, which ended this scan (EndScan)
        logger::error("UIManager: the {} scan could not start: {}", tomeScan ? "tome" : "full", e.what());
    } catch (...) {
        logger::error("UIManager: the {} scan could not start", tomeScan ? "tome" : "full");
    }
    return false;
}

void UIManager::StartScan(std::string argStr, std::shared_ptr<ScanTicket> ticket)
{
    // The tome reach pass runs for a few seconds after the game starts; a full scan
    // asked for in that time waits for it on its own thread (the game goes on), so
    // the spells whose tomes nothing hands out are marked (TomeReach.h). A tome scan
    // does not use the pass and starts at once (unless one is running, see above).
    const auto collect = [](const std::string& arg, std::shared_ptr<ScanTicket> held) {
        AddTaskToGameThread("ScanSpells", [arg, held = std::move(held)]() mutable {
            if (auto* instance = GetSingleton()) instance->CollectScan(arg, std::move(held));
        });
    };
    if (IsTomeScan(argStr)) {
        collect(argStr, std::move(ticket));
        return;
    }
    try {
        std::thread([argStr, collect, ticket]() mutable {
            // Nothing may leave a thread function: it would end the game (std::terminate)
            try {
                // The tome pairs first: a new generation restarts the pass this scan then waits for,
                // instead of restarting it after the wait and leaving the scan without its marks
                constexpr auto kRebuildWait = std::chrono::seconds(10);
                if (!RunOnGameThreadAndWait("SpellTomesBeforeScan", [] {
                        SpellTomes::Rebuild(SpellTomes::kReasonScan);
                        return true;
                    }, kRebuildWait)) {
                    logger::warn("UIManager: the tome pairs were not read again before the scan (game thread did not "
                                 "answer) - a new generation may restart the tome reach pass during it");
                }
                if (!TomeReach::WaitUntilDone(kTomeReachWait)) {
                    logger::info("UIManager: the scan did not wait for the tome reach pass (not started or still running)");
                }
                collect(argStr, std::move(ticket));
            } catch (...) {
                logger::error("UIManager: the full scan could not be handed to the game thread");
            }
        }).detach();
    } catch (const std::system_error& e) {
        logger::warn("UIManager: no thread to wait for the tome reach pass ({}) - scanning now", e.what());
        collect(argStr, std::move(ticket));
    }
}

void UIManager::CollectScan(const std::string& argStr, std::shared_ptr<ScanTicket> ticket)
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
        // Worked out here, on the game thread, for the worker's scan record (ScanRecord.h)
        LoadOrderFingerprint::Current();
        const auto collectStarted = std::chrono::steady_clock::now();
        auto output = std::make_shared<json>(
            tomeScan ? SpellScanner::CollectSpellTomes(scanConfig) : SpellScanner::CollectAllSpells(scanConfig));
        logger::info("UIManager: {} scan collected in {:.0f} ms (game thread)", tomeScan ? "tome" : "full", MsSince(collectStarted));

        // shared_ptrs: std::function (the thread and the task) needs a copyable callable
        const auto finish = [tomeScan](std::shared_ptr<std::string> text, std::shared_ptr<ScanTicket> held) {
            AddTaskToGameThread("ScanSpellsDone", [text, tomeScan, held = std::move(held)]() {
                if (auto* instance = GetSingleton()) instance->FinishScan(std::move(*text), tomeScan);
            });
        };
        const auto fail = [tomeScan](std::string what, std::shared_ptr<ScanTicket> held) {
            AddTaskToGameThread("ScanSpellsFailed", [what = std::move(what), tomeScan, held = std::move(held)]() {
                if (auto* instance = GetSingleton()) instance->ReportScanFailure(what.c_str(), tomeScan);
            });
        };
        try {
            std::thread([output, tomeScan, finish, fail, ticket]() mutable {
                // Nothing may leave a thread function: it would end the game
                // (std::terminate). A report that cannot be posted is only logged;
                // the ticket still ends the scan when this lambda goes.
                try {
                    try {
                        // The text first: the ticket must not be handed on before it exists
                        auto text = std::make_shared<std::string>(PrepareScanText(std::move(*output), tomeScan));
                        finish(std::move(text), std::move(ticket));
                    } catch (const std::exception& e) {
                        fail(e.what(), std::move(ticket));
                    } catch (...) {
                        fail("", std::move(ticket));
                    }
                } catch (...) {
                    logger::error("UIManager: the {} scan failed and the failure could not be reported",
                        tomeScan ? "tome" : "full");
                }
            }).detach();
        } catch (const std::system_error& e) {
            // No worker: the text work runs here, on the game thread, as it always did before
            logger::warn("UIManager: no thread for the scan's text work ({}) - doing it now", e.what());
            FinishScan(PrepareScanText(std::move(*output), tomeScan), tomeScan);
        }
    } catch (const std::exception& e) {
        ReportScanFailure(e.what(), tomeScan);
    } catch (...) {
        ReportScanFailure("", tomeScan);
    }
    // The scan ends when `ticket` and its copies are let go: here, or after the worker
}

std::string UIManager::PrepareScanText(json&& output, bool tomeScan)
{
    // Worker thread: what is in here takes strings and files, never a game form.
    // Librarian::ClassifyScan is the code the offline harness links without the
    // game (tools/CMakeLists.txt); it serializes itself against a Papyrus RunScan.
    // What the scan could not mark: its catalog is written but not recorded (ScanRecordRules.h)
    const std::string incomplete = tomeScan ? std::string() : ScanRecordRules::IncompleteOf(output);
    const std::string scanPairs = tomeScan ? std::string() : ScanRecordRules::TomePairsOf(output);
    std::string result = JsonText::Dump(output, 2);
    output = json();  // the parsed copy is 50-100 MB on a large load order

    // Classify what was just scanned and hand the catalog's elements on to
    // the traits the tree builder groups by and the chips the card shows.
    // Same call the Papyrus path makes, so the Scan button and the Papyrus
    // RunScan leave the same catalog behind. It logs its own failures and leaves
    // the result as it was rather than interrupting the scan.
    // The scan record follows the catalog file: a full scan's load order, or none
    // after a tome scan wrote it (ScanRecord.h; the fingerprint was worked out in CollectScan)
    Librarian::ClassifyScan(result, true, [tomeScan, &incomplete, &scanPairs]() {
        ScanRecord::CatalogWritten(!tomeScan, LoadOrderFingerprint::Known(), incomplete, scanPairs);
    });

    // Debug installs keep the full scan for offline analysis, as Papyrus RunScan does
    if (!tomeScan && SpellScanner::DebugScanDumpEnabled() && SpellScanner::WriteScanOutput(result).empty()) {
        logger::warn("UIManager: the debug scan dump was not written (see the error above); the scan itself is fine");
    }
    return result;
}

void UIManager::FinishScan(std::string result, bool tomeScan) noexcept
{
    try {
        // The perk adapters read the catalog the full scan just rebuilt, and with
        // Spell Research they write forms - so here, on the game thread. Not after
        // the tome scan the panel runs in the background: that leaves the catalog
        // as it was, and the work would land on the frames the panel opens in.
        if (!tomeScan) {
            auto started = std::chrono::steady_clock::now();
            Librarian::PerkAdapters::Apply("scan");
            const auto perkMs = MsSince(started);
            started = std::chrono::steady_clock::now();
            Librarian::SpellResearchPatch::OnCatalogChanged();
            logger::info("UIManager: after the full scan - perk adapters {:.0f} ms, Spell Research {:.0f} ms (game thread)", perkMs,
                MsSince(started));
        }

        if (tomeScan) {
            // A tome scan is a filter list, not the spells a tree is built from,
            // so it is sent and not kept.
            SendSpellData(result);
        } else {
            const auto started = std::chrono::steady_clock::now();
            const auto bytes = result.size();
            StoreAndSendFullScan(std::move(result));
            logger::info("UIManager: full scan ({} bytes) kept and sent to the panel in {:.0f} ms (game thread)", bytes,
                MsSince(started));
        }
    } catch (const std::exception& e) {
        ReportScanFailure(e.what(), tomeScan);
    } catch (...) {
        ReportScanFailure("", tomeScan);
    }
    // The scan itself ends when the task holding its ticket is let go, after this
    // returns - outside any try that could call a sent scan failed.
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

bool UIManager::TryBeginOtherScan()
{
    std::lock_guard lock(m_scanMutex);
    if (m_scanRunning) return false;
    m_scanRunning = true;
    return true;
}

void UIManager::EndOtherScan() noexcept
{
    EndScan();
}

void UIManager::EndScan() noexcept
{
    std::optional<std::string> next;
    {
        std::lock_guard lock(m_scanMutex);
        // The full scan first: the player is waiting on it, the tome list is a filter
        if (m_queuedFullScan) {
            next.swap(m_queuedFullScan);
        } else if (m_queuedTomeScan) {
            next.swap(m_queuedTomeScan);
        } else {
            m_scanRunning = false;  // with a next scan it stays set
        }
    }
    // LaunchScan clears the flag itself when the next one cannot start
    if (next) LaunchScan(std::move(*next));
}

void UIManager::ReportScanFailure(const char* what, bool tomeScan) noexcept
{
    // Every failure path ends here; a throw out of it would skip what follows at
    // its callers, so it logs its own trouble instead
    try {
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
    } catch (...) {
        logger::error("UIManager: the {} scan failed and the panel could not be told", tomeScan ? "tome" : "full");
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
