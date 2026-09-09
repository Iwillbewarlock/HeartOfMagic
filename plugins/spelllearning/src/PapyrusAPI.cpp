#include "PapyrusAPI.h"
#include "uimanager/UIManager.h"
#include "ProgressionManager.h"
#include "SpellScanner.h"
#include "ThreadUtils.h"
#include "SKSE/SKSE.h"

#include <future>

namespace PapyrusAPI
{
    constexpr const char* SCRIPT_NAME = "SpellLearning";
    constexpr const char* MOD_VERSION = "1.0.0";

    // A full-load-order scan takes a few seconds; the ceiling is only there so a
    // dropped game-thread task cannot hang the calling script forever.
    constexpr int SCAN_TIMEOUT_SECONDS = 120;

    // ModEvent names
    constexpr const char* EVENT_MENU_OPENED = "SpellLearning_MenuOpened";
    constexpr const char* EVENT_MENU_CLOSED = "SpellLearning_MenuClosed";

    // =========================================================================
    // MENU FUNCTIONS
    // =========================================================================

    void OpenMenu(RE::StaticFunctionTag*)
    {
        logger::info("PapyrusAPI: OpenMenu called");
        auto* uiManager = UIManager::GetSingleton();
        if (uiManager && uiManager->IsInitialized()) {
            uiManager->ShowPanel();
        } else {
            logger::warn("PapyrusAPI: UIManager not initialized, cannot open menu");
        }
    }

    void CloseMenu(RE::StaticFunctionTag*)
    {
        logger::info("PapyrusAPI: CloseMenu called");
        auto* uiManager = UIManager::GetSingleton();
        if (uiManager && uiManager->IsInitialized()) {
            uiManager->HidePanel();
        }
    }

    void ToggleMenu(RE::StaticFunctionTag*)
    {
        logger::info("PapyrusAPI: ToggleMenu called");
        auto* uiManager = UIManager::GetSingleton();
        if (uiManager && uiManager->IsInitialized()) {
            uiManager->TogglePanel();
        } else {
            logger::warn("PapyrusAPI: UIManager not initialized, cannot toggle menu");
        }
    }

    bool IsMenuOpen(RE::StaticFunctionTag*)
    {
        auto* uiManager = UIManager::GetSingleton();
        if (uiManager) {
            return uiManager->IsPanelVisible();
        }
        return false;
    }

    RE::BSFixedString GetVersion(RE::StaticFunctionTag*)
    {
        return RE::BSFixedString(MOD_VERSION);
    }

    // =========================================================================
    // XP FUNCTIONS
    // =========================================================================

    void RegisterXPSource(RE::StaticFunctionTag*, RE::BSFixedString sourceId, RE::BSFixedString displayName)
    {
        std::string id = sourceId.c_str();
        std::string name = displayName.c_str();
        if (id.empty()) {
            logger::warn("PapyrusAPI: RegisterXPSource called with empty sourceId");
            return;
        }
        logger::info("PapyrusAPI: RegisterXPSource('{}', '{}')", id, name);
        ProgressionManager::GetSingleton()->RegisterModdedXPSource(id, name);
    }

    float AddSourcedXP(RE::StaticFunctionTag*, RE::SpellItem* spell, float amount, RE::BSFixedString sourceName)
    {
        if (!spell) {
            logger::warn("PapyrusAPI: AddSourcedXP called with null spell");
            return 0.0f;
        }
        std::string source = sourceName.c_str();
        if (source.empty()) source = "direct";
        logger::info("PapyrusAPI: AddSourcedXP({:08X}, {:.1f}, '{}')", spell->GetFormID(), amount, source);
        return ProgressionManager::GetSingleton()->AddSourcedXP(spell->GetFormID(), amount, source);
    }

    float AddRawXP(RE::StaticFunctionTag*, RE::SpellItem* spell, float amount)
    {
        if (!spell) {
            logger::warn("PapyrusAPI: AddRawXP called with null spell");
            return 0.0f;
        }
        logger::info("PapyrusAPI: AddRawXP({:08X}, {:.1f})", spell->GetFormID(), amount);
        return ProgressionManager::GetSingleton()->AddRawXP(spell->GetFormID(), amount);
    }

    void SetSpellXP(RE::StaticFunctionTag*, RE::SpellItem* spell, float xp)
    {
        if (!spell) {
            logger::warn("PapyrusAPI: SetSpellXP called with null spell");
            return;
        }
        logger::info("PapyrusAPI: SetSpellXP({:08X}, {:.1f})", spell->GetFormID(), xp);
        ProgressionManager::GetSingleton()->SetSpellXP(spell->GetFormID(), xp);
    }

    // =========================================================================
    // PROGRESS QUERIES
    // =========================================================================

    float GetSpellProgress(RE::StaticFunctionTag*, RE::SpellItem* spell)
    {
        if (!spell) return 0.0f;
        auto progress = ProgressionManager::GetSingleton()->GetProgress(spell->GetFormID());
        return progress.progressPercent * 100.0f;
    }

    float GetSpellCurrentXP(RE::StaticFunctionTag*, RE::SpellItem* spell)
    {
        if (!spell) return 0.0f;
        auto progress = ProgressionManager::GetSingleton()->GetProgress(spell->GetFormID());
        return progress.GetCurrentXP();
    }

    float GetSpellRequiredXP(RE::StaticFunctionTag*, RE::SpellItem* spell)
    {
        if (!spell) return 0.0f;
        return ProgressionManager::GetSingleton()->GetRequiredXP(spell->GetFormID());
    }

    bool IsSpellMastered(RE::StaticFunctionTag*, RE::SpellItem* spell)
    {
        if (!spell) return false;
        return ProgressionManager::GetSingleton()->IsSpellMastered(spell->GetFormID());
    }

    bool IsSpellUnlocked(RE::StaticFunctionTag*, RE::SpellItem* spell)
    {
        if (!spell) return false;
        return ProgressionManager::GetSingleton()->IsUnlocked(spell->GetFormID());
    }

    bool IsSpellAvailableToLearn(RE::StaticFunctionTag*, RE::SpellItem* spell)
    {
        if (!spell) return false;
        return ProgressionManager::GetSingleton()->IsSpellAvailableToLearn(spell->GetFormID());
    }

    bool ArePrerequisitesMet(RE::StaticFunctionTag*, RE::SpellItem* spell)
    {
        if (!spell) return false;
        return ProgressionManager::GetSingleton()->AreTreePrerequisitesMet(spell->GetFormID());
    }

    // =========================================================================
    // LEARNING TARGET CONTROL
    // =========================================================================

    RE::SpellItem* GetLearningTarget(RE::StaticFunctionTag*, RE::BSFixedString schoolName)
    {
        std::string school = schoolName.c_str();
        RE::FormID formId = ProgressionManager::GetSingleton()->GetLearningTarget(school);
        if (formId == 0) return nullptr;
        return RE::TESForm::LookupByID<RE::SpellItem>(formId);
    }

    std::vector<RE::SpellItem*> GetAllLearningTargets(RE::StaticFunctionTag*)
    {
        std::vector<RE::SpellItem*> result;
        auto* pm = ProgressionManager::GetSingleton();
        const char* schools[] = {"Alteration", "Conjuration", "Destruction", "Illusion", "Restoration"};
        for (const char* school : schools) {
            RE::FormID formId = pm->GetLearningTarget(school);
            if (formId != 0) {
                auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
                if (spell) {
                    result.push_back(spell);
                }
            }
        }
        return result;
    }

    RE::BSFixedString GetLearningMode(RE::StaticFunctionTag*)
    {
        return RE::BSFixedString(ProgressionManager::GetSingleton()->GetXPSettings().learningMode.c_str());
    }

    void SetLearningTarget(RE::StaticFunctionTag*, RE::SpellItem* spell)
    {
        if (!spell) {
            logger::warn("PapyrusAPI: SetLearningTarget called with null spell");
            return;
        }
        logger::info("PapyrusAPI: SetLearningTarget({:08X})", spell->GetFormID());
        // Use the tome-reading path which auto-determines school
        std::stringstream ss;
        ss << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << spell->GetFormID();
        ProgressionManager::GetSingleton()->SetLearningTargetFromTome(ss.str(), spell);
    }

    void SetLearningTargetForSchool(RE::StaticFunctionTag*, RE::BSFixedString schoolName, RE::SpellItem* spell)
    {
        if (!spell) {
            logger::warn("PapyrusAPI: SetLearningTargetForSchool called with null spell");
            return;
        }
        std::string school = schoolName.c_str();
        logger::info("PapyrusAPI: SetLearningTargetForSchool('{}', {:08X})", school, spell->GetFormID());
        ProgressionManager::GetSingleton()->SetLearningTarget(school, spell->GetFormID());
    }

    void ClearLearningTarget(RE::StaticFunctionTag*, RE::BSFixedString schoolName)
    {
        std::string school = schoolName.c_str();
        logger::info("PapyrusAPI: ClearLearningTarget('{}')", school);
        ProgressionManager::GetSingleton()->ClearLearningTarget(school);
    }

    void ClearAllLearningTargets(RE::StaticFunctionTag*)
    {
        logger::info("PapyrusAPI: ClearAllLearningTargets");
        auto* pm = ProgressionManager::GetSingleton();
        const char* schools[] = {"Alteration", "Conjuration", "Destruction", "Illusion", "Restoration"};
        for (const char* school : schools) {
            pm->ClearLearningTarget(school);
        }
    }

    // =========================================================================
    // SETTINGS QUERIES
    // =========================================================================

    float GetGlobalXPMultiplier(RE::StaticFunctionTag*)
    {
        return ProgressionManager::GetSingleton()->GetXPSettings().globalMultiplier;
    }

    float GetXPForTier(RE::StaticFunctionTag*, RE::BSFixedString tier)
    {
        return ProgressionManager::GetSingleton()->GetXPForTier(tier.c_str());
    }

    float GetSourceCap(RE::StaticFunctionTag*, RE::BSFixedString sourceName)
    {
        return ProgressionManager::GetSingleton()->GetSourceCap(sourceName.c_str());
    }

    // =========================================================================
    // SCANNING
    // =========================================================================

    RE::BSFixedString RunScan(RE::StaticFunctionTag*, RE::BSFixedString mode, RE::BSFixedString preset)
    {
        const std::string modeStr = mode.c_str() ? mode.c_str() : "";
        const std::string presetStr = preset.c_str() ? preset.c_str() : "";

        logger::info("PapyrusAPI: RunScan called (mode='{}', preset='{}')", modeStr, presetStr);

        // The scan walks the game's form arrays, so it has to run on the game
        // thread. Papyrus calls this from the VM thread, but a caller already on
        // the game thread cannot submit a task and wait for it - the task only
        // runs once this call returns. Already on the right thread, so just work.
        if (IsOnGameThread()) {
            const std::string outputPath = SpellScanner::RunScanToFile(modeStr, presetStr);
            logger::info("PapyrusAPI: RunScan finished on the game thread, output '{}'", outputPath);
            return RE::BSFixedString(outputPath.c_str());
        }

        // The calling script waits for the result. The task is the promise's
        // only owner (moved in, not shared with this frame): if it is dropped
        // before running, or destroyed after an exception, the promise breaks
        // and the wait below ends at once instead of sitting out the whole
        // timeout. A shared_ptr rather than the promise itself because
        // std::function needs a copyable callable.
        auto resultPromise = std::make_shared<std::promise<std::string>>();
        auto resultFuture = resultPromise->get_future();

        AddTaskToGameThread("RunScan",
            [resultPromise = std::move(resultPromise), modeStr, presetStr]() {
                try {
                    resultPromise->set_value(SpellScanner::RunScanToFile(modeStr, presetStr));
                } catch (...) {
                    resultPromise->set_exception(std::current_exception());
                }
            });

        if (resultFuture.wait_for(std::chrono::seconds(SCAN_TIMEOUT_SECONDS)) != std::future_status::ready) {
            logger::error("PapyrusAPI: RunScan timed out after {} seconds", SCAN_TIMEOUT_SECONDS);
            return RE::BSFixedString("");
        }

        try {
            const std::string outputPath = resultFuture.get();
            logger::info("PapyrusAPI: RunScan finished, output '{}'", outputPath);
            return RE::BSFixedString(outputPath.c_str());
        } catch (const std::exception& e) {
            // Either the scan threw, or the task was dropped without running
            // (broken_promise)
            logger::error("PapyrusAPI: RunScan failed: {}", e.what());
            return RE::BSFixedString("");
        }
    }

    // =========================================================================
    // MOD EVENT SENDERS
    // =========================================================================

    void SendMenuOpenedEvent()
    {
        logger::info("PapyrusAPI: Sending {} ModEvent", EVENT_MENU_OPENED);
        SKSE::ModCallbackEvent modEvent(EVENT_MENU_OPENED, "", 0.0f, nullptr);
        SKSE::GetModCallbackEventSource()->SendEvent(&modEvent);
    }

    void SendMenuClosedEvent()
    {
        logger::info("PapyrusAPI: Sending {} ModEvent", EVENT_MENU_CLOSED);
        SKSE::ModCallbackEvent modEvent(EVENT_MENU_CLOSED, "", 0.0f, nullptr);
        SKSE::GetModCallbackEventSource()->SendEvent(&modEvent);
    }

    // =========================================================================
    // REGISTRATION
    // =========================================================================

    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm)
    {
        if (!vm) {
            logger::error("PapyrusAPI: Failed to register functions - VM is null");
            return false;
        }

        // === Menu ===
        vm->RegisterFunction("OpenMenu", SCRIPT_NAME, OpenMenu);
        vm->RegisterFunction("CloseMenu", SCRIPT_NAME, CloseMenu);
        vm->RegisterFunction("ToggleMenu", SCRIPT_NAME, ToggleMenu);
        vm->RegisterFunction("IsMenuOpen", SCRIPT_NAME, IsMenuOpen);
        vm->RegisterFunction("GetVersion", SCRIPT_NAME, GetVersion);

        // === XP ===
        vm->RegisterFunction("RegisterXPSource", SCRIPT_NAME, RegisterXPSource);
        vm->RegisterFunction("AddSourcedXP", SCRIPT_NAME, AddSourcedXP);
        vm->RegisterFunction("AddRawXP", SCRIPT_NAME, AddRawXP);
        vm->RegisterFunction("SetSpellXP", SCRIPT_NAME, SetSpellXP);

        // === Progress Queries ===
        vm->RegisterFunction("GetSpellProgress", SCRIPT_NAME, GetSpellProgress);
        vm->RegisterFunction("GetSpellCurrentXP", SCRIPT_NAME, GetSpellCurrentXP);
        vm->RegisterFunction("GetSpellRequiredXP", SCRIPT_NAME, GetSpellRequiredXP);
        vm->RegisterFunction("IsSpellMastered", SCRIPT_NAME, IsSpellMastered);
        vm->RegisterFunction("IsSpellUnlocked", SCRIPT_NAME, IsSpellUnlocked);
        vm->RegisterFunction("IsSpellAvailableToLearn", SCRIPT_NAME, IsSpellAvailableToLearn);
        vm->RegisterFunction("ArePrerequisitesMet", SCRIPT_NAME, ArePrerequisitesMet);

        // === Learning Target Control ===
        vm->RegisterFunction("GetLearningTarget", SCRIPT_NAME, GetLearningTarget);
        vm->RegisterFunction("GetAllLearningTargets", SCRIPT_NAME, GetAllLearningTargets);
        vm->RegisterFunction("GetLearningMode", SCRIPT_NAME, GetLearningMode);
        vm->RegisterFunction("SetLearningTarget", SCRIPT_NAME, SetLearningTarget);
        vm->RegisterFunction("SetLearningTargetForSchool", SCRIPT_NAME, SetLearningTargetForSchool);
        vm->RegisterFunction("ClearLearningTarget", SCRIPT_NAME, ClearLearningTarget);
        vm->RegisterFunction("ClearAllLearningTargets", SCRIPT_NAME, ClearAllLearningTargets);

        // === Settings Queries ===
        vm->RegisterFunction("GetGlobalXPMultiplier", SCRIPT_NAME, GetGlobalXPMultiplier);
        vm->RegisterFunction("GetXPForTier", SCRIPT_NAME, GetXPForTier);
        vm->RegisterFunction("GetSourceCap", SCRIPT_NAME, GetSourceCap);

        // === Scanning ===
        vm->RegisterFunction("RunScan", SCRIPT_NAME, RunScan);

        logger::info("PapyrusAPI: Registered {} functions under script '{}'", 27, SCRIPT_NAME);

        return true;
    }
}
