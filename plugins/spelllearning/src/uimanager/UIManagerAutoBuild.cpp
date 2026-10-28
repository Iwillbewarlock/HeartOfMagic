#include "Common.h"
#include "FileUtils.h"
#include "HudNotify.h"
#include "JsonText.h"
#include "LoadOrderFingerprint.h"
#include "PathText.h"
#include "ThreadUtils.h"
#include "uimanager/TreeGraftRecord.h"
#include "uimanager/TreeRecord.h"
#include "uimanager/TreeSaveCheck.h"
#include "uimanager/UIManager.h"
#include "uimanager/UIManagerInternal.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// =============================================================================
// TREE BUILT AT GAME START (docs/ARCHITECTURE.md, "Tree built at game start")
// =============================================================================
//
// The panel decides (modules/autoBuild.js); this file gives it the facts and
// keeps the one record it needs across game starts:
//
//   spell_tree_loadorder.json, beside spell_tree.json (its fields: TreeRecord.h)
//     { "version": 2, "loadOrder": "<fingerprint the tree was built under>",
//       "plugins": n, "spells": n, "noticedLoadOrder": "<fingerprint the player was told about>",
//       "seenSpells": [persistentId...], "pendingGrafted": n }
//
// The fingerprint is LoadOrderFingerprint.h's (asked for first by LoadSpellTree
// at the main menu, normally).
// Written when SaveSpellTree saves a build's Apply (manual or the one at game
// start: the panel sends those to SaveBuiltSpellTree, with the spells the
// build's scan saw); an edit mode save or an orphan repair leaves the record as
// it is, an import (SaveImportedSpellTree) only drops the seen list; a tree graft
// at the main menu (TreeGraftRun.cpp) adds to the seen list and pendingGrafted, and
// takes the load order when it placed every new spell. Fields a
// later version adds are kept. A tree saved before 1.5.0 has no record and gets
// no notice until it is built again.

namespace
{
    constexpr int kRecordIndent = 2;
    constexpr std::size_t kEventTextMax = 512;   // bytes of a panel event's text in the log, at most

    // The panel's "notice" is handled once per session
    std::atomic<bool> s_noticeAsked{ false };
    // The corner message for a run held in game ("held"), once per session
    std::atomic<bool> s_heldTold{ false };

    using TreeLoadOrderRecord = TreeRecordFile::TreeRecord;

    const LoadOrderFingerprint::Value& CurrentLoadOrder()
    {
        return LoadOrderFingerprint::Current();
    }

    TreeLoadOrderRecord ReadRecord()
    {
        TreeLoadOrderRecord record;
        std::ifstream file(UIManager::GetTreeLoadOrderFilePath());
        if (!file.is_open()) return record;
        const auto data = nlohmann::json::parse(file, nullptr, false);
        if (data.is_discarded() || !data.is_object()) {
            logger::warn("UIManager: spell_tree_loadorder.json could not be read - the saved tree's load order is unknown");
            return record;
        }
        return TreeRecordFile::ParseTreeRecord(data);
    }

    bool WriteRecord(const TreeLoadOrderRecord& record)
    {
        if (!FileUtils::WriteAtomically(UIManager::GetTreeLoadOrderFilePath(),
                JsonText::Dump(TreeRecordFile::ToJson(record), kRecordIndent), false)) {
            logger::warn("UIManager: could not write {}", PathText::Utf8(UIManager::GetTreeLoadOrderFilePath()));
            return false;
        }
        return true;
    }

    // A panel event's text for the log: at most kEventTextMax bytes, cut back to the
    // start of a UTF-8 character, line breaks made spaces (one event, one log line)
    std::string LogText(std::string text)
    {
        if (text.size() > kEventTextMax) {
            std::size_t end = kEventTextMax;
            while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
            text.resize(end);
        }
        for (char& c : text) {
            if (c == '\r' || c == '\n') c = ' ';
        }
        return text;
    }

    std::string_view StateName(UIManager::StartupTreeState state)
    {
        switch (state) {
            case UIManager::StartupTreeState::Missing: return "missing";
            case UIManager::StartupTreeState::Empty: return "empty";
            case UIManager::StartupTreeState::Present: return "present";
            case UIManager::StartupTreeState::Unreadable: return "unreadable";
        }
        return "unreadable";
    }
}

bool UIManager::WriteTreeRecord(const TreeRecordFile::TreeRecord& record)
{
    return WriteRecord(record);
}

std::size_t UIManager::CountTreeNodes(const nlohmann::json& tree)
{
    std::size_t count = 0;
    if (!tree.is_object() || !tree.contains("schools") || !tree["schools"].is_object()) return count;
    for (const auto& [name, school] : tree["schools"].items()) {
        if (school.is_object() && school.contains("nodes") && school["nodes"].is_array()) {
            count += school["nodes"].size();
        }
    }
    return count;
}

void UIManager::SendStartupTreeInfo(StartupTreeState state, std::size_t nodeCount) noexcept
{
    try {
        const auto& now = CurrentLoadOrder();
        // A record without its tree says nothing (the tree file was deleted by hand)
        const auto record = state == StartupTreeState::Missing ? TreeLoadOrderRecord{} : ReadRecord();

        nlohmann::json info;
        info["tree"] = StateName(state);
        info["nodes"] = nodeCount;
        info["loadOrder"] = now.hash;
        info["builtLoadOrder"] = record.loadOrder;
        info["noticedLoadOrder"] = record.noticedLoadOrder;

        logger::info("UIManager: startup tree info - tree {} ({} spells), load order {} ({} plugins, {} spell forms), "
                     "tree built under {}",
            StateName(state), nodeCount, now.hash.empty() ? "unknown" : now.hash, now.plugins, now.spells,
            record.loadOrder.empty() ? "unknown" : record.loadOrder);
        CallView("onStartupTreeInfo", JsonText::Dump(info).c_str());
    } catch (const std::exception& e) {
        logger::error("UIManager: startup tree info not sent: {}", e.what());
    } catch (...) {
        logger::error("UIManager: startup tree info not sent (unknown error)");
    }
}

std::optional<long long> UIManager::CheckTreeHasSpells(const std::string& treeText)
{
    // A text scan, not a parse: this runs on the game thread (TreeSaveCheck.h)
    const auto started = std::chrono::steady_clock::now();
    const bool hasSpells = TreeSaveCheck::HasSpellNodes(treeText);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    if (!hasSpells) {
        logger::error("UIManager: SaveSpellTree - the tree has no spells; spell_tree.json left as it was "
                      "(checked in {} ms)", ms);
        return std::nullopt;
    }
    return ms;
}

void UIManager::OnSaveSpellTree(const char* argument)
{
    SaveSpellTreeFromPanel(argument, TreeSaveKind::Edit);
}

void UIManager::OnSaveBuiltSpellTree(const char* argument)
{
    SaveSpellTreeFromPanel(argument, TreeSaveKind::Built);
}

void UIManager::OnSaveImportedSpellTree(const char* argument)
{
    SaveSpellTreeFromPanel(argument, TreeSaveKind::Imported);
}

void UIManager::RecordTreeLoadOrder(const std::vector<std::string>& seen)
{
    const auto& now = CurrentLoadOrder();
    if (now.hash.empty()) return;
    auto record = ReadRecord();
    // An empty list is no list: a build always has a scan, so none came (an unreadable
    // payload) and a graft takes its baseline instead of finding every spell new
    const bool hasSeen = !seen.empty();
    // Only when nothing changes: a tree built again under the load order it was
    // built for still clears a notice given meanwhile for another one (A, told
    // about B, back to A and built: B later must be told again)
    if (record.loadOrder == now.hash && record.noticedLoadOrder.empty() && record.hasSeen == hasSeen &&
        record.seen == seen && record.pendingGrafted == 0) {
        return;
    }
    record.loadOrder = now.hash;
    record.plugins = now.plugins;
    record.spells = now.spells;
    record.noticedLoadOrder.clear();   // the tree is this load order's now
    record.seen = seen;
    record.hasSeen = hasSeen;
    record.pendingGrafted = 0;         // a graft's spells are not in this tree to tell about
    WriteRecord(record);
    logger::info("UIManager: spell tree built under load order {} ({} plugins, {} spell forms); its scan saw {}",
        now.hash, now.plugins, now.spells, hasSeen ? std::format("{} spells", seen.size()) : std::string("no list (none sent)"));
}

void UIManager::ForgetSeenSpells()
{
    auto record = ReadRecord();
    if (!record.hasSeen && record.pendingGrafted == 0) return;
    record.seen.clear();
    record.hasSeen = false;
    record.pendingGrafted = 0;   // the grafted spells were the replaced tree's
    WriteRecord(record);
    logger::info("UIManager: imported spell tree - the record's seen list (another build's) and its count of grafted "
                 "spells to tell about dropped");
}

void UIManager::ForgetLostGraftedSpells(const std::vector<std::string>& lost, const TreeGraftRun::HeldLoadOrder& held)
{
    if (lost.empty()) return;
    const auto record = TreeGraftRun::RecordAfterLostGraft(ReadRecord(), lost, held);
    if (!WriteRecord(record)) {
        logger::warn("TreeGraft: the {} dropped grafted spells stay in the seen list (the record was not written) - a "
                     "rebuild places them", lost.size());
        return;
    }
    logger::info("TreeGraft: {} grafted spells the panel's save dropped (first {}) left the seen list; the record's load "
                 "order is {} again, so the next game start grafts them again", lost.size(), lost.front(),
        held.loadOrder.empty() ? "unknown" : held.loadOrder);
}

void UIManager::ShowPendingLoadOrderNotice()
{
    if (m_pendingLoadOrderNotice.empty()) return;
    const std::string loadOrder = std::move(m_pendingLoadOrderNotice);
    m_pendingLoadOrderNotice.clear();
    auto record = ReadRecord();
    if (record.loadOrder.empty() || record.loadOrder == loadOrder) {
        logger::info("AutoBuild: load order notice dropped - the tree was built under this load order since");
        return;
    }
    if (!HudNotify::Show("hud.loadOrderChanged",
            "The load order changed since the spell tree was built. Rebuild it on the Scan tab to include new spells.")) {
        logger::info("AutoBuild: load order notice not shown - asked again at the next game start");
        return;
    }
    // Told: not again for this load order (a build under it clears this)
    record.noticedLoadOrder = loadOrder;
    WriteRecord(record);
    logger::info("AutoBuild: load order notice shown for {}", loadOrder);
}

void UIManager::ShowPendingGraftNotice()
{
    auto record = ReadRecord();
    if (record.pendingGrafted == 0) return;
    if (!HudNotify::Show("hud.spellsGrafted", "{{count}} new spells joined the spell tree",
            { { "count", std::to_string(record.pendingGrafted) } })) {
        logger::info("TreeGraft: the new spells' corner message not shown - asked again at the next save load");
        return;
    }
    const auto told = record.pendingGrafted;
    record.pendingGrafted = 0;   // told: not again (the next graft counts afresh)
    if (WriteRecord(record)) logger::info("TreeGraft: corner message shown for {} new spells", told);
}

void UIManager::NotifyTreeGrafted(const std::string& loadOrder)
{
    nlohmann::json info;
    info["loadOrder"] = loadOrder;
    CallView("onTreeGrafted", JsonText::Dump(info).c_str());
}

void UIManager::OnAutoBuildEvent(const char* argument)
{
    if (!argument || argument[0] == '\0') return;

    const auto data = nlohmann::json::parse(argument, nullptr, false);
    if (data.is_discarded() || !data.is_object()) {
        logger::warn("AutoBuild: unreadable event from the panel");
        return;
    }
    const auto event = LogText(SafeJsonValue<std::string>(data, "event", ""));
    const auto detail = LogText(SafeJsonValue<std::string>(data, "detail", ""));
    logger::info("AutoBuild: {} - {}", event, detail);

    // The corner message waits for the first save load (the main menu has none);
    // it is recorded as told only once it was shown (ShowPendingLoadOrderNotice).
    // A save loaded before the panel decided (Continue pressed within its wait for
    // the config and presets) gets it now: kPostLoadGame has gone by.
    if (event == "notice" && !s_noticeAsked.exchange(true)) {
        AddTaskToGameThread("AutoBuildNotice", []() {
            auto* instance = GetSingleton();
            const auto& now = CurrentLoadOrder();
            if (!instance || now.hash.empty()) return;
            instance->m_pendingLoadOrderNotice = now.hash;
            if (instance->m_saveLoaded) {
                logger::info("AutoBuild: a save is loaded already - the load order notice is shown now");
                instance->ShowPendingLoadOrderNotice();
            }
        });
    }

    // A run held while a save is loaded (autoBuild.js _step): once per session in the
    // corner, as the notice is, so a player who never opens the panel knows why
    if (event == "held" && !s_heldTold.exchange(true)) {
        AddTaskToGameThread("AutoBuildHeld", []() {
            if (!HudNotify::Show("hud.autoBuildHeld",
                    "The spell tree will finish building when you open the Heart of Magic panel.")) {
                logger::info("AutoBuild: held message not shown (notifications off)");
            }
        });
    }
}

namespace
{
    // MainMenu opening: at start and when the player quits a game to it
    class MainMenuWatch final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        static MainMenuWatch* GetSingleton()
        {
            static MainMenuWatch singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (event && event->opening && event->menuName == RE::MainMenu::MENU_NAME) {
                AddTaskToGameThread("AutoBuildMainMenu", []() {
                    if (auto* instance = UIManager::GetSingleton()) instance->NotifyMainMenuShown();
                });
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        MainMenuWatch(const MainMenuWatch&) = delete;
        MainMenuWatch& operator=(const MainMenuWatch&) = delete;

    private:
        MainMenuWatch() = default;
    };
}

void UIManager::WatchMainMenu()
{
    static bool s_watching = false;
    if (s_watching) return;
    auto* ui = RE::UI::GetSingleton();
    if (!ui) {
        logger::warn("AutoBuild: no UI to watch the main menu - a held run goes on only when the panel opens");
        return;
    }
    ui->AddEventSink<RE::MenuOpenCloseEvent>(MainMenuWatch::GetSingleton());
    s_watching = true;
}

void UIManager::NotifyNewGameStarted()
{
    // Only the build at game start hears of it: a new game is played as a loaded save is
    // (m_saveLoaded stays false - a corner message at kNewGame may not reach the screen)
    m_newGameStarted = true;
    CallView("onNewGameStarted", "");
}

void UIManager::NotifyMainMenuShown()
{
    m_saveLoaded = false;   // nothing is played at the main menu
    m_newGameStarted = false;
    CallView("onMainMenuShown", "");
}

void UIManager::NotifySpellTomesChanged(const std::string& payload)
{
    CallView("onSpellTomesChanged", payload.c_str());
}
