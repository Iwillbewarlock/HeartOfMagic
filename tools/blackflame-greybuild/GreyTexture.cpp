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
#include "GreyGpuEncoder.h"
#include "GreyTexStats.h"

#include "../../plugins/BlackFlame/src/DataPathRule.h"  // T7: the DLL's own path rule, not a copy

#include <DirectXTex.h>

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <system_error>

namespace GreyBuild
{
    namespace
    {
        constexpr const char* kDdsSuffix = ".dds";
        // A pixel counts for the chroma measure when its alpha is above this (an effect texture's fill under zero alpha
        // is often coloured and never seen)
        constexpr float kVisibleAlpha = 1.0f / 255.0f;
        // T5: legacy luminance formats (L8, A8L8, L16) come in as RGBA with r = g = b, so they read as grey and are left
        constexpr auto kLoadFlags = static_cast<DirectX::DDS_FLAGS>(DirectX::DDS_FLAGS_PERMISSIVE | DirectX::DDS_FLAGS_EXPAND_LUMINANCE);
        constexpr std::uint64_t kMiB = 1024ull * 1024ull;
        // The CPU overload's float: BC1's alpha threshold (DirectX::TEX_THRESHOLD_DEFAULT, 0.5; the other encoders ignore
        // it). The GPU overload's float in the same place is the BC7 alpha weight (kGpuAlphaWeight, GreyGpuEncoder.h)
        constexpr float kCpuEncodeThreshold = DirectX::TEX_THRESHOLD_DEFAULT;

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

        struct ChromaMeasure
        {
            float mean = 0.0f;   // mean chroma of the measured pixels
            float share = 0.0f;  // U4: share of them with a chroma of kColouredChroma or more
            std::uint64_t lit = 0;
        };

        // T2: chroma (brightest - darkest channel) over the pixels that are lit (brightest channel at kLitFloor or above)
        // and, unless a_ignoreAlpha, visible (alpha). None lit: 0 (grey). A dark texture with small orange specks is judged
        // by its specks; the 5:6:5 noise of a dark grey BC1 stays under the line (chroma of a few 8-bit steps).
        [[nodiscard]] HRESULT MeasureChroma(const DirectX::Image& a_image, bool a_ignoreAlpha, ChromaMeasure& a_out)
        {
            double sum = 0.0;
            std::uint64_t lit = 0;
            std::uint64_t coloured = 0;
            const HRESULT result = DirectX::EvaluateImage(a_image, [&](const XMVECTOR* a_pixels, std::size_t a_width, std::size_t) {
                for (std::size_t x = 0; x < a_width; ++x) {
                    DirectX::XMFLOAT4 p;
                    DirectX::XMStoreFloat4(&p, a_pixels[x]);
                    const float high = std::max({ p.x, p.y, p.z });
                    if ((!a_ignoreAlpha && p.w <= kVisibleAlpha) || high < kLitFloor) {
                        continue;
                    }
                    const float chroma = high - std::min({ p.x, p.y, p.z });
                    sum += chroma;
                    coloured += chroma >= kColouredChroma ? 1 : 0;
                    ++lit;
                }
            });
            a_out.lit = lit;
            a_out.mean = lit > 0 ? static_cast<float>(sum / static_cast<double>(lit)) : 0.0f;
            a_out.share = lit > 0 ? static_cast<float>(static_cast<double>(coloured) / static_cast<double>(lit)) : 0.0f;
            return result;
        }

        // U5: the visible, lit pixels; when there are none (an additive texture whose alpha is zero everywhere still
        // shows its colour), the lit pixels whatever their alpha
        [[nodiscard]] HRESULT MeasureTexture(const DirectX::Image& a_image, ChromaMeasure& a_out, bool& a_alphaIgnored)
        {
            a_alphaIgnored = false;
            if (const HRESULT hr = MeasureChroma(a_image, false, a_out); FAILED(hr) || a_out.lit > 0) {
                return hr;
            }
            a_alphaIgnored = true;
            return MeasureChroma(a_image, true, a_out);
        }

        RecolourResult Fail(RecolourResult a_result, std::string a_reason, bool a_transient = false)
        {
            a_result.outcome = RecolourOutcome::kFailed;
            a_result.reason = std::move(a_reason);
            a_result.transient = a_transient;
            return a_result;
        }
    }

    bool IsTransientResult(long a_result) noexcept
    {
        constexpr DWORD kTransientErrors[] = { ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND, ERROR_ACCESS_DENIED, ERROR_SHARING_VIOLATION,
            ERROR_LOCK_VIOLATION, ERROR_READ_FAULT, ERROR_WRITE_FAULT, ERROR_DISK_FULL, ERROR_HANDLE_DISK_FULL, ERROR_NOT_ENOUGH_MEMORY,
            ERROR_OUTOFMEMORY, ERROR_COMMITMENT_LIMIT, ERROR_NOT_ENOUGH_QUOTA, ERROR_NO_SYSTEM_RESOURCES, ERROR_TOO_MANY_OPEN_FILES,
            ERROR_USER_MAPPED_FILE, ERROR_DELETE_PENDING, ERROR_IO_DEVICE, ERROR_CRC, ERROR_WORKING_SET_QUOTA, ERROR_PAGEFILE_QUOTA,
            ERROR_NETNAME_DELETED, ERROR_SEM_TIMEOUT };  // not E_FAIL: DirectXTex reads a bad header as E_FAIL too
        if (a_result == E_OUTOFMEMORY) {
            return true;
        }
        return std::any_of(std::begin(kTransientErrors), std::end(kTransientErrors),
            [&](DWORD a_code) { return a_result == HRESULT_FROM_WIN32(a_code); });
    }

    bool IsTransientWriteResult(long a_result) noexcept
    {
        return a_result != E_INVALIDARG && a_result != HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }

    const char* TextureRoleName(TextureRole a_role) noexcept
    {
        switch (a_role) {
        case TextureRole::kDiffuse: return "diffuse";
        case TextureRole::kGlow: return "glow";
        default: return "effect source";
        }
    }

    float TextureTint(TextureRole a_role, float a_diffuseTint) noexcept
    {
        switch (a_role) {
        case TextureRole::kDiffuse: return a_diffuseTint;
        case TextureRole::kGlow: return kTextureTintGlow;
        default: return kTextureTintEffect;
        }
    }

    std::optional<std::string> TextureDataPath(const std::string& a_slot)
    {
        auto path = BlackFlame::DataPathRule::Normalize(a_slot, BlackFlame::DataPathRule::kTexturesRoot);
        if (!path || !EndsWith(*path, kDdsSuffix)) {
            return std::nullopt;
        }
        return path;
    }

    std::string TextureCopyPathFor(const std::string& a_textureDataPath, float a_tint)
    {
        const long percent = std::lround(a_tint * static_cast<float>(kTintPercent));
        return fmt::format("{}{}{}\\{}", kTextureCopyRoot, kTintFolder, percent,
            a_textureDataPath.substr(std::char_traits<char>::length(kTexturesRoot)));
    }

    RecolourResult RecolourDds(const std::filesystem::path& a_source, const std::filesystem::path& a_target, float a_tint,
        GpuEncoder* a_gpu)
    {
        RecolourResult result;
        DirectX::TexMetadata meta{};
        DirectX::ScratchImage source;
        if (const HRESULT hr = DirectX::LoadFromDDSFile(a_source.c_str(), kLoadFlags, &meta, source); FAILED(hr)) {
            return Fail(result, fmt::format("not a readable DDS: {}", HrText(hr)), IsTransientResult(hr));
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
        // T17: float formats too (their values run past 1; a clamp would cut the highlights)
        if (IsHdr(format) || DirectX::FormatDataType(format) == DirectX::FORMAT_TYPE_FLOAT || DirectX::IsTypeless(format) ||
            DirectX::IsPlanar(format) || DirectX::IsPalettized(format) || DirectX::IsVideo(format)) {
            return Fail(result, fmt::format("format {} is not recoloured (HDR or float, typeless, planar, palettized or video)",
                result.format));
        }

        // The pixels in a form TransformImage can write: compressed ones decoded to 8-bit RGBA in the same colour space
        // (sRGB stays sRGB, so the encode back does no gamma conversion)
        const bool compressed = DirectX::IsCompressed(format);
        DirectX::ScratchImage decoded;
        if (compressed) {
            const DXGI_FORMAT work = DirectX::IsSRGB(format) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
            if (const HRESULT hr = DirectX::Decompress(source.GetImages(), source.GetImageCount(), meta, work, decoded); FAILED(hr)) {
                return Fail(result, fmt::format("decoding {} failed: {}", result.format, HrText(hr)), IsTransientResult(hr));
            }
        }
        const DirectX::ScratchImage& pixels = compressed ? decoded : source;

        ChromaMeasure measure;
        if (const HRESULT hr = MeasureTexture(*pixels.GetImage(0, 0, 0), measure, result.alphaIgnored); FAILED(hr)) {
            return Fail(result, fmt::format("reading the pixels failed: {}", HrText(hr)));
        }
        result.chroma = measure.mean;
        result.colouredShare = measure.share;
        // B: never "already grey" when the visual-risk check would call it coloured (TEX_COLOUR), so the check can only
        // fire on a texture that could not be done (a faint tint on many dark texels: fxbubble01's 34%). Its mip 0 as ReadRgba
        // gives it, from the pixels decoded above (no second read); a conversion that fails fails the texture
        std::string riskWhy;
        bool riskTransient = false;
        const auto rgba = ToRgba(*pixels.GetImage(0, 0, 0), DirectX::IsSRGB(format), riskWhy, &riskTransient);
        if (!rgba) {
            return Fail(result, fmt::format("reading the pixels for the visual-risk check failed: {}", riskWhy), riskTransient);
        }
        result.riskColoured = IsColouredTexture(ComputeColourStats(*rgba));
        if (result.chroma < kGreyscaleChroma && result.colouredShare <= kColouredShare && !result.riskColoured &&
            std::abs(a_tint - 1.0f) < kTintOneEpsilon) {
            result.outcome = RecolourOutcome::kGreyscale;
            result.reason = fmt::format("mean chroma of the lit{} pixels {:.3f} < {} and {:.2f}% of them at chroma {} or more "
                                        "(<= {:.2f}%)", result.alphaIgnored ? "" : ", visible", result.chroma, kGreyscaleChroma,
                result.colouredShare * 100.0f, kColouredChroma, kColouredShare * 100.0f);
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
            return Fail(result, fmt::format("recolouring the pixels failed: {}", HrText(transformed)), IsTransientResult(transformed));
        }

        DirectX::ScratchImage encoded;
        const DirectX::ScratchImage* out = &grey;
        if (compressed && a_gpu && (format == DXGI_FORMAT_BC7_UNORM || format == DXGI_FORMAT_BC7_UNORM_SRGB)) {
            std::string gpuWhy;
            if (a_gpu->Encode(grey, static_cast<unsigned>(format), encoded, gpuWhy)) {
                const auto check = CheckGpuOutput(grey, encoded);
                if (check.ok) {
                    result.gpu = true;
                    result.gpuNote = fmt::format("worst subresource {} at {:.1f} dB, largest alpha difference {}", check.worstImage,
                        check.worstPsnr, check.maxAlphaDiff);
                    out = &encoded;
                } else {
                    a_gpu->NoteRejected();
                    result.gpuNote = fmt::format("the GPU's result is over a limit ({}): encoded on the CPU", check.why);
                    encoded.Release();
                }
            } else if (!gpuWhy.empty()) {
                result.gpuNote = gpuWhy + ": encoded on the CPU";
            }
        }
        if (compressed && !result.gpu) {
            // BC7_QUICK: mode 6 only, seconds instead of minutes for a large BC7; the other encoders ignore it
            const auto flags = static_cast<DirectX::TEX_COMPRESS_FLAGS>(DirectX::TEX_COMPRESS_DEFAULT | DirectX::TEX_COMPRESS_BC7_QUICK |
                                                                        DirectX::TEX_COMPRESS_PARALLEL);
            if (const HRESULT hr = DirectX::Compress(grey.GetImages(), grey.GetImageCount(), grey.GetMetadata(), format, flags,
                    kCpuEncodeThreshold, encoded);
                FAILED(hr)) {
                return Fail(result, fmt::format("encoding {} failed: {}", result.format, HrText(hr)), IsTransientResult(hr));
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
            return Fail(result, fmt::format("making the folder of {} failed: {}", PathText(a_target), ec.message()), true);
        }
        if (const HRESULT hr = DirectX::SaveToDDSFile(out->GetImages(), out->GetImageCount(), out->GetMetadata(),
                DirectX::DDS_FLAGS_NONE, a_target.c_str());
            FAILED(hr)) {
            return Fail(result, fmt::format("writing {} failed: {}", PathText(a_target), HrText(hr)), IsTransientWriteResult(hr));
        }
        const auto written = std::filesystem::file_size(a_target, ec);
        if (ec) {
            return Fail(result, fmt::format("{} was written but cannot be read back: {}", PathText(a_target), ec.message()), true);
        }
        result.bytes = written;
        result.outcome = RecolourOutcome::kRecoloured;
        return result;
    }

    std::string TextureTotalsLine(const TextureTotals& a_totals)
    {
        return fmt::format("textures  recoloured {}, skipped greyscale {}, failed {}, not in the dump {}; {} of {} slots now name a "
                           "grey copy ({} of {} decal texture set slots); glow and Own_Emit shader emissive colours greyed {}; copies "
                           "{:.1f} MiB",
            a_totals.recoloured, a_totals.greyscale, a_totals.failed, a_totals.missing, a_totals.repointed, a_totals.slots,
            a_totals.decalRepointed, a_totals.decalSlots, a_totals.emissiveGreyed, static_cast<double>(a_totals.bytes) / static_cast<double>(kMiB));
    }
}
