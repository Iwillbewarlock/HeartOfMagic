#include "Common.h"
#include "FormIdText.h"
#include "CoSave.h"
#include "HudNotify.h"
#include "uimanager/UIManager.h"
#include "SpellScanner.h"
#include "SpellCastHandler.h"
#include "ProgressionManager.h"
#include "ISLIntegration.h"
#include "XPSource.h"
#include "SpellCastXPSource.h"
#include "PassiveLearningSource.h"
#include "SpellEffectivenessHook.h"
#include "SpellTomeHook.h"
#include "SpellTomes.h"
#include "PapyrusAPI.h"
#include "RisaMenu.h"
#include "SpellLearningAPI.h"
#include "ThreadUtils.h"
#include "environment/EnvironmentSelfTest.h"
#include "environment/EnvironmentTwins.h"
#include "librarian/PerkAdapterPatch.h"
#include "librarian/SpellResearchPatch.h"
#include "tomereach/TomeReach.h"

// =============================================================================
// SPELL LEARNING API IMPLEMENTATION (for SKSE inter-plugin messaging)
// =============================================================================

// Another plugin calls these, from its own DLL: an exception must not cross
// into it (its runtime cannot catch ours, and the game would go down). The
// call is logged and answered with R{} (0 XP, false) instead.
template <class F>
static auto GuardApiCall(const char* name, F&& call) noexcept -> std::invoke_result_t<F>
{
    using R = std::invoke_result_t<F>;
    try {
        return call();
    } catch (const std::exception& e) {
        logger::error("SpellLearning API: {} threw: {}", name, e.what());
    } catch (...) {
        logger::error("SpellLearning API: {} threw an unknown exception", name);
    }
    if constexpr (!std::is_void_v<R>) {
        return R{};
    }
}

class SpellLearningAPIImpl : public SpellLearning::ISpellLearningAPI
{
public:
    static SpellLearningAPIImpl* GetSingleton()
    {
        static SpellLearningAPIImpl singleton;
        return &singleton;
    }

    uint32_t GetAPIVersion() const override { return SpellLearning::kAPIVersion; }

    float AddSourcedXP(uint32_t spellFormID, float amount, const std::string& sourceName) override
    {
        return GuardApiCall("AddSourcedXP", [&] {
            return ProgressionManager::GetSingleton()->AddSourcedXP(
                static_cast<RE::FormID>(spellFormID), amount, sourceName);
        });
    }

    float AddRawXP(uint32_t spellFormID, float amount) override
    {
        return GuardApiCall("AddRawXP", [&] {
            return ProgressionManager::GetSingleton()->AddRawXP(
                static_cast<RE::FormID>(spellFormID), amount);
        });
    }

    void SetSpellXP(uint32_t spellFormID, float xp) override
    {
        GuardApiCall("SetSpellXP", [&] {
            ProgressionManager::GetSingleton()->SetSpellXP(
                static_cast<RE::FormID>(spellFormID), xp);
        });
    }

    bool IsSpellMastered(uint32_t spellFormID) const override
    {
        return GuardApiCall("IsSpellMastered", [&] {
            return ProgressionManager::GetSingleton()->IsSpellMastered(static_cast<RE::FormID>(spellFormID));
        });
    }

    bool IsSpellAvailableToLearn(uint32_t spellFormID) const override
    {
        return GuardApiCall("IsSpellAvailableToLearn", [&] {
            return ProgressionManager::GetSingleton()->IsSpellAvailableToLearn(static_cast<RE::FormID>(spellFormID));
        });
    }

    float GetRequiredXP(uint32_t spellFormID) const override
    {
        return GuardApiCall("GetRequiredXP", [&] {
            return ProgressionManager::GetSingleton()->GetRequiredXP(static_cast<RE::FormID>(spellFormID));
        });
    }

    float GetProgress(uint32_t spellFormID) const override
    {
        return GuardApiCall("GetProgress", [&] {
            return ProgressionManager::GetSingleton()->GetProgressPercent(static_cast<RE::FormID>(spellFormID)) * 100.0f;
        });
    }

    uint32_t GetLearningTarget(const std::string& school) const override
    {
        return GuardApiCall("GetLearningTarget", [&] {
            return static_cast<uint32_t>(ProgressionManager::GetSingleton()->GetLearningTarget(school));
        });
    }

    void SetLearningTarget(uint32_t spellFormID) override
    {
        GuardApiCall("SetLearningTarget", [&] {
            auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(
                static_cast<RE::FormID>(spellFormID));
            if (spell) {
                ProgressionManager::GetSingleton()->SetLearningTargetFromTome(FormIdText::Format(spellFormID), spell);
            }
        });
    }

    void ClearLearningTarget(const std::string& school) override
    {
        GuardApiCall("ClearLearningTarget", [&] {
            ProgressionManager::GetSingleton()->ClearLearningTarget(school);
        });
    }

    float GetGlobalMultiplier() const override
    {
        return GuardApiCall("GetGlobalMultiplier", [&] {
            return ProgressionManager::GetSingleton()->GetXPSettings().globalMultiplier;
        });
    }

    bool RegisterXPSource(const std::string& sourceId, const std::string& displayName) override
    {
        return GuardApiCall("RegisterXPSource", [&] {
            return ProgressionManager::GetSingleton()->RegisterModdedXPSource(sourceId, displayName);
        });
    }

    bool ShowNotification(const char* key, const char* englishText,
                          const char* const* namesAndValues, uint32_t pairCount) override
    {
        return GuardApiCall("ShowNotification", [&] {
            std::vector<HudNotify::Param> params;
            for (uint32_t i = 0; namesAndValues && i < pairCount; ++i) {
                const char* name = namesAndValues[i * 2];
                const char* value = namesAndValues[i * 2 + 1];
                if (name) params.emplace_back(std::string_view(name), std::string(value ? value : ""));
            }
            return HudNotify::Show(key ? key : "", englishText ? englishText : "",
                                   std::span<const HudNotify::Param>(params));
        });
    }

private:
    SpellLearningAPIImpl() = default;
};

// =============================================================================
// INPUT HANDLER - Configurable Hotkey
// =============================================================================

class InputHandler : public RE::BSTEventSink<RE::InputEvent*>
{
public:
    static InputHandler* GetSingleton()
    {
        static InputHandler singleton;
        return &singleton;
    }

    void Register()
    {
        auto* inputManager = RE::BSInputDeviceManager::GetSingleton();
        if (inputManager) {
            inputManager->AddEventSink(this);
            logger::info("Input handler registered with hotkey code: {}", m_hotkeyCode);
        }
    }
    
    void SetHotkeyCode(uint32_t code)
    {
        m_hotkeyCode = code;
        logger::info("InputHandler: Hotkey code updated to {}", code);
    }
    
    uint32_t GetHotkeyCode() const { return m_hotkeyCode; }

    RE::BSEventNotifyControl ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>*) override
    {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // PERFORMANCE: Single pass through events, exit early when possible
        for (auto* event = *a_event; event; event = event->next) {
            // Skip non-button events immediately
            if (event->eventType != RE::INPUT_EVENT_TYPE::kButton) {
                continue;
            }

            auto* buttonEvent = static_cast<RE::ButtonEvent*>(event);
            
            // PERFORMANCE: Check device first (cheapest check)
            if (buttonEvent->device != RE::INPUT_DEVICE::kKeyboard) {
                continue;
            }
            
            // Escape with the panel open: the panel may not get the key itself
            if (buttonEvent->idCode == kEscapeKeyCode && m_hotkeyCode != kEscapeKeyCode) {
                if (buttonEvent->IsDown() && UIManager::GetSingleton()->IsPanelVisible()) {
                    UIManager::GetSingleton()->OnEscapePressed();
                }
                continue;
            }

            // PERFORMANCE: Check key code before IsDown (idCode is a simple member access).
            // Hotkey "None" is code 0, which no key has, so it never matches.
            // While Risa owns the panel our key does nothing, closing included: Risa may bind the same key
            if (buttonEvent->idCode != m_hotkeyCode || !RisaMenu::NativeHotkeyEnabled()) {
                continue;
            }
            
            // Only process key down events
            if (!buttonEvent->IsDown()) {
                continue;
            }

            // Typing in the console or a text field (naming an item, a save, Risa's search)
            // is not the hotkey: opening the panel then swallowed the key and the text.
            // Only for opening - with the panel open the key still closes it, whatever
            // text input the panel's own view takes
            if (!UIManager::GetSingleton()->IsPanelVisible() && IsTypingText()) {
                continue;
            }

            // Matched! Toggle the panel
            logger::info("Hotkey {} pressed - toggling Spell Learning Panel", m_hotkeyCode);
            UIManager::GetSingleton()->TogglePanel();
            break;  // Only process one hotkey event per frame to prevent double-toggle
        }

        return RE::BSEventNotifyControl::kContinue;
    }

private:
    static constexpr std::uint32_t kEscapeKeyCode = 1;  // DirectInput scan code of Escape

    // The console is open, or a menu takes text (ControlMap's text entry count)
    static bool IsTypingText()
    {
        if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::Console::MENU_NAME)) return true;
        auto* controlMap = RE::ControlMap::GetSingleton();
        return controlMap && controlMap->GetRuntimeData().textEntryCount > 0;
    }

    InputHandler() : m_hotkeyCode(66) {}  // Default F8 = 66
    ~InputHandler() = default;
    InputHandler(const InputHandler&) = delete;
    InputHandler& operator=(const InputHandler&) = delete;
    
    uint32_t m_hotkeyCode;
};

// Global function to update hotkey (called from UIManager)
void UpdateInputHandlerHotkey(uint32_t keyCode)
{
    InputHandler::GetSingleton()->SetHotkeyCode(keyCode);
    RisaMenu::OnHotkeyChanged(keyCode);
}

// =============================================================================
// SKSE MESSAGE HANDLER
// =============================================================================

void OnDataLoaded()
{
    logger::info("Data loaded (main menu) - initializing systems");
    
    // Initialize UI Manager (connects to PrismaUI)
    if (UIManager::GetSingleton()->Initialize()) {
        logger::info("UIManager initialized successfully");
        
        // Reset tree states on main menu load - all spells start locked
        UIManager::GetSingleton()->NotifyMainMenuLoaded();
        // The build at game start, held during play, goes on at the main menu (autoBuild.js)
        UIManager::GetSingleton()->WatchMainMenu();
    } else {
        logger::error("Failed to initialize UIManager - PrismaUI may not be loaded");
    }

    // Register input handler for hotkey
    InputHandler::GetSingleton()->Register();
    
    // Register spell cast event handler for XP tracking
    SpellCastHandler::GetSingleton()->Register();
    logger::info("SpellCastHandler registered for XP tracking");

    // Keeps the per-cast tome inventory check cached until the inventory changes
    SpellTomeHook::GetSingleton()->RegisterInventoryEvents();
    
    // Initialize ISL/DEST integration (detects DEST_ISL.esp and enables event dispatch)
    DESTIntegration::Initialize();
    
    // Register and initialize XP sources
    auto& registry = SpellLearning::XPSourceRegistry::GetSingleton();
    registry.Register<SpellLearning::SpellCastXPSource>();
    registry.Register<SpellLearning::PassiveLearningSource>();
    registry.InitializeAll();
    logger::info("XP sources registered: {} total, {} active",
                 registry.GetAll().size(), registry.GetActive().size());

    // Perk overhauls see the modded spells the catalog knows (keywords added
    // in memory, from the catalog the last scan left)
    Librarian::PerkAdapters::Apply(Librarian::PerkAdapters::kDataLoadedReason);

    // HeartOfMagic.esp's explosion twins: hidden harmless effects off an explosion
    // Frostwalker reads as frost by name (docs/ENVIRONMENT.md)
    Environment::Initialize();

    // Spell Research learns the catalog spells after its Import (docs/librarian/SPELL_RESEARCH.md)
    Librarian::SpellResearchPatch::Install();

    // Tomes that teach nothing (Spell Research Synthesizer) and the spell each
    // stands for, before the tome reach pass takes the tomes (SpellTomes.h)
    SpellTomes::Rebuild("data loaded");

    // Which tomes something hands out: read from the plugin files on a worker
    // thread, ready for the scan (docs/ARCHITECTURE.md, "Tomes nothing hands out")
    TomeReach::StartAfterDataLoaded();

    // One spell from two plugins: the groups a copy's tome reads by, worked out
    // on the game thread once that pass is done, never on a cast's tome check
    // (SpellScannerDuplicates.cpp).
    SpellScanner::PrepareTreeSpellForTomeAfterPass();
}

void OnNewGame()
{
    logger::info("New game started - progression will be cleared");
    Librarian::SpellResearchPatch::OnNewGame();
    Librarian::PerkAdapters::Reassert("new game");
    Environment::SelfTest::OnGameLoaded();   // dev only: selftest.json
    // Progress is automatically cleared by OnRevert callback

    // Fix input lock if UI was open in main menu
    if (UIManager::GetSingleton()->IsInitialized()) {
        UIManager::GetSingleton()->EnsureFocusReleased();
        // The build at game start holds its next step until the panel opens (autoBuild.js)
        UIManager::GetSingleton()->NotifyNewGameStarted();
    }

    // Reset passive learning timer
    auto* passive = SpellLearning::PassiveLearningSource::GetSingleton();
    if (passive) passive->OnGameLoad();
}

void OnPostLoadGame()
{
    logger::info("Save game loaded - notifying UI to refresh player data");
    // Progress is automatically loaded by OnGameLoaded serialization callback

    // The inventory is the loaded save's now, whatever was cached before
    SpellTomeHook::GetSingleton()->InvalidateTomeInventoryCache();

    // Spell Research's lists are the loaded save's: their tome pairs first (the
    // registrations below read the tomes), then register what they lack
    SpellTomes::Rebuild(SpellTomes::kReasonSaveLoaded);
    Librarian::SpellResearchPatch::OnGameLoaded();
    Librarian::PerkAdapters::Reassert("save loaded");
    Environment::SelfTest::OnGameLoaded();   // dev only: selftest.json

    // Fix input/focus state that may be left bad by other mods or previous session
    if (UIManager::GetSingleton()->IsInitialized()) {
        UIManager::GetSingleton()->EnsureFocusReleased();
    }

    // Reset passive learning timer for loaded save
    auto* passive = SpellLearning::PassiveLearningSource::GetSingleton();
    if (passive) passive->OnGameLoad();

    // Re-register ISL aliases every save load.
    // OnInit() only fires once per save creation (not on load), so on fresh
    // game launch → existing save, g_spellTomeEventRegs is empty.
    // AutoRegisterISLAliases scans DEST_ISL quests for player aliases and
    // registers them so spell tome read events dispatch correctly.
    DESTIntegration::AutoRegisterISLAliases();

    // Notify UI to refresh - this will:
    // 1. Reset tree states to locked/available
    // 2. Request fresh progress data from co-save
    // 3. Check which spells the player knows
    if (UIManager::GetSingleton()->IsInitialized()) {
        UIManager::GetSingleton()->NotifySaveGameLoaded();
    }
}

void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
{
    // Game thread work for IsOnGameThread() while it is handled (GameThreadFlag.h)
    const GameThreadFlag::Scope gameThreadWork;
    SampleThread(ThreadSampleKind::kMessage);

    switch (a_msg->type) {
        case SKSE::MessagingInterface::kPostLoad:
            // Install hooks after all plugins are loaded but before game data
            SpellEffectivenessHook::Install();
            SpellEffectivenessHook::InstallDisplayHooks();
            // Casts that send no cast event (SpellHotbar 2, Spell.Cast) still give XP
            SpellCastHandler::InstallImmediateCastHook();

            // Install spell tome hook (intercepts book reading)
            if (SpellTomeHook::Install()) {
                logger::info("SpellTomeHook installed - spell tome interception active");
            } else {
                logger::error("SpellTomeHook failed to install - spell tomes will use vanilla behavior");
            }
            RisaMenu::Register(InputHandler::GetSingleton()->GetHotkeyCode());   // Risa's All In One Menu button
            break;
        case SKSE::MessagingInterface::kPostPostLoad: {
            // Broadcast API to all listening plugins
            // Consumers register with: messaging->RegisterListener("SpellLearning", callback)
            auto* api = SpellLearningAPIImpl::GetSingleton();
            SKSE::GetMessagingInterface()->Dispatch(
                SpellLearning::kMessageType_APIReady,
                api, sizeof(void*), nullptr);
            logger::info("SpellLearning: API broadcasted to all listeners (v{})", api->GetAPIVersion());
            break;
        }
        case SKSE::MessagingInterface::kDataLoaded:
            OnDataLoaded();
            break;
        case SKSE::MessagingInterface::kNewGame:
            OnNewGame();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            OnPostLoadGame();
            break;
    }
    // Loading steps are rare and worth having on disk (info lines are buffered)
    FlushLog();
}

// =============================================================================
// SKSE PLUGIN LOAD
// =============================================================================

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse, false);
    SetupLog();

    // Register messaging interface
    auto messaging = SKSE::GetMessagingInterface();
    if (!messaging->RegisterListener(MessageHandler)) {
        logger::error("Failed to register messaging listener");
        return false;
    }

    logger::info("SpellLearning: API will be broadcasted at kPostPostLoad for addon plugins");
    
    // Register serialization interface (co-save)
    auto serialization = SKSE::GetSerializationInterface();
    if (serialization) {
        serialization->SetUniqueID(CoSave::kUniqueID);
        serialization->SetSaveCallback(CoSave::OnGameSaved);
        serialization->SetLoadCallback(CoSave::OnGameLoaded);
        serialization->SetRevertCallback(CoSave::OnRevert);
        logger::info("SKSE Serialization interface registered (co-save enabled)");
    } else {
        logger::error("Failed to get SKSE serialization interface - co-save disabled!");
    }
    
    // Register Papyrus API functions for other mods to call
    auto papyrus = SKSE::GetPapyrusInterface();
    if (papyrus) {
        papyrus->Register(PapyrusAPI::RegisterFunctions);
        logger::info("Papyrus API registered - other mods can call SpellLearning.OpenMenu() etc.");
        
        // Register DEST_AliasExt native functions (ISL compatibility)
        // This provides the same API that DontEatSpellTomes.dll exposes,
        // allowing ISL's unmodified Papyrus scripts to work with our hook.
        papyrus->Register(DESTIntegration::RegisterDESTAliasExtFunctions);
        papyrus->Register(DESTIntegration::RegisterPapyrusFunctions);
        logger::info("DEST/ISL Papyrus API registered - ISL compatibility active");
    } else {
        logger::error("Failed to get Papyrus interface - API functions unavailable");
    }

    logger::info("{} loaded successfully", SKSE::GetPluginName());
    return true;
}
