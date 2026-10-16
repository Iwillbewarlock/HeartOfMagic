// =============================================================================
// GreySelfTestRisk - --self-test of the visual-risk check (GreyRisk.h) and the glow rules (GreyGlow.h)
// =============================================================================
// Made-up NIFs in memory (a shape, its effect shader, alpha property and colour controllers) and 4 x 4 textures handed
// to the texture source: each rule is seen to fire on the case it is for and to stay quiet on the normal one.

#include "GreySelfTest.h"
#include "GreyGlow.h"
#include "GreyPalette.h"
#include "GreyRisk.h"

#include <Animation.hpp>
#include <Geometry.hpp>
#include <NifFile.hpp>
#include <Shaders.hpp>

#include <cmath>
#include <fmt/format.h>
#include <memory>
#include <optional>
#include <string>

namespace GreyBuild::SelfTest
{
    namespace
    {
        constexpr std::uint16_t kAlphaBlend = 1;
        constexpr std::uint16_t kSrcAlpha = 6 << 1;          // source blend SRC_ALPHA (bits 1-4)
        constexpr std::uint16_t kDestOne = 0 << 5;           // destination ONE: additive
        constexpr std::uint16_t kDestInvSrcAlpha = 7 << 5;   // destination INV_SRC_ALPHA: normal
        constexpr std::size_t kTex = 4;                      // the made-up textures are 4 x 4
        const nifly::Color4 kOrange{ 1.0f, 0.5f, 0.0f, 1.0f };
        const nifly::Color4 kGreyColour{ 0.5f, 0.5f, 0.5f, 1.0f };
        const nifly::Vector3 kOrangeKey{ 1.0f, 0.4f, 0.0f };
        const nifly::Vector3 kGreyKey{ 0.4f, 0.4f, 0.4f };

        struct Spec
        {
            bool additive = true;
            nifly::Color4 base = kGreyColour;
            std::string texture;          // "" : none
            std::string palette;          // with the palette flag when not empty
            bool paletteAlpha = false;
            std::optional<nifly::Vector3> directKey;    // a colour controller's own NiPoint3Interpolator key
            std::optional<nifly::Vector3> sequenceKey;  // a key only a controlled block reaches (the controller blends)
            std::optional<nifly::Color4> vertexColour;  // every vertex, with the Vertex_Colors flag
            bool companion = false;  // a second, normal-blend grey shape (smoke) with its own shader and alpha
        };

        struct Made
        {
            std::uint32_t shader = 0;
        };

        template <class T>
        std::uint32_t Add(nifly::NifFile& a_nif, std::unique_ptr<T> a_block)
        {
            return a_nif.GetHeader().AddBlock(std::move(a_block));
        }

        Made Build(nifly::NifFile& a_nif, const Spec& a_spec)
        {
            a_nif.Create(nifly::NiVersion::getSSE());
            Made made;
            auto alpha = std::make_unique<nifly::NiAlphaProperty>();
            alpha->flags = static_cast<std::uint16_t>(kAlphaBlend | kSrcAlpha | (a_spec.additive ? kDestOne : kDestInvSrcAlpha));
            const auto alphaId = Add(a_nif, std::move(alpha));
            auto shader = std::make_unique<nifly::BSEffectShaderProperty>();
            shader->baseColor = a_spec.base;
            shader->sourceTexture.get() = a_spec.texture;
            shader->shaderFlags1 &= ~(kSlsf1GreyscaleToPaletteColor | kSlsf1GreyscaleToPaletteAlpha);
            if (!a_spec.palette.empty()) {
                shader->shaderFlags1 |= kSlsf1GreyscaleToPaletteColor | (a_spec.paletteAlpha ? kSlsf1GreyscaleToPaletteAlpha : 0u);
                shader->greyscaleTexture.get() = a_spec.palette;
            }
            if (a_spec.vertexColour) {
                shader->shaderFlags2 |= kSlsf2VertexColors;
            }
            made.shader = Add(a_nif, std::move(shader));
            auto data = std::make_unique<nifly::NiTriShapeData>();
            data->vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
            if (a_spec.vertexColour) {
                data->SetVertexColors(true);
                data->vertexColors.assign(data->vertices.size(), *a_spec.vertexColour);
            }
            const auto dataId = Add(a_nif, std::move(data));
            auto shape = std::make_unique<nifly::NiTriShape>();
            shape->name.get() = "Glow";
            shape->DataRef()->index = dataId;
            shape->ShaderPropertyRef()->index = made.shader;
            shape->AlphaPropertyRef()->index = alphaId;
            Add(a_nif, std::move(shape));
            if (a_spec.companion) {
                auto smokeAlpha = std::make_unique<nifly::NiAlphaProperty>();
                smokeAlpha->flags = static_cast<std::uint16_t>(kAlphaBlend | kSrcAlpha | kDestInvSrcAlpha);
                const auto smokeAlphaId = Add(a_nif, std::move(smokeAlpha));
                auto smokeShader = std::make_unique<nifly::BSEffectShaderProperty>();
                smokeShader->baseColor = kGreyColour;
                smokeShader->shaderFlags1 &= ~(kSlsf1GreyscaleToPaletteColor | kSlsf1GreyscaleToPaletteAlpha);
                const auto smokeShaderId = Add(a_nif, std::move(smokeShader));
                auto smokeData = std::make_unique<nifly::NiTriShapeData>();
                smokeData->vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
                const auto smokeDataId = Add(a_nif, std::move(smokeData));
                auto smoke = std::make_unique<nifly::NiTriShape>();
                smoke->name.get() = "Smoke";
                smoke->DataRef()->index = smokeDataId;
                smoke->ShaderPropertyRef()->index = smokeShaderId;
                smoke->AlphaPropertyRef()->index = smokeAlphaId;
                Add(a_nif, std::move(smoke));
            }
            const auto keyed = [&](const nifly::Vector3& a_key) {
                auto keys = std::make_unique<nifly::NiPosData>();
                keys->data.SetInterpolationType(nifly::NiKeyType::LINEAR_KEY);
                nifly::NiAnimationKey<nifly::Vector3> key;
                key.value = a_key;
                keys->data.AddKey(key);
                const auto keysId = Add(a_nif, std::move(keys));
                auto interp = std::make_unique<nifly::NiPoint3Interpolator>();
                interp->point3Value = a_key;
                interp->dataRef.index = keysId;
                return Add(a_nif, std::move(interp));
            };
            if (a_spec.directKey || a_spec.sequenceKey) {
                auto ctrl = std::make_unique<nifly::BSEffectShaderPropertyColorController>();
                ctrl->targetRef.index = made.shader;
                if (a_spec.directKey) {
                    ctrl->interpolatorRef.index = keyed(*a_spec.directKey);
                } else {
                    auto blend = std::make_unique<nifly::NiBlendPoint3Interpolator>();
                    blend->point = kOrangeKey;
                    ctrl->interpolatorRef.index = Add(a_nif, std::move(blend));
                }
                const auto ctrlId = Add(a_nif, std::move(ctrl));
                if (a_spec.sequenceKey) {
                    auto sequence = std::make_unique<nifly::NiControllerSequence>();
                    nifly::ControllerLink link;
                    link.interpolatorRef.index = keyed(*a_spec.sequenceKey);
                    link.controllerRef.index = ctrlId;
                    sequence->controlledBlocks.push_back(link);
                    Add(a_nif, std::move(sequence));
                }
            }
            return made;
        }

        // A source and its copy as the builder makes it (GreyEffects, then the glow rules when asked)
        struct Pair
        {
            nifly::NifFile source;
            nifly::NifFile copy;
        };

        void MakePair(Pair& a_pair, const Spec& a_spec, GlowContext* a_glow)
        {
            Build(a_pair.source, a_spec);
            Build(a_pair.copy, a_spec);
            (void)GreyEffects(a_pair.copy, kDefaultTint, kDefaultTintAdd);
            if (a_glow) {
                ApplyGlowRules(a_pair.copy, a_pair.source, *a_glow, "meshes\\selftest.nif");
            }
        }

        [[nodiscard]] int Count(const RiskJob& a_job, const char* a_rule)
        {
            const auto it = a_job.perRule.find(a_rule);
            return it == a_job.perRule.end() ? 0 : it->second;
        }

        [[nodiscard]] RiskJob Judge(const Spec& a_spec, TextureSource& a_textures, GlowContext* a_glow)
        {
            if (a_glow) {
                a_glow->haloKept.clear();  // one made-up NIF name for every case
            }
            Pair pair;
            MakePair(pair, a_spec, a_glow);
            RiskJob job;
            job.textures = &a_textures;
            RiskCheckNif(pair.source, pair.copy, "meshes\\selftest.nif", job,
                a_glow && a_glow->haloKept.count("meshes\\selftest.nif") != 0);  // Y7: the glow step says whether it kept G2
            return job;
        }

        // Every other texel lit: high detail (sparks)
        [[nodiscard]] RgbaImage Sparks()
        {
            RgbaImage image;
            image.width = kTex;
            image.height = kTex;
            for (std::size_t y = 0; y < kTex; ++y) {
                for (std::size_t x = 0; x < kTex; ++x) {
                    const std::uint8_t on = (x + y) % 2 == 0 ? 255 : 0;
                    image.pixels.insert(image.pixels.end(), { on, on, on, on });
                }
            }
            return image;
        }

        [[nodiscard]] RgbaImage Image(std::uint8_t a_alpha, bool a_greenRamp)
        {
            RgbaImage image;
            image.width = kTex;
            image.height = kTex;
            for (std::size_t y = 0; y < kTex; ++y) {
                for (std::size_t x = 0; x < kTex; ++x) {
                    const auto ramp = static_cast<std::uint8_t>(a_greenRamp ? 64 * x + 32 : 200);
                    image.pixels.insert(image.pixels.end(), { 200, ramp, 200, a_alpha });
                }
            }
            return image;
        }
    }

    namespace
    {
        // P1, P2 and the fallback on a 4 x 4 source palette whose alpha runs 0..255 across it
        void CheckPalettes(Checker& a_check, const std::filesystem::path& a_dir)
        {
            TextureSource textures({}, {});
            RgbaImage source;
            source.width = kTex;
            source.height = kTex;
            for (std::size_t y = 0; y < kTex; ++y) {
                for (std::size_t x = 0; x < kTex; ++x) {
                    const auto alpha = static_cast<std::uint8_t>(85 * x);
                    source.pixels.insert(source.pixels.end(), { 255, static_cast<std::uint8_t>(60 * y), 0, alpha });
                }
            }
            RgbaImage ours;
            ours.width = 2;
            ours.height = 1;
            ours.pixels = { 10, 10, 10, 255, 40, 40, 40, 255 };
            textures.Put("textures\\selftest\\palette.dds", source);
            textures.Put("textures\\heartofmagic\\blackflame\\gradblackflame.dds", ours);
            textures.Put("textures\\selftest\\unreadable.dds", std::nullopt);
            PaletteJob job;
            job.out = a_dir / "palettes";
            job.textures = &textures;
            std::string why;
            const auto add = DerivedPalette(job, "Textures/SelfTest/Palette.dds", true, why);
            const auto normal = DerivedPalette(job, "textures\\selftest\\palette.dds", false, why);
            const auto again = DerivedPalette(job, "textures\\selftest\\palette.dds", true, why);
            std::string readWhy;
            const auto addBack = add.empty() ? std::nullopt : ReadRgba(job.out / std::filesystem::path(add), readWhy);
            const auto normalBack = normal.empty() ? std::nullopt : ReadRgba(job.out / std::filesystem::path(normal), readWhy);
            bool p2 = addBack && addBack->width == kTex && addBack->height == kTex;
            bool p1 = normalBack && normalBack->width == kTex && normalBack->height == kTex;
            for (std::size_t i = 0; p2 && p1 && i < source.pixels.size(); i += 4) {
                const auto* s = source.pixels.data() + i;
                const auto* a = addBack->pixels.data() + i;
                const auto* n = normalBack->pixels.data() + i;
                const auto grey = static_cast<int>(std::lround(Luminance(s[0] / 255.0f, s[1] / 255.0f, s[2] / 255.0f) * 255.0f));
                p2 = a[0] == grey && a[1] == grey && a[2] == grey && a[3] == s[3];
                const std::size_t x = (i / 4) % kTex;
                const std::uint8_t expected = x < 2 ? 10 : 40;  // our 2 texels resampled across 4
                p1 = n[0] == expected && n[3] == s[3];
            }
            a_check.Check(add == "textures\\heartofmagic\\blackflame\\copies\\palettes\\add\\selftest\\palette.dds" && p2 &&
                              again == add && job.totals.derivedAdd == 1,
                fmt::format("palettes P2: the source palette at its luminance, every alpha exactly kept, made once ({}; {})", add,
                    readWhy.empty() ? "read back" : readWhy));
            a_check.Check(normal == "textures\\heartofmagic\\blackflame\\copies\\palettes\\normal\\selftest\\palette.dds" && p1,
                fmt::format("palettes P1: our palette's colour resampled to the source's size, the source's alpha ({})", normal));
            const auto fallback = DerivedPalette(job, "textures\\selftest\\unreadable.dds", true, why);
            a_check.Check(fallback.empty() && job.totals.fallback == 1 && !why.empty(),
                fmt::format("palettes: a source palette that does not decode keeps today's palette ({})", why));
        }
    }

    namespace
    {
        // Y1, Y2 end to end: the glow rules derive the palette and scale it, then the check reads the copy's own values
        void CheckDerivedEndToEnd(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto out = a_dir / "derived";
            TextureSource textures({ out }, {}, out);
            RgbaImage ramp;
            ramp.width = kTex;
            ramp.height = kTex;
            for (std::size_t y = 0; y < kTex; ++y) {
                for (std::size_t x = 0; x < kTex; ++x) {
                    ramp.pixels.insert(ramp.pixels.end(), { 200, static_cast<std::uint8_t>(60 * x + 20), 200, 255 });
                }
            }
            RgbaImage palette;  // orange, alpha rising across: the source palette carves the shape
            palette.width = kTex;
            palette.height = 1;
            for (std::size_t x = 0; x < kTex; ++x) {
                palette.pixels.insert(palette.pixels.end(), { 255, 120, 0, static_cast<std::uint8_t>(80 * x) });
            }
            textures.Put("textures\\selftest\\e2e_tex.dds", ramp);
            textures.Put("textures\\selftest\\e2e_pal.dds", palette);
            const auto run = [&](const Spec& a_spec, float a_tintAdd, const char* a_forceGreyscale) {
                PaletteJob palettes;
                palettes.out = out;
                palettes.textures = &textures;
                GlowContext glow;
                glow.textures = &textures;
                glow.palettes = &palettes;
                glow.tintAdd = a_tintAdd;
                Pair pair;
                MakePair(pair, a_spec, &glow);
                if (a_forceGreyscale) {
                    auto& hdr = pair.copy.GetHeader();
                    for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                        if (auto* shader = hdr.GetBlock<nifly::BSEffectShaderProperty>(id)) {
                            shader->greyscaleTexture.get() = a_forceGreyscale;
                        }
                    }
                }
                RiskJob job;
                job.textures = &textures;
                RiskCheckNif(pair.source, pair.copy, "meshes\\selftest_e2e.nif", job);
                return job;
            };
            Spec palShape;
            palShape.texture = "textures\\selftest\\e2e_tex.dds";
            palShape.palette = "textures\\selftest\\e2e_pal.dds";
            palShape.paletteAlpha = true;
            palShape.base = nifly::Color4{ 1.0f, 0.5f, 0.0f, 1.0f };
            palShape.companion = true;
            const auto good = run(palShape, kDefaultTintAdd, nullptr);
            a_check.Check(RiskGatePasses(good) && Count(good, "PAL_SHEET") == 0 && Count(good, "PAL_VANISH") == 0,
                fmt::format("Y2: glow rules -> derived palette -> the check, from the copy's own values, passes ({})", RiskGateLine(good)));
            const auto dark = run(palShape, 0.0f, nullptr);
            a_check.Check(!RiskGatePasses(dark), fmt::format("Y2: --tint-add 0 really darkens a derived additive palette (emissive "
                "multiple 0) and the check fails it ({})", RiskGateLine(dark)));
            Spec flagOnly = palShape;
            flagOnly.texture.clear();
            const auto flag = run(flagOnly, kDefaultTintAdd, nullptr);
            a_check.Check(Count(flag, "PAL_FOREIGN") == 0 && RiskGatePasses(flag),
                fmt::format("Y1: a palette flag without a source texture takes a derived palette (PAL_FOREIGN {}; {})",
                    Count(flag, "PAL_FOREIGN"), RiskGateLine(flag)));
            const auto foreign = run(flagOnly, kDefaultTintAdd, "textures\\selftest\\e2e_pal.dds");
            a_check.Check(Count(foreign, "PAL_FOREIGN") == 1, fmt::format("Y1: a copy that keeps the source's palette is "
                "PAL_FOREIGN ({})", Count(foreign, "PAL_FOREIGN")));
        }
    }

    void CheckRisk(Checker& a_check, const std::filesystem::path& a_dir)
    {
        CheckPalettes(a_check, a_dir);
        CheckDerivedEndToEnd(a_check, a_dir);
        TextureSource textures({}, {});
        GlowContext glow;
        glow.textures = &textures;

        Spec normal;
        normal.directKey = kGreyKey;
        const auto normalJob = Judge(normal, textures, nullptr);
        a_check.Check(RiskCount(normalJob, RiskLevel::kFail) == 0 && RiskCount(normalJob, RiskLevel::kUnread) == 0,
            fmt::format("risk: a grey shape with grey keys only passes ({})", RiskGateLine(normalJob)));

        Spec sequence;
        sequence.sequenceKey = kOrangeKey;
        const auto before = Judge(sequence, textures, nullptr);
        const auto after = Judge(sequence, textures, &glow);
        a_check.Check(Count(before, "KEYS_LEFT") == 1 && Count(after, "KEYS_LEFT") == 0 && glow.totals.sequenceKeys == 1 &&
                          glow.totals.blendValues == 1,
            fmt::format("risk + K: an orange key reached only through a controlled block is KEYS_LEFT after GreyEffects ({}) "
                        "and greyed by the glow rules ({} left; {} sequence key, {} blend value greyed)",
                Count(before, "KEYS_LEFT"), Count(after, "KEYS_LEFT"), glow.totals.sequenceKeys, glow.totals.blendValues));

        Spec halo;
        halo.base = kOrange;
        halo.companion = true;
        const auto haloJob = Judge(halo, textures, nullptr);
        const int zeroedBefore = glow.totals.g1;
        const auto haloAfter = Judge(halo, textures, &glow);
        a_check.Check(Count(haloJob, "GREY_HALO") == 1 && Count(haloAfter, "GREY_HALO") == 0 && Count(haloAfter, "EFFECT_EMPTY") == 0 &&
                          glow.totals.g1 == zeroedBefore + 1,
            fmt::format("risk + G1: a textureless additive orange glow is GREY_HALO after GreyEffects ({}) and RGB 0 after the glow "
                        "rules ({} left), the smoke beside it still drawn", Count(haloJob, "GREY_HALO"), Count(haloAfter, "GREY_HALO")));
        Spec alone = halo;
        alone.companion = false;
        const int keptBefore = glow.totals.g1Kept;
        const auto aloneAfter = Judge(alone, textures, &glow);
        a_check.Check(Count(aloneAfter, "EFFECT_EMPTY") == 0 && glow.totals.g1Kept == keptBefore + 1 &&
                          Count(aloneAfter, "HALO_KEPT") == 1 && Count(aloneAfter, "GREY_HALO") == 0,
            fmt::format("G1 exception: a NIF whose only drawn shape is a soft warm glow keeps it at G2, listed as HALO_KEPT "
                        "(EFFECT_EMPTY {}, kept {}, HALO_KEPT {}, GREY_HALO {})", Count(aloneAfter, "EFFECT_EMPTY"),
                glow.totals.g1Kept - keptBefore, Count(aloneAfter, "HALO_KEPT"), Count(aloneAfter, "GREY_HALO")));
        // Y7: the same lone glow judged without the glow step's word is a GREY_HALO failure, not a HALO_KEPT
        const auto aloneUntold = Judge(alone, textures, nullptr);
        a_check.Check(Count(aloneUntold, "GREY_HALO") == 1 && Count(aloneUntold, "HALO_KEPT") == 0,
            fmt::format("Y7: HALO_KEPT only when the glow step kept the NIF (without it: GREY_HALO {}, HALO_KEPT {})",
                Count(aloneUntold, "GREY_HALO"), Count(aloneUntold, "HALO_KEPT")));


        Spec vertex;
        vertex.vertexColour = nifly::Color4{ 1.0f, 0.3f, 0.0f, 1.0f };
        const auto vertexBefore = Judge(vertex, textures, nullptr);
        const auto vertexAfter = Judge(vertex, textures, &glow);
        a_check.Check(Count(vertexBefore, "VC_LEFT") == 1 && Count(vertexAfter, "VC_LEFT") == 0,
            fmt::format("risk + V: orange vertex colours are VC_LEFT after GreyEffects ({}) and greyed by the glow rules ({} left)",
                Count(vertexBefore, "VC_LEFT"), Count(vertexAfter, "VC_LEFT")));

        // Palette shapes: the source palette's alpha is 0, the copy's 1 - the shape becomes a sheet
        textures.Put("textures\\selftest\\ramp.dds", Image(255, true));
        textures.Put("textures\\selftest\\clear.dds", Image(0, false));
        textures.Put("textures\\selftest\\solid.dds", Image(255, false));
        textures.Put("textures\\selftest\\broken.dds", std::nullopt);
        textures.Put("textures\\selftest\\sparks.dds", Sparks());
        Spec flames = halo;
        flames.texture = "textures\\selftest\\sparks.dds";
        const int g2Before = glow.totals.g2;
        const auto flamesAfter = Judge(flames, textures, &glow);
        a_check.Check(glow.totals.g2 == g2Before + 1 && Count(flamesAfter, "GREY_HALO") == 0,
            fmt::format("G2: an additive orange shape with a detailed texture goes to luminance x tintAdd ({} shader), no GREY_HALO",
                glow.totals.g2 - g2Before));
        Spec sheet;
        sheet.texture = "textures\\selftest\\ramp.dds";
        sheet.palette = "textures\\selftest\\clear.dds";
        sheet.paletteAlpha = true;
        Pair sheetPair;
        MakePair(sheetPair, sheet, nullptr);
        auto& sheetHdr = sheetPair.copy.GetHeader();
        for (std::uint32_t id = 0; id < sheetHdr.GetNumBlocks(); ++id) {
            if (auto* copyShader = sheetHdr.GetBlock<nifly::BSEffectShaderProperty>(id)) {
                copyShader->greyscaleTexture.get() = "textures\\selftest\\solid.dds";  // a palette that is opaque everywhere
            }
        }
        RiskJob sheetJob;
        sheetJob.textures = &textures;
        RiskCheckNif(sheetPair.source, sheetPair.copy, "meshes\\selftest.nif", sheetJob);
        a_check.Check(Count(sheetJob, "PAL_SHEET") == 1, fmt::format("risk: a palette shape whose copy palette lifts alpha 0 to 1 "
            "is PAL_SHEET ({})", Count(sheetJob, "PAL_SHEET")));

        // X6: the copy side is judged with the texture the copy names (a grey texture copy passes, the source left fails)
        RgbaImage orange = Image(255, false);
        for (std::size_t i = 0; i < orange.pixels.size(); i += 4) {
            orange.pixels[i + 1] = 60;
            orange.pixels[i + 2] = 0;
        }
        textures.Put("textures\\selftest\\orange.dds", orange);
        textures.Put("textures\\selftest\\greycopy.dds", Image(255, false));
        Spec painted;
        painted.additive = false;
        painted.texture = "textures\\selftest\\orange.dds";
        const auto retarget = [&](const char* a_texture, const char* a_name) {
            Pair pair;
            MakePair(pair, painted, nullptr);
            auto& hdr = pair.copy.GetHeader();
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                if (auto* shader = hdr.GetBlock<nifly::BSEffectShaderProperty>(id); shader && a_texture) {
                    shader->sourceTexture.get() = a_texture;
                }
                if (auto* shape = hdr.GetBlock<nifly::NiTriShape>(id); shape && a_name) {
                    shape->name.get() = a_name;
                }
            }
            RiskJob job;
            job.textures = &textures;
            RiskCheckNif(pair.source, pair.copy, "meshes\\selftest.nif", job);
            return job;
        };
        const auto left = retarget(nullptr, nullptr);
        const auto greyed = retarget("textures\\selftest\\greycopy.dds", nullptr);
        a_check.Check(Count(left, "TEX_COLOUR") == 1 && Count(greyed, "TEX_COLOUR") == 0,
            fmt::format("risk (X6): a coloured texture the copy still names is TEX_COLOUR ({}), its grey copy passes ({})",
                Count(left, "TEX_COLOUR"), Count(greyed, "TEX_COLOUR")));
        const auto renamed = retarget(nullptr, "NotTheSource");
        a_check.Check(Count(renamed, "BLOCK_LAYOUT") == 1, fmt::format("risk (X8): a copy whose shape at the same block has another "
            "name is BLOCK_LAYOUT ({})", Count(renamed, "BLOCK_LAYOUT")));

        Spec unread = sheet;
        unread.palette = "textures\\selftest\\broken.dds";
        const auto unreadJob = Judge(unread, textures, nullptr);
        a_check.Check(Count(unreadJob, "UNREAD") == 1 && !RiskGatePasses(unreadJob),
            fmt::format("risk: a palette that does not decode is UNREAD and fails the gate ({})", RiskGateLine(unreadJob)));
    }
}
