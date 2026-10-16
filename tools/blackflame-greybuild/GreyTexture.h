#pragma once

// =============================================================================
// GreyTexture - grey copies of the textures a copy NIF names (README "Texture copies")
// =============================================================================
// GreyEffects changes shader values only, so colour painted into a texture stays: an orange diffuse or glow map on a
// lighting-shaded mesh, or the source texture of an effect shader that keeps its own texture (not on the palette).
// The selection reads NIF data only (any load order): BSLightingShaderProperty slots 0 (diffuse) and 2 (glow), and
// BSEffectShaderProperty source textures UsesPalette rejects. Each texture is found in the --in folders (--from-game:
// the game's dump, which the DLL writes from the game's own file system), decoded with DirectXTex, every pixel set to
// its Rec. 709 luminance x the role's tint (alpha kept), encoded again in the source format with its mip count, and
// written to textures\heartofmagic\blackflame\copies\<path after textures\>; the slot then names the copy. One mean
// saturation under kGreyscaleSaturation (mip 0, visible pixels) means the texture is already grey: left as it is.
// The source file is never written. A texture that is missing, unreadable or fails leaves its slot as it was.

#include "GreyBuild.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nifly
{
    class NifFile;
}

namespace GreyBuild
{
    inline constexpr const char* kTexturesRoot = "textures\\";
    // Every texture copy lives here (a NIF slot names it with textures\ in front, as kPalettePath does)
    inline constexpr const char* kTextureCopyRoot = "textures\\heartofmagic\\blackflame\\copies\\";
    // Our own textures (the palettes, earlier copies): never recoloured again
    inline constexpr const char* kOwnTextureRoot = "textures\\heartofmagic\\blackflame\\";

    // The tint per role: a lighting shader is left alone by GreyEffects, so its diffuse carries the darkening (the DLL's
    // tintScale default, as kDefaultTint); a glow map is a brightness mask the shader's emissive colour multiplies, kept at
    // full range; an effect shader's base colour is already luminance x tint, so its texture is plain luminance
    inline constexpr float kTextureTintDiffuse = 0.6f;
    inline constexpr float kTextureTintGlow = 1.0f;
    inline constexpr float kTextureTintEffect = 1.0f;

    // Mean HSV saturation (0..1) of mip 0 below which a texture counts as already grey. BC1 quantisation alone leaves grey
    // texels at about 0.02 (5:6:5 endpoints), so the line sits above that
    inline constexpr float kGreyscaleSaturation = 0.05f;

    // BSShaderTextureSet slots
    inline constexpr std::uint32_t kSlotDiffuse = 0;
    inline constexpr std::uint32_t kSlotGlow = 2;

    enum class TextureRole
    {
        kDiffuse,
        kGlow,
        kEffectSource
    };

    [[nodiscard]] const char* TextureRoleName(TextureRole a_role) noexcept;
    [[nodiscard]] float TextureTint(TextureRole a_role) noexcept;

    /// A slot's text as a Data-relative texture path: lower case, '/' as '\', every leading "data\" and "textures\" dropped
    /// and one "textures\" put in front (the DLL's NormalizeTexturePath rule, so a slot finds the file the dump wrote). nullopt for an empty path, a drive or ':', a ".." token, or a name not ending in ".dds".
    [[nodiscard]] std::optional<std::string> TextureDataPath(const std::string& a_slot);

    /// The copy's path for a TextureDataPath result: kTextureCopyRoot + the part after textures\.
    [[nodiscard]] std::string TextureCopyPathFor(const std::string& a_textureDataPath);

    enum class RecolourOutcome
    {
        kRecoloured,
        kGreyscale,  // already grey (or one channel): nothing written
        kFailed      // unreadable, a format that cannot be done, or an encode / write failure: nothing written
    };

    struct RecolourResult
    {
        RecolourOutcome outcome = RecolourOutcome::kFailed;
        std::string reason;     // kFailed: why; kGreyscale: the measure
        std::string format;     // the source's DXGI format number and name, e.g. "77 BC3_UNORM"
        float saturation = 0.0f;
        std::size_t mips = 0;
        std::uint64_t bytes = 0;  // kRecoloured: the copy's file size
    };

    /// Reads a_source (DDS), and unless it is already grey writes its grey copy to a_target (folders made): luminance x
    /// a_tint per pixel, alpha kept, the same format, size, mip count and array size. Never writes a_source.
    [[nodiscard]] RecolourResult RecolourDds(const std::filesystem::path& a_source, const std::filesystem::path& a_target,
        float a_tint);

    struct TextureTotals
    {
        int slots = 0;        // texture slots the rule picked (every NIF, every use)
        int repointed = 0;    // of those, now naming a grey copy
        int recoloured = 0;   // distinct textures written
        int greyscale = 0;    // distinct textures already grey
        int failed = 0;       // distinct textures that could not be done (slot left)
        int missing = 0;      // distinct textures in no --in folder (not in the dump)
        std::uint64_t bytes = 0;  // the copies' size
    };

    /// One run's texture work: where textures are read and written, what is done so far (each texture once)
    struct TextureJob
    {
        std::vector<std::filesystem::path> inputs;   // searched in order, as for NIFs
        std::filesystem::path out;                   // the copies go to out / kTextureCopyRoot...
        std::map<std::string, std::string> notDumped;  // --from-game: texture -> why the game left it out of the dump
        struct Done
        {
            RecolourOutcome outcome = RecolourOutcome::kFailed;
            bool found = false;
            TextureRole role = TextureRole::kDiffuse;
            std::string copy;  // the slot's new text, "" when the slot stays
        };
        std::map<std::string, Done> done;  // by TextureDataPath
        TextureTotals totals;
    };

    /// Every texture slot of a_nif the rule picks (UsesPalette on the effect shaders as GreyEffects leaves them): each
    /// distinct texture recoloured once per job into a_job.out, each slot whose texture got a copy pointed at it. Prints
    /// one line per texture the first time it is met. Never throws for a texture; the NIF stays loadable as it was.
    void RetargetTextures(nifly::NifFile& a_nif, TextureJob& a_job, const std::string& a_nifRel);

    /// "textures  recoloured N, ..." for a run's end
    [[nodiscard]] std::string TextureTotalsLine(const TextureTotals& a_totals);
}
