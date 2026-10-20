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
#include <process.h>
#include <fstream>
#include <iostream>
#include <memory>

#include "../../plugins/BlackFlame/src/CopyContractTest.h"

namespace GreyBuild
{
    namespace
    {
        constexpr float kTestTint = 0.6f;
        constexpr float kTestTintAdd = 0.85f;  // v2i: additive palette shaders (not the default, so the map's value is used)
        constexpr std::uint16_t kAlphaNormal = 0x12ED;    // blending on, SRC_ALPHA -> INV_SRC_ALPHA, alpha test
        constexpr std::uint16_t kAlphaAdditive = 0x000D;  // blending on, SRC_ALPHA -> ONE
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

        // v2i: a shape naming a_shader and an alpha property with a_alphaFlags (the blend picks the palette)
        void AddShape(nifly::NifFile& a_nif, std::uint32_t a_shader, std::uint16_t a_alphaFlags)
        {
            std::uint32_t alphaId = 0;
            auto* alpha = AddBlock(a_nif, std::make_unique<nifly::NiAlphaProperty>(), alphaId);
            alpha->flags = a_alphaFlags;
            std::uint32_t shapeId = 0;
            auto* shape = AddBlock(a_nif, std::make_unique<nifly::BSTriShape>(), shapeId);
            shape->ShaderPropertyRef()->index = a_shader;
            shape->AlphaPropertyRef()->index = alphaId;
            a_nif.GetRootNode()->childRefs.AddBlockRef(shapeId);
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

        // An effect colour controller on a_target (NIF_NPOS: none); its keys are kCtrlKey0 / kCtrlKey1
        void AddColorController(nifly::NifFile& a_nif, std::uint32_t a_target)
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
            ctrl->targetRef.index = a_target;
        }

        constexpr const char* kOrphanString = "HoMBF_OrphanHeaderString";
        constexpr const char* kDuplicateString = "HoMBF_DuplicateHeaderString";

        [[nodiscard]] int CountString(const nifly::NifFile& a_nif, const std::string& a_text)
        {
            int count = 0;
            for (std::uint32_t i = 0; i < a_nif.GetHeader().GetStringCount(); ++i) {
                count += a_nif.GetHeader().GetStringById(i) == a_text ? 1 : 0;
            }
            return count;
        }

        [[nodiscard]] std::uint32_t LightingNameIndex(nifly::NifFile& a_nif)
        {
            auto& hdr = a_nif.GetHeader();
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                if (auto* lit = hdr.GetBlock<nifly::BSLightingShaderProperty>(id)) {
                    return lit->name.GetIndex();
                }
            }
            return 0;
        }
        constexpr const char* kOnTexture = "textures\\effects\\fxfire02.dds";
        constexpr const char* kOrangePalette = "textures\\effects\\gradients\\gradflame01.dds";

        /// The made-up source: 2 AddOnNodes (9, 24), 4 effect shaders (palette off; on with a texture on a normal-blend shape;
        /// the same on an additive shape; on without a texture), two shapes with their alpha properties,
        /// a lighting shader, a simple colour modifier and two effect colour controllers (none / the additive shader).
        void MakeSource(const std::filesystem::path& a_file)
        {
            nifly::NifFile nif;
            nif.Create(nifly::NiVersion::getSSE());
            AddValueNode(nif, "AddOnNode9", kMappedA);
            AddValueNode(nif, "AddOnNode24", kMappedB);
            // I10: a node whose empty name has a real string index, before the lighting shader in block order: nifly's Save
            // then puts "" in the table and points the shader's no-string name at it (the case the fix undoes)
            std::uint32_t emptyNamedId = 0;
            auto* emptyNamed = AddBlock(nif, std::make_unique<nifly::NiNode>(), emptyNamedId);
            emptyNamed->name.SetIndex(nif.GetHeader().AddOrFindStringId("", true));
            nif.GetRootNode()->childRefs.AddBlockRef(emptyNamedId);

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
            on->sourceTexture.get() = kOnTexture;
            on->greyscaleTexture.get() = kOrangePalette;
            AddShape(nif, id, kAlphaNormal);
            // v2i: the same kind of palette shader on an additive shape: the bright-edge palette and tintAdd
            auto* add = AddBlock(nif, std::make_unique<nifly::BSEffectShaderProperty>(), id);
            add->name.get() = "FlameAdd";
            add->shaderFlags1 |= kSlsf1GreyscaleToPaletteColor;
            add->baseColor = kWarmBase2;
            add->sourceTexture.get() = kOnTexture;
            add->greyscaleTexture.get() = kOrangePalette;
            AddShape(nif, id, kAlphaAdditive);
            const std::uint32_t addId = id;
            // Y1: the flag on but no source texture: the shader samples the palette all the same (Effect.hlsl), so it takes
            // ours like any palette shader (no shape uses it: normal blend, I12)
            auto* glow = AddBlock(nif, std::make_unique<nifly::BSEffectShaderProperty>(), id);
            glow->name.get() = "GlowNoTexture";
            glow->shaderFlags1 |= kSlsf1GreyscaleToPaletteColor;
            glow->baseColor = kWarmBase;
            glow->greyscaleTexture.get() = kOrangePalette;

            auto* lit = AddBlock(nif, std::make_unique<nifly::BSLightingShaderProperty>(), id);
            lit->emissiveColor = kEmissive;

            auto* mod = AddBlock(nif, std::make_unique<nifly::BSPSysSimpleColorModifier>(), id);
            mod->color1 = kKey1;
            mod->color2 = kKey2;
            mod->color3 = kKey3;

            AddColorController(nif, nifly::NIF_NPOS);
            AddColorController(nif, addId);  // v2i: drives the additive palette shader: keys x tintAdd
            // v2i: a header string no block names (as Dragonfire's 'AutoPlay'): the copy must keep it, in its place
            (void)nif.GetHeader().AddOrFindStringId(kOrphanString);
            // I10: a string twice in the table (made through a placeholder slot) and an empty table string; the lighting
            // shader's empty name stays a reference to no string (NIF_NPOS) through the copy
            auto& hdr = nif.GetHeader();
            (void)hdr.AddOrFindStringId(kDuplicateString);
            hdr.SetStringById(hdr.AddOrFindStringId("HoMBF_DuplicateSlot"), kDuplicateString);
            (void)hdr.AddOrFindStringId("", true);
            SaveNif(nif, a_file);
        }

        [[nodiscard]] GreyMap TestMap(bool a_withB)
        {
            GreyMap map;
            map.tint = kTestTint;
            map.tintAdd = kTestTintAdd;
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
            int posData = 0;
            int interps = 0;
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                if (const auto* node = hdr.GetBlock<nifly::BSValueNode>(id)) {
                    ++values;
                    const bool a = node->name.get() == "AddOnNode9" && node->value == kGreyA;
                    const bool b = node->name.get() == "AddOnNode24" && node->value == kGreyB;
                    a_check.Check(a || b, fmt::format("value node '{}' = {} (name kept, value grey)",
                        node->name.get(), node->value));
                } else if (const auto* prop = hdr.GetBlock<nifly::BSEffectShaderProperty>(id)) {
                    ++effects;
                    const auto& name = prop->name.get();
                    const bool isOff = name == "FlameOff";
                    const bool isOn = name == "FlameOn";
                    const bool isAdd = name == "FlameAdd";
                    // FlameOn, FlameAdd and (Y1) GlowNoTexture - the flag on - take a palette by their shape's blend; flags and
                    // textures are kept
                    a_check.Check(((prop->shaderFlags1 & kSlsf1GreyscaleToPaletteColor) != 0) == !isOff,
                        fmt::format("'{}' palette colour flag as it was", name));
                    a_check.Check(((prop->shaderFlags1 & kSlsf1GreyscaleToPaletteAlpha) != 0) == isOn,
                        fmt::format("'{}' palette alpha flag as it was", name));
                    const std::string expectedGreyscale = isOff ? "" : (isAdd ? kPaletteAddPath : kPalettePath);
                    a_check.Check(prop->greyscaleTexture.get() == expectedGreyscale,
                        fmt::format("'{}' greyscale texture '{}' (expected '{}')", name, prop->greyscaleTexture.get(),
                            expectedGreyscale));
                    a_check.Check(IsGreyOf(prop->baseColor, isOn || isAdd ? kWarmBase2 : kWarmBase, isAdd ? kTestTintAdd : kTestTint),
                        fmt::format("'{}' base colour = luminance x {}, alpha kept", name, isAdd ? "tintAdd" : "tint"));
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
                    // block order kept: the first controller has no target (tint), the second the additive shader
                    const float scale = posData++ == 0 ? kTestTint : kTestTintAdd;
                    const bool ok = data->data.GetNumKeys() == 2 &&
                                    IsGreyOf(data->data.GetKey(0).value, kCtrlKey0, scale) &&
                                    IsGreyOf(data->data.GetKey(1).value, kCtrlKey1, scale);
                    a_check.Check(ok, fmt::format("colour controller keys = luminance x {}", scale));
                } else if (const auto* interp = hdr.GetBlock<nifly::NiPoint3Interpolator>(id)) {
                    const float scale = interps++ == 0 ? kTestTint : kTestTintAdd;
                    a_check.Check(IsGreyOf(interp->point3Value, kCtrlKey0, scale),
                        fmt::format("colour controller constant = luminance x {}", scale));
                }
            }
            a_check.Check(posData == 2 && interps == 2, fmt::format("two colour controllers ({} data, {} interpolators)", posData,
                interps));
            a_check.Check(values == 2 && effects == 4, fmt::format("block counts kept ({} values, {} effects)", values,
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

    void SelfTest::MakeSourceNif(const std::filesystem::path& a_file)
    {
        MakeSource(a_file);
    }

    int RunSelfTest(const SelfTestOptions& a_options)
    {
        // FMA3 is off for the whole process (main), so the golden table's maths give the same bits on every CPU
        Checker check;
        std::error_code ec;
        const auto temp = std::filesystem::temp_directory_path(ec);
        if (ec) {
            std::cout << fmt::format("FAIL no temp folder: {}\nSELF-TEST pass=0 fail=1\n", ec.message());
            return 1;
        }
        // one folder per process: a POST_BUILD run and a manual one (or another worktree's build) never share it
        const auto dir = temp / fmt::format("blackflame-greybuild-selftest-{}", _getpid());
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            std::cout << fmt::format("FAIL cannot make '{}': {}\nSELF-TEST pass=0 fail=1\n", dir.string(),
                ec.message());
            return 1;
        }
        const auto source = dir / "source.nif";
        const auto grey = dir / "grey.nif";
        const auto& golden = a_options.goldenDir;  // empty: the golden part is skipped (a tester package has no fixtures)
        if (a_options.printGolden) {
            if (golden.empty()) {
                std::cerr << "--print-golden needs --golden-dir <the repository's tools\\blackflame-greybuild\\golden>\n";
                std::filesystem::remove_all(dir, ec);
                return 1;
            }
            const int printed = SelfTest::PrintGolden(dir, golden);
            std::filesystem::remove_all(dir, ec);
            return printed;
        }

        try {
            MakeSource(source);

            nifly::NifFile again;
            LoadNif(again, source);
            check.Check(SaveToBytes(again) == ReadBytes(source), "round trip: an unchanged save is byte-identical");

            nifly::NifFile nif;
            LoadNif(nif, source);
            const GreyMap map = TestMap(true);
            std::vector<std::string> notes;
            const GreyStats stats = GreyEffects(nif, map.tint, map.tintAdd, &notes);
            check.Check(stats.effectShaders == 4 && stats.paletted == 2 && stats.palettedAdd == 1 && stats.paletteNoAlpha == 0 &&
                            stats.paletteLeft == 1 && stats.lightingShaders == 1 &&
                            stats.colorModifiers == 1 && stats.colorControllers == 2 && stats.controllerKeys == 4 && notes.empty(),
                fmt::format("grey stats (effects {}, paletted {}, additive {}, no alpha {}, left {}, lighting {}, modifiers {}, "
                    "controllers {}, keys {})", stats.effectShaders, stats.paletted, stats.palettedAdd, stats.paletteNoAlpha,
                    stats.paletteLeft, stats.lightingShaders, stats.colorModifiers,
                    stats.colorControllers, stats.controllerKeys));
            const RemapStats remap = RemapAddonValues(nif, map);
            check.Check(remap.valueNodes == 2 && remap.remapped == 2 && remap.unmapped.empty(),
                fmt::format("remap stats (nodes {}, remapped {}, unmapped {})", remap.valueNodes, remap.remapped,
                    remap.unmapped.size()));
            SaveNif(nif, grey);

            nifly::NifFile written;
            LoadNif(written, grey);
            CheckOutput(check, written, map);
            {
                nifly::NifFile original;
                LoadNif(original, source);
                const auto problems = CheckHeaderStrings(original, written);
                check.Check(problems.empty() && written.GetHeader().FindStringId(kOrphanString) != nifly::NIF_NPOS &&
                                written.GetHeader().FindStringId(kOrphanString) == original.GetHeader().FindStringId(kOrphanString),
                    fmt::format("header strings kept as the source has them, the orphan string included ({} source, {} copy{})",
                        original.GetHeader().GetStringCount(), written.GetHeader().GetStringCount(),
                        problems.empty() ? std::string() : "; " + problems.front()));
                check.Check(CountString(original, kDuplicateString) == 2 && CountString(written, kDuplicateString) == 2,
                    fmt::format("a string the table holds twice is kept twice (source {}, copy {})",
                        CountString(original, kDuplicateString), CountString(written, kDuplicateString)));
                check.Check(LightingNameIndex(original) == nifly::NIF_NPOS && LightingNameIndex(written) == nifly::NIF_NPOS,
                    fmt::format("an empty name with no string stays none beside an empty table string (source index {}, copy {})",
                        LightingNameIndex(original), LightingNameIndex(written)));
            }

            nifly::NifFile partial;
            LoadNif(partial, source);
            const RemapStats missing = RemapAddonValues(partial, TestMap(false));
            check.Check(missing.unmapped.size() == 1 && missing.unmapped.front() == "AddOnNode24: 24",
                "a value with no map entry is reported (AddOnNode24: 24)");

            SelfTest::CheckPlanAndEsl(check, dir, source);
            SelfTest::CheckForms(check, dir, source);
            SelfTest::CheckGameRequest(check, dir, source);
            SelfTest::CheckPathRule(check);
            {
                const int contract = BlackFlame::CopyContractTest::Run(
                    [&](bool a_ok, const std::string& a_what) { check.Check(a_ok, "contract: " + a_what); },
                    [](const std::string& a_what) { std::cout << "SKIP contract: " << a_what << "\n"; });
                check.Check(contract >= BlackFlame::CopyContractTest::kMinChecks,
                    fmt::format("contract: the shared table ran {} checks (at least {})", contract,
                        BlackFlame::CopyContractTest::kMinChecks));
            }
            SelfTest::CheckTextures(check, dir);
            SelfTest::CheckTexStats(check, dir);
            SelfTest::CheckRisk(check, dir);
            SelfTest::CheckDecals(check, dir);
            CheckMapRefusals(check, dir);
            SelfTest::CheckSlotsAndProgress(check, dir);
            SelfTest::CheckKeyed(check, dir, source);
            SelfTest::CheckHands(check, dir, source);
            SelfTest::CheckMarble(check, dir);
            SelfTest::CheckHandFx(check, dir);
            SelfTest::CheckEfsh(check, dir);
            SelfTest::CheckEfshBases(check, dir);
            SelfTest::CheckGolden(check, dir, golden, a_options.goldenRequired);
            SelfTest::CheckGpu(check, dir, golden);
        } catch (const std::exception& e) {
            check.Check(false, fmt::format("exception: {}", e.what()));
        }

        std::filesystem::remove_all(dir, ec);
        std::cout << fmt::format("SELF-TEST pass={} fail={}\n", check.pass, check.fail);
        return check.fail == 0 && check.pass > 0 ? 0 : 1;
    }
}
