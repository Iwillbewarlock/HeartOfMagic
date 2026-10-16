// =============================================================================
// GreyTexture - the DDS recolour (DirectXTex) and the texture path rules (GreyTexture.h)
// =============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include "GreyTexture.h"

#include <DirectXTex.h>

#include <algorithm>
#include <fmt/format.h>
#include <system_error>

namespace GreyBuild
{
    namespace
    {
        constexpr const char* kDataRoot = "data\\";
        constexpr const char* kDdsSuffix = ".dds";
        // A pixel counts for the saturation measure when its alpha is above this (an effect texture's fill under zero
        // alpha is often coloured and never seen)
        constexpr float kVisibleAlpha = 1.0f / 255.0f;
        constexpr std::uint64_t kMiB = 1024ull * 1024ull;

        using DirectX::XMVECTOR;

        [[nodiscard]] std::string HrText(HRESULT a_result)
        {
            return fmt::format("HRESULT 0x{:08X} ({})", static_cast<std::uint32_t>(a_result),
                std::system_category().message(a_result));
        }

        [[nodiscard]] const char* FormatName(DXGI_FORMAT a_format) noexcept
        {
            switch (a_format) {
            case DXGI_FORMAT_BC1_UNORM: return "BC1_UNORM";
            case DXGI_FORMAT_BC1_UNORM_SRGB: return "BC1_UNORM_SRGB";
            case DXGI_FORMAT_BC2_UNORM: return "BC2_UNORM";
            case DXGI_FORMAT_BC2_UNORM_SRGB: return "BC2_UNORM_SRGB";
            case DXGI_FORMAT_BC3_UNORM: return "BC3_UNORM";
            case DXGI_FORMAT_BC3_UNORM_SRGB: return "BC3_UNORM_SRGB";
            case DXGI_FORMAT_BC4_UNORM: return "BC4_UNORM";
            case DXGI_FORMAT_BC5_UNORM: return "BC5_UNORM";
            case DXGI_FORMAT_BC6H_UF16: return "BC6H_UF16";
            case DXGI_FORMAT_BC7_UNORM: return "BC7_UNORM";
            case DXGI_FORMAT_BC7_UNORM_SRGB: return "BC7_UNORM_SRGB";
            case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
            case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
            case DXGI_FORMAT_B8G8R8X8_UNORM: return "B8G8R8X8_UNORM";
            case DXGI_FORMAT_B5G6R5_UNORM: return "B5G6R5_UNORM";
            case DXGI_FORMAT_R8_UNORM: return "R8_UNORM";
            case DXGI_FORMAT_A8_UNORM: return "A8_UNORM";
            case DXGI_FORMAT_R8G8_UNORM: return "R8G8_UNORM";
            default: return "other";
            }
        }

        // One value per pixel: no colour to take out
        [[nodiscard]] bool IsSingleChannel(DXGI_FORMAT a_format) noexcept
        {
            switch (a_format) {
            case DXGI_FORMAT_BC4_TYPELESS:
            case DXGI_FORMAT_BC4_UNORM:
            case DXGI_FORMAT_BC4_SNORM:
            case DXGI_FORMAT_R8_UNORM:
            case DXGI_FORMAT_R8_SNORM:
            case DXGI_FORMAT_R8_UINT:
            case DXGI_FORMAT_R8_SINT:
            case DXGI_FORMAT_A8_UNORM:
            case DXGI_FORMAT_R16_UNORM:
            case DXGI_FORMAT_R16_FLOAT:
            case DXGI_FORMAT_R32_FLOAT:
                return true;
            default:
                return false;
            }
        }

        // Red and green only: a grey (r = g = b) cannot be stored, so the texture is left as it is
        [[nodiscard]] bool IsTwoChannel(DXGI_FORMAT a_format) noexcept
        {
            switch (a_format) {
            case DXGI_FORMAT_BC5_TYPELESS:
            case DXGI_FORMAT_BC5_UNORM:
            case DXGI_FORMAT_BC5_SNORM:
            case DXGI_FORMAT_R8G8_UNORM:
            case DXGI_FORMAT_R8G8_SNORM:
            case DXGI_FORMAT_R16G16_UNORM:
            case DXGI_FORMAT_R16G16_FLOAT:
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] bool IsHdr(DXGI_FORMAT a_format) noexcept
        {
            return a_format == DXGI_FORMAT_BC6H_TYPELESS || a_format == DXGI_FORMAT_BC6H_UF16 || a_format == DXGI_FORMAT_BC6H_SF16;
        }

        // Mean HSV saturation over the visible pixels of a_image (all pixels when none is visible). Out: the mean.
        [[nodiscard]] HRESULT MeanSaturation(const DirectX::Image& a_image, float& a_mean)
        {
            double visibleSum = 0.0;
            double allSum = 0.0;
            std::uint64_t visible = 0;
            std::uint64_t all = 0;
            const HRESULT result = DirectX::EvaluateImage(a_image, [&](const XMVECTOR* a_pixels, std::size_t a_width, std::size_t) {
                for (std::size_t x = 0; x < a_width; ++x) {
                    DirectX::XMFLOAT4 p;
                    DirectX::XMStoreFloat4(&p, a_pixels[x]);
                    const float high = std::max({ p.x, p.y, p.z });
                    const float low = std::min({ p.x, p.y, p.z });
                    const double saturation = high > 0.0f ? (high - low) / high : 0.0;
                    allSum += saturation;
                    ++all;
                    if (p.w > kVisibleAlpha) {
                        visibleSum += saturation;
                        ++visible;
                    }
                }
            });
            a_mean = visible > 0 ? static_cast<float>(visibleSum / static_cast<double>(visible)) :
                                   (all > 0 ? static_cast<float>(allSum / static_cast<double>(all)) : 0.0f);
            return result;
        }

        RecolourResult Fail(RecolourResult a_result, std::string a_reason)
        {
            a_result.outcome = RecolourOutcome::kFailed;
            a_result.reason = std::move(a_reason);
            return a_result;
        }
    }

    const char* TextureRoleName(TextureRole a_role) noexcept
    {
        switch (a_role) {
        case TextureRole::kDiffuse: return "diffuse";
        case TextureRole::kGlow: return "glow";
        default: return "effect source";
        }
    }

    float TextureTint(TextureRole a_role) noexcept
    {
        switch (a_role) {
        case TextureRole::kDiffuse: return kTextureTintDiffuse;
        case TextureRole::kGlow: return kTextureTintGlow;
        default: return kTextureTintEffect;
        }
    }

    std::optional<std::string> TextureDataPath(const std::string& a_slot)
    {
        std::string path = NormalizeDataPath(a_slot);
        path.erase(0, path.find_first_not_of('\\'));
        while (StartsWith(path, kDataRoot)) {
            path.erase(0, std::char_traits<char>::length(kDataRoot));
        }
        if (path.empty() || path.find(':') != std::string::npos || !EndsWith(path, kDdsSuffix)) {
            return std::nullopt;
        }
        for (std::size_t at = 0; at < path.size();) {
            const auto end = std::min(path.find('\\', at), path.size());
            if (path.compare(at, end - at, "..") == 0) {
                return std::nullopt;
            }
            at = end + 1;
        }
        return StartsWith(path, kTexturesRoot) ? path : std::string(kTexturesRoot) + path;
    }

    std::string TextureCopyPathFor(const std::string& a_textureDataPath)
    {
        return std::string(kTextureCopyRoot) + a_textureDataPath.substr(std::char_traits<char>::length(kTexturesRoot));
    }

    RecolourResult RecolourDds(const std::filesystem::path& a_source, const std::filesystem::path& a_target, float a_tint)
    {
        RecolourResult result;
        DirectX::TexMetadata meta{};
        DirectX::ScratchImage source;
        if (const HRESULT hr = DirectX::LoadFromDDSFile(a_source.c_str(), DirectX::DDS_FLAGS_PERMISSIVE, &meta, source); FAILED(hr)) {
            return Fail(result, fmt::format("not a readable DDS: {}", HrText(hr)));
        }
        const DXGI_FORMAT format = meta.format;
        result.format = fmt::format("{} {}", static_cast<int>(format), FormatName(format));
        result.mips = meta.mipLevels;
        if (IsSingleChannel(format)) {
            result.outcome = RecolourOutcome::kGreyscale;
            result.reason = "one channel (no colour stored)";
            return result;
        }
        if (IsTwoChannel(format)) {
            return Fail(result, fmt::format("{} has no blue channel, so it cannot hold grey; left as it is", result.format));
        }
        if (IsHdr(format) || DirectX::IsTypeless(format) || DirectX::IsPlanar(format) || DirectX::IsPalettized(format) ||
            DirectX::IsVideo(format)) {
            return Fail(result, fmt::format("format {} is not recoloured (HDR, typeless, planar, palettized or video)", result.format));
        }

        // The pixels in a form TransformImage can write: compressed ones decoded to 8-bit RGBA in the same colour space
        // (sRGB stays sRGB, so the encode back does no gamma conversion)
        const bool compressed = DirectX::IsCompressed(format);
        DirectX::ScratchImage decoded;
        if (compressed) {
            const DXGI_FORMAT work = DirectX::IsSRGB(format) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
            if (const HRESULT hr = DirectX::Decompress(source.GetImages(), source.GetImageCount(), meta, work, decoded); FAILED(hr)) {
                return Fail(result, fmt::format("decoding {} failed: {}", result.format, HrText(hr)));
            }
        }
        const DirectX::ScratchImage& pixels = compressed ? decoded : source;

        if (const HRESULT hr = MeanSaturation(*pixels.GetImage(0, 0, 0), result.saturation); FAILED(hr)) {
            return Fail(result, fmt::format("reading the pixels failed: {}", HrText(hr)));
        }
        if (result.saturation < kGreyscaleSaturation) {
            result.outcome = RecolourOutcome::kGreyscale;
            result.reason = fmt::format("mean saturation {:.3f} < {}", result.saturation, kGreyscaleSaturation);
            return result;
        }

        DirectX::ScratchImage grey;
        const HRESULT transformed = DirectX::TransformImage(pixels.GetImages(), pixels.GetImageCount(), pixels.GetMetadata(),
            [a_tint](XMVECTOR* a_out, const XMVECTOR* a_in, std::size_t a_width, std::size_t) {
                for (std::size_t x = 0; x < a_width; ++x) {
                    DirectX::XMFLOAT4 p;
                    DirectX::XMStoreFloat4(&p, a_in[x]);
                    const float value = std::clamp(Luminance(p.x, p.y, p.z) * a_tint, 0.0f, 1.0f);
                    a_out[x] = DirectX::XMVectorSet(value, value, value, p.w);
                }
            },
            grey);
        if (FAILED(transformed)) {
            return Fail(result, fmt::format("recolouring the pixels failed: {}", HrText(transformed)));
        }

        DirectX::ScratchImage encoded;
        const DirectX::ScratchImage* out = &grey;
        if (compressed) {
            // BC7_QUICK: mode 6 only, seconds instead of minutes for a large BC7; the other encoders ignore it
            const auto flags = static_cast<DirectX::TEX_COMPRESS_FLAGS>(DirectX::TEX_COMPRESS_DEFAULT | DirectX::TEX_COMPRESS_BC7_QUICK |
                                                                        DirectX::TEX_COMPRESS_PARALLEL);
            if (const HRESULT hr = DirectX::Compress(grey.GetImages(), grey.GetImageCount(), grey.GetMetadata(), format, flags,
                    DirectX::TEX_THRESHOLD_DEFAULT, encoded);
                FAILED(hr)) {
                return Fail(result, fmt::format("encoding {} failed: {}", result.format, HrText(hr)));
            }
            out = &encoded;
        }
        if (out->GetMetadata().format != format || out->GetMetadata().mipLevels != meta.mipLevels ||
            out->GetMetadata().arraySize != meta.arraySize) {
            return Fail(result, fmt::format("the encoded copy is format {} with {} mips, the source {} with {}",
                static_cast<int>(out->GetMetadata().format), out->GetMetadata().mipLevels, result.format, meta.mipLevels));
        }

        std::error_code ec;
        std::filesystem::create_directories(a_target.parent_path(), ec);
        if (ec) {
            return Fail(result, fmt::format("making the folder of {} failed: {}", PathText(a_target), ec.message()));
        }
        if (const HRESULT hr = DirectX::SaveToDDSFile(out->GetImages(), out->GetImageCount(), out->GetMetadata(),
                DirectX::DDS_FLAGS_NONE, a_target.c_str());
            FAILED(hr)) {
            return Fail(result, fmt::format("writing {} failed: {}", PathText(a_target), HrText(hr)));
        }
        result.bytes = std::filesystem::file_size(a_target, ec);
        result.outcome = RecolourOutcome::kRecoloured;
        return result;
    }

    std::string TextureTotalsLine(const TextureTotals& a_totals)
    {
        return fmt::format("textures  recoloured {}, skipped greyscale {}, failed {}, not in the dump {}; {} of {} slots now name a "
                           "grey copy; copies {:.1f} MiB",
            a_totals.recoloured, a_totals.greyscale, a_totals.failed, a_totals.missing, a_totals.repointed, a_totals.slots,
            static_cast<double>(a_totals.bytes) / static_cast<double>(kMiB));
    }
}
