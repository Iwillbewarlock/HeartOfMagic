// =============================================================================
// GreyDump - prints what the builder reads and writes (--dump)
// =============================================================================

#include "GreyBuild.h"

#include <Animation.hpp>
#include <NifFile.hpp>
#include <Particles.hpp>
#include <Shaders.hpp>

#include <fmt/format.h>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace GreyBuild
{
    namespace
    {
        [[nodiscard]] std::string ColorText(const nifly::Color4& a_color)
        {
            return fmt::format("({:.4f}, {:.4f}, {:.4f}, {:.4f})", a_color.r, a_color.g, a_color.b, a_color.a);
        }

        [[nodiscard]] std::string NameOf(const nifly::NiObjectNET& a_object)
        {
            return a_object.name.get().empty() ? std::string("<no name>") : a_object.name.get();
        }

        void DumpParticleBlock(const nifly::NiHeader& a_hdr, std::uint32_t a_id)
        {
            auto* block = a_hdr.GetBlock<nifly::NiObject>(a_id);
            if (!block) return;
            if (const auto* system = dynamic_cast<nifly::NiParticleSystem*>(block)) {
                std::cout << fmt::format("  [{}] {} '{}' data {} psysData {} shader {} alpha {} controller {} worldSpace {}\n",
                    a_id, block->GetBlockName(), NameOf(*system), system->dataRef.index, system->psysDataRef.index,
                    system->shaderPropertyRef.index, system->alphaPropertyRef.index, system->controllerRef.index,
                    system->isWorldSpace);
            } else if (const auto* data = dynamic_cast<nifly::NiPSysData*>(block)) {
                std::cout << fmt::format("  [{}] {} particle pool {} active {} atlas cells {}\n", a_id,
                    block->GetBlockName(), data->GetNumVertices(), data->numActive, data->subtexOffsets.size());
                for (std::uint32_t i = 0; i < data->subtexOffsets.size(); ++i) {
                    const auto& uv = data->subtexOffsets.data()[i];
                    std::cout << fmt::format("      atlas {} ({:.4f}, {:.4f}, {:.4f}, {:.4f})\n", i, uv.x, uv.y, uv.z, uv.w);
                }
            } else if (const auto* emitter = dynamic_cast<nifly::NiPSysEmitter*>(block)) {
                const auto* volume = dynamic_cast<const nifly::NiPSysVolumeEmitter*>(emitter);
                std::cout << fmt::format("  [{}] {} '{}' target {} emitterNode {} size {:.4f} +/- {:.4f} lifetime {:.4f} +/- {:.4f} colour {}\n",
                    a_id, block->GetBlockName(), emitter->name.get(), emitter->targetRef.index,
                    volume ? volume->emitterNodeRef.index : nifly::NIF_NPOS, emitter->radius, emitter->radiusVariation,
                    emitter->lifeSpan, emitter->lifeSpanVariation, ColorText(emitter->color));
            } else if (const auto* ctrl = dynamic_cast<nifly::NiPSysModifierCtlr*>(block)) {
                std::cout << fmt::format("  [{}] {} modifier '{}' target {} next {} interpolator {}\n", a_id,
                    block->GetBlockName(), ctrl->modifierName.get(), ctrl->targetRef.index,
                    ctrl->nextControllerRef.index, ctrl->interpolatorRef.index);
            } else if (const auto* rotation = dynamic_cast<nifly::NiPSysRotationModifier*>(block)) {
                std::cout << fmt::format("  [{}] NiPSysRotationModifier angle {:.4f} +/- {:.4f} target {}\n", a_id,
                    rotation->initialAngle, rotation->initialAngleVariation, rotation->targetRef.index);
            } else if (const auto* interp = dynamic_cast<nifly::NiFloatInterpolator*>(block)) {
                std::cout << fmt::format("  [{}] NiFloatInterpolator value {:.4f} data {}\n", a_id,
                    interp->floatValue, interp->dataRef.index);
            } else if (const auto* keys = dynamic_cast<nifly::NiFloatData*>(block)) {
                std::cout << fmt::format("  [{}] NiFloatData keys {}\n", a_id, keys->data.GetNumKeys());
                for (unsigned i = 0; i < keys->data.GetNumKeys(); ++i) {
                    const auto key = keys->data.GetKey(i);
                    std::cout << fmt::format("      t={:.4f} value {:.4f}\n", key.time, key.value);
                }
            } else if (auto* sequence = dynamic_cast<nifly::NiSequence*>(block)) {
                std::cout << fmt::format("  [{}] {} '{}' links {}\n", a_id, block->GetBlockName(),
                    sequence->name.get(), sequence->controlledBlocks.size());
                for (auto& link : sequence->controlledBlocks) {
                    std::cout << fmt::format("      '{}' controller {} interpolator {} modifier '{}'\n",
                        link.nodeName.get(), link.controllerRef.index, link.interpolatorRef.index, link.ctrlID.get());
                }
            } else if (const auto* alpha = dynamic_cast<nifly::NiAlphaProperty*>(block)) {
                std::cout << fmt::format("  [{}] NiAlphaProperty flags {} (blend {}, src {}, dst {}, test {}, no-sorter {})\n",
                    a_id, alpha->flags, (alpha->flags & 1) != 0, (alpha->flags >> 1) & 15,
                    (alpha->flags >> 5) & 15, (alpha->flags & 512) != 0, (alpha->flags & 8192) != 0);
            }
            // Include every block and link so a saved dump can independently audit graph remapping.
            std::set<nifly::NiRef*> refs;
            std::set<nifly::NiPtr*> ptrs;
            block->GetChildRefs(refs);
            if (auto* blend = dynamic_cast<nifly::NiBlendInterpolator*>(block))
                for (auto& item : blend->interpItems) refs.insert(&item.interpolatorRef);
            block->GetPtrs(ptrs);
            std::cout << fmt::format("      graph [{}] {} refs", a_id, block->GetBlockName());
            for (const auto* ref : refs) if (!ref->IsEmpty()) std::cout << ' ' << ref->index;
            std::cout << " ptrs";
            for (const auto* ptr : ptrs) if (!ptr->IsEmpty()) std::cout << ' ' << ptr->index;
            std::cout << '\n';
        }

        void DumpColorController(const nifly::NiHeader& a_hdr, std::uint32_t a_id,
            const nifly::BSEffectShaderPropertyColorController& a_ctrl)
        {
            std::cout << fmt::format("  [{}] BSEffectShaderPropertyColorController colour type {}\n", a_id,
                a_ctrl.typeOfControlledColor);
            const nifly::NiRef& interpRef = a_ctrl.interpolatorRef;
            const auto* interp = a_hdr.GetBlock<nifly::NiPoint3Interpolator>(interpRef);
            if (!interp) {
                std::cout << "      (no NiPoint3Interpolator)\n";
                return;
            }
            std::cout << fmt::format("      constant ({:.4f}, {:.4f}, {:.4f})\n", interp->point3Value.x,
                interp->point3Value.y, interp->point3Value.z);
            const nifly::NiRef& dataRef = interp->dataRef;
            const auto* data = a_hdr.GetBlock<nifly::NiPosData>(dataRef);
            if (!data) {
                return;
            }
            const auto keyCount = static_cast<int>(data->data.GetNumKeys());
            for (int i = 0; i < keyCount; ++i) {
                const auto key = data->data.GetKey(i);
                std::cout << fmt::format("      key t={:.3f} ({:.4f}, {:.4f}, {:.4f})\n", key.time, key.value.x,
                    key.value.y, key.value.z);
            }
        }
    }

    int DumpNif(const std::filesystem::path& a_file)
    {
        nifly::NifFile nif;
        try {
            LoadNif(nif, a_file);
        } catch (const std::exception& e) {
            std::cerr << "FAIL " << e.what() << "\n";
            return 1;
        }
        const auto& hdr = nif.GetHeader();
        const auto& version = hdr.GetVersion();
        std::cout << fmt::format("{}\n  version {} user {} stream {} (SSE: {}), {} blocks, {} unknown\n",
            a_file.string(), version.String(), version.User(), version.Stream(), version.IsSSE() ? "yes" : "no",
            hdr.GetNumBlocks(), CountUnknownBlocks(nif));

        std::map<std::string, int> census;
        for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
            if (auto* block = hdr.GetBlock<nifly::NiObject>(id)) {
                ++census[block->GetBlockName()];
            }
        }
        std::cout << "  blocks:";
        for (const auto& [name, count] : census) {
            std::cout << fmt::format(" {} x{};", name, count);
        }
        std::cout << "\n";

        for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
            DumpParticleBlock(hdr, id);
            if (const auto* node = hdr.GetBlock<nifly::BSValueNode>(id)) {
                std::cout << fmt::format("  [{}] BSValueNode '{}' value {} flags {}\n", id, NameOf(*node),
                    node->value, static_cast<int>(node->valueFlags));
            } else if (const auto* prop = hdr.GetBlock<nifly::BSEffectShaderProperty>(id)) {
                std::cout << fmt::format(
                    "  [{}] BSEffectShaderProperty '{}' SLSF1 0x{:08X} (palette colour {}, palette alpha {}) "
                    "SLSF2 0x{:08X}\n      source '{}'\n      greyscale '{}'\n      base {} x{:.3f}\n",
                    id, NameOf(*prop), prop->shaderFlags1,
                    (prop->shaderFlags1 & kSlsf1GreyscaleToPaletteColor) ? "on" : "off",
                    (prop->shaderFlags1 & kSlsf1GreyscaleToPaletteAlpha) ? "on" : "off", prop->shaderFlags2,
                    prop->sourceTexture.get(), prop->greyscaleTexture.get(), ColorText(prop->baseColor),
                    prop->baseColorScale);
                std::cout << fmt::format("      z-test {} z-write {}\n",
                    (prop->shaderFlags1 & (1u << 31)) ? "on" : "off", (prop->shaderFlags2 & 1) ? "on" : "off");
            } else if (const auto* lit = hdr.GetBlock<nifly::BSLightingShaderProperty>(id)) {
                std::cout << fmt::format("  [{}] BSLightingShaderProperty '{}' (left alone)\n", id, NameOf(*lit));
            } else if (const auto* mod = hdr.GetBlock<nifly::BSPSysSimpleColorModifier>(id)) {
                std::cout << fmt::format("  [{}] BSPSysSimpleColorModifier '{}' keys {} {} {}\n", id,
                    mod->name.get(), ColorText(mod->color1), ColorText(mod->color2), ColorText(mod->color3));
            } else if (const auto* ctrl = hdr.GetBlock<nifly::BSEffectShaderPropertyColorController>(id)) {
                DumpColorController(hdr, id, *ctrl);
            } else if (const auto* colorMod = hdr.GetBlock<nifly::NiPSysColorModifier>(id)) {
                std::cout << fmt::format("  [{}] NiPSysColorModifier '{}' data block {}\n", id,
                    colorMod->name.get(), colorMod->dataRef.index);
            } else if (hdr.GetBlock<nifly::NiUnknown>(id)) {
                std::cout << fmt::format("  [{}] unknown block (kept as raw bytes)\n", id);
            }
        }
        return 0;
    }

    int RoundTripReport(const std::filesystem::path& a_file)
    {
        try {
            const RoundTrip result = CheckRoundTrip(a_file);
            std::cout << fmt::format("{}\n  {}: {}\n", a_file.string(), RoundTripName(result.kind), result.detail);
            return result.kind == RoundTripKind::Different ? 1 : 0;
        } catch (const std::exception& e) {
            std::cerr << "FAIL " << e.what() << "\n";
            return 1;
        }
    }
}
