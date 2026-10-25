#pragma once

// =============================================================================
// FILE INDEX - engine file index: Register (RegisterLoose) and Find (FindRecord), lifted from FileProbeEngine
// =============================================================================
// A NIF written into Data while the game runs is never seen by BSModelDB::Demand until its path is registered in the
// engine's resource index (probe run 3: A-mesh FAIL, C-mesh PASS after RegisterLoose); a DDS loads without it (A-texture
// PASS), and is registered anyway to keep one rule. Facts from Temp File Framework (GPL-3.0, HDharder,
// src/ArchiveBypass.cpp; only its ids, signatures and layouts are used, none of its code):
//   PathToID          68631 / 69971  ID* (ID* out, const char* dataRelativePath)
//   IndexGuard ctor   68321 / 69681  takes the archive index lock and starts a search; dtor 68322 / 69682.
//                                    Engine-sized buffer: 0x200 bytes, 16-aligned; the search state at +0x08
//   FindRecord        68329 / 69689  bool (search, const ID*, Record** out); reads the ArchiveManager static
//                                    523840 / 410404 (a `mov rcx, [rip+disp32]` near its start - checked by scan)
//   RegisterLoose     68327 / 69687  void (guard, const ID*, BSTSmartPointer<Stream>*)
//   Record            +0x0C int32: negative = an archive record, else a loose one
//   Stream count      the engine's own inlined Stream::IncRef in 68483 / 69839 (`lock cmpxchg [rbx+disp8], ecx`):
//                     0x0C on SE and AE before 1.6.1170, 0x10 from 1.6.1170 on; only the value expected for the running
//                     build is accepted (0x10 alone on 1.6.1170 and 1.7.104, whose code was read). Our streams are
//                     counted at the engine's offset, never through CommonLib's BSTSmartPointer (its StreamBase::GetFlags uses 0x10 on every AE build)
//   GlobalLocations   found by its vtable in the game's .data, checked by walking its entry list;
//                     DoCreateStream("data\\" + path, out, where, false) makes the loose stream the engine would make
// Gate (single-launch v3 decision 6): RegisterLoose is used only when every structural check passed - the ids, the
// guard's ctor and dtor shape (FileIndexGuardCheck.h), FindRecord loading the manager, the stream count offset,
// GlobalLocations found, and a FindRecord of a shipped path finding its record. Otherwise Available() is false with the
// reason, and copies written into Data are seen only by the next start's startup index (the next-launch fallback).
// Checked at every start. Lifted from the spike's FileProbeEngine (feature/black-flame-spike-s1 f79ae0c, run 3 on
// 1.6.1170 with CRDW on); the probe's tests stay on that branch.
// Threads: Init and W1's registrations (FormCopies::ApplyLastPublished, CopyGroups' groups opened at kDataLoaded) run on the
// kDataLoaded thread, which on 1.6.1170 under MO2 (run 2026-10-18) is the loader thread, not RE::Main's; W2's run in the
// main-thread drains. Every Find and Register runs under the engine's own IndexGuard, which takes the archive index lock -
// that lock, not the thread, is what W1 relies on.

#include "Common.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace BlackFlame::FileIndex
{
    /// kDataLoaded (its thread), once: the ids, the scans, GlobalLocations and the shipped-path lookup; logs whether
    /// usvfs_x64.dll is loaded and the Data folder the game's exe resolves. A second call does nothing
    void Init();

    /// Init ran and every check passed: Find and Register may be used
    [[nodiscard]] bool Available() noexcept;

    /// Why Find and Register cannot be used ("" when Available)
    [[nodiscard]] std::string Unavailable();

    struct Lookup
    {
        bool ran = false;      // the guard and the lookup ran without a fault
        bool found = false;
        bool archive = false;  // the record's +0x0C was negative
        std::int32_t sizeFlags = 0;
    };

    /// FindRecord for a_dataPath ("meshes\\..." / "textures\\...", normalised) under its own IndexGuard
    [[nodiscard]] Lookup Find(const std::string& a_dataPath);

    [[nodiscard]] std::string Describe(const Lookup& a_lookup);

    enum class Outcome
    {
        kRegistered,     // no record: RegisterLoose ran, FindRecord found the loose record after it
        kAlreadyLoose,   // a loose record was there (the startup index, or an earlier registration): nothing done
        kArchiveClash,   // an archive record of this path: a name clash, never registered (logged)
        kUnavailable,    // the index is not usable this session (Unavailable says why)
        kOpenFailed,     // the loose stream did not open
        kSizeMismatch,   // the loose stream's size is not the bytes expected (not our file): not registered
        kFault,          // a hardware fault inside the engine calls (logged)
        kNotFoundAfter,  // RegisterLoose ran but FindRecord does not find the record
    };

    [[nodiscard]] const char* OutcomeName(Outcome a_outcome) noexcept;

    /// The path can be loaded through the index: registered now or already loose
    [[nodiscard]] constexpr bool Usable(Outcome a_outcome) noexcept
    {
        return a_outcome == Outcome::kRegistered || a_outcome == Outcome::kAlreadyLoose;
    }

    struct Registration
    {
        Outcome outcome = Outcome::kUnavailable;
        std::string note;  // every step, for the log
    };

    /// kDataLoaded thread (W1) or a main-thread drain (W2), under the IndexGuard's lock. FindRecord first (one guard): a loose record -> kAlreadyLoose, RegisterLoose not called; an archive
    /// record -> kArchiveClash; none -> the loose stream of a_dataPath (GlobalLocations, readOnly false), its size against
    /// a_expectedSize, RegisterLoose, then FindRecord again. Counted (GetCounters); the first registration of a session
    /// logs FindRecord before and after in full
    [[nodiscard]] Registration Register(const std::string& a_dataPath, std::uint64_t a_expectedSize);

    struct Counters
    {
        std::uint32_t registered = 0;
        std::uint32_t alreadyLoose = 0;
        std::uint32_t clashes = 0;
        std::uint32_t failed = 0;      // every other outcome
        std::int64_t micros = 0;       // time inside Register, all calls
    };

    [[nodiscard]] Counters GetCounters() noexcept;

    /// The Data folder from the game's exe (<exe folder>\Data, as probe run 3 resolved it), nullopt when the exe path
    /// cannot be read whole. Writes through it go through MO2's VFS when it is there (into Overwrite)
    [[nodiscard]] std::optional<std::filesystem::path> DataFolder();
}
