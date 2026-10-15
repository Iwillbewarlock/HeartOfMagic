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
