#pragma once

// =============================================================================
// ROUTE START TESTS - the two rules the first in-game run (1.6.1170, MO2, 2026-10-18) broke
// =============================================================================
// SlotLedgerFiles::SaveFolderFromPrepared (PrepareFileSavePath appends ".ess") and MainThreadProof::Decide (the proof is
// the first drain's thread, not kDataLoaded's). Included by main.cpp.

#include "../../plugins/BlackFlame/src/MainThreadProof.h"
#include "../../plugins/BlackFlame/src/SlotLedgerFiles.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace RouteStartTests
{
    namespace LF = BlackFlame::SlotLedgerFiles;
    namespace MP = BlackFlame::MainThreadProof;
    namespace fs = std::filesystem;

    inline constexpr const char* kName = "HoMBF_slots.json";

    template <class Check>
    void Folder(Check&& a_check, const std::string& a_returned, const char* a_expected, const std::string& a_what)
    {
        std::string why;
        const auto folder = LF::SaveFolderFromPrepared(a_returned, kName, why);
        const bool ok = a_expected ? folder && *folder == fs::path(a_expected) : !folder && !why.empty();
        a_check(ok, "save folder: " + a_what + " ('" + a_returned + "' -> " + (folder ? "'" + LF::PathText(*folder) + "'" : "refused: " + why) + ")");
    }

    template <class Check>
    void Run(Check&& a_check)
    {
        Folder(a_check, R"(C:\Users\qtuna\Documents\My Games\Skyrim Special Edition\__MO_Saves\HoMBF_slots.json.ess)",
            R"(C:\Users\qtuna\Documents\My Games\Skyrim Special Edition\__MO_Saves)", "\".ess\" appended (the in-game return)");
        Folder(a_check, R"(C:\Users\x\Documents\My Games\Skyrim Special Edition\Saves\HoMBF_slots.json)",
            R"(C:\Users\x\Documents\My Games\Skyrim Special Edition\Saves)", "no extension appended");
        Folder(a_check, R"(D:\Profiles\Mage\saves\hombf_SLOTS.JSON.ess)", R"(D:\Profiles\Mage\saves)", "another folder, other letter case");
        Folder(a_check, R"(\\server\share\Saves\HoMBF_slots.json.ess)", R"(\\server\share\Saves)", "a UNC folder");
        Folder(a_check, R"(Saves\HoMBF_slots.json.ess)", nullptr, "a relative path refused");
        Folder(a_check, R"(C:HoMBF_slots.json.ess)", nullptr, "a drive-relative path refused");
        Folder(a_check, "", nullptr, "an empty path refused");
        Folder(a_check, R"(C:\Saves\Other.ess)", nullptr, "another file name refused");
        Folder(a_check, R"(C:\Saves\HoMBF_slots.js)", nullptr, "a cut file name refused");
        Folder(a_check, R"(C:\Saves\HoMBF_slots.json.bak)", nullptr, "another suffix refused");
        Folder(a_check, R"(C:\Saves\HoMBF_slots.jsonx)", nullptr, "a longer name refused");
        Folder(a_check, R"(C:\Saves\HoMBF_slots.json.ess.ess)", nullptr, "a doubled suffix refused");
        Folder(a_check, R"(C:\a\b\..\Saves\.\HoMBF_slots.json.ess)", R"(C:\a\Saves)", "normalised ('..' and '.' folded)");
        Folder(a_check, R"(..\Saves\HoMBF_slots.json.ess)", nullptr, "a leading '..' refused");
        Folder(a_check, R"(C:\HoMBF_slots.json.ess)", R"(C:\)", "a drive root");

        constexpr std::uint32_t kMain = 23672;
        constexpr std::uint32_t kOther = 11504;
        a_check(MP::Decide(true, 1, kMain, kMain, kMain) == MP::Verdict::kProven,
            "proof: the first drain on Main's thread is proven (kDataLoaded's thread is not an input)");
        a_check(MP::Decide(true, 1, kOther, kMain, kOther) == MP::Verdict::kRefused,
            "proof: a first drain on another thread is refused (asked from that drain)");
        a_check(MP::Decide(false, 5, kMain, kMain, kMain) == MP::Verdict::kRefused, "proof: no main-thread queue is refused");
        a_check(MP::Decide(true, 0, 0, kMain, kOther) == MP::Verdict::kPending, "proof: before the first drain it is pending");
        a_check(MP::Decide(true, 1, kMain, 0, kMain) == MP::Verdict::kPending, "proof: Main::threadID not set yet is pending, not refused");
        a_check(MP::Decide(true, 1, 0, kMain, kOther) == MP::Verdict::kPending,
            "proof: frame counted but its thread not stored yet is pending, not refused (review)");
        a_check(MP::Decide(true, 1, kMain, kMain, kOther) == MP::Verdict::kPending,
            "proof: asked from another thread (kDataLoaded's) it stays pending - only the drain decides");
        a_check(MP::Decide(true, 1, kOther, kMain, kMain) == MP::Verdict::kPending,
            "proof: asked from Main's thread while the drain ran elsewhere it stays pending");
    }
}
