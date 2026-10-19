// =============================================================================
// blackflame-cosave-check - offline check of Black Flame's BFSL co-save record and the copy contract
// =============================================================================
//   blackflame-cosave-check --self-test
//   blackflame-cosave-check --cosave <file.skse> [<file.skse> ...]
//   blackflame-cosave-check --nif-values <file.nif> [<file.nif> ...]
// --self-test: round-trips plugins/BlackFlame/src/SlotCoSave.h on byte buffers (golden bytes, cut short at every byte,
// every write failing, over the limits, the stored form, an empty table, the full pool), runs CopyContract's shared test
// table (CopyContractTest.h; the builder's --self-test runs the same table), the co-save file reader (CoSaveFile.h;
// CoSaveFileTests.h) on buffers built per the spike's layout, the slot ledger (SlotLedgerModel.h, SlotLedgerFiles.h;
// SlotLedgerTests.h - its file cases use a folder under the system temp folder, removed after) and the BSValueNode reader
// (NifValueNodes.h; NifValueNodesTests.h), the progress.jsonl tail (ProgressTail.h; ProgressTailTests.h) and the DLL's
// read of the builder's output identity (CopyContractIdentity.h; IdentityTests.h, on the builder built beside this exe), and
// the builder launcher's pure parts (BuilderControl.h; BuilderControlTests.h: the throttle's CPU rate, the exit code text),
// and the publication plan's decisions and the ledger's previous generation (CopyGroupsDecide.h; PublicationTests.h).
// --cosave and --nif-values (SkseFile.cpp): the DLL's readers on real files.
// Exit 0 when every check (or every file) passes, 1 on a failure, 2 on a usage error. No game.

#include "../../plugins/BlackFlame/src/CopyContractTest.h"
#include "../../plugins/BlackFlame/src/SlotCoSave.h"
#include "BuilderControlTests.h"
#include "CoSaveFileTests.h"
#include "IdentityTests.h"
#include "NifValueNodesTests.h"
#include "ProgressTailTests.h"
#include "PublicationTests.h"
#include "RouteStartTests.h"
#include "TxstPathsTests.h"
#include "TxstRuleTests.h"
#include "SkseFile.h"
#include "SlotLedgerTests.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    namespace CC = BlackFlame::CopyContract;
    namespace Co = BlackFlame::SlotCoSave;
    using Bytes = std::vector<unsigned char>;

    struct Checker
    {
        int pass = 0;
        int fail = 0;
        int skip = 0;

        void operator()(bool a_ok, const std::string& a_what)
        {
            std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
            (a_ok ? pass : fail) += 1;
        }

        void Skip(const std::string& a_what)
        {
            std::cout << "SKIP " << a_what << "\n";
            ++skip;
        }
    };

    // SKSE::SerializationInterface's calls over a byte buffer; once a write fails, every later one fails too
    struct BufferWriter
    {
        Bytes bytes;
        std::size_t failAt = SIZE_MAX;  // the write that would cross this size fails (a full co-save)
        bool failed = false;
        int opened = 0;
        bool openFails = false;

        bool OpenRecord(std::uint32_t a_type, std::uint32_t a_version)
        {
            ++opened;
            return !openFails && a_type == Co::kRecordType && a_version == Co::kVersion;
        }

        bool WriteRecordData(const void* a_data, std::uint32_t a_size)
        {
            if (failed || bytes.size() + a_size > failAt) {
                failed = true;
                return false;
            }
            const auto* begin = static_cast<const unsigned char*>(a_data);
            bytes.insert(bytes.end(), begin, begin + a_size);
            return true;
        }

        template <class T>
        void Put(const T& a_value)
        {
            (void)WriteRecordData(&a_value, static_cast<std::uint32_t>(sizeof(T)));
        }

        void PutName(const std::string& a_name)
        {
            Put(static_cast<std::uint16_t>(a_name.size()));
            (void)WriteRecordData(a_name.data(), static_cast<std::uint32_t>(a_name.size()));
        }

        void PutSlot(std::uint16_t a_slot, std::uint8_t a_type, std::uint16_t a_plugin, std::uint32_t a_source)
        {
            Put(a_slot);
            Put(a_type);
            Put(a_plugin);
            Put(a_source);
        }
    };

    struct BufferReader
    {
        const Bytes& bytes;
        std::size_t available;  // the stream ends here (a file cut short)
        std::size_t at = 0;

        std::uint32_t ReadRecordData(void* a_buffer, std::uint32_t a_size)
        {
            const std::size_t end = (std::min)(available, bytes.size());
            const std::size_t count = at >= end ? 0 : (std::min)(static_cast<std::size_t>(a_size), end - at);
            if (count > 0) {
                std::memcpy(a_buffer, bytes.data() + at, count);
            }
            at += count;
            return static_cast<std::uint32_t>(count);
        }
    };

    template <class LeadByte = BlackFlame::DataPathRule::SystemLeadByte>
    [[nodiscard]] Co::Result ReadBack(const Bytes& a_bytes, Co::Snapshot& a_out, std::size_t a_available, std::uint32_t a_length,
        std::uint32_t a_version = Co::kVersion, LeadByte a_isLeadByte = {})
    {
        BufferReader reader{ a_bytes, a_available };
        return Co::Read(reader, a_version, a_length, a_out, a_isLeadByte);
    }

    [[nodiscard]] Co::Result ReadAll(const Bytes& a_bytes, Co::Snapshot& a_out)
    {
        return ReadBack(a_bytes, a_out, a_bytes.size(), static_cast<std::uint32_t>(a_bytes.size()));
    }

    [[nodiscard]] bool Same(const Co::Snapshot& a_lhs, const Co::Snapshot& a_rhs)
    {
        return a_lhs.epoch == a_rhs.epoch && a_lhs.ledgerVersion == a_rhs.ledgerVersion && a_lhs.slots == a_rhs.slots;
    }

    [[nodiscard]] bool IsEmpty(const Co::Snapshot& a_snapshot)
    {
        return a_snapshot.epoch == 0 && a_snapshot.ledgerVersion == 0 && a_snapshot.slots.empty();
    }

    // Five slots in no order, names in mixed case (Write stores them lower-cased and sorted)
    [[nodiscard]] Co::Snapshot Normal()
    {
        Co::Snapshot snapshot;
        snapshot.epoch = 0xFEDCBA9876543210ull;
        snapshot.ledgerVersion = 7;
        snapshot.slots = { { 0xFCF, CC::SlotType::kADDN, "Dawnguard.esm", 0x1ABC },
            { 0x800, CC::SlotType::kPROJ, "Skyrim.esm", 0x10FBED },
            { 0xEE8, CC::SlotType::kTXST, std::string("\xB8\xB6\xB9\xFD.esp"), 0xFFFFFF },  // a code page's bytes
            { 0x9E0, CC::SlotType::kIPCT, "Embers XD.esp", 0 },
            { 0xEE7, CC::SlotType::kTXST, "Skyrim.esm", 0x3C9AA } };
        return snapshot;
    }

    [[nodiscard]] Bytes Written(const Co::Snapshot& a_snapshot)
    {
        BufferWriter out;
        return Co::Write(out, a_snapshot) == Co::Result::Ok ? out.bytes : Bytes{};
    }

    void CheckRoundTrips(Checker& a_check)
    {
        Co::Snapshot two;
        two.epoch = 0x0102030405060708ull;
        two.ledgerVersion = 3;
        two.slots = { { 0x9E0, CC::SlotType::kIPCT, "Skyrim.esm", 0x10 }, { 0x800, CC::SlotType::kPROJ, "B.esp", 0x10FBED } };
        const Bytes expected = { 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00,
            0x05, 0x00, 'b', '.', 'e', 's', 'p', 0x0A, 0x00, 's', 'k', 'y', 'r', 'i', 'm', '.', 'e', 's', 'm', 0x02, 0x00,
            0x00, 0x08, 0x01, 0x00, 0x00, 0xED, 0xFB, 0x10, 0x00, 0xE0, 0x09, 0x02, 0x01, 0x00, 0x10, 0x00, 0x00, 0x00 };
        BufferWriter golden;
        a_check(Co::Write(golden, two) == Co::Result::Ok && golden.bytes == expected && golden.opened == 1,
            "BFSL: the golden bytes of two slots given out of order (names lowered, table and slots sorted, little-endian)");
        auto reordered = two;
        std::reverse(reordered.slots.begin(), reordered.slots.end());
        reordered.slots[0].plugin = "b.ESP";
        a_check(Written(reordered) == expected, "BFSL: one byte sequence per snapshot (slot order and name case do not matter)");

        const auto snapshot = Normal();
        const Bytes normal = Written(snapshot);
        Co::Snapshot back;
        const std::size_t size = 8 + 4 + 2 + (2 + 13) + (2 + 13) + (2 + 10) + (2 + 8) + 2 + 5 * Co::kSlotEntrySize;
        const bool normalOk = normal.size() == size && ReadAll(normal, back) == Co::Result::Ok && Same(back, Co::Canonical(snapshot)) &&
                              Written(back) == normal;
        a_check(normalOk, "BFSL: a normal snapshot (5 slots, 4 plugins, a non-ASCII name, source id 0) reads back in its stored "
                          "form and writes the same bytes again, " + std::to_string(normal.size()) + " bytes");

        Co::Snapshot none;
        none.epoch = 42;
        none.ledgerVersion = 1;
        Co::Snapshot emptyBack = Normal();
        const Bytes empty = Written(none);
        a_check(empty.size() == 16 && ReadAll(empty, emptyBack) == Co::Result::Ok && Same(emptyBack, none),
            "BFSL: an empty table and no slots round-trip (16 bytes)");

        Co::Snapshot full;
        full.epoch = 1;
        for (const auto& pool : CC::kSlotPools) {
            for (std::uint16_t i = 0; i < pool.count; ++i) {
                full.slots.push_back({ static_cast<std::uint16_t>(pool.first + i), pool.type, "plugin" + std::to_string(i % 300) + ".esp",
                    static_cast<std::uint32_t>(0x800 + i) });
            }
        }
        const Bytes fullBytes = Written(full);
        Co::Snapshot fullBack;
        a_check(full.slots.size() == CC::kSlotCount && ReadAll(fullBytes, fullBack) == Co::Result::Ok && Same(fullBack, Co::Canonical(full)),
            "BFSL: the whole pool (2000 slots, 300 plugins) round-trips, " + std::to_string(fullBytes.size()) + " bytes");

        int refused = 0;
        int keptNothing = 0;
        for (std::size_t n = 0; n < normal.size(); ++n) {
            Co::Snapshot byLength = Normal();
            Co::Snapshot byStream = Normal();
            const auto length = ReadBack(normal, byLength, normal.size(), static_cast<std::uint32_t>(n));  // the record says n
            const auto stream = ReadBack(normal, byStream, n, static_cast<std::uint32_t>(normal.size()));  // the file ends at n
            refused += length != Co::Result::Ok && stream != Co::Result::Ok;
            keptNothing += IsEmpty(byLength) && IsEmpty(byStream);
        }
        const auto cuts = static_cast<int>(normal.size());
        a_check(cuts > 0 && refused == cuts && keptNothing == cuts,
            "BFSL: cut short at every byte (" + std::to_string(cuts) + " cuts, by the record length alone and by the stream "
            "alone): every read refused, no partial snapshot");

        int writeFailures = 0;
        for (std::size_t n = 0; n < normal.size(); ++n) {
            BufferWriter stopping;
            stopping.failAt = n;
            writeFailures += Co::Write(stopping, snapshot) == Co::Result::WriteFailed;
        }
        BufferWriter closed;
        closed.openFails = true;
        a_check(writeFailures == cuts && Co::Write(closed, snapshot) == Co::Result::WriteFailed && closed.bytes.empty(),
            "BFSL: a stream that stops taking bytes at any byte, or a record that does not open, is a failed write");

        Bytes trailing = normal;
        trailing.push_back(0);
        Co::Snapshot trailingBack = Normal();
        Co::Snapshot claimsMore = Normal();
        a_check(ReadAll(trailing, trailingBack) == Co::Result::TrailingBytes && IsEmpty(trailingBack) &&
                    ReadBack(normal, claimsMore, normal.size(), static_cast<std::uint32_t>(normal.size() + 4)) == Co::Result::ShortRead &&
                    IsEmpty(claimsMore),
            "BFSL: a byte after the last slot is refused; a record longer than the stream is a short read");
        bool versionsRefused = true;
        for (const std::uint32_t version : { 0u, 2u, 0xFFFFFFFFu }) {
            Co::Snapshot out = Normal();
            versionsRefused = versionsRefused &&
                              ReadBack(normal, out, normal.size(), static_cast<std::uint32_t>(normal.size()), version) == Co::Result::BadVersion &&
                              IsEmpty(out);
        }
        a_check(versionsRefused, "BFSL: record versions 0, 2 and 0xFFFFFFFF refused, nothing kept");
    }

    // Plugin names in code page 932 (a fixed lead-byte rule, so it runs on any system): trail bytes kept and unchecked
    void CheckCodePage(Checker& a_check)
    {
        using BlackFlame::CopyContractTest::PathDetail::IsCp932Lead;
        using BlackFlame::CopyContractTest::PathDetail::NoLead;
        const std::string hyou("\x95\x5C.esp", 6);   // 表: trail 0x5C
        const std::string so("\x83\x5C.esp", 6);     // ソ: trail 0x5C
        const std::string a("\x83\x41.esp", 6);      // ア: trail 0x41 ('A')
        const std::string smallYa("\x83\x61.esp", 6);  // ャ: trail 0x61 ('a'), what a lowered ア would become
        Co::Snapshot snapshot;
        snapshot.epoch = 5;
        snapshot.slots = { { 0x800, CC::SlotType::kPROJ, hyou, 1 }, { 0x801, CC::SlotType::kPROJ, so, 2 },
            { 0x802, CC::SlotType::kPROJ, a, 3 }, { 0x803, CC::SlotType::kPROJ, smallYa, 4 },
            { 0x804, CC::SlotType::kPROJ, "Skyrim.ESM", 5 } };
        BufferWriter out;
        Co::Snapshot back = Normal();
        const bool written = Co::Write(out, snapshot, IsCp932Lead) == Co::Result::Ok;
        const bool read = written && ReadBack(out.bytes, back, out.bytes.size(), static_cast<std::uint32_t>(out.bytes.size()), Co::kVersion,
                                         IsCp932Lead) == Co::Result::Ok;
        const bool kept = read && back.slots.size() == 5 && back.slots[0].plugin == hyou && back.slots[1].plugin == so &&
                          back.slots[2].plugin == a && back.slots[3].plugin == smallYa && back.slots[4].plugin == "skyrim.esm";
        a_check(kept, "BFSL: code page 932 names round-trip unchanged (95 5C, 83 5C, 83 41 and 83 61 stay four names; "
                      "Skyrim.ESM is lowered)");
        BufferWriter refused;
        Co::Snapshot asSingleBytes = Normal();
        a_check(Co::Write(refused, snapshot, NoLead) == Co::Result::BadNameByte && refused.opened == 0 &&
                    ReadBack(out.bytes, asSingleBytes, out.bytes.size(), static_cast<std::uint32_t>(out.bytes.size()), Co::kVersion,
                        NoLead) != Co::Result::Ok &&
                    IsEmpty(asSingleBytes),
            "BFSL: read as single bytes the same names are refused (0x5C is '\\', 0x41 a capital)");
    }

    // A record from a header, table names and slots, written field by field (types as raw bytes)
    struct RawSlot
    {
        std::uint16_t slot;
        std::uint8_t type;
        std::uint16_t plugin;
        std::uint32_t source;
    };

    [[nodiscard]] Bytes Record(std::uint16_t a_pluginCount, const std::vector<std::string>& a_names, std::uint16_t a_slotCount,
        const std::vector<RawSlot>& a_slots)
    {
        BufferWriter out;
        out.Put(std::uint64_t{ 9 });
        out.Put(std::uint32_t{ 1 });
        out.Put(a_pluginCount);
        for (const auto& name : a_names) {
            out.PutName(name);
        }
        out.Put(a_slotCount);
        for (const auto& slot : a_slots) {
            out.PutSlot(slot.slot, slot.type, slot.plugin, slot.source);
        }
        return out.bytes;
    }

    void CheckLimits(Checker& a_check)
    {
        const auto proj = static_cast<std::uint8_t>(CC::SlotType::kPROJ);
        const RawSlot first{ 0x800, proj, 0, 0x10 };
        const RawSlot second{ 0x801, proj, 1, 0x10 };
        struct Case
        {
            const char* what;
            Bytes bytes;
            Co::Result expected;
        };
        const std::vector<Case> cases = {
            { "a name of 261 bytes", Record(1, { std::string(261, 'a') }, 1, { first }), Co::Result::NameTooLong },
            { "a name of 0 bytes", Record(1, { "" }, 1, { first }), Co::Result::EmptyName },
            { "a name with a capital", Record(1, { "A.esp" }, 1, { first }), Co::Result::NotLowerCase },
            { "a name with a NUL", Record(1, { std::string("a.esp\0x", 7) }, 1, { first }), Co::Result::BadNameByte },
            { "a name with a '\\'", Record(1, { "a\\b.esp" }, 1, { first }), Co::Result::BadNameByte },
            { "2001 plugin names", Record(2001, {}, 0, {}), Co::Result::TooManyPlugins },
            { "a plugin count the record cannot hold", Record(5, { "a.esp" }, 0, {}), Co::Result::CountTooLarge },
            { "the same plugin twice", Record(2, { "a.esp", "a.esp" }, 2, { first, second }), Co::Result::DuplicatePlugin },
            { "a table out of order", Record(2, { "b.esp", "a.esp" }, 2, { first, second }), Co::Result::TableNotSorted },
            { "a name no slot uses", Record(2, { "a.esp", "b.esp" }, 1, { first }), Co::Result::UnusedPlugin },
            { "2001 slots", Record(1, { "a.esp" }, 2001, {}), Co::Result::TooManySlots },
            { "a slot count the record cannot hold", Record(1, { "a.esp" }, 2, { first }), Co::Result::CountTooLarge },
            { "slot 0x7FF", Record(1, { "a.esp" }, 1, { { 0x7FF, proj, 0, 1 } }), Co::Result::BadSlotId },
            { "slot 0xFD0 (reserved)", Record(1, { "a.esp" }, 1, { { 0xFD0, 7, 0, 1 } }), Co::Result::BadSlotId },
            { "a PROJ slot typed IPCT", Record(1, { "a.esp" }, 1, { { 0x800, 2, 0, 1 } }), Co::Result::TypeMismatch },
            { "type byte 0", Record(1, { "a.esp" }, 1, { { 0x800, 0, 0, 1 } }), Co::Result::TypeMismatch },
            { "type byte 8", Record(1, { "a.esp" }, 1, { { 0x800, 8, 0, 1 } }), Co::Result::TypeMismatch },
            { "type byte 0xFF", Record(1, { "a.esp" }, 1, { { 0x800, 0xFF, 0, 1 } }), Co::Result::TypeMismatch },
            { "plugin index 1 of 1", Record(1, { "a.esp" }, 1, { { 0x800, proj, 1, 1 } }), Co::Result::BadPluginIndex },
            { "source id 0x1000000", Record(1, { "a.esp" }, 1, { { 0x800, proj, 0, 0x1000000 } }), Co::Result::BadSourceId },
            { "one slot twice", Record(1, { "a.esp" }, 2, { first, first }), Co::Result::DuplicateSlot },
            { "slots out of order", Record(2, { "a.esp", "b.esp" }, 2, { second, first }), Co::Result::SlotsNotSorted },
        };
        for (const auto& c : cases) {
            Co::Snapshot out = Normal();
            const auto result = ReadAll(c.bytes, out);
            a_check(result == c.expected && IsEmpty(out), std::string("BFSL: refused, ") + c.what + " (" + Co::Describe(result) + ")");
        }
        Co::Snapshot ok;
        a_check(ReadAll(Record(1, { std::string(260, 'a') }, 1, { first }), ok) == Co::Result::Ok && ok.slots.size() == 1 &&
                    ok.slots[0].plugin.size() == 260,
            "BFSL: a name of exactly 260 bytes is taken");

        struct WriteCase
        {
            const char* what;
            Co::Slot slot;
            Co::Result expected;
        };
        const std::vector<WriteCase> writes = {
            { "an empty name", { 0x800, CC::SlotType::kPROJ, "", 1 }, Co::Result::EmptyName },
            { "a name of 261 bytes", { 0x800, CC::SlotType::kPROJ, std::string(261, 'a'), 1 }, Co::Result::NameTooLong },
            { "a name with ':'", { 0x800, CC::SlotType::kPROJ, "a:b.esp", 1 }, Co::Result::BadNameByte },
            { "slot 0xFD0", { 0xFD0, CC::SlotType::kADDN, "a.esp", 1 }, Co::Result::BadSlotId },
            { "a wrong type", { 0x800, CC::SlotType::kIPCT, "a.esp", 1 }, Co::Result::TypeMismatch },
            { "source id 0x1000000", { 0x800, CC::SlotType::kPROJ, "a.esp", 0x1000000 }, Co::Result::BadSourceId },
        };
        for (const auto& w : writes) {
            BufferWriter out;
            auto snapshot = Normal();
            snapshot.slots.push_back(w.slot);
            const auto result = Co::Write(out, snapshot);
            a_check(result == w.expected && out.bytes.empty() && out.opened == 0,
                std::string("BFSL: the writer refuses ") + w.what + " before opening the record (" + Co::Describe(result) + ")");
        }
        BufferWriter twice;
        auto duplicate = Normal();
        duplicate.slots.push_back(duplicate.slots.front());
        a_check(Co::Write(twice, duplicate) == Co::Result::DuplicateSlot && twice.opened == 0, "BFSL: the writer refuses one slot twice");
    }
}

int main(int a_argc, char** a_argv)
{
    // review agy redo 7.1: the real-file modes report a throw as a failure (exit 1), never an abort
    const auto guardedTool = [](int (*a_tool)(int, char**), int a_count, char** a_values) {
        try {
            return a_tool(a_count, a_values);
        } catch (const std::exception& e) {
            std::cerr << "failed: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "failed: an exception of an unknown type\n";
        }
        return 1;
    };
    if (a_argc >= 3 && std::string(a_argv[1]) == "--cosave") {
        return guardedTool(&BlackFlame::SkseFile::WalkFiles, a_argc, a_argv);
    }
    if (a_argc >= 3 && std::string(a_argv[1]) == "--nif-values") {
        return guardedTool(&BlackFlame::SkseFile::NifValues, a_argc, a_argv);
    }
    if (a_argc != 2 || std::string(a_argv[1]) != "--self-test") {
        std::cerr << "Usage:\n  blackflame-cosave-check --self-test\n  blackflame-cosave-check --cosave <file.skse> [<file.skse> ...]\n"
                     "  blackflame-cosave-check --nif-values <file.nif> [<file.nif> ...]\n";
        return 2;
    }
    Checker check;
    try {
        CheckRoundTrips(check);
        CheckLimits(check);
        CheckCodePage(check);
        const int contract = BlackFlame::CopyContractTest::Run(
            [&](bool a_ok, const std::string& a_what) { check(a_ok, "contract: " + a_what); },
            [&](const std::string& a_what) { check.Skip("contract: " + a_what); });
        check(contract >= BlackFlame::CopyContractTest::kMinChecks,
            "contract: the shared table ran " + std::to_string(contract) + " checks (at least " +
                std::to_string(BlackFlame::CopyContractTest::kMinChecks) + ")");
    } catch (const std::exception& e) {
        check(false, std::string("exception: ") + e.what());
    } catch (...) {
        check(false, "an exception of an unknown type");
    }
    // review agy redo 7.5: one suite that throws is named and does not keep the others from running
    const auto report = [&](bool a_ok, const std::string& a_what) { check(a_ok, a_what); };
    const auto suite = [&](const char* a_name, auto&& a_run) {
        try {
            a_run(report);
        } catch (const std::exception& e) {
            check(false, std::string(a_name) + ": exception: " + e.what());
        } catch (...) {
            check(false, std::string(a_name) + ": an exception of an unknown type");
        }
    };
    suite("co-save file", [](auto&& a_check) { CoSaveFileTests::Run(a_check); });
    suite("ledger", [](auto&& a_check) { SlotLedgerTests::Run(a_check); });
    suite("NIF value nodes", [](auto&& a_check) { NifValueNodesTests::Run(a_check); });
    suite("progress tail", [](auto&& a_check) { ProgressTailTests::Run(a_check); });
    suite("route start", [](auto&& a_check) { RouteStartTests::Run(a_check); });
    suite("txst paths", [](auto&& a_check) { TxstPathsTests::Run(a_check); });
    suite("txst rule", [](auto&& a_check) { TxstRuleTests::Run(a_check); });
    suite("builder control", [](auto&& a_check) { BuilderControlTests::Run(a_check); });
    suite("publication", [](auto&& a_check) { PublicationTests::Run(a_check); });
    suite("identity", [&](auto&& a_check) {
        auto skip = [&](const std::string& a_what) { check.Skip(a_what); };
        IdentityTests::Run(a_check, skip);
    });
    std::cout << "SELF-TEST pass=" << check.pass << " fail=" << check.fail << " skip=" << check.skip << "\n";
    return check.fail == 0 ? 0 : 1;
}
