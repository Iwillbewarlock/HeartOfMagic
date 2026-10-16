// =============================================================================
// GreyPalette - the palettes the full-grey copies' palette shaders take (GreyPalette.h)
// =============================================================================

#include "GreyPalette.h"
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

        // Y13: sRGB transfer (IEC 61966-2-1), so an sRGB palette's luminance is taken from linear values
        [[nodiscard]] float ToLinear(float a_value) noexcept
        {
            return a_value <= 0.04045f ? a_value / 12.92f : std::pow((a_value + 0.055f) / 1.055f, 2.4f);
        }

        [[nodiscard]] float ToSrgb(float a_value) noexcept
        {
            return a_value <= 0.0031308f ? a_value * 12.92f : 1.055f * std::pow(a_value, 1.0f / 2.4f) - 0.055f;
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
                    r = ToLinear(r);
                    g = ToLinear(g);
                    b = ToLinear(b);
                }
                const float lum = Luminance(r, g, b);
                const auto grey = Byte(a_image.srgb ? ToSrgb(lum) : lum);
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
            return {};
        }
        const std::string key = fmt::format("{}|{}", a_additive ? "add" : "normal", source.path);
        auto it = a_job.made.find(key);
        if (it == a_job.made.end()) {
            RgbaImage image = *source.image;
            std::string why;
            bool ok = true;
            if (a_additive) {
                Desaturate(image);
            } else {
                const auto ours = a_job.textures->Look(kPalettePath);
                ok = ours.status == TextureRead::kOk;
                if (ok) {
                    TakeColour(image, *ours.image);
                    image.srgb = ours.image->srgb;  // Y13: the colour bytes are ours, so is their encoding (alpha is linear)
                } else {
                    why = fmt::format("our palette {}: {}", kPalettePath, ours.why);
                }
            }
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
