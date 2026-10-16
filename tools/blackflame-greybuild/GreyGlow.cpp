// =============================================================================
// GreyGlow - the colour rules of the full-grey copies after GreyEffects (GreyGlow.h)
// =============================================================================

#include "GreyGlow.h"

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

        /// How a shader's colours become grey: luminance x scale
        struct ColourRule
        {
            float scale = kDefaultTint;
        };

        [[nodiscard]] nifly::Vector3 Apply(const ColourRule& a_rule, const nifly::Vector3& a_value) noexcept
        {
            const float grey = Luminance(a_value.x, a_value.y, a_value.z) * a_rule.scale;
            return { grey, grey, grey };
        }

        /// GreyEffects' own rule, kept for every shader: an additive palette shader's colours x tintAdd, every other x tint
        [[nodiscard]] ColourRule RuleFor(const std::vector<const EffectShape*>& a_shapes, const GlowContext& a_ctx)
        {
            const bool palette = std::any_of(a_shapes.begin(), a_shapes.end(), [](const EffectShape* a_s) { return IsPaletteShape(*a_s); });
            const bool additive =
                std::any_of(a_shapes.begin(), a_shapes.end(), [](const EffectShape* a_s) { return a_s->blend == ShapeBlend::kAdditive; });
            return { palette && additive ? a_ctx.tintAdd : a_ctx.tint };
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
                    const auto byte = static_cast<std::uint8_t>(std::clamp(std::lround(grey * kByteMax), 0L, 255L));
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
        // K: every reached colour key with its shader's rule; an interpolator two shaders share keeps the first rule
        std::set<std::uint32_t> done;
        for (const auto& [shader, users] : byShader) {
            const ColourRule rule = RuleFor(users, a_ctx);
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
        return fmt::format("glow  colour keys reached through controlled blocks greyed {}; blend interpolator values greyed {}; "
                           "colour controllers without keys {}; vertex colours to luminance on {} shape(s) ({} left: geometry a "
                           "palette shape shares)", a_totals.sequenceKeys, a_totals.blendValues, a_totals.unreached,
            a_totals.vertexShapes, a_totals.vertexShared);
    }
}
