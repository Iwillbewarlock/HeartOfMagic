#pragma once

// =============================================================================
// COPY CONTRACT KEYS - the canonical key string of every model object
// =============================================================================
// Key = FNV-1a 64 of one string; key16 = its 16 lower-case hex digits. The string: fields joined with '|' (no spaces),
// each name=value, in the order the functions below write them.
// Paths: Data-relative, DataPathRule::Normalize'd (lower case, '\'). A normalised path of printable ASCII goes in as
// <name>=<path>; one with any other byte (a code page's characters) as <name>hex=<lower-case hex of the normalised
// bytes>, because a double-byte character's trail byte may be '|' or '='; a path Normalize refuses as
// <name>raw=<lower-case hex of its own bytes> (texture and ADDN model paths; a NIF or child source path that Normalize
// refuses gives no key at all: no copy can be made of it). So '|' only ever separates fields and the string decodes
// one way. fnv64 values: 16 lower-case hex digits; floats: TintText (to_chars fixed, 6 decimals, |x| <= 16, -0 as 0);
// booleans 0/1; sentinels notdumped:<size>, missing, none.
// Refusal (nullopt): an unset FileHash (FileState::kUnset), an unknown enum value, a tint TintText refuses, one path
// named twice with two different states, a local id over 0xFFFFFF.
//
// Encoding: the functions hash the bytes they are given. The DLL passes the engine's own bytes (record strings and
// TexturePathsIn text, the system code page); the builder's child keys hash its own text the same way every run.
// tint and tintAdd: the DLL passes QuantizedTint values and writes the same numbers into request.json; the builder
// passes the numbers it read back, and VerifyKeyPrefix checks the rebuilt prefix against the DLL's keyPrefix text.
//
// Common prefix (KeyPrefixString):
//   format=BFK3|exe=<builder exe fnv64>|tint=<f>|tintadd=<f>|palette=<derived|godskin>|grad=<h>|gradadd=<h>
// NIF (form model, PROJ muzzle, ADDN MPS - one formula, the kind in the string):
//   <prefix>|kind=<model|muzzle|mps>|nif=<path>|nifhash=<h>|tex=<path>=<h>...|addn=<h|none>
//   tex: every TexturePathsIn path, normalised under textures\, sorted by its field text and each once.
//   addn: none for a NIF without the block type string "BSValueNode" (HasValueNodeBlock); else AddnTableHash.
// TXST: <prefix>|kind=txst|slot0=<hex>...|slot7=<hex>|tx00=<h>|tx03=<h>|dodt=<0|1>
//   slotN: TX0N's raw record bytes in lower-case hex (texturesRaw's bytes); tx00/tx03: the texture's state, none when
//   the slot is empty.
// Texture copy (builder): <prefix>|kind=texture|role=<diffuse|glow|effect>|src=<path>|srchash=<h>
// Palette copy (builder): <prefix>|kind=palette|blend=<add|normal>|src=<path>|srchash=<h>
// ADDN table (AddnTableString):
//   format=BFA2|addn=<index>|src=<plugin>:<local id hex>|model=<path>|modelhash=<h>...|slot=<local id hex>|src=...
//   ADDN records sorted by (index, plugin, local id, model field, model hash), ledger pairs by (slot, plugin, local id),
//   so the same set in any order gives one string. plugin: ASCII lower case only (other bytes kept); a name with a
//   byte outside printable ASCII or one of |:=/\ goes in as srchex=<lower-case hex>:<id>. Ids lower-case hex, no 0x.

#include "CopyContract.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace BlackFlame::CopyContract
{
    inline constexpr std::string_view kKeyFormat = "BFK3";
    inline constexpr std::string_view kAddnTableFormat = "BFA2";
    inline constexpr std::string_view kValueNodeBlockType = "BSValueNode";
    inline constexpr std::size_t kTxstSlots = 8;
    inline constexpr std::size_t kTxstDiffuseSlot = 0;  // TX00
    inline constexpr std::size_t kTxstGlowSlot = 3;     // TX03
    inline constexpr std::uint32_t kMaxLocalId = 0xFFFFFF;

    enum class NifKind : std::uint8_t
    {
        kModel,   // a form's main model
        kMuzzle,  // a PROJ's muzzle flash model
        kMps,     // an ADDN slot's master particle system
    };

    [[nodiscard]] constexpr std::string_view NifKindText(NifKind a_kind) noexcept
    {
        switch (a_kind) {
            case NifKind::kModel: return "model";
            case NifKind::kMuzzle: return "muzzle";
            case NifKind::kMps: return "mps";
        }
        return "";
    }

    enum class TextureRole : std::uint8_t
    {
        kDiffuse,
        kGlow,
        kEffectSource,
    };

    [[nodiscard]] constexpr std::string_view TextureRoleText(TextureRole a_role) noexcept
    {
        switch (a_role) {
            case TextureRole::kDiffuse: return "diffuse";
            case TextureRole::kGlow: return "glow";
            case TextureRole::kEffectSource: return "effect";
        }
        return "";
    }

    enum class PaletteBlend : std::uint8_t
    {
        kAdd,
        kNormal,
    };

    [[nodiscard]] constexpr std::string_view PaletteBlendText(PaletteBlend a_blend) noexcept
    {
        switch (a_blend) {
            case PaletteBlend::kAdd: return "add";
            case PaletteBlend::kNormal: return "normal";
        }
        return "";
    }

    struct KeyPrefix
    {
        std::uint64_t builderExeFnv64 = 0;  // the builder exe's bytes
        double tint = 0.0;                  // QuantizedTint values
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

    // A builder child: made only after the builder read the source's bytes, so its hash is always a real FNV
    struct ChildKeyInput
    {
        std::string sourcePath;  // the source texture or palette (normalised under textures\ here)
        std::uint64_t sourceFnv64 = 0;
    };

    struct AddnRecord
    {
        std::uint32_t index = 0;          // its AddOnNode index
        std::string plugin;               // the source record's plugin file name
        std::uint32_t localId = 0;        // the source record's local id (no load order byte; at most 0xFFFFFF)
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

        [[nodiscard]] inline bool IsPrintableAscii(std::string_view a_text) noexcept
        {
            return std::all_of(a_text.begin(), a_text.end(), [](char c) {
                return static_cast<unsigned char>(c) >= DataPathRule::kFirstPrintable &&
                       static_cast<unsigned char>(c) < DataPathRule::kDelete;
            });
        }

        /// The field text of a path: name=<path>, <name>hex=<hex> (non-ASCII), <name>raw=<hex> (refused; nullopt when
        /// a_refuseRaw)
        [[nodiscard]] inline std::optional<std::string> PathField(std::string_view a_name, std::string_view a_text,
            std::string_view a_root, bool a_refuseRaw)
        {
            const auto path = DataPathRule::Normalize(a_text, a_root);
            if (!path) {
                return a_refuseRaw ? std::nullopt : std::optional<std::string>(std::string(a_name) + "raw=" + LowerHex(a_text));
            }
            return IsPrintableAscii(*path) ? std::string(a_name) + "=" + *path : std::string(a_name) + "hex=" + LowerHex(*path);
        }

        /// <plugin>:<id> with the plugin ASCII-lowered, or srchex form; the whole field "src=..." / "srchex=..."
        [[nodiscard]] inline std::optional<std::string> SourceField(std::string_view a_plugin, std::uint32_t a_localId)
        {
            if (a_plugin.empty() || a_localId > kMaxLocalId) {
                return std::nullopt;
            }
            std::string lowered(a_plugin);
            for (char& c : lowered) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            const bool plain = IsPrintableAscii(lowered) && lowered.find_first_of("|:=/\\") == std::string::npos;
            return (plain ? "src=" + lowered : "srchex=" + LowerHex(lowered)) + ":" + HexId(a_localId);
        }

        [[nodiscard]] inline std::optional<std::string> TintPair(const KeyPrefix& a_prefix)
        {
            const auto tint = TintText(a_prefix.tint);
            const auto tintAdd = TintText(a_prefix.tintAdd);
            if (!tint || !tintAdd) {
                return std::nullopt;
            }
            return "tint=" + *tint + "|tintadd=" + *tintAdd;
        }
    }

    /// The common prefix; nullopt for a tint TintText refuses, an unknown palette mode or an unset palette hash
    [[nodiscard]] inline std::optional<std::string> KeyPrefixString(const KeyPrefix& a_prefix)
    {
        const auto tints = Detail::TintPair(a_prefix);
        const auto palette = PaletteModeText(a_prefix.paletteMode);
        const auto grad = FileHashText(a_prefix.gradBlackFlame);
        const auto gradAdd = FileHashText(a_prefix.gradBlackFlameAdd);
        if (!tints || palette.empty() || !grad || !gradAdd) {
            return std::nullopt;
        }
        std::string out = "format=" + std::string(kKeyFormat);
        Detail::Field(out, "exe", Hex16(a_prefix.builderExeFnv64));
        out.push_back('|');
        out.append(*tints);
        Detail::Field(out, "palette", palette);
        Detail::Field(out, "grad", *grad);
        Detail::Field(out, "gradadd", *gradAdd);
        return out;
    }

    /// The builder's check of request v5 options: the prefix rebuilt from what it received (tint, tintAdd, paletteMode
    /// text) and what it measured itself (its exe's FNV, the dumped palettes' hashes) must be the DLL's keyPrefix text
    /// exactly; false (refuse the run) on any difference or an option it cannot read
    [[nodiscard]] inline bool VerifyKeyPrefix(std::string_view a_received, double a_tint, double a_tintAdd,
        std::string_view a_paletteMode, std::uint64_t a_builderExeFnv64, const FileHash& a_grad, const FileHash& a_gradAdd)
    {
        const auto mode = PaletteModeFromText(a_paletteMode);
        if (!mode) {
            return false;
        }
        const KeyPrefix rebuilt{ a_builderExeFnv64, a_tint, a_tintAdd, *mode, a_grad, a_gradAdd };
        const auto text = KeyPrefixString(rebuilt);
        return text && *text == a_received;
    }

    /// The NIF key string. nullopt when the NIF path is refused by Normalize (no copy can be named), for an unknown kind,
    /// an unset hash, or one texture path given twice with two different states. Texture refs: normalised, sorted by
    /// their field text, each once.
    [[nodiscard]] inline std::optional<std::string> NifKeyString(const KeyPrefix& a_prefix, const NifKeyInput& a_input)
    {
        auto out = KeyPrefixString(a_prefix);
        const auto nif = Detail::PathField("nif", a_input.path, DataPathRule::kMeshesRoot, true);
        const auto nifHash = FileHashText(a_input.nif);
        const auto kind = NifKindText(a_input.kind);
        if (!out || !nif || !nifHash || kind.empty()) {
            return std::nullopt;
        }
        Detail::Field(*out, "kind", kind);
        out->push_back('|');
        out->append(*nif);
        Detail::Field(*out, "nifhash", *nifHash);
        std::vector<std::pair<std::string, std::string>> textures;  // (field text, state)
        textures.reserve(a_input.textures.size());
        for (const auto& texture : a_input.textures) {
            auto field = Detail::PathField("tex", texture.path, DataPathRule::kTexturesRoot, false);
            auto state = FileHashText(texture.hash);
            if (!field || !state) {
                return std::nullopt;
            }
            textures.emplace_back(std::move(*field), std::move(*state));
        }
        std::sort(textures.begin(), textures.end());
        for (std::size_t i = 0; i < textures.size(); ++i) {
            if (i > 0 && textures[i].first == textures[i - 1].first) {
                if (textures[i].second != textures[i - 1].second) {
                    return std::nullopt;  // one texture, two states: the caller's lists disagree
                }
                continue;
            }
            out->push_back('|');
            out->append(textures[i].first).append("=").append(textures[i].second);
        }
        Detail::Field(*out, "addn", a_input.addnTable ? Hex16(*a_input.addnTable) : std::string(kSentinelNone));
        return out;
    }

    /// The TXST key string; nullopt unless slotsRaw has exactly 8 entries, or when a non-empty TX00 / TX03 has an unset
    /// hash (or the prefix is refused)
    [[nodiscard]] inline std::optional<std::string> TxstKeyString(const KeyPrefix& a_prefix, const TxstKeyInput& a_input)
    {
        auto out = KeyPrefixString(a_prefix);
        if (!out || a_input.slotsRaw.size() != kTxstSlots) {
            return std::nullopt;
        }
        const auto state = [&](std::size_t a_slot, const FileHash& a_hash) {
            return a_input.slotsRaw[a_slot].empty() ? std::optional<std::string>(std::string(kSentinelNone)) : FileHashText(a_hash);
        };
        const auto tx00 = state(kTxstDiffuseSlot, a_input.tx00);
        const auto tx03 = state(kTxstGlowSlot, a_input.tx03);
        if (!tx00 || !tx03) {
            return std::nullopt;
        }
        Detail::Field(*out, "kind", "txst");
        for (std::size_t i = 0; i < kTxstSlots; ++i) {
            Detail::Field(*out, "slot" + std::to_string(i), LowerHex(a_input.slotsRaw[i]));
        }
        Detail::Field(*out, "tx00", *tx00);
        Detail::Field(*out, "tx03", *tx03);
        Detail::Field(*out, "dodt", a_input.dodt ? "1" : "0");
        return out;
    }

    namespace Detail
    {
        [[nodiscard]] inline std::optional<std::string> ChildKeyString(const KeyPrefix& a_prefix, std::string_view a_kind,
            std::string_view a_variantName, std::string_view a_variant, const ChildKeyInput& a_input)
        {
            auto out = KeyPrefixString(a_prefix);
            const auto source = PathField("src", a_input.sourcePath, DataPathRule::kTexturesRoot, true);
            if (!out || !source || a_variant.empty()) {
                return std::nullopt;
            }
            Field(*out, "kind", a_kind);
            Field(*out, a_variantName, a_variant);
            out->push_back('|');
            out->append(*source);
            Field(*out, "srchash", Hex16(a_input.sourceFnv64));
            return out;
        }
    }

    /// The builder's texture copy key string; nullopt for a source Normalize refuses or an unknown role
    [[nodiscard]] inline std::optional<std::string> TextureKeyString(const KeyPrefix& a_prefix, TextureRole a_role,
        const ChildKeyInput& a_input)
    {
        return Detail::ChildKeyString(a_prefix, "texture", "role", TextureRoleText(a_role), a_input);
    }

    /// The builder's derived palette key string; nullopt for a source Normalize refuses or an unknown blend
    [[nodiscard]] inline std::optional<std::string> PaletteKeyString(const KeyPrefix& a_prefix, PaletteBlend a_blend,
        const ChildKeyInput& a_input)
    {
        return Detail::ChildKeyString(a_prefix, "palette", "blend", PaletteBlendText(a_blend), a_input);
    }

    /// The ADDN part's string: every ADDN record of the closure and every ADDN slot ledger pair, in one total order;
    /// nullopt for a local id over 0xFFFFFF, an empty plugin name or an unset model hash
    [[nodiscard]] inline std::optional<std::string> AddnTableString(const std::vector<AddnRecord>& a_records,
        const std::vector<LedgerPair>& a_pairs)
    {
        using RecordRow = std::tuple<std::uint32_t, std::string, std::string, std::string>;  // index, src, model, hash
        std::vector<RecordRow> records;
        for (const auto& record : a_records) {
            auto source = Detail::SourceField(record.plugin, record.localId);
            auto model = Detail::PathField("model", record.model, DataPathRule::kMeshesRoot, false);
            auto hash = FileHashText(record.modelHash);
            if (!source || !model || !hash) {
                return std::nullopt;
            }
            records.emplace_back(record.index, std::move(*source), std::move(*model), std::move(*hash));
        }
        using PairRow = std::pair<std::uint16_t, std::string>;  // slot, src
        std::vector<PairRow> pairs;
        for (const auto& pair : a_pairs) {
            auto source = Detail::SourceField(pair.plugin, pair.localId);
            if (!source) {
                return std::nullopt;
            }
            pairs.emplace_back(pair.slot, std::move(*source));
        }
        std::stable_sort(records.begin(), records.end());
        std::stable_sort(pairs.begin(), pairs.end());
        std::string out = "format=" + std::string(kAddnTableFormat);
        for (const auto& [index, source, model, hash] : records) {
            Detail::Field(out, "addn", std::to_string(index));
            out.append("|").append(source).append("|").append(model);
            Detail::Field(out, "modelhash", hash);
        }
        for (const auto& [slot, source] : pairs) {
            Detail::Field(out, "slot", Detail::HexId(slot));
            out.append("|").append(source);
        }
        return out;
    }

    [[nodiscard]] inline std::optional<std::uint64_t> AddnTableHash(const std::vector<AddnRecord>& a_records,
        const std::vector<LedgerPair>& a_pairs)
    {
        const auto text = AddnTableString(a_records, a_pairs);
        return text ? std::optional<std::uint64_t>(Fnv64(*text)) : std::nullopt;
    }

    /// A NIF whose bytes hold the block type string "BSValueNode" (the header's block type list): its key takes the ADDN
    /// part. A plain byte scan by design (v3.2 section 3), no parser: a match elsewhere in the file only makes the key
    /// wider (a rebuild, never stale).
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
