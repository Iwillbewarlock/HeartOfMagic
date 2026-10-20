#pragma once

// =============================================================================
// COPY CONTRACT - what the DLL and the builder agree on for the single-launch copies (P0, frozen)
// =============================================================================
// Included by both sides like DataPathRule.h: the DLL (C++23, CommonLib) and tools/blackflame-greybuild (C++17, no
// CommonLib), and by tools/blackflame-cosave-check. C++17, no game types. Design: single-launch-design v3.2 with the
// v3.3 decisions. Each part includes this core and nothing includes back:
//   this file              contract version, the slot pool table, FNV-1a 64, hex, the tint text, file states,
//                          request.json v5 names
//   CopyContractKeys.h     the key strings of every model object (form NIF, muzzle, ADDN MPS, TXST, texture, palette)
//   CopyContractProgress.h the builder's progress.jsonl line: outcome states, writer and tolerant parser
//   CopyContractEfsh.h     request efshPalettes[]: its merge, writer and reader (contract 6)
//   CopyContractEfshBase.h request efshBases[]: the non-palette fill / particle base textures and their key (contract 8)
//   CopyContractTest.h     the shared test table both self-tests run (cosave-check and the builder)
// The copies output path rule lives in DataPathRule.h (CopyPath, ParseCopyPath); the co-save record in SlotCoSave.h.

#include "DataPathRule.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace BlackFlame::CopyContract
{
    // Bumped when a key string, the copy path, the pool table, the progress line or request v5 changes meaning.
    // 2: copies named copy.<ext>, notdumped:<size>, non-ASCII paths as <name>hex=, the ADDN table's full sort order.
    // 3: tints in the builder's own range 0..2, ADDN rows once, plugin names lowered outside double-byte characters.
    // 4: derived palettes named palette.dds (IsOurPalettePath), request textures[].state and addnMps[].index, addn deps
    //    on mps lines, the too_large line's cap field.
    // 5: key format BFK4: the prefix's out=<output identity> in place of exe=<builder exe fnv64> (CopyContractKeys.h).
    // 6: request efshPalettes[] (CopyContractEfsh.h: the effect shader palettes the builder greys for the corpse rule, each
    //    one's dds line at its key's palette.dds), required; builder output version 2.
    // 7: casting-art objects carry tintAddHands and marble opacity/scale, keyed independently of other copies.
    // 8: request efshBases[] (CopyContractEfshBase.h: the effect shaders' non-palette membrane fill and particle base
    //    textures the builder greys for the corpse rule, each one's dds line at its key's copy.dds, key kind efshbase),
    //    required; builder output version stays 2 (a new kind: no earlier key string or output byte changes).
    inline constexpr std::uint32_t kContractVersion = 8;

    // =========================================================================
    // Slot pool (the slot ESL's local ids 0x800-0xFFF): fixed types, contiguous, never reused
    // =========================================================================

    // The co-save's type byte (SlotCoSave.h); 0 is no type. The value is the pool's place in kSlotPools + 1.
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

    [[nodiscard]] constexpr std::string_view SlotTypeTag(SlotType a_type) noexcept
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
    inline constexpr std::size_t kTxstPoolIndex = 5;
    inline constexpr std::size_t kAddnPoolIndex = 6;
    inline constexpr SlotPool kAddnPool = kSlotPools[kAddnPoolIndex];

    inline constexpr std::uint16_t kFirstReservedLocalId = 0xFD0;  // 0xFD0-0xFFF: reserved, no slot
    inline constexpr std::uint16_t kSlotCount = 2000;

    namespace Detail
    {
        [[nodiscard]] constexpr bool PoolIs(std::size_t a_index, SlotType a_type, std::uint16_t a_first, std::uint16_t a_count) noexcept
        {
            return kSlotPools[a_index].type == a_type && kSlotPools[a_index].first == a_first && kSlotPools[a_index].count == a_count;
        }

        [[nodiscard]] constexpr bool PoolsContiguous() noexcept
        {
            std::uint32_t next = kFirstEslLocalId;
            for (const auto& pool : kSlotPools) {
                if (pool.first != next || pool.count == 0) {
                    return false;
                }
                next = static_cast<std::uint32_t>(pool.first) + pool.count;
            }
            return next == kFirstReservedLocalId;
        }

        [[nodiscard]] constexpr bool TypesInEnumOrder() noexcept
        {
            for (std::size_t i = 0; i < kSlotPools.size(); ++i) {
                if (static_cast<std::size_t>(kSlotPools[i].type) != i + 1) {
                    return false;  // also makes every type appear once
                }
            }
            return true;
        }

        [[nodiscard]] constexpr std::uint32_t PoolTotal() noexcept
        {
            std::uint32_t total = 0;
            for (const auto& pool : kSlotPools) {
                total += pool.count;
            }
            return total;
        }
    }

    // The frozen table, entry by entry (a change here moves every local id a co-save names)
    static_assert(Detail::PoolIs(0, SlotType::kPROJ, 0x800, 480), "PROJ: 480 at 0x800");
    static_assert(Detail::PoolIs(1, SlotType::kIPCT, 0x9E0, 480), "IPCT: 480 at 0x9E0");
    static_assert(Detail::PoolIs(2, SlotType::kEXPL, 0xBC0, 320), "EXPL: 320 at 0xBC0");
    static_assert(Detail::PoolIs(3, SlotType::kARTO, 0xD00, 224), "ARTO: 224 at 0xD00");
    static_assert(Detail::PoolIs(4, SlotType::kHAZD, 0xDE0, 192), "HAZD: 192 at 0xDE0");
    static_assert(Detail::PoolIs(kTxstPoolIndex, SlotType::kTXST, 0xEA0, 96), "TXST: 96 at 0xEA0");
    static_assert(Detail::PoolIs(kAddnPoolIndex, SlotType::kADDN, 0xF00, 208), "ADDN: 208 at 0xF00");
    static_assert(Detail::TypesInEnumOrder(), "pool i has type value i + 1, each type once");
    static_assert(Detail::PoolsContiguous(), "the slot pools follow each other from 0x800 and end at 0xFD0");
    static_assert(Detail::PoolTotal() == kSlotCount, "the slot pools hold 2000 slots");
    static_assert(kFirstReservedLocalId <= kLastEslLocalId + 1u, "the pools stay inside the ESL range");
    static_assert(kSlotPools[kTxstPoolIndex].first + kTxstWithDodt == 0xEE8, "TXST: 0xEA0-0xEE7 with DODT, 0xEE8-0xEFF without");

    inline constexpr std::uint32_t kAddnIndexEnd = kAddnIndexBase + kAddnPool.count;  // one past the last ADDN index

    /// The pool a local id belongs to, or nullopt (outside 0x800-0xFCF)
    [[nodiscard]] constexpr std::optional<SlotPool> PoolOf(std::uint32_t a_localId) noexcept
    {
        for (const auto& pool : kSlotPools) {
            if (a_localId >= pool.first && a_localId < static_cast<std::uint32_t>(pool.first) + pool.count) {
                return pool;
            }
        }
        return std::nullopt;
    }

    /// The pool of a type, or nullopt for kNone or a value no pool has
    [[nodiscard]] constexpr std::optional<SlotPool> PoolOfType(SlotType a_type) noexcept
    {
        for (const auto& pool : kSlotPools) {
            if (pool.type == a_type) {
                return pool;
            }
        }
        return std::nullopt;
    }

    /// The type of a local id's pool, kNone outside every pool
    [[nodiscard]] constexpr SlotType TypeOf(std::uint32_t a_localId) noexcept
    {
        const auto pool = PoolOf(a_localId);
        return pool ? pool->type : SlotType::kNone;
    }

    /// A TXST slot whose record carries the DODT stub (false for any other id)
    [[nodiscard]] constexpr bool TxstHasDodt(std::uint32_t a_localId) noexcept
    {
        const auto& txst = kSlotPools[kTxstPoolIndex];
        return a_localId >= txst.first && a_localId < static_cast<std::uint32_t>(txst.first) + kTxstWithDodt;
    }

    /// A slot ADDN's AddOnNode index (40000 + its place in the ADDN pool), or nullopt for a local id outside that pool
    [[nodiscard]] constexpr std::optional<std::uint32_t> AddnIndexOfSlot(std::uint32_t a_localId) noexcept
    {
        if (a_localId < kAddnPool.first || a_localId >= static_cast<std::uint32_t>(kAddnPool.first) + kAddnPool.count) {
            return std::nullopt;
        }
        return kAddnIndexBase + (a_localId - kAddnPool.first);
    }

    /// AddnIndexOfSlot back: the ADDN slot's local id, or nullopt outside 40000-40207
    [[nodiscard]] constexpr std::optional<std::uint16_t> SlotOfAddnIndex(std::uint32_t a_index) noexcept
    {
        if (a_index < kAddnIndexBase || a_index >= kAddnIndexEnd) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(kAddnPool.first + (a_index - kAddnIndexBase));
    }

    static_assert(TypeOf(0x7FF) == SlotType::kNone && TypeOf(0x800) == SlotType::kPROJ && TypeOf(0x9DF) == SlotType::kPROJ &&
                      TypeOf(0x9E0) == SlotType::kIPCT && TypeOf(0xBBF) == SlotType::kIPCT && TypeOf(0xBC0) == SlotType::kEXPL &&
                      TypeOf(0xCFF) == SlotType::kEXPL && TypeOf(0xD00) == SlotType::kARTO && TypeOf(0xDDF) == SlotType::kARTO &&
                      TypeOf(0xDE0) == SlotType::kHAZD && TypeOf(0xE9F) == SlotType::kHAZD && TypeOf(0xEA0) == SlotType::kTXST &&
                      TypeOf(0xEFF) == SlotType::kTXST && TypeOf(0xF00) == SlotType::kADDN && TypeOf(0xFCF) == SlotType::kADDN &&
                      TypeOf(0xFD0) == SlotType::kNone,
        "every pool's first and last id, and the ids just outside");
    static_assert(!TxstHasDodt(0xE9F) && TxstHasDodt(0xEA0) && TxstHasDodt(0xEE7) && !TxstHasDodt(0xEE8) && !TxstHasDodt(0xEFF),
        "TXST DODT split");
    static_assert(AddnIndexOfSlot(0xF00) == 40000u && AddnIndexOfSlot(0xF0C) == 40012u && AddnIndexOfSlot(0xFCF) == 40207u &&
                      !AddnIndexOfSlot(0xEFF) && !AddnIndexOfSlot(0xFD0) && SlotOfAddnIndex(40000) == 0xF00 &&
                      SlotOfAddnIndex(40207) == 0xFCF && !SlotOfAddnIndex(39999) && !SlotOfAddnIndex(40208) &&
                      !SlotOfAddnIndex(0xFFFFFFFFu),
        "ADDN index = 40000 + place, 40000-40207");

    // =========================================================================
    // FNV-1a 64 and hex (the key, file hashes, the progress line)
    // =========================================================================

    inline constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ull;  // the same as NifDump::kFnvOffset
    inline constexpr std::uint64_t kFnvPrime = 0x100000001B3ull;
    inline constexpr std::size_t kHex16Digits = 16;

    /// FNV-1a 64 over a_bytes, continuing from a_hash. Every key is the hash of one delimited string, never a chain.
    [[nodiscard]] constexpr std::uint64_t Fnv64(std::string_view a_bytes, std::uint64_t a_hash = kFnvOffset) noexcept
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
    [[nodiscard]] constexpr std::optional<std::uint64_t> ParseHex16(std::string_view a_text) noexcept
    {
        if (!DataPathRule::IsCopyKey16(a_text)) {
            return std::nullopt;
        }
        std::uint64_t value = 0;
        for (const char c : a_text) {
            value = (value << 4) | static_cast<std::uint64_t>(c <= '9' ? c - '0' : c - 'a' + 10);
        }
        return value;
    }

    [[nodiscard]] constexpr bool IsKey16(std::string_view a_text) noexcept
    {
        return DataPathRule::IsCopyKey16(a_text);
    }

    static_assert(ParseHex16("0123456789abcdef") == 0x0123456789ABCDEFull && ParseHex16("ffffffffffffffff") == ~0ull &&
                      !ParseHex16("0123456789ABCDEF") && !ParseHex16("0123456789abcde") && !ParseHex16("0123456789abcdef0") &&
                      !ParseHex16("") && !ParseHex16("-123456789abcdef") && !ParseHex16("+123456789abcdef") &&
                      !ParseHex16(" 123456789abcdef") && !ParseHex16("0x23456789abcdef") && !ParseHex16("0123456789abcdeg"),
        "hex16: exactly 16 lower-case hex digits");

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
    // The tint text: the one form a tint takes in a key and in request.json
    // =========================================================================

    inline constexpr int kFloatDecimals = 6;
    // The range the builder takes for tint and tintAdd alike (tools/blackflame-greybuild GreyBuild.h kMinTint / kMaxTint,
    // checked by main.cpp ParseTint and GreyMap.cpp), so the DLL refuses the same values before it writes a request
    inline constexpr double kMinTint = 0.0;
    inline constexpr double kMaxTint = 2.0;

    /// a_value as std::to_chars fixed with 6 decimals (locale-free), "-0.000000" written as "0.000000"; nullopt for NaN,
    /// infinity or a value outside kMinTint..kMaxTint (checked before rounding: 2.0000004 is refused, -0.0 taken)
    [[nodiscard]] inline std::optional<std::string> TintText(double a_value, double a_min = kMinTint, double a_max = kMaxTint)
    {
        if (!std::isfinite(a_value) || !(a_value >= a_min && a_value <= a_max)) {
            return std::nullopt;
        }
        char buffer[32];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), a_value, std::chars_format::fixed, kFloatDecimals);
        if (result.ec != std::errc()) {
            return std::nullopt;
        }
        std::string text(buffer, result.ptr);
        if (text == "-0.000000") {
            text.erase(0, 1);
        }
        return text;
    }

    /// The tint the DLL writes into request.json and builds the key prefix from: TintText's decimal read back
    /// (std::from_chars), so the JSON number, the key text and what the builder renders are one value; -0 is 0
    [[nodiscard]] inline std::optional<double> QuantizedTint(double a_value, double a_min = kMinTint, double a_max = kMaxTint)
    {
        const auto text = TintText(a_value, a_min, a_max);
        if (!text) {
            return std::nullopt;
        }
        double value = 0.0;
        const auto result = std::from_chars(text->data(), text->data() + text->size(), value, std::chars_format::fixed);
        if (result.ec != std::errc() || result.ptr != text->data() + text->size()) {
            return std::nullopt;
        }
        return value == 0.0 ? 0.0 : value;
    }

    inline constexpr double kDefaultHandMarbleOpacity = 0.9;
    inline constexpr double kDefaultHandMarbleScale = 1.15;
    inline constexpr double kMinHandMarbleScale = 0.1;
    inline constexpr double kMaxHandMarbleScale = 4.0;

    [[nodiscard]] inline double ClampHandMarbleOpacity(double a_value)
    {
        return std::isfinite(a_value) ? QuantizedTint(std::clamp(a_value, 0.0, 1.0)).value() : kDefaultHandMarbleOpacity;
    }

    [[nodiscard]] inline double ClampHandMarbleScale(double a_value)
    {
        return std::isfinite(a_value) ? QuantizedTint(std::clamp(a_value, kMinHandMarbleScale, kMaxHandMarbleScale),
            kMinHandMarbleScale, kMaxHandMarbleScale).value() : kDefaultHandMarbleScale;
    }

    inline constexpr double kDefaultTintAddHands = 1.4;

    /// Config policy for hand cores: finite values clamp to 0..2, then use the key's six decimals.
    [[nodiscard]] inline double ClampTintAddHands(double a_value)
    {
        if (!std::isfinite(a_value)) {
            return kDefaultTintAddHands;
        }
        return QuantizedTint(a_value < kMinTint ? kMinTint : a_value > kMaxTint ? kMaxTint : a_value).value_or(kDefaultTintAddHands);
    }

    // =========================================================================
    // A file's state as a key string names it
    // =========================================================================

    inline constexpr std::string_view kSentinelNotDumped = "notdumped";  // outside the dump caps: notdumped:<size>
    inline constexpr std::string_view kSentinelMissing = "missing";      // does not open (or its path is refused)
    inline constexpr std::string_view kSentinelNone = "none";            // no file at all (an empty TXST slot, no ADDN part)

    // kUnset is the default: a FileHash nobody filled makes every key builder refuse (nullopt), never a valid key
    enum class FileState : std::uint8_t
    {
        kUnset = 0,
        kHashed,     // read whole: fnv64 is its bytes' FNV-1a 64
        kNotDumped,  // opened, outside the dump caps: only its size is known (the DLL does not read it)
        kMissing,    // does not open
    };

    // notdumped: content-free by design. A same-size replacement of a not-dumped file is not detected; that is safe
    // because the builder never reads a not-dumped file's content (--from-game reads only the dump folder, and the dump
    // removes a texture that is no longer planned: CopyRequestDump.cpp, "One not planned has its stale copy removed").
    struct FileHash
    {
        FileState state = FileState::kUnset;
        std::uint64_t fnv64 = 0;  // kHashed only
        std::uint64_t size = 0;   // kNotDumped only

        [[nodiscard]] static constexpr FileHash Hashed(std::uint64_t a_fnv64) noexcept { return { FileState::kHashed, a_fnv64, 0 }; }
        [[nodiscard]] static constexpr FileHash NotDumped(std::uint64_t a_size) noexcept { return { FileState::kNotDumped, 0, a_size }; }
        [[nodiscard]] static constexpr FileHash Missing() noexcept { return { FileState::kMissing, 0, 0 }; }
    };

    /// The value a key string writes for a_hash: 16 hex digits, notdumped:<size> or missing; nullopt for kUnset or a
    /// value no state has
    [[nodiscard]] inline std::optional<std::string> FileHashText(const FileHash& a_hash)
    {
        switch (a_hash.state) {
            case FileState::kHashed: return Hex16(a_hash.fnv64);
            case FileState::kNotDumped: return std::string(kSentinelNotDumped) + ":" + std::to_string(a_hash.size);
            case FileState::kMissing: return std::string(kSentinelMissing);
            case FileState::kUnset: break;
        }
        return std::nullopt;
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

    /// "derived" or "godskin"; "" for a value no mode has (the key builders refuse it)
    [[nodiscard]] constexpr std::string_view PaletteModeText(PaletteMode a_mode) noexcept
    {
        switch (a_mode) {
            case PaletteMode::kDerived: return kPaletteModeDerived;
            case PaletteMode::kGodskin: return kPaletteModeGodskin;
        }
        return "";
    }

    [[nodiscard]] constexpr std::optional<PaletteMode> PaletteModeFromText(std::string_view a_text) noexcept
    {
        if (a_text == kPaletteModeDerived) {
            return PaletteMode::kDerived;
        }
        if (a_text == kPaletteModeGodskin) {
            return PaletteMode::kGodskin;
        }
        return std::nullopt;
    }

    // request.json v5 (the DLL writes, the builder reads). Keys in the request are key16 text. Names are const char*
    // because both sides index nlohmann::json with them.
    namespace Request
    {
        inline constexpr std::uint32_t kVersion = 5;
        // Top level of the request (beside "version"), not in options: kContractVersion the DLL was built with
        inline constexpr const char* kContractVersion = "contractVersion";
        inline constexpr const char* kOptions = "options";          // { tint, tintAdd, paletteMode, keyPrefix }
        inline constexpr const char* kTint = "tint";                // number: QuantizedTint of the DLL's config value
        inline constexpr const char* kHandMarbleOpacity = "handMarbleOpacity";
        inline constexpr const char* kHandMarbleScale = "handMarbleScale";
        inline constexpr const char* kTintAddHands = "tintAddHands";  // casting-art object override only
        inline constexpr const char* kKeyString = "keyString";      // canonical casting-art object key, checked by builder
        inline constexpr const char* kCastingArtRole = "castingArt";
        inline constexpr const char* kTintAdd = "tintAdd";          // number: QuantizedTint as well
        inline constexpr const char* kPaletteMode = "paletteMode";  // "derived" | "godskin"
        // The key prefix the DLL built (KeyPrefixString). The builder rebuilds it from options, its compiled output
        // identity and the dumped palettes' hashes (VerifyKeyPrefix) and refuses the run when the two differ
        inline constexpr const char* kKeyPrefix = "keyPrefix";
        inline constexpr const char* kKey = "key";                  // a form's model key16, and each entry's below
        inline constexpr const char* kMuzzles = "muzzles";          // [{ path, key }]
        // [{ slot, index, path, key }]: index (required in v5) is the source ADDN's AddOnNode index the slot copies, so the
        // builder maps that index to the slot's (AddnIndexOfSlot) in the NIFs it remaps
        inline constexpr const char* kAddnMps = "addnMps";
        inline constexpr const char* kIndex = "index";              // a source ADDN's AddOnNode index (number)
        inline constexpr const char* kTxst = "txst";                // [{ slot, key, dodt }]
        inline constexpr const char* kPath = "path";
        inline constexpr const char* kSlot = "slot";                // a slot's local id (number)
        inline constexpr const char* kDodt = "dodt";                // bool
        // textures[] (the dump's list, CopyRequestData TexturesJson): each entry's "state" (required in v5) says what the
        // dump did with it, so neither side reads the error text; "error" stays for people
        inline constexpr const char* kTextures = "textures";
        inline constexpr const char* kTextureState = "state";
        inline constexpr std::string_view kTextureStateOk = "ok";               // in the dump: hashed (FileHash::Hashed)
        inline constexpr std::string_view kTextureStateNotDumped = "notdumped"; // opened, over a dump cap (NotDumped(size))
        inline constexpr std::string_view kTextureStateMissing = "missing";     // does not open (FileHash::Missing)
        inline constexpr std::string_view kTextureStateUnread = "unread";       // opened, its read failed: no copy this run
        inline constexpr const char* kTextureSize = "size";         // the stream's size (number), for notdumped:<size>
        // contract 6: [{ path, role, state, key (state ok only) }] (CopyContractEfsh.h EfshPalettesJson / ParseEfshPalettes)
        inline constexpr const char* kEfshPalettes = "efshPalettes";
        inline constexpr const char* kRole = "role";                // "particle" | "membrane" | "both"
        // contract 8: [{ path, role, state, key (state ok only) }], role "membrane-fill" | "particle-base" | "both"
        // (CopyContractEfshBase.h EfshBasesJson / ParseEfshBases)
        inline constexpr const char* kEfshBases = "efshBases";

        enum class TextureState : std::uint8_t
        {
            kOk,
            kNotDumped,
            kMissing,
            kUnread,
        };

        [[nodiscard]] constexpr std::string_view TextureStateText(TextureState a_state) noexcept
        {
            switch (a_state) {
                case TextureState::kOk: return kTextureStateOk;
                case TextureState::kNotDumped: return kTextureStateNotDumped;
                case TextureState::kMissing: return kTextureStateMissing;
                case TextureState::kUnread: return kTextureStateUnread;
            }
            return "";
        }

        [[nodiscard]] constexpr std::optional<TextureState> TextureStateFromText(std::string_view a_text) noexcept
        {
            for (const auto state : { TextureState::kOk, TextureState::kNotDumped, TextureState::kMissing, TextureState::kUnread }) {
                if (!a_text.empty() && TextureStateText(state) == a_text) {
                    return state;
                }
            }
            return std::nullopt;
        }
    }

    // Files both sides name
    inline constexpr const char* kStagingFolder = "staging";              // <SKSE log folder>\HeartOfMagic_BlackFlame\staging
    inline constexpr const char* kProgressFile = "progress.jsonl";        // in the staging folder
    inline constexpr const char* kBuilderExeRelative = "HeartOfMagic_BlackFlame\\blackflame-greybuild.exe";  // beside the DLL
}
