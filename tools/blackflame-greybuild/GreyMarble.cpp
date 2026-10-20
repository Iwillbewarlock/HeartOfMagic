#include "GreyMarble.h"
#include "GreyKeyed.h"
#include "GreyShapes.h"

#include <NifFile.hpp>
#include <Particles.hpp>
#include <Shaders.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace GreyBuild
{
    namespace
    {
        constexpr float kDark = 0.006f;
        constexpr float kAngle = 0.384f; // 22 degrees; independent random rotation remains enabled
        constexpr std::uint16_t kNormalUnsorted = 0x30ED; // 12525: normal blend, test disabled, No_Sorter (bit 13).
        using Remap = std::map<std::uint32_t, std::uint32_t>;

        void ChildRefs(nifly::NiObject& block, std::set<nifly::NiRef*>& refs)
        {
            block.GetChildRefs(refs);
            // nifly's NiBlendInterpolator does not enumerate its persisted child items.
            if (auto* blend = dynamic_cast<nifly::NiBlendInterpolator*>(&block))
                for (auto& item : blend->interpItems) refs.insert(&item.interpolatorRef);
        }

        // Only owned children are copied. Scene nodes, other shapes and back pointers stay external.
        // All indices are in the same header; nifly rebuilds its string table when saving.
        std::uint32_t CloneGraph(nifly::NiHeader& dst, nifly::NiHeader& src,
            std::uint32_t id, std::uint32_t root, Remap& map)
        {
            if (id == nifly::NIF_NPOS) return id;
            if (const auto it = map.find(id); it != map.end()) return it->second;
            auto* block = src.GetBlock<nifly::NiObject>(id);
            if (!block || src.GetBlock<nifly::NiUnknown>(id))
                throw std::runtime_error("marble: missing or unknown owned block");
            if (id != root && (dynamic_cast<nifly::NiAVObject*>(block) ||
                dynamic_cast<nifly::NiControllerManager*>(block))) return id;
            const auto added = dst.AddBlock(block->Clone());
            map.emplace(id, added);
            std::set<nifly::NiRef*> refs;
            ChildRefs(*dst.GetBlock<nifly::NiObject>(added), refs);
            // GetChildRefs orders pointers, which depends on heap layout. Allocate by source block
            // index instead so two fresh builds of the same key produce identical graph bytes.
            std::vector<nifly::NiRef*> ordered(refs.begin(), refs.end());
            std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) { return a->index < b->index; });
            for (auto* ref : ordered) ref->index = CloneGraph(dst, src, ref->index, root, map);
            return added;
        }

        bool DropShaderController(nifly::NiHeader& hdr, std::uint32_t id)
        {
            if (hdr.GetBlock<nifly::BSEffectShaderPropertyColorController>(id)) return true;
            auto* f = hdr.GetBlock<nifly::BSEffectShaderPropertyFloatController>(id);
            return f && f->typeOfControlledVariable == kFloatVarEmissiveMultiple;
        }

        void RemapPointers(nifly::NiHeader& hdr, const Remap& map)
        {
            for (const auto& [oldId, newId] : map) {
                auto* block = hdr.GetBlock<nifly::NiObject>(newId);
                std::set<nifly::NiPtr*> ptrs;
                block->GetPtrs(ptrs);
                for (auto* ptr : ptrs) {
                    if (const auto it = map.find(ptr->index); it != map.end()) ptr->index = it->second;
                }
            }
        }

        void ScaleFloat(nifly::NiHeader& hdr, std::uint32_t id, float scale, std::set<std::uint32_t>& done)
        {
            if (id == nifly::NIF_NPOS || !done.insert(id).second) return;
            const auto value = [scale](float& f) { if (std::abs(f) < kHugeFloatValue) f *= scale; };
            if (auto* interp = hdr.GetBlock<nifly::NiFloatInterpolator>(id)) {
                value(interp->floatValue);
                ScaleFloat(hdr, interp->dataRef.index, scale, done);
            } else if (auto* blend = hdr.GetBlock<nifly::NiBlendFloatInterpolator>(id)) {
                value(blend->value);
                for (const auto& item : blend->interpItems)
                    ScaleFloat(hdr, item.interpolatorRef.index, scale, done);
            } else if (auto* data = hdr.GetBlock<nifly::NiFloatData>(id)) {
                for (unsigned i = 0; i < data->data.GetNumKeys(); ++i) {
                    auto key = data->data.GetKey(i);
                    value(key.value); value(key.forward); value(key.backward);
                    data->data.SetKey(i, key);
                }
            } else {
                throw std::runtime_error("marble: unsupported animated radius/alpha interpolator");
            }
        }

        // A sequence owns interpolators not reachable from the particle's controller chain.
        // Duplicate its links, resolving reference-based and name-only bindings, for the new name.
        std::uint32_t NamedController(nifly::NiHeader& src, std::uint32_t root, const nifly::ControllerLink& link)
        {
            auto* system = src.GetBlock<nifly::NiParticleSystem>(root);
            std::uint32_t target = root;
            if (!link.propType.get().empty()) {
                if (link.propType.get() == "BSEffectShaderProperty") target = system->shaderPropertyRef.index;
                else if (link.propType.get() == "NiAlphaProperty") target = system->alphaPropertyRef.index;
                else throw std::runtime_error("marble: unsupported named controller property " + link.propType.get());
            }
            std::uint32_t found = nifly::NIF_NPOS;
            // Include sequence-owned controllers outside the object's chain, but require an exact target.
            for (std::uint32_t id = 0; id < src.GetNumBlocks(); ++id) {
                auto* ctrl = src.GetBlock<nifly::NiTimeController>(id);
                if (!ctrl || ctrl->targetRef.index != target || link.ctrlType.get() != ctrl->GetBlockName()) continue;
                if (auto* modifier = dynamic_cast<nifly::NiPSysModifierCtlr*>(ctrl)) {
                    if (!link.ctrlID.get().empty() && link.ctrlID.get() != modifier->modifierName.get()) continue;
                }
                if (auto* shader = dynamic_cast<nifly::BSEffectShaderPropertyFloatController*>(ctrl)) {
                    if (!link.ctrlID.get().empty() && link.ctrlID.get() != std::to_string(shader->typeOfControlledVariable)) continue;
                }
                if (found != nifly::NIF_NPOS)
                    throw std::runtime_error("marble: ambiguous named controller " + link.ctrlType.get());
                found = id;
            }
            if (found == nifly::NIF_NPOS)
                throw std::runtime_error("marble: unresolved named controller " + link.ctrlType.get());
            return found;
        }

        void CloneSequenceLinks(nifly::NiHeader& dst, nifly::NiHeader& src, std::uint32_t root,
            Remap& map, const std::string& oldName, const std::string& newName)
        {
            for (std::uint32_t id = 0; id < src.GetNumBlocks(); ++id) {
                auto* sequence = src.GetBlock<nifly::NiControllerSequence>(id);
                if (!sequence) continue;
                auto* target = dst.GetBlock<nifly::NiControllerSequence>(id);
                for (auto link : sequence->controlledBlocks) {
                    auto controller = link.controllerRef.index;
                    const bool direct = map.count(controller) != 0;
                    const bool named = !oldName.empty() && link.nodeName.get() == oldName;
                    if (!direct && !named) continue;
                    if (link.ctrlType.get() == "BSEffectShaderPropertyColorController") continue;
                    if (controller == nifly::NIF_NPOS) controller = NamedController(src, root, link);
                    // A matching name is not ownership: explicit links may target a different same-named node.
                    const auto* ctrl = src.GetBlock<nifly::NiTimeController>(controller);
                    const auto* system = src.GetBlock<nifly::NiParticleSystem>(root);
                    if (!ctrl || (ctrl->targetRef.index != root &&
                        ctrl->targetRef.index != system->shaderPropertyRef.index &&
                        ctrl->targetRef.index != system->alphaPropertyRef.index)) continue;
                    if (DropShaderController(src, controller)) continue;
                    link.controllerRef.index = CloneGraph(dst, src, controller, root, map);
                    link.interpolatorRef.index = CloneGraph(dst, src, link.interpolatorRef.index, root, map);
                    link.nodeName.get() = newName;
                    target->controlledBlocks.push_back(link);
                }
            }
        }

        void Before(nifly::NiRefArray& refs, std::uint32_t source, std::uint32_t added)
        {
            std::vector<std::uint32_t> ids, out;
            refs.GetIndices(ids);
            for (const auto id : ids) {
                if (id == source) out.push_back(added);
                out.push_back(id);
            }
            refs.SetIndices(out);
        }

        void Attach(nifly::NiHeader& dst, nifly::NiHeader& src, std::uint32_t root,
            std::uint32_t added, const std::string& name)
        {
            bool attached = false;
            for (std::uint32_t id = 0; id < src.GetNumBlocks(); ++id) {
                if (auto* node = dst.GetBlock<nifly::NiNode>(id)) {
                    for (auto& ref : node->childRefs) attached |= ref.index == root;
                    Before(node->childRefs, root, added);
                }
                if (auto* master = dst.GetBlock<nifly::BSMasterParticleSystem>(id))
                    Before(master->particleSysRefs, root, added);
                if (auto* palette = dst.GetBlock<nifly::NiDefaultAVObjectPalette>(id)) {
                    bool found = false;
                    for (auto& obj : palette->objects) found |= obj.objectRef.index == root;
                    if (found) {
                        nifly::AVObject obj;
                        obj.name.get() = name;
                        obj.objectRef.index = added;
                        palette->objects.push_back(obj);
                    }
                }
            }
            if (!attached) throw std::runtime_error("marble: source system has no scene parent");
        }

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

        void Configure(nifly::NiHeader& hdr, const Remap& map, std::uint32_t added, float opacity, float scale)
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
            alpha->flags = kNormalUnsorted; // SrcAlpha / InvSrcAlpha; alpha test off; No_Sorter on
            alpha->threshold = 0;

            // A new emitter instance draws its own random samples; never share mutable modifier/controller state.
            bool rotated = false;
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
                if (auto* rotation = hdr.GetBlock<nifly::NiPSysRotationModifier>(newId)) {
                    rotation->initialAngle += kAngle;
                    rotation->initialAngleVariation = std::max(rotation->initialAngleVariation, 0.57596f);
                    rotated = true;
                }
                if (auto* data = hdr.GetBlock<nifly::NiPSysData>(newId)) {
                    data->hasRotationAngles = true;
                    // Shift atlas indexing one cell: sample 0/3/6 becomes 1/4/7 without a new texture.
                    if (data->subtexOffsets.size() > 1)
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
            if (!rotated) {
                auto rotation = std::make_unique<nifly::NiPSysRotationModifier>();
                rotation->name.get() = "HoM Marble Rotation";
                rotation->targetRef.index = added;
                rotation->order = 3000;
                rotation->isActive = true;
                rotation->initialAngle = kAngle;
                rotation->initialAngleVariation = 0.57596f;
                system->modifierRefs.AddBlockRef(hdr.AddBlock(std::move(rotation)));
            }
        }
    }

    void ValidateMarbleRefs(nifly::NifFile& nif)
    {
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
        MarbleTotals totals;
        auto& src = source.GetHeader();
        auto& dst = copy.GetHeader();
        if (dst.GetNumBlocks() != src.GetNumBlocks())
            throw std::runtime_error("marble: source and destination block counts differ");
        if (!std::isfinite(opacity) || opacity < 0 || opacity > 1 || !std::isfinite(scale) || scale < 0.1 || scale > 4)
            throw std::runtime_error("marble: settings outside contract range");
        if (opacity == 0) return totals;
        for (std::uint32_t id = 0; id < src.GetNumBlocks(); ++id) {
            auto* system = src.GetBlock<nifly::NiParticleSystem>(id);
            if (!system) continue;
            auto* shader = src.GetBlock<nifly::BSEffectShaderProperty>(system->shaderPropertyRef);
            auto* alpha = src.GetBlock<nifly::NiAlphaProperty>(system->alphaPropertyRef);
            auto* data = src.GetBlock<nifly::NiPSysData>(system->dataRef);
            if (!data) data = src.GetBlock<nifly::NiPSysData>(system->psysDataRef);
            if (!shader || !data || data->subtexOffsets.size() < 2 || shader->sourceTexture.get().empty() ||
                !(shader->shaderFlags1 & kSlsf1GreyscaleToPaletteColor) || BlendOfAlpha(alpha) != ShapeBlend::kAdditive) continue;
            const auto palette = textures.Look(shader->greyscaleTexture.get());
            if (!palette.image || EfshFireShare(*palette.image) < kEfshFireShare) continue;
            Remap map;
            const auto added = CloneGraph(dst, src, id, id, map);
            const auto name = system->name.get() + " HoM Marble " + std::to_string(id);
            dst.GetBlock<nifly::NiParticleSystem>(added)->name.get() = name;
            CloneSequenceLinks(dst, src, id, map, system->name.get(), name);
            RemapPointers(dst, map);
            Configure(dst, map, added, static_cast<float>(opacity), static_cast<float>(scale));
            Attach(dst, src, id, added, name);
            // Also suppress sorting on the paired additive pass; child order expresses dark then light.
            // Skyrim can regroup render passes regardless: engine draw order still needs in-game validation.
            auto* bright = dst.GetBlock<nifly::NiParticleSystem>(id);
            auto brightAlpha = dst.GetBlock<nifly::NiAlphaProperty>(bright->alphaPropertyRef)->Clone();
            brightAlpha->flags |= 1u << 13; // Preserve the source test function (bits 10-12).
            bright->alphaPropertyRef.index = dst.AddBlock(std::move(brightAlpha));
            ++totals.systems;
            totals.sourceParticles += data->GetNumVertices();
            totals.addedParticles += data->GetNumVertices();
        }
        ValidateMarbleRefs(copy);
        return totals;
    }
}
