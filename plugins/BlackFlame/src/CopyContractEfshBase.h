#pragma once

// =============================================================================
// COPY CONTRACT EFSH BASE - request.json's efshBases[] (contract 8): the effect shaders' base textures the builder greys
// =============================================================================
// Black Flame v2.1 (build/fec-base/REPORT.md). A marked corpse's ShaderReferenceEffect still drew orange where its colour
// comes from a base texture, not a palette: the membrane fill (EFSH ICON, SRE +0xE0) of a shader whose membrane stage is
// on without greyscale-to-colour, and the particle base (ICO2, SRE +0xD0) of one whose particle stage is on without
// particle greyscale-to-colour. With the colour palette on, the base's green is the palette coordinate, so greying it
// would move the lookup: such a base is never listed and never swapped. Greyscale-to-alpha alone reads the base's alpha
// (Effect.hlsl), which the grey copy keeps exactly, so it does not exclude the base.
// The rule is generic: every TESEffectShader of the load order (SlotKeys, beside the palettes); no plugin or form named.
// The builder greys a listed base only when it passes its fire test (GreyKeyedBases.cpp) and writes it to its key's
// copy.dds (all mips, RGBA8, alpha kept exactly) with a dds progress line; one that is not fire gets no line.
// One entry shape with efshPalettes[] (CopyContractEfsh.h EfshPalette, merge, guard, writer and reader); the role bits
// read here as kMembrane = the membrane fill, kParticle = the particle base:
//   EfshBaseRoles     which base slots a shader's flags make the SRE take its colour from
//   EfshBaseKeyString <prefix>|kind=efshbase|grey=luma1|src=<path>|srchash=<h> (kEfshBaseRecipe; the role is not in it:
//                     one recipe, one copy)
//   KeyEfshBases / EfshBasesJson / ParseEfshBases  the palettes' functions over kEfshBaseList
// C++17, no game types (DLL, builder, cosave-check).

#include "CopyContractEfsh.h"

namespace BlackFlame::CopyContract
{
    [[nodiscard]] constexpr std::string_view EfshBaseRoleText(EfshRole a_role) noexcept
    {
        switch (a_role) {
            case EfshRole::kParticle: return "particle-base";
            case EfshRole::kMembrane: return "membrane-fill";
            case EfshRole::kBoth: return "both";
            case EfshRole::kNone: break;
        }
        return "";
    }

    [[nodiscard]] constexpr std::optional<EfshRole> EfshBaseRoleFromText(std::string_view a_text) noexcept
    {
        for (const auto role : { EfshRole::kParticle, EfshRole::kMembrane, EfshRole::kBoth }) {
            if (!a_text.empty() && EfshBaseRoleText(role) == a_text) {
                return role;
            }
        }
        return std::nullopt;
    }

    /// The base textures whose RGB a shader's flags make the SRE draw: the membrane fill (kMembrane) when the membrane
    /// stage is on and not greyscale to colour, the particle base (kParticle) the same with the particle flags. The DLL
    /// collects by it (SlotKeys) and hook A swaps by it again on each SRE's own shader (CorpseGrey)
    [[nodiscard]] constexpr EfshRole EfshBaseRoles(const EfshShaderFlags& a_flags) noexcept
    {
        const bool fill = !a_flags.disableTextureShader && !a_flags.greyscaleToColor;
        const bool particle = !a_flags.disableParticleShader && !a_flags.particleGreyscaleColor;
        return EfshRoleJoin(particle ? EfshRole::kParticle : EfshRole::kNone, fill ? EfshRole::kMembrane : EfshRole::kNone);
    }

    // The base copy's recipe in its key (grey=<this>): neither the golden fixture nor the output identity covers this kind,
    // so a change to what GreyKeyedBases.cpp writes for the same source (luminance, rounding, alpha, mips, format) bumps it
    // and every base copy is keyed and built anew. luma1: Rec. 709 luminance of the encoded bytes rounded to the nearest
    // byte, alpha bytes kept, every mip, R8G8B8A8_UNORM
    inline constexpr std::string_view kEfshBaseRecipe = "luma1";

    // Every base copy together at most (bytes on disk; RGBA8, so about the texture memory the DLL pins for the session):
    // the builder writes no new one past it (counted and logged) and the gate loads no more than it (2026-10-17: the
    // user's PC ran out of system commit). FEC's two fire bases take about 27 MiB
    inline constexpr std::uint64_t kMaxBaseCopyBytes = 128ull * 1024ull * 1024ull;

    /// An effect shader base texture's grey copy key string (contract 8): the recipe kEfshBaseRecipe (GreyKeyedBases.cpp);
    /// nullopt for a source Normalize refuses or a refused prefix
    [[nodiscard]] inline std::optional<std::string> EfshBaseKeyString(const KeyPrefix& a_prefix, const ChildKeyInput& a_input,
        std::string_view a_recipe = kEfshBaseRecipe)
    {
        return Detail::ChildKeyString(a_prefix, "efshbase", "grey", a_recipe, a_input);
    }

    inline constexpr EfshListSpec kEfshBaseList{ Request::kEfshBases, &EfshBaseRoleText, &EfshBaseRoleFromText,
        "membrane-fill, particle-base or both", "contract 8" };

    /// Each hashed base's key; the others' keys cleared
    inline void KeyEfshBases(std::vector<EfshPalette>& a_bases, const KeyPrefix& a_prefix)
    {
        KeyEfshEntries(a_bases, a_prefix, [](const KeyPrefix& a_p, const ChildKeyInput& a_i) { return EfshBaseKeyString(a_p, a_i); });
    }

    /// The request's efshBases array (throws as EfshPalettesJson does)
    template <class Json>
    [[nodiscard]] Json EfshBasesJson(const std::vector<EfshPalette>& a_bases)
    {
        return EfshListJson<Json>(kEfshBaseList, a_bases);
    }

    /// a_root[efshBases] read back; anything malformed (no array included) is an error
    template <class Json>
    [[nodiscard]] EfshParse ParseEfshBases(const Json& a_root)
    {
        return ParseEfshList(a_root, kEfshBaseList);
    }
}
