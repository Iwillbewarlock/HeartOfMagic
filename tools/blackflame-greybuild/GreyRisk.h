#pragma once

// =============================================================================
// GreyRisk - the offline visual-risk check of the builder's own output (README "Visual-risk check")
// =============================================================================
// Each full-grey copy NIF (and grey MPS) is compared with its source shape by shape (same block numbers: the builder
// changes values only) and judged by data alone: colour left in the copy (KEYS_LEFT, VC_LEFT, PSYS_LEFT), a soft
// additive warm glow turned into a visible grey haze (GREY_HALO), a coloured texture the copy still names (TEX_COLOUR),
// a palette shape that turns into a sheet or vanishes (PAL_SHEET, PAL_VANISH), a NIF whose every visible shape went
// dark (EFFECT_EMPTY), a decal texture set copy that still names a coloured texture (DECAL_COLOUR, GreyFormsTxst.h), and
// warnings (PAL_BRIGHTER, NEUTRAL_HALO, PAL_FLAT). A texture the check needs and cannot read
// is UNREAD (never a pass); one the game itself does not have is MISSING (listed only). The gate is FAIL 0 and UNREAD 0.
// The rules and constants are the prototype's (scratchpad glow-rule/riskcheck.js and glow-rule.md section 4), which
// assumes NifSkope's effect shader model: the palette colour looked up by the texture's green channel, its alpha by the
// texture's alpha - not checked against the game.

#include "GreyTexStats.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace nifly
{
    class NifFile;
}

namespace GreyBuild
{
    inline constexpr double kRiskWarmSat = 0.35;        // a source colour this saturated or more is warm (coloured)
    inline constexpr double kRiskMinChannel = 0.02;     // a colour darker than this has no saturation verdict
    inline constexpr double kRiskSoftDetail = 0.06;     // texture detail below this: a soft glow texture
    inline constexpr double kRiskHaloLumMax = 0.01;     // a soft additive copy brighter than this is visible
    inline constexpr double kRiskMaxKeySat = 0.05;      // any copy colour more saturated than this is colour left
    inline constexpr double kRiskMaxVcSat = 0.10;       // copy vertex colours (alpha-weighted mean) more saturated: colour left
    inline constexpr double kRiskTexColoredFrac = 0.20; // share of coloured lit texels from which a texture is coloured
    inline constexpr double kRiskTexMeanSat = 0.30;     // or the saturation of its alpha-weighted mean colour
    inline constexpr double kRiskSheetRatio = 4.0;      // palette energy (additive) or coverage (normal) ratio copy / source
    inline constexpr double kRiskSheetCover = 0.5;      // ... with the copy covering at least this share: a sheet
    inline constexpr double kRiskVanishRatio = 0.25;    // additive palette energy ratio below which the shape vanishes
    inline constexpr double kRiskMinSrcEnergy = 0.01;   // ... when the source drew at least this much
    inline constexpr double kRiskMinGBand = 0.05;       // palette texture green spread below this: one flat colour
    inline constexpr double kRiskDarkLum = 0.15;        // palette colour darker than this: the "dark valley"
    inline constexpr double kRiskEnergyFloor = 1e-4;    // an energy or coverage below this counts as drawing nothing
    inline constexpr double kRiskModelSpread = 2.0;     // Y3: the two readings of the palette shader disagree beyond this
    inline constexpr double kRiskDimRatio = 0.25;       // Y5: a NIF giving less than this share of its source's light

    enum class RiskLevel
    {
        kFail,
        kWarn,
        kUnread,
        kMissing
    };

    struct RiskFinding
    {
        RiskLevel level = RiskLevel::kFail;
        std::string rule;
        std::string nif;
        std::uint32_t block = 0;
        std::string name;
        std::string detail;
    };

    /// One run's check: where textures come from, and everything found so far
    struct RiskJob
    {
        TextureSource* textures = nullptr;  // none: every texture rule is UNREAD
        int nifs = 0;
        int shapes = 0;
        std::vector<RiskFinding> findings;
        std::map<std::string, int> perRule;
        // Codex 11: our two fixed palettes (DataPathRule kOurPalette*) unread: one ordinary finding each (so the gate counts
        // it), its index and the number of shapes that needed it
        std::map<std::string, std::pair<std::size_t, int>> ownPaletteUnread;
    };

    /// Compares a copy NIF with its source and adds what it finds to a_job (a_nif: the copy's Data-relative path)
    /// a_haloKept: the glow rules kept this NIF's soft warm glows visible (the G1 exception, GlowContext::haloKept);
    /// a_mixed: its shaders the glow rules kept out of G1, and why (Z4: said in the GREY_HALO line)
    void RiskCheckNif(nifly::NifFile& a_source, nifly::NifFile& a_copy, const std::string& a_nif, RiskJob& a_job,
        bool a_haloKept = false, const std::map<std::uint32_t, std::string>* a_mixed = nullptr);

    /// Z5: a shape draws at all - shown, blended, its texture not blank; a palette shape with an emissive multiple above 0
    /// (and alpha above 0 unless palette alpha picks it), a plain one with alpha and colour above 0 (Community Shaders'
    /// Effect.hlsl: the emissive multiple scales the palette branch only). The glow rules' G1 exception uses it too
    [[nodiscard]] bool IsDrawn(const EffectShape& a_shape, const TexStats* a_stats);

    [[nodiscard]] int RiskCount(const RiskJob& a_job, RiskLevel a_level);

    /// "T0 risk: PASS fail=0 unread=0" or "T0 risk: FAIL fail=<n> unread=<m> (see <file name>)"
    [[nodiscard]] std::string RiskGateLine(const RiskJob& a_job);

    [[nodiscard]] bool RiskGatePasses(const RiskJob& a_job);

    /// The report (UTF-8, tab separated): header, constants, SUMMARY, then FAIL / WARN / UNREAD / MISSING lines. False
    /// with a_why when it could not be written (X9: the caller warns, the build goes on)
    [[nodiscard]] bool WriteRiskReport(const RiskJob& a_job, const std::filesystem::path& a_file, std::string& a_why);

    inline constexpr const char* kRiskReportName = "HeartOfMagic_BlackFlame_risk.txt";
}
