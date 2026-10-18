// =============================================================================
// GreySelfTestStats - --self-test of the texture statistics (GreyTexStats.h): the integer ComputeStats against the double
// one it replaced, the risk check's reuse of RecolourDds's pixels, and LookStats across cache clears
// =============================================================================
// The double ComputeStats lives on here only, as the reference (ReferenceStats). The two agree on every field (alphaMean
// to a rounding step: it is not rounded) except on a texel exactly on kStatColouredSat, where the double ratio of two
// byte values rounds above 0.25 for 19 of the 63 (max, min) pairs with 4 x (max - min) = max: the integer compare says
// not coloured, as the rule reads.

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include "GreySelfTest.h"
#include "GreyTexStats.h"
#include "GreyTexture.h"

#include <DirectXTex.h>

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <random>
#include <vector>

namespace GreyBuild::SelfTest
{
    namespace
    {
        constexpr double kByteMax = 255.0;
        constexpr double kAlphaMeanTolerance = 1e-12;  // alphaMean is not rounded: the double sum's order shows in its last bits
        constexpr std::uint32_t kSeed = 20261018;      // std::mt19937 is specified bit for bit: the same images everywhere
        constexpr std::size_t kSmallW = 37;
        constexpr std::size_t kSmallH = 23;
        constexpr std::size_t kLargeW = 640;           // 307200 texels: over GreyTexStats.cpp's kParallelTexels (OpenMP rows)
        constexpr std::size_t kLargeH = 480;
        constexpr std::size_t kLine = 50;              // the 1 x N and N x 1 images
        constexpr auto kLoadFlags = static_cast<DirectX::DDS_FLAGS>(DirectX::DDS_FLAGS_PERMISSIVE | DirectX::DDS_FLAGS_EXPAND_LUMINANCE);

        // The ComputeStats this builder had before the integer sums (double per texel, one thread), kept as the reference
        [[nodiscard]] TexStats ReferenceStats(const RgbaImage& a_image)
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

        [[nodiscard]] bool OnSatLine(std::uint8_t a_r, std::uint8_t a_g, std::uint8_t a_b) noexcept
        {
            const unsigned high = std::max({ a_r, a_g, a_b });
            const unsigned low = std::min({ a_r, a_g, a_b });
            return high > 0 && 4 * (high - low) == high;
        }

        // Seeded bytes; a texel exactly on kStatColouredSat is drawn again (the one place the two may differ, checked apart)
        [[nodiscard]] RgbaImage RandomImage(std::size_t a_w, std::size_t a_h, std::mt19937& a_random, bool a_transparent = false)
        {
            RgbaImage image{ a_w, a_h, std::vector<std::uint8_t>(a_w * a_h * 4), false };
            for (std::size_t i = 0; i < a_w * a_h; ++i) {
                std::uint8_t* p = image.pixels.data() + i * 4;
                do {
                    for (int c = 0; c < 3; ++c) {
                        p[c] = static_cast<std::uint8_t>(a_random() & 0xFF);
                    }
                } while (OnSatLine(p[0], p[1], p[2]));
                p[3] = a_transparent ? 0 : static_cast<std::uint8_t>(a_random() & 0xFF);
            }
            return image;
        }

        [[nodiscard]] RgbaImage Texel(std::uint8_t a_r, std::uint8_t a_g, std::uint8_t a_b, std::uint8_t a_a)
        {
            return { 1, 1, { a_r, a_g, a_b, a_a }, false };
        }

        [[nodiscard]] bool SameStats(const TexStats& a_left, const TexStats& a_right, std::string& a_why)
        {
            const auto field = [&](const char* a_name, double a_l, double a_r, double a_tolerance) {
                if (std::fabs(a_l - a_r) > a_tolerance) {
                    a_why += fmt::format(" {} {} vs {}", a_name, a_l, a_r);
                }
            };
            a_why.clear();
            field("meanRgb.r", a_left.meanRgb.r, a_right.meanRgb.r, 0.0);
            field("meanRgb.g", a_left.meanRgb.g, a_right.meanRgb.g, 0.0);
            field("meanRgb.b", a_left.meanRgb.b, a_right.meanRgb.b, 0.0);
            field("meanSat", a_left.meanSat, a_right.meanSat, 0.0);
            field("colouredFrac", a_left.colouredFrac, a_right.colouredFrac, 0.0);
            field("energyMean", a_left.energyMean, a_right.energyMean, 0.0);
            field("detail", a_left.detail, a_right.detail, 0.0);
            field("alphaMean", a_left.alphaMean, a_right.alphaMean, kAlphaMeanTolerance);
            return a_why.empty();
        }

        void CheckAgainstReference(Checker& a_check, const RgbaImage& a_image, const std::string& a_what)
        {
            std::string why;
            const TexStats stats = ComputeStats(a_image);
            a_check.Check(SameStats(stats, ReferenceStats(a_image), why), fmt::format("stats: {} ({} x {}): the integer ComputeStats "
                "equals the double one{}", a_what, a_image.width, a_image.height, why.empty() ? std::string() : ":" + why));
            const TexStats colour = ComputeColourStats(a_image);
            a_check.Check(colour.colouredFrac == stats.colouredFrac && colour.meanSat == stats.meanSat && colour.meanRgb.r == stats.meanRgb.r &&
                              colour.alphaMean == stats.alphaMean && colour.detail == 0.0 && colour.energyMean == 0.0,
                fmt::format("stats: {}: ComputeColourStats is ComputeStats's first pass (no detail, no energy)", a_what));
        }

        void CheckIntegerStats(Checker& a_check)
        {
            std::mt19937 random(kSeed);
            CheckAgainstReference(a_check, RandomImage(kSmallW, kSmallH, random), "seeded bytes");
            const RgbaImage large = RandomImage(kLargeW, kLargeH, random);
            CheckAgainstReference(a_check, large, "seeded bytes, rows in parallel");
            std::string why;
            a_check.Check(SameStats(ComputeStats(large), ComputeStats(large), why), "stats: the parallel sums give the same result twice");
            CheckAgainstReference(a_check, Texel(255, 128, 0, 255), "1 x 1 (no texel with both neighbours: detail and energy 0)");
            CheckAgainstReference(a_check, RandomImage(1, kLine, random), "1 x N");
            CheckAgainstReference(a_check, RandomImage(kLine, 1, random), "N x 1");
            const RgbaImage clear = RandomImage(8, 8, random, true);
            CheckAgainstReference(a_check, clear, "fully transparent");
            const TexStats none = ComputeStats(clear);
            a_check.Check(none.alphaMean == 0.0 && none.colouredFrac == 0.0 && none.meanRgb.r == 0.0 && none.energyMean == 0.0 &&
                              none.detail == 0.0, "stats: fully transparent: no lit texel, mean colour 0, alpha 0, energy 0");
            const RgbaImage empty{ 0, 0, {}, false };
            const TexStats nothing = ComputeStats(empty);
            a_check.Check(nothing.alphaMean == 0.0 && nothing.energyMean == 0.0 && nothing.detail == 0.0 && nothing.colouredFrac == 0.0,
                "stats: an image of no texels gives zeros (no division by zero)");

            // Lit: alpha x max > 0.1 x 255 x 255 = 6502.5, which no byte product equals: the nearest are 6501 and 6507
            const TexStats under = ComputeStats(Texel(33, 0, 0, 197));   // 197 x 33 = 6501
            const TexStats over = ComputeStats(Texel(241, 0, 0, 27));    // 27 x 241 = 6507
            a_check.Check(under.colouredFrac == 0.0 && over.colouredFrac == 1.0 &&
                              ReferenceStats(Texel(33, 0, 0, 197)).colouredFrac == 0.0 && ReferenceStats(Texel(241, 0, 0, 27)).colouredFrac == 1.0,
                "stats: lit threshold: alpha x max 6501 is not lit, 6507 is (a red texel: coloured), as the double test said");
            // Coloured: S = (max - min) / max exactly 0.25 is not over kStatColouredSat; one step either side
            const TexStats onLine = ComputeStats(Texel(92, 69, 69, 255));
            const TexStats belowLine = ComputeStats(Texel(93, 70, 70, 255));
            const TexStats aboveLine = ComputeStats(Texel(92, 68, 68, 255));
            a_check.Check(onLine.colouredFrac == 0.0 && belowLine.colouredFrac == 0.0 && aboveLine.colouredFrac == 1.0,
                "stats: coloured threshold: S exactly 0.25 (92, 69) is not coloured, (93, 70) neither, (92, 68) is");
            a_check.Check(ReferenceStats(Texel(92, 69, 69, 255)).colouredFrac == 1.0,
                "stats: the double test it replaced called (92, 69) coloured (its ratio rounds above 0.25): the one known change");
            int lineDiffers = 0;
            for (unsigned high = 1; high <= 255; ++high) {
                for (unsigned low = 0; low <= high; ++low) {
                    if (4 * (high - low) == high) {
                        const auto texel = Texel(static_cast<std::uint8_t>(high), static_cast<std::uint8_t>(low), static_cast<std::uint8_t>(low), 255);
                        lineDiffers += ComputeStats(texel).colouredFrac != ReferenceStats(texel).colouredFrac ? 1 : 0;
                    }
                }
            }
            a_check.Check(lineDiffers == 19, fmt::format("stats: of the 63 (max, min) pairs exactly on S 0.25, the double test "
                "differs on {} (expected 19)", lineDiffers));
        }

        [[nodiscard]] bool SameImage(const RgbaImage& a_left, const RgbaImage& a_right)
        {
            return a_left.width == a_right.width && a_left.height == a_right.height && a_left.srgb == a_right.srgb &&
                   a_left.pixels == a_right.pixels;
        }

        // The risk check's image: RecolourDds decodes a BCn source whole (mip 0 then already RGBA8), an uncompressed one
        // it keeps as loaded; ToRgba of that mip 0 must be ReadRgba's image
        void CheckRiskImage(Checker& a_check, const std::filesystem::path& a_dir, unsigned a_format, bool a_orange, const char* a_name)
        {
            const auto file = a_dir / fmt::format("{}{}.dds", a_name, a_orange ? "_orange" : "_grey");
            if (!WriteTestTexture(file, a_format, a_orange)) {
                a_check.Check(false, fmt::format("stats: {} test texture written", a_name));
                return;
            }
            std::string why;
            const auto old = ReadRgba(file, why);
            DirectX::TexMetadata meta{};
            DirectX::ScratchImage source;
            DirectX::ScratchImage decoded;
            bool loaded = SUCCEEDED(DirectX::LoadFromDDSFile(file.c_str(), kLoadFlags, &meta, source));
            const bool compressed = loaded && DirectX::IsCompressed(meta.format);
            if (compressed) {
                const DXGI_FORMAT work = DirectX::IsSRGB(meta.format) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
                loaded = SUCCEEDED(DirectX::Decompress(source.GetImages(), source.GetImageCount(), meta, work, decoded));
            }
            const auto reused = loaded ? ToRgba(*(compressed ? decoded : source).GetImage(0, 0, 0), DirectX::IsSRGB(meta.format), why) :
                                         std::nullopt;
            a_check.Check(old && reused && SameImage(*old, *reused), fmt::format("stats: {} {}: the risk check's image from the "
                "decoded pixels is ReadRgba's, byte for byte", a_name, a_orange ? "orange" : "grey"));
            if (!old || !reused) {
                return;
            }
            const TexStats was = ReferenceStats(*old);
            const TexStats now = ComputeColourStats(*reused);
            const auto result = RecolourDds(file, a_dir / "out" / file.filename(), 1.0f);
            a_check.Check(now.colouredFrac == was.colouredFrac && now.meanSat == was.meanSat &&
                              result.riskColoured == IsColouredTexture(was) && result.riskColoured == a_orange,
                fmt::format("stats: {} {}: RecolourDds's risk verdict ({}) is the old second read's (colouredFrac {}, meanSat {})",
                    a_name, a_orange ? "orange" : "grey", result.riskColoured, was.colouredFrac, was.meanSat));
        }

        void CheckRiskReuse(Checker& a_check, const std::filesystem::path& a_dir)
        {
            CheckRiskImage(a_check, a_dir, DXGI_FORMAT_B8G8R8A8_UNORM, true, "B8G8R8A8");
            CheckRiskImage(a_check, a_dir, DXGI_FORMAT_B8G8R8A8_UNORM, false, "B8G8R8A8");
            CheckRiskImage(a_check, a_dir, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, true, "B8G8R8A8_SRGB");
            CheckRiskImage(a_check, a_dir, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, true, "R8G8B8A8_SRGB");
            CheckRiskImage(a_check, a_dir, DXGI_FORMAT_BC3_UNORM, true, "BC3");
            // A format the conversion refuses: no image and a reason (RecolourDds fails the texture with it, never "not coloured")
            std::vector<std::uint8_t> bytes(16, 0);
            DirectX::Image palettized{};
            palettized.width = 4;
            palettized.height = 4;
            palettized.format = DXGI_FORMAT_P8;
            palettized.rowPitch = 4;
            palettized.slicePitch = bytes.size();
            palettized.pixels = bytes.data();
            std::string why;
            bool transient = true;
            const auto refused = ToRgba(palettized, false, why, &transient);
            a_check.Check(!refused && !transient && why.find("decoding format") != std::string::npos,
                fmt::format("stats: ToRgba of a format it cannot convert: none, not transient, why '{}'", why));
        }

        void CheckLookStats(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto root = a_dir / "lookstats";
            const auto copies = a_dir / "lookstats-out";
            std::filesystem::remove_all(root);
            std::filesystem::remove_all(copies);
            const std::string kept = "textures\\lookstats\\kept.dds";
            const std::string other = "textures\\lookstats\\other.dds";
            const std::string gone = "textures\\lookstats\\gone.dds";
            const std::string copy = std::string(kTextureCopyRoot) + "0123456789abcdef\\copy.dds";
            const bool written = WriteTestTexture(root / kept, DXGI_FORMAT_BC3_UNORM, true) &&
                                 WriteTestTexture(root / other, DXGI_FORMAT_BC3_UNORM, false);
            TextureSource source({ root }, { { gone, "does not open (last error none)" } }, copies);
            int gateCalls = 0;
            source.SetReadGate([&](const std::filesystem::path&, std::string&) {
                ++gateCalls;
                return true;
            });

            const auto first = source.Look(kept);
            const TexStats firstStats = first.stats ? *first.stats : TexStats{};
            const auto missing = source.Look(gone);
            source.Clear();
            const auto hit = source.LookStats(kept);
            std::string why;
            a_check.Check(written && first.status == TextureRead::kOk && hit.status == TextureRead::kOk && hit.stats && !hit.image &&
                              SameStats(*hit.stats, firstStats, why) && hit.file == first.file && source.Counts().decodes == 1 &&
                              source.Counts().statsAnswers == 1 && gateCalls == 1,
                "LookStats: after a Clear the statistics come back without a decode or a gate call (image null)");
            const auto missingAgain = source.LookStats(gone);
            a_check.Check(missing.status == TextureRead::kMissing && missingAgain.status == TextureRead::kMissing &&
                              missingAgain.why == missing.why && !missingAgain.stats && source.Counts().statsAnswers == 2,
                fmt::format("LookStats: a missing texture keeps its status and why across a Clear ('{}')", missingAgain.why));
            const auto miss = source.LookStats(other);
            a_check.Check(miss.status == TextureRead::kOk && miss.image && miss.stats && source.Counts().decodes == 2 && gateCalls == 2 &&
                              source.Counts().paths == 2,
                "LookStats: a path never looked up is read as Look reads it (the gate, a decode, the image)");
            const auto cached = source.LookStats(other);
            a_check.Check(cached.image == miss.image && source.Counts().decodes == 2 && source.Counts().statsAnswers == 2,
                "LookStats: a path still cached is the cache's look (its image too)");
            source.Clear();
            const auto reread = source.Look(kept);
            a_check.Check(reread.image && reread.stats == hit.stats && SameStats(*reread.stats, firstStats, why) &&
                              source.Counts().decodes == 3 && source.Counts().paths == 2,
                "LookStats: Look after a Clear decodes again for the pixels and keeps the statistics it had");

            // Our copies may appear later in the run: never answered from an earlier look
            const auto before = source.LookStats(copy);
            const bool made = WriteTestTexture(copies / copy, DXGI_FORMAT_BC3_UNORM, false);
            source.Clear();
            const auto after = source.LookStats(copy);
            a_check.Check(before.status == TextureRead::kUnread && made && after.status == TextureRead::kOk && after.image,
                "LookStats: one of our copies is read again after a Clear (written meanwhile: found)");

            // A gated (transient) read is not remembered: the next look asks the gate again
            TextureSource refusing({ root }, {});
            int refusals = 0;
            refusing.SetReadGate([&](const std::filesystem::path&, std::string& a_why) {
                ++refusals;
                a_why = "over the budget (self-test)";
                return false;
            });
            const auto gated = refusing.LookStats(kept);
            const auto gatedAgain = refusing.LookStats(kept);
            a_check.Check(gated.transient && gated.gated && gatedAgain.gated && refusals == 2 && refusing.Counts().statsAnswers == 0 &&
                              refusing.Counts().decodes == 0,
                "LookStats: a read the gate refused is not remembered (the gate is asked again, nothing decoded)");
        }
    }

    void CheckTexStats(Checker& a_check, const std::filesystem::path& a_dir)
    {
        const auto dir = a_dir / "stats";
        std::filesystem::create_directories(dir);
        CheckIntegerStats(a_check);
        CheckRiskReuse(a_check, dir);
        CheckLookStats(a_check, dir);
    }
}
