// Casting art through the real request reader, scheduler, NIF writer and keyed palette cache.
#include "GreySelfTestKeyed.h"
#include "GreyPalette.h"
#include "GreyShapes.h"

#include <NifFile.hpp>
#include <Shaders.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace GreyBuild::SelfTest
{
    namespace
    {
        namespace CC = BlackFlame::CopyContract;
        namespace K = Keyed;

        // Independent oracle for the documented smoothstep ramp, Rec. 709 luminance and sRGB transfer.
        std::uint8_t ExpectedRampPixel(const RgbaImage& a_source, std::size_t a_x, std::size_t a_y, double a_tint)
        {
            const auto luminance = [&](std::size_t a_col) {
                auto p = a_source.At(a_col, a_y);
                if (a_source.srgb) {
                    for (int c = 0; c < 3; ++c) {
                        p[c] = p[c] <= 0.04045 ? p[c] / 12.92 : std::pow((p[c] + 0.055) / 1.055, 2.4);
                    }
                }
                return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
            };
            double peak = 0.0;
            for (std::size_t x = 0; x < a_source.width; ++x) peak = std::max(peak, luminance(x));
            const double u = a_source.width > 1 ? double(a_x) / double(a_source.width - 1) : 1.0;
            const double t = std::clamp((u - 0.30) / 0.40, 0.0, 1.0);
            double grey = peak > 0.0 ? std::clamp(a_tint * t * t * (3.0 - 2.0 * t) * luminance(a_x) / peak, 0.0, 1.0) : 0.0;
            if (a_source.srgb) grey = grey <= 0.0031308 ? 12.92 * grey : 1.055 * std::pow(grey, 1.0 / 2.4) - 0.055;
            return static_cast<std::uint8_t>(std::lround(grey * 255.0));
        }

        void CheckInvalidTint(Checker& a_check)
        {
            ProgressLog log;
            Budget budget(kBytesPerMiB * 1024, 0, nullptr, nullptr);
            TextureJob textures;
            KeyedOutputs outputs(log, budget, CC::KeyPrefix{}, textures, {}, {});
            for (const double tint : { std::numeric_limits<double>::quiet_NaN(), -0.1, 2.1 }) {
                KeyedObject object;
                outputs.Begin(&object, tint);
                bool called = false, rejected = false;
                std::string why;
                try {
                    const auto slot = outputs.Palette(TextureLook{}, true,
                        [&](RgbaImage&, std::string&) { called = true; return KeyedOutputs::Made::kMade; },
                        [&](const RgbaImage&, const std::filesystem::path&, std::string&) { called = true; return true; }, why);
                    rejected = slot.empty() && outputs.ObjectFailed() && !why.empty() && object.failed == why;
                } catch (...) {
                    rejected = false;
                }
                a_check.Check(rejected && !called,
                    "hands palette: invalid tint follows the object failure path without throwing or touching pixels/files");
            }
            outputs.Begin(nullptr);
        }

        void CheckRamp(Checker& a_check)
        {
            // Two distinct row profiles and a black row, including nontrivial alpha.
            RgbaImage image{ 17, 3, std::vector<std::uint8_t>(17 * 3 * 4), false };
            for (std::size_t y = 0; y < image.height; ++y) {
                for (std::size_t x = 0; x < image.width; ++x) {
                    auto* p = image.pixels.data() + (y * image.width + x) * 4;
                    p[0] = y == 2 ? 0 : static_cast<std::uint8_t>(x * (y == 0 ? 15 : 8));
                    p[1] = y == 2 ? 0 : static_cast<std::uint8_t>(y == 0 ? x * 4 : x * x / 2);
                    p[2] = 0;
                    p[3] = static_cast<std::uint8_t>((x * 13 + y * 31) % 256);
                }
            }
            const auto source = image;
            MakeHandPalette(image, 1.4f);
            bool alpha = true, grey = true, dark = true, clipped = true, rows = false;
            for (std::size_t y = 0; y < image.height; ++y) {
                for (std::size_t x = 0; x < image.width; ++x) {
                    const auto p = image.At(x, y);
                    alpha &= p[3] == source.At(x, y)[3];
                    grey &= p[0] == p[1] && p[1] == p[2];
                    if (x <= 4 || y == 2) dark &= p[0] == 0.0;
                    if (y < 2 && x == 16) clipped &= p[0] == 1.0;
                    rows |= image.At(x, 0)[0] != image.At(x, 1)[0];
                }
            }
            a_check.Check(alpha && grey && dark && clipped && rows && image.width == 17 && image.height == 3,
                "hands ramp: dark low indices, white clipped core, distinct rows, black row and all alpha bytes preserved");
            auto zero = source;
            MakeHandPalette(zero, 0.0f);
            bool black = true;
            for (std::size_t i = 0; i < zero.pixels.size(); i += 4) black &= zero.pixels[i] == 0;
            a_check.Check(black, "hands ramp: tint zero is black");
            auto srgb = source;
            srgb.srgb = true;
            MakeHandPalette(srgb, 2.0f);
            a_check.Check(srgb.srgb && srgb.At(16, 0)[0] == 1.0 && srgb.At(0, 0)[0] == 0.0 &&
                srgb.At(8, 0)[3] == source.At(8, 0)[3], "hands ramp: sRGB encoding and upper clamp preserve alpha");
            auto srgbSource = source;
            srgbSource.srgb = true;
            const auto srgbMid = ExpectedRampPixel(srgbSource, 8, 0, 2.0);
            const auto linearMid = ExpectedRampPixel(source, 8, 0, 2.0);
            auto linear = source;
            MakeHandPalette(linear, 2.0f);
            a_check.Check(std::abs(srgb.At(8, 0)[0] * 255.0 - srgbMid) <= 1.0 &&
                std::abs(linear.At(8, 0)[0] * 255.0 - linearMid) <= 1.0 && srgbMid > linearMid,
                "hands ramp: mid-ramp sRGB matches decoded-luminance/encoded output and differs from linear bytes");
        }

        std::uint64_t AddHands(nlohmann::json& a_request, const K::Fixture& a_fixture, double a_tint)
        {
            CC::KeyPrefix prefix;
            prefix.outputIdentity = OutputIdentity();
            prefix.tint = 0.6;
            prefix.tintAdd = 0.9;
            prefix.gradBlackFlame = CC::FileHash::Hashed(K::Fnv(a_fixture.dump / kPalettePath));
            prefix.gradBlackFlameAdd = CC::FileHash::Hashed(K::Fnv(a_fixture.dump / kPaletteAddPath));
            CC::NifKeyInput input;
            input.path = "meshes\\magic\\keyed.nif";
            input.nif = CC::FileHash::Hashed(K::Fnv(a_fixture.dump / input.path));
            input.tintAddHands = a_tint;
            const auto text = CC::NifKeyString(prefix, input).value();
            auto hand = a_request["forms"][0];
            hand["source"] = "Test.esp|000801";
            hand["type"] = "ARTO";
            hand["data"] = { { "artType", 0 } };
            hand["role"] = "castingArt";
            hand["tintAddHands"] = a_tint;
            hand["handMarbleOpacity"] = 0.9;
            hand["handMarbleScale"] = 1.15;
            hand["keyString"] = text;
            hand["key"] = CC::Hex16(CC::KeyOf(text));
            hand["slot"] = 0xD00;
            a_request["forms"].insert(a_request["forms"].begin(), hand);
            return CC::KeyOf(text);
        }
    }

    void CheckHands(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif)
    {
        CheckRamp(a_check);
        CheckInvalidTint(a_check);
        const auto sourceFile = a_dir / "hands-source.nif";
        nifly::NifFile fixture;
        LoadNif(fixture, a_sourceNif);
        auto& hdr = fixture.GetHeader();
        for (std::uint32_t i = 0; i < hdr.GetNumBlocks(); ++i) {
            auto* shader = hdr.GetBlock<nifly::BSEffectShaderProperty>(i);
            if (!shader || shader->name.get() != "FlameOff") continue;
            shader->sourceTexture.get().clear();  // A soft warm G1 halo, with palette flames still drawing beside it.
            auto alpha = std::make_unique<nifly::NiAlphaProperty>();
            alpha->flags = 13;
            const auto alphaId = hdr.AddBlock(std::move(alpha));
            auto shape = std::make_unique<nifly::BSTriShape>();
            shape->ShaderPropertyRef()->index = i;
            shape->AlphaPropertyRef()->index = alphaId;
            const auto shapeId = hdr.AddBlock(std::move(shape));
            fixture.GetRootNode()->childRefs.AddBlockRef(shapeId);
            break;
        }
        SaveNif(fixture, sourceFile);
        const auto baseline = K::MakeFixture(a_dir, "hands-baseline", sourceFile, K::Variant::kNormal);
        a_check.Check(K::Run(baseline) == 0, "hands control: original object fixture builds");
        const auto before = K::Progress(baseline);
        auto mixed = baseline;
        mixed.staging = baseline.folder / "mixed";
        auto request = nlohmann::json::parse(K::ReadAll(baseline.folder / "request.json"));
        const auto handKey = AddHands(request, baseline, 1.4);
        K::Write(baseline.folder / "request.json", request.dump());
        a_check.Check(K::Run(mixed) == 0, "hands request: casting art and ordinary objects build together");
        const auto after = K::Progress(mixed);
        const auto* hand = K::Find(after, handKey);
        bool unchanged = true;
        for (const auto& line : before.lines) {
            if (!CC::IsPublishable(line.outcome)) continue;
            const auto* now = K::Find(after, line.key);
            unchanged &= now && now->path == line.path && now->fnv64 == line.fnv64 && now->bytes == line.bytes;
            if (now && !line.path.empty()) {
                unchanged &= K::ReadAll(StagedFile(baseline.staging, line.path)) == K::ReadAll(StagedFile(mixed.staging, now->path));
            }
        }
        a_check.Check(unchanged, "hands isolation: every ordinary NIF, MPS, muzzle, TXST and DDS remains byte-identical");
        a_check.Check(hand && CC::IsPublishable(hand->outcome) && !hand->path.empty(), "hands request: casting object has a published NIF");
        if (!hand || hand->path.empty()) return;
        const auto* ordinary = K::Find(before, K::kModel);
        a_check.Check(ordinary && !ordinary->path.empty(), "hands control: ordinary palette NIF is available");
        if (!ordinary || ordinary->path.empty()) return;
        nifly::NifFile original, written, ordinaryNif;
        LoadNif(original, sourceFile);
        LoadNif(written, StagedFile(mixed.staging, hand->path));
        LoadNif(ordinaryNif, StagedFile(baseline.staging, ordinary->path));
        std::string why;
        const auto palette = ReadRgba(baseline.dump / K::kSourcePalette, why).value();
        const auto normalPalette = ReadRgba(baseline.dump / kPalettePath, why).value();
        int checked = 0, nonPalette = 0, normal = 0, additive = 0;
        bool rows = true, scales = true, normalReused = true;
        for (const auto& shape : EffectShapes(original)) {
            const auto* shader = written.GetHeader().GetBlock<nifly::BSEffectShaderProperty>(shape.shader);
            if (!shader) { rows = scales = false; continue; }
            if (!IsPaletteShape(shape)) {
                if (shape.blend == ShapeBlend::kAdditive) {
                    a_check.Check(shader->baseColor.r == 0.0f && shader->baseColor.r == shader->baseColor.g &&
                        shader->baseColor.g == shader->baseColor.b && shader->baseColor.a == shape.baseAlpha,
                        "hands NIF: non-palette soft warm halo keeps G1 RGB zero and source alpha");
                    ++nonPalette;
                }
                continue;
            }
            const auto actual = ReadRgba(StagedFile(mixed.staging, shader->greyscaleTexture.get()), why);
            const bool isAdditive = shape.blend == ShapeBlend::kAdditive;
            ++(isAdditive ? additive : normal);
            if (!isAdditive) {
                const auto* ordinaryShader = ordinaryNif.GetHeader().GetBlock<nifly::BSEffectShaderProperty>(shape.shader);
                normalReused &= ordinaryShader && shader->greyscaleTexture.get() == ordinaryShader->greyscaleTexture.get();
                const auto child = std::find_if(before.lines.begin(), before.lines.end(), [&](const auto& line) {
                    return ordinaryShader && line.path == ordinaryShader->greyscaleTexture.get();
                });
                normalReused &= child != before.lines.end() && std::count_if(after.lines.begin(), after.lines.end(), [&](const auto& line) {
                    return line.key == child->key && line.path == shader->greyscaleTexture.get();
                }) == 1;
            }
            rows &= actual && actual->width == palette.width && actual->height == palette.height &&
                actual->srgb == (isAdditive ? palette.srgb : normalPalette.srgb);
            if (actual && actual->width == palette.width && actual->height == palette.height) {
                for (std::size_t y = 0; y < palette.height; ++y) {
                    const auto oy = std::lround(double(y) / double(palette.height - 1) * double(normalPalette.height - 1));
                    for (std::size_t x = 0; x < palette.width; ++x) {
                        const auto ox = std::lround(double(x) / double(palette.width - 1) * double(normalPalette.width - 1));
                        const auto p = actual->At(x, y);
                        const auto expected = isAdditive ? ExpectedRampPixel(palette, x, y, 1.4) : 0;
                        for (int c = 0; c < 3; ++c) {
                            rows &= isAdditive ? std::abs(p[c] * 255.0 - expected) <= 1.0 : p[c] == normalPalette.At(ox, oy)[c];
                        }
                        rows &= p[3] == palette.At(x, y)[3];
                    }
                }
            }
            scales &= shader->baseColor.r == shape.baseColour.r && shader->baseColor.g == shape.baseColour.r &&
                      shader->baseColor.b == shape.baseColour.r && shader->baseColorScale == shape.colourScale &&
                      shader->baseColor.a == shape.baseAlpha;
            ++checked;
        }
        a_check.Check(additive > 0 && normal > 0 && rows,
            "hands NIF: additive pixels follow the documented ramp formula; normal smoke keeps the normal palette");
        a_check.Check(normal > 0 && normalReused, "hands smoke: normal palette reuses the ordinary key and copy path without a duplicate");
        a_check.Check(checked >= 2 && scales, "hands NIF: palette row, base alpha and source emissive scale preserved (brightness applied once)");
        a_check.Check(nonPalette > 0, "hands NIF: non-palette additive fixture was exercised");
        a_check.Check(written.GetHeader().GetNumBlocks() == original.GetHeader().GetNumBlocks(),
            "hands detection: triangle-only fixture receives no marble particle system");
        const auto hash = K::Fnv(StagedFile(mixed.staging, hand->path));
        a_check.Check(K::Run(mixed) == 0 && K::Fnv(StagedFile(mixed.staging, hand->path)) == hash,
            "hands cache: a repeated request reuses the same casting NIF bytes");
    }
}
