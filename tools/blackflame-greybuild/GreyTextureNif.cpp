// =============================================================================
// GreyTextureNif - which texture slots of a copy NIF get a grey copy (GreyTexture.h)
// =============================================================================

#include "GreyTexture.h"

#include <NifFile.hpp>
#include <Shaders.hpp>

#include <fmt/format.h>
#include <iostream>
#include <set>

namespace GreyBuild
{
    namespace
    {
        // One slot: the texture recoloured the first time any NIF names it, the slot pointed at its copy when there is one
        void Retarget(std::string& a_slot, TextureRole a_role, TextureJob& a_job, const std::string& a_nifRel)
        {
            if (a_slot.empty()) {
                return;
            }
            const auto path = TextureDataPath(a_slot);
            if (path && StartsWith(*path, kOwnTextureRoot)) {
                return;  // our palette or a copy already
            }
            ++a_job.totals.slots;
            if (!path) {
                std::cout << fmt::format("TEX   FAIL {}: slot '{}' ({}) is not a texture path the builder reads (a drive, '..', or "
                                         "not .dds): left\n", a_nifRel, a_slot, TextureRoleName(a_role));
                ++a_job.totals.failed;
                return;
            }
            auto found = a_job.done.find(*path);
            if (found == a_job.done.end()) {
                TextureJob::Done done;
                done.role = a_role;
                const auto source = FindSource(a_job.inputs, *path);
                done.found = source.has_value();
                if (!source) {
                    const auto why = a_job.notDumped.find(*path);
                    std::cout << fmt::format("TEX   MISS {} ({}, first in {}): in no --in folder{}; slot left\n", *path,
                        TextureRoleName(a_role), a_nifRel, why == a_job.notDumped.end() ? std::string() : " - the game: " + why->second);
                    ++a_job.totals.missing;
                } else {
                    const std::string copy = TextureCopyPathFor(*path);
                    const auto result = RecolourDds(*source, a_job.out / std::filesystem::path(copy), TextureTint(a_role));
                    done.outcome = result.outcome;
                    switch (result.outcome) {
                    case RecolourOutcome::kRecoloured:
                        done.copy = copy;
                        ++a_job.totals.recoloured;
                        a_job.totals.bytes += result.bytes;
                        std::cout << fmt::format("TEX   OK   {} -> {} ({}, tint {}, {}, {} mips, saturation {:.3f}, {} bytes)\n", *path,
                            copy, TextureRoleName(a_role), TextureTint(a_role), result.format, result.mips, result.saturation,
                            result.bytes);
                        break;
                    case RecolourOutcome::kGreyscale:
                        ++a_job.totals.greyscale;
                        std::cout << fmt::format("TEX   GREY {} ({}): {}; slot left\n", *path, result.format, result.reason);
                        break;
                    default:
                        ++a_job.totals.failed;
                        std::cout << fmt::format("TEX   FAIL {} ({}, first in {}): {}; slot left\n", *path, TextureRoleName(a_role),
                            a_nifRel, result.reason);
                        break;
                    }
                }
                found = a_job.done.emplace(*path, std::move(done)).first;
            } else if (found->second.role != a_role && TextureTint(found->second.role) != TextureTint(a_role)) {
                std::cout << fmt::format("TEX   NOTE {}: {} uses it as {}, its copy was made as {} (tint {}): that copy is used\n",
                    *path, a_nifRel, TextureRoleName(a_role), TextureRoleName(found->second.role), TextureTint(found->second.role));
            }
            if (!found->second.copy.empty()) {
                a_slot = found->second.copy;
                ++a_job.totals.repointed;
            }
        }
    }

    void RetargetTextures(nifly::NifFile& a_nif, TextureJob& a_job, const std::string& a_nifRel)
    {
        auto& hdr = a_nif.GetHeader();
        std::set<std::uint32_t> sets;  // a texture set two lighting shaders share: its slots once
        for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
            if (auto* lit = hdr.GetBlock<nifly::BSLightingShaderProperty>(id)) {
                auto* set = hdr.GetBlock<nifly::BSShaderTextureSet>(lit->textureSetRef);
                if (!set || !sets.insert(lit->textureSetRef.index).second) {
                    continue;
                }
                for (const auto& [slot, role] : { std::pair{ kSlotDiffuse, TextureRole::kDiffuse }, std::pair{ kSlotGlow, TextureRole::kGlow } }) {
                    if (slot < set->textures.size()) {
                        Retarget(set->textures[slot].get(), role, a_job, a_nifRel);
                    }
                }
            } else if (auto* effect = hdr.GetBlock<nifly::BSEffectShaderProperty>(id)) {
                if (!UsesPalette(*effect)) {  // a palette shader's texture is a greyscale map through the palette
                    Retarget(effect->sourceTexture.get(), TextureRole::kEffectSource, a_job, a_nifRel);
                }
            }
        }
    }
}
