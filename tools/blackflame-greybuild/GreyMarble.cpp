#include "GreyMarble.h"
#include "GreyKeyed.h"
#include "GreyShapes.h"
#include "GreyParticleGraph.h"
#include "GreyHandFx.h"

#include <NifFile.hpp>
#include <Particles.hpp>
#include <Shaders.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace GreyBuild
{
    namespace
    {
        constexpr float kDark = 0.006f;
        // No_Sorter (0x2000) is not Bethesda's object-order control. The ordered parent owns that.
        constexpr std::uint16_t kNormalBlend = 0x10ED; // 4333: SrcAlpha / InvSrcAlpha, alpha test disabled.
        using namespace HandGraph;

        void White(nifly::Color4& color) { color.r = color.g = color.b = 1.0f; }

        void AlphaPeak(nifly::NiHeader& hdr, std::uint32_t id, float& peak, std::set<std::uint32_t>& seen)
        {
            if (id == nifly::NIF_NPOS || !seen.insert(id).second) return;
            const auto take = [&](float value) {
                if (std::isfinite(value) && value >= 0 && value < kHugeFloatValue) peak = std::max(peak, value);
            };
            if (auto* interp = hdr.GetBlock<nifly::NiFloatInterpolator>(id)) {
                take(interp->floatValue);
                AlphaPeak(hdr, interp->dataRef.index, peak, seen);
            } else if (auto* blend = hdr.GetBlock<nifly::NiBlendFloatInterpolator>(id)) {
                take(blend->value);
                for (const auto& item : blend->interpItems) AlphaPeak(hdr, item.interpolatorRef.index, peak, seen);
            } else if (auto* data = hdr.GetBlock<nifly::NiFloatData>(id)) {
                for (unsigned i = 0; i < data->data.GetNumKeys(); ++i) take(data->data.GetKey(i).value);
            } else throw std::runtime_error("marble: unsupported alpha peak interpolator");
        }

        float ShaderAlphaScale(nifly::NiHeader& hdr, const Remap& map, const nifly::NiParticleSystem& system, float opacity)
        {
            float peak = hdr.GetBlock<nifly::BSEffectShaderProperty>(system.shaderPropertyRef)->baseColor.a;
            if (!std::isfinite(peak) || peak < 0) peak = 0;
            std::set<std::uint32_t> seen;
            for (const auto& [oldId, newId] : map) {
                auto* ctrl = hdr.GetBlock<nifly::BSEffectShaderPropertyFloatController>(newId);
                if (ctrl && ctrl->typeOfControlledVariable == kFloatVarAlpha)
                    AlphaPeak(hdr, ctrl->interpolatorRef.index, peak, seen);
            }
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                auto* sequence = hdr.GetBlock<nifly::NiControllerSequence>(id);
                if (!sequence) continue;
                for (const auto& link : sequence->controlledBlocks) {
                    if (link.nodeName.get() != system.name.get()) continue;
                    auto* ctrl = hdr.GetBlock<nifly::BSEffectShaderPropertyFloatController>(link.controllerRef.index);
                    if (ctrl && ctrl->typeOfControlledVariable == kFloatVarAlpha)
                        AlphaPeak(hdr, link.interpolatorRef.index, peak, seen);
                }
            }
            // Normalise authored values across the whole envelope, never each key independently.
            // Cubic overshoot between keys remains the source curve's engine interpolation.
            return peak > 0 ? opacity / peak : opacity;
        }

        std::string Configure(nifly::NiHeader& hdr, const Remap& map, std::uint32_t added, float opacity, float scale, bool ember)
        {
            auto* system = hdr.GetBlock<nifly::NiParticleSystem>(added);
            auto* shader = hdr.GetBlock<nifly::BSEffectShaderProperty>(system->shaderPropertyRef);
            const float alphaScale = ShaderAlphaScale(hdr, map, *system, opacity);
            // Use atlas alpha directly. Flat near-black material avoids the bright ramp's palette RGB.
            // Atlas RGB still modulates this <=.006 material, as with ordinary effect-shader alpha blending.
            shader->shaderFlags1 &= ~(kSlsf1GreyscaleToPaletteColor | kSlsf1GreyscaleToPaletteAlpha);
            shader->shaderFlags1 |= 1u << 31; // ZBuffer_Test
            shader->shaderFlags2 &= ~1u; // ZBuffer_Write off
            shader->shaderFlags2 |= kSlsf2VertexColors;
            shader->greyscaleTexture.get().clear();
            shader->baseColor.r = shader->baseColor.g = shader->baseColor.b = kDark;
            shader->baseColor.a *= alphaScale;
            shader->baseColorScale = 1.0f;
            auto* alpha = hdr.GetBlock<nifly::NiAlphaProperty>(system->alphaPropertyRef);
            alpha->flags = kNormalBlend;
            alpha->threshold = 0;

            // A new emitter instance draws its own random samples; never share mutable modifier/controller state.
            std::set<std::uint32_t> scaledRadius, scaledAlpha;
            Remap radiusCopies, alphaCopies;
            const auto scaleRadius = [&](nifly::NiRef& ref) {
                // Split the entire float path, including data shared through distinct interpolators.
                // Emission rate/lifetime and sequence links retain the unscaled cloned graph.
                ref.index = CloneGraph(hdr, hdr, ref.index, nifly::NIF_NPOS, radiusCopies);
                ScaleFloat(hdr, ref.index, scale, scaledRadius);
            };
            const auto scaleAlpha = [&](nifly::NiRef& ref) {
                // Opacity may share interpolators/data with rate, lifetime or radius as well.
                ref.index = CloneGraph(hdr, hdr, ref.index, nifly::NIF_NPOS, alphaCopies);
                ScaleFloat(hdr, ref.index, alphaScale, scaledAlpha);
            };
            for (const auto& [oldId, newId] : map) {
                if (auto* emitter = hdr.GetBlock<nifly::NiPSysEmitter>(newId)) {
                    emitter->radius *= scale; emitter->radiusVariation *= scale;
                    White(emitter->color);
                    if (emitter->color.a > 0) emitter->color.a = 1.0f;
                }
                if (auto* colors = hdr.GetBlock<nifly::BSPSysSimpleColorModifier>(newId)) {
                    White(colors->color1); White(colors->color2); White(colors->color3);
                    const float peak = std::max({ colors->color1.a, colors->color2.a, colors->color3.a });
                    if (peak > 0) {
                        colors->color1.a /= peak; colors->color2.a /= peak; colors->color3.a /= peak;
                    }
                }
                if (auto* colors = hdr.GetBlock<nifly::NiColorData>(newId)) {
                    float peak = 0;
                    for (unsigned i = 0; i < colors->data.GetNumKeys(); ++i)
                        peak = std::max(peak, colors->data.GetKey(i).value.a);
                    for (unsigned i = 0; i < colors->data.GetNumKeys(); ++i) {
                        auto key = colors->data.GetKey(i);
                        White(key.value);
                        if (peak > 0) {
                            key.value.a /= peak; key.forward.a /= peak; key.backward.a /= peak;
                        }
                        key.forward.r = key.forward.g = key.forward.b = 0;
                        key.backward.r = key.backward.g = key.backward.b = 0;
                        colors->data.SetKey(i, key);
                    }
                }
                if (auto* data = hdr.GetBlock<nifly::NiPSysData>(newId)) {
                    // Shift atlas indexing one cell: sample 0/3/6 becomes 1/4/7 without a new texture.
                    if (!ember && data->subtexOffsets.size() > 1)
                        std::rotate(data->subtexOffsets.begin(), data->subtexOffsets.begin() + 1, data->subtexOffsets.end());
                    for (auto& color : data->vertexColors) White(color);
                }
                if (auto* ctrl = hdr.GetBlock<nifly::NiPSysEmitterInitialRadiusCtlr>(newId))
                    scaleRadius(ctrl->interpolatorRef);
                if (auto* ctrl = hdr.GetBlock<nifly::BSEffectShaderPropertyFloatController>(newId)) {
                    if (ctrl->typeOfControlledVariable == kFloatVarAlpha)
                        scaleAlpha(ctrl->interpolatorRef);
                }
                if (auto* object = hdr.GetBlock<nifly::NiObjectNET>(newId)) {
                    // Keep detached cloned controllers: nifly deletion renumbers refs without enumerating
                    // NiBlendInterpolator::interpItems. Safe pruning needs that upstream traversal fixed.
                    auto* ref = &object->controllerRef;
                    while (auto* ctrl = hdr.GetBlock<nifly::NiTimeController>(*ref)) {
                        if (DropShaderController(hdr, ref->index)) ref->index = ctrl->nextControllerRef.index;
                        else ref = &ctrl->nextControllerRef;
                    }
                }
            }
            // Sequence-supplied radius/alpha values also override the static values.
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                auto* sequence = hdr.GetBlock<nifly::NiControllerSequence>(id);
                if (!sequence) continue;
                for (auto& link : sequence->controlledBlocks) {
                    if (link.nodeName.get() != system->name.get()) continue;
                    if (hdr.GetBlock<nifly::NiPSysEmitterInitialRadiusCtlr>(link.controllerRef.index) ||
                        link.ctrlType.get() == "NiPSysEmitterInitialRadiusCtlr")
                        scaleRadius(link.interpolatorRef);
                    auto* ctrl = hdr.GetBlock<nifly::BSEffectShaderPropertyFloatController>(link.controllerRef.index);
                    if (ctrl && ctrl->typeOfControlledVariable == kFloatVarAlpha)
                        scaleAlpha(link.interpolatorRef);
                }
            }
            if (ember) {
                ReduceDarkEmberRate(hdr, map, added);
                return {};
            }
            const auto reason = SetHandMarbleMotion(hdr, added);
            if (!reason.empty()) OrderHandParticleUpdate(hdr, added);
            return reason;
        }
    }

    void ValidateMarbleRefs(nifly::NifFile& nif)
    {
        using namespace HandGraph;
        auto& hdr = nif.GetHeader();
        for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
            auto* block = hdr.GetBlock<nifly::NiObject>(id);
            std::set<nifly::NiRef*> refs;
            std::set<nifly::NiPtr*> ptrs;
            ChildRefs(*block, refs); block->GetPtrs(ptrs);
            for (auto* ref : refs)
                if (ref->index != nifly::NIF_NPOS && ref->index >= hdr.GetNumBlocks())
                    throw std::runtime_error("marble: invalid child reference");
            for (auto* ptr : ptrs)
                if (ptr->index != nifly::NIF_NPOS && ptr->index >= hdr.GetNumBlocks())
                    throw std::runtime_error("marble: invalid back pointer");
        }
    }

    MarbleTotals AddHandMarble(nifly::NifFile& copy, nifly::NifFile& source,
        TextureSource& textures, double opacity, double scale)
    {
        using namespace HandGraph;
        MarbleTotals totals;
        auto& src = source.GetHeader();
        auto& dst = copy.GetHeader();
        if (dst.GetNumBlocks() != src.GetNumBlocks())
            throw std::runtime_error("marble: source and destination block counts differ");
        if (!std::isfinite(opacity) || opacity < 0 || opacity > 1 || !std::isfinite(scale) || scale < 0.1 || scale > 4)
            throw std::runtime_error("marble: settings outside contract range");
        if (opacity == 0) return totals; // Disable the entire hand recipe, including bright ember changes.
        std::vector<std::string> motionSkips;
        for (std::uint32_t id = 0; id < src.GetNumBlocks(); ++id) {
            auto* system = src.GetBlock<nifly::NiParticleSystem>(id);
            if (!system) continue;
            auto* shader = src.GetBlock<nifly::BSEffectShaderProperty>(system->shaderPropertyRef);
            auto* alpha = src.GetBlock<nifly::NiAlphaProperty>(system->alphaPropertyRef);
            auto* data = src.GetBlock<nifly::NiPSysData>(system->dataRef);
            if (!data) data = src.GetBlock<nifly::NiPSysData>(system->psysDataRef);
            if (!shader || !data || shader->sourceTexture.get().empty() ||
                !(shader->shaderFlags1 & kSlsf1GreyscaleToPaletteColor) || BlendOfAlpha(alpha) != ShapeBlend::kAdditive) continue;
            const auto palette = textures.Look(shader->greyscaleTexture.get());
            if (!palette.image || EfshFireShare(*palette.image) < kEfshFireShare) continue;
            const bool ember = IsHandEmber(src, *system, *data);
            if (!ember && data->subtexOffsets.size() < 2) continue;
            Remap map;
            const auto added = CloneGraph(dst, src, id, id, map);
            const auto name = system->name.get() + (ember ? " HoM Dark Ember " : " HoM Marble ") + std::to_string(id);
            dst.GetBlock<nifly::NiParticleSystem>(added)->name.get() = name;
            CloneSequenceLinks(dst, src, id, map, system->name.get(), name);
            RemapPointers(dst, map);
            const auto reason = Configure(dst, map, added,
                ember ? std::min(kHandDarkEmberOpacity, static_cast<float>(opacity)) : static_cast<float>(opacity),
                ember ? 1.0f : static_cast<float>(scale), ember);
            if (!reason.empty()) motionSkips.push_back(name + ": " + reason);
            Attach(dst, src, id, added, name);
            if (ember) {
                BrightenHandEmber(copy, id);
                ++totals.emberSystems;
            } else ++totals.systems;
            totals.sourceParticles += data->GetNumVertices();
            totals.addedParticles += data->GetNumVertices();
        }
        ValidateMarbleRefs(copy);
        if (!motionSkips.empty()) {
            std::cout << "hand fx motion: " << motionSkips.size()
                << " marble system(s) kept; new rotation/SubTex skipped:\n";
            for (const auto& reason : motionSkips) std::cout << "  " << reason << '\n';
        }
        return totals;
    }
}
