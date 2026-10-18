// =============================================================================
// GreyGlow - the colour rules of the full-grey copies after GreyEffects (GreyGlow.h)
// =============================================================================

#include "GreyGlow.h"
#include "GreyRisk.h"
#include "GreyTexture.h"

#include <Animation.hpp>
#include <Geometry.hpp>
#include <NifFile.hpp>
#include <Shaders.hpp>

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <iostream>
#include <map>
#include <set>

namespace GreyBuild
{
    namespace
    {
        constexpr float kByteMax = 255.0f;
        constexpr long kByteTop = 255;           // Y19: the largest 8-bit value

        /// How a shader's colours become grey: luminance x scale, or (a derived palette's shader) the red kept as grey
        struct ColourRule
        {
            float scale = kDefaultTint;
            bool red = false;
            bool g2 = false;  // Y19: decided as G2 (counted by this, not by comparing scales)
        };

        [[nodiscard]] nifly::Vector3 Apply(const ColourRule& a_rule, const nifly::Vector3& a_value) noexcept
        {
            const float grey = a_rule.red ? a_value.x : Luminance(a_value.x, a_value.y, a_value.z) * a_rule.scale;
            return { grey, grey, grey };
        }

        // Y2: a derived additive palette's brightness: the emissive multiple scales the palette output (Effect.hlsl:
        // baseColor.xyz = BaseColorScale.x x palette; the base colour only picks the row), so --tint-add goes there - the
        // shader's baseColorScale and every emissive-multiple value and key, from the source's
        void ScaleEmissive(nifly::NifFile& a_copy, nifly::NifFile& a_source, const std::set<std::uint32_t>& a_shaders,
            float a_scale, GlowContext& a_ctx, const std::string& a_rel)
        {
            if (a_shaders.empty()) {
                return;
            }
            auto& copyHdr = a_copy.GetHeader();
            auto& sourceHdr = a_source.GetHeader();
            for (const auto shader : a_shaders) {  // the property's own value: never shared
                auto* copy = copyHdr.GetBlock<nifly::BSEffectShaderProperty>(shader);
                const auto* source = sourceHdr.GetBlock<nifly::BSEffectShaderProperty>(shader);
                if (copy && source) {
                    copy->baseColorScale = source->baseColorScale * a_scale;
                }
            }
            // Z1: a controller block is scaled only when every controller that reaches it drives the emissive multiple of a
            // shader scaled here; one also shared with another variable (alpha, UV) or another shader is left and noted
            std::map<std::uint32_t, bool> scalable;  // block -> every user qualifies
            for (const auto& use : FloatControllerUses(a_source)) {
                const bool ours = use.variable == kFloatVarEmissiveMultiple && a_shaders.count(use.shader) != 0;
                auto it = scalable.try_emplace(use.block, ours).first;  // every user must qualify
                it->second = it->second && ours;
            }
            std::set<std::uint32_t> touched;
            for (const auto& use : FloatControllerUses(a_source)) {
                if (use.variable != kFloatVarEmissiveMultiple || a_shaders.count(use.shader) == 0 || !touched.insert(use.block).second) {
                    continue;
                }
                if (!scalable[use.block]) {
                    ++a_ctx.totals.sharedEmissive;
                    std::cout << fmt::format("NOTE  {}: block {} drives the emissive multiple of shader {} and also another variable "
                                             "or shader: left as it is (not x tintAdd)\n", a_rel, use.block, use.shader);
                    continue;
                }
                const auto scaled = [&](float a_value) {
                    return a_value > kUnusedKeyValue && a_value < kHugeFloatValue ? a_value * a_scale : a_value;
                };
                if (auto* interp = copyHdr.GetBlock<nifly::NiFloatInterpolator>(use.block)) {
                    if (const auto* source = sourceHdr.GetBlock<nifly::NiFloatInterpolator>(use.block)) {
                        interp->floatValue = scaled(source->floatValue);
                    }
                } else if (auto* blend = copyHdr.GetBlock<nifly::NiBlendFloatInterpolator>(use.block)) {
                    if (const auto* source = sourceHdr.GetBlock<nifly::NiBlendFloatInterpolator>(use.block)) {
                        blend->value = scaled(source->value);  // Z2
                    }
                } else if (auto* data = copyHdr.GetBlock<nifly::NiFloatData>(use.block)) {
                    const auto* source = sourceHdr.GetBlock<nifly::NiFloatData>(use.block);
                    if (!source) {
                        continue;
                    }
                    const auto count = static_cast<int>(std::min(data->data.GetNumKeys(), source->data.GetNumKeys()));
                    for (int i = 0; i < count; ++i) {
                        const auto from = source->data.GetKey(i);
                        auto key = data->data.GetKey(i);
                        key.value = scaled(from.value);
                        key.forward = from.forward * a_scale;
                        key.backward = from.backward * a_scale;
                        data->data.SetKey(i, key);
                    }
                }
                ++a_ctx.totals.emissiveBlocks;
            }
        }

        // P: a palette shader's derived palette (GreyPalette.h); false keeps GreyEffects' palette and rule
        bool TakeDerivedPalette(nifly::NifFile& a_copy, std::uint32_t a_shader, const std::vector<const EffectShape*>& a_users,
            GlowContext& a_ctx, const std::string& a_rel)
        {
            if (!a_ctx.palettes || a_ctx.palettes->mode != PaletteMode::kDerived) {
                return false;
            }
            const auto* first = a_users.front();
            const bool additive = AnyAdditive(a_users);
            std::string why;
            const auto slot = DerivedPalette(*a_ctx.palettes, first->greyscaleTexture, additive, why);
            if (slot.empty()) {
                std::cout << fmt::format("NOTE  {}: effect shader block {}: {} - today's palette kept\n", a_rel, a_shader, why);
                return false;
            }
            if (auto* shader = a_copy.GetHeader().GetBlock<nifly::BSEffectShaderProperty>(a_shader)) {
                shader->greyscaleTexture.get() = slot;
            }
            return true;
        }

        enum class Glow
        {
            kG0,  // not drawn: hidden, no blending, a blank texture, colour 0 - luminance x tint, nothing judged
            kG1,  // additive, soft, warm: RGB 0 (its role was the orange haze; grey it would float a grey disc or ball)
            kG2,  // additive with detail (flames, sparks): luminance x tintAdd, the silver edge
            kG3,  // additive, soft, neutral (white before): luminance x tint, a WARN in the risk check
            kG5   // normal blend: luminance x tint (grey smoke reads as the body of a black fire)
        };

        struct Judged
        {
            Glow glow = Glow::kG0;
            bool unread = false;  // the texture could not be read: G2 (soft or not cannot be told)
            std::string why;
        };

        // The class of one shape without the palette flag (glow-rule.md section 3, the same measures as the risk check)
        [[nodiscard]] Judged Classify(const EffectShape& a_shape, GlowContext& a_ctx)
        {
            if (a_shape.hidden || a_shape.blend == ShapeBlend::kNoAlpha || a_shape.blend == ShapeBlend::kOpaque) {
                return { Glow::kG0 };
            }
            if (a_shape.blend != ShapeBlend::kAdditive) {
                return { Glow::kG5 };
            }
            const TexStats* stats = nullptr;
            if (!a_shape.sourceTexture.empty()) {
                if (!a_ctx.textures) {
                    return { Glow::kG2, true, "no texture source in this run" };
                }
                const auto look = a_ctx.textures->LookStats(a_shape.sourceTexture);  // statistics only
                if (look.status != TextureRead::kOk) {
                    return { Glow::kG2, true, fmt::format("{}: {}", look.path.empty() ? a_shape.sourceTexture : look.path, look.why) };
                }
                stats = look.stats;
                if (stats->energyMean == 0.0) {
                    return { Glow::kG0 };  // a blank texture draws nothing
                }
            }
            const Rgb white{ 1.0, 1.0, 1.0 };
            const Rgb texMean = stats ? stats->meanRgb : white;
            const bool vertexOn = (a_shape.flags2 & kSlsf2VertexColors) != 0 && a_shape.vertexColours;
            const Rgb vertex = vertexOn ? a_shape.vertexMean : white;
            double sat = 0.0;
            double high = 0.0;
            for (const auto& shown : ShownColours(a_shape, texMean, vertex)) {  // Z3: the risk check's measure too
                sat = std::max(sat, Saturation(shown, kRiskMinChannel));
                high = std::max({ high, shown.r, shown.g, shown.b });
            }
            if (high <= 0.0) {
                return { Glow::kG0 };
            }
            const bool soft = !stats || stats->detail < kRiskSoftDetail;
            if (!soft) {
                return { Glow::kG2 };
            }
            return { sat >= kRiskWarmSat ? Glow::kG1 : Glow::kG3 };
        }

        /// A shader's rule from its shapes: a palette shader keeps GreyEffects' (additive x tintAdd, else x tint); every
        /// other one by its shapes' classes - 0 when every drawn shape is G1, tintAdd when one is G2, else tint
        [[nodiscard]] ColourRule RuleFor(const std::vector<const EffectShape*>& a_shapes, GlowContext& a_ctx, bool& a_zeroed,
            const std::string& a_rel)
        {
            a_zeroed = false;
            const bool palette = std::any_of(a_shapes.begin(), a_shapes.end(), [](const EffectShape* a_s) { return IsPaletteShape(*a_s); });
            const bool additive = AnyAdditive(a_shapes);
            if (palette) {
                return { additive ? a_ctx.tintAdd : a_ctx.tint };
            }
            bool anyG2 = false;
            bool allG1 = true;
            bool anyG1 = false;
            bool drawn = false;
            std::string other;  // Y18: the first drawn shape that is not G1
            for (const auto* shape : a_shapes) {
                if (shape->hidden) {
                    allG1 = false;  // Y5: a hidden shape may be shown later (a visibility controller): never RGB 0 under it
                }
                const auto judged = Classify(*shape, a_ctx);
                if (judged.unread) {
                    ++a_ctx.totals.unread;
                    std::cout << fmt::format("NOTE  {}: shape block {} '{}': texture not read ({}) - G2 (luminance x tintAdd)\n",
                        a_rel, shape->block, shape->name, judged.why);
                }
                if (judged.glow == Glow::kG0) {
                    continue;
                }
                drawn = true;
                anyG2 |= judged.glow == Glow::kG2;
                allG1 &= judged.glow == Glow::kG1;
                anyG1 |= judged.glow == Glow::kG1;
                if (judged.glow != Glow::kG1 && other.empty()) {
                    other = fmt::format("'{}' (block {})", shape->name, shape->block);
                }
                a_ctx.totals.g3 += judged.glow == Glow::kG3 ? 1 : 0;
                a_ctx.totals.g5 += judged.glow == Glow::kG5 ? 1 : 0;
            }
            if (drawn && allG1) {
                a_zeroed = true;
                return { 0.0f };
            }
            if (anyG1) {  // Y18: one shader for a soft warm glow and another kind of shape: no RGB 0, said once
                ++a_ctx.totals.mixed;
                a_ctx.mixed[a_rel][a_shapes.front()->shader] = fmt::format("shares its shader with {}: kept out of G1",
                    other.empty() ? std::string("a hidden shape") : other);  // Z4: the risk check's GREY_HALO says why
                std::cout << fmt::format("NOTE  {}: a soft warm glow shares its shader with {}: not RGB 0 (G1), {}\n", a_rel,
                    other.empty() ? std::string("a hidden shape") : other, anyG2 ? "luminance x tintAdd" : "luminance x tint");
            }
            return { anyG2 ? a_ctx.tintAdd : a_ctx.tint, false, anyG2 };
        }

        // EFFECT_EMPTY's exception: drawn at all in the source (the risk check's measure)
        [[nodiscard]] bool Drawn(const EffectShape& a_shape, GlowContext& a_ctx)
        {
            const TexStats* stats = nullptr;
            if (!a_shape.sourceTexture.empty() && a_ctx.textures) {
                stats = a_ctx.textures->LookStats(a_shape.sourceTexture).stats;
            }
            return IsDrawn(a_shape, stats);  // Z5: the risk check's own measure (GreyRisk.h)
        }

        // K: one interpolator and its keys from the source's values (the unused marker kept as it is)
        void GreyKeys(nifly::NifFile& a_copy, nifly::NifFile& a_source, const ColourKeys& a_keys, const ColourRule& a_rule)
        {
            auto& copyHdr = a_copy.GetHeader();
            auto& sourceHdr = a_source.GetHeader();
            auto* interp = copyHdr.GetBlock<nifly::NiPoint3Interpolator>(a_keys.interpolator);
            const auto* sourceInterp = sourceHdr.GetBlock<nifly::NiPoint3Interpolator>(a_keys.interpolator);
            if (!interp || !sourceInterp) {
                return;
            }
            if (sourceInterp->point3Value.x > kUnusedKeyValue) {
                interp->point3Value = Apply(a_rule, sourceInterp->point3Value);
            }
            auto* data = copyHdr.GetBlock<nifly::NiPosData>(a_keys.data);
            const auto* sourceData = sourceHdr.GetBlock<nifly::NiPosData>(a_keys.data);
            if (!data || !sourceData) {
                return;
            }
            const auto count = static_cast<int>(std::min(data->data.GetNumKeys(), sourceData->data.GetNumKeys()));
            for (int i = 0; i < count; ++i) {
                const auto source = sourceData->data.GetKey(i);
                auto key = data->data.GetKey(i);
                key.value = Apply(a_rule, source.value);
                key.forward = Apply(a_rule, source.forward);  // tangents: the same linear map keeps the curve grey
                key.backward = Apply(a_rule, source.backward);
                data->data.SetKey(i, key);
            }
        }

        // K: a colour controller whose own interpolator is a blend interpolator (its keys come through controlled blocks)
        // keeps a value of its own; greyed with the shader's rule. Returns the controllers greyed so
        int GreyBlendValues(nifly::NifFile& a_copy, nifly::NifFile& a_source, std::uint32_t a_shader, const ColourRule& a_rule)
        {
            auto& copyHdr = a_copy.GetHeader();
            auto& sourceHdr = a_source.GetHeader();
            int greyed = 0;
            for (std::uint32_t id = 0; id < sourceHdr.GetNumBlocks(); ++id) {
                const auto* ctrl = sourceHdr.GetBlock<nifly::BSEffectShaderPropertyColorController>(id);
                if (!ctrl || ctrl->targetRef.index != a_shader) {
                    continue;
                }
                const auto* source = sourceHdr.GetBlock<nifly::NiBlendPoint3Interpolator>(ctrl->interpolatorRef.index);
                auto* copy = copyHdr.GetBlock<nifly::NiBlendPoint3Interpolator>(ctrl->interpolatorRef.index);
                if (source && copy && source->point.x > kUnusedKeyValue) {
                    copy->point = Apply(a_rule, source->point);
                    ++greyed;
                }
            }
            return greyed;
        }

        // V: one geometry's vertex colours to their luminance, alpha kept
        bool GreyVertexColours(nifly::NifFile& a_copy, std::uint32_t a_block)
        {
            auto& hdr = a_copy.GetHeader();
            if (auto* tri = hdr.GetBlock<nifly::BSTriShape>(a_block)) {
                for (auto& vertex : tri->vertData) {
                    const float grey = Luminance(vertex.colorData[0] / kByteMax, vertex.colorData[1] / kByteMax,
                        vertex.colorData[2] / kByteMax);
                    const auto byte = static_cast<std::uint8_t>(std::clamp(std::lround(grey * kByteMax), 0L, kByteTop));
                    vertex.colorData[0] = byte;
                    vertex.colorData[1] = byte;
                    vertex.colorData[2] = byte;
                }
                return true;
            }
            if (auto* data = hdr.GetBlock<nifly::NiGeometryData>(a_block)) {
                for (auto& colour : data->vertexColors) {
                    const float grey = Luminance(colour.r, colour.g, colour.b);
                    colour.r = grey;
                    colour.g = grey;
                    colour.b = grey;
                }
                return true;
            }
            return false;
        }
    }

    void ApplyGlowRules(nifly::NifFile& a_copy, nifly::NifFile& a_source, GlowContext& a_ctx, const std::string& a_rel)
    {
        const auto shapes = EffectShapes(a_source);
        std::map<std::uint32_t, std::vector<const EffectShape*>> byShader;
        for (const auto& shape : shapes) {
            byShader[shape.shader].push_back(&shape);
        }
        // G: each shader's rule; G1 (RGB 0) only while the NIF keeps a drawn shape - else those shaders take G2
        std::map<std::uint32_t, ColourRule> rules;
        std::set<std::uint32_t> zeroed;
        std::set<std::uint32_t> emissiveShaders;  // derived additive palettes: --tint-add on their emissive multiple
        for (const auto& [shader, users] : byShader) {
            bool zero = false;
            rules[shader] = RuleFor(users, a_ctx, zero, a_rel);
            if (zero) {
                zeroed.insert(shader);
            }
            if (IsPaletteShape(*users.front()) && TakeDerivedPalette(a_copy, shader, users, a_ctx, a_rel)) {
                rules[shader].red = true;
                if (AnyAdditive(users)) {
                    emissiveShaders.insert(shader);
                }
            }
        }
        ScaleEmissive(a_copy, a_source, emissiveShaders, a_ctx.tintAdd, a_ctx, a_rel);
        int drawnBefore = 0;
        int drawnAfter = 0;
        for (const auto& shape : shapes) {
            if (Drawn(shape, a_ctx)) {
                ++drawnBefore;
                drawnAfter += zeroed.count(shape.shader) == 0 ? 1 : 0;
            }
        }
        if (!zeroed.empty() && drawnBefore > 0 && drawnAfter == 0) {
            for (const auto shader : zeroed) {
                rules[shader] = { a_ctx.tintAdd, false, true };
            }
            ++a_ctx.totals.g1Kept;
            a_ctx.haloKept.insert(a_rel);  // Y7: the risk check lists its GREY_HALO as HALO_KEPT
            std::cout << fmt::format("NOTE  {}: every drawn shape is a soft warm glow (G1): RGB 0 would leave the effect empty - "
                                     "G2 (luminance x tintAdd) instead\n", a_rel);
            zeroed.clear();
        }
        a_ctx.totals.g1 += static_cast<int>(zeroed.size());
        // Base colours (alpha kept) and K: every reached colour key with its shader's rule; an interpolator two shaders
        // share keeps the first rule
        std::set<std::uint32_t> done;
        for (const auto& [shader, users] : byShader) {
            const ColourRule rule = rules[shader];
            a_ctx.totals.g2 += rule.g2 ? 1 : 0;
            auto* copyShader = a_copy.GetHeader().GetBlock<nifly::BSEffectShaderProperty>(shader);
            const auto* sourceShader = a_source.GetHeader().GetBlock<nifly::BSEffectShaderProperty>(shader);
            if (copyShader && sourceShader) {
                const auto grey = Apply(rule, { sourceShader->baseColor.r, sourceShader->baseColor.g, sourceShader->baseColor.b });
                copyShader->baseColor.r = grey.x;
                copyShader->baseColor.g = grey.y;
                copyShader->baseColor.b = grey.z;
            }
            const auto& first = *users.front();
            for (const auto& keys : first.colourKeys) {
                if (!done.insert(keys.interpolator).second) {
                    continue;
                }
                GreyKeys(a_copy, a_source, keys, rule);
                if (keys.viaSequence) {
                    if (const auto* data = a_source.GetHeader().GetBlock<nifly::NiPosData>(keys.data)) {
                        a_ctx.totals.sequenceKeys += static_cast<int>(data->data.GetNumKeys());
                    }
                }
            }
            a_ctx.totals.blendValues += GreyBlendValues(a_copy, a_source, shader, rule);
            if (first.unreachedControllers > 0) {
                a_ctx.totals.unreached += first.unreachedControllers;
                std::cout << fmt::format("NOTE  {}: effect shader block {}: {} colour controller(s) with no NiPoint3Interpolator "
                                         "reached directly or through a controlled block (no keys; a blend interpolator's own value "
                                         "is greyed)\n", a_rel, shader, first.unreachedControllers);
            }
        }
        // V: geometry no palette shape uses
        std::set<std::uint32_t> paletteGeometry;
        for (const auto& shape : shapes) {
            if (IsPaletteShape(shape) && shape.vertexColours) {
                paletteGeometry.insert(shape.vertexBlock);
            }
        }
        std::set<std::uint32_t> greyed;
        for (const auto& shape : shapes) {
            if (IsPaletteShape(shape) || !shape.vertexColours || (shape.flags2 & kSlsf2VertexColors) == 0) {
                continue;
            }
            if (paletteGeometry.count(shape.vertexBlock) != 0) {
                ++a_ctx.totals.vertexShared;
                continue;
            }
            if (greyed.insert(shape.vertexBlock).second && GreyVertexColours(a_copy, shape.vertexBlock)) {
                ++a_ctx.totals.vertexShapes;
            }
        }
    }

    std::string GlowTotalsLine(const GlowTotals& a_totals)
    {
        return fmt::format("glow  shaders: G1 RGB 0 {} ({} NIF(s) kept at G2: nothing else drawn), G2 x tintAdd {}; shapes: G3 "
                           "soft neutral {}, G5 normal blend {}, texture not read {}; colour keys reached through controlled blocks "
                           "greyed {}; blend interpolator values greyed {}; colour controllers without keys {}; vertex colours to "
                           "luminance on {} shape(s) ({} left: geometry a palette shape shares)", a_totals.g1, a_totals.g1Kept,
            a_totals.g2, a_totals.g3, a_totals.g5, a_totals.unread, a_totals.sequenceKeys, a_totals.blendValues,
            a_totals.unreached, a_totals.vertexShapes, a_totals.vertexShared) +
               fmt::format("; shaders shared by a soft warm glow and another kind of shape (no G1) {}; emissive-multiple "
                           "controller blocks x tintAdd {}, left as shared with another variable or shader {}",
                   a_totals.mixed, a_totals.emissiveBlocks, a_totals.sharedEmissive);
    }
}
