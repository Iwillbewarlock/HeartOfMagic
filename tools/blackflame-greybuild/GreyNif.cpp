// =============================================================================
// GreyNif - the two transforms (grey effects, AddOnNode remap) and NIF I/O
// =============================================================================

#include "GreyBuild.h"

#include <Animation.hpp>
#include <NifFile.hpp>
#include <Particles.hpp>
#include <Shaders.hpp>

#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace GreyBuild
{
    namespace
    {
        // nifly's NifFile::Load / Save return 0 on success
        constexpr int kNiflyOk = 0;

        [[nodiscard]] std::string Lower(std::string a_text)
        {
            std::transform(a_text.begin(), a_text.end(), a_text.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return a_text;
        }

        /// RGB set to luminance x a_scale, alpha kept
        void Desaturate(nifly::Color4& a_color, float a_scale) noexcept
        {
            const float grey = Luminance(a_color.r, a_color.g, a_color.b) * a_scale;
            a_color.r = grey;
            a_color.g = grey;
            a_color.b = grey;
        }

        void Desaturate(nifly::Vector3& a_rgb, float a_scale) noexcept
        {
            const float grey = Luminance(a_rgb.x, a_rgb.y, a_rgb.z) * a_scale;
            a_rgb.x = grey;
            a_rgb.y = grey;
            a_rgb.z = grey;
        }

        [[nodiscard]] bool IsGrey(const nifly::Color4& a_color) noexcept
        {
            return a_color.r == a_color.g && a_color.g == a_color.b;
        }

        template <class T, class Fn>
        void ForEachBlock(nifly::NifFile& a_nif, Fn&& a_fn)
        {
            auto& hdr = a_nif.GetHeader();
            const std::uint32_t count = hdr.GetNumBlocks();
            for (std::uint32_t id = 0; id < count; ++id) {
                if (auto* block = hdr.GetBlock<T>(id)) {
                    a_fn(id, *block);
                }
            }
        }

        /// The keys (and constant) of an effect colour controller's NiPoint3Interpolator:
        /// the same rule as the base colour it drives (luminance x tint).
        void GreyColorController(nifly::NifFile& a_nif, nifly::BSEffectShaderPropertyColorController& a_ctrl,
            float a_tint, std::set<std::uint32_t>& a_done, GreyStats& a_stats)
        {
            auto& hdr = a_nif.GetHeader();
            const nifly::NiRef& interpRef = a_ctrl.interpolatorRef;
            auto* interp = hdr.GetBlock<nifly::NiPoint3Interpolator>(interpRef);
            if (!interp) {
                ++a_stats.controllersUnreached;  // e.g. a blend interpolator fed by controller sequences
                return;
            }
            if (!a_done.insert(interpRef.index).second) {
                return;
            }
            Desaturate(interp->point3Value, a_tint);
            const nifly::NiRef& dataRef = interp->dataRef;
            auto* data = hdr.GetBlock<nifly::NiPosData>(dataRef);
            if (!data || !a_done.insert(dataRef.index).second) {
                return;
            }
            const auto keyCount = static_cast<int>(data->data.GetNumKeys());
            for (int i = 0; i < keyCount; ++i) {
                auto key = data->data.GetKey(i);
                Desaturate(key.value, a_tint);
                Desaturate(key.forward, a_tint);  // tangents: the same linear map keeps the curve grey
                Desaturate(key.backward, a_tint);
                data->data.SetKey(i, key);
                ++a_stats.controllerKeys;
            }
        }

        void GreyColorData(nifly::NifFile& a_nif, nifly::NiPSysColorModifier& a_mod, std::set<std::uint32_t>& a_done,
            GreyStats& a_stats)
        {
            const nifly::NiRef& dataRef = a_mod.dataRef;
            auto* data = a_nif.GetHeader().GetBlock<nifly::NiColorData>(dataRef);
            if (!data || !a_done.insert(dataRef.index).second) {
                return;
            }
            const auto keyCount = static_cast<int>(data->data.GetNumKeys());
            for (int i = 0; i < keyCount; ++i) {
                auto key = data->data.GetKey(i);
                Desaturate(key.value, 1.0f);
                Desaturate(key.forward, 1.0f);
                Desaturate(key.backward, 1.0f);
                data->data.SetKey(i, key);
            }
            ++a_stats.psysColorData;
        }
    }

    float Luminance(float a_red, float a_green, float a_blue) noexcept
    {
        return a_red * kLumaRed + a_green * kLumaGreen + a_blue * kLumaBlue;
    }

    // =========================================================================
    // Grey effects
    // =========================================================================

    GreyStats GreyEffects(nifly::NifFile& a_nif, float a_tint)
    {
        GreyStats stats;
        std::set<std::uint32_t> done;  // data blocks two controllers may share: grey them once

        ForEachBlock<nifly::BSEffectShaderProperty>(a_nif, [&](std::uint32_t, nifly::BSEffectShaderProperty& a_prop) {
            ++stats.effectShaders;
            if ((a_prop.shaderFlags1 & kSlsf1GreyscaleToPaletteColor) == 0) {
                a_prop.shaderFlags1 |= kSlsf1GreyscaleToPaletteColor;
                ++stats.paletteForced;
            }
            a_prop.greyscaleTexture.get() = kPalettePath;
            Desaturate(a_prop.baseColor, a_tint);
        });
        ForEachBlock<nifly::BSLightingShaderProperty>(a_nif,
            [&](std::uint32_t, nifly::BSLightingShaderProperty&) { ++stats.lightingShaders; });
        ForEachBlock<nifly::BSPSysSimpleColorModifier>(a_nif,
            [&](std::uint32_t, nifly::BSPSysSimpleColorModifier& a_mod) {
                Desaturate(a_mod.color1, 1.0f);
                Desaturate(a_mod.color2, 1.0f);
                Desaturate(a_mod.color3, 1.0f);
                ++stats.colorModifiers;
            });
        ForEachBlock<nifly::BSEffectShaderPropertyColorController>(a_nif,
            [&](std::uint32_t, nifly::BSEffectShaderPropertyColorController& a_ctrl) {
                ++stats.colorControllers;
                GreyColorController(a_nif, a_ctrl, a_tint, done, stats);
            });
        ForEachBlock<nifly::NiPSysColorModifier>(a_nif,
            [&](std::uint32_t, nifly::NiPSysColorModifier& a_mod) { GreyColorData(a_nif, a_mod, done, stats); });
        return stats;
    }

    // =========================================================================
    // AddOnNode remap
    // =========================================================================

    RemapStats RemapAddonValues(nifly::NifFile& a_nif, const GreyMap& a_map)
    {
        std::set<int> greyIndices;
        for (const auto& [index, entry] : a_map.addons) {
            greyIndices.insert(entry.grey);
        }

        RemapStats stats;
        ForEachBlock<nifly::BSValueNode>(a_nif, [&](std::uint32_t a_id, nifly::BSValueNode& a_node) {
            ++stats.valueNodes;
            const std::string name = a_node.name.get().empty() ? fmt::format("block {}", a_id) : a_node.name.get();
            if (const auto it = a_map.addons.find(a_node.value); it != a_map.addons.end()) {
                stats.remapLines.push_back(fmt::format("{}: {} -> {}", name, a_node.value, it->second.grey));
                a_node.value = it->second.grey;
                ++stats.remapped;
            } else if (greyIndices.count(a_node.value) != 0) {
                ++stats.alreadyGrey;
            } else {
                stats.unmapped.push_back(fmt::format("{}: {}", name, a_node.value));
            }
        });
        return stats;
    }

    // =========================================================================
    // I/O
    // =========================================================================

    void LoadNif(nifly::NifFile& a_nif, const std::filesystem::path& a_file)
    {
        std::ifstream in(a_file, std::ios::binary);
        if (!in) {
            throw std::runtime_error(fmt::format("cannot open '{}' for reading", a_file.string()));
        }
        const int result = a_nif.Load(in);
        if (result != kNiflyOk || !a_nif.IsValid()) {
            throw std::runtime_error(fmt::format("nifly could not load '{}' (code {}): not a NIF it reads",
                a_file.string(), result));
        }
    }

    std::string SaveToBytes(nifly::NifFile& a_nif)
    {
        nifly::NifSaveOptions options;
        options.optimize = false;    // no bounds update, no unreferenced block removal
        options.sortBlocks = false;  // keep the source's block order
        std::ostringstream out(std::ios::binary);
        const int result = a_nif.Save(out, options);
        if (result != kNiflyOk) {
            throw std::runtime_error(fmt::format("nifly could not write the NIF (code {})", result));
        }
        return out.str();
    }

    void SaveNif(nifly::NifFile& a_nif, const std::filesystem::path& a_file)
    {
        const std::string bytes = SaveToBytes(a_nif);
        std::filesystem::create_directories(a_file.parent_path());
        std::ofstream out(a_file, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error(fmt::format("cannot open '{}' for writing", a_file.string()));
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();  // the last buffer is flushed here; a full disk shows only now
        if (!out) {
            throw std::runtime_error(fmt::format("writing '{}' failed", a_file.string()));
        }
    }

    int CountUnknownBlocks(const nifly::NifFile& a_nif)
    {
        const auto& hdr = a_nif.GetHeader();
        int unknown = 0;
        for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
            unknown += hdr.GetBlock<nifly::NiUnknown>(id) ? 1 : 0;
        }
        return unknown;
    }

    // =========================================================================
    // Verify
    // =========================================================================

    std::vector<std::string> VerifyGrey(nifly::NifFile& a_nif, bool a_expectGreyEffects, const GreyMap& a_map)
    {
        std::vector<std::string> problems;
        const std::string palette = Lower(kPalettePath);
        if (a_expectGreyEffects) {
            ForEachBlock<nifly::BSEffectShaderProperty>(a_nif,
                [&](std::uint32_t a_id, nifly::BSEffectShaderProperty& a_prop) {
                    if ((a_prop.shaderFlags1 & kSlsf1GreyscaleToPaletteColor) == 0) {
                        problems.push_back(fmt::format("effect shader block {}: palette colour flag off", a_id));
                    }
                    if (Lower(a_prop.greyscaleTexture.get()) != palette) {
                        problems.push_back(fmt::format("effect shader block {}: greyscale texture '{}'", a_id,
                            a_prop.greyscaleTexture.get()));
                    }
                    if (!IsGrey(a_prop.baseColor)) {
                        problems.push_back(fmt::format("effect shader block {}: base colour not grey", a_id));
                    }
                });
            ForEachBlock<nifly::BSPSysSimpleColorModifier>(a_nif,
                [&](std::uint32_t a_id, nifly::BSPSysSimpleColorModifier& a_mod) {
                    if (!IsGrey(a_mod.color1) || !IsGrey(a_mod.color2) || !IsGrey(a_mod.color3)) {
                        problems.push_back(fmt::format("colour modifier block {}: a key is not grey", a_id));
                    }
                });
        }
        const RemapStats values = RemapAddonValues(a_nif, a_map);  // on the reloaded copy: nothing may change
        if (values.remapped != 0) {
            problems.push_back(fmt::format("{} AddOnNode value(s) still hold an original index", values.remapped));
        }
        for (const auto& line : values.unmapped) {
            problems.push_back(fmt::format("AddOnNode {} is in no map entry", line));
        }
        return problems;
    }
}
