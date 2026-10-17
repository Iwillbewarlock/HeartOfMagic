// =============================================================================
// blackflame-cosave-check - offline check of Black Flame's BFSL co-save record and the copy contract
// =============================================================================
//   blackflame-cosave-check --self-test
//   blackflame-cosave-check --cosave <file.skse> [<file.skse> ...]
// --self-test: round-trips plugins/BlackFlame/src/SlotCoSave.h on byte buffers (normal, cut short at every byte, over the
// limits, an empty table, the full pool), runs CopyContract's shared test table (CopyContractTest.h; the builder's
// --self-test runs the same table), the co-save file reader (CoSaveFile.h; CoSaveFileTests.h) on buffers built per the
// spike's layout, and the slot ledger (SlotLedgerModel.h, SlotLedgerFiles.h; SlotLedgerTests.h - its file cases use a
// folder under the system temp folder, removed after). --cosave: walks real co-saves with the DLL's reader and prints each
// block and whether a BFSL slot list is there. Exit 0 when every check (or every walk) passes, 1 on a failure, 2 on a
// usage error. No game.

#include "../../plugins/BlackFlame/src/CopyContractTest.h"
#include "../../plugins/BlackFlame/src/SlotCoSave.h"
#include "CoSaveFileTests.h"
#include "SlotLedgerTests.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
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

        void operator()(bool a_ok, const std::string& a_what)
        {
            std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
            (a_ok ? pass : fail) += 1;
        }
    };

    // SKSE::SerializationInterface's two calls over a byte buffer
    struct BufferWriter
    {
        Bytes bytes;
        std::size_t failAt = SIZE_MAX;  // the write that crosses this size fails (a full co-save)

        bool WriteRecordData(const void* a_data, std::uint32_t a_size)
        {
            if (bytes.size() + a_size > failAt) {
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

        void PutSlot(std::uint16_t a_slot, CC::SlotType a_type, std::uint16_t a_plugin, std::uint32_t a_source)
        {
            Put(a_slot);
            Put(static_cast<std::uint8_t>(a_type));
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
            const std::size_t count = (std::min)(static_cast<std::size_t>(a_size), available - at);
            if (count > 0) {
                std::memcpy(a_buffer, bytes.data() + at, count);
            }
            at += count;
            return static_cast<std::uint32_t>(count);
        }
    };

    [[nodiscard]] Co::Result ReadBack(const Bytes& a_bytes, Co::Snapshot& a_out, std::size_t a_available, std::uint32_t a_length,
        std::uint32_t a_version = Co::kVersion)
    {
        BufferReader reader{ a_bytes, a_available };
        return Co::Read(reader, a_version, a_length, a_out);
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

    [[nodiscard]] Co::Snapshot Normal()
    {
        Co::Snapshot snapshot;
        snapshot.epoch = 0xFEDCBA9876543210ull;
        snapshot.ledgerVersion = 7;
        snapshot.slots = { { 0x800, CC::SlotType::kPROJ, "Skyrim.esm", 0x10FBED },
            { 0x9E0, CC::SlotType::kIPCT, "Embers XD.esp", 0x812 },
            { 0xEE7, CC::SlotType::kTXST, "Skyrim.esm", 0x3C9AA },
            { 0xEE8, CC::SlotType::kTXST, std::string("\xB8\xB6\xB9\xFD.esp"), 0xFFFFFF },  // a code page's bytes
            { 0xFCF, CC::SlotType::kADDN, "Dawnguard.esm", 0x1ABC } };
        return snapshot;
    }

    void CheckRoundTrips(Checker& a_check)
    {
        BufferWriter golden;
        Co::Snapshot one;
        one.epoch = 0x0102030405060708ull;
        one.ledgerVersion = 3;
        one.slots = { { 0x800, CC::SlotType::kPROJ, "Skyrim.esm", 0x10FBED } };
        const Bytes expected = { 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0A, 0x00,
            'S', 'k', 'y', 'r', 'i', 'm', '.', 'e', 's', 'm', 0x01, 0x00, 0x00, 0x08, 0x01, 0x00, 0x00, 0xED, 0xFB, 0x10, 0x00 };
        a_check(Co::Write(golden, one) == Co::Result::Ok && golden.bytes == expected,
            "BFSL: the golden bytes of one slot (epoch, ledger version, table, 9-byte slot, little-endian)");

        BufferWriter normal;
        const auto snapshot = Normal();
        Co::Snapshot back;
        const std::size_t size = 8 + 4 + 2 + (2 + 10) + (2 + 13) + (2 + 13) + (2 + 8) + 2 + 5 * Co::kSlotEntrySize;
        const bool normalOk = Co::Write(normal, snapshot) == Co::Result::Ok && normal.bytes.size() == size &&
                              ReadAll(normal.bytes, back) == Co::Result::Ok && Same(back, snapshot);
        a_check(normalOk,
            "BFSL: a normal snapshot (5 slots, 4 plugins, a non-ASCII name) reads back slot for slot, " +
                std::to_string(normal.bytes.size()) + " bytes");

        BufferWriter empty;
        Co::Snapshot none;
        none.epoch = 42;
        none.ledgerVersion = 1;
        Co::Snapshot emptyBack;
        a_check(Co::Write(empty, none) == Co::Result::Ok && empty.bytes.size() == 16 && ReadAll(empty.bytes, emptyBack) == Co::Result::Ok &&
                    Same(emptyBack, none),
            "BFSL: an empty table and no slots round-trip (16 bytes)");

        Co::Snapshot full;
        full.epoch = 1;
        for (const auto& pool : CC::kSlotPools) {
            for (std::uint16_t i = 0; i < pool.count; ++i) {
                full.slots.push_back({ static_cast<std::uint16_t>(pool.first + i), pool.type, "Plugin" + std::to_string(i % 300) + ".esp",
                    static_cast<std::uint32_t>(0x800 + i) });
            }
        }
        BufferWriter fullOut;
        Co::Snapshot fullBack;
        const bool fullOk = full.slots.size() == CC::kSlotCount && Co::Write(fullOut, full) == Co::Result::Ok &&
                            ReadAll(fullOut.bytes, fullBack) == Co::Result::Ok && Same(fullBack, full);
        a_check(fullOk,
            "BFSL: the whole pool (2000 slots, 300 plugins) round-trips, " + std::to_string(fullOut.bytes.size()) + " bytes");

        int cutRefused = 0;
        int cutKeptNothing = 0;
        for (std::size_t n = 0; n < normal.bytes.size(); ++n) {
            Co::Snapshot shortLength = Normal();
            Co::Snapshot shortStream = Normal();
            const auto byLength = ReadBack(normal.bytes, shortLength, n, static_cast<std::uint32_t>(n));
            const auto byStream = ReadBack(normal.bytes, shortStream, n, static_cast<std::uint32_t>(normal.bytes.size()));
            cutRefused += byLength != Co::Result::Ok && byStream != Co::Result::Ok;
            cutKeptNothing += IsEmpty(shortLength) && IsEmpty(shortStream);
        }
        const auto cuts = static_cast<int>(normal.bytes.size());
        a_check(cutRefused == cuts && cutKeptNothing == cuts,
            "BFSL: cut short at every byte (" + std::to_string(cuts) + " cuts, by the record length and by the stream): every "
            "read refused, no partial snapshot (" + std::to_string(cutRefused) + " refused, " + std::to_string(cutKeptNothing) +
            " empty)");

        Bytes trailing = normal.bytes;
        trailing.push_back(0);
        Co::Snapshot trailingBack;
        Co::Snapshot versionBack;
        a_check(ReadAll(trailing, trailingBack) == Co::Result::TrailingBytes && IsEmpty(trailingBack) &&
                    ReadBack(normal.bytes, versionBack, normal.bytes.size(), static_cast<std::uint32_t>(normal.bytes.size()), 2) ==
                        Co::Result::BadVersion,
            "BFSL: a byte after the last slot and a record version 2 are refused");

        BufferWriter refused;
        auto bad = Normal();
        bad.slots.back().slot = 0xFD0;
        BufferWriter full2;
        full2.failAt = 20;
        a_check(Co::Write(refused, bad) == Co::Result::BadSlotId && refused.bytes.empty() &&
                    Co::Write(full2, Normal()) == Co::Result::WriteFailed,
            "BFSL: the writer refuses an invalid snapshot before writing a byte, and reports a stream that stops taking bytes");
    }

    // A record from a header and one table name and some slots, written field by field
    [[nodiscard]] Bytes Record(std::uint16_t a_pluginCount, const std::vector<std::string>& a_names, std::uint16_t a_slotCount,
        const std::vector<Co::Slot>& a_slots, const std::vector<std::uint16_t>& a_pluginIndex)
    {
        BufferWriter out;
        out.Put(std::uint64_t{ 9 });
        out.Put(std::uint32_t{ 1 });
        out.Put(a_pluginCount);
        for (const auto& name : a_names) {
            out.PutName(name);
        }
        out.Put(a_slotCount);
        for (std::size_t i = 0; i < a_slots.size(); ++i) {
            out.PutSlot(a_slots[i].slot, a_slots[i].type, a_pluginIndex[i], a_slots[i].sourceLocalId);
        }
        return out.bytes;
    }

    void CheckLimits(Checker& a_check)
    {
        const Co::Slot proj{ 0x800, CC::SlotType::kPROJ, "", 0x10 };
        struct Case
        {
            const char* what;
            Bytes bytes;
            Co::Result expected;
        };
        const std::vector<Case> cases = {
            { "a name of 261 bytes", Record(1, { std::string(261, 'a') }, 1, { proj }, { 0 }), Co::Result::NameTooLong },
            { "a name of 0 bytes", Record(1, { "" }, 1, { proj }, { 0 }), Co::Result::EmptyName },
            { "2001 plugin names", Record(2001, {}, 0, {}, {}), Co::Result::TooManyPlugins },
            { "a plugin count the record cannot hold", Record(5, { "a.esp" }, 0, {}, {}), Co::Result::CountTooLarge },
            { "the same plugin twice", Record(2, { "a.esp", "a.esp" }, 1, { proj }, { 0 }), Co::Result::DuplicatePlugin },
            { "2001 slots", Record(1, { "a.esp" }, 2001, {}, {}), Co::Result::TooManySlots },
            { "a slot count the record cannot hold", Record(1, { "a.esp" }, 2, { proj }, { 0 }), Co::Result::CountTooLarge },
            { "slot 0x7FF", Record(1, { "a.esp" }, 1, { { 0x7FF, CC::SlotType::kPROJ, "", 1 } }, { 0 }), Co::Result::BadSlotId },
            { "slot 0xFD0 (reserved)", Record(1, { "a.esp" }, 1, { { 0xFD0, CC::SlotType::kADDN, "", 1 } }, { 0 }), Co::Result::BadSlotId },
            { "a PROJ slot typed IPCT", Record(1, { "a.esp" }, 1, { { 0x800, CC::SlotType::kIPCT, "", 1 } }, { 0 }), Co::Result::TypeMismatch },
            { "type byte 0", Record(1, { "a.esp" }, 1, { { 0x800, CC::SlotType::kNone, "", 1 } }, { 0 }), Co::Result::TypeMismatch },
            { "plugin index 1 of 1", Record(1, { "a.esp" }, 1, { proj }, { 1 }), Co::Result::BadPluginIndex },
            { "source id 0x1000000", Record(1, { "a.esp" }, 1, { { 0x800, CC::SlotType::kPROJ, "", 0x1000000 } }, { 0 }), Co::Result::BadSourceId },
            { "one slot twice", Record(1, { "a.esp" }, 2, { proj, proj }, { 0, 0 }), Co::Result::DuplicateSlot },
        };
        for (const auto& c : cases) {
            Co::Snapshot out = Normal();
            const auto result = ReadAll(c.bytes, out);
            a_check(result == c.expected && IsEmpty(out), std::string("BFSL: refused, ") + c.what + " (" + Co::Describe(result) + ")");
        }
        Co::Snapshot ok;
        a_check(ReadAll(Record(1, { std::string(260, 'a') }, 1, { proj }, { 0 }), ok) == Co::Result::Ok && ok.slots.size() == 1 &&
                    ok.slots[0].plugin.size() == 260,
            "BFSL: a name of exactly 260 bytes is taken");
    }
}

namespace
{
    // --cosave: every block of each file and the HBFS block's state; 0 when every file walks to its end
    int WalkFiles(int a_argc, char** a_argv)
    {
        namespace CF = BlackFlame::CoSaveFile;
        int failed = 0;
        for (int i = 2; i < a_argc; ++i) {
            std::ifstream in(a_argv[i], std::ios::binary);
            if (!in) {
                std::cout << a_argv[i] << ": does not open\n";
                ++failed;
                continue;
            }
            const Bytes bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const auto walk = CF::WalkCoSave(bytes.data(), bytes.size());
            const auto bfsl = CF::ReadBfsl(bytes.data(), bytes.size());
            std::cout << a_argv[i] << ": " << bytes.size() << " bytes, walk " << CF::Describe(walk.result);
            if (walk.result != CF::Result::Ok) {
                std::cout << " at offset " << walk.failAt;
            }
            std::cout << ", runtime 0x" << std::hex << walk.runtimeVersion << std::dec << ", " << walk.plugins.size()
                      << " blocks; Black Flame: " << CF::Describe(bfsl.state);
            if (bfsl.state == CF::BfslState::kRead) {
                std::cout << " (" << bfsl.snapshot.slots.size() << " slots, epoch " << CC::Hex16(bfsl.snapshot.epoch) << ")";
            } else if (!bfsl.why.empty()) {
                std::cout << " (" << bfsl.why << ")";
            }
            std::cout << "\n";
            for (const auto& plugin : walk.plugins) {
                std::string uid;
                for (int shift = 24; shift >= 0; shift -= 8) {
                    const auto c = static_cast<unsigned char>((plugin.uniqueId >> shift) & 0xFF);
                    uid.push_back(c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '.');
                }
                std::cout << "  block '" << uid << "' (0x" << std::hex << plugin.uniqueId << std::dec << ") at " << plugin.offset
                          << ": " << plugin.records.size() << " records\n";
            }
            failed += walk.result != CF::Result::Ok ? 1 : 0;
        }
        return failed == 0 ? 0 : 1;
    }
}

int main(int a_argc, char** a_argv)
{
    if (a_argc >= 3 && std::string(a_argv[1]) == "--cosave") {
        return WalkFiles(a_argc, a_argv);
    }
    if (a_argc != 2 || std::string(a_argv[1]) != "--self-test") {
        std::cerr << "Usage:\n  blackflame-cosave-check --self-test\n  blackflame-cosave-check --cosave <file.skse> [<file.skse> ...]\n";
        return 2;
    }
    Checker check;
    try {
        CheckRoundTrips(check);
        CheckLimits(check);
        BlackFlame::CopyContractTest::Run([&](bool a_ok, const std::string& a_what) { check(a_ok, "contract: " + a_what); });
        CoSaveFileTests::Run([&](bool a_ok, const std::string& a_what) { check(a_ok, a_what); });
        SlotLedgerTests::Run([&](bool a_ok, const std::string& a_what) { check(a_ok, a_what); });
    } catch (const std::exception& e) {
        check(false, std::string("exception: ") + e.what());
    }
    std::cout << "SELF-TEST pass=" << check.pass << " fail=" << check.fail << "\n";
    return check.fail == 0 ? 0 : 1;
}
