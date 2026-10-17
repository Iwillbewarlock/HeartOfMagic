// =============================================================================
// GreyTexStats - textures read for judging (GreyTexStats.h)
// =============================================================================

#include "GreyTexStats.h"
#include "GreyTexture.h"

#include <DirectXTex.h>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace GreyBuild
{
    namespace
    {
        constexpr double kByteMax = 255.0;
        constexpr auto kLoadFlags = static_cast<DirectX::DDS_FLAGS>(DirectX::DDS_FLAGS_PERMISSIVE | DirectX::DDS_FLAGS_EXPAND_LUMINANCE);
        constexpr const char* kNotOpening = "does not open";  // the DLL's reason for a texture the game cannot open
        // No WIC: COM is not set up in the builder (WIC conversions fail with E_NOINTERFACE)
        constexpr auto kConvertFilter = static_cast<DirectX::TEX_FILTER_FLAGS>(DirectX::TEX_FILTER_DEFAULT | DirectX::TEX_FILTER_FORCE_NON_WIC);

        [[nodiscard]] std::size_t Clamp(double a_coordinate, std::size_t a_size) noexcept
        {
            const double scaled = std::floor(a_coordinate * static_cast<double>(a_size - 1) + 0.5);  // JS Math.round
            return static_cast<std::size_t>(std::clamp(scaled, 0.0, static_cast<double>(a_size - 1)));
        }

        [[nodiscard]] std::array<double, 4> Lookup(const RgbaImage& a_palette, double a_u, double a_v) noexcept
        {
            return a_palette.At(Clamp(a_u, a_palette.width), Clamp(a_v, a_palette.height));
        }

        [[nodiscard]] double Lum(const std::array<double, 4>& a_c) noexcept
        {
            return a_c[0] * kLumaRed + a_c[1] * kLumaGreen + a_c[2] * kLumaBlue;
        }
    }

    std::array<double, 4> RgbaImage::At(std::size_t a_x, std::size_t a_y) const noexcept
    {
        const std::uint8_t* p = pixels.data() + (a_y * width + a_x) * 4;
        return { p[0] / kByteMax, p[1] / kByteMax, p[2] / kByteMax, p[3] / kByteMax };
    }

    std::optional<RgbaImage> ReadRgba(const std::filesystem::path& a_file, std::string& a_why)
    {
        DirectX::TexMetadata meta{};
        DirectX::ScratchImage source;
        if (const HRESULT hr = DirectX::LoadFromDDSFile(a_file.c_str(), kLoadFlags, &meta, source); FAILED(hr)) {
            a_why = fmt::format("not a readable DDS (HRESULT 0x{:08X})", static_cast<std::uint32_t>(hr));
            return std::nullopt;
        }
        const DirectX::Image* top = source.GetImage(0, 0, 0);
        if (!top) {
            a_why = "no image";
            return std::nullopt;
        }
        // The stored bytes, not a colour space conversion: sRGB decodes to sRGB
        const DXGI_FORMAT target = DirectX::IsSRGB(meta.format) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        DirectX::ScratchImage converted;
        const DirectX::Image* image = top;
        if (top->format != target) {
            const HRESULT hr = DirectX::IsCompressed(top->format) ? DirectX::Decompress(*top, target, converted) :
                                                                    DirectX::Convert(*top, target, kConvertFilter,
                                                                        DirectX::TEX_THRESHOLD_DEFAULT, converted);
            if (FAILED(hr)) {
                a_why = fmt::format("decoding format {} failed (HRESULT 0x{:08X})", static_cast<int>(top->format),
                    static_cast<std::uint32_t>(hr));
                return std::nullopt;
            }
            image = converted.GetImage(0, 0, 0);
        }
        RgbaImage out;
        out.srgb = DirectX::IsSRGB(meta.format);
        out.width = image->width;
        out.height = image->height;
        out.pixels.resize(out.width * out.height * 4);
        for (std::size_t y = 0; y < out.height; ++y) {
            std::copy_n(image->pixels + y * image->rowPitch, out.width * 4, out.pixels.data() + y * out.width * 4);
        }
        if (out.width == 0 || out.height == 0) {
            a_why = "an empty image";
            return std::nullopt;
        }
        return out;
    }

    TexStats ComputeStats(const RgbaImage& a_image)
    {
        TexStats stats;
        const std::size_t w = a_image.width;
        const std::size_t h = a_image.height;
        double alphaSum = 0.0;
        Rgb weighted;
        std::size_t lit = 0;
        std::size_t coloured = 0;
        for (std::size_t y = 0; y < h; ++y) {
            for (std::size_t x = 0; x < w; ++x) {
                const auto c = a_image.At(x, y);
                const double high = std::max({ c[0], c[1], c[2] });
                const double low = std::min({ c[0], c[1], c[2] });
                if (c[3] * high > kStatLitWeight) {
                    ++lit;
                    coloured += (high - low) / high > kStatColouredSat ? 1 : 0;
                }
                alphaSum += c[3];
                weighted.r += c[0] * c[3];
                weighted.g += c[1] * c[3];
                weighted.b += c[2] * c[3];
            }
        }
        const Rgb mean = alphaSum > 0.0 ? Rgb{ weighted.r / alphaSum, weighted.g / alphaSum, weighted.b / alphaSum } : Rgb{};
        stats.meanRgb = { Round3(mean.r), Round3(mean.g), Round3(mean.b) };
        stats.meanSat = Round3(Saturation(mean, kStatMinChannel));
        stats.colouredFrac = lit > 0 ? Round3(static_cast<double>(coloured) / static_cast<double>(lit)) : 0.0;
        // Detail and energy over the texels with a right and a lower neighbour
        const auto energy = [&](std::size_t a_x, std::size_t a_y) {
            const auto c = a_image.At(a_x, a_y);
            return std::max({ c[0], c[1], c[2] }) * c[3];
        };
        double gradient = 0.0;
        double total = 0.0;
        std::size_t count = 0;
        for (std::size_t y = 0; y + 1 < h; ++y) {
            for (std::size_t x = 0; x + 1 < w; ++x) {
                const double e = energy(x, y);
                gradient += std::fabs(energy(x + 1, y) - e) + std::fabs(energy(x, y + 1) - e);
                total += e;
                ++count;
            }
        }
        stats.detail = total > 0.0 ? Round3(gradient / total) : 0.0;
        stats.alphaMean = w * h > 0 ? alphaSum / static_cast<double>(w * h) : 0.0;
        stats.energyMean = Round3(total / static_cast<double>(std::max<std::size_t>(count, 1)));
        return stats;
    }

    bool IsColouredTexture(const TexStats& a_stats) noexcept
    {
        return a_stats.colouredFrac >= kStatColouredFrac || a_stats.meanSat >= kStatMeanSat;
    }

    const RgbaImage& WhiteTexel()
    {
        static const RgbaImage white{ 1, 1, { 255, 255, 255, 255 }, false };
        return white;
    }

    float SrgbToLinear(float a_value) noexcept
    {
        return a_value <= kSrgbLinearLimit ? a_value / kSrgbLinearScale : std::pow((a_value + kSrgbOffset) / (1.0f + kSrgbOffset), kSrgbGamma);
    }

    float LinearToSrgb(float a_value) noexcept
    {
        return a_value <= kLinearLimit ? a_value * kSrgbLinearScale : (1.0f + kSrgbOffset) * std::pow(a_value, 1.0f / kSrgbGamma) - kSrgbOffset;
    }

    PaletteSim SimulatePalette(const RgbaImage& a_texture, const RgbaImage& a_palette, bool a_paletteAlpha, double a_colourRow,
        double a_alphaRow, double a_darkLum, double a_testThreshold, bool a_cutout)
    {
        PaletteSim sim;
        const std::size_t step = std::max<std::size_t>(1, std::max(a_texture.width, a_texture.height) / kSimGrid);
        double energy = 0.0;
        double alpha = 0.0;
        std::size_t samples = 0;
        std::size_t lit = 0;
        std::size_t dark = 0;
        double uMin = 1.0;
        double uMax = 0.0;
        for (std::size_t y = 0; y < a_texture.height; y += step) {
            for (std::size_t x = 0; x < a_texture.width; x += step) {
                const auto t = a_texture.At(x, y);
                auto c = Lookup(a_palette, t[1], a_colourRow);
                if (a_palette.srgb) {  // Z7: the luminance of the colour the shader gets (decoded), as the builder computes it
                    for (int i = 0; i < 3; ++i) {
                        c[i] = SrgbToLinear(static_cast<float>(c[i]));
                    }
                }
                const double raw = a_paletteAlpha ? Lookup(a_palette, t[3], a_alphaRow)[3] : t[3];
                const bool passes = a_testThreshold < 0.0 || raw >= a_testThreshold;  // Z8: GEQUAL
                const double a = passes ? raw : 0.0;
                const double lum = Lum(c);
                energy += lum * a;
                alpha += a_cutout ? (passes ? 1.0 : 0.0) : a;
                ++samples;
                if (t[3] > kSimLitAlpha) {
                    ++lit;
                    dark += lum < a_darkLum ? 1 : 0;
                    uMin = std::min(uMin, t[1]);
                    uMax = std::max(uMax, t[1]);
                }
            }
        }
        if (samples > 0) {
            sim.energy = energy / static_cast<double>(samples);
            sim.coverage = alpha / static_cast<double>(samples);
        }
        sim.uRange = std::max(0.0, uMax - uMin);
        sim.darkFrac = lit > 0 ? static_cast<double>(dark) / static_cast<double>(lit) : 0.0;
        return sim;
    }

    TextureSource::TextureSource(std::vector<std::filesystem::path> a_folders, std::map<std::string, std::string> a_notInGame,
        std::filesystem::path a_generated) :
        m_folders(std::move(a_folders)), m_generated(std::move(a_generated)), m_notInGame(std::move(a_notInGame))
    {}

    std::vector<std::filesystem::path> TextureSource::OwnFolders()
    {
        std::vector<std::filesystem::path> out;
        std::wstring exe(MAX_PATH, L'\0');
        for (;;) {
            const DWORD length = ::GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
            if (length == 0) {
                return out;
            }
            if (length < exe.size()) {
                exe.resize(length);
                break;
            }
            exe.resize(exe.size() * 2);
        }
        const auto dir = std::filesystem::path(exe).parent_path();
        // tools\BlackFlame\blackflame-greybuild.exe in the tester package: the mod root two folders up;
        // build\tools\Release\ in the repository: plugins\BlackFlame\data three folders up
        for (const auto& candidate : { dir / ".." / "..", dir / ".." / ".." / ".." / "plugins" / "BlackFlame" / "data" }) {
            std::error_code ec;
            if (std::filesystem::is_directory(candidate / "textures", ec)) {
                out.push_back(std::filesystem::weakly_canonical(candidate, ec));
            }
        }
        return out;
    }

    void TextureSource::Put(const std::string& a_path, std::optional<RgbaImage> a_image)
    {
        Entry entry;
        if (a_image) {
            entry.status = TextureRead::kOk;
            entry.stats = std::make_unique<TexStats>(ComputeStats(*a_image));
            entry.image = std::make_unique<RgbaImage>(std::move(*a_image));
        } else {
            entry.why = "does not decode (self-test)";
        }
        m_cache[a_path] = std::move(entry);
    }

    TextureLook TextureSource::Look(const std::string& a_slot)
    {
        TextureLook look;
        const auto path = TextureDataPath(a_slot);
        if (!path) {
            look.why = fmt::format("'{}' is no texture path the builder reads", a_slot);
            return look;
        }
        look.path = *path;
        auto it = m_cache.find(*path);
        if (it == m_cache.end()) {
            Entry entry;
            std::vector<std::filesystem::path> folders = m_folders;
            if (!m_generated.empty() && StartsWith(*path, kTextureCopyRoot)) {
                folders = { m_generated };  // Y14, Z11: one of our copies is this run's, read from the output only
            }
            if (const auto file = FindSource(folders, *path)) {
                if (auto image = ReadRgba(*file, entry.why)) {
                    entry.status = TextureRead::kOk;
                    entry.stats = std::make_unique<TexStats>(ComputeStats(*image));
                    entry.image = std::make_unique<RgbaImage>(std::move(*image));
                }
            } else if (const auto reason = m_notInGame.find(*path);
                       reason != m_notInGame.end() && StartsWith(reason->second, kNotOpening)) {
                entry.status = TextureRead::kMissing;
                entry.why = "the game: " + reason->second;
            } else {
                entry.why = reason != m_notInGame.end() ? "not in the dump - the game: " + reason->second :
                                                          std::string("in none of the folders read");
            }
            it = m_cache.emplace(*path, std::move(entry)).first;
        }
        look.status = it->second.status;
        look.image = it->second.image.get();
        look.stats = it->second.stats.get();
        look.why = it->second.why;
        return look;
    }
}
