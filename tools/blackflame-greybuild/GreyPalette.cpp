// =============================================================================
// GreyPalette - the palettes the full-grey copies' palette shaders take (GreyPalette.h)
// =============================================================================

#include "GreyPalette.h"
#include "GreyKeyed.h"
#include "GreyTexture.h"

#include <DirectXTex.h>

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <system_error>

namespace GreyBuild
{
    namespace
    {
        constexpr float kByteMax = 255.0f;
        constexpr long kByteTop = 255;

        [[nodiscard]] std::uint8_t Byte(float a_value) noexcept
        {
            return static_cast<std::uint8_t>(std::clamp(std::lround(a_value * kByteMax), 0L, kByteTop));
        }


        // P2: every texel's colour to its luminance (8-bit, the builder's Luminance; an sRGB palette decoded to linear and
        // encoded back), alpha untouched
        void Desaturate(RgbaImage& a_image)
        {
            for (std::size_t i = 0; i + 3 < a_image.pixels.size(); i += 4) {
                auto* p = a_image.pixels.data() + i;
                float r = p[0] / kByteMax;
                float g = p[1] / kByteMax;
                float b = p[2] / kByteMax;
                if (a_image.srgb) {
                    r = SrgbToLinear(r);  // Y13, Z7: the shared transfer (GreyTexStats.h)
                    g = SrgbToLinear(g);
                    b = SrgbToLinear(b);
                }
                const float lum = Luminance(r, g, b);
                const auto grey = Byte(a_image.srgb ? LinearToSrgb(lum) : lum);
                p[0] = grey;
                p[1] = grey;
                p[2] = grey;
            }
        }

        // P1: our palette's colour at the same relative u and v, the source's alpha
        void TakeColour(RgbaImage& a_image, const RgbaImage& a_ours)
        {
            for (std::size_t y = 0; y < a_image.height; ++y) {
                const double v = a_image.height > 1 ? static_cast<double>(y) / static_cast<double>(a_image.height - 1) : 0.0;
                const auto oy = static_cast<std::size_t>(std::lround(v * static_cast<double>(a_ours.height - 1)));
                for (std::size_t x = 0; x < a_image.width; ++x) {
                    const double u = a_image.width > 1 ? static_cast<double>(x) / static_cast<double>(a_image.width - 1) : 0.0;
                    const auto ox = static_cast<std::size_t>(std::lround(u * static_cast<double>(a_ours.width - 1)));
                    const auto* from = a_ours.pixels.data() + (oy * a_ours.width + ox) * 4;
                    auto* to = a_image.pixels.data() + (y * a_image.width + x) * 4;
                    std::copy_n(from, 3, to);
                }
            }
        }

        [[nodiscard]] bool Write(const RgbaImage& a_image, const std::filesystem::path& a_file, std::string& a_why)
        {
            DirectX::ScratchImage image;
            const DXGI_FORMAT format = a_image.srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
            if (FAILED(image.Initialize2D(format, a_image.width, a_image.height, 1, 1))) {
                a_why = "allocating the image failed";
                return false;
            }
            const DirectX::Image& target = *image.GetImage(0, 0, 0);
            for (std::size_t y = 0; y < a_image.height; ++y) {
                std::copy_n(a_image.pixels.data() + y * a_image.width * 4, a_image.width * 4, target.pixels + y * target.rowPitch);
            }
            std::error_code ec;
            std::filesystem::create_directories(a_file.parent_path(), ec);
            if (const HRESULT hr = DirectX::SaveToDDSFile(target, DirectX::DDS_FLAGS_NONE, a_file.c_str()); FAILED(hr)) {
                a_why = fmt::format("writing {} failed (HRESULT 0x{:08X})", a_file.string(), static_cast<std::uint32_t>(hr));
                return false;
            }
            return true;
        }
    }

    std::string DerivedPalette(PaletteJob& a_job, const std::string& a_sourcePalette, bool a_additive, std::string& a_why)
    {
        if (!a_job.textures) {
            a_why = "no texture source in this run";
            return {};
        }
        const auto source = a_job.textures->Look(a_sourcePalette);
        if (source.status != TextureRead::kOk) {
            a_why = fmt::format("source palette {}: {}", source.path.empty() ? a_sourcePalette : source.path, source.why);
            ++a_job.totals.fallback;
            if (a_job.keyed && !source.transient) {  // a transient read made the object failed already (the sink, the gate)
                a_job.keyed->Fallback(a_why + ": today's palette kept");  // GreyPalette.cpp:98 in the design: deterministic
            }
            return {};
        }
        // P2 (additive) or P1 (normal blend) from the source image; false with a_why when our palette does not read
        using Made = KeyedOutputs::Made;
        const auto make = [&](RgbaImage& a_image, std::string& a_makeWhy) {
            if (a_additive && a_job.tintAddHands) {
                MakeHandPalette(a_image, static_cast<float>(*a_job.tintAddHands));
                return Made::kMade;
            }
            if (a_additive) {
                Desaturate(a_image);
                return Made::kMade;
            }
            const auto ours = a_job.textures->Look(kPalettePath);
            if (ours.status != TextureRead::kOk) {
                a_makeWhy = fmt::format("our palette {}: {}", kPalettePath, ours.why);
                return ours.gated ? Made::kGated : ours.transient ? Made::kFailed : Made::kFallback;
            }
            TakeColour(a_image, *ours.image);
            a_image.srgb = ours.image->srgb;  // Y13: the colour bytes are ours, so is their encoding (alpha is linear)
            return Made::kMade;
        };
        if (a_job.keyed) {  // key mode: the copy at its own key's folder, its line, the object's outcome (GreyKeyed.h)
            const auto slot = a_job.keyed->Palette(source, a_additive, make, Write, a_why);
            ++(slot.empty() ? a_job.totals.fallback : (a_additive ? a_job.totals.shadersAdd : a_job.totals.shadersNormal));
            return slot;
        }
        const std::string key = fmt::format("{}|{}", a_additive ? "add" : "normal", source.path);
        auto it = a_job.made.find(key);
        if (it == a_job.made.end()) {
            RgbaImage image = *source.image;
            std::string why;
            bool ok = make(image, why) == Made::kMade;
            const std::string slot = std::string(kPaletteCopyRoot) + (a_additive ? kPaletteAddFolder : kPaletteNormalFolder) +
                                     source.path.substr(std::char_traits<char>::length(kTexturesRoot));
            if (ok) {
                ok = Write(image, a_job.out / std::filesystem::path(slot), why);
            }
            if (ok) {
                ++(a_additive ? a_job.totals.derivedAdd : a_job.totals.derivedNormal);
            } else {
                ++a_job.totals.failed;
            }
            it = a_job.made.emplace(key, ok ? slot : std::string()).first;
            if (!ok) {
                a_why = why;
            }
        }
        if (it->second.empty()) {
            ++a_job.totals.fallback;
            if (a_why.empty()) {
                a_why = "its derived palette could not be made (see the first NOTE for it)";
            }
            return {};
        }
        ++(a_additive ? a_job.totals.shadersAdd : a_job.totals.shadersNormal);
        return it->second;
    }

    void DesaturatePalette(RgbaImage& a_image)
    {
        Desaturate(a_image);
    }

    void MakeHandPalette(RgbaImage& a_image, float a_tint)
    {
        const auto luminance = [&](std::size_t a_x, std::size_t a_y) {
            const auto p = a_image.At(a_x, a_y);
            const auto linear = [&](double a_v) {
                return a_image.srgb ? SrgbToLinear(static_cast<float>(a_v)) : static_cast<float>(a_v);
            };
            return Luminance(linear(p[0]), linear(p[1]), linear(p[2]));
        };
        for (std::size_t y = 0; y < a_image.height; ++y) {
            float peak = 0.0f;
            for (std::size_t x = 0; x < a_image.width; ++x) {
                peak = std::max(peak, luminance(x, y));
            }
            for (std::size_t x = 0; x < a_image.width; ++x) {
                const float u = a_image.width > 1 ? static_cast<float>(x) / static_cast<float>(a_image.width - 1) : 1.0f;
                const float t = std::clamp((u - 0.30f) / 0.40f, 0.0f, 1.0f);
                const float row = peak > 0.0f ? luminance(x, y) / peak : 0.0f;
                const float value = std::clamp(a_tint * t * t * (3.0f - 2.0f * t) * row, 0.0f, 1.0f);
                const auto grey = Byte(a_image.srgb ? LinearToSrgb(value) : value);
                auto* pixel = a_image.pixels.data() + (y * a_image.width + x) * 4;
                pixel[0] = pixel[1] = pixel[2] = grey;
            }
        }
    }

    bool WritePaletteDds(const RgbaImage& a_image, const std::filesystem::path& a_file, std::string& a_why)
    {
        return Write(a_image, a_file, a_why);
    }

    std::string PaletteTotalsLine(const PaletteJob& a_job)
    {
        const auto& t = a_job.totals;
        if (a_job.mode == PaletteMode::kGodskin) {
            return "palettes  godskin mode (--palette-mode godskin): GradBlackFlameAdd.dds on additive palette shaders, "
                   "GradBlackFlame.dds on normal blend";
        }
        return fmt::format("palettes  derived: additive (P2, source palette at luminance, its alpha) {} file(s) for {} shader(s); "
                           "normal blend (P1, our colour, the source's alpha) {} file(s) for {} shader(s); source palette not "
                           "read, today's palette kept: {} shader(s); files that could not be written {}",
            t.derivedAdd, t.shadersAdd, t.derivedNormal, t.shadersNormal, t.fallback, t.failed);
    }
}
