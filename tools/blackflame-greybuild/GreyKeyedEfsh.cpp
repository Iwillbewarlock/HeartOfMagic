// =============================================================================
// GreyKeyedEfsh - key mode's effect shader palettes: the fire test and each grey copy's dds line (GreyKeyed.h)
// =============================================================================
// One pass before the scheduled objects (palettes are small). Per keyed request entry:
//   reusable line in staging           counted as reused (an ok line), or as its terminal outcome
//   not in the dump                    textures[] unread: counted (the game reads it again next start); else a failed line
//   the dump's bytes not the key's     a failed line (the key is EfshPaletteKeyString over those bytes, CopyContractKeys.h)
//   over the job memory cap            a too_large line with the cap; the process over it: a failed line
//   does not decode                    the machine's fault (IsTransientResult): a failed line; the file's: counted, no line
//   not fire (EfshFireShare)           counted, no line: the source palette stays
//   fire                               DesaturatePalette, written as <copy>.tmp and renamed, an ok dds line
// An entry with no key (missing, notdumped) is counted only: as unread when textures[] says the game could not read it.

#include "GreyKeyed.h"

#include "GreyPalette.h"

#include "../../plugins/BlackFlame/src/DataPathRule.h"

#include <algorithm>
#include <fmt/format.h>
#include <iostream>

namespace GreyBuild
{
    namespace
    {
        namespace CC = BlackFlame::CopyContract;
        namespace Rule = BlackFlame::DataPathRule;

        constexpr double kChannelMax = 255.0;
        constexpr double kHueSextant = 6.0;

        [[nodiscard]] CC::ProgressLine DdsLine(std::uint64_t a_key, CC::Outcome a_outcome, const std::string& a_reason)
        {
            CC::ProgressLine line;
            line.kind = CC::ProgressKind::kDds;
            line.key = a_key;
            line.outcome = a_outcome;
            line.reason = a_reason;
            return line;
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

        struct Pass
        {
            const KeyedRequest& request;
            const std::filesystem::path& dump;
            ProgressLog& log;
            Budget& budget;
            const CC::KeyPrefix& prefix;
            EfshTotals totals;
        };

        void Append(Pass& a_pass, CC::ProgressLine a_line, const std::string& a_path)
        {
            if (a_line.outcome == CC::Outcome::kFailed) {
                ++a_pass.totals.failed;
            } else if (a_line.outcome == CC::Outcome::kTooLarge) {
                ++a_pass.totals.tooLarge;
            }
            std::cout << fmt::format("EFSH  {} {}{}\n", CC::OutcomeText(a_line.outcome), a_path,
                a_line.reason.empty() ? std::string() : " - " + a_line.reason);
            if (std::string why; !a_pass.log.Append(a_line, why)) {
                std::cout << fmt::format("WARN  the line of effect shader palette {} was not written: {}\n", a_path, why);
            }
        }

        void Failed(Pass& a_pass, std::uint64_t a_key, const std::string& a_path, const std::string& a_why)
        {
            Append(a_pass, DdsLine(a_key, CC::Outcome::kFailed, a_why), a_path);
        }

        /// a_path's decoded image, or nullopt (a line written or the skip counted)
        [[nodiscard]] std::optional<RgbaImage> Read(Pass& a_pass, std::uint64_t a_key, const std::string& a_path,
            const std::filesystem::path& a_file)
        {
            std::string why;
            bool transient = false;
            const auto estimate = EstimateReadBytes(a_file, why, &transient);
            if (estimate) {
                switch (a_pass.budget.Acquire(*estimate, why, false)) {
                case Admission::kAdmit:
                    break;
                case Admission::kTooLarge: {
                    auto line = DdsLine(a_key, CC::Outcome::kTooLarge, why);
                    line.cap = a_pass.budget.Cap();
                    Append(a_pass, std::move(line), a_path);
                    return std::nullopt;
                }
                default:
                    Failed(a_pass, a_key, a_path, why);
                    return std::nullopt;
                }
            }
            auto image = estimate ? ReadRgba(a_file, why, &transient) : std::nullopt;
            if (!image && transient) {
                Failed(a_pass, a_key, a_path, why);
            } else if (!image) {
                ++a_pass.totals.unreadable;
                std::cout << fmt::format("EFSH  SKIP {}: does not decode ({}): the source palette stays\n", a_path, why);
            }
            return image;
        }

        void BuildOne(Pass& a_pass, const CC::EfshPalette& a_palette)
        {
            const auto key = *a_palette.key;
            const auto& path = a_palette.path;
            const auto slot = Rule::PaletteCopyPath(CC::Hex16(key));
            if (a_pass.log.Reusable(key, a_pass.budget.Cap())) {
                const auto* line = a_pass.log.Last(key);
                ++(line->outcome == CC::Outcome::kOk ? a_pass.totals.reused : a_pass.totals.tooLarge);
                return;
            }
            const bool listed = a_pass.request.base.texturesDumped.count(path) != 0;
            const auto source = listed ? FindSource({ a_pass.dump }, path) : std::nullopt;
            if (!source) {
                const auto state = a_pass.request.base.textureStates.find(path);
                if (state != a_pass.request.base.textureStates.end() && state->second == CC::Request::TextureState::kUnread) {
                    ++a_pass.totals.unread;  // the game opened it and its read failed: read again at the next start
                    std::cout << fmt::format("EFSH  UNREAD {}: the game could not read it this start\n", path);
                    return;
                }
                Failed(a_pass, key, path, listed ? "listed as dumped but not in the dump" : "keyed, yet textures[] does not list it as dumped");
                return;
            }
            std::string why;
            const auto digest = DigestFile(*source, why);
            const auto text = digest ? CC::EfshPaletteKeyString(a_pass.prefix, { path, digest->fnv64 }) : std::nullopt;
            if (!digest || !text || CC::KeyOf(*text) != key) {
                Failed(a_pass, key, path, !digest ? why : "the dump's bytes are not the ones the request keyed");
                return;
            }
            auto image = Read(a_pass, key, path, *source);
            if (!image) {
                return;
            }
            const double share = EfshFireShare(*image);
            if (share < kEfshFireShare) {
                ++a_pass.totals.notFire;
                std::cout << fmt::format("EFSH  SKIP {}: not fire ({:.3f} of its lit texels fire coloured, under {})\n", path, share,
                    kEfshFireShare);
                return;
            }
            DesaturatePalette(*image);
            const auto target = StagedFile(a_pass.log.Staging(), *slot);
            const auto temp = TempPathOf(target);
            const auto written = WritePaletteDds(*image, temp, why) ? DigestFile(temp, why) : std::nullopt;
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
            Append(a_pass, std::move(line), fmt::format("{} -> {} ({:.3f} fire coloured)", path, *slot, share));
        }
    }

    double EfshFireShare(const RgbaImage& a_image) noexcept
    {
        std::size_t lit = 0;
        std::size_t fire = 0;
        for (std::size_t i = 0; i + 3 < a_image.pixels.size(); i += 4) {
            const double r = a_image.pixels[i] / kChannelMax;
            const double g = a_image.pixels[i + 1] / kChannelMax;
            const double b = a_image.pixels[i + 2] / kChannelMax;
            const double max = std::max({ r, g, b });
            const double min = std::min({ r, g, b });
            if (max <= kStatLitWeight) {
                continue;
            }
            ++lit;
            const double saturation = (max - min) / max;
            if (saturation < kEfshFireMinSat || max < kEfshFireMinValue) {
                continue;
            }
            const double hue = Hue(r, g, b, max, min);
            fire += hue <= kEfshFireHueMax || hue >= kEfshFireHueWrap ? 1 : 0;
        }
        return lit == 0 ? 0.0 : static_cast<double>(fire) / static_cast<double>(lit);
    }

    EfshTotals BuildEfshPalettes(const KeyedRequest& a_request, const std::filesystem::path& a_dump, ProgressLog& a_log, Budget& a_budget,
        const CC::KeyPrefix& a_prefix, const std::function<bool()>& a_stop)
    {
        Pass pass{ a_request, a_dump, a_log, a_budget, a_prefix, {} };
        pass.totals.listed = a_request.efshPalettes.size();
        for (const auto& palette : a_request.efshPalettes) {
            if (a_log.Broken() || (a_stop && a_stop())) {
                break;  // the caller says why (a write failed, or the parent is gone)
            }
            if (!palette.key) {
                // review round 1: the DLL lists a palette it could not read as missing here and unread in textures[]: unread
                const auto state = a_request.base.textureStates.find(palette.path);
                const bool unread = state != a_request.base.textureStates.end() && state->second == CC::Request::TextureState::kUnread;
                ++(unread ? pass.totals.unread : palette.hash.state == CC::FileState::kMissing ? pass.totals.missing : pass.totals.notDumped);
                continue;
            }
            try {
                BuildOne(pass, palette);
            } catch (const std::bad_alloc&) {
                Failed(pass, *palette.key, palette.path, "out of memory (bad_alloc)");
            } catch (const std::exception& e) {
                Failed(pass, *palette.key, palette.path, e.what());
            }
        }
        return pass.totals;
    }

    std::string EfshTotalsLine(const EfshTotals& a_totals)
    {
        return fmt::format("efsh   {} effect shader palette(s): grey {} written, {} reused; skipped (the source stays): {} not fire, "
                           "{} not decodable, {} missing in the game, {} outside the dump caps, {} unread this start; failed {}, "
                           "too_large {}", a_totals.listed, a_totals.grey, a_totals.reused, a_totals.notFire, a_totals.unreadable,
            a_totals.missing, a_totals.notDumped, a_totals.unread, a_totals.failed, a_totals.tooLarge);
    }
}
