// =============================================================================
// GreyShapes - every effect-shader shape of a NIF with what its look depends on (GreyShapes.h)
// =============================================================================

#include "GreyShapes.h"

#include <Animation.hpp>
#include <Geometry.hpp>
#include <NifFile.hpp>
#include <Nodes.hpp>
#include <Particles.hpp>
#include <Shaders.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

namespace GreyBuild
{
    namespace
    {
        constexpr std::uint16_t kAlphaBlendOn = 1u;
        constexpr int kDestBlendShift = 5;
        constexpr std::uint16_t kBlendModeMask = 0xF;
        constexpr std::uint16_t kBlendOne = 0;
        constexpr std::uint32_t kHiddenFlag = 1u;   // NiAVObject flags bit 0
        constexpr int kMaxDepth = 64;               // a parent chain longer than this is a loop
        constexpr double kByteMax = 255.0;
        constexpr double kCensusSatFloor = 1e-6;    // the census's own saturation guard for vertex colours
        constexpr const char* kEffectPropertyType = "BSEffectShaderProperty";

        [[nodiscard]] ShapeBlend BlendOf(const nifly::NiAlphaProperty* a_alpha) noexcept
        {
            if (!a_alpha) {
                return ShapeBlend::kNoAlpha;
            }
            if ((a_alpha->flags & kAlphaBlendOn) == 0) {
                return (a_alpha->flags & kAlphaTestOn) != 0 ? ShapeBlend::kTest : ShapeBlend::kOpaque;
            }
            const auto dest = static_cast<std::uint16_t>((a_alpha->flags >> kDestBlendShift) & kBlendModeMask);
            return dest == kBlendOne ? ShapeBlend::kAdditive : dest == kBlendInvSrcAlpha ? ShapeBlend::kNormal : ShapeBlend::kOther;
        }

        [[nodiscard]] std::map<std::uint32_t, std::uint32_t> Parents(nifly::NifFile& a_nif)
        {
            auto& hdr = a_nif.GetHeader();
            std::map<std::uint32_t, std::uint32_t> parent;
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                if (auto* node = hdr.GetBlock<nifly::NiNode>(id)) {
                    for (auto& child : node->childRefs) {
                        if (!child.IsEmpty()) {
                            parent[child.index] = id;
                        }
                    }
                }
            }
            return parent;
        }

        // a_test on the block and every ancestor
        template <class Test>
        [[nodiscard]] bool OnChain(nifly::NifFile& a_nif, const std::map<std::uint32_t, std::uint32_t>& a_parent,
            std::uint32_t a_block, bool a_includeSelf, const Test& a_test)
        {
            auto& hdr = a_nif.GetHeader();
            std::uint32_t current = a_block;
            if (!a_includeSelf) {
                const auto it = a_parent.find(current);
                if (it == a_parent.end()) {
                    return false;
                }
                current = it->second;
            }
            for (int depth = 0; depth < kMaxDepth; ++depth) {
                if (a_test(hdr, current)) {
                    return true;
                }
                const auto it = a_parent.find(current);
                if (it == a_parent.end()) {
                    return false;
                }
                current = it->second;
            }
            return false;
        }

        // A controlled block without a controller reference names it by node name, property type and controller type:
        // the first controller of that type on the effect shader of a shape with that name
        [[nodiscard]] std::uint32_t ControllerByNames(nifly::NifFile& a_nif, const nifly::ControllerLink& a_link)
        {
            if (a_link.propType.get() != kEffectPropertyType) {
                return nifly::NIF_NPOS;
            }
            auto& hdr = a_nif.GetHeader();
            const std::string& node = a_link.nodeName.get();
            const std::string& type = a_link.ctrlType.get();
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                std::uint32_t shader = nifly::NIF_NPOS;
                if (auto* shape = hdr.GetBlock<nifly::NiShape>(id)) {
                    if (shape->name.get() == node && shape->ShaderPropertyRef()) {
                        shader = shape->ShaderPropertyRef()->index;
                    }
                } else if (auto* system = hdr.GetBlock<nifly::NiParticleSystem>(id)) {
                    if (system->name.get() == node) {
                        shader = system->shaderPropertyRef.index;
                    }
                }
                const auto* effect = hdr.GetBlock<nifly::BSEffectShaderProperty>(shader);
                if (!effect) {
                    continue;
                }
                std::uint32_t ctrl = effect->controllerRef.index;
                std::set<std::uint32_t> seen;
                while (ctrl != nifly::NIF_NPOS && seen.insert(ctrl).second) {
                    auto* block = hdr.GetBlock<nifly::NiTimeController>(ctrl);
                    if (!block) {
                        break;
                    }
                    if (type == block->GetBlockName()) {
                        return ctrl;
                    }
                    ctrl = block->nextControllerRef.index;
                }
            }
            return nifly::NIF_NPOS;
        }

        // controller -> the interpolators NiControllerSequence controlled blocks give it
        [[nodiscard]] std::map<std::uint32_t, std::vector<std::uint32_t>> SequenceInterpolators(nifly::NifFile& a_nif)
        {
            auto& hdr = a_nif.GetHeader();
            std::map<std::uint32_t, std::vector<std::uint32_t>> out;
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                auto* sequence = hdr.GetBlock<nifly::NiControllerSequence>(id);
                if (!sequence) {
                    continue;
                }
                for (auto& link : sequence->controlledBlocks) {
                    std::uint32_t ctrl = link.controllerRef.index;
                    if (ctrl == nifly::NIF_NPOS) {
                        ctrl = ControllerByNames(a_nif, link);
                    }
                    if (ctrl != nifly::NIF_NPOS && link.interpolatorRef.index != nifly::NIF_NPOS) {
                        out[ctrl].push_back(link.interpolatorRef.index);
                    }
                }
            }
            return out;
        }

        void AddPoint3Values(nifly::NifFile& a_nif, std::uint32_t a_interp, std::vector<Rgb>& a_out)
        {
            auto& hdr = a_nif.GetHeader();
            auto* interp = hdr.GetBlock<nifly::NiPoint3Interpolator>(a_interp);
            if (!interp) {
                return;
            }
            const auto& value = interp->point3Value;
            if (value.x > kUnusedKeyValue) {
                a_out.push_back({ value.x, value.y, value.z });
            }
            if (auto* data = hdr.GetBlock<nifly::NiPosData>(interp->dataRef)) {
                const auto count = static_cast<int>(data->data.GetNumKeys());
                for (int i = 0; i < count; ++i) {
                    const auto key = data->data.GetKey(i);
                    a_out.push_back({ key.value.x, key.value.y, key.value.z });
                }
            }
        }

        void AddFloatValues(nifly::NifFile& a_nif, std::uint32_t a_interp, double& a_max)
        {
            auto& hdr = a_nif.GetHeader();
            auto* interp = hdr.GetBlock<nifly::NiFloatInterpolator>(a_interp);
            if (!interp) {
                return;
            }
            const auto take = [&](float a_value) {
                if (a_value < kHugeFloatValue) {
                    a_max = std::max(a_max, static_cast<double>(a_value));
                }
            };
            if (interp->floatValue > kUnusedKeyValue) {
                take(interp->floatValue);
            }
            if (auto* data = hdr.GetBlock<nifly::NiFloatData>(interp->dataRef)) {
                const auto count = static_cast<int>(data->data.GetNumKeys());
                for (int i = 0; i < count; ++i) {
                    take(data->data.GetKey(i).value);
                }
            }
        }

        // The census's vertex colour summary: alpha-weighted mean (3 decimals) and its saturation (3 decimals)
        void VertexSummary(const std::vector<std::array<double, 4>>& a_colours, EffectShape& a_shape)
        {
            if (a_colours.empty()) {
                return;
            }
            double weight = 0.0;
            Rgb sum;
            for (const auto& c : a_colours) {
                weight += c[3];
                sum.r += c[0] * c[3];
                sum.g += c[1] * c[3];
                sum.b += c[2] * c[3];
            }
            Rgb mean;
            if (weight > 0.0) {
                mean = { sum.r / weight, sum.g / weight, sum.b / weight };
            }
            a_shape.vertexColours = true;
            a_shape.vertexMean = { Round3(mean.r), Round3(mean.g), Round3(mean.b) };
            a_shape.vertexSaturation = Round3(Saturation(a_shape.vertexMean, kCensusSatFloor));
        }

        void ReadVertexColours(nifly::NifFile& a_nif, std::uint32_t a_block, EffectShape& a_shape)
        {
            auto& hdr = a_nif.GetHeader();
            std::vector<std::array<double, 4>> colours;
            if (auto* tri = hdr.GetBlock<nifly::BSTriShape>(a_block)) {
                if (!tri->HasVertexColors()) {
                    return;
                }
                for (const auto& vertex : tri->vertData) {
                    colours.push_back({ vertex.colorData[0] / kByteMax, vertex.colorData[1] / kByteMax, vertex.colorData[2] / kByteMax,
                        vertex.colorData[3] / kByteMax });
                }
                a_shape.vertexBlock = a_block;
            } else if (auto* geometry = hdr.GetBlock<nifly::NiGeometry>(a_block)) {
                auto* data = hdr.GetBlock<nifly::NiGeometryData>(*geometry->DataRef());
                if (!data || !data->HasVertexColors()) {
                    return;
                }
                for (const auto& c : data->vertexColors) {
                    colours.push_back({ c.r, c.g, c.b, c.a });
                }
                a_shape.vertexBlock = geometry->DataRef()->index;
            }
            VertexSummary(colours, a_shape);
        }
    }

    double Saturation(const Rgb& a_colour, double a_minChannel) noexcept
    {
        const double high = std::max({ a_colour.r, a_colour.g, a_colour.b });
        const double low = std::min({ a_colour.r, a_colour.g, a_colour.b });
        return high >= a_minChannel && high > 0.0 ? (high - low) / high : 0.0;
    }

    double Luminance(const Rgb& a_colour) noexcept
    {
        return a_colour.r * kLumaRed + a_colour.g * kLumaGreen + a_colour.b * kLumaBlue;
    }

    Rgb Multiply(const Rgb& a_left, const Rgb& a_right) noexcept
    {
        return { a_left.r * a_right.r, a_left.g * a_right.g, a_left.b * a_right.b };
    }

    double Round3(double a_value) noexcept
    {
        constexpr double kThousand = 1000.0;
        return std::floor(a_value * kThousand + 0.5) / kThousand;
    }

    bool IsPaletteShape(const EffectShape& a_shape) noexcept
    {
        return (a_shape.flags1 & kSlsf1GreyscaleToPaletteColor) != 0 && !a_shape.sourceTexture.empty();
    }

    std::vector<ColourKeys> ColourKeysOf(nifly::NifFile& a_nif, std::uint32_t a_shader, int& a_unreached, int& a_sequenceLinks)
    {
        auto& hdr = a_nif.GetHeader();
        const auto sequences = SequenceInterpolators(a_nif);
        std::vector<ColourKeys> out;
        a_unreached = 0;
        a_sequenceLinks = 0;
        for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
            auto* ctrl = hdr.GetBlock<nifly::BSEffectShaderPropertyColorController>(id);
            if (!ctrl || ctrl->targetRef.index != a_shader) {
                continue;
            }
            std::vector<std::pair<std::uint32_t, bool>> interps{ { ctrl->interpolatorRef.index, false } };
            if (const auto it = sequences.find(id); it != sequences.end()) {
                a_sequenceLinks += static_cast<int>(it->second.size());
                for (const auto interp : it->second) {
                    interps.emplace_back(interp, true);
                }
            }
            bool reached = false;
            for (const auto& [interp, viaSequence] : interps) {
                auto* point3 = hdr.GetBlock<nifly::NiPoint3Interpolator>(interp);
                if (!point3) {
                    continue;
                }
                reached = true;
                out.push_back({ id, interp, point3->dataRef.index, viaSequence });
            }
            a_unreached += reached ? 0 : 1;
        }
        return out;
    }

    std::vector<EffectShape> EffectShapes(nifly::NifFile& a_nif)
    {
        auto& hdr = a_nif.GetHeader();
        const auto parent = Parents(a_nif);
        const auto sequences = SequenceInterpolators(a_nif);
        std::vector<EffectShape> shapes;
        for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
            EffectShape shape;
            std::uint32_t alphaRef = nifly::NIF_NPOS;
            if (auto* mesh = hdr.GetBlock<nifly::NiShape>(id)) {
                shape.shader = mesh->ShaderPropertyRef() ? mesh->ShaderPropertyRef()->index : nifly::NIF_NPOS;
                alphaRef = mesh->AlphaPropertyRef() ? mesh->AlphaPropertyRef()->index : nifly::NIF_NPOS;
                shape.name = mesh->name.get();
            } else if (auto* system = hdr.GetBlock<nifly::NiParticleSystem>(id)) {
                shape.shader = system->shaderPropertyRef.index;
                alphaRef = system->alphaPropertyRef.index;
                shape.name = system->name.get();
                shape.particles = true;
            } else {
                continue;
            }
            auto* effect = hdr.GetBlock<nifly::BSEffectShaderProperty>(shape.shader);
            if (!effect) {
                continue;
            }
            shape.block = id;
            shape.blend = BlendOf(hdr.GetBlock<nifly::NiAlphaProperty>(alphaRef));
            shape.hidden = OnChain(a_nif, parent, id, true, [](nifly::NiHeader& a_hdr, std::uint32_t a_id) {
                const auto* object = a_hdr.GetBlock<nifly::NiAVObject>(a_id);
                return object && (object->flags & kHiddenFlag) != 0;
            });
            shape.billboard = OnChain(a_nif, parent, id, false, [](nifly::NiHeader& a_hdr, std::uint32_t a_id) {
                return a_hdr.GetBlock<nifly::NiBillboardNode>(a_id) != nullptr;
            });
            shape.flags1 = effect->shaderFlags1;
            shape.flags2 = effect->shaderFlags2;
            shape.sourceTexture = effect->sourceTexture.get();
            shape.greyscaleTexture = effect->greyscaleTexture.get();
            shape.baseColour = { effect->baseColor.r, effect->baseColor.g, effect->baseColor.b };
            shape.colourScale = effect->baseColorScale;
            shape.multipleMax = effect->baseColorScale;
            shape.colours.push_back(shape.baseColour);
            shape.colourKeys = ColourKeysOf(a_nif, shape.shader, shape.unreachedControllers, shape.sequenceLinks);
            for (const auto& keys : shape.colourKeys) {
                AddPoint3Values(a_nif, keys.interpolator, shape.colours);
            }
            for (std::uint32_t c = 0; c < hdr.GetNumBlocks(); ++c) {
                auto* ctrl = hdr.GetBlock<nifly::BSEffectShaderPropertyFloatController>(c);
                if (!ctrl || ctrl->targetRef.index != shape.shader || ctrl->typeOfControlledVariable != kFloatVarEmissiveMultiple) {
                    continue;
                }
                AddFloatValues(a_nif, ctrl->interpolatorRef.index, shape.multipleMax);
                if (const auto it = sequences.find(c); it != sequences.end()) {
                    for (const auto interp : it->second) {
                        AddFloatValues(a_nif, interp, shape.multipleMax);
                    }
                }
            }
            if (!shape.particles) {
                ReadVertexColours(a_nif, id, shape);
            }
            shapes.push_back(std::move(shape));
        }
        return shapes;
    }
}
