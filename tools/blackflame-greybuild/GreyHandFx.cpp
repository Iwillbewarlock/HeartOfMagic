#include "GreyHandFx.h"
#include "GreyShapes.h"

#include <NifFile.hpp>
#include <Particles.hpp>
#include <Shaders.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace GreyBuild
{
    namespace
    {
        using namespace HandGraph;

        nifly::NiPSysData* Data(nifly::NiHeader& hdr, nifly::NiParticleSystem& system)
        {
            auto* data = hdr.GetBlock<nifly::NiPSysData>(system.dataRef);
            return data ? data : hdr.GetBlock<nifly::NiPSysData>(system.psysDataRef);
        }

        float EffectiveLife(nifly::NiHeader& hdr, nifly::NiParticleSystem& system, std::string& reason)
        {
            double sum = 0;
            unsigned count = 0;
            for (const auto& ref : system.modifierRefs) {
                auto* emitter = hdr.GetBlock<nifly::NiPSysEmitter>(ref);
                if (!emitter) continue;
                // SSE EmitParticles uses life + (U - .5) * Life Span Variation (full width).
                const double life = emitter->lifeSpan, variation = std::abs(emitter->lifeSpanVariation) * 0.5;
                if (life == 0 && variation == 0) { ++count; continue; } // No live particles at this static setting.
                if (!std::isfinite(life) || !std::isfinite(variation) || life <= variation) {
                    reason = "emitter lifespan must be finite and exceed half its variation";
                    return 0;
                }
                // Count and life random samples are independent. The harmonic mean compensates
                // E[1/life], so average cell speed is 8.4 at the authored static lifespan.
                // Animated lifetimes still change the speed; exact per-second cycling would
                // require an engine-side modifier, not a different NIF float.
                sum += variation > 0 ? (2 * variation) / std::log((life + variation) / (life - variation)) : life;
                ++count;
            }
            if (!count) {
                reason = "no emitter lifespan";
                return 0;
            }
            return static_cast<float>(sum / count);
        }

        void BoostFloat(nifly::NiHeader& hdr, std::uint32_t id, std::set<std::uint32_t>& done)
        {
            if (id == nifly::NIF_NPOS || !done.insert(id).second) return;
            const auto value = [](float f) {
                return std::abs(f) < kHugeFloatValue ?
                    std::clamp(f * kHandEmberBrightness, 0.0f, kHandEmberMaxBrightness) : f;
            };
            if (auto* interp = hdr.GetBlock<nifly::NiFloatInterpolator>(id)) {
                interp->floatValue = value(interp->floatValue);
                BoostFloat(hdr, interp->dataRef.index, done);
            } else if (auto* blend = hdr.GetBlock<nifly::NiBlendFloatInterpolator>(id)) {
                blend->value = value(blend->value);
                for (const auto& item : blend->interpItems) BoostFloat(hdr, item.interpolatorRef.index, done);
            } else if (auto* data = hdr.GetBlock<nifly::NiFloatData>(id)) {
                for (unsigned i = 0; i < data->data.GetNumKeys(); ++i) {
                    auto key = data->data.GetKey(i);
                    const float old = key.value;
                    key.value = value(old);
                    // Flatten tangents at the cap; never propagate overbright cubic endpoints.
                    const float factor = old * kHandEmberBrightness >= kHandEmberMaxBrightness ?
                        0.0f : kHandEmberBrightness;
                    key.forward *= factor; key.backward *= factor;
                    data->data.SetKey(i, key);
                }
            } else throw std::runtime_error("hand ember: unsupported emissive interpolator");
        }
    }

    bool IsHandEmber(nifly::NiHeader& hdr, nifly::NiParticleSystem& system, const nifly::NiPSysData& data)
    {
        if (dynamic_cast<nifly::BSStripParticleSystem*>(&system) || data.subtexOffsets.size() > 1) return false;
        bool found = false;
        for (const auto& ref : system.modifierRefs) {
            auto* emitter = hdr.GetBlock<nifly::NiPSysEmitter>(ref);
            if (!emitter) continue;
            const float radius = emitter->radius + std::abs(emitter->radiusVariation);
            if (!std::isfinite(radius) || radius <= 0 || radius > kHandEmberMaxRadius) return false;
            found = true;
        }
        return found;
    }

    void OrderHandParticleUpdate(nifly::NiHeader& hdr, std::uint32_t id)
    {
        auto* system = hdr.GetBlock<nifly::NiParticleSystem>(id);
        std::vector<std::uint32_t> mods;
        system->modifierRefs.GetIndices(mods);
        std::stable_sort(mods.begin(), mods.end(), [&](auto a, auto b) {
            auto* x = hdr.GetBlock<nifly::NiPSysModifier>(a);
            auto* y = hdr.GetBlock<nifly::NiPSysModifier>(b);
            if (!x || !y) throw std::runtime_error("hand motion: invalid modifier");
            return x->order < y->order;
        });
        system->modifierRefs.SetIndices(mods);
        auto* ref = &system->controllerRef;
        std::set<std::uint32_t> seen;
        auto updateId = nifly::NIF_NPOS;
        while (auto* ctrl = hdr.GetBlock<nifly::NiTimeController>(*ref)) {
            if (!seen.insert(ref->index).second) throw std::runtime_error("hand motion: cyclic controller chain");
            if (dynamic_cast<nifly::NiPSysUpdateCtlr*>(ctrl)) {
                updateId = ref->index;
                ref->index = ctrl->nextControllerRef.index;
                ctrl->nextControllerRef.Clear();
            } else ref = &ctrl->nextControllerRef;
        }
        if (updateId == nifly::NIF_NPOS) {
            auto update = std::make_unique<nifly::NiPSysUpdateCtlr>();
            update->targetRef.index = id;
            update->flags = 76; // Vanilla hand particle update controller conventions.
            update->frequency = 1.0f;
            update->startTime = 0.0f;
            update->stopTime = 0.0f;
            updateId = hdr.AddBlock(std::move(update));
        }
        ref->index = updateId;
    }

    std::string SetHandMarbleMotion(nifly::NiHeader& hdr, std::uint32_t id)
    {
        auto* system = hdr.GetBlock<nifly::NiParticleSystem>(id);
        auto* data = Data(hdr, *system);
        if (!data || data->subtexOffsets.size() < 2) throw std::runtime_error("hand motion: missing atlas");
        // Reject only this recipe before removing source motion or its controller bindings.
        // Invalid static lifetime must not discard other hand systems in the NIF.
        std::string reason;
        const float frameCount = kHandMarbleCellsPerSecond * EffectiveLife(hdr, *system, reason);
        if (!reason.empty()) return reason;
        if (!std::isfinite(frameCount) || frameCount + kHandMarbleFrameCountFudge >= 65536)
            return "lifespan-derived frame count exceeds engine uint16";
        std::set<std::string> replaced;
        std::vector<std::uint32_t> kept;
        for (const auto& ref : system->modifierRefs) {
            auto* mod = hdr.GetBlock<nifly::NiPSysModifier>(ref);
            if (dynamic_cast<nifly::NiPSysRotationModifier*>(mod) ||
                dynamic_cast<nifly::BSPSysSubTexModifier*>(mod)) replaced.insert(mod->name.get());
            else kept.push_back(ref.index);
        }
        system->modifierRefs.SetIndices(kept);
        // Old animated speed/angle/active bindings must not override this independent recipe.
        const auto removed = [&](std::uint32_t ctrlId) {
            auto* ctrl = hdr.GetBlock<nifly::NiPSysModifierCtlr>(ctrlId);
            return ctrl && ctrl->targetRef.index == id && replaced.count(ctrl->modifierName.get());
        };
        auto* link = &system->controllerRef;
        std::set<std::uint32_t> seen;
        while (auto* ctrl = hdr.GetBlock<nifly::NiTimeController>(*link)) {
            if (!seen.insert(link->index).second) throw std::runtime_error("hand motion: cyclic controller chain");
            if (removed(link->index)) link->index = ctrl->nextControllerRef.index;
            else link = &ctrl->nextControllerRef;
        }
        for (std::uint32_t b = 0; b < hdr.GetNumBlocks(); ++b) {
            if (auto* seq = hdr.GetBlock<nifly::NiControllerSequence>(b)) {
                auto& links = seq->controlledBlocks;
                const auto end = std::remove_if(links.begin(), links.end(), [&](const auto& l) {
                    return l.nodeName.get() == system->name.get() && removed(l.controllerRef.index);
                });
                links.resize(static_cast<std::uint32_t>(std::distance(links.begin(), end)));
            }
        }
        auto rotation = std::make_unique<nifly::NiPSysRotationModifier>();
        rotation->name.get() = "HoM Marble Rotation";
        rotation->targetRef.index = id;
        rotation->order = kHandParticleMotionOrder;
        rotation->isActive = true;
        rotation->initialSpeed = kHandMarbleRadiansPerSecond;
        rotation->initialSpeedVariation = kHandMarbleSpeedVariation;
        rotation->randomSpeedSign = true;
        rotation->initialAngle = kHandMarbleInitialAngle;
        rotation->initialAngleVariation = kHandMarbleAngleVariation;
        system->modifierRefs.AddBlockRef(hdr.AddBlock(std::move(rotation)));
        auto subtex = std::make_unique<nifly::BSPSysSubTexModifier>();
        subtex->name.get() = "HoM Marble Atlas";
        subtex->targetRef.index = id;
        subtex->order = kHandParticleMotionOrder;
        subtex->isActive = true;
        // Skyrim SSE nif.xml fields, using vanilla firesprayfx01's inclusive last frame,
        // random birth cell over all cells (engine truncates), loop at zero. Frame Count is frames per life, NOT FPS.
        subtex->startFrame = 0;
        subtex->startFrameVariation = static_cast<float>(data->subtexOffsets.size()) - kHandMarbleBirthCellMargin;
        subtex->endFrame = static_cast<float>(data->subtexOffsets.size() - 1);
        subtex->loopStartFrame = subtex->loopStartFrameVariation = 0;
        subtex->frameCount = frameCount;
        subtex->frameCountVariation = kHandMarbleFrameCountFudge;
        system->modifierRefs.AddBlockRef(hdr.AddBlock(std::move(subtex)));
        data->hasRotationAngles = data->hasRotationSpeeds = data->hasTextureIndices = true;
        OrderHandParticleUpdate(hdr, id);
        return {};
    }

    void BrightenHandEmber(nifly::NifFile& nif, std::uint32_t id)
    {
        // Snapshot only this casting-art NIF so cloning sequence entries cannot invalidate the
        // source iteration. Shared shaders and all shared float/data paths stay unmodified.
        // O(embers * blocks): a future large particle graph should batch snapshots and index links.
        nifly::NifFile before(nif);
        auto& src = before.GetHeader();
        auto& dst = nif.GetHeader();
        auto* system = dst.GetBlock<nifly::NiParticleSystem>(id);
        Remap map;
        system->shaderPropertyRef.index = CloneGraph(dst, src, system->shaderPropertyRef.index, nifly::NIF_NPOS, map);
        CloneSequenceLinks(dst, src, id, map, system->name.get(), system->name.get(), true);
        RemapPointers(dst, map);
        auto* shader = dst.GetBlock<nifly::BSEffectShaderProperty>(system->shaderPropertyRef);
        shader->baseColorScale = std::clamp(shader->baseColorScale * kHandEmberBrightness, 0.0f, kHandEmberMaxBrightness);
        Remap values;
        std::set<std::uint32_t> done;
        const auto boost = [&](nifly::NiRef& ref) {
            ref.index = CloneGraph(dst, dst, ref.index, nifly::NIF_NPOS, values);
            BoostFloat(dst, ref.index, done);
        };
        const auto emissive = [&](std::uint32_t ctrlId) {
            auto* ctrl = dst.GetBlock<nifly::BSEffectShaderPropertyFloatController>(ctrlId);
            return ctrl && ctrl->targetRef.index == system->shaderPropertyRef.index &&
                ctrl->typeOfControlledVariable == kFloatVarEmissiveMultiple;
        };
        for (const auto& [oldId, newId] : map)
            if (emissive(newId)) boost(dst.GetBlock<nifly::NiFloatInterpController>(newId)->interpolatorRef);
        for (std::uint32_t b = 0; b < dst.GetNumBlocks(); ++b)
            if (auto* seq = dst.GetBlock<nifly::NiControllerSequence>(b))
                for (auto& l : seq->controlledBlocks)
                    if (emissive(l.controllerRef.index)) boost(l.interpolatorRef);
    }

    void ReduceDarkEmberRate(nifly::NiHeader& hdr, const HandGraph::Remap& map, std::uint32_t id)
    {
        Remap values;
        std::set<std::uint32_t> done;
        const auto reduce = [&](nifly::NiRef& ref) {
            ref.index = CloneGraph(hdr, hdr, ref.index, nifly::NIF_NPOS, values);
            ScaleFloat(hdr, ref.index, kHandDarkEmberBirthFraction, done);
        };
        for (const auto& [oldId, newId] : map)
            if (auto* ctrl = hdr.GetBlock<nifly::NiPSysEmitterCtlr>(newId)) reduce(ctrl->interpolatorRef);
        for (std::uint32_t b = 0; b < hdr.GetNumBlocks(); ++b) {
            auto* seq = hdr.GetBlock<nifly::NiControllerSequence>(b);
            if (!seq) continue;
            for (auto& l : seq->controlledBlocks) {
                auto* ctrl = hdr.GetBlock<nifly::NiPSysEmitterCtlr>(l.controllerRef.index);
                // This controller has TWO sequence channels: float Birth Rate and bool
                // Emitter Active (including blend/timeline bools). Preserve activation keys.
                if (hdr.GetBlock<nifly::NiBoolInterpolator>(l.interpolatorRef) ||
                    hdr.GetBlock<nifly::NiBlendBoolInterpolator>(l.interpolatorRef)) continue;
                if (ctrl && ctrl->targetRef.index == id) reduce(l.interpolatorRef);
            }
        }
        OrderHandParticleUpdate(hdr, id);
    }
}
