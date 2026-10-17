#pragma once

// =============================================================================
// CO-SAVE FILE - an SKSE co-save (.skse) read from its bytes, and the BFSL record found in it
// =============================================================================
// For the kPreLoadGame preflight (v3.3 #5) and the total-loss union (v3.3 #6), which read a save's co-save from disk
// before or without SKSE's own load. Layout (spike scratchpad\cosave-spike\LAYOUT.md; a walk of two real 1.6.1170 .skse
// files with cosave_walk.js ended exactly at the file size, so the "inferred" marks there are confirmed): all
// little-endian, no padding, no trailer
//   file header 20 bytes   magic u32 (disk bytes "SKSE"), formatVersion u32 (1), skseVersion u32, runtimeVersion u32,
//                          numPlugins u32
//   per plugin   12 bytes  uniqueId u32 (SetUniqueID), numRecords u32, length u32 (every record header and data after it)
//   per record   12 bytes  type u32, version u32, length u32 (the data after it), then the data
// Every count and length is checked against what is left (u64 arithmetic), each plugin's records must end exactly at its
// block end and the last block at the file size; any failure invalidates the whole file (nothing from it is used). A
// uniqueId seen twice is refused. C++17, no game types: tools/blackflame-cosave-check tests it on byte buffers and on
// real .skse files (--cosave <file>).

#include "SlotCoSave.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace BlackFlame::CoSaveFile
{
    inline constexpr std::uint32_t kMagic = 0x45534B53;  // the disk bytes "SKSE" read as a little-endian u32
    inline constexpr std::uint32_t kFormatVersion = 1;
    inline constexpr std::size_t kFileHeaderSize = 20;
    inline constexpr std::size_t kPluginHeaderSize = 12;
    inline constexpr std::size_t kRecordHeaderSize = 12;

    enum class Result
    {
        Ok,
        TooSmall,         // shorter than the file header
        BadMagic,
        BadFormat,        // a formatVersion other than 1
        PluginHeaderCut,  // a plugin header past the end of the file
        PluginTooLong,    // a plugin block's length past the end of the file
        TooManyRecords,   // more records than the block's length can hold headers for
        RecordHeaderCut,  // a record header past its block's end
        RecordTooLong,    // a record's data past its block's end
        BlockNotFilled,   // a block's records end before its length says
        TrailingBytes,    // bytes after the last block
        DuplicateUid,     // one uniqueId twice
    };

    [[nodiscard]] inline const char* Describe(Result a_result) noexcept
    {
        switch (a_result) {
            case Result::Ok: return "ok";
            case Result::TooSmall: return "shorter than the 20-byte file header";
            case Result::BadMagic: return "not an SKSE co-save (magic)";
            case Result::BadFormat: return "a co-save format other than 1";
            case Result::PluginHeaderCut: return "a plugin block header runs past the end of the file";
            case Result::PluginTooLong: return "a plugin block runs past the end of the file";
            case Result::TooManyRecords: return "a plugin block claims more records than its length holds";
            case Result::RecordHeaderCut: return "a record header runs past its plugin block";
            case Result::RecordTooLong: return "a record runs past its plugin block";
            case Result::BlockNotFilled: return "a plugin block's records end before the block does";
            case Result::TrailingBytes: return "bytes after the last plugin block";
            case Result::DuplicateUid: return "a plugin unique id appears twice";
        }
        return "unknown";
    }

    struct Record
    {
        std::uint32_t type = 0;
        std::uint32_t version = 0;
        std::uint64_t offset = 0;  // the data's offset in the file
        std::uint32_t length = 0;
    };

    struct Plugin
    {
        std::uint32_t uniqueId = 0;
        std::uint64_t offset = 0;  // the block header's offset
        std::vector<Record> records;
    };

    struct Walk
    {
        Result result = Result::Ok;
        std::uint64_t failAt = 0;  // the offset the failure was found at
        std::uint32_t skseVersion = 0;
        std::uint32_t runtimeVersion = 0;
        std::vector<Plugin> plugins;  // empty unless Ok
    };

    namespace Detail
    {
        [[nodiscard]] inline std::uint32_t U32(const unsigned char* a_bytes, std::uint64_t a_at) noexcept
        {
            std::uint32_t value = 0;
            std::memcpy(&value, a_bytes + a_at, sizeof(value));  // x64 is little-endian, as the file
            return value;
        }

        // SlotCoSave::Read's stream over one record's bytes
        struct RecordStream
        {
            const unsigned char* bytes;
            std::uint32_t size;
            std::uint32_t at = 0;

            std::uint32_t ReadRecordData(void* a_buffer, std::uint32_t a_size)
            {
                const std::uint32_t count = a_size < size - at ? a_size : size - at;
                if (count > 0) {
                    std::memcpy(a_buffer, bytes + at, count);
                }
                at += count;
                return count;
            }
        };
    }

    /// The whole file's structure; anything but Ok leaves plugins empty
    [[nodiscard]] inline Walk WalkCoSave(const unsigned char* a_bytes, std::size_t a_size)
    {
        Walk walk;
        const auto fail = [&](Result a_result, std::uint64_t a_at) {
            walk.result = a_result;
            walk.failAt = a_at;
            walk.plugins.clear();
            return walk;
        };
        const std::uint64_t size = a_size;
        if (!a_bytes || size < kFileHeaderSize) {
            return fail(Result::TooSmall, 0);
        }
        if (Detail::U32(a_bytes, 0) != kMagic) {
            return fail(Result::BadMagic, 0);
        }
        if (Detail::U32(a_bytes, 4) != kFormatVersion) {
            return fail(Result::BadFormat, 4);
        }
        walk.skseVersion = Detail::U32(a_bytes, 8);
        walk.runtimeVersion = Detail::U32(a_bytes, 12);
        const std::uint32_t pluginCount = Detail::U32(a_bytes, 16);
        std::uint64_t at = kFileHeaderSize;
        std::set<std::uint32_t> seen;
        for (std::uint32_t p = 0; p < pluginCount; ++p) {
            if (size - at < kPluginHeaderSize) {
                return fail(Result::PluginHeaderCut, at);
            }
            Plugin plugin;
            plugin.offset = at;
            plugin.uniqueId = Detail::U32(a_bytes, at);
            const std::uint32_t recordCount = Detail::U32(a_bytes, at + 4);
            const std::uint64_t length = Detail::U32(a_bytes, at + 8);
            const std::uint64_t dataAt = at + kPluginHeaderSize;
            if (length > size - dataAt) {
                return fail(Result::PluginTooLong, at + 8);
            }
            if (recordCount > length / kRecordHeaderSize) {
                return fail(Result::TooManyRecords, at + 4);
            }
            if (!seen.insert(plugin.uniqueId).second) {
                return fail(Result::DuplicateUid, at);
            }
            const std::uint64_t end = dataAt + length;
            at = dataAt;
            plugin.records.reserve(recordCount);
            for (std::uint32_t r = 0; r < recordCount; ++r) {
                if (end - at < kRecordHeaderSize) {
                    return fail(Result::RecordHeaderCut, at);
                }
                Record record;
                record.type = Detail::U32(a_bytes, at);
                record.version = Detail::U32(a_bytes, at + 4);
                record.length = Detail::U32(a_bytes, at + 8);
                record.offset = at + kRecordHeaderSize;
                if (record.length > end - record.offset) {
                    return fail(Result::RecordTooLong, at + 8);
                }
                at = record.offset + record.length;
                plugin.records.push_back(record);
            }
            if (at != end) {
                return fail(Result::BlockNotFilled, at);
            }
            walk.plugins.push_back(std::move(plugin));
        }
        if (at != size) {
            return fail(Result::TrailingBytes, at);
        }
        return walk;
    }

    enum class BfslState
    {
        kNoBlock,   // the co-save has no 'HBFS' block: nothing of ours was saved (no slot reference to repair)
        kNoRecord,  // an 'HBFS' block without a BFSL record
        kRead,      // one BFSL record, read whole
        kBad,       // the file's structure, a second BFSL record, or the record itself refused (why says which)
    };

    [[nodiscard]] inline const char* Describe(BfslState a_state) noexcept
    {
        switch (a_state) {
            case BfslState::kNoBlock: return "no Black Flame block";
            case BfslState::kNoRecord: return "a Black Flame block without a slot list";
            case BfslState::kRead: return "slot list read";
            case BfslState::kBad: return "unreadable";
        }
        return "unknown";
    }

    struct BfslRead
    {
        BfslState state = BfslState::kBad;
        std::string why;
        SlotCoSave::Snapshot snapshot;  // kRead only
    };

    /// The BFSL record of a co-save's bytes: the walk must pass whole first (a file that does not walk is kBad even when
    /// the block itself would read)
    [[nodiscard]] inline BfslRead ReadBfsl(const unsigned char* a_bytes, std::size_t a_size)
    {
        BfslRead out;
        const auto walk = WalkCoSave(a_bytes, a_size);
        if (walk.result != Result::Ok) {
            out.why = std::string(Describe(walk.result)) + " at offset " + std::to_string(walk.failAt);
            return out;
        }
        const Plugin* block = nullptr;
        for (const auto& plugin : walk.plugins) {
            if (plugin.uniqueId == SlotCoSave::kUniqueId) {
                block = &plugin;
            }
        }
        if (!block) {
            out.state = BfslState::kNoBlock;
            return out;
        }
        const Record* bfsl = nullptr;
        for (const auto& record : block->records) {
            if (record.type != SlotCoSave::kRecordType) {
                continue;
            }
            if (bfsl) {
                out.why = "two BFSL records";
                return out;
            }
            bfsl = &record;
        }
        if (!bfsl) {
            out.state = BfslState::kNoRecord;
            return out;
        }
        Detail::RecordStream stream{ a_bytes + bfsl->offset, bfsl->length };
        SlotCoSave::Snapshot snapshot;
        const auto result = SlotCoSave::Read(stream, bfsl->version, bfsl->length, snapshot);
        if (result != SlotCoSave::Result::Ok) {
            out.why = std::string("BFSL v") + std::to_string(bfsl->version) + ": " + SlotCoSave::Describe(result);
            return out;
        }
        out.state = BfslState::kRead;
        out.snapshot = std::move(snapshot);
        return out;
    }
}
