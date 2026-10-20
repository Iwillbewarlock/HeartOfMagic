#pragma once

// =============================================================================
// GreyPalette - the palettes the full-grey copies' palette shaders take (README "Palettes")
// =============================================================================
// A palette shader maps its texture through a palette: colour by the texture's green, alpha (palette alpha on) by its
// alpha, the row by base colour red x vertex red (NifSkope's model). Our one-row palettes (GradBlackFlame.dds,
// GradBlackFlameAdd.dds) replaced every source palette, so a shape whose source palette alpha carved its outline lit up
// whole (a sheet) or fell into the palette's dark band (it vanished). The derived mode keeps each source palette's shape:
//   additive (P2): the source palette, every texel's colour set to its luminance, alpha exactly as it was;
//   normal blend (P1): GradBlackFlame.dds's colour resampled to the source palette's size, the source's alpha.
// One file per source palette and mode, under textures\heartofmagic\blackflame\copies\palettes\{add,normal}\<path after
// textures\> (8-bit RGBA, one mip, sRGB as the source). A source palette that cannot be read keeps today's palette
// (GradBlackFlameAdd.dds / GradBlackFlame.dds) and the risk check reports it (UNREAD, or MISSING when the game lacks it).
// The godskin mode (--palette-mode godskin) is the earlier rule: those two palettes for every palette shader.

#include "GreyTexStats.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace GreyBuild
{
    class KeyedOutputs;  // GreyKeyed.h

    inline constexpr const char* kPaletteCopyRoot = "textures\\heartofmagic\\blackflame\\copies\\palettes\\";
    // Y16: the folder of each blend under kPaletteCopyRoot (GreyPalette.cpp writes there, VerifyGrey expects them)
    inline constexpr const char* kPaletteAddFolder = "add\\";
    inline constexpr const char* kPaletteNormalFolder = "normal\\";

    enum class PaletteMode
    {
        kDerived,  // P2 for additive, P1 for normal blend (the default)
        kGodskin   // GradBlackFlameAdd.dds for additive, GradBlackFlame.dds for normal blend
    };

    struct PaletteTotals
    {
        int derivedAdd = 0;     // P2 palette files written
        int derivedNormal = 0;  // P1 palette files written
        int shadersAdd = 0;     // shaders pointed at a P2 palette
        int shadersNormal = 0;  // shaders pointed at a P1 palette
        int fallback = 0;       // shaders whose source palette could not be read: today's palette kept
        int failed = 0;         // palette files that could not be written (today's palette kept)
    };

    struct PaletteJob
    {
        PaletteMode mode = PaletteMode::kDerived;
        std::filesystem::path out;           // the staging folder (files go to out / kPaletteCopyRoot ...)
        TextureSource* textures = nullptr;   // the source palettes and GradBlackFlame.dds
        std::map<std::string, std::string> made;  // "add|path" / "normal|path" -> the slot text ("" when it failed)
        PaletteTotals totals;
        KeyedOutputs* keyed = nullptr;  // key mode: each palette at its own key's copies folder, with its line (GreyKeyed.h)
        std::optional<double> tintAddHands;  // this object's casting-art override, never a run-wide tint
    };

    /// The greyscale texture slot a palette shader's copy takes for a_sourcePalette (its slot text), made once per run;
    /// "" with a_why when the source palette cannot be read or the file cannot be written (the caller keeps today's)
    [[nodiscard]] std::string DerivedPalette(PaletteJob& a_job, const std::string& a_sourcePalette, bool a_additive,
        std::string& a_why);

    /// P2 alone (the effect shader palettes' grey, GreyKeyedEfsh.cpp): every texel's colour to its luminance, alpha and
    /// rows untouched
    void DesaturatePalette(RgbaImage& a_image);

    /// Casting art: dark low indices, white core; preserve each row's relative luminance and every alpha byte.
    void MakeHandPalette(RgbaImage& a_image, float a_tint);

    /// a_image as an 8-bit RGBA DDS, one mip, sRGB as it is (the derived palettes' format); false with a_why
    [[nodiscard]] bool WritePaletteDds(const RgbaImage& a_image, const std::filesystem::path& a_file, std::string& a_why);

    /// "palettes  ..." for a run's end
    [[nodiscard]] std::string PaletteTotalsLine(const PaletteJob& a_job);
}
