#pragma once

// =============================================================================
// COPY CONTRACT KEYS - the canonical key string of every model object (included by CopyContract.h)
// =============================================================================
// Key = FNV-1a 64 of one string; key16 = its 16 lower-case hex digits. The string: fields joined with '|' (no spaces),
// each name=value, in the order the functions below write them. Paths are Data-relative, DataPathRule::Normalize'd
// (lower case, '\'); a path Normalize refuses goes in as <name>raw=<its bytes in lower-case hex> instead (a different
// field name, so no refused text can equal a normalised one). '|' never occurs in a normalised path or a plugin file
// name, so the string decodes one way. fnv64 values: 16 lower-case hex digits; floats: std::to_chars fixed, 6 decimals
// (locale-free); booleans 0/1; sentinels notdumped, missing, none.
//
// Encoding: the functions hash the bytes they are given. The DLL passes the engine's own bytes (record strings and
// TexturePathsIn text, the system code page); the builder's child keys hash its own text the same way every run.
// tint and tintAdd are doubles: both sides pass the value as request.json holds it (the DLL writes its config's double),
// never through a float on one side only, since to_chars rounds the exact binary value.
//
// Common prefix (KeyPrefixString):
//   format=BFK2|exe=<builder exe fnv64>|tint=<f>|tintadd=<f>|palette=<derived|godskin>|grad=<h>|gradadd=<h>
// NIF (form model, PROJ muzzle, ADDN MPS - one formula, the kind in the string):
//   <prefix>|kind=<model|muzzle|mps>|nif=<path>|nifhash=<h>|tex=<path>=<h>...|addn=<h|none>
//   tex: every TexturePathsIn path, normalised under textures\, sorted and each once.
//   addn: none for a NIF without the block type string "BSValueNode" (HasValueNodeBlock); else AddnTableHash.
// TXST: <prefix>|kind=txst|slot0=<hex>...|slot7=<hex>|tx00=<h>|tx03=<h>|dodt=<0|1>
//   slotN: TX0N's raw record bytes in lower-case hex (texturesRaw's bytes); tx00/tx03: the texture's state, none when
//   the slot is empty.
// Texture copy (builder): <prefix>|kind=texture|role=<diffuse|glow|effect>|src=<path>|srchash=<h>
// Palette copy (builder): <prefix>|kind=palette|blend=<add|normal>|src=<path>|srchash=<h>
// ADDN table (AddnTableHash's string):
//   format=BFA1|addn=<index>|src=<plugin>:<local id hex>|model=<path>|modelhash=<h>...|slot=<local id hex>|src=...
//   ADDNs by index, ledger pairs by slot; plugin names ASCII lower case; ids lower-case hex, no 0x, no padding.

#include "CopyContract.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace BlackFlame::CopyContract
{
    inline constexpr std::string_view kKeyFormat = "BFK2";
    inline constexpr std::string_view kAddnTableFormat = "BFA1";
    inline constexpr std::string_view kValueNodeBlockType = "BSValueNode";
    inline constexpr int kFloatDecimals = 6;
    inline constexpr std::size_t kTxstSlots = 8;
    inline constexpr std::size_t kTxstDiffuseSlot = 0;  // TX00
    inline constexpr std::size_t kTxstGlowSlot = 3;     // TX03

    enum class NifKind : std::uint8_t
    {
        kModel,   // a form's main model
        kMuzzle,  // a PROJ's muzzle flash model
        kMps,     // an ADDN slot's master particle system
    };

    [[nodiscard]] inline constexpr std::string_view NifKindText(NifKind a_kind) noexcept
    {
        switch (a_kind) {
            case NifKind::kMuzzle: return "muzzle";
            case NifKind::kMps: return "mps";
            case NifKind::kModel: break;
        }
        return "model";
    }

    enum class TextureRole : std::uint8_t
    {
        kDiffuse,
        kGlow,
        kEffectSource,
    };

    [[nodiscard]] inline constexpr std::string_view TextureRoleText(TextureRole a_role) noexcept
    {
        switch (a_role) {
            case TextureRole::kGlow: return "glow";
            case TextureRole::kEffectSource: return "effect";
            case TextureRole::kDiffuse: break;
        }
        return "diffuse";
    }

    enum class PaletteBlend : std::uint8_t
    {
        kAdd,
        kNormal,
    };

    [[nodiscard]] inline constexpr std::string_view PaletteBlendText(PaletteBlend a_blend) noexcept
    {
        return a_blend == PaletteBlend::kAdd ? "add" : "normal";
    }

    struct KeyPrefix
    {
        std::uint64_t builderExeFnv64 = 0;  // the builder exe's bytes
        double tint = 0.0;
        double tintAdd = 0.0;
        PaletteMode paletteMode = PaletteMode::kDerived;
        FileHash gradBlackFlame;     // textures\heartofmagic\blackflame\gradblackflame.dds
        FileHash gradBlackFlameAdd;  // textures\heartofmagic\blackflame\gradblackflameadd.dds
    };

    struct TextureRef
    {
        std::string path;  // as the NIF or record names it (normalised here)
        FileHash hash;
    };

    struct NifKeyInput
    {
        NifKind kind = NifKind::kModel;
        std::string path;  // as the record names it (normalised under meshes\ here)
        FileHash nif;
        std::vector<TextureRef> textures;  // TexturePathsIn of the NIF's bytes
        std::optional<std::uint64_t> addnTable;  // nullopt: no BSValueNode (addn=none); else AddnTableHash
    };

    struct TxstKeyInput
    {
        std::vector<std::string> slotsRaw;  // TX00..TX07 raw record bytes (exactly kTxstSlots; empty string = empty slot)
        FileHash tx00;                      // ignored when slotsRaw[0] is empty
        FileHash tx03;                      // ignored when slotsRaw[3] is empty
        bool dodt = false;
    };

    struct ChildKeyInput
    {
        std::string sourcePath;  // the source texture or palette (normalised under textures\ here)
        std::uint64_t sourceFnv64 = 0;
    };

    struct AddnRecord
    {
        std::uint32_t index = 0;          // its AddOnNode index
        std::string plugin;               // the source record's plugin file name
        std::uint32_t localId = 0;        // the source record's local id
        std::string model;                // its model path as the record stores it
        FileHash modelHash;
    };

    struct LedgerPair
    {
        std::uint16_t slot = 0;           // the ADDN slot's local id
        std::string plugin;               // the source it is bound to
        std::uint32_t localId = 0;
    };

    namespace Detail
    {
        /// fixed, kFloatDecimals digits, locale-free; nullopt for NaN or infinity
        [[nodiscard]] inline std::optional<std::string> FixedText(double a_value)
        {
            if (!std::isfinite(a_value)) {
                return std::nullopt;
            }
            char buffer[64];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), a_value, std::chars_format::fixed, kFloatDecimals);
            if (result.ec != std::errc()) {
                return std::nullopt;
            }
            return std::string(buffer, result.ptr);
        }

        [[nodiscard]] inline std::string HexId(std::uint32_t a_value)
        {
            char buffer[16];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), a_value, 16);
            return std::string(buffer, result.ptr);
        }

        inline void Field(std::string& a_out, std::string_view a_name, std::string_view a_value)
        {
            a_out.push_back('|');
            a_out.append(a_name).append("=").append(a_value);
        }

        /// name=<normalised path>, or <name>raw=<hex bytes> when Normalize refuses the text
        [[nodiscard]] inline std::string PathField(std::string_view a_name, std::string_view a_text, std::string_view a_root)
        {
            const auto path = DataPathRule::Normalize(a_text, a_root);
            return path ? std::string(a_name) + "=" + *path : std::string(a_name) + "raw=" + LowerHex(a_text);
        }
    }

    /// The common prefix, or nullopt when a tint is not finite
    [[nodiscard]] inline std::optional<std::string> KeyPrefixString(const KeyPrefix& a_prefix)
    {
        const auto tint = Detail::FixedText(a_prefix.tint);
        const auto tintAdd = Detail::FixedText(a_prefix.tintAdd);
        if (!tint || !tintAdd) {
            return std::nullopt;
        }
        std::string out = "format=" + std::string(kKeyFormat);
        Detail::Field(out, "exe", Hex16(a_prefix.builderExeFnv64));
        Detail::Field(out, "tint", *tint);
        Detail::Field(out, "tintadd", *tintAdd);
        Detail::Field(out, "palette", PaletteModeText(a_prefix.paletteMode));
        Detail::Field(out, "grad", FileHashText(a_prefix.gradBlackFlame));
        Detail::Field(out, "gradadd", FileHashText(a_prefix.gradBlackFlameAdd));
        return out;
    }

    /// The NIF key string. nullopt when the NIF path is refused by Normalize (no copy can be named) or a tint is not
    /// finite. Texture refs: normalised, sorted, each once (one texture named twice keys as once).
    [[nodiscard]] inline std::optional<std::string> NifKeyString(const KeyPrefix& a_prefix, const NifKeyInput& a_input)
    {
        auto out = KeyPrefixString(a_prefix);
        const auto nif = DataPathRule::Normalize(a_input.path, DataPathRule::kMeshesRoot);
        if (!out || !nif) {
            return std::nullopt;
        }
        Detail::Field(*out, "kind", NifKindText(a_input.kind));
        Detail::Field(*out, "nif", *nif);
        Detail::Field(*out, "nifhash", FileHashText(a_input.nif));
        std::vector<std::string> textures;
        textures.reserve(a_input.textures.size());
        for (const auto& texture : a_input.textures) {
            textures.push_back(Detail::PathField("tex", texture.path, DataPathRule::kTexturesRoot) + "=" +
                               FileHashText(texture.hash));
        }
        std::sort(textures.begin(), textures.end());
        textures.erase(std::unique(textures.begin(), textures.end()), textures.end());
        for (const auto& texture : textures) {
            out->push_back('|');
            out->append(texture);
        }
        Detail::Field(*out, "addn", a_input.addnTable ? Hex16(*a_input.addnTable) : std::string(kSentinelNone));
        return out;
    }

    /// The TXST key string; nullopt unless slotsRaw has exactly 8 entries (or a tint is not finite)
    [[nodiscard]] inline std::optional<std::string> TxstKeyString(const KeyPrefix& a_prefix, const TxstKeyInput& a_input)
    {
        auto out = KeyPrefixString(a_prefix);
        if (!out || a_input.slotsRaw.size() != kTxstSlots) {
            return std::nullopt;
        }
        Detail::Field(*out, "kind", "txst");
        for (std::size_t i = 0; i < kTxstSlots; ++i) {
            Detail::Field(*out, "slot" + std::to_string(i), LowerHex(a_input.slotsRaw[i]));
        }
        const auto state = [&](std::size_t a_slot, const FileHash& a_hash) {
            return a_input.slotsRaw[a_slot].empty() ? std::string(kSentinelNone) : FileHashText(a_hash);
        };
        Detail::Field(*out, "tx00", state(kTxstDiffuseSlot, a_input.tx00));
        Detail::Field(*out, "tx03", state(kTxstGlowSlot, a_input.tx03));
        Detail::Field(*out, "dodt", a_input.dodt ? "1" : "0");
        return out;
    }

    /// The builder's texture copy key string
    [[nodiscard]] inline std::optional<std::string> TextureKeyString(const KeyPrefix& a_prefix, TextureRole a_role,
        const ChildKeyInput& a_input)
    {
        auto out = KeyPrefixString(a_prefix);
        if (!out) {
            return std::nullopt;
        }
        Detail::Field(*out, "kind", "texture");
        Detail::Field(*out, "role", TextureRoleText(a_role));
        out->push_back('|');
        out->append(Detail::PathField("src", a_input.sourcePath, DataPathRule::kTexturesRoot));
        Detail::Field(*out, "srchash", Hex16(a_input.sourceFnv64));
        return out;
    }

    /// The builder's derived palette key string
    [[nodiscard]] inline std::optional<std::string> PaletteKeyString(const KeyPrefix& a_prefix, PaletteBlend a_blend,
        const ChildKeyInput& a_input)
    {
        auto out = KeyPrefixString(a_prefix);
        if (!out) {
            return std::nullopt;
        }
        Detail::Field(*out, "kind", "palette");
        Detail::Field(*out, "blend", PaletteBlendText(a_blend));
        out->push_back('|');
        out->append(Detail::PathField("src", a_input.sourcePath, DataPathRule::kTexturesRoot));
        Detail::Field(*out, "srchash", Hex16(a_input.sourceFnv64));
        return out;
    }

    /// The ADDN part's string: every ADDN record of the closure (by index) and every ADDN slot ledger pair (by slot)
    [[nodiscard]] inline std::string AddnTableString(std::vector<AddnRecord> a_records, std::vector<LedgerPair> a_pairs)
    {
        std::sort(a_records.begin(), a_records.end(), [](const AddnRecord& a, const AddnRecord& b) { return a.index < b.index; });
        std::sort(a_pairs.begin(), a_pairs.end(), [](const LedgerPair& a, const LedgerPair& b) { return a.slot < b.slot; });
        const auto source = [](const std::string& a_plugin, std::uint32_t a_localId) {
            return DataPathRule::LowerSlashes(a_plugin) + ":" + Detail::HexId(a_localId);
        };
        std::string out = "format=" + std::string(kAddnTableFormat);
        for (const auto& record : a_records) {
            Detail::Field(out, "addn", std::to_string(record.index));
            Detail::Field(out, "src", source(record.plugin, record.localId));
            out.push_back('|');
            out.append(Detail::PathField("model", record.model, DataPathRule::kMeshesRoot));
            Detail::Field(out, "modelhash", FileHashText(record.modelHash));
        }
        for (const auto& pair : a_pairs) {
            Detail::Field(out, "slot", Detail::HexId(pair.slot));
            Detail::Field(out, "src", source(pair.plugin, pair.localId));
        }
        return out;
    }

    [[nodiscard]] inline std::uint64_t AddnTableHash(const std::vector<AddnRecord>& a_records, const std::vector<LedgerPair>& a_pairs)
    {
        return Fnv64(AddnTableString(a_records, a_pairs));
    }

    /// A NIF whose bytes hold the block type string "BSValueNode" (the header's block type list): its key takes the ADDN
    /// part. A byte scan, no parser; a match elsewhere in the file only makes the key wider (a rebuild, never stale).
    [[nodiscard]] inline bool HasValueNodeBlock(std::string_view a_nifBytes) noexcept
    {
        return a_nifBytes.find(kValueNodeBlockType) != std::string_view::npos;
    }

    /// A key string's key and key16
    [[nodiscard]] inline std::uint64_t KeyOf(std::string_view a_keyString) noexcept
    {
        return Fnv64(a_keyString);
    }

    [[nodiscard]] inline std::string Key16Of(std::string_view a_keyString)
    {
        return Hex16(KeyOf(a_keyString));
    }
}
