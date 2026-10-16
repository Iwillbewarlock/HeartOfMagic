// =============================================================================
// GreySelfTestRisk - --self-test of the visual-risk check (GreyRisk.h) and the glow rules (GreyGlow.h)
// =============================================================================
// Made-up NIFs in memory (a shape, its effect shader, alpha property and colour controllers) and 4 x 4 textures handed
// to the texture source: each rule is seen to fire on the case it is for and to stay quiet on the normal one.

#include "GreySelfTest.h"
#include "GreyGlow.h"
#include "GreyRisk.h"

#include <Animation.hpp>
#include <Geometry.hpp>
#include <NifFile.hpp>
#include <Shaders.hpp>

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
            Pair pair;
            MakePair(pair, a_spec, a_glow);
            RiskJob job;
            job.textures = &a_textures;
            RiskCheckNif(pair.source, pair.copy, "meshes\\selftest.nif", job);
            return job;
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

    void CheckRisk(Checker& a_check)
    {
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
        const auto haloJob = Judge(halo, textures, nullptr);
        a_check.Check(Count(haloJob, "GREY_HALO") == 1, fmt::format("risk: a textureless additive orange glow greyed by GreyEffects "
            "is GREY_HALO ({})", Count(haloJob, "GREY_HALO")));

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

        Spec unread = sheet;
        unread.palette = "textures\\selftest\\broken.dds";
        const auto unreadJob = Judge(unread, textures, nullptr);
        a_check.Check(Count(unreadJob, "UNREAD") == 1 && !RiskGatePasses(unreadJob),
            fmt::format("risk: a palette that does not decode is UNREAD and fails the gate ({})", RiskGateLine(unreadJob)));
    }
}
