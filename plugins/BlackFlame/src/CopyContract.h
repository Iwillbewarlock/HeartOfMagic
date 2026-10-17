#pragma once

// =============================================================================
// COPY CONTRACT - what the DLL and the builder agree on for the single-launch copies (P0, frozen)
// =============================================================================
// Included by both sides like DataPathRule.h: the DLL (C++23, CommonLib) and tools/blackflame-greybuild (C++17, no
// CommonLib), and by tools/blackflame-cosave-check. C++17, no game types. Design: single-launch-design v3.2 with the
// v3.3 decisions. Parts:
//   this file              contract version, the slot pool table, FNV-1a 64, hex, file states, request.json v5 names
//   CopyContractKeys.h     the key strings of every model object (form NIF, muzzle, ADDN MPS, TXST, texture, palette)
//   CopyContractProgress.h the builder's progress.jsonl line: outcome states, writer and tolerant parser
//   CopyContractTest.h     the shared test table both self-tests run (cosave-check and the builder)
// The copies output path rule lives in DataPathRule.h (CopyPath, ParseCopyPath); the co-save record in SlotCoSave.h.

#include "DataPathRule.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace BlackFlame::CopyContract
{
    // Bumped only when a key string, the pool table, the progress line or request v5 changes meaning
    inline constexpr std::uint32_t kContractVersion = 1;

    // =========================================================================
    // Slot pool (the slot ESL's local ids 0x800-0xFFF): fixed types, contiguous, never reused
    // =========================================================================

    // The co-save's type byte (SlotCoSave.h); 0 is no type
    enum class SlotType : std::uint8_t
    {
        kNone = 0,
        kPROJ = 1,
        kIPCT = 2,
        kEXPL = 3,
        kARTO = 4,
        kHAZD = 5,
        kTXST = 6,
        kADDN = 7,
    };

    [[nodiscard]] inline constexpr std::string_view SlotTypeTag(SlotType a_type) noexcept
    {
        switch (a_type) {
            case SlotType::kPROJ: return "PROJ";
            case SlotType::kIPCT: return "IPCT";
            case SlotType::kEXPL: return "EXPL";
            case SlotType::kARTO: return "ARTO";
            case SlotType::kHAZD: return "HAZD";
            case SlotType::kTXST: return "TXST";
            case SlotType::kADDN: return "ADDN";
            case SlotType::kNone: break;
        }
        return "";
    }

    struct SlotPool
    {
        SlotType type;
        std::uint16_t first;  // first local id
        std::uint16_t count;
    };

    inline constexpr std::uint16_t kFirstEslLocalId = 0x800;
    inline constexpr std::uint16_t kLastEslLocalId = 0xFFF;
    inline constexpr std::uint16_t kTxstWithDodt = 72;    // the TXST pool's first 72 carry a DODT stub
    inline constexpr std::uint16_t kTxstWithoutDodt = 24; // its last 24 none
    inline constexpr std::uint32_t kAddnIndexBase = 40000;  // a slot ADDN's AddOnNode index = base + its place in the pool

    inline constexpr std::array<SlotPool, 7> kSlotPools{ {
        { SlotType::kPROJ, 0x800, 480 },
        { SlotType::kIPCT, 0x9E0, 480 },
        { SlotType::kEXPL, 0xBC0, 320 },
        { SlotType::kARTO, 0xD00, 224 },
        { SlotType::kHAZD, 0xDE0, 192 },
        { SlotType::kTXST, 0xEA0, kTxstWithDodt + kTxstWithoutDodt },
        { SlotType::kADDN, 0xF00, 208 },
    } };

    inline constexpr std::uint16_t kFirstReservedLocalId = 0xFD0;  // 0xFD0-0xFFF: reserved, no slot
    inline constexpr std::uint16_t kSlotCount = 2000;

    namespace Detail
    {
        [[nodiscard]] inline constexpr bool PoolsContiguous() noexcept
        {
            std::uint32_t next = kFirstEslLocalId;
            for (const auto& pool : kSlotPools) {
                if (pool.first != next || pool.count == 0 || pool.type == SlotType::kNone) {
                    return false;
                }
                next = static_cast<std::uint32_t>(pool.first) + pool.count;
            }
            return next == kFirstReservedLocalId;
        }

        [[nodiscard]] inline constexpr std::uint32_t PoolTotal() noexcept
        {
            std::uint32_t total = 0;
            for (const auto& pool : kSlotPools) {
                total += pool.count;
            }
            return total;
        }
    }

    static_assert(Detail::PoolsContiguous(), "the slot pools must follow each other from 0x800 and end at 0xFD0");
    static_assert(Detail::PoolTotal() == kSlotCount, "the slot pools hold 2000 slots");
    static_assert(kFirstReservedLocalId <= kLastEslLocalId + 1u, "the pools stay inside the ESL range");
    static_assert(kSlotPools[5].type == SlotType::kTXST && kSlotPools[5].count == 96, "TXST pool: 96");
    static_assert(kSlotPools[5].first + kTxstWithDodt == 0xEE8, "TXST: 0xEA0-0xEE7 with DODT, 0xEE8-0xEFF without");
    static_assert(kSlotPools[6].type == SlotType::kADDN && kSlotPools[6].first == 0xF00 && kSlotPools[6].count == 208,
        "ADDN pool: 208 at 0xF00");

    inline constexpr std::uint32_t kAddnIndexEnd = kAddnIndexBase + kSlotPools[6].count;  // one past the last ADDN index

    /// The pool a local id belongs to, or nullopt (outside 0x800-0xFCF)
    [[nodiscard]] inline constexpr std::optional<SlotPool> PoolOf(std::uint32_t a_localId) noexcept
    {
        for (const auto& pool : kSlotPools) {
            if (a_localId >= pool.first && a_localId < static_cast<std::uint32_t>(pool.first) + pool.count) {
                return pool;
            }
        }
        return std::nullopt;
    }

    /// The pool of a type, or nullopt for kNone or a value no pool has
    [[nodiscard]] inline constexpr std::optional<SlotPool> PoolOfType(SlotType a_type) noexcept
    {
        for (const auto& pool : kSlotPools) {
            if (pool.type == a_type) {
                return pool;
            }
        }
        return std::nullopt;
    }

    /// A TXST slot whose record carries the DODT stub (false for any other id)
    [[nodiscard]] inline constexpr bool TxstHasDodt(std::uint32_t a_localId) noexcept
    {
        const auto pool = PoolOf(a_localId);
        return pool && pool->type == SlotType::kTXST && a_localId < static_cast<std::uint32_t>(pool->first) + kTxstWithDodt;
    }

    /// A slot ADDN's AddOnNode index (40000 + its place in the ADDN pool), or nullopt for a local id outside that pool
    [[nodiscard]] inline constexpr std::optional<std::uint32_t> AddnIndexOfSlot(std::uint32_t a_localId) noexcept
    {
        const auto pool = PoolOf(a_localId);
        if (!pool || pool->type != SlotType::kADDN) {
            return std::nullopt;
        }
        return kAddnIndexBase + (a_localId - pool->first);
    }

    /// AddnIndexOfSlot back: the ADDN slot's local id, or nullopt outside 40000-40207
    [[nodiscard]] inline constexpr std::optional<std::uint16_t> SlotOfAddnIndex(std::uint32_t a_index) noexcept
    {
        if (a_index < kAddnIndexBase || a_index >= kAddnIndexEnd) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(kSlotPools[6].first + (a_index - kAddnIndexBase));
    }

    static_assert(AddnIndexOfSlot(0xF0C) == 40012u && SlotOfAddnIndex(40012) == 0xF0C, "ADDN index base 40000");
    static_assert(!PoolOf(0x7FF) && !PoolOf(kFirstReservedLocalId) && PoolOf(0x800)->type == SlotType::kPROJ, "pool edges");
    static_assert(TxstHasDodt(0xEE7) && !TxstHasDodt(0xEE8) && !TxstHasDodt(0xEFF + 1), "TXST DODT split");

    // =========================================================================
    // FNV-1a 64 and hex (the key, file hashes, the progress line)
    // =========================================================================

    inline constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ull;  // the same as NifDump::kFnvOffset
    inline constexpr std::uint64_t kFnvPrime = 0x100000001B3ull;
    inline constexpr std::size_t kHex16Digits = 16;

    /// FNV-1a 64 over a_bytes, continuing from a_hash
    [[nodiscard]] inline constexpr std::uint64_t Fnv64(std::string_view a_bytes, std::uint64_t a_hash = kFnvOffset) noexcept
    {
        for (const char c : a_bytes) {
            a_hash = (a_hash ^ static_cast<unsigned char>(c)) * kFnvPrime;
        }
        return a_hash;
    }

    static_assert(Fnv64("") == 0xCBF29CE484222325ull && Fnv64("a") == 0xAF63DC4C8601EC8Cull &&
                      Fnv64("foobar") == 0x85944171F73967E8ull,
        "FNV-1a 64 published test vectors");

    /// 16 lower-case hex digits, no 0x (the key16, every fnv64 in a key string or a progress line)
    [[nodiscard]] inline std::string Hex16(std::uint64_t a_value)
    {
        constexpr char kDigits[] = "0123456789abcdef";
        std::string out(kHex16Digits, '0');
        for (std::size_t i = 0; i < kHex16Digits; ++i) {
            out[kHex16Digits - 1 - i] = kDigits[(a_value >> (4 * i)) & 0xF];
        }
        return out;
    }

    /// Hex16 back: exactly 16 lower-case hex digits, else nullopt (upper case is refused: one spelling per key)
    [[nodiscard]] inline std::optional<std::uint64_t> ParseHex16(std::string_view a_text) noexcept
    {
        if (a_text.size() != kHex16Digits) {
            return std::nullopt;
        }
        std::uint64_t value = 0;
        for (const char c : a_text) {
            int digit = -1;
            if (c >= '0' && c <= '9') {
                digit = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                digit = c - 'a' + 10;
            }
            if (digit < 0) {
                return std::nullopt;
            }
            value = (value << 4) | static_cast<std::uint64_t>(digit);
        }
        return value;
    }

    [[nodiscard]] inline bool IsKey16(std::string_view a_text) noexcept
    {
        return ParseHex16(a_text).has_value();
    }

    /// a_bytes as lower-case hex, two digits a byte (raw record strings in a key string)
    [[nodiscard]] inline std::string LowerHex(std::string_view a_bytes)
    {
        constexpr char kDigits[] = "0123456789abcdef";
        std::string out;
        out.reserve(a_bytes.size() * 2);
        for (const char c : a_bytes) {
            const auto byte = static_cast<unsigned char>(c);
            out.push_back(kDigits[byte >> 4]);
            out.push_back(kDigits[byte & 0x0F]);
        }
        return out;
    }

    // =========================================================================
    // A file's state as a key string names it
    // =========================================================================

    inline constexpr std::string_view kSentinelNotDumped = "notdumped";  // outside the dump caps (not read)
    inline constexpr std::string_view kSentinelMissing = "missing";      // does not open (or its path is refused)
    inline constexpr std::string_view kSentinelNone = "none";            // no file at all (an empty TXST slot, no ADDN part)

    enum class FileState : std::uint8_t
    {
        kHashed,     // read whole: fnv64 is its bytes' FNV-1a 64
        kNotDumped,  // opened, outside the dump caps
        kMissing,    // does not open
    };

    struct FileHash
    {
        FileState state = FileState::kMissing;
        std::uint64_t fnv64 = 0;  // kHashed only

        [[nodiscard]] static FileHash Hashed(std::uint64_t a_fnv64) noexcept { return { FileState::kHashed, a_fnv64 }; }
        [[nodiscard]] static FileHash NotDumped() noexcept { return { FileState::kNotDumped, 0 }; }
        [[nodiscard]] static FileHash Missing() noexcept { return { FileState::kMissing, 0 }; }
    };

    /// The value a key string writes for a_hash: 16 hex digits, "notdumped" or "missing"
    [[nodiscard]] inline std::string FileHashText(const FileHash& a_hash)
    {
        switch (a_hash.state) {
            case FileState::kHashed: return Hex16(a_hash.fnv64);
            case FileState::kNotDumped: return std::string(kSentinelNotDumped);
            case FileState::kMissing: break;
        }
        return std::string(kSentinelMissing);
    }

    // =========================================================================
    // Options carried in request.json v5 (the builder in key mode takes them only from there)
    // =========================================================================

    enum class PaletteMode : std::uint8_t
    {
        kDerived,  // the builder's default (GreyPalette.h)
        kGodskin,
    };

    inline constexpr std::string_view kPaletteModeDerived = "derived";
    inline constexpr std::string_view kPaletteModeGodskin = "godskin";

    [[nodiscard]] inline constexpr std::string_view PaletteModeText(PaletteMode a_mode) noexcept
    {
        return a_mode == PaletteMode::kGodskin ? kPaletteModeGodskin : kPaletteModeDerived;
    }

    [[nodiscard]] inline constexpr std::optional<PaletteMode> PaletteModeFromText(std::string_view a_text) noexcept
    {
        if (a_text == kPaletteModeDerived) {
            return PaletteMode::kDerived;
        }
        if (a_text == kPaletteModeGodskin) {
            return PaletteMode::kGodskin;
        }
        return std::nullopt;
    }

    // request.json v5 (the DLL writes, the builder reads). Keys in the request are key16 text.
    namespace Request
    {
        inline constexpr std::uint32_t kVersion = 5;
        inline constexpr const char* kOptions = "options";          // { tint, tintAdd, paletteMode, keyPrefix }
        inline constexpr const char* kTint = "tint";                // number (double as the DLL's config holds it)
        inline constexpr const char* kTintAdd = "tintAdd";          // number
        inline constexpr const char* kPaletteMode = "paletteMode";  // "derived" | "godskin"
        // P0 addition: the key prefix the DLL built (KeyPrefixString), so the builder's own child keys (textures,
        // palettes) start from the same text instead of a reconstruction that could drift
        inline constexpr const char* kKeyPrefix = "keyPrefix";
        inline constexpr const char* kContractVersion = "contractVersion";  // P0 addition: kContractVersion
        inline constexpr const char* kFormKey = "key";              // per form: its model's key16
        inline constexpr const char* kMuzzles = "muzzles";          // [{ path, key }]
        inline constexpr const char* kAddnMps = "addnMps";          // [{ slot, path, key }]
        inline constexpr const char* kTxst = "txst";                // [{ slot, key, dodt }]
        inline constexpr const char* kPath = "path";
        inline constexpr const char* kKey = "key";
        inline constexpr const char* kSlot = "slot";                // a slot's local id (number)
        inline constexpr const char* kDodt = "dodt";                // bool
    }

    // Files both sides name
    inline constexpr const char* kStagingFolder = "staging";              // <SKSE log folder>\HeartOfMagic_BlackFlame\staging
    inline constexpr const char* kProgressFile = "progress.jsonl";        // in the staging folder
    inline constexpr const char* kBuilderExeRelative = "HeartOfMagic_BlackFlame\\blackflame-greybuild.exe";  // beside the DLL
}

#include "CopyContractKeys.h"
#include "CopyContractProgress.h"
