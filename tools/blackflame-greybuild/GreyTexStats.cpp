// =============================================================================
// GreyTexStats - textures read for judging (GreyTexStats.h)
// =============================================================================

#include "GreyTexStats.h"

#include "../../plugins/BlackFlame/src/DataPathRule.h"
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
#include <cstdlib>
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
        constexpr std::uint32_t kByte = 255;
        // lit: alpha x max channel > kStatLitWeight in 0..1, in bytes a x m > kStatLitWeight x 255 x 255 (6502.5), which for
        // a whole a x m is a x m > its whole part (no product is within double rounding of it, so the double test agreed)
        constexpr auto kLitProduct = static_cast<std::uint32_t>(kStatLitWeight * kByte * kByte);
        // coloured: (max - min) / max > kStatColouredSat exactly, as (max - min) x kSatScale > kSatNumerator x max
        constexpr std::uint32_t kSatScale = 1024;
        constexpr auto kSatNumerator = static_cast<std::uint32_t>(kStatColouredSat * kSatScale);
        static_assert(kSatNumerator == kStatColouredSat * kSatScale, "kStatColouredSat must be a multiple of 1/kSatScale for the exact compare");
        constexpr std::size_t kParallelTexels = 64 * 1024;  // below this a texture's sums run on one thread (no fork)

        struct ColourSums
        {
            std::uint64_t alpha = 0;  // sum of a
            std::uint64_t r = 0;      // sum of r x a (g, b alike)
            std::uint64_t g = 0;
            std::uint64_t b = 0;
            std::uint64_t lit = 0;
            std::uint64_t coloured = 0;
        };

        // The first pass over every texel. Known limit: OpenMP 2.0 (MSVC) wants a signed loop index and scalar reductions
        [[nodiscard]] ColourSums SumColour(const RgbaImage& a_image) noexcept
        {
            const std::uint8_t* const base = a_image.pixels.data();
            const std::size_t w = a_image.width;
            const auto h = static_cast<std::int64_t>(a_image.height);
            std::uint64_t alpha = 0, r = 0, g = 0, b = 0, lit = 0, coloured = 0;
#pragma omp parallel for reduction(+ : alpha, r, g, b, lit, coloured) if (w * a_image.height >= kParallelTexels)
            for (std::int64_t y = 0; y < h; ++y) {
                const std::uint8_t* p = base + static_cast<std::size_t>(y) * w * 4;
                for (std::size_t x = 0; x < w; ++x, p += 4) {
                    const std::uint32_t a = p[3];
                    const std::uint32_t high = std::max({ p[0], p[1], p[2] });
                    const std::uint32_t low = std::min({ p[0], p[1], p[2] });
                    if (a * high > kLitProduct) {
                        ++lit;
                        coloured += (high - low) * kSatScale > kSatNumerator * high ? 1 : 0;
                    }
                    alpha += a;
                    r += p[0] * a;
                    g += p[1] * a;
                    b += p[2] * a;
                }
            }
            return { alpha, r, g, b, lit, coloured };
        }

        // max channel x alpha in bytes (0..255 x 255)
        [[nodiscard]] std::int64_t Energy(const std::uint8_t* a_texel) noexcept
        {
            return static_cast<std::int64_t>(std::max({ a_texel[0], a_texel[1], a_texel[2] })) * a_texel[3];
        }

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

    std::optional<RgbaImage> ReadRgba(const std::filesystem::path& a_file, std::string& a_why, bool* a_transient)
    {
        if (a_transient) {
            *a_transient = false;
        }
        DirectX::TexMetadata meta{};
        DirectX::ScratchImage source;
        if (const HRESULT hr = DirectX::LoadFromDDSFile(a_file.c_str(), kLoadFlags, &meta, source); FAILED(hr)) {
            if (a_transient) {
                *a_transient = IsTransientResult(hr);
            }
            a_why = fmt::format("not a readable DDS (HRESULT 0x{:08X})", static_cast<std::uint32_t>(hr));
            return std::nullopt;
        }
        const DirectX::Image* top = source.GetImage(0, 0, 0);
        if (!top) {
            a_why = "no image";
            return std::nullopt;
        }
        return ToRgba(*top, DirectX::IsSRGB(meta.format), a_why, a_transient);
    }

    std::optional<RgbaImage> ToRgba(const DirectX::Image& a_top, bool a_srgb, std::string& a_why, bool* a_transient)
    {
        if (a_transient) {
            *a_transient = false;
        }
        // The stored bytes, not a colour space conversion: sRGB decodes to sRGB
        const DXGI_FORMAT target = a_srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        DirectX::ScratchImage converted;
        const DirectX::Image* image = &a_top;
        if (a_top.format != target) {
            const HRESULT hr = DirectX::IsCompressed(a_top.format) ? DirectX::Decompress(a_top, target, converted) :
                                                                     DirectX::Convert(a_top, target, kConvertFilter,
                                                                         DirectX::TEX_THRESHOLD_DEFAULT, converted);
            if (FAILED(hr)) {
                if (a_transient) {
                    *a_transient = IsTransientResult(hr);
                }
                a_why = fmt::format("decoding format {} failed (HRESULT 0x{:08X})", static_cast<int>(a_top.format),
                    static_cast<std::uint32_t>(hr));
                return std::nullopt;
            }
            image = converted.GetImage(0, 0, 0);
        }
        RgbaImage out;
        out.srgb = a_srgb;
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

    TexStats ComputeColourStats(const RgbaImage& a_image)
    {
        TexStats stats;
        const ColourSums sums = SumColour(a_image);
        const double alpha = static_cast<double>(sums.alpha);  // every sum is under 2^53: exact in a double
        const Rgb mean = sums.alpha > 0 ? Rgb{ static_cast<double>(sums.r) / (alpha * kByteMax), static_cast<double>(sums.g) / (alpha * kByteMax),
                                              static_cast<double>(sums.b) / (alpha * kByteMax) } :
                                          Rgb{};
        stats.meanRgb = { Round3(mean.r), Round3(mean.g), Round3(mean.b) };
        stats.meanSat = Round3(Saturation(mean, kStatMinChannel));
        stats.colouredFrac = sums.lit > 0 ? Round3(static_cast<double>(sums.coloured) / static_cast<double>(sums.lit)) : 0.0;
        const std::size_t texels = a_image.width * a_image.height;
        stats.alphaMean = texels > 0 ? alpha / (kByteMax * static_cast<double>(texels)) : 0.0;
        return stats;
    }

    TexStats ComputeStats(const RgbaImage& a_image)
    {
        TexStats stats = ComputeColourStats(a_image);
        // Detail and energy over the texels with a right and a lower neighbour
        const std::uint8_t* const base = a_image.pixels.data();
        const std::size_t w = a_image.width;
        const auto rows = static_cast<std::int64_t>(a_image.height > 0 ? a_image.height - 1 : 0);
        std::uint64_t gradient = 0;
        std::uint64_t total = 0;
#pragma omp parallel for reduction(+ : gradient, total) if (w * a_image.height >= kParallelTexels)
        for (std::int64_t y = 0; y < rows; ++y) {
            const std::uint8_t* p = base + static_cast<std::size_t>(y) * w * 4;
            const std::size_t below = w * 4;
            for (std::size_t x = 0; x + 1 < w; ++x, p += 4) {
                const std::int64_t e = Energy(p);
                gradient += static_cast<std::uint64_t>(std::llabs(Energy(p + 4) - e) + std::llabs(Energy(p + below) - e));
                total += static_cast<std::uint64_t>(e);
            }
        }
        const std::size_t count = w > 0 && a_image.height > 0 ? (w - 1) * (a_image.height - 1) : 0;
        // both sums are in byte x byte steps: detail is their ratio, the energy mean their mean over 255 x 255
        stats.detail = total > 0 ? Round3(static_cast<double>(gradient) / static_cast<double>(total)) : 0.0;
        stats.energyMean = Round3(static_cast<double>(total) / (kByteMax * kByteMax * static_cast<double>(std::max<std::size_t>(count, 1))));
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

    std::filesystem::path ThisExecutable()
    {
        std::wstring exe(MAX_PATH, L'\0');
        for (;;) {
            const DWORD length = ::GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
            if (length == 0) {
                return {};
            }
            if (length < exe.size()) {
                exe.resize(length);
                return std::filesystem::path(exe);
            }
            exe.resize(exe.size() * 2);
        }
    }

    std::vector<std::filesystem::path> TextureSource::OwnFolders()
    {
        std::vector<std::filesystem::path> out;
        const auto exe = ThisExecutable();
        if (exe.empty()) {
            return out;
        }
        const auto dir = exe.parent_path();
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

    TextureSource::Known& TextureSource::Remember(const std::string& a_path, const Entry& a_entry, bool a_reusable, bool a_fresh)
    {
        auto& known = m_known[a_path];
        // a re-read of the same file after a Clear keeps the statistics it had (the dump does not change in a run)
        const bool same = !a_fresh && known.reusable && a_reusable && known.status == TextureRead::kOk &&
                          a_entry.status == TextureRead::kOk && known.file == a_entry.file;
        if (!same) {
            known.stats = a_entry.image ? ComputeStats(*a_entry.image) : TexStats{};
        }
        known.status = a_entry.status;
        known.why = a_entry.why;
        known.file = a_entry.file;
        known.reusable = a_reusable;
        return known;
    }

    TextureLook TextureSource::LookOf(const std::string& a_path, const Entry& a_entry) const
    {
        TextureLook look;
        look.path = a_path;
        look.status = a_entry.status;
        look.image = a_entry.image.get();
        const auto known = m_known.find(a_path);
        look.stats = a_entry.status == TextureRead::kOk && known != m_known.end() ? &known->second.stats : nullptr;
        look.why = a_entry.why;
        look.file = a_entry.file;
        return look;
    }

    void TextureSource::Put(const std::string& a_path, std::optional<RgbaImage> a_image)
    {
        Entry entry;
        if (a_image) {
            entry.status = TextureRead::kOk;
            entry.image = std::make_unique<RgbaImage>(std::move(*a_image));
        } else {
            entry.why = "does not decode (self-test)";
        }
        (void)Remember(a_path, entry, true, true);
        if (const auto old = m_cache.find(a_path); old != m_cache.end() && old->second.image) {
            m_cachedBytes -= old->second.image->pixels.size();
        }
        m_cachedBytes += entry.image ? entry.image->pixels.size() : 0;
        m_cache[a_path] = std::move(entry);
    }

    std::uint64_t TextureSource::CachedBytes() const noexcept
    {
        return m_cachedBytes;
    }

    TextureSource::ReadCounts TextureSource::Counts() const noexcept
    {
        return { m_decodes, m_decodedPaths.size(), m_statsAnswers };
    }

    void TextureSource::Clear() noexcept
    {
        m_cache.clear();
        m_cachedBytes = 0;
    }

    TextureLook TextureSource::LookStats(const std::string& a_slot)
    {
        const auto path = TextureDataPath(a_slot);
        if (path && m_cache.find(*path) == m_cache.end()) {
            if (const auto known = m_known.find(*path); known != m_known.end() && known->second.reusable) {
                ++m_statsAnswers;
                TextureLook look;
                look.path = *path;
                look.status = known->second.status;
                look.stats = known->second.status == TextureRead::kOk ? &known->second.stats : nullptr;
                look.why = known->second.why;
                look.file = known->second.file;
                return look;
            }
        }
        return Look(a_slot);
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
            const bool generated = !m_generated.empty() && StartsWith(*path, kTextureCopyRoot);
            if (generated) {
                folders = { m_generated };  // Y14, Z11: one of our copies is this run's, read from the output only
            }
            const bool listed = !m_limited || generated || m_listed.count(*path) != 0;
            const auto found = listed ? FindSource(folders, *path) : std::nullopt;
            if (m_limited && !generated && listed && !found) {
                // the request lists it as dumped and it is not there: the dump is broken, not the texture (Codex review 2)
                look.why = "listed as dumped, not in the dump";
                look.transient = true;
                if (m_transientSink) {
                    m_transientSink(fmt::format("{}: {}", *path, look.why));
                }
                return look;
            }
            if (const auto file = found) {
                if (m_gate && !m_gate(*file, look.why)) {
                    look.transient = true;
                    look.gated = true;
                    return look;  // over the memory budget: not read, not cached (the gate told the object)
                }
                entry.file = *file;
                bool transient = false;
                ++m_decodes;
                m_decodedPaths.insert(*path);
                auto image = ReadRgba(*file, entry.why, &transient);
                if (!image && transient) {
                    look.why = entry.why;
                    look.transient = true;
                    if (m_transientSink) {
                        m_transientSink(fmt::format("{}: {}", *path, entry.why));
                    }
                    return look;  // not cached: a later object reads it again
                }
                if (image) {
                    entry.status = TextureRead::kOk;
                    entry.image = std::make_unique<RgbaImage>(std::move(*image));
                }
            } else if (const auto reason = m_notInGame.find(*path);
                       reason != m_notInGame.end() && StartsWith(reason->second, kNotOpening)) {
                entry.status = TextureRead::kMissing;
                entry.why = "the game: " + reason->second;
            } else if (reason == m_notInGame.end() &&
                       (*path == BlackFlame::DataPathRule::kOurPalette || *path == BlackFlame::DataPathRule::kOurPaletteAdd)) {
                // our fixed palette, which only a DLL of this release dumps (CopyRequestData PlanTextures)
                entry.why = "not in the dump (a request from a Black Flame DLL before the palette dump: start the game once to "
                            "the main menu with this release, quit, run the builder again) nor in the builder's own folders";
            } else {
                entry.why = reason != m_notInGame.end() ? "not in the dump - the game: " + reason->second :
                                                          std::string("in none of the folders read");
            }
            (void)Remember(*path, entry, !generated, false);
            it = m_cache.emplace(*path, std::move(entry)).first;
            m_cachedBytes += it->second.image ? it->second.image->pixels.size() : 0;  // counted once it is in the cache
        }
        return LookOf(*path, it->second);
    }
}
