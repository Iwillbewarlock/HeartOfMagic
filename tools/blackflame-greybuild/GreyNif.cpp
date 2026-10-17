// =============================================================================
// GreyNif - the two transforms (grey effects, AddOnNode remap) and NIF I/O
// =============================================================================

#include "GreyBuild.h"

#include "../../plugins/BlackFlame/src/DataPathRule.h"
#include "GreyPalette.h"
#include "GreyShapes.h"

#include <Animation.hpp>
#include <NifFile.hpp>
#include <Particles.hpp>
#include <Shaders.hpp>

#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <fstream>
#include <map>
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


        enum class Blend
        {
            kNoAlpha,
            kNormal,
            kAdditive
        };

        // The blend of the shape(s) that use each effect shader: an NiShape or an NiParticleSystem names its shader and its
        // alpha property. A shader two shapes share with different blends counts as additive if any of them is.
        std::map<std::uint32_t, Blend> BlendByShader(nifly::NifFile& a_nif)
        {
            auto& hdr = a_nif.GetHeader();
            std::map<std::uint32_t, Blend> out;
            const auto note = [&](std::uint32_t a_shader, std::uint32_t a_alpha) {
                if (a_shader == nifly::NIF_NPOS) {
                    return;
                }
                const auto* alpha = a_alpha == nifly::NIF_NPOS ? nullptr : hdr.GetBlock<nifly::NiAlphaProperty>(a_alpha);
                const ShapeBlend shape = BlendOfAlpha(alpha);  // Z12: the one blend classification (GreyShapes.h)
                const Blend blend = shape == ShapeBlend::kNoAlpha  ? Blend::kNoAlpha :
                                    shape == ShapeBlend::kAdditive ? Blend::kAdditive :
                                                                     Blend::kNormal;
                auto& slot = out.try_emplace(a_shader, blend).first->second;
                slot = std::max(slot, blend);
            };
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                if (auto* shape = hdr.GetBlock<nifly::NiShape>(id)) {
                    const auto* shader = shape->ShaderPropertyRef();
                    const auto* alpha = shape->AlphaPropertyRef();
                    note(shader ? shader->index : nifly::NIF_NPOS, alpha ? alpha->index : nifly::NIF_NPOS);
                } else if (auto* system = hdr.GetBlock<nifly::NiParticleSystem>(id)) {
                    note(system->shaderPropertyRef.index, system->alphaPropertyRef.index);
                }
            }
            return out;
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

        /// The keys (and constant) of an effect colour controller's NiPoint3Interpolator: the same rule as the base colour it
        /// drives (luminance x tint). a_tintOf gives the scale of a block (the interpolator, its data): the largest of the
        /// controllers that share it (I12), so a block is greyed once.
        template <class TintOf>
        void GreyColorController(nifly::NifFile& a_nif, nifly::BSEffectShaderPropertyColorController& a_ctrl,
            const TintOf& a_tintOf, std::set<std::uint32_t>& a_done, GreyStats& a_stats)
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
            Desaturate(interp->point3Value, a_tintOf(interpRef.index));
            const nifly::NiRef& dataRef = interp->dataRef;
            auto* data = hdr.GetBlock<nifly::NiPosData>(dataRef);
            if (!data || !a_done.insert(dataRef.index).second) {
                return;
            }
            const float tint = a_tintOf(dataRef.index);
            const auto keyCount = static_cast<int>(data->data.GetNumKeys());
            for (int i = 0; i < keyCount; ++i) {
                auto key = data->data.GetKey(i);
                Desaturate(key.value, tint);
                Desaturate(key.forward, tint);  // tangents: the same linear map keeps the curve grey
                Desaturate(key.backward, tint);
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

    bool UsesPalette(const nifly::BSEffectShaderProperty& a_prop)
    {
        return (a_prop.shaderFlags1 & kSlsf1GreyscaleToPaletteColor) != 0;  // Y1: the flag alone
    }

    void AddShaderCounts(GreyStats& a_sum, const GreyStats& a_stats) noexcept
    {
        a_sum.effectShaders += a_stats.effectShaders;
        a_sum.paletted += a_stats.paletted;
        a_sum.palettedAdd += a_stats.palettedAdd;
        a_sum.paletteNoAlpha += a_stats.paletteNoAlpha;
        a_sum.paletteLeft += a_stats.paletteLeft;
    }

    // =========================================================================
    // Grey effects
    // =========================================================================

    GreyStats GreyEffects(nifly::NifFile& a_nif, float a_tint, float a_tintAdd, std::vector<std::string>* a_log)
    {
        GreyStats stats;
        std::set<std::uint32_t> done;  // data blocks two controllers may share: grey them once
        const auto blends = BlendByShader(a_nif);
        std::set<std::uint32_t> additive;  // shaders that took the additive palette: their colour controllers use a_tintAdd

        // v2i (cause A2), Y1: only a shader whose palette colour flag is already on gets ours (the shader samples the palette
        // with or without a source texture). Forcing the flag and the
        // palette on the others (textureless glows, FXWhite tint spheres, lens flares, palette-alpha-only shaders) turned
        // them into flat grey shapes; they keep their flags and textures and only their base colour is greyed
        // v2i (cause A1): an additive palette shader gets the bright-edge palette and its own tint (additive cannot draw dark)
        ForEachBlock<nifly::BSEffectShaderProperty>(a_nif, [&](std::uint32_t a_id, nifly::BSEffectShaderProperty& a_prop) {
            ++stats.effectShaders;
            if (!UsesPalette(a_prop)) {
                ++stats.paletteLeft;
                Desaturate(a_prop.baseColor, a_tint);
                return;
            }
            const auto it = blends.find(a_id);
            // I12: a shader no shape uses has no blend to tell: normal palette, without the no-alpha NOTE
            const Blend blend = it != blends.end() ? it->second : Blend::kNormal;
            if (blend == Blend::kAdditive) {
                a_prop.greyscaleTexture.get() = kPaletteAddPath;
                ++stats.palettedAdd;
                additive.insert(a_id);
                Desaturate(a_prop.baseColor, a_tintAdd);
                return;
            }
            if (blend == Blend::kNoAlpha) {
                ++stats.paletteNoAlpha;
                if (a_log) {
                    a_log->push_back(fmt::format("effect shader block {} '{}': no NiAlphaProperty on its shape - normal palette",
                        a_id, a_prop.name.get()));
                }
            }
            a_prop.greyscaleTexture.get() = kPalettePath;
            ++stats.paletted;
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
        // Each controller's scale is the base colour's of the shader it targets; an interpolator or key block two controllers
        // share takes the larger of their scales (noted when they differ)
        std::map<std::uint32_t, float> blockTint;
        std::set<std::uint32_t> mixed;
        ForEachBlock<nifly::BSEffectShaderPropertyColorController>(a_nif,
            [&](std::uint32_t, nifly::BSEffectShaderPropertyColorController& a_ctrl) {
                const float tint = additive.count(a_ctrl.targetRef.index) != 0 ? a_tintAdd : a_tint;
                const auto note = [&](std::uint32_t a_block) {
                    if (a_block == nifly::NIF_NPOS) {
                        return;
                    }
                    auto [it, fresh] = blockTint.try_emplace(a_block, tint);
                    if (!fresh && it->second != tint) {
                        mixed.insert(a_block);
                        it->second = std::max(it->second, tint);
                    }
                };
                note(a_ctrl.interpolatorRef.index);
                if (const auto* interp = a_nif.GetHeader().GetBlock<nifly::NiPoint3Interpolator>(a_ctrl.interpolatorRef)) {
                    note(interp->dataRef.index);
                }
            });
        for (const auto block : mixed) {
            if (a_log) {
                a_log->push_back(fmt::format("block {}: colour keys shared by controllers of additive and normal shaders - greyed "
                    "with the larger scale {}", block, blockTint[block]));
            }
        }
        const auto tintOf = [&](std::uint32_t a_block) {
            const auto it = blockTint.find(a_block);
            return it != blockTint.end() ? it->second : a_tint;
        };
        ForEachBlock<nifly::BSEffectShaderPropertyColorController>(a_nif,
            [&](std::uint32_t, nifly::BSEffectShaderPropertyColorController& a_ctrl) {
                ++stats.colorControllers;
                GreyColorController(a_nif, a_ctrl, tintOf, done, stats);
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

    namespace
    {
        std::vector<std::string> HeaderStrings(const nifly::NifFile& a_nif)
        {
            const auto& hdr = a_nif.GetHeader();
            std::vector<std::string> out;
            out.reserve(hdr.GetStringCount());
            for (std::uint32_t i = 0; i < hdr.GetStringCount(); ++i) {
                out.push_back(hdr.GetStringById(i));
            }
            return out;
        }

        // v2i: nifly's Save rebuilds the header string table from the string references it knows (FinalizeData ->
        // NiHeader::UpdateHeaderStrings(false): ClearStrings, then each block's GetStringRefs in block order), so strings
        // no known reference names are dropped and the rest renumbered (Dragonfire's copy lost 'AutoPlay',
        // 'GenericBehaviors\Autoplay.hkx', 'Magic\SprayRange.hkx'). A reference nifly does not enumerate keeps its raw
        // index and would then name another string. Here the source's table comes back exactly (same strings, same
        // order, duplicates included: source tables do hold a string twice, e.g. icehazard02.nif's 'IceHazard02' at 0 and
        // 72) after nifly's Save; every known reference gets back its index from before the Save when it still names that
        // string (so a reference to the second of two equal strings keeps the second), else the first equal string or a
        // new one at the end; and the file is written the way NifFile::Save writes it (nifly 680a3c0, the vcpkg port),
        // without the rebuild. False (nothing written) when the references no longer line up with the snapshot.
        std::vector<std::uint32_t> RefIndices(nifly::NifFile& a_nif, std::vector<nifly::NiStringRef*>* a_refs = nullptr)
        {
            auto& hdr = a_nif.GetHeader();
            std::vector<std::uint32_t> out;
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                auto* block = hdr.GetBlock<nifly::NiObject>(id);
                if (!block) {
                    continue;
                }
                std::vector<nifly::NiStringRef*> refs;
                block->GetStringRefs(refs);
                for (auto* ref : refs) {
                    out.push_back(ref->GetIndex());
                    if (a_refs) {
                        a_refs->push_back(ref);
                    }
                }
            }
            return out;
        }

        bool WriteKeepingStrings(nifly::NifFile& a_nif, const std::vector<std::string>& a_strings,
            const std::vector<std::uint32_t>& a_before, std::ostream& a_out)
        {
            auto& hdr = a_nif.GetHeader();
            std::vector<nifly::NiStringRef*> refs;
            (void)RefIndices(a_nif, &refs);
            if (refs.size() != a_before.size()) {
                return false;
            }
            // The table as it was: slots made with distinct placeholders (AddOrFindStringId would merge equal strings), then
            // each slot set to its string
            hdr.ClearStrings();
            for (std::uint32_t i = 0; i < a_strings.size(); ++i) {
                if (hdr.AddOrFindStringId(fmt::format("\x01HoMBF slot {}", i), true) != i) {
                    hdr.UpdateHeaderStrings(false);  // I11: nifly's own table back from the references (none was moved yet)
                    return false;
                }
            }
            for (std::uint32_t i = 0; i < a_strings.size(); ++i) {
                hdr.SetStringById(i, a_strings[i]);
            }
            for (std::size_t i = 0; i < refs.size(); ++i) {
                auto* ref = refs[i];
                const std::uint32_t was = a_before[i];
                if (was != nifly::NIF_NPOS && was < a_strings.size() && a_strings[was] == ref->get()) {
                    ref->SetIndex(was);  // the very slot it named
                } else if (was == nifly::NIF_NPOS && ref->get().empty()) {
                    // I10: no string in the source stays none (nifly's Save may have pointed it at an empty table string)
                    ref->SetIndex(nifly::NIF_NPOS);
                } else {
                    ref->SetIndex(hdr.AddOrFindStringId(ref->get(), true));  // changed or new: first equal string, or appended
                }
            }
            hdr.UpdateMaxStringLength();

            nifly::NiOStream stream(&a_out, hdr.GetVersion());
            hdr.Put(stream);
            stream.InitBlockSize();
            std::vector<std::streamsize> sizes(hdr.GetNumBlocks());
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                hdr.GetBlock<nifly::NiObject>(id)->Put(stream);
                sizes[id] = stream.GetBlockSize();
                stream.InitBlockSize();
            }
            std::uint32_t endPad = 1;  // the footer: one root, block 0 (as NifFile::Save)
            stream << endPad;
            endPad = 0;
            stream << endPad;
            if (const auto sizePos = hdr.GetBlockSizeStreamPos(); sizePos != std::streampos()) {
                a_out.seekp(sizePos);
                for (const auto size : sizes) {
                    stream << static_cast<std::uint32_t>(size);
                }
                hdr.ResetBlockSizeStreamPos();
            }
            return true;
        }
    }

    std::string SaveToBytes(nifly::NifFile& a_nif)
    {
        const auto strings = HeaderStrings(a_nif);  // the source's table, as loaded (nifly does not touch it at load)
        const auto before = RefIndices(a_nif);       // each known reference's index into it
        nifly::NifSaveOptions options;
        options.optimize = false;    // no bounds update, no unreferenced block removal
        options.sortBlocks = false;  // keep the source's block order
        std::ostringstream out(std::ios::binary);
        const int result = a_nif.Save(out, options);
        if (result != kNiflyOk) {
            throw std::runtime_error(fmt::format("nifly could not write the NIF (code {})", result));
        }
        std::ostringstream kept(std::ios::binary);
        if (!WriteKeepingStrings(a_nif, strings, before, kept) || !kept) {
            return out.str();  // the references did not line up: nifly's own table (CheckHeaderStrings reports it)
        }
        // I11: our bytes parsed back before anyone writes them: a NIF nifly reads whose table starts with the source's;
        // otherwise nifly's own bytes (CheckHeaderStrings then reports the table)
        std::string bytes = kept.str();
        std::istringstream back(bytes, std::ios::binary);
        nifly::NifFile check;
        if (check.Load(back) != kNiflyOk || !check.IsValid()) {
            return out.str();
        }
        const auto written = HeaderStrings(check);
        if (written.size() < strings.size() || !std::equal(strings.begin(), strings.end(), written.begin())) {
            return out.str();
        }
        return bytes;
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

    std::vector<std::string> CheckHeaderStrings(const nifly::NifFile& a_source, const nifly::NifFile& a_written)
    {
        std::vector<std::string> problems;
        const auto before = HeaderStrings(a_source);
        const auto after = HeaderStrings(a_written);
        for (std::size_t i = 0; i < before.size(); ++i) {
            if (i >= after.size() || after[i] != before[i]) {
                problems.push_back(fmt::format("header string {} '{}' not kept (now '{}')", i, before[i],
                    i < after.size() ? after[i] : std::string("missing")));
                break;  // the first one says it
            }
        }
        return problems;
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

    std::vector<std::string> VerifyGrey(nifly::NifFile& a_nif, bool a_expectGreyEffects, const GreyMap& a_map, bool a_allowUnmapped)
    {
        std::vector<std::string> problems;
        const std::string palette = Lower(kPalettePath);
        const std::string paletteAdd = Lower(kPaletteAddPath);
        const auto blends = BlendByShader(a_nif);
        if (a_expectGreyEffects) {
            ForEachBlock<nifly::BSEffectShaderProperty>(a_nif,
                [&](std::uint32_t a_id, nifly::BSEffectShaderProperty& a_prop) {
                    // v2i, Y1, Z6: a palette shader (the flag on) carries one of ours for its blend; every other one only a grey
                    // base colour (flags and textures are never changed, so the copy tells which kind each was)
                    if (UsesPalette(a_prop)) {
                        const auto it = blends.find(a_id);
                        const bool additive = it != blends.end() && it->second == Blend::kAdditive;
                        // Z6: one of ours by the shared definition, and the one for its blend: our fixed palette or a
                        // palette derived for that blend (GreyPalette.h)
                        const std::string greyscale = Lower(a_prop.greyscaleTexture.get());
                        const std::string derived = std::string(kPaletteCopyRoot) + (additive ? kPaletteAddFolder : kPaletteNormalFolder);
                        if (!BlackFlame::DataPathRule::IsOurPalettePath(greyscale) ||
                            (greyscale != (additive ? paletteAdd : palette) && !StartsWith(greyscale, derived))) {
                            problems.push_back(fmt::format("effect shader block {}: {} palette shader, greyscale texture '{}'",
                                a_id, additive ? "additive" : "normal-blend", a_prop.greyscaleTexture.get()));
                        }
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
        for (const auto& line : a_allowUnmapped ? std::vector<std::string>() : values.unmapped) {
            problems.push_back(fmt::format("AddOnNode {} is in no map entry", line));
        }
        return problems;
    }
}
