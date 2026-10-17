#pragma once

// =============================================================================
// SLOT CO-SAVE - the byte layout of the BFSL record (which source each pool slot held when the save was made)
// =============================================================================
// Templated on the stream like master's plugins/spelllearning/include/ProgressionCoSave.h: the DLL passes
// SKSE::SerializationInterface as it is (bool WriteRecordData(const void*, uint32), uint32 ReadRecordData(void*, uint32)
// returning the bytes read), tools/blackflame-cosave-check a byte buffer. No game types; C++17.
//
// Unique id 'HBFS', record 'BFSL' v1, little-endian, no padding:
//   epoch u64, ledger version u32,
//   plugin count u16, then per plugin: name length u16 (1..260), name bytes,
//   slot count u16 (at most 2000), then per slot 9 bytes: slot local id u16, type u8 (CopyContract::SlotType),
//   plugin index u16 (into the table), source local id u32 (at most 0xFFFFFF).
// Unlike SLPR, a read cut short or out of limits invalidates the whole record: Read returns the failure and leaves the
// snapshot empty, never a partial list. The record must end exactly after the last slot (trailing bytes are refused).
// Every slot must be in the pool table with its pool's type, once; the table names no plugin twice. Write runs the same
// checks first, so nothing it writes is unreadable. The table is written sorted (one byte sequence per snapshot).

#include "CopyContract.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace BlackFlame::SlotCoSave
{
    inline constexpr std::uint32_t kUniqueId = 'HBFS';   // Heart of magic Black Flame Slots
    inline constexpr std::uint32_t kRecordType = 'BFSL';
    inline constexpr std::uint32_t kVersion = 1;
    inline constexpr std::size_t kMaxPluginName = 260;
    inline constexpr std::size_t kMaxPlugins = CopyContract::kSlotCount;  // more names than slots is no snapshot
    inline constexpr std::size_t kMaxSlots = CopyContract::kSlotCount;
    inline constexpr std::uint32_t kMaxSourceLocalId = 0xFFFFFF;
    inline constexpr std::uint32_t kSlotEntrySize = sizeof(std::uint16_t) + sizeof(std::uint8_t) + sizeof(std::uint16_t) +
                                                    sizeof(std::uint32_t);
    static_assert(kSlotEntrySize == 9, "BFSL slot entry: 9 bytes");

    struct Slot
    {
        std::uint16_t slot = 0;  // the pool slot's local id
        CopyContract::SlotType type = CopyContract::SlotType::kNone;
        std::string plugin;      // the source's plugin file name
        std::uint32_t sourceLocalId = 0;

        [[nodiscard]] bool operator==(const Slot& a_other) const
        {
            return slot == a_other.slot && type == a_other.type && plugin == a_other.plugin &&
                   sourceLocalId == a_other.sourceLocalId;
        }
    };

    struct Snapshot
    {
        std::uint64_t epoch = 0;
        std::uint32_t ledgerVersion = 0;
        std::vector<Slot> slots;
    };

    enum class Result
    {
        Ok,
        WriteFailed,     // the stream refused a write
        BadVersion,      // a record version other than kVersion
        ShortRead,       // the record ended inside a field
        EmptyName,       // a plugin name of length 0
        NameTooLong,     // a plugin name over kMaxPluginName
        TooManyPlugins,  // more plugin names than kMaxPlugins
        DuplicatePlugin, // the table names one plugin twice
        TooManySlots,    // more slots than kMaxSlots
        CountTooLarge,   // a count the record's length cannot hold
        BadSlotId,       // a slot local id outside the pool table
        TypeMismatch,    // a type byte that is not the slot's pool type
        BadPluginIndex,  // a plugin index past the table
        BadSourceId,     // a source local id over kMaxSourceLocalId
        DuplicateSlot,   // one slot listed twice
        TrailingBytes,   // bytes after the last slot
    };

    [[nodiscard]] constexpr const char* Describe(Result a_result) noexcept
    {
        switch (a_result) {
            case Result::Ok: return "ok";
            case Result::WriteFailed: return "a write failed";
            case Result::BadVersion: return "not a BFSL v1 record";
            case Result::ShortRead: return "the record ended inside a field";
            case Result::EmptyName: return "a plugin name is empty";
            case Result::NameTooLong: return "a plugin name is longer than 260 bytes";
            case Result::TooManyPlugins: return "more plugin names than the limit";
            case Result::DuplicatePlugin: return "the plugin table names a plugin twice";
            case Result::TooManySlots: return "more slots than the pool holds";
            case Result::CountTooLarge: return "a count larger than the record";
            case Result::BadSlotId: return "a slot id outside the pool";
            case Result::TypeMismatch: return "a slot's type is not its pool's";
            case Result::BadPluginIndex: return "a plugin index past the table";
            case Result::BadSourceId: return "a source local id over 0xFFFFFF";
            case Result::DuplicateSlot: return "a slot listed twice";
            case Result::TrailingBytes: return "bytes after the last slot";
        }
        return "unknown";
    }

    /// The checks Read makes on the decoded record, for one slot
    [[nodiscard]] inline Result ValidateSlot(const Slot& a_slot) noexcept
    {
        const auto pool = CopyContract::PoolOf(a_slot.slot);
        if (!pool) {
            return Result::BadSlotId;
        }
        if (pool->type != a_slot.type) {
            return Result::TypeMismatch;
        }
        if (a_slot.plugin.empty()) {
            return Result::EmptyName;
        }
        if (a_slot.plugin.size() > kMaxPluginName) {
            return Result::NameTooLong;
        }
        if (a_slot.sourceLocalId > kMaxSourceLocalId) {
            return Result::BadSourceId;
        }
        return Result::Ok;
    }

    /// Every slot valid, none twice, at most kMaxSlots, at most kMaxPlugins distinct plugins
    [[nodiscard]] inline Result Validate(const Snapshot& a_snapshot)
    {
        if (a_snapshot.slots.size() > kMaxSlots) {
            return Result::TooManySlots;
        }
        std::set<std::uint16_t> seen;
        std::set<std::string> plugins;
        for (const auto& slot : a_snapshot.slots) {
            if (const auto result = ValidateSlot(slot); result != Result::Ok) {
                return result;
            }
            if (!seen.insert(slot.slot).second) {
                return Result::DuplicateSlot;
            }
            plugins.insert(slot.plugin);
        }
        return plugins.size() > kMaxPlugins ? Result::TooManyPlugins : Result::Ok;
    }

    namespace Detail
    {
        template <class Stream, class T>
        [[nodiscard]] bool Write(Stream& a_out, const T& a_value)
        {
            return a_out.WriteRecordData(&a_value, static_cast<std::uint32_t>(sizeof(T)));
        }

        // Reads one field and counts it against the record's length
        template <class Stream>
        class Reader
        {
        public:
            Reader(Stream& a_in, std::uint32_t a_length) : m_in(a_in), m_left(a_length) {}

            template <class T>
            [[nodiscard]] bool Read(T& a_value)
            {
                return Bytes(&a_value, static_cast<std::uint32_t>(sizeof(T)));
            }

            [[nodiscard]] bool Bytes(void* a_buffer, std::uint32_t a_size)
            {
                if (a_size > m_left || m_in.ReadRecordData(a_buffer, a_size) != a_size) {
                    return false;
                }
                m_left -= a_size;
                return true;
            }

            [[nodiscard]] std::uint32_t Left() const noexcept { return m_left; }

        private:
            Stream& m_in;
            std::uint32_t m_left;
        };
    }

    /// The record's data (after OpenRecord(kRecordType, kVersion)); Validate first, nothing written when it fails
    template <class Stream>
    [[nodiscard]] Result Write(Stream& a_out, const Snapshot& a_snapshot)
    {
        if (const auto result = Validate(a_snapshot); result != Result::Ok) {
            return result;
        }
        std::vector<std::string> table;
        for (const auto& slot : a_snapshot.slots) {
            table.push_back(slot.plugin);
        }
        std::sort(table.begin(), table.end());
        table.erase(std::unique(table.begin(), table.end()), table.end());

        bool ok = Detail::Write(a_out, a_snapshot.epoch) && Detail::Write(a_out, a_snapshot.ledgerVersion) &&
                  Detail::Write(a_out, static_cast<std::uint16_t>(table.size()));
        for (const auto& name : table) {
            ok = ok && Detail::Write(a_out, static_cast<std::uint16_t>(name.size())) &&
                 a_out.WriteRecordData(name.data(), static_cast<std::uint32_t>(name.size()));
        }
        ok = ok && Detail::Write(a_out, static_cast<std::uint16_t>(a_snapshot.slots.size()));
        for (const auto& slot : a_snapshot.slots) {
            const auto index = static_cast<std::uint16_t>(std::lower_bound(table.begin(), table.end(), slot.plugin) - table.begin());
            ok = ok && Detail::Write(a_out, slot.slot) && Detail::Write(a_out, static_cast<std::uint8_t>(slot.type)) &&
                 Detail::Write(a_out, index) && Detail::Write(a_out, slot.sourceLocalId);
        }
        return ok ? Result::Ok : Result::WriteFailed;
    }

    /// One BFSL record of a_version and a_length bytes (GetNextRecordInfo's). Anything but Ok leaves a_snapshot empty.
    template <class Stream>
    [[nodiscard]] Result Read(Stream& a_in, std::uint32_t a_version, std::uint32_t a_length, Snapshot& a_snapshot)
    {
        a_snapshot = Snapshot{};
        if (a_version != kVersion) {
            return Result::BadVersion;
        }
        Detail::Reader<Stream> in(a_in, a_length);
        Snapshot read;
        std::uint16_t pluginCount = 0;
        if (!in.Read(read.epoch) || !in.Read(read.ledgerVersion) || !in.Read(pluginCount)) {
            return Result::ShortRead;
        }
        if (pluginCount > kMaxPlugins) {
            return Result::TooManyPlugins;
        }
        if (static_cast<std::uint64_t>(pluginCount) * (sizeof(std::uint16_t) + 1) > in.Left()) {
            return Result::CountTooLarge;  // each name takes at least its length and one byte
        }
        std::vector<std::string> table;
        table.reserve(pluginCount);
        std::set<std::string> names;
        for (std::uint16_t i = 0; i < pluginCount; ++i) {
            std::uint16_t length = 0;
            if (!in.Read(length)) {
                return Result::ShortRead;
            }
            if (length == 0) {
                return Result::EmptyName;
            }
            if (length > kMaxPluginName) {
                return Result::NameTooLong;
            }
            std::string name(length, '\0');
            if (!in.Bytes(name.data(), length)) {
                return Result::ShortRead;
            }
            if (!names.insert(name).second) {
                return Result::DuplicatePlugin;
            }
            table.push_back(std::move(name));
        }
        std::uint16_t slotCount = 0;
        if (!in.Read(slotCount)) {
            return Result::ShortRead;
        }
        if (slotCount > kMaxSlots) {
            return Result::TooManySlots;
        }
        if (static_cast<std::uint64_t>(slotCount) * kSlotEntrySize > in.Left()) {
            return Result::CountTooLarge;
        }
        read.slots.reserve(slotCount);
        for (std::uint16_t i = 0; i < slotCount; ++i) {
            Slot slot;
            std::uint8_t type = 0;
            std::uint16_t plugin = 0;
            if (!in.Read(slot.slot) || !in.Read(type) || !in.Read(plugin) || !in.Read(slot.sourceLocalId)) {
                return Result::ShortRead;
            }
            if (plugin >= table.size()) {
                return Result::BadPluginIndex;
            }
            slot.type = static_cast<CopyContract::SlotType>(type);
            slot.plugin = table[plugin];
            read.slots.push_back(std::move(slot));
        }
        if (in.Left() != 0) {
            return Result::TrailingBytes;
        }
        if (const auto result = Validate(read); result != Result::Ok) {
            return result;
        }
        a_snapshot = std::move(read);
        return Result::Ok;
    }
}
