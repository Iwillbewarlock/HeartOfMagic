// =============================================================================
// GreySelfTest - --self-test on a made-up NIF (no game files)
// =============================================================================
// Builds a small SSE NIF with AddOnNodes, effect and lighting shaders, a particle
// colour modifier and an effect colour controller, runs the same transforms the
// build runs, writes it, reads it back and checks every value. Also checks the map
// reader's refusals. Exit code 0 = all pass, 1 = a failure.

#include "GreySelfTest.h"

#include <Animation.hpp>
#include <NifFile.hpp>
#include <Particles.hpp>
#include <Shaders.hpp>

#include <cmath>
#include <fmt/format.h>
#include <fstream>
#include <iostream>
#include <memory>

namespace GreyBuild
{
    namespace
    {
        constexpr float kTestTint = 0.6f;
        constexpr float kTolerance = 1e-6f;
        constexpr int kMappedA = 9;
        constexpr int kMappedB = 24;
        constexpr int kGreyA = 9109;
        constexpr int kGreyB = 9124;

        const nifly::Color4 kWarmBase{ 1.0f, 0.5f, 0.2f, 0.8f };
        const nifly::Color4 kWarmBase2{ 0.9f, 0.3f, 0.1f, 1.0f };
        const nifly::Color4 kKey1{ 1.0f, 0.5f, 0.0f, 1.0f };
        const nifly::Color4 kKey2{ 0.8f, 0.2f, 0.0f, 0.5f };
        const nifly::Color4 kKey3{ 0.2f, 0.1f, 0.0f, 0.0f };
        const nifly::Vector3 kEmissive{ 1.0f, 0.5f, 0.0f };
        const nifly::Vector3 kCtrlKey0{ 1.0f, 0.4f, 0.1f };
        const nifly::Vector3 kCtrlKey1{ 0.6f, 0.2f, 0.0f };

        using SelfTest::Checker;

        [[nodiscard]] bool Near(float a_left, float a_right) noexcept { return std::fabs(a_left - a_right) <= kTolerance; }

        [[nodiscard]] bool IsGreyOf(const nifly::Color4& a_now, const nifly::Color4& a_was, float a_scale) noexcept
        {
            const float grey = Luminance(a_was.r, a_was.g, a_was.b) * a_scale;
            return Near(a_now.r, grey) && Near(a_now.g, grey) && Near(a_now.b, grey) && Near(a_now.a, a_was.a);
        }

        [[nodiscard]] bool IsGreyOf(const nifly::Vector3& a_now, const nifly::Vector3& a_was, float a_scale) noexcept
        {
            const float grey = Luminance(a_was.x, a_was.y, a_was.z) * a_scale;
            return Near(a_now.x, grey) && Near(a_now.y, grey) && Near(a_now.z, grey);
        }

        template <class T>
        T* AddBlock(nifly::NifFile& a_nif, std::unique_ptr<T> a_block, std::uint32_t& a_id)
        {
            T* raw = a_block.get();
            a_id = a_nif.GetHeader().AddBlock(std::move(a_block));
            return raw;
        }

        void AddValueNode(nifly::NifFile& a_nif, const std::string& a_name, int a_value)
        {
            auto node = std::make_unique<nifly::BSValueNode>();
            node->name.get() = a_name;
            node->value = a_value;
            std::uint32_t id = 0;
            AddBlock(a_nif, std::move(node), id);
            a_nif.GetRootNode()->childRefs.AddBlockRef(id);
        }

        void AddColorController(nifly::NifFile& a_nif)
        {
            std::uint32_t dataId = 0;
            auto* data = AddBlock(a_nif, std::make_unique<nifly::NiPosData>(), dataId);
            data->data.SetInterpolationType(nifly::LINEAR_KEY);
            nifly::NiAnimationKey<nifly::Vector3> key;
            key.type = nifly::LINEAR_KEY;
            key.time = 0.0f;
            key.value = kCtrlKey0;
            data->data.AddKey(key);
            key.time = 1.0f;
            key.value = kCtrlKey1;
            data->data.AddKey(key);

            std::uint32_t interpId = 0;
            auto* interp = AddBlock(a_nif, std::make_unique<nifly::NiPoint3Interpolator>(), interpId);
            interp->point3Value = kCtrlKey0;
            interp->dataRef.index = dataId;

            std::uint32_t ctrlId = 0;
            auto* ctrl = AddBlock(a_nif, std::make_unique<nifly::BSEffectShaderPropertyColorController>(), ctrlId);
            ctrl->interpolatorRef.index = interpId;
        }

        /// The made-up source: 2 AddOnNodes (9, 24), 2 effect shaders (palette off / on),
        /// a lighting shader, a simple colour modifier and an effect colour controller.
        void MakeSource(const std::filesystem::path& a_file)
        {
            nifly::NifFile nif;
            nif.Create(nifly::NiVersion::getSSE());
            AddValueNode(nif, "AddOnNode9", kMappedA);
            AddValueNode(nif, "AddOnNode24", kMappedB);

            std::uint32_t id = 0;
            auto* off = AddBlock(nif, std::make_unique<nifly::BSEffectShaderProperty>(), id);
            off->name.get() = "FlameOff";
            off->shaderFlags1 &= ~kSlsf1GreyscaleToPaletteColor;
            off->baseColor = kWarmBase;
            off->sourceTexture.get() = "textures\\effects\\fxfire01.dds";
            auto* on = AddBlock(nif, std::make_unique<nifly::BSEffectShaderProperty>(), id);
            on->name.get() = "FlameOn";
            on->shaderFlags1 |= kSlsf1GreyscaleToPaletteColor | kSlsf1GreyscaleToPaletteAlpha;
            on->baseColor = kWarmBase2;
            on->greyscaleTexture.get() = "textures\\effects\\gradients\\gradflame01.dds";

            auto* lit = AddBlock(nif, std::make_unique<nifly::BSLightingShaderProperty>(), id);
            lit->emissiveColor = kEmissive;

            auto* mod = AddBlock(nif, std::make_unique<nifly::BSPSysSimpleColorModifier>(), id);
            mod->color1 = kKey1;
            mod->color2 = kKey2;
            mod->color3 = kKey3;

            AddColorController(nif);
            SaveNif(nif, a_file);
        }

        [[nodiscard]] GreyMap TestMap(bool a_withB)
        {
            GreyMap map;
            map.tint = kTestTint;
            map.addons[kMappedA] = AddonEntry{ kMappedA, kGreyA, "meshes\\mps\\a.nif",
                "meshes\\heartofmagic\\blackflame\\mps\\a.nif" };
            if (a_withB) {
                map.addons[kMappedB] = AddonEntry{ kMappedB, kGreyB, "meshes\\mps\\b.nif",
                    "meshes\\heartofmagic\\blackflame\\mps\\b.nif" };
            }
            return map;
        }

        [[nodiscard]] std::string ReadBytes(const std::filesystem::path& a_file)
        {
            std::ifstream in(a_file, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        void CheckOutput(Checker& a_check, nifly::NifFile& a_nif, const GreyMap& a_map)
        {
            auto& hdr = a_nif.GetHeader();
            int values = 0;
            int effects = 0;
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                if (const auto* node = hdr.GetBlock<nifly::BSValueNode>(id)) {
                    ++values;
                    const bool a = node->name.get() == "AddOnNode9" && node->value == kGreyA;
                    const bool b = node->name.get() == "AddOnNode24" && node->value == kGreyB;
                    a_check.Check(a || b, fmt::format("value node '{}' = {} (name kept, value grey)",
                        node->name.get(), node->value));
                } else if (const auto* prop = hdr.GetBlock<nifly::BSEffectShaderProperty>(id)) {
                    ++effects;
                    const bool isOff = prop->name.get() == "FlameOff";
                    a_check.Check((prop->shaderFlags1 & kSlsf1GreyscaleToPaletteColor) != 0,
                        fmt::format("'{}' palette colour flag on", prop->name.get()));
                    a_check.Check(((prop->shaderFlags1 & kSlsf1GreyscaleToPaletteAlpha) != 0) == !isOff,
                        fmt::format("'{}' palette alpha flag as it was", prop->name.get()));
                    a_check.Check(prop->greyscaleTexture.get() == kPalettePath,
                        fmt::format("'{}' greyscale texture = palette", prop->name.get()));
                    a_check.Check(IsGreyOf(prop->baseColor, isOff ? kWarmBase : kWarmBase2, kTestTint),
                        fmt::format("'{}' base colour = luminance x tint, alpha kept", prop->name.get()));
                    if (isOff) {
                        a_check.Check(prop->sourceTexture.get() == "textures\\effects\\fxfire01.dds",
                            "source texture kept");
                    }
                } else if (const auto* lit = hdr.GetBlock<nifly::BSLightingShaderProperty>(id)) {
                    a_check.Check(Near(lit->emissiveColor.x, kEmissive.x) && Near(lit->emissiveColor.y, kEmissive.y),
                        "lighting shader left alone");
                } else if (const auto* mod = hdr.GetBlock<nifly::BSPSysSimpleColorModifier>(id)) {
                    a_check.Check(IsGreyOf(mod->color1, kKey1, 1.0f) && IsGreyOf(mod->color2, kKey2, 1.0f) &&
                                      IsGreyOf(mod->color3, kKey3, 1.0f),
                        "colour modifier keys = luminance, alpha kept");
                } else if (const auto* data = hdr.GetBlock<nifly::NiPosData>(id)) {
                    const bool ok = data->data.GetNumKeys() == 2 &&
                                    IsGreyOf(data->data.GetKey(0).value, kCtrlKey0, kTestTint) &&
                                    IsGreyOf(data->data.GetKey(1).value, kCtrlKey1, kTestTint);
                    a_check.Check(ok, "colour controller keys = luminance x tint");
                } else if (const auto* interp = hdr.GetBlock<nifly::NiPoint3Interpolator>(id)) {
                    a_check.Check(IsGreyOf(interp->point3Value, kCtrlKey0, kTestTint),
                        "colour controller constant = luminance x tint");
                }
            }
            a_check.Check(values == 2 && effects == 2, fmt::format("block counts kept ({} values, {} effects)", values,
                effects));
            const auto problems = VerifyGrey(a_nif, true, a_map);
            a_check.Check(problems.empty(), fmt::format("VerifyGrey finds nothing ({} problem(s))", problems.size()));
        }

        void CheckMapRefusals(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto write = [&](const char* a_name, const std::string& a_text) {
                const auto path = a_dir / a_name;
                std::ofstream(path, std::ios::binary) << a_text;
                return path;
            };
            const auto refuses = [&](const std::filesystem::path& a_file) {
                try {
                    (void)LoadMap(a_file);
                    return false;
                } catch (const std::runtime_error&) {
                    return true;
                }
            };
            const std::string entry =
                R"({"index":9,"grey":9109,"model":"Meshes/MPS/A.nif","greyModel":"meshes\\heartofmagic\\blackflame\\mps\\a.nif"})";
            const auto good = write("good.json", R"({"version":1,"tint":0.6,"addons":[)" + entry +
                                                     R"(],"models":{"meshes\\fx\\b.nif":"meshes\\heartofmagic\\blackflame\\fx\\b.nif"}})");
            bool goodLoads = false;
            try {
                const GreyMap map = LoadMap(good);
                goodLoads = map.addons.size() == 1 && map.addons.at(9).model == "meshes\\mps\\a.nif" &&
                            map.models.size() == 1;
            } catch (const std::runtime_error& e) {
                std::cout << "      " << e.what() << "\n";
            }
            a_check.Check(goodLoads, "map: a good map loads, paths lower case with backslashes");
            a_check.Check(refuses(write("dupgrey.json", R"({"version":1,"tint":0.6,"addons":[)" + entry + "," +
                                                            R"({"index":3,"grey":9109,"model":"meshes\\mps\\c.nif","greyModel":"meshes\\heartofmagic\\blackflame\\mps\\c.nif"}],"models":{}})")),
                "map: a grey index used twice is refused");
            a_check.Check(refuses(write("dupcopy.json", R"({"version":1,"tint":0.6,"addons":[)" + entry + "," +
                                                            R"({"index":3,"grey":9103,"model":"meshes\\mps\\c.nif","greyModel":"meshes\\heartofmagic\\blackflame\\mps\\a.nif"}],"models":{}})")),
                "map: one greyModel written from two different MPS NIFs is refused");
            a_check.Check(!refuses(write("sharedmps.json", R"({"version":1,"tint":0.6,"addons":[)" + entry + "," +
                                                               R"({"index":3,"grey":9103,"model":"meshes\\mps\\a.nif","greyModel":"meshes\\heartofmagic\\blackflame\\mps\\a.nif"}],"models":{}})")),
                "map: two indices sharing one MPS and its grey copy load");
            a_check.Check(refuses(write("escape.json", R"({"version":1,"tint":0.6,"addons":[],"models":{"meshes\\..\\..\\x.nif":"meshes\\heartofmagic\\blackflame\\x.nif"}})")),
                "map: a path leaving the Data folder is refused");
            a_check.Check(refuses(write("outside.json", R"({"version":1,"tint":0.6,"addons":[],"models":{"meshes\\x.nif":"meshes\\x2.nif"}})")),
                "map: a grey path outside meshes\\heartofmagic\\blackflame is refused");
            a_check.Check(refuses(write("tint.json", R"({"version":1,"tint":5,"addons":[],"models":{}})")),
                "map: a tint outside 0..2 is refused");
        }
    }

    int RunSelfTest()
    {
        Checker check;
        std::error_code ec;
        const auto temp = std::filesystem::temp_directory_path(ec);
        if (ec) {
            std::cout << fmt::format("FAIL no temp folder: {}\nSELF-TEST pass=0 fail=1\n", ec.message());
            return 1;
        }
        const auto dir = temp / "blackflame-greybuild-selftest";
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            std::cout << fmt::format("FAIL cannot make '{}': {}\nSELF-TEST pass=0 fail=1\n", dir.string(),
                ec.message());
            return 1;
        }
        const auto source = dir / "source.nif";
        const auto grey = dir / "grey.nif";

        try {
            MakeSource(source);

            nifly::NifFile again;
            LoadNif(again, source);
            check.Check(SaveToBytes(again) == ReadBytes(source), "round trip: an unchanged save is byte-identical");

            nifly::NifFile nif;
            LoadNif(nif, source);
            const GreyMap map = TestMap(true);
            const GreyStats stats = GreyEffects(nif, map.tint);
            check.Check(stats.effectShaders == 2 && stats.paletteForced == 1 && stats.lightingShaders == 1 &&
                            stats.colorModifiers == 1 && stats.colorControllers == 1 && stats.controllerKeys == 2,
                fmt::format("grey stats (effects {}, forced {}, lighting {}, modifiers {}, controllers {}, keys {})",
                    stats.effectShaders, stats.paletteForced, stats.lightingShaders, stats.colorModifiers,
                    stats.colorControllers, stats.controllerKeys));
            const RemapStats remap = RemapAddonValues(nif, map);
            check.Check(remap.valueNodes == 2 && remap.remapped == 2 && remap.unmapped.empty(),
                fmt::format("remap stats (nodes {}, remapped {}, unmapped {})", remap.valueNodes, remap.remapped,
                    remap.unmapped.size()));
            SaveNif(nif, grey);

            nifly::NifFile written;
            LoadNif(written, grey);
            CheckOutput(check, written, map);

            nifly::NifFile partial;
            LoadNif(partial, source);
            const RemapStats missing = RemapAddonValues(partial, TestMap(false));
            check.Check(missing.unmapped.size() == 1 && missing.unmapped.front() == "AddOnNode24: 24",
                "a value with no map entry is reported (AddOnNode24: 24)");

            SelfTest::CheckPlanAndEsl(check, dir, source);
            SelfTest::CheckForms(check, dir, source);
            CheckMapRefusals(check, dir);
        } catch (const std::exception& e) {
            check.Check(false, fmt::format("exception: {}", e.what()));
        }

        std::filesystem::remove_all(dir, ec);
        std::cout << fmt::format("SELF-TEST pass={} fail={}\n", check.pass, check.fail);
        return check.fail == 0 && check.pass > 0 ? 0 : 1;
    }
}
