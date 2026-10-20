#pragma once

// =============================================================================
// OLD COPIES - removes the copies\<key16>\ folders of older generations, once a session (config cleanOldCopies)
// =============================================================================
// A builder identity change gives every object a new key, so the last generation's folders stay behind in Data
// (meshes|textures\heartofmagic\blackflame\copies\<key16>\; under MO2 the overwrite folder) and in staging (the same paths
// under <SKSE log folder>\HeartOfMagic_BlackFlame\staging). The decision is OldCopiesRule.h's.
//   when    a main-thread poll queued once the main menu has opened (Arm), every second while the publish window is open,
//           until: the main-thread proof holds, no other game holds the request folder, the ledger was read whole this
//           session (not rebuilt, not restored from a backup or the mirror), no ledger write this session failed (main or
//           the mirror), publication settled (its final ledger write done in the same drain), the progress worker caught up
//           after the builder ended (BuildProgress::CaughtUp, lock free), progress.jsonl had no refused line, and only then
//           the ledger was written this session. A load (the publish window closes) before that ends the poll for the
//           session: a loaded save's differing slot pairs clear published objects from the ledger while their forms still
//           show them this session, so the keep set is taken only before a load
//   keep    the ledger's objects (published and previous), what the slots were published with this session, and this
//           session's request keys (taken on the main thread); then on the worker: every one of those keys followed through
//           its progress line, written as this ledger epoch's keep set file (OldCopiesKeepsets.h; every ledger write merges
//           the ledger's keys into it, NoteLedgerWritten, and registers the epoch), every keep set file of every epoch (a
//           registered epoch without one, or one that does not read, stops the cleanup), every ledger mirror in the log
//           folder (one that does not read stops it), and every kept key's progress line followed again. The own keep set
//           file is replaced, or merged when a ledger write came after the ledger copy it stands on (Decide counts them).
//           At start (NoteSessionStart) every mirror of another epoch is adopted: registered, its keys merged into its keep set file
//   remove  a detached below-normal thread, at most OldCopiesRule::kMaxFoldersPerSession folders, stopping when a load
//           begins or another game process comes to hold the request folder (both looked at before each key): under each root (the Data or staging folder itself and every folder down to the copies root plain
//           folders, no reparse point), a direct child named by 16 lower-case hex digits that the keep set lacks, a plain
//           folder holding plain files only, whose newest file (Data or staging) is older than oldCopiesGraceDays: its
//           staging folder first, then its Data folder (never a Data folder whose staging folder stayed), files then the
//           folder through std::filesystem - the route BuildProgress writes Data through (the game's own file API, which
//           MO2's VFS sees). Under MO2 (usvfs_x64.dll loaded) a Data folder goes only when every file's real path
//           (GetFinalPathNameByHandleW) is under MO2's overwrite folder, found from a file this session copied into Data;
//           without one, Data is counted only. Never remove_all, never a link followed
//   log     one summary line ("old copies: N folders (X MiB) removed from Data, M (Y MiB) from staging, K kept, ...") and
//           at most 20 failure lines; under MO2 one line with the overwrite folder and the sample it came from, and one with
//           the first Data candidate's real path. cleanOldCopies false: the same counts as a dry run, nothing removed

#include "Common.h"

#include "SlotLedgerModel.h"

namespace BlackFlame::OldCopies
{
    /// Any thread (SingleLaunch's main menu listener, slot route on): queues the main-thread poll, once a session
    void Arm();

    /// SlotLedger::Load, under SlotLedger's lock (it never calls SlotLedger), the epoch final and before this session's
    /// first ledger or mirror write: every ledger mirror of another epoch registered and its keys merged into that epoch's
    /// keep set file (OldCopiesKeepsets.h AdoptMirrors: a profile that ran an older build has neither). Failures logged.
    /// False when a mirror did not read or another epoch was not adopted: SlotLedger then holds the mirror (Codex final)
    [[nodiscard]] bool NoteSessionStart(std::uint64_t a_epoch);

    /// SlotLedger, after main took a write (under SlotLedger's lock: it never calls SlotLedger): a_ledger's keys merged
    /// into its epoch's keep set file and the epoch registered (once a session). A failure is logged, never thrown
    void NoteLedgerWritten(const SlotLedgerModel::Ledger& a_ledger);
}
