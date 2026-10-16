#pragma once

#include "Common.h"
#include "ProgressionCoSave.h"
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <filesystem>

// =============================================================================
// ProgressionManager — Spell learning progress and XP tracking
//
// THREADING INVARIANT: All public methods MUST be called from the game thread.
// This class has no internal synchronization (no mutex). Thread safety is
// guaranteed by the fact that all callers dispatch through AddTaskToGameThread():
//   - SpellCastHandler event sink (game thread inherently)
//   - SpellTomeHook (game thread inherently)
//   - UIManager callbacks (deferred via AddTaskToGameThread)
//   - PassiveLearningSource (dispatches XP grants via AddTaskToGameThread)
//   - SKSE serialization callbacks (game thread)
//   - PapyrusAPI native functions (VM thread = game thread)
//   - BookXP addon via API (MenuOpenCloseEvent = game thread)
//
// DO NOT call ProgressionManager methods from background threads directly.
// Use AddTaskToGameThread() to marshal calls to the game thread.
// =============================================================================
class ProgressionManager
{
public:
    struct SpellProgress {
        float progressPercent = 0.0f;  // 0.0 to 1.0 (percentage stored in co-save)
        float requiredXP = 100.0f;     // Loaded from tree data at runtime
        bool unlocked = false;

        // XP from each source (for cap tracking)
        float xpFromAny = 0.0f;        // XP gained from any-spell casts
        float xpFromSchool = 0.0f;     // XP gained from same-school casts
        float xpFromDirect = 0.0f;     // XP gained from direct prereq casts
        float xpFromSelf = 0.0f;       // XP gained from self-casting

        // XP from modded sources (for per-source cap tracking)
        std::unordered_map<std::string, float> xpFromModded;  // source name -> tracked XP

        // Computed property
        float GetCurrentXP() const { return progressPercent * requiredXP; }
        float GetTotalTrackedXP() const {
            float total = xpFromAny + xpFromSchool + xpFromDirect + xpFromSelf;
            for (const auto& [name, xp] : xpFromModded) total += xp;
            return total;
        }
    };

    static ProgressionManager* GetSingleton();

    // Learning targets (one per school)
    void SetLearningTarget(const std::string& school, RE::FormID formId, const std::vector<RE::FormID>& prereqs = {});
    RE::FormID GetLearningTarget(const std::string& school) const;
    // Clears the school's target, and that spell under any other school key too
    void ClearLearningTarget(const std::string& school);
    void ClearLearningTargetForSpell(RE::FormID formId);  // Clear every school targeting this spell (mastered, unlocked, relocked)
    // The school key a spell is a learning target under: the tree node's school
    // (its first effect, SpellScanner::GetSpellSchool), which the panel keys by too;
    // the costliest effect's school for a spell whose first effect has none.
    // "" when neither is one of the five magic schools.
    static std::string TargetSchoolOf(RE::SpellItem* spell);
    
    // Direct prerequisite checking (for XP bonuses)
    bool IsDirectPrerequisite(RE::FormID targetSpellId, RE::FormID castSpellId) const;
    // True when the cast spell lists the target as a prerequisite: the spell above
    // it that opened it, for a spell learned downward (see XPSettings::reverseUnlock)
    bool IsDirectChild(RE::FormID targetSpellId, RE::FormID castSpellId) const;
    void SetTargetPrerequisites(RE::FormID targetSpellId, const std::vector<RE::FormID>& prereqs);
    
    // Tree prerequisites - unified hard/soft system
    // Hard prereqs: ALL must be mastered
    // Soft prereqs: at least softNeeded must be mastered
    struct PrereqRequirements {
        std::vector<RE::FormID> hardPrereqs;   // Must have ALL of these
        std::vector<RE::FormID> softPrereqs;   // Must have X of these (where X = softNeeded)
        int softNeeded = 0;                     // How many soft prereqs required
    };
    
    void SetPrereqRequirements(RE::FormID spellId, const PrereqRequirements& reqs);
    void ClearAllTreePrerequisites();  // Called when tree reloads
    // Whether the tree the panel sent (SetPrereqRequirements, roots included) has
    // this spell as a node. Empty until the panel sends its tree.
    bool IsTreeSpell(RE::FormID spellId) const;
    PrereqRequirements GetPrereqRequirements(RE::FormID spellId) const;
    bool AreTreePrerequisitesMet(RE::FormID spellId) const;
    // True when XPSettings::reverseUnlock is on and a spell that lists this one as
    // a prerequisite (hard or soft) is mastered - or, with reverseUnlockToRoot,
    // any spell further up that chain
    bool IsUnlockedByKnownChild(RE::FormID spellId) const;
    std::vector<RE::FormID> GetUnmetHardPrerequisites(RE::FormID spellId) const;
    std::pair<int, int> GetSoftPrerequisiteStatus(RE::FormID spellId) const;  // (mastered, needed)
    bool IsSpellMastered(RE::FormID spellId) const;  // 100% progress or explicitly unlocked
    // Another plugin's copy of this spell (SpellScanner::DuplicateGroupOf) that the
    // player knows, learned before the copies were merged or from outside the tree:
    // it is this spell to the tree (IsSpellMastered), and this one is not granted on
    // top of it (CanUnlock, the early grant, the tome's "already know").
    bool PlayerKnowsCopyOf(RE::FormID spellId) const;
    
    // Legacy compatibility
    void SetTreePrerequisites(RE::FormID spellId, const std::vector<RE::FormID>& prereqs);
    std::vector<RE::FormID> GetTreePrerequisites(RE::FormID spellId) const;

    // XP tracking
    // a_path: which way the cast was seen ("cast event", "cast hook"), for the XP log
    void OnSpellCast(const std::string& school, RE::FormID castSpellId, float baseXP, std::string_view a_path);
    // One line per XP grant: amount, spell, where it came from (cast event,
    // cast hook, tome, study, source <name>, raw) and what earned it. Info level,
    // or debug for a cast's per-target lines (OnSpellCast logs one info line per cast)
    static void LogXPGrant(std::string_view a_path, RE::FormID targetId, float amount, std::string_view a_detail = {},
                           bool a_debugLevel = false);
    void AddXP(RE::FormID targetSpellId, float amount);
    void AddXP(const std::string& formIdStr, float amount);  // String overload for DEST integration
    // ISL study XP: recorded without the early grant (ISL teaches the spell itself),
    // all study XP of a spell capped at its unlock-threshold share (tracked under
    // kStudySource in xpFromModded, so the co-save keeps it), and never taking the
    // spell to 1.0 (ProgressionMath::NoGrantProgress). Returns the XP added.
    float AddXPNoGrant(const std::string& formIdStr, float amount);
    static constexpr const char* kStudySource = "isl_study";
    SpellProgress GetProgress(RE::FormID formId) const;
    // Just the progress (0..1, 0 if none): no copy of the whole SpellProgress,
    // whose modded-source map allocates. For the per-effect and per-cast paths.
    float GetProgressPercent(RE::FormID formId) const;
    void SetRequiredXP(RE::FormID formId, float required);
    
    // Get required XP for a spell (from progress data or tier default)
    float GetRequiredXP(const std::string& formIdStr) const;
    float GetRequiredXP(RE::FormID formId) const;
    
    // Set learning target from tome reading (auto-determines school)
    void SetLearningTargetFromTome(const std::string& formIdStr, RE::SpellItem* spell);

    // Spell unlocking
    bool CanUnlock(RE::FormID formId) const;
    bool UnlockSpell(RE::FormID formId);
    // Cheat-mode Relock: the spell's progress goes back to nothing (not unlocked,
    // no XP), so a reload or the next progress reply does not unlock it again.
    // An early-learned spell gets its own name and descriptions back and leaves
    // the early-learned set; a learning target on it is cleared, and its tome
    // gives its XP again (SpellTomeHook::UnmarkTomeXPGranted). The caller
    // takes the spell from the player. Game thread only.
    void RelockSpell(RE::FormID formId);
    bool IsUnlocked(RE::FormID formId) const;
    
    // Check if a spell is available to learn (has progress entry, not yet unlocked)
    bool IsSpellAvailableToLearn(const std::string& formIdStr) const;
    bool IsSpellAvailableToLearn(RE::FormID formId) const;

    // =========================================================================
    // SKSE CO-SAVE SERIALIZATION
    // =========================================================================
    // SLTR is written as version 2 (versions 1 and 2 have the same layout). SLPR's
    // and SLPX's versions and layouts are in ProgressionCoSave.h: SLPR stays v2 as
    // 1.5.0 writes it, the built-in cap trackers go in SLPX, which older builds skip.
    static constexpr uint32_t kTargetsVersion = 2;
    static constexpr uint32_t kProgressRecord = 'SLPR';     // Spell Learning Progress Record
    static constexpr uint32_t kTargetsRecord = 'SLTR';      // Spell Learning Targets Record
    static constexpr uint32_t kCapTrackersRecord = 'SLPX';  // Spell Learning Progress XP by source
    
    // Called by SKSE serialization callbacks
    void OnGameSaved(SKSE::SerializationInterface* a_intfc);
    void OnRevert(SKSE::SerializationInterface* a_intfc);

    // Co-save reading. There is one record stream and more than one owner, so
    // nobody may run their own GetNextRecordInfo loop - the first to do it
    // drains the stream and the next owner silently gets nothing. Main.cpp runs
    // the single loop and offers each record here; this returns true when the
    // record was ours, false to let the next owner see it.
    void BeginLoad();
    bool ReadRecord(SKSE::SerializationInterface* a_intfc, uint32_t type, uint32_t version, uint32_t length);
    void EndLoad();

    // Legacy save/load (for external JSON files - kept for backwards compat)
    void LoadProgress(const std::string& saveName);
    void SaveProgress();
    void SetCurrentSave(const std::string& saveName);
    std::string GetCurrentSave() const { return m_currentSaveName; }

    // Get all progress data for UI
    std::string GetProgressJSON() const;
    
    // Clear all progress (called on new game/revert)
    void ClearAllProgress();
    
    // Modded XP source configuration (per-source balancing)
    struct ModdedSourceConfig {
        std::string displayName;    // e.g. "Combat Training"
        bool enabled = true;
        float multiplier = 100.0f;  // 0-200%
        float cap = 25.0f;          // 0-100% of required XP
        bool internal = false;      // Internal sources: tracked, not capped here, not shown in modded UI
    };

    // How fast XP comes in: the overall multiplier, and a multiplier and a cap
    // (max % of the required XP) per built-in source
    struct XPGainRates {
        float globalMultiplier = 1.0f;
        float multiplierDirect = 1.0f;
        float multiplierSchool = 0.5f;
        float multiplierAny = 0.1f;
        float capAny = 5.0f;
        float capSchool = 15.0f;
        float capDirect = 50.0f;
    };

    // XP Settings (loaded from unified config)
    struct XPSettings {
        std::string learningMode = "perSchool";  // "perSchool" or "single"
        float globalMultiplier = 1.0f;   // Direct multiplier (1.0 = normal, 2.0 = double XP)
        float multiplierDirect = 1.0f;   // 0.0-1.0 for direct prerequisite spells
        float multiplierSchool = 0.5f;   // 0.0-1.0 for same school spells
        float multiplierAny = 0.1f;      // 0.0-1.0 for any spell
        // XP caps (max % contribution from each source, 0-100)
        float capAny = 5.0f;             // Max 5% from any spell casts
        float capSchool = 15.0f;         // Max 15% from same-school casts
        float capDirect = 50.0f;         // Max 50% from direct prereq casts
        // Tier XP requirements
        float xpNovice = 100.0f;
        float xpApprentice = 200.0f;
        float xpAdept = 400.0f;
        float xpExpert = 800.0f;
        float xpMaster = 1500.0f;
        // A spell the player already knows opens its direct prerequisites - or,
        // with reverseUnlockToRoot, every spell below it down to the root: they
        // become learnable whatever their own prerequisites (IsUnlockedByKnownChild),
        // and cost a share of their XP set per tier of the opened spell
        // (GetReverseUnlockXPShare). Config "reverseUnlock", "reverseUnlockToRoot",
        // "reverseUnlockXPNovice" ... "reverseUnlockXPMaster".
        bool reverseUnlock = true;
        bool reverseUnlockToRoot = false;
        float reverseUnlockXPNovice = 0.3f;
        float reverseUnlockXPApprentice = 0.4f;
        float reverseUnlockXPAdept = 0.5f;
        float reverseUnlockXPExpert = 0.7f;
        float reverseUnlockXPMaster = 0.8f;
        // With reverseXPSeparate on, spells learned downward gain XP at their own
        // rates (reverseGain) instead of the ones above (GetGainRates). Config
        // "reverseXpSeparate", "reverseXpGlobalMultiplier", "reverseXpMultiplier*",
        // "reverseXpCap*".
        bool reverseXPSeparate = false;
        XPGainRates reverseGain;
        // Modded XP sources (registered by external mods)
        std::unordered_map<std::string, ModdedSourceConfig> moddedSources;
    };
    
    void SetXPSettings(const XPSettings& settings);
    const XPSettings& GetXPSettings() const { return m_xpSettings; }
    XPSettings& GetXPSettingsMutable() { return m_xpSettings; }
    float GetXPForTier(const std::string& tier) const;
    // Share of its XP an opened spell of this tier costs (see XPSettings::reverseUnlock)
    float GetReverseUnlockXPShare(const std::string& tier) const;
    // The XP gain rates for a learning target: reverseGain for a spell learned
    // downward with reverseXPSeparate on, the usual ones otherwise
    XPGainRates GetGainRates(RE::FormID targetId) const;

    // Direct XP manipulation (cheat mode, API, Papyrus): clamped to 0..required XP
    void SetSpellXP(RE::FormID formId, float xp);

    // =========================================================================
    // PUBLIC MODDER API
    // =========================================================================

    // Grant XP through the cap system with named source (other mods: C++ API, Papyrus).
    // Built-in sources: "any", "school", "direct", "self"
    // Custom sources: any string (auto-registers if unknown). Refused (0, logged
    // once): the plugin's own internal source names (IsReservedSourceName), a name
    // over ProgressionCoSave::kMaxNameLength bytes, and a new name once a spell or
    // the registry holds kMaxModdedSources - the co-save could not read it back.
    // Returns actual XP granted after caps/multipliers.
    float AddSourcedXP(RE::FormID targetId, float amount, const std::string& sourceName = "direct");

    // The plugin's own internal sources (passive learning): tracked per spell, not
    // cut by the modded cap or multiplier - the source applies its own.
    float AddInternalSourcedXP(RE::FormID targetId, float amount, const std::string& sourceName);

    // Grant raw XP bypassing ALL caps and multipliers.
    float AddRawXP(RE::FormID targetId, float amount);

    // Register a named modded XP source (creates UI controls).
    // Returns true if newly registered, false if already existed or refused (the
    // same names AddSourcedXP refuses, for an external source).
    // Internal sources (passive learning) don't appear in the modded XP sources UI;
    // AddInternalSourcedXP tracks their XP per spell but does not cut it with the
    // modded cap or multiplier - the source applies its own (passive: a cap per tier).
    bool RegisterModdedXPSource(const std::string& sourceId, const std::string& displayName, bool internal = false);

    // Names only the plugin uses: kPassiveSource, kStudySource, any source
    // registered as internal. Other mods may not grant XP or register under them.
    static constexpr const char* kPassiveSource = "passive";
    bool IsReservedSourceName(const std::string& sourceName) const;

    // Get the cap value for a source (works for built-in and modded)
    float GetSourceCap(const std::string& sourceName) const;

    // Send a ModEvent to Papyrus listeners
    static void SendModEvent(const char* eventName, const std::string& strArg, float numArg, RE::TESForm* sender = nullptr);

private:
    ProgressionManager() = default;
    ~ProgressionManager() = default;
    ProgressionManager(const ProgressionManager&) = delete;
    ProgressionManager& operator=(const ProgressionManager&) = delete;

    std::filesystem::path GetProgressFilePath() const;

    // The spell's progress entry, made on first use with the spell's own required
    // XP (GetRequiredXP: tier, reverse unlock share) - not the struct's 100, which
    // made a Master tome's read worth 100% of an entry nobody had sized yet.
    SpellProgress& EnsureProgress(RE::FormID formId);

    // AddSourcedXP / AddInternalSourcedXP: internalCaller lets the internal names through
    float AddSourcedXPImpl(RE::FormID targetId, float amount, const std::string& sourceName, bool internalCaller);
    // An external source name the co-save can carry (length) and not reserved; logs once
    bool AcceptExternalSourceName(const std::string& sourceName) const;

    // One spell is the learning target of one school at most: a spell under two keys
    // earned XP twice per cast (ProgressionManagerCore.cpp). Every key dropped is
    // told to the panel (UIManager::NotifyLearningTargetDropped) and fires
    // SpellLearning_TargetChanged with 0 for that key
    void DropTargetFromOtherSchools(const std::string& keepSchool, RE::FormID formId);
    // At save load: each target under one key, its TargetSchoolOf key unless a target
    // staying put holds it (a save from before kept a tome's target under the costliest
    // effect's school, which the panel could neither see nor clear). Order-independent
    // (collect, then place); every key changed is told to the panel and fires
    // SpellLearning_TargetChanged (0 for a key cleared, 1 for a key set)
    void CanonicalizeLearningTargets();
    // The school a ModEvent about this spell names: the key it is a target under,
    // else TargetSchoolOf, else "Unknown"
    std::string EventSchoolOf(RE::FormID formId) const;

    // Learning targets: school name -> spell formId
    std::unordered_map<std::string, RE::FormID> m_learningTargets;
    
    // Direct prerequisites: target spell formId -> list of prereq formIds (for XP bonuses)
    std::unordered_map<RE::FormID, std::vector<RE::FormID>> m_targetPrerequisites;
    
    // Tree prerequisites: spell formId -> hard/soft prereq requirements
    std::unordered_map<RE::FormID, PrereqRequirements> m_prereqRequirements;

    // Every node of that tree, prerequisites or not (IsTreeSpell)
    std::unordered_set<RE::FormID> m_treeSpells;

    // The same links the other way: spell formId -> spells that list it as a hard
    // or soft prerequisite (IsUnlockedByKnownChild), so the per-cast checks look
    // children up instead of scanning every spell in the tree. Kept in step by
    // SetPrereqRequirements / ClearAllTreePrerequisites, never rebuilt on a read.
    const std::vector<RE::FormID>& GetRequiredBy(RE::FormID spellId) const;
    void LinkRequiredBy(RE::FormID spellId, const PrereqRequirements& reqs, bool link);
    std::unordered_map<RE::FormID, std::vector<RE::FormID>> m_requiredBy;

    bool IsOwnSpellMastered(RE::FormID spellId) const;  // IsSpellMastered without the copies

    // Progress data: spell formId -> progress
    std::unordered_map<RE::FormID, SpellProgress> m_spellProgress;

    // SLPX entries read during a load (form ids resolved), put onto m_spellProgress
    // in EndLoad - after every SLPR entry is in, whatever order the records came in
    std::vector<ProgressionCoSave::CapEntry> m_pendingCapTrackers;
    bool ReadCapTrackers(SKSE::SerializationInterface* a_intfc, uint32_t length);
    void WriteCapTrackers(SKSE::SerializationInterface* a_intfc) const;

    // Current save name for file naming
    std::string m_currentSaveName = "default";

    // Dirty flag for save optimization
    bool m_dirty = false;
    
    // XP Settings
    XPSettings m_xpSettings;
};
