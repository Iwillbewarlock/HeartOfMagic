#include "Common.h"
#include "FileUtils.h"
#include "HudNotify.h"
#include "JsonText.h"
#include "PathText.h"
#include "ThreadUtils.h"
#include "uimanager/UIManager.h"
#include "uimanager/UIManagerInternal.h"

#include <cstdint>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

// =============================================================================
// TREE BUILT AT GAME START (docs/ARCHITECTURE.md, "Tree built at game start")
// =============================================================================
//
// The panel decides (modules/autoBuild.js); this file gives it the facts and
// keeps the one record it needs across game starts:
//
//   spell_tree_loadorder.json, beside spell_tree.json
//     { "version": 1, "loadOrder": "<fingerprint the tree was saved under>",
//       "plugins": n, "spells": n, "noticedLoadOrder": "<fingerprint the player was told about>" }
//
// The fingerprint is FNV-1a 64 over the loaded plugin file names in load order
// (full plugins, then light ones) and the number of SpellItem forms: the same
// for the same load order on every start, different when a plugin is added,
// removed or moved. Written whenever SaveSpellTree saves (a build's Apply, an
// edit mode save, an import); a tree saved before 1.4.1 has no record and
// gets no notice until its next save.

namespace
{
    constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;
    constexpr std::string_view kNameEnd = "\n";
    constexpr std::string_view kLightPluginsMark = "|light|\n";
    constexpr int kRecordVersion = 1;
    constexpr int kRecordIndent = 2;

    struct LoadOrderFingerprint
    {
        std::string hash;          // 16 hex digits; empty before kDataLoaded
        std::size_t plugins = 0;
        std::size_t spells = 0;
    };

    struct TreeLoadOrderRecord
    {
        std::string loadOrder;
        std::string noticedLoadOrder;
    };

    void HashBytes(std::uint64_t& hash, std::string_view bytes)
    {
        for (const unsigned char c : bytes) {
            hash ^= c;
            hash *= kFnvPrime;
        }
    }

    // Game thread. Worked out once: the load order does not change while the game runs.
    const LoadOrderFingerprint& CurrentLoadOrder()
    {
        static std::optional<LoadOrderFingerprint> s_cached;
        static const LoadOrderFingerprint s_unknown{};
        if (s_cached) return *s_cached;

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return s_unknown;

        LoadOrderFingerprint result;
        std::uint64_t hash = kFnvOffsetBasis;
        auto addFile = [&](const RE::TESFile* file) {
            if (!file) return;
            HashBytes(hash, file->GetFilename());
            HashBytes(hash, kNameEnd);
            ++result.plugins;
        };
        for (std::uint32_t i = 0; i < dh->GetLoadedModCount(); ++i) addFile(dh->GetLoadedMods()[i]);
        HashBytes(hash, kLightPluginsMark);
        if (const auto* light = dh->GetLoadedLightMods()) {
            for (std::uint32_t i = 0; i < dh->GetLoadedLightModCount(); ++i) addFile(light[i]);
        }
        result.spells = dh->GetFormArray<RE::SpellItem>().size();
        HashBytes(hash, std::to_string(result.spells));
        result.hash = std::format("{:016x}", hash);

        s_cached = std::move(result);
        return *s_cached;
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
        record.loadOrder = SafeJsonValue<std::string>(data, "loadOrder", "");
        record.noticedLoadOrder = SafeJsonValue<std::string>(data, "noticedLoadOrder", "");
        return record;
    }

    void WriteRecord(const TreeLoadOrderRecord& record)
    {
        const auto& now = CurrentLoadOrder();
        nlohmann::json data;
        data["version"] = kRecordVersion;
        data["loadOrder"] = record.loadOrder;
        data["plugins"] = now.plugins;
        data["spells"] = now.spells;
        data["noticedLoadOrder"] = record.noticedLoadOrder;
        if (!FileUtils::WriteAtomically(UIManager::GetTreeLoadOrderFilePath(), JsonText::Dump(data, kRecordIndent), false)) {
            logger::warn("UIManager: could not write {}", PathText::Utf8(UIManager::GetTreeLoadOrderFilePath()));
        }
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
                     "tree saved under {}",
            StateName(state), nodeCount, now.hash.empty() ? "unknown" : now.hash, now.plugins, now.spells,
            record.loadOrder.empty() ? "unknown" : record.loadOrder);
        CallView("onStartupTreeInfo", JsonText::Dump(info).c_str());
    } catch (const std::exception& e) {
        logger::error("UIManager: startup tree info not sent: {}", e.what());
    } catch (...) {
        logger::error("UIManager: startup tree info not sent (unknown error)");
    }
}

void UIManager::RecordTreeLoadOrder()
{
    const auto& now = CurrentLoadOrder();
    if (now.hash.empty()) return;
    auto record = ReadRecord();
    if (record.loadOrder == now.hash) return;
    record.loadOrder = now.hash;
    record.noticedLoadOrder.clear();   // the tree is this load order's now
    WriteRecord(record);
    logger::info("UIManager: spell tree saved under load order {} ({} plugins, {} spell forms)",
        now.hash, now.plugins, now.spells);
}

void UIManager::ShowPendingLoadOrderNotice()
{
    if (!m_loadOrderNoticePending) return;
    m_loadOrderNoticePending = false;
    HudNotify::Show("hud.loadOrderChanged",
        "Heart of Magic: your load order changed since the spell tree was built. Rebuild it on the Scan tab to add new spells.");
    logger::info("AutoBuild: load order notice shown");
}

void UIManager::OnAutoBuildEvent(const char* argument)
{
    if (!argument || argument[0] == '\0') return;

    const auto data = nlohmann::json::parse(argument, nullptr, false);
    if (data.is_discarded() || !data.is_object()) {
        logger::warn("AutoBuild: unreadable event from the panel");
        return;
    }
    const auto event = SafeJsonValue<std::string>(data, "event", "");
    logger::info("AutoBuild: {} - {}", event, SafeJsonValue<std::string>(data, "detail", ""));

    if (event == "notice") {
        AddTaskToGameThread("AutoBuildNotice", []() {
            auto* instance = GetSingleton();
            const auto& now = CurrentLoadOrder();
            if (!instance || now.hash.empty()) return;
            auto record = ReadRecord();
            record.noticedLoadOrder = now.hash;
            WriteRecord(record);
            instance->m_loadOrderNoticePending = true;
        });
    }
}
