// =============================================================================
// GreyKeyedBases - key mode's effect shader base textures (contract 8): the fire test and each grey copy's dds line
// (GreyKeyed.h)
// =============================================================================
// One pass after the palettes, before the scheduled objects. Per keyed request entry:
//   reusable line in staging           counted as reused (an ok line), or as its terminal outcome
//   in the not-fire cache              counted as not fire, nothing read (staging efshbase-notfire.txt, builder only)
//   not in the dump                    textures[] unread: counted (the game reads it again next start); else a failed line
//   the dump's bytes not the key's     a failed line (the key is EfshBaseKeyString over those bytes)
//   over the job memory cap            a too_large line with the cap; the process over it: a failed line
//   does not load / decode             the machine's fault (IsTransientResult): a failed line; the file's: counted, no line
//   a format left out                  counted and logged, no line (sRGB counted apart: its transfer is not decided yet)
//   not fire (EfshBaseFireShare)       its top mip alone decoded and judged; counted, no line, the key into the cache
//   past the copies cap                the reused copies and this run's together over kMaxBaseCopyBytes: counted, logged
//   fire                               every image greyed (RGBA8, alpha untouched), saved with every mip as <copy>.tmp,
//                                      read back and checked, renamed, an ok dds line at CopyPath(textures\, key16, source)
// An entry with no key (missing, notdumped) is counted only: as unread when textures[] says the game could not read it.

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include "GreyKeyed.h"

#include "../../plugins/BlackFlame/src/DataPathRule.h"

#include <DirectXTex.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fmt/format.h>
#include <fstream>
#include <iostream>
#include <set>
#include <system_error>

namespace GreyBuild
{
    namespace
    {
        namespace CC = BlackFlame::CopyContract;
        namespace Rule = BlackFlame::DataPathRule;

        constexpr double kChannelMax = 255.0;
        constexpr float kByteMax = 255.0f;
        constexpr double kHueSextant = 6.0;
        constexpr std::size_t kRgba = 4;
        constexpr DXGI_FORMAT kWorkFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        constexpr auto kLoadFlags = static_cast<DirectX::DDS_FLAGS>(DirectX::DDS_FLAGS_PERMISSIVE | DirectX::DDS_FLAGS_EXPAND_LUMINANCE);

        [[nodiscard]] std::string HrText(HRESULT a_result)
        {
            return fmt::format("HRESULT 0x{:08X} ({})", static_cast<std::uint32_t>(a_result), std::system_category().message(a_result));
        }

        /// HSV hue in 0..1 of a texel whose brightest channel a_max is above its darkest a_min
        [[nodiscard]] double Hue(double a_r, double a_g, double a_b, double a_max, double a_min) noexcept
        {
            const double range = a_max - a_min;
            double sextant = 0.0;
            if (a_max == a_r) {
                sextant = (a_g - a_b) / range;
            } else if (a_max == a_g) {
                sextant = (a_b - a_r) / range + 2.0;
            } else {
                sextant = (a_r - a_g) / range + 4.0;
            }
            const double hue = sextant / kHueSextant;
            return hue < 0.0 ? hue + 1.0 : hue;
        }

        /// The colour formats a base copy is made from (decoded or converted to kWorkFormat, alpha exact); sRGB ones are
        /// told apart (left out for now), every other format left out
        enum class Accept
        {
            kYes,
            kSrgb,
            kNo
        };

        [[nodiscard]] Accept AcceptFormat(DXGI_FORMAT a_format) noexcept
        {
            switch (a_format) {
            case DXGI_FORMAT_BC1_UNORM:
            case DXGI_FORMAT_BC2_UNORM:
            case DXGI_FORMAT_BC3_UNORM:
            case DXGI_FORMAT_BC7_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_B8G8R8A8_UNORM:
            case DXGI_FORMAT_B8G8R8X8_UNORM:
            case DXGI_FORMAT_B5G6R5_UNORM:
            case DXGI_FORMAT_B5G5R5A1_UNORM:
            case DXGI_FORMAT_B4G4R4A4_UNORM:
                return Accept::kYes;
            case DXGI_FORMAT_BC1_UNORM_SRGB:
            case DXGI_FORMAT_BC2_UNORM_SRGB:
            case DXGI_FORMAT_BC3_UNORM_SRGB:
            case DXGI_FORMAT_BC7_UNORM_SRGB:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
                return Accept::kSrgb;
            default:
                return Accept::kNo;
            }
        }

        /// One kWorkFormat image's pixels as an RgbaImage (rows packed)
        [[nodiscard]] RgbaImage TopImage(const DirectX::Image& a_image)
        {
            RgbaImage out;
            out.width = a_image.width;
            out.height = a_image.height;
            out.pixels.resize(a_image.width * a_image.height * kRgba);
            for (std::size_t y = 0; y < a_image.height; ++y) {
                std::memcpy(out.pixels.data() + y * a_image.width * kRgba, a_image.pixels + y * a_image.rowPitch, a_image.width * kRgba);
            }
            return out;
        }

        /// Every pixel of every image: RGB to its luminance (the builder's Rec. 709 Luminance on the encoded bytes,
        /// rounded), alpha untouched
        void GreyChain(DirectX::ScratchImage& a_chain) noexcept
        {
            for (std::size_t i = 0; i < a_chain.GetImageCount(); ++i) {
                const auto& image = a_chain.GetImages()[i];
                for (std::size_t y = 0; y < image.height; ++y) {
                    auto* row = image.pixels + y * image.rowPitch;
                    for (std::size_t x = 0; x < image.width; ++x) {
                        auto* p = row + x * kRgba;
                        const float lum = Luminance(p[0] / kByteMax, p[1] / kByteMax, p[2] / kByteMax);
                        const auto grey = static_cast<std::uint8_t>(std::clamp(std::lround(lum * kByteMax), 0L, 255L));
                        p[0] = grey;
                        p[1] = grey;
                        p[2] = grey;
                    }
                }
            }
        }

        /// The written file read back against a_grey (the decoded source with only its RGB greyed): its format, size, mip
        /// and image count, every byte (so alpha exactly the source's) and R = G = B; "" when it holds, else why
        [[nodiscard]] std::string CheckWritten(const std::filesystem::path& a_file, const DirectX::ScratchImage& a_grey)
        {
            DirectX::TexMetadata meta{};
            DirectX::ScratchImage back;
            if (const HRESULT hr = DirectX::LoadFromDDSFile(a_file.c_str(), DirectX::DDS_FLAGS_NONE, &meta, back); FAILED(hr)) {
                return fmt::format("the copy does not read back: {}", HrText(hr));
            }
            const auto& want = a_grey.GetMetadata();
            if (meta.format != kWorkFormat || meta.width != want.width || meta.height != want.height || meta.mipLevels != want.mipLevels ||
                back.GetImageCount() != a_grey.GetImageCount()) {
                return fmt::format("the copy reads back as format {} {}x{} with {} mips, not RGBA8 {}x{} with {}", static_cast<int>(meta.format),
                    meta.width, meta.height, meta.mipLevels, want.width, want.height, want.mipLevels);
            }
            for (std::size_t i = 0; i < back.GetImageCount(); ++i) {
                const auto& got = back.GetImages()[i];
                const auto& expected = a_grey.GetImages()[i];
                for (std::size_t y = 0; y < got.height; ++y) {
                    const auto* row = got.pixels + y * got.rowPitch;
                    if (std::memcmp(row, expected.pixels + y * expected.rowPitch, got.width * kRgba) != 0) {
                        return fmt::format("image {} row {} differs from the grey chain (alpha not exact)", i, y);
                    }
                    for (std::size_t x = 0; x < got.width; ++x) {
                        if (row[x * kRgba] != row[x * kRgba + 1] || row[x * kRgba + 1] != row[x * kRgba + 2]) {
                            return fmt::format("image {} texel ({}, {}) is not grey", i, x, y);
                        }
                    }
                }
            }
            return {};
        }

        [[nodiscard]] CC::ProgressLine DdsLine(std::uint64_t a_key, CC::Outcome a_outcome, const std::string& a_reason)
        {
            CC::ProgressLine line;
            line.kind = CC::ProgressKind::kDds;
            line.key = a_key;
            line.outcome = a_outcome;
            line.reason = a_reason;
            return line;
        }

        // The builder's own memory of the bases that are not fire (staging, beside progress.jsonl; outside the contract, the
        // DLL never reads it): "<key16> <tag>" a line. The key holds the source's bytes and the copy recipe; the tag the fire
        // test's version and numbers, so a change to either judges the base again. Append-only, not atomic: a cut or foreign
        // line never matches the tag and is ignored (the base is judged again). A missing or unreadable file is an empty cache
        constexpr const char* kNotFireCacheFile = "efshbase-notfire.txt";
        constexpr std::uint64_t kDdsHeaderBytes = 148;  // magic, DDS_HEADER and DX10 header: the most an RGBA8 copy adds

        [[nodiscard]] std::string NotFireTag()
        {
            return fmt::format("fire{}:share={}:red={}:orange={}-{}:wrap={}:sat={}:value={}:lit={}", kEfshBaseFireTestVersion, kEfshBaseFireShare,
                kEfshBaseRedMinValue, kEfshBaseOrangeHueMin, kEfshFireHueMax, kEfshFireHueWrap, kEfshFireMinSat, kEfshFireMinValue,
                kStatLitWeight);
        }

        [[nodiscard]] std::set<std::uint64_t> ReadNotFire(const std::filesystem::path& a_staging)
        {
            std::set<std::uint64_t> out;
            std::ifstream in(a_staging / kNotFireCacheFile, std::ios::binary);
            const auto tag = NotFireTag();
            for (std::string line; std::getline(in, line);) {
                const auto space = line.find(' ');
                const auto key = space == std::string::npos ? std::nullopt : CC::ParseHex16(line.substr(0, space));
                if (key && line.compare(space + 1, std::string::npos, tag) == 0) {
                    out.insert(*key);
                }
            }
            return out;
        }

        struct Pass
        {
            const KeyedRequest& request;
            const std::filesystem::path& dump;
            ProgressLog& log;
            Budget& budget;
            const CC::KeyPrefix& prefix;
            std::uint64_t copyCap = CC::kMaxBaseCopyBytes;
            EfshBaseTotals totals;
            std::set<std::uint64_t> notFire;  // the cache read at the start
            std::uint64_t copyBytes = 0;      // the base copies staging holds for this request: reused, then written
            bool cacheWarned = false;
        };

        /// a_key recorded as not fire (a failed write is warned once: the base is judged again next run)
        void RememberNotFire(Pass& a_pass, std::uint64_t a_key)
        {
            std::ofstream out(a_pass.log.Staging() / kNotFireCacheFile, std::ios::binary | std::ios::app);
            out << CC::Hex16(a_key) << ' ' << NotFireTag() << '\n';
            if (!out && !a_pass.cacheWarned) {
                a_pass.cacheWarned = true;
                std::cout << fmt::format("WARN  {} could not be written: bases that are not fire are judged again next run\n",
                    PathText(a_pass.log.Staging() / kNotFireCacheFile));
            }
        }

        /// What an RGBA8 copy of every mip of a_meta's image takes (texel bytes; the DDS header aside)
        [[nodiscard]] std::uint64_t ChainBytes(const DirectX::TexMetadata& a_meta) noexcept
        {
            std::uint64_t bytes = 0;
            std::uint64_t width = a_meta.width;
            std::uint64_t height = a_meta.height;
            for (std::size_t mip = 0; mip < a_meta.mipLevels; ++mip) {
                bytes += width * height * kRgba;
                width = std::max<std::uint64_t>(width / 2, 1);
                height = std::max<std::uint64_t>(height / 2, 1);
            }
            return bytes;
        }

        void Append(Pass& a_pass, CC::ProgressLine a_line, const std::string& a_path)
        {
            if (a_line.outcome == CC::Outcome::kFailed) {
                ++a_pass.totals.failed;
            } else if (a_line.outcome == CC::Outcome::kTooLarge) {
                ++a_pass.totals.tooLarge;
            }
            std::cout << fmt::format("EBASE {} {}{}\n", CC::OutcomeText(a_line.outcome), a_path,
                a_line.reason.empty() ? std::string() : " - " + a_line.reason);
            if (std::string why; !a_pass.log.Append(a_line, why)) {
                std::cout << fmt::format("WARN  the line of effect shader base texture {} was not written: {}\n", a_path, why);
            }
        }

        void Failed(Pass& a_pass, std::uint64_t a_key, const std::string& a_path, const std::string& a_why)
        {
            Append(a_pass, DdsLine(a_key, CC::Outcome::kFailed, a_why), a_path);
        }

        void Skip(Pass& a_pass, const std::string& a_path, const std::string& a_why, bool a_srgb = false)
        {
            ++a_pass.totals.leftOut;
            a_pass.totals.srgb += a_srgb ? 1 : 0;
            std::cout << fmt::format("EBASE SKIP {}: {}: the source stays\n", a_path, a_why);
        }

        /// The budget's word for a_file: true to go on (a line written otherwise)
        [[nodiscard]] bool Admit(Pass& a_pass, std::uint64_t a_key, const std::string& a_path, const std::filesystem::path& a_file)
        {
            std::string why;
            bool transient = false;
            const auto estimate = EstimateTextureBytes(a_file, why, &transient);
            if (!estimate) {
                if (transient) {
                    Failed(a_pass, a_key, a_path, why);
                } else {
                    Skip(a_pass, a_path, why);
                }
                return false;
            }
            switch (a_pass.budget.Acquire(*estimate, why, false)) {
            case Admission::kAdmit:
                return true;
            case Admission::kTooLarge: {
                auto line = DdsLine(a_key, CC::Outcome::kTooLarge, why);
                line.cap = a_pass.budget.Cap();
                Append(a_pass, std::move(line), a_path);
                return false;
            }
            default:
                Failed(a_pass, a_key, a_path, why);
                return false;
            }
        }

        /// A loaded source: its chain as stored and its header
        struct Source
        {
            DirectX::TexMetadata meta{};
            DirectX::ScratchImage stored;
        };

        /// a_file loaded and checked (a plain 2D texture of an accepted format), or nullopt (a line written or the skip counted)
        [[nodiscard]] std::optional<Source> Load(Pass& a_pass, std::uint64_t a_key, const std::string& a_path, const std::filesystem::path& a_file)
        {
            Source source;
            if (const HRESULT hr = DirectX::LoadFromDDSFile(a_file.c_str(), kLoadFlags, &source.meta, source.stored); FAILED(hr)) {
                if (IsTransientResult(hr)) {
                    Failed(a_pass, a_key, a_path, fmt::format("not readable: {}", HrText(hr)));
                } else {
                    Skip(a_pass, a_path, fmt::format("not a readable DDS ({})", HrText(hr)));
                }
                return std::nullopt;
            }
            const auto& meta = source.meta;
            if (meta.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || meta.arraySize != 1 || meta.depth != 1 || meta.IsCubemap()) {
                Skip(a_pass, a_path, "not a plain 2D texture (a cube map, an array or a volume)");
                return std::nullopt;
            }
            const auto accept = AcceptFormat(meta.format);
            if (accept != Accept::kYes) {
                Skip(a_pass, a_path, accept == Accept::kSrgb ? fmt::format("format {} is sRGB (left out for now)", static_cast<int>(meta.format)) :
                                                               fmt::format("format {} is not a colour format this pass greys", static_cast<int>(meta.format)),
                    accept == Accept::kSrgb);
                return std::nullopt;
            }
            return std::optional<Source>(std::move(source));
        }

        /// a_images (a_count of them, a_meta's) in kWorkFormat, or nullopt (a line written or the skip counted)
        [[nodiscard]] std::optional<DirectX::ScratchImage> ToWork(Pass& a_pass, std::uint64_t a_key, const std::string& a_path,
            const DirectX::Image* a_images, std::size_t a_count, const DirectX::TexMetadata& a_meta)
        {
            DirectX::ScratchImage work;
            const HRESULT hr = DirectX::IsCompressed(a_meta.format) ?
                DirectX::Decompress(a_images, a_count, a_meta, kWorkFormat, work) :
                DirectX::Convert(a_images, a_count, a_meta, kWorkFormat, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, work);
            if (FAILED(hr)) {
                if (IsTransientResult(hr)) {
                    Failed(a_pass, a_key, a_path, fmt::format("decoding failed: {}", HrText(hr)));
                } else {
                    Skip(a_pass, a_path, fmt::format("format {} does not decode ({})", static_cast<int>(a_meta.format), HrText(hr)));
                }
                return std::nullopt;
            }
            return std::optional<DirectX::ScratchImage>(std::move(work));
        }

        /// The fire test's input: the top mip alone in kWorkFormat (no other mip is decoded for a base that is not fire)
        [[nodiscard]] std::optional<RgbaImage> TopOf(Pass& a_pass, std::uint64_t a_key, const std::string& a_path, const Source& a_source)
        {
            const auto* top = a_source.stored.GetImage(0, 0, 0);
            if (a_source.meta.format == kWorkFormat) {
                return TopImage(*top);
            }
            auto meta = a_source.meta;
            meta.mipLevels = 1;
            const auto work = ToWork(a_pass, a_key, a_path, top, 1, meta);
            return work ? std::optional<RgbaImage>(TopImage(*work->GetImage(0, 0, 0))) : std::nullopt;
        }

        /// Every mip in kWorkFormat (a fire base only)
        [[nodiscard]] std::optional<DirectX::ScratchImage> ChainOf(Pass& a_pass, std::uint64_t a_key, const std::string& a_path, Source& a_source)
        {
            if (a_source.meta.format == kWorkFormat) {
                return std::optional<DirectX::ScratchImage>(std::move(a_source.stored));
            }
            return ToWork(a_pass, a_key, a_path, a_source.stored.GetImages(), a_source.stored.GetImageCount(), a_source.meta);
        }

        void BuildOne(Pass& a_pass, const CC::EfshPalette& a_base)
        {
            const auto key = *a_base.key;
            const auto& path = a_base.path;
            if (a_pass.log.Reusable(key, a_pass.budget.Cap())) {
                const auto* line = a_pass.log.Last(key);
                ++(line->outcome == CC::Outcome::kOk ? a_pass.totals.reused : a_pass.totals.tooLarge);
                return;  // its bytes are in copyBytes already (BuildEfshBases)
            }
            if (a_pass.notFire.count(key) != 0) {
                ++a_pass.totals.notFire;
                ++a_pass.totals.notFireCached;  // judged by an earlier run: not read, not decoded
                return;
            }
            const auto slot = Rule::CopyPath(Rule::kTexturesRoot, CC::Hex16(key), path);
            if (!slot) {
                Failed(a_pass, key, path, "no copy path for this source");
                return;
            }
            const bool listed = a_pass.request.base.texturesDumped.count(path) != 0;
            const auto source = listed ? FindSource({ a_pass.dump }, path) : std::nullopt;
            if (!source) {
                const auto state = a_pass.request.base.textureStates.find(path);
                if (state != a_pass.request.base.textureStates.end() && state->second == CC::Request::TextureState::kUnread) {
                    ++a_pass.totals.unread;
                    std::cout << fmt::format("EBASE UNREAD {}: the game could not read it this start\n", path);
                    return;
                }
                Failed(a_pass, key, path, listed ? "listed as dumped but not in the dump" : "keyed, yet textures[] does not list it as dumped");
                return;
            }
            std::string why;
            const auto digest = DigestFile(*source, why);
            const auto text = digest ? CC::EfshBaseKeyString(a_pass.prefix, { path, digest->fnv64 }) : std::nullopt;
            if (!digest || !text || CC::KeyOf(*text) != key) {
                Failed(a_pass, key, path, !digest ? why : "the dump's bytes are not the ones the request keyed");
                return;
            }
            if (!Admit(a_pass, key, path, *source)) {
                return;
            }
            auto loaded = Load(a_pass, key, path, *source);
            const auto top = loaded ? TopOf(a_pass, key, path, *loaded) : std::nullopt;
            if (!top) {
                return;
            }
            const double share = EfshBaseFireShare(*top);
            if (share < kEfshBaseFireShare) {
                ++a_pass.totals.notFire;
                RememberNotFire(a_pass, key);
                std::cout << fmt::format("EBASE SKIP {}: not fire ({:.4f} of its visible lit texels, alpha-weighted, under {})\n", path,
                    share, kEfshBaseFireShare);
                return;
            }
            const auto expected = ChainBytes(loaded->meta) + kDdsHeaderBytes;
            if (a_pass.copyBytes + expected > a_pass.copyCap) {
                ++a_pass.totals.overCopyCap;
                std::cout << fmt::format("EBASE SKIP {}: its copy ({} bytes) would take the base copies past their cap ({} of {} "
                                         "bytes held): the source stays\n", path, expected, a_pass.copyBytes, a_pass.copyCap);
                return;
            }
            auto chain = ChainOf(a_pass, key, path, *loaded);
            if (!chain) {
                return;
            }
            GreyChain(*chain);
            const auto target = StagedFile(a_pass.log.Staging(), *slot);
            const auto temp = TempPathOf(target);
            std::error_code ec;
            std::filesystem::create_directories(temp.parent_path(), ec);
            if (const HRESULT hr = DirectX::SaveToDDSFile(chain->GetImages(), chain->GetImageCount(), chain->GetMetadata(),
                    DirectX::DDS_FLAGS_NONE, temp.c_str());
                FAILED(hr)) {
                RemoveQuietly(temp);
                Failed(a_pass, key, path, fmt::format("writing {} failed: {}{}", PathText(temp), HrText(hr),
                    ec ? fmt::format(" (making its folder failed first: {})", ec.message()) : std::string()));
                return;
            }
            why = CheckWritten(temp, *chain);
            const auto written = why.empty() ? DigestFile(temp, why) : std::nullopt;
            if (!written || !CommitTemp(temp, target, why)) {
                RemoveQuietly(temp);
                Failed(a_pass, key, path, why);
                return;
            }
            a_pass.log.NoteWritten(*slot, *written);
            auto line = DdsLine(key, CC::Outcome::kOk, "");
            line.path = *slot;
            line.bytes = written->bytes;
            line.fnv64 = written->fnv64;
            ++a_pass.totals.grey;
            a_pass.totals.bytes += written->bytes;
            a_pass.copyBytes += written->bytes;
            Append(a_pass, std::move(line), fmt::format("{} ({}) -> {} ({:.4f} fire, {} mips, RGBA8)", path,
                CC::EfshBaseRoleText(a_base.role), *slot, share, chain->GetMetadata().mipLevels));
        }
    }

    double EfshBaseFireShare(const RgbaImage& a_image) noexcept
    {
        double visible = 0.0;
        double fire = 0.0;
        for (std::size_t i = 0; i + 3 < a_image.pixels.size(); i += kRgba) {
            const double r = a_image.pixels[i] / kChannelMax;
            const double g = a_image.pixels[i + 1] / kChannelMax;
            const double b = a_image.pixels[i + 2] / kChannelMax;
            const double alpha = a_image.pixels[i + 3] / kChannelMax;
            const double max = std::max({ r, g, b });
            const double min = std::min({ r, g, b });
            if (alpha <= 0.0 || max <= kStatLitWeight) {
                continue;
            }
            visible += alpha;
            if ((max - min) / max < kEfshFireMinSat || max < kEfshFireMinValue) {
                continue;
            }
            const double hue = Hue(r, g, b, max, min);
            const bool orange = hue >= kEfshBaseOrangeHueMin && hue <= kEfshFireHueMax;
            const bool red = hue < kEfshBaseOrangeHueMin || hue >= kEfshFireHueWrap;
            fire += orange || (red && max >= kEfshBaseRedMinValue) ? alpha : 0.0;
        }
        return visible <= 0.0 ? 0.0 : fire / visible;
    }

    EfshBaseTotals BuildEfshBases(const KeyedRequest& a_request, const std::filesystem::path& a_dump, ProgressLog& a_log, Budget& a_budget,
        const CC::KeyPrefix& a_prefix, const std::function<bool()>& a_stop, std::uint64_t a_copyCap)
    {
        Pass pass{ a_request, a_dump, a_log, a_budget, a_prefix, a_copyCap, {}, ReadNotFire(a_log.Staging()) };
        pass.totals.listed = a_request.efshBases.size();
        // the copies staging already holds for this request count first, so a new one never pushes a built one past the cap
        for (const auto& base : a_request.efshBases) {
            const auto* line = base.key && a_log.Reusable(*base.key, a_budget.Cap()) ? a_log.Last(*base.key) : nullptr;
            pass.copyBytes += line && line->outcome == CC::Outcome::kOk ? line->bytes : 0;
        }
        pass.totals.copyBytes = pass.copyBytes;
        for (const auto& base : a_request.efshBases) {
            if (a_log.Broken() || (a_stop && a_stop())) {
                break;  // the caller says why (a write failed, or the parent is gone)
            }
            if (!base.key) {
                const auto state = a_request.base.textureStates.find(base.path);
                const bool unread = state != a_request.base.textureStates.end() && state->second == CC::Request::TextureState::kUnread;
                ++(unread ? pass.totals.unread : base.hash.state == CC::FileState::kMissing ? pass.totals.missing : pass.totals.notDumped);
                continue;
            }
            try {
                BuildOne(pass, base);
            } catch (const std::bad_alloc&) {
                Failed(pass, *base.key, base.path, "out of memory (bad_alloc)");
            } catch (const std::exception& e) {
                Failed(pass, *base.key, base.path, e.what());
            }
        }
        pass.totals.copyBytes = pass.copyBytes;
        return pass.totals;
    }

    std::string EfshBaseTotalsLine(const EfshBaseTotals& a_totals)
    {
        const auto mib = [](std::uint64_t a_bytes) { return static_cast<double>(a_bytes) / static_cast<double>(kBytesPerMiB); };
        return fmt::format("bases  {} effect shader base texture(s): grey {} written ({:.1f} MiB), {} reused; copies {:.1f} MiB of the "
                           "{:.0f} MiB cap; skipped (the source stays): {} not fire ({} known from the cache), {} over the copies cap, "
                           "{} format left out or not decodable ({} sRGB), {} missing in the game, {} outside the dump caps, {} unread "
                           "this start; failed {}, too_large {}", a_totals.listed, a_totals.grey, mib(a_totals.bytes), a_totals.reused,
            mib(a_totals.copyBytes), mib(CC::kMaxBaseCopyBytes), a_totals.notFire, a_totals.notFireCached, a_totals.overCopyCap,
            a_totals.leftOut, a_totals.srgb, a_totals.missing, a_totals.notDumped, a_totals.unread, a_totals.failed, a_totals.tooLarge);
    }
}
