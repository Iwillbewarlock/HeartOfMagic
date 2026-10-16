// =============================================================================
// GreyRisk - the offline visual-risk check of the builder's own output (GreyRisk.h)
// =============================================================================

#include "GreyRisk.h"
#include "GreyTexture.h"

#include <Animation.hpp>
#include <NifFile.hpp>
#include <Particles.hpp>

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <fstream>
#include <limits>
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
            int warmSoft = 0;                 // drawn soft additive warm shapes (G1 by the glow rules)
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

        // A texture the rule needs and cannot have: MISSING when the game itself lacks it, UNREAD otherwise
        void NotRead(Context& a_ctx, const EffectShape& a_shape, const TextureLook& a_look, const std::string& a_slot,
            const char* a_what)
        {
            const bool missing = a_look.status == TextureRead::kMissing;
            Add(a_ctx, missing ? RiskLevel::kMissing : RiskLevel::kUnread, missing ? kRuleMissing : kRuleUnread, &a_shape,
                fmt::format("{} {}: {}", a_what, a_look.path.empty() ? a_slot : a_look.path, a_look.why));
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

        void CheckPalette(Context& a_ctx, const EffectShape& a_source, const EffectShape& a_copy, const TexStats* a_stats)
        {
            const auto texture = Look(a_ctx.job, a_source.sourceTexture);
            const auto copyTexture = Look(a_ctx.job, a_copy.sourceTexture);  // X6: the copy side with the copy's own texture
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
            const bool paletteAlpha = (a_source.flags1 & kSlsf1GreyscaleToPaletteAlpha) != 0;
            const bool vertexOn = (a_source.flags2 & kSlsf2VertexColors) != 0 && a_source.vertexColours;
            const double v = std::clamp(a_source.baseColour.r * (vertexOn ? a_source.vertexMean.r : 1.0), 0.0, 1.0);
            const auto src = SimulatePalette(*texture.image, *sourcePalette.image, paletteAlpha, v, kRiskDarkLum);
            const auto copy = SimulatePalette(*copyTexture.image, *copyPalette.image, paletteAlpha, v, kRiskDarkLum);
            const char* alphaNote = paletteAlpha ? " (palette alpha)" : "";
            if (a_source.blend == ShapeBlend::kTest) {
                // X7: alpha test without blending draws an opaque cut-out: the colour rules apply, the energy and coverage
                // rules (sheet, vanish) do not - the cut-out's outline is the texture's alpha test, not a blend
            } else if (a_source.blend == ShapeBlend::kAdditive) {
                const double ratio = src.energy > kRiskEnergyFloor ? a_ctx.job.tintAdd * copy.energy / src.energy :
                                     copy.energy > kRiskEnergyFloor ? std::numeric_limits<double>::infinity() : 1.0;
                if (ratio > kRiskSheetRatio && copy.coverage >= kRiskSheetCover) {
                    Add(a_ctx, RiskLevel::kFail, kRuleSheet, &a_source, fmt::format("energy x{}, coverage {:.2f} -> {:.2f}{}",
                        Ratio(ratio), src.coverage, copy.coverage, alphaNote));
                } else if (ratio > kRiskSheetRatio) {
                    Add(a_ctx, RiskLevel::kWarn, kRuleBrighter, &a_source, fmt::format("energy x{}", Ratio(ratio)));
                }
                if (ratio < kRiskVanishRatio && src.energy >= kRiskMinSrcEnergy) {
                    Add(a_ctx, RiskLevel::kFail, kRuleVanish, &a_source, fmt::format("energy x{}, {:.0f}% of lit texels on the "
                        "palette dark band", Ratio(ratio), copy.darkFrac * 100.0));
                }
            } else {
                const double ratio = src.coverage > kRiskEnergyFloor ? copy.coverage / src.coverage :
                                     copy.coverage > kRiskEnergyFloor ? std::numeric_limits<double>::infinity() : 1.0;
                if (ratio > kRiskSheetRatio && copy.coverage >= kRiskSheetCover) {
                    Add(a_ctx, RiskLevel::kFail, kRuleSheet, &a_source, fmt::format("normal blend coverage {:.2f} -> {:.2f}{}",
                        src.coverage, copy.coverage, alphaNote));
                }
            }
            if (src.uRange < kRiskMinGBand && !(a_stats && a_stats->energyMean == 0.0)) {
                Add(a_ctx, RiskLevel::kWarn, kRuleFlat, &a_source, fmt::format("texture lit green range {:.3f}", src.uRange));
            }
        }

        void CheckPlain(Context& a_ctx, const EffectShape& a_source, const EffectShape& a_copy, const TextureLook& a_texture,
            const TextureLook& a_copyTexture)
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
            const bool coloured = copyStats && IsColouredTexture(*copyStats);
            if (coloured) {
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
            for (const auto& colour : a_source.colours) {
                sourceSat = std::max(sourceSat, Saturation(Multiply(Multiply(colour, texMean), sourceVertex), kRiskMinChannel));
            }
            double copyLum = 0.0;
            for (const auto& colour : a_copy.colours) {
                copyLum = std::max(copyLum, Luminance(Multiply(Multiply(colour, copyTexMean), copyVertex)));
            }
            copyLum *= a_copy.multipleMax;
            a_ctx.warmSoft += soft && sourceSat >= kRiskWarmSat ? 1 : 0;
            if (soft && sourceSat >= kRiskWarmSat && copyLum > kRiskHaloLumMax) {
                a_ctx.halos.push_back({ RiskLevel::kFail, kRuleHalo, a_ctx.nif, a_source.block, a_source.name,
                    fmt::format("soft additive {}, source S {:.2f}, copy grey luminance {:.3f}{}", a_source.sourceTexture.empty() ?
                        std::string("no texture") : fmt::format("texture {} (detail {})", a_texture.path, stats ? stats->detail : 0.0),
                        sourceSat, copyLum, a_source.billboard ? ", under a billboard node" : "") });
            } else if (soft && copyLum > kRiskHaloLumMax) {
                Add(a_ctx, RiskLevel::kWarn, kRuleNeutralHalo, &a_source, fmt::format("soft additive neutral glow, copy "
                    "luminance {:.3f}", copyLum));
            }
        }

        // Drawn at all: shown, blended, a texture that is not blank, a colour that is not black (a palette shape's colour
        // comes from its palette)
        [[nodiscard]] bool Visible(const EffectShape& a_shape, const TextureLook& a_texture)
        {
            if (a_shape.hidden || a_shape.blend == ShapeBlend::kNoAlpha || a_shape.blend == ShapeBlend::kOpaque) {
                return false;
            }
            if (a_texture.stats && a_texture.stats->energyMean == 0.0) {
                return false;
            }
            return IsPaletteShape(a_shape) || MaxChannel(a_shape.colours) > 0.0;
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

    void RiskCheckNif(nifly::NifFile& a_source, nifly::NifFile& a_copy, const std::string& a_nif, RiskJob& a_job)
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
            if (IsPaletteShape(source)) {
                CheckPalette(ctx, source, copy, texture.stats);
            } else {
                CheckPlain(ctx, source, copy, texture, copyTexture);
            }
        }
        // The G1 exception (GreyGlow.h): when every drawn shape is a soft warm glow, RGB 0 would empty the effect, so the
        // glow rules keep them visible - a WARN (HALO_KEPT), not a failure
        const bool kept = visibleBefore > 0 && ctx.warmSoft == visibleBefore;
        for (auto& halo : ctx.halos) {
            if (kept) {
                halo.level = RiskLevel::kWarn;
                halo.rule = kRuleHaloKept;
                halo.detail += " - the NIF's only drawn shape(s): kept visible (G2) so the effect is not empty";
            }
            ++a_job.perRule[halo.rule];
            a_job.findings.push_back(std::move(halo));
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
                            "kRiskVanishRatio={} kRiskMinSrcEnergy={} kRiskMinGBand={} kRiskDarkLum={} tintAdd={}\n",
            kRiskWarmSat, kRiskMinChannel, kRiskSoftDetail, kRiskHaloLumMax, kRiskMaxKeySat, kRiskMaxVcSat, kRiskTexColoredFrac,
            kRiskTexMeanSat, kRiskSheetRatio, kRiskSheetCover, kRiskVanishRatio, kRiskMinSrcEnergy, kRiskMinGBand, kRiskDarkLum,
            a_job.tintAdd);
        text += "# model (not checked against the game): palette colour by the texture's green, palette alpha by its alpha\n";
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
