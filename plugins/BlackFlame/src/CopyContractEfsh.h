#pragma once

// =============================================================================
// COPY CONTRACT EFSH - request.json's efshPalettes[] (contract 6): the effect shader palettes the builder greys
// =============================================================================
// The corpse rule (CorpseGrey, design v2 section 2) swaps a marked ShaderReferenceEffect's particle palette (NAM9) and
// membrane palette (NAM8) for grey copies. The DLL names every palette the load order's effect shaders use, the builder
// greys each one that passes its fire test (GreyKeyedEfsh.cpp) and writes it to its key's palette.dds with a dds progress
// line; a palette that is not fire gets no line (the builder's output counts it). Shared, so the request the builder's
// self-test reads is made by the very code the DLL writes it with:
//   EfshPaletteRoles   which palette slots a shader's flags make the SRE read (SlotKeys picks its record texts by it)
//   MergeEfshPalettes  the record texts (as TESTexture holds them) normalised under textures\, one entry per path with its
//                      roles joined, sorted; a path Normalize refuses, one with a byte outside printable ASCII (its JSON text
//                      would not hash back to the same bytes) and one under our own folder are left out and counted
//   TexturePlanOrder   the dump plan's path order: the NIF and TXST textures, then the palettes not already among them
//   KeyEfshPalettes   each hashed one's key: KeyOf(EfshPaletteKeyString(prefix, { path, fnv64 }))
//   EfshPalettesJson   [{ path, role, state, key }]: state ok (in the dump, hashed: key required), notdumped or missing
//                      (no key: nothing to build); DemoteUnwritableEfsh first turns an entry it would refuse
//                      (EfshEntryFault) into a missing one, so one slip never costs the whole request
//   ParseEfshPalettes  the builder's reader of the same array: anything malformed refuses the request (the DLL wrote it)
// The writer, reader and keying run over an EfshListSpec, so efshBases[] (contract 8, CopyContractEfshBase.h) shares them.
// C++17, no game types (DLL, builder, cosave-check).

#include "CopyContractKeys.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace BlackFlame::CopyContract
{
    inline constexpr std::string_view kEfshOwnFolder = "textures\\heartofmagic\\blackflame\\";  // ours: never a source

    // Which palette slots of the effect shaders name a path (bits; kBoth = both)
    enum class EfshRole : std::uint8_t
    {
        kNone = 0,
        kParticle = 1,  // NAM9, the particle shader's palette
        kMembrane = 2,  // NAM8, the membrane shader's palette
        kBoth = 3,
    };

    [[nodiscard]] constexpr std::string_view EfshRoleText(EfshRole a_role) noexcept
    {
        switch (a_role) {
            case EfshRole::kParticle: return "particle";
            case EfshRole::kMembrane: return "membrane";
            case EfshRole::kBoth: return "both";
            case EfshRole::kNone: break;
        }
        return "";
    }

    [[nodiscard]] constexpr std::optional<EfshRole> EfshRoleFromText(std::string_view a_text) noexcept
    {
        for (const auto role : { EfshRole::kParticle, EfshRole::kMembrane, EfshRole::kBoth }) {
            if (!a_text.empty() && EfshRoleText(role) == a_text) {
                return role;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr EfshRole EfshRoleJoin(EfshRole a_one, EfshRole a_other) noexcept
    {
        return static_cast<EfshRole>(static_cast<std::uint8_t>(a_one) | static_cast<std::uint8_t>(a_other));
    }

    /// a_set holds a_one's bit(s)
    [[nodiscard]] constexpr bool EfshRoleHas(EfshRole a_set, EfshRole a_one) noexcept
    {
        return a_one != EfshRole::kNone && (static_cast<std::uint8_t>(a_set) & static_cast<std::uint8_t>(a_one)) == static_cast<std::uint8_t>(a_one);
    }

    /// The effect shader flags the palette rule reads (RE::EffectShaderData::Flags, as plain bools: no game type here)
    struct EfshShaderFlags
    {
        bool disableParticleShader = false;
        bool particleGreyscaleColor = false;
        bool particleGreyscaleAlpha = false;
        bool disableTextureShader = false;
        bool greyscaleToColor = false;  // the membrane's
        bool greyscaleToAlpha = false;  // the membrane's
    };

    /// The palettes an effect shader's flags make the SRE read (design v2 C10, C11): the particle one (NAM9) when the particle
    /// shader is on and greyscale to colour or alpha; the membrane one (NAM8) the same with the texture shader's flags
    [[nodiscard]] constexpr EfshRole EfshPaletteRoles(const EfshShaderFlags& a_flags) noexcept
    {
        const bool particle = !a_flags.disableParticleShader && (a_flags.particleGreyscaleColor || a_flags.particleGreyscaleAlpha);
        const bool membrane = !a_flags.disableTextureShader && (a_flags.greyscaleToColor || a_flags.greyscaleToAlpha);
        return EfshRoleJoin(particle ? EfshRole::kParticle : EfshRole::kNone, membrane ? EfshRole::kMembrane : EfshRole::kNone);
    }

    /// One palette slot as a record holds it
    struct EfshPaletteSource
    {
        std::string text;  // TESTexture's text ("effects\gradients\x.dds", any case and separator)
        EfshRole role = EfshRole::kParticle;
    };

    struct EfshPalette
    {
        std::string path;            // Normalize'd under textures\ (printable ASCII)
        EfshRole role = EfshRole::kNone;
        FileHash hash;               // the DLL's read (kHashed: in the dump); the reader sets the state only
        std::optional<std::uint64_t> key;  // kHashed only
    };

    struct EfshMerge
    {
        std::vector<EfshPalette> palettes;  // sorted by path, each once
        std::size_t refused = 0;            // Normalize refused the text
        std::size_t nonAscii = 0;
        std::size_t own = 0;                // under our own folder (kEfshOwnFolder)
    };

    /// The record texts as request entries (empty texts skipped; see the header)
    template <class LeadByte = DataPathRule::SystemLeadByte>
    [[nodiscard]] EfshMerge MergeEfshPalettes(const std::vector<EfshPaletteSource>& a_sources, LeadByte a_isLeadByte = {})
    {
        EfshMerge out;
        std::vector<EfshPalette> all;
        for (const auto& source : a_sources) {
            if (source.text.empty() || EfshRoleText(source.role).empty()) {
                continue;  // no text, or no role (kNone): no slot to swap
            }
            auto path = DataPathRule::NormalizeWith(source.text, DataPathRule::kTexturesRoot, a_isLeadByte);
            if (!path) {
                ++out.refused;
            } else if (!Detail::IsPrintableAscii(*path)) {
                ++out.nonAscii;
            } else if (DataPathRule::StartsWith(*path, kEfshOwnFolder)) {
                ++out.own;
            } else {
                all.push_back({ std::move(*path), source.role, {}, std::nullopt });
            }
        }
        std::sort(all.begin(), all.end(), [](const EfshPalette& a, const EfshPalette& b) { return a.path < b.path; });
        for (auto& palette : all) {
            if (!out.palettes.empty() && out.palettes.back().path == palette.path) {
                out.palettes.back().role = EfshRoleJoin(out.palettes.back().role, palette.role);
            } else {
                out.palettes.push_back(std::move(palette));
            }
        }
        return out;
    }

    /// The dump plan's path order (CopyRequestData PlanTextures): each tier's texts under textures\, refused and our own
    /// (kEfshOwnFolder) left out, sorted, each once, without a path an earlier tier has; the tiers one after another (the
    /// NIF and TXST textures, contract 6 the palettes, contract 8 the base textures). Planned in this order, a later tier
    /// takes only what the caps leave and never pushes an earlier one out of the dump, and no path is planned twice
    template <class LeadByte = DataPathRule::SystemLeadByte>
    [[nodiscard]] std::vector<std::string> TexturePlanTiers(const std::vector<std::vector<std::string>>& a_tiers, LeadByte a_isLeadByte = {})
    {
        std::set<std::string> seen;
        std::vector<std::string> out;
        for (const auto& tier : a_tiers) {
            std::set<std::string> paths;
            for (const auto& text : tier) {
                auto path = DataPathRule::NormalizeWith(text, DataPathRule::kTexturesRoot, a_isLeadByte);
                if (path && !DataPathRule::StartsWith(*path, kEfshOwnFolder) && seen.count(*path) == 0) {
                    paths.insert(std::move(*path));
                }
            }
            seen.insert(paths.begin(), paths.end());
            out.insert(out.end(), paths.begin(), paths.end());
        }
        return out;
    }

    /// TexturePlanTiers of two tiers: a_first (the NIF and TXST textures), then a_late (the palettes)
    template <class LeadByte = DataPathRule::SystemLeadByte>
    [[nodiscard]] std::vector<std::string> TexturePlanOrder(const std::vector<std::string>& a_first, const std::vector<std::string>& a_late,
        LeadByte a_isLeadByte = {})
    {
        return TexturePlanTiers({ a_first, a_late }, a_isLeadByte);
    }

    /// Why a_palette cannot be written as an efshPalettes entry (no state, no role, or a key that does not go with its state:
    /// a key exactly when hashed), or nullopt. EfshPalettesJson refuses such an entry
    [[nodiscard]] inline std::optional<std::string> EfshEntryFault(const EfshPalette& a_palette)
    {
        if (a_palette.hash.state == FileState::kUnset) {
            return "no state (its hash was never set)";
        }
        if (EfshRoleText(a_palette.role).empty()) {
            return "no role";
        }
        if ((a_palette.hash.state == FileState::kHashed) != a_palette.key.has_value()) {
            return a_palette.key ? "a key, yet not hashed" : "hashed, yet no key";
        }
        return std::nullopt;
    }

    /// The DLL's guard before EfshPalettesJson: an entry with a fault (EfshEntryFault) is written as missing (no key, nothing
    /// to build) or, with no role, left out; returns one "<path>: <why>" a demoted or dropped entry, for the log
    [[nodiscard]] inline std::vector<std::string> DemoteUnwritableEfsh(std::vector<EfshPalette>& a_palettes)
    {
        std::vector<std::string> notes;
        std::vector<EfshPalette> kept;
        for (auto& palette : a_palettes) {
            if (const auto fault = EfshEntryFault(palette)) {
                if (EfshRoleText(palette.role).empty()) {
                    notes.push_back(palette.path + ": " + *fault + " - left out");
                    continue;
                }
                notes.push_back(palette.path + ": " + *fault + " - written as missing");
                palette.hash = FileHash::Missing();
                palette.key.reset();
            }
            kept.push_back(std::move(palette));
        }
        a_palettes = std::move(kept);
        return notes;
    }

    using EfshKeyStringFn = std::optional<std::string> (*)(const KeyPrefix&, const ChildKeyInput&);

    /// Each hashed entry's key by a_keyString; the others' keys cleared
    inline void KeyEfshEntries(std::vector<EfshPalette>& a_entries, const KeyPrefix& a_prefix, EfshKeyStringFn a_keyString)
    {
        for (auto& entry : a_entries) {
            entry.key.reset();
            if (entry.hash.state == FileState::kHashed) {
                if (const auto text = a_keyString(a_prefix, { entry.path, entry.hash.fnv64 })) {
                    entry.key = KeyOf(*text);
                }
            }
        }
    }

    /// Each hashed palette's key; the others' keys cleared
    inline void KeyEfshPalettes(std::vector<EfshPalette>& a_palettes, const KeyPrefix& a_prefix)
    {
        KeyEfshEntries(a_palettes, a_prefix, &EfshPaletteKeyString);
    }

    /// One request array of effect shader entries (efshPalettes, contract 6; efshBases, contract 8): its name and how its
    /// roles are written. Both arrays share one entry shape, writer and reader
    struct EfshListSpec
    {
        const char* name;
        std::string_view (*roleText)(EfshRole) noexcept;
        std::optional<EfshRole> (*roleFromText)(std::string_view) noexcept;
        const char* roleHelp;  // the reader's refusal: "role must be <roleHelp>, ..."
        const char* since;     // "contract 6"
    };

    inline constexpr EfshListSpec kEfshPaletteList{ Request::kEfshPalettes, &EfshRoleText, &EfshRoleFromText,
        "particle, membrane or both", "contract 6" };

    namespace Detail
    {
        [[nodiscard]] inline std::string_view EfshStateText(FileState a_state) noexcept
        {
            switch (a_state) {
                case FileState::kHashed: return Request::kTextureStateOk;
                case FileState::kNotDumped: return Request::kTextureStateNotDumped;
                case FileState::kMissing: return Request::kTextureStateMissing;
                case FileState::kUnset: break;
            }
            return "";
        }
    }

    /// The request's efshPalettes array (a_Json: nlohmann::ordered_json in the DLL, nlohmann::json in the tests). Throws
    /// std::runtime_error for an entry no reader would take (an unset hash, a hashed one without a key): the DLL's slip,
    /// never written
    template <class Json>
    [[nodiscard]] Json EfshListJson(const EfshListSpec& a_spec, const std::vector<EfshPalette>& a_palettes)
    {
        Json list = Json::array();
        for (const auto& palette : a_palettes) {
            if (const auto fault = EfshEntryFault(palette)) {
                throw std::runtime_error(std::string(a_spec.name) + ": " + palette.path + ": " + *fault);
            }
            const auto state = Detail::EfshStateText(palette.hash.state);
            const auto role = a_spec.roleText(palette.role);
            Json item = Json::object();
            item[Request::kPath] = palette.path;
            item[Request::kRole] = std::string(role);
            item[Request::kTextureState] = std::string(state);
            if (palette.key) {
                item[Request::kKey] = Hex16(*palette.key);
            }
            list.push_back(std::move(item));
        }
        return list;
    }

    template <class Json>
    [[nodiscard]] Json EfshPalettesJson(const std::vector<EfshPalette>& a_palettes)
    {
        return EfshListJson<Json>(kEfshPaletteList, a_palettes);
    }

    struct EfshParse
    {
        std::vector<EfshPalette> palettes;  // hash: its state only (the builder hashes the dump itself)
        std::string error;                  // "" when read
    };

    /// a_root[a_spec.name] read back (see the header); error names the entry and field
    template <class Json>
    [[nodiscard]] EfshParse ParseEfshList(const Json& a_root, const EfshListSpec& a_spec)
    {
        EfshParse out;
        const auto fail = [&](const std::string& a_why) {
            out.palettes.clear();
            out.error = a_why;
            return out;
        };
        if (!a_root.is_object() || !a_root.contains(a_spec.name) || !a_root[a_spec.name].is_array()) {
            return fail("'" + std::string(a_spec.name) + "' must be an array (" + a_spec.since + ")");
        }
        std::set<std::string> seen;
        const auto& list = a_root[a_spec.name];
        for (std::size_t i = 0; i < list.size(); ++i) {
            const auto& item = list[i];
            const std::string where = std::string(a_spec.name) + "[" + std::to_string(i) + "]";
            const auto text = [&](const char* a_name) -> std::optional<std::string> {
                if (!item.is_object() || !item.contains(a_name) || !item[a_name].is_string()) {
                    return std::nullopt;
                }
                return item[a_name].template get<std::string>();
            };
            const auto path = text(Request::kPath);
            const auto normal = path ? DataPathRule::Normalize(*path, DataPathRule::kTexturesRoot) : std::nullopt;
            if (!normal || *normal != *path || !Detail::IsPrintableAscii(*path) || DataPathRule::StartsWith(*path, kEfshOwnFolder)) {
                return fail(where + ": path must be a normalised printable ASCII texture path outside our folder");
            }
            if (!seen.insert(*path).second) {
                return fail(where + ": " + *path + " is listed twice");
            }
            const auto roleText = text(Request::kRole);
            const auto role = roleText ? a_spec.roleFromText(*roleText) : std::nullopt;
            const auto stateText = text(Request::kTextureState);
            const auto state = stateText ? Request::TextureStateFromText(*stateText) : std::nullopt;
            if (!role || !state || *state == Request::TextureState::kUnread) {
                return fail(where + ": role must be " + a_spec.roleHelp + ", state ok, notdumped or missing");
            }
            EfshPalette palette{ *path, *role, {}, std::nullopt };
            palette.hash.state = *state == Request::TextureState::kOk ? FileState::kHashed :
                                 *state == Request::TextureState::kNotDumped ? FileState::kNotDumped : FileState::kMissing;
            const auto keyText = text(Request::kKey);
            const bool hasKey = item.contains(Request::kKey);
            if (hasKey != (palette.hash.state == FileState::kHashed) || (hasKey && !(keyText && ParseHex16(*keyText)))) {
                return fail(where + ": key (16 lower-case hex digits) is given exactly when state is ok");
            }
            if (keyText) {
                palette.key = ParseHex16(*keyText);
            }
            out.palettes.push_back(std::move(palette));
        }
        return out;
    }

    template <class Json>
    [[nodiscard]] EfshParse ParseEfshPalettes(const Json& a_root)
    {
        return ParseEfshList(a_root, kEfshPaletteList);
    }
}
