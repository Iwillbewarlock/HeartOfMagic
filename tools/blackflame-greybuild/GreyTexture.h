#pragma once

// =============================================================================
// GreyTexture - grey copies of the textures a copy NIF names (README "Texture copies")
// =============================================================================
// GreyEffects changes shader values only, so colour painted into a texture stays: an orange diffuse or glow map on a
// lighting-shaded mesh, or the source texture of an effect shader that keeps its own texture (not on the palette).
// The selection reads NIF data only (any load order): BSLightingShaderProperty slot 0 (diffuse), slot 2 only on a glow
// shader (glow map type or the SLSF2 glow flag; on other types slot 2 is a skin or detail map), and BSEffectShaderProperty
// source textures UsesPalette rejects. Each texture is found in the --in folders (--from-game: the game's dump, which
// the DLL writes from the game's own file system), decoded with DirectXTex, every pixel set to its Rec. 709 luminance x
// the role's tint (alpha kept), encoded again in the source format with its mip count, and written under
// textures\heartofmagic\blackflame\copies\_tint\<percent>\ (every tint, 1 included); the slot then names
// the copy. A glow shader's emissive colour is greyed too (a grey glow map under an orange emissive colour stays
// orange). A texture whose lit, visible pixels of mip 0 carry a mean chroma under kGreyscaleChroma is already grey and
// left as it is. The source file is never written. A texture that is missing, unreadable or fails leaves its slot.

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

    // The tint per role. Diffuse: the run's --tint (TextureJob::diffuseTint; lighting shaders are otherwise left alone,
    // so the diffuse carries the darkening). Glow: a brightness mask the shader's emissive colour multiplies, kept at
    // full range. Effect source: the shader's base colour is already luminance x tint, so the texture is plain luminance
    inline constexpr float kTextureTintGlow = 1.0f;
    inline constexpr float kTextureTintEffect = 1.0f;
    inline constexpr int kTintPercent = 100;  // a copy's tint folder is <tint x this>, under kTintFolder
    // The fixed segment in front of every tint folder: copies\_tint\<percent>\<path after textures\>
    inline constexpr const char* kTintFolder = "_tint\\";

    // Mean chroma (max - min channel, 0..1) of the lit, visible pixels of mip 0 below which a texture counts as already
    // grey. Chroma, not HSV saturation: the 5:6:5 noise of a dark grey BC1 gives a high saturation but a chroma of about
    // 0.03; small orange specks on black still count fully, since only lit pixels are averaged
    inline constexpr float kGreyscaleChroma = 0.05f;
    // A pixel counts as lit from this brightest channel up (darker ones carry no colour anyone sees); none lit: grey
    inline constexpr float kLitFloor = 0.1f;
    // U4: a texture is also coloured when more than kColouredShare of its lit, visible pixels have a chroma of
    // kColouredChroma or more: grey smoke with a few orange embers has a low mean chroma but shows orange. The 5:6:5 noise
    // of a dark grey BC1 (chroma of a few 8-bit steps) stays far under kColouredChroma
    inline constexpr float kColouredChroma = 0.15f;
    inline constexpr float kColouredShare = 0.005f;

    // BSShaderTextureSet slots
    inline constexpr std::uint32_t kSlotDiffuse = 0;
    inline constexpr std::uint32_t kSlotGlow = 2;
    // SLSF2 bit 6, Glow_Map (CommonLibSSE-NG RE/B/BSShaderProperty.h EShaderPropertyFlag8 kGlowMap = 38 - 32; nifly
    // BSShaderProperty::HasGlowmap tests the same bit)
    inline constexpr std::uint32_t kSlsf2GlowMap = 1u << 6;
    // SLSF1 bit 22, Own_Emit (CommonLibSSE-NG RE/B/BSShaderProperty.h EShaderPropertyFlag kOwnEmit = 22): the shader
    // adds its emissive colour, glow map or not (U6)
    inline constexpr std::uint32_t kSlsf1OwnEmit = 1u << 22;
    // U7: a tint this close to 1 leaves an already grey texture as it is (any other tint darkens it into a copy)
    inline constexpr float kTintOneEpsilon = 0.005f;

    enum class TextureRole
    {
        kDiffuse,
        kGlow,
        kEffectSource
    };

    [[nodiscard]] const char* TextureRoleName(TextureRole a_role) noexcept;

    /// The tint of a role; a_diffuseTint is the run's --tint
    [[nodiscard]] float TextureTint(TextureRole a_role, float a_diffuseTint) noexcept;

    /// A slot's text as a Data-relative texture path: the DLL's own rule (plugins/BlackFlame/src/DataPathRule.h,
    /// Normalize with "textures\"), so a slot finds the file the dump wrote for it, and a name ending in ".dds".
    /// nullopt for what that rule refuses (empty, a drive or ':', a ".." token, a character no file name has) or another
    /// extension.
    [[nodiscard]] std::optional<std::string> TextureDataPath(const std::string& a_slot);

    /// The copy's path for a TextureDataPath result: kTextureCopyRoot + kTintFolder + "<percent>\" + the part after
    /// textures\. Every tint (1 included) has its own folder under one fixed segment, so one texture used with two tints
    /// gets two copies, and no source path can land on another source's copy (a source under textures\t60\ once did,
    /// when tint 1 had no folder and 0.6 used t60\)
    [[nodiscard]] std::string TextureCopyPathFor(const std::string& a_textureDataPath, float a_tint);

    enum class RecolourOutcome
    {
        kRecoloured,
        kGreyscale,  // already grey (or one channel, or a legacy luminance format): nothing written
        kFailed      // unreadable, a format that cannot be done, or an encode / write failure: nothing written
    };

    struct RecolourResult
    {
        RecolourOutcome outcome = RecolourOutcome::kFailed;
        std::string reason;     // kFailed: why; kGreyscale: the measure
        std::string format;     // the source's DXGI format number and name, e.g. "77 BC3_UNORM"
        float chroma = 0.0f;    // the mean chroma of the lit, visible pixels of mip 0
        float colouredShare = 0.0f;  // U4: the share of those pixels with a chroma of kColouredChroma or more
        bool alphaIgnored = false;   // U5: no pixel was both visible and lit, so the measure ran over the lit pixels
        std::size_t mips = 0;
        std::uint64_t bytes = 0;  // kRecoloured: the copy's file size
    };

    /// Reads a_source (DDS; legacy luminance formats expanded to RGBA, so they read as grey) and writes its grey copy to
    /// a_target (folders made): luminance x a_tint per pixel, alpha kept, the same format, size, mip count and array size.
    /// An already grey texture is left (kGreyscale, nothing written) at a tint of 1 only; at another tint it is copied
    /// darkened like any other (U7: the diffuse carries --tint). A one-channel format is left at any tint. Float and HDR
    /// formats are refused, not clamped. Never writes a_source.
    [[nodiscard]] RecolourResult RecolourDds(const std::filesystem::path& a_source, const std::filesystem::path& a_target,
        float a_tint);

    struct TextureTotals
    {
        int slots = 0;           // texture slots the rule picked (every NIF, every use)
        int repointed = 0;       // of those, now naming a grey copy
        int recoloured = 0;      // copies written, one per texture and tint (a texture used with two tints counts twice)
        int greyscale = 0;       // textures left as already grey, each path once (U8)
        int failed = 0;          // textures that could not be done, each path once (U8), plus slots whose text is no
                                 // texture path (each slot)
        int missing = 0;         // textures in no --in folder (not in the dump), each path once (U8)
        int emissiveGreyed = 0;  // glow and Own_Emit lighting shaders whose emissive colour was greyed
        std::uint64_t bytes = 0;  // the copies' size
    };

    /// One run's texture work: where textures are read and written, what is done so far (each texture and tint once)
    struct TextureJob
    {
        std::vector<std::filesystem::path> inputs;   // searched in order, as for NIFs
        std::filesystem::path out;                   // the copies go to out / kTextureCopyRoot...
        float diffuseTint = kDefaultTint;            // the run's --tint
        std::map<std::string, std::string> notDumped;  // --from-game: texture -> why the game left it out of the dump
        struct Done
        {
            RecolourOutcome outcome = RecolourOutcome::kFailed;
            bool found = false;
            std::string copy;  // the slot's new text, "" when the slot stays
        };
        std::map<std::string, Done> done;  // by TextureDataPath + "|" + tint percent
        struct PathState               // U8: what holds for a path whatever the tint, judged once
        {
            bool judged = false;
            std::optional<std::filesystem::path> source;  // the file in the --in folders; nullopt: MISS
            bool failed = false;     // it could not be done once: not tried at another tint
            bool greyCounted = false;  // GREY printed and counted once
        };
        std::map<std::string, PathState> paths;  // by TextureDataPath
        TextureTotals totals;
    };

    /// Every texture slot of a_nif the rule picks (UsesPalette on the effect shaders as GreyEffects leaves them): each
    /// distinct texture and tint recoloured once per job into a_job.out, each slot whose texture got a copy pointed at it;
    /// a glow shader's emissive colour set to its luminance. Prints one line per texture the first time it is met. Never
    /// throws for a texture; the NIF stays loadable as it was.
    void RetargetTextures(nifly::NifFile& a_nif, TextureJob& a_job, const std::string& a_nifRel);

    /// "textures  recoloured N, ..." for a run's end
    [[nodiscard]] std::string TextureTotalsLine(const TextureTotals& a_totals);
}
