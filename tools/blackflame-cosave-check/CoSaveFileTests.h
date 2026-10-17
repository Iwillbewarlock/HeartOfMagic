#pragma once

// =============================================================================
// CO-SAVE FILE TESTS - plugins/BlackFlame/src/CoSaveFile.h on byte buffers built per the spike's LAYOUT.md
// =============================================================================
// Included by main.cpp (the frozen target lists main.cpp only). a_check: (bool ok, const std::string& what).

#include "../../plugins/BlackFlame/src/CoSaveFile.h"

#include <cstdint>
#include <string>
#include <vector>

namespace CoSaveFileTests
{
    namespace CF = BlackFlame::CoSaveFile;
    namespace Co = BlackFlame::SlotCoSave;
    namespace CC = BlackFlame::CopyContract;
    using Bytes = std::vector<unsigned char>;

    struct RecordSpec
    {
        std::uint32_t type;
        std::uint32_t version;
        Bytes data;
    };

    struct PluginSpec
    {
        std::uint32_t uid;
        std::vector<RecordSpec> records;
        std::int64_t lengthDelta = 0;   // added to the block length (a broken file)
        std::int64_t countDelta = 0;    // added to the record count
    };

    inline void Put32(Bytes& a_out, std::uint32_t a_value)
    {
        for (int i = 0; i < 4; ++i) {
            a_out.push_back(static_cast<unsigned char>((a_value >> (8 * i)) & 0xFF));
        }
    }

    /// A co-save file: header, then each plugin block with its records
    inline Bytes File(const std::vector<PluginSpec>& a_plugins, std::uint32_t a_magic = CF::kMagic, std::uint32_t a_format = CF::kFormatVersion)
    {
        Bytes out;
        Put32(out, a_magic);
        Put32(out, a_format);
        Put32(out, 0x02020080);  // skseVersion as the real files hold it
        Put32(out, 0x01064920);  // 1.6.1170
        Put32(out, static_cast<std::uint32_t>(a_plugins.size()));
        for (const auto& plugin : a_plugins) {
            std::uint64_t length = 0;
            for (const auto& record : plugin.records) {
                length += CF::kRecordHeaderSize + record.data.size();
            }
            Put32(out, plugin.uid);
            Put32(out, static_cast<std::uint32_t>(static_cast<std::int64_t>(plugin.records.size()) + plugin.countDelta));
            Put32(out, static_cast<std::uint32_t>(static_cast<std::int64_t>(length) + plugin.lengthDelta));
            for (const auto& record : plugin.records) {
                Put32(out, record.type);
                Put32(out, record.version);
                Put32(out, static_cast<std::uint32_t>(record.data.size()));
                out.insert(out.end(), record.data.begin(), record.data.end());
            }
        }
        return out;
    }

    struct Writer
    {
        Bytes bytes;
        bool OpenRecord(std::uint32_t, std::uint32_t) { return true; }
        bool WriteRecordData(const void* a_data, std::uint32_t a_size)
        {
            const auto* begin = static_cast<const unsigned char*>(a_data);
            bytes.insert(bytes.end(), begin, begin + a_size);
            return true;
        }
    };

    inline Co::Snapshot Sample()
    {
        Co::Snapshot snapshot;
        snapshot.epoch = 0x1122334455667788ull;
        snapshot.ledgerVersion = 4;
        snapshot.slots = { { 0x800, CC::SlotType::kPROJ, "Skyrim.esm", 0x10FBED }, { 0xF00, CC::SlotType::kADDN, "Skyrim.esm", 0x2F } };
        return snapshot;
    }

    inline Bytes BfslData(const Co::Snapshot& a_snapshot)
    {
        Writer writer;
        return Co::Write(writer, a_snapshot) == Co::Result::Ok ? writer.bytes : Bytes{};  // a failed write gives no record
    }

    inline std::vector<PluginSpec> Normal()
    {
        return { { 0, { { 'PLGN', 0, { 0x01, 0x00, 0x00, 0x0A, 0x00, 'S', 'k', 'y', 'r', 'i', 'm', '.', 'e', 's', 'm' } } } },
            { 'SPLL', { { 'SLTR', 2, { 1, 2, 3, 4 } }, { 'SLPR', 2, {} } } },
            { Co::kUniqueId, { { Co::kRecordType, Co::kVersion, BfslData(Sample()) } } },
            { 'GNWU', { { 'ABCD', 1, { 9 } } } } };
    }

    template <class Check>
    void Run(Check&& a_check)
    {
        const auto normal = File(Normal());
        const auto walk = CF::WalkCoSave(normal.data(), normal.size());
        a_check(walk.result == CF::Result::Ok && walk.plugins.size() == 4 && walk.plugins[1].records.size() == 2 &&
                    walk.runtimeVersion == 0x01064920,
            "co-save: a normal file (4 blocks, an empty record) walks to its end");
        const auto read = CF::ReadBfsl(normal.data(), normal.size());
        const auto sample = Co::Canonical(Sample());  // names stored lower-cased
        a_check(read.state == CF::BfslState::kRead && read.snapshot.epoch == sample.epoch && read.snapshot.slots == sample.slots,
            "co-save: the HBFS block's BFSL record reads back slot for slot");

        int cutsRefused = 0;
        for (std::size_t n = 0; n < normal.size(); ++n) {
            cutsRefused += CF::WalkCoSave(normal.data(), n).result != CF::Result::Ok && CF::ReadBfsl(normal.data(), n).state == CF::BfslState::kBad;
        }
        a_check(cutsRefused == static_cast<int>(normal.size()),
            "co-save: cut at every byte (" + std::to_string(normal.size()) + " cuts): every walk refused, no slot list taken (" +
                std::to_string(cutsRefused) + ")");

        auto trailing = normal;
        trailing.push_back(0);
        a_check(CF::WalkCoSave(trailing.data(), trailing.size()).result == CF::Result::TrailingBytes &&
                    CF::ReadBfsl(trailing.data(), trailing.size()).state == CF::BfslState::kBad,
            "co-save: a byte after the last block is refused (the whole file, not only the block)");

        struct Case
        {
            const char* what;
            Bytes bytes;
            CF::Result expected;
        };
        auto longBlock = Normal();
        longBlock[1].lengthDelta = 1;
        auto shortBlock = Normal();
        shortBlock[1].lengthDelta = -1;
        auto shortFirst = Normal();
        shortFirst[0].lengthDelta = -1;
        auto manyRecords = Normal();
        manyRecords[2].countDelta = 50;
        auto fewerRecords = Normal();
        fewerRecords[1].countDelta = -1;
        auto twice = Normal();
        twice.push_back(twice[2]);
        auto pastEnd = Normal();
        pastEnd.back().lengthDelta = 1000;
        const std::vector<Case> cases = {
            { "an empty file", {}, CF::Result::TooSmall },
            { "19 bytes", Bytes(19, 0), CF::Result::TooSmall },
            { "another magic", File(Normal(), 0x45534B54), CF::Result::BadMagic },
            { "format 2", File(Normal(), CF::kMagic, 2), CF::Result::BadFormat },
            { "a block one byte longer than its records", File(longBlock), CF::Result::BlockNotFilled },
            { "a block one byte shorter than its second record's header", File(shortBlock), CF::Result::RecordHeaderCut },
            { "a block one byte shorter than its only record's data", File(shortFirst), CF::Result::RecordTooLong },
            { "more records than the block holds", File(manyRecords), CF::Result::TooManyRecords },
            { "one record fewer than the block holds", File(fewerRecords), CF::Result::BlockNotFilled },
            { "the last block past the end of the file", File(pastEnd), CF::Result::PluginTooLong },
            { "HBFS twice", File(twice), CF::Result::DuplicateUid },
        };
        for (const auto& c : cases) {
            const auto result = CF::WalkCoSave(c.bytes.data(), c.bytes.size()).result;
            a_check(result == c.expected && CF::ReadBfsl(c.bytes.data(), c.bytes.size()).state == CF::BfslState::kBad,
                std::string("co-save: refused, ") + c.what + " (" + CF::Describe(result) + ")");
        }

        auto noBlock = Normal();
        noBlock.erase(noBlock.begin() + 2);
        const auto noBlockFile = File(noBlock);
        auto noRecord = Normal();
        noRecord[2].records = { { 'XXXX', 1, { 1 } } };
        const auto noRecordFile = File(noRecord);
        auto two = Normal();
        two[2].records.push_back(two[2].records[0]);
        const auto twoFile = File(two);
        auto badData = Normal();
        badData[2].records[0].data = BfslData(Sample());
        badData[2].records[0].data[16 + 2 + 10] = 0xFF;  // the first slot id's low byte: 0x8FF stays PROJ, so damage the type
        badData[2].records[0].data[16 + 2 + 10 + 2] = 7;  // PROJ slot typed ADDN
        const auto badDataFile = File(badData);
        auto version2 = Normal();
        version2[2].records[0].version = 2;
        const auto version2File = File(version2);
        a_check(CF::ReadBfsl(noBlockFile.data(), noBlockFile.size()).state == CF::BfslState::kNoBlock,
            "co-save: a file without an HBFS block reads as no Black Flame block (nothing to repair)");
        a_check(CF::ReadBfsl(noRecordFile.data(), noRecordFile.size()).state == CF::BfslState::kNoRecord,
            "co-save: an HBFS block without BFSL reads as no slot list");
        const auto twoRead = CF::ReadBfsl(twoFile.data(), twoFile.size());
        const auto badRead = CF::ReadBfsl(badDataFile.data(), badDataFile.size());
        const auto v2Read = CF::ReadBfsl(version2File.data(), version2File.size());
        a_check(twoRead.state == CF::BfslState::kBad && badRead.state == CF::BfslState::kBad && badRead.snapshot.slots.empty() &&
                    v2Read.state == CF::BfslState::kBad,
            "co-save: two BFSL records, a BFSL whose slot is typed against its pool, and BFSL v2 are refused (" + twoRead.why +
                "; " + badRead.why + "; " + v2Read.why + ")");
    }
}
