#pragma once

// =============================================================================
// SLOT LEDGER - HoMBF_slots.json (PrepareFileSavePath, 5 backups, the mirror, epoch, lastPublished) and the BFSL callbacks
// =============================================================================
// The DLL side of SlotLedgerModel.h / SlotLedgerFiles.h (design v3.1 "Ledger and BFSL", v3.2 §7, v3.3 #5 and #6):
//   path      RE::BSWin32SaveDataSystemUtility::GetSingleton()->PrepareFileSavePath("HoMBF_slots.json", buf[0x104], false,
//             false): the folder the game saves to (MO2 profile-local saves: __MO_Saves). It appends ".ess" (in game, 1.6.1170:
//             "...\__MO_Saves\HoMBF_slots.json.ess"), so only its folder is taken (SlotLedgerFiles::SaveFolderFromPrepared:
//             absolute, file name starting with the name, else no slots this session, logged) and the ledger is
//             <folder>\HoMBF_slots.json. That folder is also where the saves' co-saves are looked for.
//   mirror    <SKSE log folder>\HeartOfMagic_BlackFlame\HoMBF_slots.<8 hex>.json, the hash of the save folder's path (one
//             per save folder, merged only on that folder's epoch)
//   load      kDataLoaded: main, its 5 backups and the mirror merged (the union of reservations, conflicts refused and
//             named). Main not readable, or total loss (none readable): the BFSL of every .skse in the save folder is read
//             with CoSaveFile.h and the union taken (newest first); new assignments are refused this session when any co-save could not be read whole
//   co-save   unique id 'HBFS', record BFSL v1 (SlotCoSave.h). Save: the ledger's pairs now (what the slots are filled
//             from, and the save's pairs after a mismatch). Load: compared with what the forms were filled from this
//             session; differing slots go to the mismatch handler (SlotAssign: gates closed, HUD line), the save's pairs
//             win in the ledger (tombstones; their current sources get new slots next launch), and the ledger is saved
//   preflight kPreLoadGame (config slotPreflight, off: the message's save name format and refilling at that point are
//             unverified in game): the save's co-save read from disk before the engine reads the save; with the key on,
//             differing slots are refilled from the save's sources (data only, no SetModel), else only logged. Absent and
//             corrupt differ, whatever the key says: no co-save, no HBFS block or a block without a BFSL record (a save made
//             before the slot route) is normal; a co-save that is there but does not read, a BFSL record that does not read,
//             or a preflight that throws closes every gate for the session: "this save's slot list could not be read; grey is
//             off for N spells"; a slot a save binds otherwise stays unusable for the session
// The ledger is shared by the kDataLoaded thread, the main-thread drains and the serialization callbacks: under one mutex.

#include "CopyContractKeys.h"
#include "SlotCoSave.h"
#include "SlotLedgerModel.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace BlackFlame::SlotLedger
{
    inline constexpr const char* kLedgerFileName = "HoMBF_slots.json";

    struct Status
    {
        bool available = false;   // a ledger is in use this session (read, rebuilt or new)
        bool totalLoss = false;   // no ledger file, backup or mirror was readable
        bool allowNew = false;    // new slots may be assigned this session (Load sets it once the ledger path is known)
        std::string why;          // why not available, or why new slots are refused
        std::filesystem::path main;
        std::filesystem::path mirror;
        std::filesystem::path saveFolder;
        std::size_t filesRead = 0;        // ledger files read (main, backups, mirror)
        std::size_t coSavesRead = 0;      // total loss: co-saves that walked whole
        std::size_t coSavesWithSlots = 0; // of those, with a BFSL slot list
        std::size_t coSavesBad = 0;       // co-saves that could not be read whole
        std::vector<std::string> conflicts;
        bool restoredFromBackup = false;  // main was missing or refused, a backup or the mirror gave the ledger
        std::size_t saves = 0;            // ledger writes this session that main and the mirror both took (Save)
        std::size_t saveFailures = 0;     // and those that failed (the old-copies cleanup runs only with none, OldCopies.h)
    };

    /// kDataLoaded (singleLaunch): the paths, the files, the total-loss union; the session counter bumped. False: no slots
    bool Load();

    [[nodiscard]] Status GetStatus();

    /// a_work(SlotLedgerModel::Ledger&) under the ledger's lock
    void Edit(const std::function<void(SlotLedgerModel::Ledger&)>& a_work);

    /// A copy of the ledger now
    [[nodiscard]] SlotLedgerModel::Ledger Copy();

    /// The session number this start runs as (after Load)
    [[nodiscard]] std::uint32_t Session() noexcept;

    /// Writes main (backups rolled) and the mirror; the version bumped. a_reason for the log
    bool Save(const char* a_reason);

    /// After the slot forms were filled: what they are filled from (the pairs a loaded save is compared with)
    void SetSessionFill(SlotLedgerModel::Ledger a_fill);

    /// Updates the session fill of one slot after a refill from a save's source
    void NoteRefilled(std::uint16_t a_slot, const SlotCoSave::Slot& a_save);

    enum class MismatchWhen
    {
        kPreflight,  // kPreLoadGame, slotPreflight on: refill before the engine reads the save
        kAfterLoad,  // the BFSL load callback (or a preflight with the key off): the save's refs already resolved
        kUnreadable, // the save's slot list could not be read: every gate closes
    };

    /// SlotAssign's reaction to a loaded save whose pairs differ (gates, refills, the HUD line); set once at kDataLoaded
    void SetMismatchHandler(std::function<void(const std::vector<SlotLedgerModel::Mismatch>&, MismatchWhen)> a_handler);

    /// SKSEPluginLoad (singleLaunch): SetUniqueID('HBFS') and the save, load and revert callbacks
    void RegisterCoSave();

    /// kPreLoadGame: the preflight (always reads and compares; refills only with slotPreflight)
    void OnPreLoadGame(const void* a_data, std::uint32_t a_length);

    struct LoadedSave
    {
        bool seen = false;        // a load callback ran this session
        bool hadRecord = false;   // the save held a BFSL record
        bool read = false;        // it read whole
        std::size_t slots = 0;
        std::size_t mismatches = 0;
        std::string why;
    };

    /// What the last save load's BFSL said (the self-test's BFSL round trip)
    [[nodiscard]] LoadedSave LastLoadedSave();

    /// The BFSL bytes of the ledger now, written and read back in memory (the self-test's in-process round trip)
    [[nodiscard]] bool RoundTripNow(std::string& a_detail);
}
