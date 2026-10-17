#pragma once

// =============================================================================
// SLOT CO-SAVE - the byte layout of the BFSL record (which source each pool slot held when the save was made)
// =============================================================================
// Templated on the stream like master's plugins/spelllearning/include/ProgressionCoSave.h: the DLL passes
// SKSE::SerializationInterface as it is (bool OpenRecord(uint32 type, uint32 version), bool WriteRecordData(const void*,
// uint32), uint32 ReadRecordData(void*, uint32) returning the bytes read), tools/blackflame-cosave-check a byte buffer.
// No game types; C++17. Fields are integers copied as the host holds them: little-endian (x64 Windows, as SKSE).
//
// Unique id 'HBFS', record 'BFSL' v1, no padding:
//   epoch u64, ledger version u32,
//   plugin count u16, then per plugin: name length u16 (1..260), name bytes,
//   slot count u16 (at most 2000), then per slot 9 bytes: slot local id u16, type u8 (CopyContract::SlotType),
//   plugin index u16 (into the table), source local id u32 (at most 0xFFFFFF).
// One byte sequence per snapshot: plugin names are stored ASCII-lower-cased (A-Z only; bytes >= 0x80 untouched), the
// table strictly ascending (sorted, no name twice), every name used by a slot, the slots strictly ascending by slot id.
// A name holds no byte < 0x20 and none of <>"|?*:/\. Write puts any snapshot into that form (Canonical) and checks it;
// Read refuses a record that is not in it.
// Unlike SLPR, a read cut short or out of limits invalidates the whole record: Read returns the failure and leaves the
// snapshot empty, never a partial list. The record must end exactly after the last slot.
// Write validates before it opens the record (OpenRecord), so a snapshot it refuses leaves no empty BFSL record behind.

#include "CopyContract.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace BlackFlame::SlotCoSave
{
    /// A four-character code as SKSE takes it (the value MSVC gives the literal 'ABCD')
    [[nodiscard]] constexpr std::uint32_t FourCC(char a_1, char a_2, char a_3, char a_4) noexcept
    {
        return (static_cast<std::uint32_t>(static_cast<unsigned char>(a_1)) << 24) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(a_2)) << 16) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(a_3)) << 8) | static_cast<unsigned char>(a_4);
    }

    inline constexpr std::uint32_t kUniqueId = FourCC('H', 'B', 'F', 'S');   // Heart of magic Black Flame Slots
    inline constexpr std::uint32_t kRecordType = FourCC('B', 'F', 'S', 'L');
    static_assert(kUniqueId == 0x48424653u && kRecordType == 0x4246534Cu, "FourCC as SKSE's 'HBFS' and 'BFSL'");
    inline constexpr std::uint32_t kVersion = 1;
    inline constexpr std::size_t kMaxPluginName = 260;
    inline constexpr std::size_t kMaxPlugins = CopyContract::kSlotCount;  // every name is used by a slot
    inline constexpr std::size_t kMaxSlots = CopyContract::kSlotCount;
    inline constexpr std::uint32_t kMaxSourceLocalId = 0xFFFFFF;
    inline constexpr std::uint32_t kSlotEntrySize = sizeof(std::uint16_t) + sizeof(std::uint8_t) + sizeof(std::uint16_t) +
                                                    sizeof(std::uint32_t);
    inline constexpr std::uint32_t kMinNameEntry = sizeof(std::uint16_t) + 1;  // a length and at least one byte
    static_assert(kSlotEntrySize == 9, "BFSL slot entry: 9 bytes");
    inline constexpr std::string_view kNameForbidden = "<>\"|?*:/\\";

    struct Slot
    {
        std::uint16_t slot = 0;  // the pool slot's local id
        CopyContract::SlotType type = CopyContract::SlotType::kNone;
        std::string plugin;      // the source's plugin file name (stored ASCII-lower-cased)
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
        WriteFailed,      // the stream refused a write, or OpenRecord failed
        BadVersion,       // a record version other than kVersion
        ShortRead,        // the record ended inside a field
        EmptyName,        // a plugin name of length 0
        NameTooLong,      // a plugin name over kMaxPluginName
        BadNameByte,      // a byte < 0x20 or one of <>"|?*:/\ in a plugin name
        NotLowerCase,     // a stored plugin name with an ASCII capital
        TooManyPlugins,   // more plugin names than kMaxPlugins
        DuplicatePlugin,  // the table names one plugin twice
        TableNotSorted,   // the table is not in ascending order
        UnusedPlugin,     // a table name no slot uses
        TooManySlots,     // more slots than kMaxSlots
        CountTooLarge,    // a count the record's length cannot hold
        BadSlotId,        // a slot local id outside the pool table
        TypeMismatch,     // a type byte that is not the slot's pool type
        BadPluginIndex,   // a plugin index past the table
        BadSourceId,      // a source local id over kMaxSourceLocalId
        DuplicateSlot,    // one slot listed twice
        SlotsNotSorted,   // the slots are not in ascending slot id order
        TrailingBytes,    // bytes after the last slot
    };

    [[nodiscard]] constexpr const char* Describe(Result a_result) noexcept
    {
        switch (a_result) {
            case Result::Ok: return "ok";
            case Result::WriteFailed: return "a write failed";
            case Result::BadVersion: return "not a BFSL v1 record";
            case Result::ShortRead: return "the record ended inside a field";
            case Result::EmptyName: return "a plugin name is empty";
            case Result::NameTooLong: return "a plugin name is longer than the limit";
            case Result::BadNameByte: return "a plugin name holds a control byte or a character no file name has";
            case Result::NotLowerCase: return "a stored plugin name is not ASCII lower case";
            case Result::TooManyPlugins: return "more plugin names than the limit";
            case Result::DuplicatePlugin: return "the plugin table names a plugin twice";
            case Result::TableNotSorted: return "the plugin table is not sorted";
            case Result::UnusedPlugin: return "the plugin table names a plugin no slot uses";
            case Result::TooManySlots: return "more slots than the pool holds";
            case Result::CountTooLarge: return "a count larger than the record";
            case Result::BadSlotId: return "a slot id outside the pool";
            case Result::TypeMismatch: return "a slot's type is not its pool's";
            case Result::BadPluginIndex: return "a plugin index past the table";
            case Result::BadSourceId: return "a source local id over the limit";
            case Result::DuplicateSlot: return "a slot listed twice";
            case Result::SlotsNotSorted: return "the slots are not sorted by slot id";
            case Result::TrailingBytes: return "bytes after the last slot";
        }
        return "unknown";
    }

    /// A-Z to a-z, every other byte as it is (bytes >= 0x80 untouched)
    [[nodiscard]] inline std::string LowerPluginName(std::string_view a_name)
    {
        std::string out(a_name);
        for (char& c : out) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return out;
    }

    /// The rules a stored plugin name follows
    [[nodiscard]] inline Result CheckStoredName(std::string_view a_name) noexcept
    {
        if (a_name.empty()) {
            return Result::EmptyName;
        }
        if (a_name.size() > kMaxPluginName) {
            return Result::NameTooLong;
        }
        for (const char c : a_name) {
            if (static_cast<unsigned char>(c) < DataPathRule::kFirstPrintable || kNameForbidden.find(c) != std::string_view::npos) {
                return Result::BadNameByte;
            }
            if (c >= 'A' && c <= 'Z') {
                return Result::NotLowerCase;
            }
        }
        return Result::Ok;
    }

    /// The checks one slot of a stored snapshot passes
    [[nodiscard]] inline Result ValidateSlot(const Slot& a_slot) noexcept
    {
        const auto pool = CopyContract::PoolOf(a_slot.slot);
        if (!pool) {
            return Result::BadSlotId;
        }
        if (pool->type != a_slot.type) {
            return Result::TypeMismatch;
        }
        if (const auto name = CheckStoredName(a_slot.plugin); name != Result::Ok) {
            return name;
        }
        return a_slot.sourceLocalId > kMaxSourceLocalId ? Result::BadSourceId : Result::Ok;
    }

    /// A stored snapshot: at most kMaxSlots slots, each valid, strictly ascending by slot id
    [[nodiscard]] inline Result Validate(const Snapshot& a_snapshot)
    {
        if (a_snapshot.slots.size() > kMaxSlots) {
            return Result::TooManySlots;
        }
        for (std::size_t i = 0; i < a_snapshot.slots.size(); ++i) {
            if (const auto result = ValidateSlot(a_snapshot.slots[i]); result != Result::Ok) {
                return result;
            }
            if (i > 0 && a_snapshot.slots[i].slot <= a_snapshot.slots[i - 1].slot) {
                return a_snapshot.slots[i].slot == a_snapshot.slots[i - 1].slot ? Result::DuplicateSlot : Result::SlotsNotSorted;
            }
        }
        return Result::Ok;
    }

    /// a_snapshot in its stored form: names ASCII-lower-cased, slots sorted by slot id (equal ids keep their order, so
    /// Validate still sees a duplicate)
    [[nodiscard]] inline Snapshot Canonical(Snapshot a_snapshot)
    {
        for (auto& slot : a_snapshot.slots) {
            slot.plugin = LowerPluginName(slot.plugin);
        }
        std::stable_sort(a_snapshot.slots.begin(), a_snapshot.slots.end(),
            [](const Slot& a, const Slot& b) { return a.slot < b.slot; });
        return a_snapshot;
    }

    namespace Detail
    {
        template <class Stream, class T>
        [[nodiscard]] bool Write(Stream& a_out, const T& a_value)
        {
            static_assert(std::is_integral_v<T>, "BFSL fields are integers");
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
                static_assert(std::is_integral_v<T>, "BFSL fields are integers");
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

            /// After the last field: TrailingBytes when the record holds more, ShortRead when it says it does but the
            /// stream has none
            [[nodiscard]] Result End()
            {
                if (m_left == 0) {
                    return Result::Ok;
                }
                std::uint8_t byte = 0;
                return m_in.ReadRecordData(&byte, 1) == 1 ? Result::TrailingBytes : Result::ShortRead;
            }

            [[nodiscard]] std::uint32_t Left() const noexcept { return m_left; }

        private:
            Stream& m_in;
            std::uint32_t m_left;
        };
    }

    /// Canonical(a_snapshot) validated, then OpenRecord(kRecordType, kVersion) and the record's data; nothing opened or
    /// written when the check fails
    template <class Stream>
    [[nodiscard]] Result Write(Stream& a_out, const Snapshot& a_snapshot)
    {
        const Snapshot stored = Canonical(a_snapshot);
        if (const auto result = Validate(stored); result != Result::Ok) {
            return result;
        }
        std::vector<std::string> table;
        for (const auto& slot : stored.slots) {
            table.push_back(slot.plugin);
        }
        std::sort(table.begin(), table.end());
        table.erase(std::unique(table.begin(), table.end()), table.end());

        if (!a_out.OpenRecord(kRecordType, kVersion)) {
            return Result::WriteFailed;
        }
        bool ok = Detail::Write(a_out, stored.epoch) && Detail::Write(a_out, stored.ledgerVersion) &&
                  Detail::Write(a_out, static_cast<std::uint16_t>(table.size()));
        for (const auto& name : table) {
            ok = ok && Detail::Write(a_out, static_cast<std::uint16_t>(name.size())) &&
                 a_out.WriteRecordData(name.data(), static_cast<std::uint32_t>(name.size()));
        }
        ok = ok && Detail::Write(a_out, static_cast<std::uint16_t>(stored.slots.size()));
        for (const auto& slot : stored.slots) {
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
        if (static_cast<std::uint64_t>(pluginCount) * kMinNameEntry + sizeof(std::uint16_t) > in.Left()) {
            return Result::CountTooLarge;  // the names at their shortest, then the slot count
        }
        std::vector<std::string> table;
        table.reserve(pluginCount);
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
            if (const auto check = CheckStoredName(name); check != Result::Ok) {
                return check;
            }
            if (!table.empty() && name <= table.back()) {
                return name == table.back() ? Result::DuplicatePlugin : Result::TableNotSorted;
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
        std::vector<bool> used(table.size(), false);
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
            used[plugin] = true;
            slot.type = static_cast<CopyContract::SlotType>(type);
            slot.plugin = table[plugin];
            read.slots.push_back(std::move(slot));
        }
        if (const auto end = in.End(); end != Result::Ok) {
            return end;
        }
        if (const auto result = Validate(read); result != Result::Ok) {
            return result;
        }
        if (std::find(used.begin(), used.end(), false) != used.end()) {
            return Result::UnusedPlugin;
        }
        a_snapshot = std::move(read);
        return Result::Ok;
    }
}
