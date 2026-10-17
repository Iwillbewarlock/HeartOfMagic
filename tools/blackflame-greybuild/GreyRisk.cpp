// =============================================================================
// GreyRisk - the offline visual-risk check of the builder's own output (GreyRisk.h)
// =============================================================================

#include "GreyRisk.h"

#include "../../plugins/BlackFlame/src/DataPathRule.h"
#include "GreyTexture.h"

#include <Animation.hpp>
#include <NifFile.hpp>
#include <Particles.hpp>

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace GreyBuild
{
    namespace
    {
        static_assert(kStatMinChannel == kRiskMinChannel && kStatColouredFrac == kRiskTexColoredFrac && kStatMeanSat == kRiskTexMeanSat,
            "the texture statistics and the risk check judge colour alike");
        constexpr const char* kRuleKeys = "KEYS_LEFT";
        constexpr const char* kRuleVc = "VC_LEFT";
        constexpr const char* kRulePsys = "PSYS_LEFT";
        constexpr const char* kRuleHalo = "GREY_HALO";
        constexpr const char* kRuleHaloKept = "HALO_KEPT";
        constexpr const char* kRuleLayout = "BLOCK_LAYOUT";
        constexpr const char* kRuleForeign = "PAL_FOREIGN";
        constexpr const char* kRuleModel = "PAL_MODEL";
        constexpr const char* kRuleDim = "EFFECT_DIM";
        constexpr const char* kRuleUnreached = "KEYS_UNREACHED";


        constexpr const char* kRuleTexColour = "TEX_COLOUR";
        constexpr const char* kRuleSheet = "PAL_SHEET";
        constexpr const char* kRuleVanish = "PAL_VANISH";
        constexpr const char* kRuleBrighter = "PAL_BRIGHTER";
        constexpr const char* kRuleNeutralHalo = "NEUTRAL_HALO";
        constexpr const char* kRuleFlat = "PAL_FLAT";
        constexpr const char* kRuleEmpty = "EFFECT_EMPTY";
        constexpr const char* kRuleUnread = "UNREAD";
        constexpr const char* kRuleMissing = "MISSING";
        const Rgb kWhite{ 1.0, 1.0, 1.0 };

        struct Context
        {
            RiskJob& job;
            const std::string& nif;
            double energyBefore = 0.0;        // Y5: what the drawn shapes give, source and copy (EFFECT_DIM)
            double energyAfter = 0.0;
            std::vector<RiskFinding> halos;   // GREY_HALO, judged when the NIF is done (the G1 exception)
        };

        void Add(Context& a_ctx, RiskLevel a_level, const char* a_rule, const EffectShape* a_shape, std::string a_detail)
        {
            RiskFinding finding;
            finding.level = a_level;
            finding.rule = a_rule;
            finding.nif = a_ctx.nif;
            finding.block = a_shape ? a_shape->block : 0;
            finding.name = a_shape ? a_shape->name : std::string();
            finding.detail = std::move(a_detail);
            ++a_ctx.job.perRule[a_rule];
            a_ctx.job.findings.push_back(std::move(finding));
        }

        [[nodiscard]] double MaxChannel(const std::vector<Rgb>& a_colours) noexcept
        {
            double high = 0.0;
            for (const auto& c : a_colours) {
                high = std::max({ high, c.r, c.g, c.b });
            }
            return high;
        }

        [[nodiscard]] std::string Ratio(double a_value)
        {
            return std::isinf(a_value) ? std::string("inf") : fmt::format("{:.2f}", a_value);
        }

        // A texture the rule needs and cannot have: MISSING when the game itself lacks it, UNREAD otherwise. Codex 11: one of
        // our two fixed palettes is one finding for the run (the first shape's), counting the shapes that needed it
        void NotRead(Context& a_ctx, const EffectShape& a_shape, const TextureLook& a_look, const std::string& a_slot,
            const char* a_what)
        {
            if (a_look.path == BlackFlame::DataPathRule::kOurPalette || a_look.path == BlackFlame::DataPathRule::kOurPaletteAdd) {
                auto& job = a_ctx.job;
                if (const auto it = job.ownPaletteUnread.find(a_look.path); it != job.ownPaletteUnread.end()) {
                    auto& [index, shapes] = it->second;
                    ++shapes;
                    job.findings[index].detail = fmt::format("our palette {} not read for {} shapes (the first: {} {}): {}",
                        a_look.path, shapes, job.findings[index].nif, job.findings[index].block, a_look.why);
                    return;
                }
                job.ownPaletteUnread.emplace(a_look.path, std::make_pair(job.findings.size(), 1));
                Add(a_ctx, a_look.status == TextureRead::kMissing ? RiskLevel::kMissing : RiskLevel::kUnread,
                    a_look.status == TextureRead::kMissing ? kRuleMissing : kRuleUnread, &a_shape,
                    fmt::format("our palette {} not read for 1 shape: {}", a_look.path, a_look.why));
                return;
            }
            const bool missing = a_look.status == TextureRead::kMissing;
            Add(a_ctx, missing ? RiskLevel::kMissing : RiskLevel::kUnread, missing ? kRuleMissing : kRuleUnread, &a_shape,
                fmt::format("{} {}: {}", a_what, a_look.path.empty() ? a_slot : a_look.path, a_look.why));
        }

        [[nodiscard]] bool Visible(const EffectShape& a_shape, const TextureLook& a_texture)
        {
            return IsDrawn(a_shape, a_texture.stats);
        }

        [[nodiscard]] TextureLook Look(RiskJob& a_job, const std::string& a_slot)
        {
            if (!a_job.textures) {
                TextureLook look;
                look.why = "no texture source in this run";
                return look;
            }
            return a_job.textures->Look(a_slot);
        }

        // Y1: an empty source texture slot samples a default texture (assumed one white texel); else the slot's texture
        [[nodiscard]] TextureLook TextureOf(RiskJob& a_job, const std::string& a_slot)
        {
            if (!a_slot.empty()) {
                return Look(a_job, a_slot);
            }
            TextureLook white;
            white.status = TextureRead::kOk;
            white.image = &WhiteTexel();
            white.path = "(no source texture: one white texel assumed)";
            return white;
        }

        struct Rows
        {
            double colour = 1.0;  // base red x vertex red (Effect.hlsl: grayscaleToColorUv.y = baseColorMul.x)
            double alpha = 1.0;   // base alpha x vertex alpha (the alpha lookup's v)
        };

        // Y2, Y17: each side's own rows, from its own values (the copy's base colour, not the source's)
        [[nodiscard]] Rows RowsOf(const EffectShape& a_shape)
        {
            const bool vertexOn = (a_shape.flags2 & kSlsf2VertexColors) != 0 && a_shape.vertexColours;
            return { std::clamp(a_shape.baseColour.r * (vertexOn ? a_shape.vertexMean.r : 1.0), 0.0, 1.0),
                std::clamp(a_shape.baseAlpha * (vertexOn ? a_shape.vertexAlpha : 1.0), 0.0, 1.0) };
        }

        [[nodiscard]] double RatioOf(double a_copy, double a_source)
        {
            return a_source > kRiskEnergyFloor ? a_copy / a_source :
                   a_copy > kRiskEnergyFloor   ? std::numeric_limits<double>::infinity() : 1.0;
        }

        // Z8: one side's palette simulation with its own flags: palette alpha, alpha test (threshold, GEQUAL) and whether it
        // blends at all (an alpha-test-only shape is an opaque cut-out: its coverage is the share of samples that pass)
        [[nodiscard]] PaletteSim Simulate(const EffectShape& a_shape, const RgbaImage& a_texture, const RgbaImage& a_palette)
        {
            const Rows rows = RowsOf(a_shape);
            return SimulatePalette(a_texture, a_palette, (a_shape.flags1 & kSlsf1GreyscaleToPaletteAlpha) != 0, rows.colour,
                rows.alpha, kRiskDarkLum, a_shape.alphaTest ? a_shape.alphaThreshold : -1.0, a_shape.blend == ShapeBlend::kTest);
        }

        [[nodiscard]] double Spread(double a_one, double a_other)
        {
            const bool finite = !std::isinf(a_one) && !std::isinf(a_other) && a_one > 0.0 && a_other > 0.0;
            return finite ? std::max(a_one / a_other, a_other / a_one) : (a_one == a_other ? 1.0 : std::numeric_limits<double>::infinity());
        }

        void CheckPalette(Context& a_ctx, const EffectShape& a_source, const EffectShape& a_copy, const TexStats* a_stats,
            bool a_drawnBefore, bool a_drawnAfter)
        {
            const auto texture = TextureOf(a_ctx.job, a_source.sourceTexture);
            const auto copyTexture = TextureOf(a_ctx.job, a_copy.sourceTexture);  // X6: the copy side with the copy's texture
            const auto sourcePalette = Look(a_ctx.job, a_source.greyscaleTexture);
            const auto copyPalette = Look(a_ctx.job, a_copy.greyscaleTexture);
            for (const auto& [look, slot, what] : { std::tuple{ texture, a_source.sourceTexture, "texture" },
                     std::tuple{ copyTexture, a_copy.sourceTexture, "copy texture" },
                     std::tuple{ sourcePalette, a_source.greyscaleTexture, "source palette" },
                     std::tuple{ copyPalette, a_copy.greyscaleTexture, "copy palette" } }) {
                if (look.status != TextureRead::kOk) {
                    NotRead(a_ctx, a_source, look, slot, what);
                    return;
                }
            }
            const auto src = Simulate(a_source, *texture.image, *sourcePalette.image);
            const auto copy = Simulate(a_copy, *copyTexture.image, *copyPalette.image);
            // Z9: EFFECT_DIM's light, counted only for a drawn side (the same unit as a plain shape's: mean luminance x alpha)
            a_ctx.energyBefore += a_drawnBefore ? src.energy * a_source.multipleMax : 0.0;
            a_ctx.energyAfter += a_drawnAfter ? copy.energy * a_copy.multipleMax : 0.0;
            const char* alphaNote = (a_source.flags1 & kSlsf1GreyscaleToPaletteAlpha) != 0 ? " (palette alpha)" : "";
            if (a_source.blend == ShapeBlend::kAdditive) {
                // Y2: the output scale is each shader's own emissive multiple (Effect.hlsl: baseColor.xyz = BaseColorScale.x x
                // palette), read from the source and from the copy - not assumed from the run's settings
                const double ratio = RatioOf(copy.energy * a_copy.multipleMax, src.energy * a_source.multipleMax);
                if (ratio > kRiskSheetRatio && copy.coverage >= kRiskSheetCover) {
                    Add(a_ctx, RiskLevel::kFail, kRuleSheet, &a_source, fmt::format("energy x{}, coverage {:.2f} -> {:.2f}{}",
                        Ratio(ratio), src.coverage, copy.coverage, alphaNote));
                } else if (ratio > kRiskSheetRatio) {
                    Add(a_ctx, RiskLevel::kWarn, kRuleBrighter, &a_source, fmt::format("energy x{}", Ratio(ratio)));
                }
                if (ratio < kRiskVanishRatio && src.energy * a_source.multipleMax >= kRiskMinSrcEnergy) {
                    Add(a_ctx, RiskLevel::kFail, kRuleVanish, &a_source, fmt::format("energy x{}, {:.0f}% of lit texels on the "
                        "palette dark band", Ratio(ratio), copy.darkFrac * 100.0));
                }
                // Y3: under the other reading of the shader (the base colour multiplies the palette output, as in NifSkope),
                // the copy's (r, r, r) base against the source's coloured one changes the energy; warn when the two readings
                // disagree by more than kRiskModelSpread
                const double alt = RatioOf(copy.energy * a_copy.multipleMax * Luminance(a_copy.baseColour),
                    src.energy * a_source.multipleMax * Luminance(a_source.baseColour));
                if (Spread(alt, ratio) > kRiskModelSpread) {
                    Add(a_ctx, RiskLevel::kWarn, kRuleModel, &a_source, fmt::format("energy x{} (palette x emissive multiple, "
                        "Effect.hlsl) but x{} if the base colour multiplied the palette", Ratio(ratio), Ratio(alt)));
                }
            } else {
                const double ratio = RatioOf(copy.coverage, src.coverage);
                if (ratio > kRiskSheetRatio && copy.coverage >= kRiskSheetCover) {
                    Add(a_ctx, RiskLevel::kFail, kRuleSheet, &a_source, fmt::format("{} coverage {:.2f} -> {:.2f}{}",
                        a_source.blend == ShapeBlend::kTest ? "alpha test" : "normal blend", src.coverage, copy.coverage, alphaNote));
                }
            }
            if (src.uRange < kRiskMinGBand && !(a_stats && a_stats->energyMean == 0.0) && !a_source.sourceTexture.empty()) {
                Add(a_ctx, RiskLevel::kWarn, kRuleFlat, &a_source, fmt::format("texture lit green range {:.3f}", src.uRange));
            }
        }

        // Z3, Z5: the brightest shown luminance of a plain shape (no emissive multiple: Effect.hlsl applies BaseColorScale in
        // the palette branch only)
        [[nodiscard]] double ShownLuminance(const EffectShape& a_shape, const Rgb& a_texMean, const Rgb& a_vertex)
        {
            double lum = 0.0;
            for (const auto& shown : ShownColours(a_shape, a_texMean, a_vertex)) {
                lum = std::max(lum, Luminance(shown));
            }
            return lum;
        }

        void CheckPlain(Context& a_ctx, const EffectShape& a_source, const EffectShape& a_copy, const TextureLook& a_texture,
            const TextureLook& a_copyTexture, const std::string* a_mixed)
        {
            const bool vertexOn = (a_source.flags2 & kSlsf2VertexColors) != 0 && a_source.vertexColours;
            if (vertexOn && a_copy.vertexColours && a_copy.vertexSaturation > kRiskMaxVcSat) {
                Add(a_ctx, RiskLevel::kFail, kRuleVc, &a_source, fmt::format("copy vertex colour mean {},{},{} (S {:.3f})",
                    a_copy.vertexMean.r, a_copy.vertexMean.g, a_copy.vertexMean.b, a_copy.vertexSaturation));
            }
            if (!a_source.sourceTexture.empty() && a_texture.status != TextureRead::kOk) {
                NotRead(a_ctx, a_source, a_texture, a_source.sourceTexture, "texture");
                return;
            }
            if (!a_copy.sourceTexture.empty() && a_copyTexture.status != TextureRead::kOk) {
                NotRead(a_ctx, a_source, a_copyTexture, a_copy.sourceTexture, "copy texture");  // X6: a generated copy unread
                return;
            }
            const TexStats* stats = a_texture.stats;
            const TexStats* copyStats = a_copyTexture.stats;
            // X6: TEX_COLOUR on the texture the copy names (a texture copy that came out coloured, or the source left)
            if (copyStats && IsColouredTexture(*copyStats)) {
                Add(a_ctx, RiskLevel::kFail, kRuleTexColour, &a_source, fmt::format("texture {} coloured (mean {},{},{}, {:.0f}% "
                    "coloured texels) named by the copy", a_copyTexture.path, copyStats->meanRgb.r, copyStats->meanRgb.g,
                    copyStats->meanRgb.b, copyStats->colouredFrac * 100.0));
            }
            if (a_source.blend != ShapeBlend::kAdditive || (stats && stats->energyMean == 0.0)) {
                return;
            }
            const bool soft = a_source.sourceTexture.empty() || (stats && stats->detail < kRiskSoftDetail);
            const Rgb texMean = stats ? stats->meanRgb : kWhite;
            const Rgb copyTexMean = copyStats ? copyStats->meanRgb : (a_copy.sourceTexture.empty() ? kWhite : texMean);
            const Rgb sourceVertex = vertexOn ? a_source.vertexMean : kWhite;
            const Rgb copyVertex = vertexOn && a_copy.vertexColours ? a_copy.vertexMean : sourceVertex;
            double sourceSat = 0.0;
            for (const auto& shown : ShownColours(a_source, texMean, sourceVertex)) {  // Z3: the glow classes' measure
                sourceSat = std::max(sourceSat, Saturation(shown, kRiskMinChannel));
            }
            const double copyLum = ShownLuminance(a_copy, copyTexMean, copyVertex);
            if (soft && sourceSat >= kRiskWarmSat && copyLum > kRiskHaloLumMax) {
                a_ctx.halos.push_back({ RiskLevel::kFail, kRuleHalo, a_ctx.nif, a_source.block, a_source.name,
                    fmt::format("soft additive {}, source S {:.2f}, copy grey luminance {:.3f}{}{}", a_source.sourceTexture.empty() ?
                        std::string("no texture") : fmt::format("texture {} (detail {})", a_texture.path, stats ? stats->detail : 0.0),
                        sourceSat, copyLum, a_source.billboard ? ", under a billboard node" : "",
                        a_mixed ? " - " + *a_mixed : std::string()) });  // Z4: why the glow rules did not zero it
            } else if (soft && copyLum > kRiskHaloLumMax) {
                Add(a_ctx, RiskLevel::kWarn, kRuleNeutralHalo, &a_source, fmt::format("soft additive neutral glow, copy "
                    "luminance {:.3f}", copyLum));
            }
        }

        // Y5, Z9: a plain shape's light, in the palette side's unit (mean luminance x alpha): its brightest shown colour x
        // the texture's mean alpha
        [[nodiscard]] double PlainEnergy(const EffectShape& a_shape, const TextureLook& a_texture)
        {
            const Rgb texMean = a_texture.stats ? a_texture.stats->meanRgb : kWhite;
            const bool vertexOn = (a_shape.flags2 & kSlsf2VertexColors) != 0 && a_shape.vertexColours;
            return ShownLuminance(a_shape, texMean, vertexOn ? a_shape.vertexMean : kWhite) *
                   (a_texture.stats ? a_texture.stats->alphaMean : 1.0);
        }

        void CheckParticleColours(Context& a_ctx, nifly::NifFile& a_copy)
        {
            auto& hdr = a_copy.GetHeader();
            const auto worst = [](std::initializer_list<nifly::Color4> a_colours) {
                double sat = 0.0;
                for (const auto& c : a_colours) {
                    sat = std::max(sat, Saturation({ c.r, c.g, c.b }, kRiskMinChannel));
                }
                return sat;
            };
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                double sat = 0.0;
                if (const auto* simple = hdr.GetBlock<nifly::BSPSysSimpleColorModifier>(id)) {
                    sat = worst({ simple->color1, simple->color2, simple->color3 });
                } else if (const auto* data = hdr.GetBlock<nifly::NiColorData>(id)) {
                    const auto count = static_cast<int>(data->data.GetNumKeys());
                    for (int i = 0; i < count; ++i) {
                        sat = std::max(sat, worst({ data->data.GetKey(i).value }));
                    }
                } else {
                    continue;
                }
                if (sat > kRiskMaxKeySat) {
                    RiskFinding finding{ RiskLevel::kFail, kRulePsys, a_ctx.nif, id, std::string(),
                        fmt::format("particle colour key saturation {:.2f}", sat) };
                    ++a_ctx.job.perRule[kRulePsys];
                    a_ctx.job.findings.push_back(std::move(finding));
                }
            }
        }

        [[nodiscard]] const char* LevelName(RiskLevel a_level) noexcept
        {
            switch (a_level) {
            case RiskLevel::kFail: return "FAIL";
            case RiskLevel::kWarn: return "WARN";
            case RiskLevel::kUnread: return "UNREAD";
            default: return "MISSING";
            }
        }
    }

    bool IsDrawn(const EffectShape& a_shape, const TexStats* a_stats)
    {
        if (a_shape.hidden || a_shape.blend == ShapeBlend::kNoAlpha || a_shape.blend == ShapeBlend::kOpaque) {
            return false;
        }
        if (a_stats && a_stats->energyMean == 0.0) {
            return false;  // a blank texture
        }
        if (IsPaletteShape(a_shape)) {
            // Z5: Effect.hlsl scales the palette colour by BaseColorScale (the emissive multiple); with palette alpha the
            // alpha comes from the palette's row (base alpha x vertex alpha, row 0 included), so base alpha 0 does not hide it
            const bool paletteAlpha = (a_shape.flags1 & kSlsf1GreyscaleToPaletteAlpha) != 0;
            return a_shape.multipleMax > 0.0 && (paletteAlpha || a_shape.alphaMax > 0.0);
        }
        // Z5: a plain shape is not scaled by the emissive multiple (BaseColorScale is used in the palette branch only); its
        // alpha (alpha-transparency controller keys included) and colour decide
        return a_shape.alphaMax > 0.0 && MaxChannel(a_shape.colours) > 0.0;
    }

    void RiskCheckNif(nifly::NifFile& a_source, nifly::NifFile& a_copy, const std::string& a_nif, RiskJob& a_job, bool a_haloKept,
        const std::map<std::uint32_t, std::string>* a_mixed)
    {
        Context ctx{ a_job, a_nif };
        ++a_job.nifs;
        const auto sources = EffectShapes(a_source);
        const auto copies = EffectShapes(a_copy);
        std::map<std::uint32_t, const EffectShape*> copyByBlock;
        for (const auto& shape : copies) {
            copyByBlock[shape.block] = &shape;
        }
        int visibleBefore = 0;
        int visibleAfter = 0;
        std::set<std::uint32_t> unreachedShaders;
        for (const auto& source : sources) {
            const auto it = copyByBlock.find(source.block);
            // X8: the builder changes values only, so a copy has the source's effect shapes at the same block numbers
            // under the same names; anything else means the layout changed and nothing below would compare like with like
            if (it == copyByBlock.end() || it->second->name != source.name) {
                Add(ctx, RiskLevel::kFail, kRuleLayout, &source, it == copyByBlock.end() ?
                    std::string("the copy has no effect shape at this block") :
                    fmt::format("the copy's shape at this block is '{}'", it->second->name));
                continue;
            }
            const EffectShape& copy = *it->second;
            ++a_job.shapes;
            const auto texture = source.sourceTexture.empty() ? TextureLook{} : Look(a_job, source.sourceTexture);
            const auto copyTexture = copy.sourceTexture.empty() ? TextureLook{} : Look(a_job, copy.sourceTexture);
            if (Visible(source, texture)) {
                ++visibleBefore;
                visibleAfter += Visible(copy, copyTexture) ? 1 : 0;  // X6: a blank generated texture draws nothing
            }
            // Y1: the shader samples the palette whenever the flag is on (Community Shaders' Effect.hlsl: GrayscaleToColor,
            // with or without a source texture), blended or not: a palette that is not ours shows the source's colours
            // Z6: the shared definition of ours; hidden shapes too (a visibility controller may show them)
            if ((copy.flags1 & kSlsf1GreyscaleToPaletteColor) != 0 && !BlackFlame::DataPathRule::IsOurPalettePath(copy.greyscaleTexture)) {
                Add(ctx, RiskLevel::kFail, kRuleForeign, &source, fmt::format("palette flag on, greyscale texture '{}' is not one of "
                    "ours{}", copy.greyscaleTexture, copy.sourceTexture.empty() ? " (no source texture)" : ""));
            }
            if (source.hidden || source.blend == ShapeBlend::kNoAlpha || source.blend == ShapeBlend::kOpaque) {
                continue;  // not drawn, or a helper without blending (EditorMarker-type): out of scope
            }
            double worst = 0.0;
            for (const auto& colour : copy.colours) {
                worst = std::max(worst, Saturation(colour, kRiskMinChannel));
            }
            if (worst > kRiskMaxKeySat) {
                Add(ctx, RiskLevel::kFail, kRuleKeys, &source, fmt::format("copy colour key saturation {:.2f} ({} colours, {} "
                    "controlled-block links)", worst, copy.colours.size(), source.sequenceLinks));
            }
            // Y6, Z10: keys nothing reaches cannot be judged (nor greyed) - once per shader
            if (copy.unreachedControllers > 0 && unreachedShaders.insert(copy.shader).second) {
                Add(ctx, RiskLevel::kWarn, kRuleUnreached, &source, fmt::format("{} colour controller(s) with no reachable "
                    "NiPoint3Interpolator", copy.unreachedControllers));
            }
            const bool drawnBefore = Visible(source, texture);
            const bool drawnAfter = Visible(copy, copyTexture);
            if (IsPaletteShape(source)) {
                CheckPalette(ctx, source, copy, texture.stats, drawnBefore, drawnAfter);
            } else {
                ctx.energyBefore += drawnBefore ? PlainEnergy(source, texture) : 0.0;
                ctx.energyAfter += drawnAfter ? PlainEnergy(copy, copyTexture) : 0.0;
                const std::string* mixed = nullptr;
                if (a_mixed) {
                    if (const auto m = a_mixed->find(source.shader); m != a_mixed->end()) {
                        mixed = &m->second;
                    }
                }
                CheckPlain(ctx, source, copy, texture, copyTexture, mixed);
            }
        }
        // The G1 exception (GreyGlow.h): when the glow rules kept a NIF's soft warm glows visible because RGB 0 would have
        // emptied it (Y7: told by the glow step, not guessed here), its GREY_HALO is a WARN (HALO_KEPT), not a failure
        for (auto& halo : ctx.halos) {
            if (a_haloKept) {
                halo.level = RiskLevel::kWarn;
                halo.rule = kRuleHaloKept;
                halo.detail += " - the NIF's only drawn shape(s): kept visible (G2) so the effect is not empty";
            }
            ++a_job.perRule[halo.rule];
            a_job.findings.push_back(std::move(halo));
        }
        // Y5: the effect as a whole much dimmer than its source (G1's RGB 0 included): a WARN, the look is a choice
        if (ctx.energyBefore > kRiskEnergyFloor && ctx.energyAfter / ctx.energyBefore < kRiskDimRatio && visibleAfter > 0) {
            Add(ctx, RiskLevel::kWarn, kRuleDim, nullptr, fmt::format("the drawn shapes give {:.0f}% of the source's light",
                ctx.energyAfter / ctx.energyBefore * 100.0));
        }
        if (visibleBefore > 0 && visibleAfter == 0) {
            Add(ctx, RiskLevel::kFail, kRuleEmpty, nullptr, fmt::format("the source draws {} shape(s), the copy none (every "
                "one black or blank)", visibleBefore));
        }
        CheckParticleColours(ctx, a_copy);
    }

    int RiskCount(const RiskJob& a_job, RiskLevel a_level)
    {
        return static_cast<int>(std::count_if(a_job.findings.begin(), a_job.findings.end(),
            [&](const RiskFinding& a_finding) { return a_finding.level == a_level; }));
    }

    bool RiskGatePasses(const RiskJob& a_job)
    {
        return RiskCount(a_job, RiskLevel::kFail) == 0 && RiskCount(a_job, RiskLevel::kUnread) == 0;
    }

    std::string RiskGateLine(const RiskJob& a_job)
    {
        const int fail = RiskCount(a_job, RiskLevel::kFail);
        const int unread = RiskCount(a_job, RiskLevel::kUnread);
        return RiskGatePasses(a_job) ? fmt::format("T0 risk: PASS fail=0 unread=0 (warn {}, missing {})",
                                           RiskCount(a_job, RiskLevel::kWarn), RiskCount(a_job, RiskLevel::kMissing)) :
                                       fmt::format("T0 risk: FAIL fail={} unread={} (see {})", fail, unread, kRiskReportName);
    }

    bool WriteRiskReport(const RiskJob& a_job, const std::filesystem::path& a_file, std::string& a_why)
    {
        std::string text = "# HeartOfMagic Black Flame visual-risk report v1\n";
        text += fmt::format("# constants kRiskWarmSat={} kRiskMinChannel={} kRiskSoftDetail={} kRiskHaloLumMax={} kRiskMaxKeySat={} "
                            "kRiskMaxVcSat={} kRiskTexColoredFrac={} kRiskTexMeanSat={} kRiskSheetRatio={} kRiskSheetCover={} "
                            "kRiskVanishRatio={} kRiskMinSrcEnergy={} kRiskMinGBand={} kRiskDarkLum={} kRiskModelSpread={}\n",
            kRiskWarmSat, kRiskMinChannel, kRiskSoftDetail, kRiskHaloLumMax, kRiskMaxKeySat, kRiskMaxVcSat, kRiskTexColoredFrac,
            kRiskTexMeanSat, kRiskSheetRatio, kRiskSheetCover, kRiskVanishRatio, kRiskMinSrcEnergy, kRiskMinGBand, kRiskDarkLum,
            kRiskModelSpread);
        text += "# model (Community Shaders' Effect.hlsl; the vanilla shader may differ): palette colour by the texture's green on "
                "the row base red x vertex red, x the emissive multiple; palette alpha by the texture's alpha on the row base "
                "alpha x vertex alpha; the emissive multiple scales palette shapes only; an empty source texture slot is taken "
                "as one white texel (assumed, Z17)\n";
        text += fmt::format("SUMMARY\tnifs={}\tshapes={}\tfail={}\twarn={}\tunread={}\tmissing={}", a_job.nifs, a_job.shapes,
            RiskCount(a_job, RiskLevel::kFail), RiskCount(a_job, RiskLevel::kWarn), RiskCount(a_job, RiskLevel::kUnread),
            RiskCount(a_job, RiskLevel::kMissing));
        for (const auto& [rule, count] : a_job.perRule) {
            text += fmt::format("\t{}={}", rule, count);
        }
        text += "\n";
        for (const auto level : { RiskLevel::kFail, RiskLevel::kWarn, RiskLevel::kUnread, RiskLevel::kMissing }) {
            for (const auto& f : a_job.findings) {
                if (f.level == level) {
                    text += fmt::format("{}\t{}\t{}\t{}\t{}\t{}\n", LevelName(f.level), f.rule, f.nif, f.block, f.name, f.detail);
                }
            }
        }
        std::error_code ec;
        std::filesystem::create_directories(a_file.parent_path(), ec);
        std::ofstream out(a_file, std::ios::binary | std::ios::trunc);
        out << text;
        out.close();  // X9: judged after the close (a failed flush shows only then)
        if (!out || ec) {
            a_why = fmt::format("writing {} failed{}", a_file.string(), ec ? " (" + ec.message() + ")" : std::string());
            return false;
        }
        return true;
    }
}
