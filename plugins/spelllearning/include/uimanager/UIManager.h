#pragma once

#include "Common.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "PrismaUI_API.h"
#include "treebuilder/TreeBuilder.h"
#include "uimanager/ConfigFile.h"

class UIManager
{
public:
    static UIManager* GetSingleton();

    // Initialize PrismaUI connection
    bool Initialize();

    // Panel visibility (single panel with tabs)
    void TogglePanel();
    void ShowPanel();
    void HidePanel();
    bool IsPanelVisible() const { return m_isPanelVisible; }

    // Escape pressed while the panel is open: the panel decides (drop the
    // selected spell, else close). The game's input reaches this whether or
    // not the key also gets through to the view, which it does not always.
    void OnEscapePressed();
    bool IsInitialized() const { return m_isInitialized; }
    
    // Settings
    void SetPauseGameOnFocus(bool pause) { m_pauseGameOnFocus = pause; }

    // Developer mode, from the config. The panel's ordinary console lines
    // reach SpellLearning.log only while it is on; warnings and errors always
    // do. Off by default, so a player never pays for hundreds of lines per
    // panel load they will not read.
    static void SetPanelInfoLogging(bool enabled) { s_panelInfoLogging.store(enabled, std::memory_order_relaxed); }
    static bool PanelInfoLogging() { return s_panelInfoLogging.load(std::memory_order_relaxed); }
    bool GetPauseGameOnFocus() const { return m_pauseGameOnFocus; }

    // How the panel's saves treat config.json, from the last load
    // (ConfigFile::SaveModeFor). Set and read under the config file lock:
    // the load on the game thread, the saves on their worker.
    static void SetConfigSaveMode(ConfigFile::SaveMode mode) { s_configSaveMode.store(mode, std::memory_order_relaxed); }
    static ConfigFile::SaveMode ConfigSaveMode() { return s_configSaveMode.load(std::memory_order_relaxed); }

    // Send data to Scanner Tab
    void SendSpellData(const std::string& jsonData);
    void UpdateStatus(const std::string& message);
    void SendPrompt(const std::string& promptContent);

    // Send data to Tree Tab (same panel)
    void SendTreeData(const std::string& jsonData);
    void SendSpellInfo(const std::string& jsonData);
    void SendSpellInfoBatch(const std::string& jsonData);
    void UpdateSpellState(const std::string& formId, const std::string& state);
    void UpdateTreeStatus(const std::string& message);
    void SendDeclutterResult(const std::string& jsonData);  // onDeclutterResult (UIManagerDeclutter.cpp)

    // Clipboard support
    void SendClipboardContent(const std::string& content);

    // Progression system notifications
    void NotifyProgressUpdate(RE::FormID formId, float currentXP, float requiredXP);
    void NotifyProgressUpdate(const std::string& formIdStr);  // Triggers UI refresh for spell
    void NotifySpellReady(RE::FormID formId);
    void NotifySpellUnlocked(RE::FormID formId, bool success);
    void NotifyLearningTargetSet(const std::string& school, RE::FormID formId, const std::string& spellName);
    void NotifyLearningTargetCleared(RE::FormID formId);  // When a learning target is switched/cleared
    // A school key no longer holds this spell (it stays the target under another
    // key, or was cleared there too): the panel drops its copy of that key
    void NotifyLearningTargetDropped(const std::string& school, RE::FormID formId);
    
    // Game state notifications
    void NotifyMainMenuLoaded();      // Called on kDataLoaded - reset tree states
    void NotifySaveGameLoaded();      // Called on kPostLoadGame - refresh player data
    void SendProgressData(const std::string& jsonData);
    
    // Focus management for main menu → game transition
    void EnsureFocusReleased();       // Call on game load to fix input lock
    
    // Modded XP source notifications
    void NotifyModdedSourceRegistered(const std::string& sourceId, const std::string& displayName,
                                       float multiplier, float cap);

    // Spell Research registrations (SpellResearchPatch::StatusJson) to the panel:
    // when it opens and whenever a run changed them. Nothing when SR has no status.
    void NotifySpellResearchStatus();

    // The tree built at game start (UIManagerAutoBuild.cpp; docs/ARCHITECTURE.md,
    // "Tree built at game start"). Game thread.
    enum class StartupTreeState { Missing, Empty, Present, Unreadable };
    // The saved tree's state and the load order fingerprints to the panel
    // (onStartupTreeInfo), at the end of every LoadSpellTree
    void SendStartupTreeInfo(StartupTreeState state, std::size_t nodeCount) noexcept;
    // The load order spell_tree.json was just written under (spell_tree_loadorder.json)
    void RecordTreeLoadOrder();
    // The load order notice the panel asked for, in the corner: kPostLoadGame / kNewGame
    void ShowPendingLoadOrderNotice();
    // Spells in a saved tree's JSON (every school's nodes), before any validation
    [[nodiscard]] static std::size_t CountTreeNodes(const nlohmann::json& tree);

    // Get the PrismaUI API (for SpellScanner to use)
    //PRISMA_UI_API::IVPrismaUI1* GetAPI() const { return m_prismaUI; }
    //PrismaView GetView() const { return m_view; }

    // File paths
    static std::filesystem::path GetPromptFilePath();
    static std::filesystem::path GetTreeFilePath();
    static std::filesystem::path GetTreeLoadOrderFilePath();

private:
    UIManager() = default;
    ~UIManager() = default;
    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    // PrismaUI callbacks - All on single view
    static void OnDomReady(PrismaView view);
    
    // Scanner tab callbacks
    // A scan (UIManagerScanner.cpp): the form reads in one game thread task
    // (CollectScan), the text work on a worker thread (PrepareScanText), the form
    // writes and the panel in a second game thread task (FinishScan). One at a
    // time; a request meanwhile waits (one per mode) and EndScan starts it. The
    // steps pass a ScanTicket along; letting go of its last copy ends the scan.
    struct ScanTicket;
    static void OnScanSpells(const char* argument);
    void RequestScan(std::string argStr);
    // false when it could not start (logged; the running flag is cleared)
    bool LaunchScan(std::string argStr) noexcept;
    void StartScan(std::string argStr, std::shared_ptr<ScanTicket> ticket);
    void CollectScan(const std::string& argStr, std::shared_ptr<ScanTicket> ticket);
    static std::string PrepareScanText(nlohmann::json&& output, bool tomeScan);
    void FinishScan(std::string result, bool tomeScan) noexcept;
    void StoreAndSendFullScan(std::string result);
    void EndScan() noexcept;
    // What the panel is told when a scan throws (onScanFailed); never throws itself
    void ReportScanFailure(const char* what, bool tomeScan) noexcept;
    static void OnSaveOutput(const char* argument);
    static void OnLoadPrompt(const char* argument);
    
    // Tree tab callbacks
    static void OnLoadSpellTree(const char* argument);
    static void OnGetSpellInfo(const char* argument);
    static void OnGetSpellInfoBatch(const char* argument);
    static void OnGetSpellIcon(const char* argument);
    static void OnSaveSpellTree(const char* argument);
    
    // Progression callbacks
    static void OnSetLearningTarget(const char* argument);
    static void OnClearLearningTarget(const char* argument);
    static void OnUnlockSpell(const char* argument);
    static void OnGetProgress(const char* argument);
    static void OnCheatUnlockSpell(const char* argument);
    static void OnRelockSpell(const char* argument);
    static void OnSetSpellXP(const char* argument);
    static void OnSetRequiredXP(const char* argument);
    static void OnGetPlayerKnownSpells(const char* argument);
    static void OnSetPauseGameOnFocus(const char* argument);
    static void OnSetTreePrerequisites(const char* argument);
    
    // Unified config callbacks
    static void OnLoadUnifiedConfig(const char* argument);
    static void OnSaveUnifiedConfig(const char* argument);  // UIManagerConfigSave.cpp

    // Clipboard callbacks
    static void OnGetClipboard(const char* argument);

    // Panel log lines into the plugin log
    static void OnLogMessage(const char* argument);
    // The build at game start's steps, always logged; "notice" queues the corner message
    static void OnAutoBuildEvent(const char* argument);
    
    // Procedural tree generation (C++ native)
    static void OnProceduralTreeGenerate(const char* argument);

    // Pre Req Master NLP scoring (C++ native)
    static void OnPreReqMasterScore(const char* argument);

    // Tree declutter before save (C++ native, UIManagerDeclutter.cpp)
    static void OnDeclutterTree(const char* argument);
    static void OnDeclutterCancel(const char* argument);

    // Panel control callbacks
    static void OnHidePanel(const char* argument);

    // Preset file I/O callbacks
    static void OnSavePreset(const char* argument);
    static void OnDeletePreset(const char* argument);
    static void OnLoadPresets(const char* argument);

    // Console message callback
    static void OnConsoleMessage(PrismaView view, PRISMA_UI_API::ConsoleMessageLevel level, const char* message);

    // Every call into the panel goes through here. Work that finished on a
    // background thread reaches the panel some time later, and by then the
    // view may have been torn down - and InteropCall on a dead view is not
    // something PrismaUI defines. IsValid is cheap; the Notify functions
    // already did this by hand, the completion paths did not.
    bool ViewReady() const;
    void CallView(const char* function, const char* payload);

    // PrismaUI members
    PRISMA_UI_API::IVPrismaUI1* m_prismaUI = nullptr;
    PRISMA_UI_API::IVPrismaUI2* m_prismaUIv2 = nullptr;
    PrismaView m_view = 0;
    std::atomic<bool> m_isPanelVisible{false};  // read by Papyrus off the game thread
    static inline std::atomic<bool> s_panelInfoLogging{false};
    static inline std::atomic<ConfigFile::SaveMode> s_configSaveMode{ConfigFile::SaveMode::Normal};
    bool m_isInitialized = false;
    bool m_hasFocus = false;  // Track if we have focus (for main menu → game fix)
    bool m_pauseGameOnFocus = false;  // Default false to avoid input conflicts with menu mods in heavy modlists
    
    // Config save debouncing - prevent duplicate saves and defer off critical frame
    std::mutex m_configSaveMutex;
    std::chrono::steady_clock::time_point m_lastConfigSaveTime{};
    static constexpr int kConfigSaveDebounceMs = 500;  // Ignore saves within 500ms of each other

    // Background computation guards - prevent concurrent builds/scoring
    std::atomic<bool> m_treeBuildInProgress{false};

    // The last full scan, exactly as sent to the panel, and its number. A tree
    // build names the spells it wants by id and scan number instead of sending
    // the 9-20 MB back; the text is parsed on the build thread, not here.
    // Game thread only.
    std::shared_ptr<const std::string> m_scanText;
    std::uint32_t m_scanId = 0;
    std::atomic<bool> m_prmScoreInProgress{false};

    // Whether a scan is between RequestScan and EndScan, and the requests that
    // came in meanwhile (the latest of each mode). Under m_scanMutex.
    std::mutex m_scanMutex;
    bool m_scanRunning = false;
    std::optional<std::string> m_queuedFullScan;
    std::optional<std::string> m_queuedTomeScan;

    // The panel asked for the load order notice; shown at the next save load
    // or new game (the main menu has no corner messages). Game thread only.
    bool m_loadOrderNoticePending = false;

};
