#pragma once

// =============================================================================
// SLOT LEDGER TESTS - plugins/BlackFlame/src/SlotLedgerModel.h, SlotLedgerJson.h and SlotLedgerFiles.h
// =============================================================================
// Included by main.cpp. The file cases write into a fresh folder under the system temp folder and remove it.

#include "../../plugins/BlackFlame/src/SlotLedgerFiles.h"
#include "SlotLedgerReviewTests.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace SlotLedgerTests
{
    namespace LM = BlackFlame::SlotLedgerModel;
    namespace LF = BlackFlame::SlotLedgerFiles;
    namespace CC = BlackFlame::CopyContract;
    namespace Co = BlackFlame::SlotCoSave;
    namespace fs = std::filesystem;

    inline std::vector<LM::Want> Wants()
    {
        return { { CC::SlotType::kPROJ, "Skyrim.esm", 0x10FBED }, { CC::SlotType::kPROJ, "Embers XD.esp", 0x812 },
            { CC::SlotType::kIPCT, "Skyrim.esm", 0x3C9AA }, { CC::SlotType::kADDN, "Skyrim.esm", 0x2F },
            { CC::SlotType::kTXST, std::string("\xB8\xB6\xB9\xFD.esp"), 0x801, true } };
    }

    // A slot's state, nullopt when the slot is not there (review Gemini 22: a check that never dereferences null)
    inline std::optional<LM::State> StateOf(const LM::Ledger& a_ledger, std::uint16_t a_slot)
    {
        const auto* entry = LM::FindSlot(a_ledger, a_slot);
        return entry ? std::optional<LM::State>(entry->state) : std::nullopt;
    }

    inline bool SameEntries(const LM::Ledger& a_lhs, const LM::Ledger& a_rhs)
    {
        if (a_lhs.epoch != a_rhs.epoch || a_lhs.version != a_rhs.version || a_lhs.sessions != a_rhs.sessions ||
            a_lhs.entries.size() != a_rhs.entries.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a_lhs.entries.size(); ++i) {
            const auto& l = a_lhs.entries[i];
            const auto& r = a_rhs.entries[i];
            if (l.slot != r.slot || l.type != r.type || l.plugin != r.plugin || l.localId != r.localId || l.state != r.state ||
                l.since != r.since || l.conflict != r.conflict || l.terminal != r.terminal || l.published.size() != r.published.size() ||
                l.parseFailures.size() != r.parseFailures.size()) {
                return false;
            }
            // review Gemini 22: the parse failures' keys and values, not only how many
            if (!std::equal(l.parseFailures.begin(), l.parseFailures.end(), r.parseFailures.begin(), [](const auto& a_a, const auto& a_b) {
                    return a_a.first == a_b.first && a_a.second.count == a_b.second.count &&
                           a_a.second.lastSession == a_b.second.lastSession;
                })) {
                return false;
            }
            for (std::size_t j = 0; j < l.published.size(); ++j) {
                const auto& a = l.published[j];
                const auto& b = r.published[j];
                if (a.role != b.role || a.key != b.key || a.keyText != b.keyText || a.path != b.path || a.bytes != b.bytes || a.fnv64 != b.fnv64 || !(a.deps == b.deps)) {
                    return false;
                }
            }
        }
        return true;
    }

    template <class Check>
    void RunAssign(Check&& a_check)
    {
        LM::Ledger ledger;
        ledger.epoch = 7;
        const auto wants = Wants();
        const auto first = LM::Assign(ledger, wants, true, 1);
        const bool firstOk = first.assigned == 5 && first.slots[0] == 0x800 && first.slots[1] == 0x801 && first.slots[2] == 0x9E0 &&
                             first.slots[3] == 0xF00 && first.slots[4] == 0xEA0 && first.changed;
        a_check(firstOk, "ledger: five new sources take the lowest id of their type's pool (PROJ 800, 801, IPCT 9e0, ADDN f00, TXST ea0)");

        auto again = wants;
        again[1].plugin = "EMBERS XD.ESP";  // the P0 rule: plugin names compare case-insensitively
        const auto second = LM::Assign(ledger, again, true, 2);
        a_check(second.kept == 5 && !second.changed && second.slots == first.slots,
            "ledger: the same sources at the next start keep their slots, nothing changes (a plugin name in another case too)");

        const std::vector<LM::Want> fewer{ wants[0], wants[2], wants[3], wants[4] };
        const auto third = LM::Assign(ledger, fewer, true, 3);
        const auto* left = LM::FindSlot(ledger, 0x801);
        a_check(third.tombstoned == 1 && left && left->state == LM::State::kTombstone,
            "ledger: a source that left the closure leaves a tombstone (its slot stays reserved)");

        const std::vector<LM::Want> other{ wants[0], { CC::SlotType::kPROJ, "Other.esp", 0x900 } };
        const auto fourth = LM::Assign(ledger, other, true, 4);
        a_check(fourth.slots[1] == 0x802, "ledger: a new source never takes a tombstone's slot (0x801 kept, 0x802 given)");

        const auto fifth = LM::Assign(ledger, wants, true, 5);
        a_check(fifth.slots[1] == 0x801 && fifth.reactivated >= 1 && StateOf(ledger, 0x801) == LM::State::kActive,
            "ledger: the source back in the closure gets its own slot back (tombstone re-activated)");

        LM::Ledger refused;
        const auto none = LM::Assign(refused, wants, false, 1);
        a_check(none.refused.size() == 5 && refused.entries.empty() && !none.slots[0],
            "ledger: with new assignment refused (total loss, a co-save unread) no source gets a slot");

        LM::Ledger full;
        std::vector<LM::Want> many;
        for (std::uint32_t i = 0; i < 481; ++i) {
            many.push_back({ CC::SlotType::kPROJ, "Many.esp", 0x800 + i });
        }
        const auto packed = LM::Assign(full, many, true, 1);
        a_check(packed.assigned == 480 && packed.poolFull.size() == 1 && packed.poolFull[0] == 480 && !packed.slots[480],
            "ledger: the 481st PROJ finds its pool full (480) and stays without a slot");

        LM::Ledger decals;
        const LM::Want withDodt{ CC::SlotType::kTXST, "Skyrim.esm", 0x6051D, true };
        const LM::Want withoutDodt{ CC::SlotType::kTXST, "Skyrim.esm", 0x6051E, false };
        const auto split = LM::Assign(decals, { withDodt, withoutDodt }, true, 1);
        auto flipped = withDodt;
        flipped.txstDodt = false;  // a mod removed the source's DODT: the stubbed slot no longer fits
        const auto moved = LM::Assign(decals, { flipped, withoutDodt }, true, 2);
        a_check(split.slots[0] == 0xEA0 && split.slots[1] == 0xEE8 && moved.slots[0] == 0xEE9 && moved.slots[1] == 0xEE8 &&
                    StateOf(decals, 0xEA0) == LM::State::kTombstone,
            "ledger: a TXST with DODT takes a stubbed slot (ea0), one without the first unstubbed (ee8); a source whose DODT went "
            "away moves to an unstubbed slot and its old one stays reserved");

        LM::Ledger addn;
        const auto first400 = LM::Assign(addn, { { CC::SlotType::kADDN, "a.esp", 1 }, { CC::SlotType::kARTO, "a.esp", 2 },
                                                  { CC::SlotType::kHAZD, "a.esp", 3 }, { CC::SlotType::kEXPL, "a.esp", 4 } }, true, 1);
        a_check(first400.slots[0] == 0xF00 && first400.slots[1] == 0xD00 && first400.slots[2] == 0xDE0 && first400.slots[3] == 0xBC0 &&
                    CC::AddnIndexOfSlot(*first400.slots[0]) == 40000u,
            "ledger: ADDN, ARTO, HAZD and EXPL take the first id of their pools (f00 = AddOnNode index 40000, d00, de0, bc0)");
    }

    template <class Check>
    void RunMerge(Check&& a_check)
    {
        LM::Ledger mine;
        mine.epoch = 1;
        (void)LM::Assign(mine, { { CC::SlotType::kPROJ, "A.esp", 1 }, { CC::SlotType::kPROJ, "B.esp", 2 } }, true, 1);
        LM::Ledger theirs;
        theirs.epoch = 2;
        (void)LM::Assign(theirs, { { CC::SlotType::kPROJ, "A.esp", 1 }, { CC::SlotType::kPROJ, "C.esp", 3 }, { CC::SlotType::kPROJ, "D.esp", 4 } },
            true, 1);
        std::vector<std::string> conflicts;
        const auto added = LM::Merge(mine, theirs, conflicts, "a backup");
        const auto* conflict = LM::FindSlot(mine, 0x801);
        a_check(added == 1 && LM::FindSlot(mine, 0x802) && conflicts.size() == 1 && conflict && conflict->state == LM::State::kConflict &&
                    conflicts[0].find("B.esp") != std::string::npos && conflicts[0].find("C.esp") != std::string::npos,
            "ledger: a merge adds the other ledger's reservations and refuses one slot bound to two sources, naming both (" +
                (conflicts.empty() ? std::string() : conflicts[0]) + ")");
        const auto after = LM::Assign(mine, { { CC::SlotType::kPROJ, "B.esp", 2 } }, true, 2);
        a_check(after.slots[0] && *after.slots[0] == 0x803 && StateOf(mine, 0x801) == LM::State::kConflict,
            "ledger: a conflict slot is never given out again; its source gets a new one");

        LM::Ledger two;
        (void)LM::Assign(two, { { CC::SlotType::kPROJ, "A.esp", 1 } }, true, 1);
        LM::Ledger moved;  // another ledger that gave A.esp's source slot 0x801 (0x800 free there)
        LM::Entry elsewhere;
        elsewhere.slot = 0x801;
        elsewhere.type = CC::SlotType::kPROJ;
        elsewhere.plugin = "A.esp";
        elsewhere.localId = 1;
        (void)LM::Insert(moved, elsewhere);
        std::vector<std::string> none;
        (void)LM::Merge(two, moved, none, "the mirror");
        const auto pick = LM::Assign(two, { { CC::SlotType::kPROJ, "A.esp", 1 } }, true, 2);
        a_check(pick.slots[0] == 0x800 && StateOf(two, 0x801) == LM::State::kTombstone && none.empty(),
            "ledger: a source two ledgers placed in two slots keeps the lower one; the other stays reserved as a tombstone");
    }

    template <class Check>
    void RunJsonAndSaves(Check&& a_check)
    {
        LM::Ledger ledger;
        ledger.epoch = 0xFEDCBA9876543210ull;
        ledger.version = 9;
        ledger.sessions = 3;
        (void)LM::Assign(ledger, Wants(), true, 2);
        auto* projSlot = LM::FindSlot(ledger, 0x800);
        if (!projSlot) {
            a_check(false, "ledger: Assign gave slot 0x800 (needed by the JSON tests)");
            return;
        }
        auto& proj = *projSlot;
        LM::SetPublished(proj, { LM::Role::kModel, 0x1111, "format=BFK3|kind=model", "meshes\\heartofmagic\\blackflame\\copies\\0000000000001111\\copy.nif",
                                    100, 0x22, { { true, 40000, "", 0 }, { false, 0, "textures\\fx\\a.dds", 0x33 } } });
        LM::SetPublished(proj, { LM::Role::kMuzzle, 0x4444, "", "meshes\\heartofmagic\\blackflame\\copies\\0000000000004444\\copy.nif", 5, 0x55, {} });
        const bool firstFail = !LM::NoteParseFailure(proj, 0x6666, 2);
        const bool sameSession = !LM::NoteParseFailure(proj, 0x6666, 2);
        const bool terminal = LM::NoteParseFailure(proj, 0x6666, 3);
        a_check(firstFail && sameSession && terminal && proj.terminal.count(0x6666) == 1,
            "ledger: a parse failure counts once a session; the second session's makes the key terminal \"parse\"");
        // review agy redo 2.1 / 6.1: a key string and a source dep path in the code page's bytes, through the text itself
        LM::SetPublished(proj, { LM::Role::kModel, 0x1111, std::string("format=BFK3|path=meshes\\\xB8\xB6.nif"),
                                    "meshes\\heartofmagic\\blackflame\\copies\\0000000000001111\\copy.nif", 100, 0x22,
                                    { { true, 40000, "", 0 }, { false, 0, std::string("textures\\\xB8\xB6\\a.dds"), 0x33 } } });
        std::string why;
        std::string text;
        std::optional<LM::Ledger> back;
        try {
            text = LM::ToJson(ledger).dump(1);
            back = LM::FromJson(LM::Json::parse(text), why);
        } catch (const LM::Json::exception& e) {
            why = e.what();
        }
        a_check(back && SameEntries(*back, ledger) && text.find("pluginHex") != std::string::npos &&
                    text.find("keyStringHex") != std::string::npos && text.find("pathHex") != std::string::npos,
            "ledger: JSON round trip through its text (published objects with deps, parse counts; a non-ASCII plugin name, "
            "key string and dep path as hex) " + why);

        auto badFormat = LM::ToJson(ledger);
        badFormat["format"] = 2;
        auto wrongPool = LM::ToJson(ledger);
        wrongPool["slots"][0]["type"] = "IPCT";
        auto twice = LM::ToJson(ledger);
        twice["slots"].push_back(twice["slots"][0]);
        auto badHex = LM::ToJson(ledger);
        badHex["slots"][0]["localId"] = "10FBED";
        std::string w1;
        std::string w2;
        std::string w3;
        std::string w4;
        const bool allRefused = !LM::FromJson(badFormat, w1) && !LM::FromJson(wrongPool, w2) && !LM::FromJson(twice, w3) &&
                                !LM::FromJson(badHex, w4);  // before the text below reads the reasons
        a_check(allRefused,
            "ledger: refused: format 2, a slot typed against its pool, a slot twice, an upper-case id (" + w1 + "; " + w2 + "; " + w3 + "; " + w4 + ")");

        // BFSL: the session's pairs through the co-save record and back; a save from another assignment differs
        const auto snapshot = LM::ToSnapshot(ledger);
        struct Buffer
        {
            std::vector<unsigned char> bytes;
            std::size_t at = 0;
            bool OpenRecord(std::uint32_t, std::uint32_t) { return true; }
            bool WriteRecordData(const void* a_d, std::uint32_t a_n)
            {
                bytes.insert(bytes.end(), static_cast<const unsigned char*>(a_d), static_cast<const unsigned char*>(a_d) + a_n);
                return true;
            }
            std::uint32_t ReadRecordData(void* a_d, std::uint32_t a_n)
            {
                const auto n = (std::min)(static_cast<std::size_t>(a_n), bytes.size() - at);
                std::copy(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.begin() + static_cast<std::ptrdiff_t>(at + n),
                    static_cast<unsigned char*>(a_d));
                at += n;
                return static_cast<std::uint32_t>(n);
            }
        } buffer;
        Co::Snapshot read;
        const bool trip = Co::Write(buffer, snapshot) == Co::Result::Ok &&
                          Co::Read(buffer, Co::kVersion, static_cast<std::uint32_t>(buffer.bytes.size()), read) == Co::Result::Ok;
        a_check(trip && LM::Compare(ledger, read).empty() && read.slots.size() == ledger.entries.size(),
            "ledger: the session's pairs survive the BFSL record (write, read, compare: no mismatch)");
        auto other = read;
        if (other.slots.size() < 2) {
            a_check(false, "ledger: the BFSL round trip gave two slots (needed by the save tests)");
            return;
        }
        other.slots[1].plugin = "Else.esp";
        other.slots.push_back({ 0xBC0, CC::SlotType::kEXPL, "New.esp", 5 });
        const auto mismatches = LM::Compare(ledger, other);
        LM::ApplySave(ledger, mismatches, 4);
        const auto* taken = LM::FindSlot(ledger, other.slots[1].slot);
        a_check(mismatches.size() == 2 && !mismatches[0].unknown && mismatches[1].unknown && taken && taken->plugin == "Else.esp" &&
                    taken->state == LM::State::kTombstone && LM::FindSlot(ledger, 0xBC0) && LM::Compare(ledger, other).empty(),
            "ledger: a save's differing and unknown slots are found, and its pairs win for them");
    }

    template <class Check>
    void RunFiles(Check&& a_check)
    {
        std::error_code ec;
        const auto temp = fs::temp_directory_path(ec);
        if (ec || !temp.is_absolute()) {
            a_check(false, "ledger file tests: no temporary folder (" + ec.message() + ")");
            return;
        }
        const auto folder = temp / ("blackflame-cosave-check-" + std::to_string(::GetCurrentProcessId()));
        fs::remove_all(folder, ec);
        fs::create_directories(folder / "mirror", ec);
        const auto main = folder / "HoMBF_slots.json";
        const auto mirror = folder / "mirror" / "HoMBF_slots.json";
        a_check(!LF::LoadAll(main, mirror).ledger, "ledger files: no file at all reads as no ledger (total loss)");
        LM::Ledger ledger;
        ledger.epoch = 5;
        std::string mainWhy;
        std::string mirrorWhy;
        bool saved = true;
        for (std::uint32_t i = 0; i < 7; ++i) {
            ledger.version = i + 1;
            (void)LM::Assign(ledger, { { CC::SlotType::kHAZD, "Haz.esp", i } }, true, i + 1);
            saved = saved && LF::SaveAll(ledger, main, mirror, mainWhy, mirrorWhy);
        }
        bool backups = true;
        for (int n = 1; n <= LF::kBackups; ++n) {
            backups = backups && fs::exists(LF::BackupPath(main, n), ec);
        }
        const auto loaded = LF::LoadAll(main, mirror);
        a_check(saved && backups && !fs::exists(LF::BackupPath(main, LF::kBackups + 1), ec) && loaded.ledger &&
                    SameEntries(*loaded.ledger, ledger) && loaded.readable == 7,
            "ledger files: seven writes keep five backups and the mirror; the load equals the last write " + mainWhy + mirrorWhy);
        fs::remove(main, ec);
        fs::remove(mirror, ec);
        const auto lost = LF::LoadAll(main, mirror);
        a_check(lost.ledger && lost.ledger->entries.size() == 6 && lost.ledger->version == 6,
            "ledger files: main and mirror lost: the newest backup restores every reservation but the last write's (" +
                std::to_string(lost.ledger ? lost.ledger->entries.size() : 0) + " of 7 slots)");
        (void)LF::SaveAll(ledger, main, mirror, mainWhy, mirrorWhy);
        { std::ofstream cut(main, std::ios::binary | std::ios::trunc); cut << "{\"format\":1,\"epo"; }
        const auto cutLoad = LF::LoadAll(main, mirror);
        a_check(cutLoad.ledger && cutLoad.ledger->entries.size() == 7 && !cutLoad.mainReadable && cutLoad.unreadable == 1,
            "ledger files: a main cut short is refused and the mirror's reservations are all kept");
        fs::remove_all(folder, ec);
    }

    // Review fixes (P2 reviews 2026-10-18): each a regression test of a data-corruption finding
    template <class Check>
    void RunReviewFixes(Check&& a_check)
    {
        std::error_code ec;
        const auto temp = fs::temp_directory_path(ec);
        if (ec || !temp.is_absolute()) {
            a_check(false, "ledger review tests: no temporary folder (" + ec.message() + ")");
            return;
        }
        const auto folder = temp / ("blackflame-cosave-check-r-" + std::to_string(::GetCurrentProcessId()));
        fs::remove_all(folder, ec);
        fs::create_directories(folder / "mirror", ec);
        const auto main = folder / "HoMBF_slots.json";
        const auto mirror = folder / "mirror" / "HoMBF_slots.0000abcd.json";
        std::string w1;
        std::string w2;

        // D1: a save's pairs rebind slot 0x800 (ApplySave) after a backup holding the old binding was written
        LM::Ledger ledger;
        ledger.epoch = 77;
        ledger.version = 1;
        (void)LM::Assign(ledger, { { CC::SlotType::kPROJ, "Cur.esp", 1 } }, true, 1);
        a_check(LM::FindSlot(ledger, 0x800) && LM::FindSlot(ledger, 0x800)->plugin == "Cur.esp",
            "review fixtures: Cur.esp holds slot 0x800 before the save rebinds it (agy redo 6_01 #5)");
        (void)LF::SaveAll(ledger, main, mirror, w1, w2);
        Co::Slot save{ 0x800, CC::SlotType::kPROJ, "save.esp", 2 };
        LM::ApplySave(ledger, LM::Compare(ledger, { 77, 1, { save } }), 2);
        ledger.version = 2;
        (void)LF::SaveAll(ledger, main, mirror, w1, w2);  // rotates: .1 holds the old binding Cur.esp
        ledger.version = 3;
        (void)LF::SaveAll(ledger, main, mirror, w1, w2);
        const auto rebound = LF::LoadAll(main, mirror);
        const auto* slot = rebound.ledger ? LM::FindSlot(*rebound.ledger, 0x800) : nullptr;
        a_check(slot && slot->plugin == "save.esp" && slot->state == LM::State::kTombstone && rebound.conflicts.empty(),
            "review D1: a save's rebind survives a backup with the older binding of the same epoch (the newer version wins, no "
            "conflict; the slot stays usable)");

        // the same backup from another epoch is still a conflict
        LM::Ledger other = ledger;
        other.epoch = 78;
        other.version = 1;
        if (auto* bound = LM::FindSlot(other, 0x800)) {
            bound->plugin = "Cur.esp";
        }
        std::vector<std::string> conflicts;
        LM::Ledger mine = ledger;
        (void)LM::Merge(mine, other, conflicts, "another epoch");
        a_check(conflicts.size() == 1 && StateOf(mine, 0x800) == LM::State::kConflict,
            "review D1: another epoch's binding of the slot stays a conflict");

        // mirror of another epoch (another profile's) is not merged
        LM::Ledger foreign;
        foreign.epoch = 99;
        foreign.version = 50;
        (void)LM::Assign(foreign, { { CC::SlotType::kHAZD, "Foreign.esp", 9 } }, true, 1);
        std::string mirrorWhy;
        (void)LF::WriteReplacing(mirror, LM::ToJson(foreign).dump(), mirrorWhy);
        const auto withForeign = LF::LoadAll(main, mirror);
        const bool foreignAbsent = withForeign.ledger && std::none_of(withForeign.ledger->entries.begin(), withForeign.ledger->entries.end(),
                                                             [](const LM::Entry& a_e) { return a_e.plugin == "Foreign.esp"; });
        a_check(!foreign.entries.empty() && withForeign.ledger && withForeign.ledger->epoch == 77 && foreignAbsent,
            "review OCR DC2: a mirror of another epoch (another profile) is not merged (no entry of Foreign.esp)");

        // an unreadable main is not rolled into .1
        const auto backupBefore = LF::ReadOne(LF::BackupPath(main, 1), w1);
        { std::ofstream cut(main, std::ios::binary | std::ios::trunc); cut << "{\"format\":1,"; }
        ledger.version = 4;
        const bool savedOverCut = LF::SaveAll(ledger, main, mirror, w1, w2);
        std::string w3;
        const auto backupAfter = LF::ReadOne(LF::BackupPath(main, 1), w3);
        const auto mainAfter = LF::ReadOne(main, w3);
        a_check(savedOverCut && mainAfter && mainAfter->version == 4 && backupBefore && backupAfter && backupAfter->version == backupBefore->version,
            "review OCR DC3: a main that does not read is not copied over the newest backup, and main is written (v4)");

        // lead-byte-aware plugin names (code page 932: 0x83 0x41 and 0x83 0x61 are two characters)
        const auto cp932 = [](unsigned char a_byte) { return (a_byte >= 0x81 && a_byte <= 0x9F) || (a_byte >= 0xE0 && a_byte <= 0xFC); };
        a_check(!LM::SamePlugin("\x83\x41.esp", "\x83\x61.esp", cp932) && LM::SamePlugin("Ab\x83\x41.ESP", "ab\x83\x41.esp", cp932),
            "review Codex #2: plugin names fold ASCII case but never a double-byte character's trail byte");
        a_check(LM::SamePlugin("a\x83", "A\x83", cp932) && !LM::SamePlugin("a\x83", "a", cp932),
            "review agy redo 6_01 #10: a lead byte alone at the end is read safely (kept as it is)");

        // a published path that is not a copy path is dropped on load; an addn over 32 bits is refused
        auto json = LM::ToJson(ledger);
        json["slots"][0]["published"] = LM::Json::array({ { { "role", "model" }, { "key", CC::Hex16(1) }, { "path", "meshes\\..\\x.nif" },
            { "bytes", std::uint64_t{ 1 } }, { "fnv64", CC::Hex16(2) }, { "deps", LM::Json::array() } } });
        std::string why;
        const auto dropped = LM::FromJson(json, why);
        auto wide = LM::ToJson(ledger);
        wide["slots"][0]["published"] = LM::Json::array({ { { "role", "model" }, { "key", CC::Hex16(1) },
            { "path", "meshes\\heartofmagic\\blackflame\\copies\\0000000000000001\\copy.nif" }, { "bytes", std::uint64_t{ 1 } }, { "fnv64", CC::Hex16(2) },
            { "deps", LM::Json::array({ { { "addn", 40000ull + (1ull << 32) } } }) } } });
        auto good = LM::ToJson(ledger);
        good["slots"][0]["published"] = LM::Json::array({ { { "role", "model" }, { "key", CC::Hex16(1) },
            { "path", "meshes\\heartofmagic\\blackflame\\copies\\0000000000000001\\copy.nif" }, { "bytes", std::uint64_t{ 1 } }, { "fnv64", CC::Hex16(2) },
            { "deps", LM::Json::array({ { { "addn", 40000ull } } }) } } });
        std::string goodWhy;
        const auto kept = LM::FromJson(good, goodWhy);
        const auto* keptSlot = kept ? LM::FindSlot(*kept, 0x800) : nullptr;
        a_check(keptSlot && keptSlot->published.size() == 1 && dropped && LM::FindSlot(*dropped, 0x800) &&
                    LM::FindSlot(*dropped, 0x800)->published.empty() && !LM::FromJson(wide, why),
            "review OCR DC4/DC7: the same object with a copy path and addn 40000 is kept; with a path that is no copy path it "
            "is dropped; with an addn dep of 2^32 + 40000 the ledger is refused " + goodWhy);
        fs::remove_all(folder, ec);
    }

    template <class Check>
    void Run(Check&& a_check)
    {
        RunAssign(a_check);
        RunMerge(a_check);
        RunJsonAndSaves(a_check);
        RunFiles(a_check);
        RunReviewFixes(a_check);
        SlotLedgerReviewTests::Run(a_check);
    }
}
