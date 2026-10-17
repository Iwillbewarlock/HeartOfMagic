// =============================================================================
// GreyTextureNif - which texture slots of a copy NIF get a grey copy (GreyTexture.h)
// =============================================================================

#include "GreyTexture.h"

#include "GreyKeyed.h"

#include <NifFile.hpp>
#include <Shaders.hpp>

#include <cmath>
#include <fmt/format.h>
#include <iostream>
#include <set>
#include <utility>

namespace GreyBuild
{
    namespace
    {
        // T6: slot 2 is a glow map only on a glow shader (BSLSP_GLOWMAP, or the SLSF2 glow flag); on a skin, face or
        // multi-layer shader it holds another map
        [[nodiscard]] bool IsGlowShader(const nifly::BSLightingShaderProperty& a_shader)
        {
            return a_shader.GetShaderType() == nifly::BSLSP_GLOWMAP || (a_shader.shaderFlags2 & kSlsf2GlowMap) != 0;
        }

        // T3: a glow shader's emissive colour to its luminance (x 1): a grey glow map under an orange emissive colour glows
        // orange. U6: an Own_Emit shader adds its emissive colour too, glow map or not
        void GreyEmissive(nifly::BSLightingShaderProperty& a_shader, TextureJob& a_job)
        {
            auto& colour = a_shader.emissiveColor;
            const float grey = Luminance(colour.x, colour.y, colour.z);
            colour.x = grey;
            colour.y = grey;
            colour.z = grey;
            ++a_job.totals.emissiveGreyed;
        }
    }

    void RetargetTextureSlot(std::string& a_slot, TextureRole a_role, TextureJob& a_job, const std::string& a_nifRel)
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
            std::cout << fmt::format("TEX   FAIL {}: slot '{}' ({}) is not a texture path the builder reads (a drive, '..', a "
                                     "character no file name has, or not .dds): left\n", a_nifRel, a_slot, TextureRoleName(a_role));
            ++a_job.totals.failed;
            if (a_job.keyed) {
                a_job.keyed->Fallback(fmt::format("{}: slot '{}' is no texture path the builder reads: left", a_nifRel, a_slot));
            }
            return;
        }
        const float tint = TextureTint(a_role, a_job.diffuseTint);
        if (a_job.keyed) {  // key mode: the copy at copies\<key16>\, its line and the object's outcome (GreyKeyed.h)
            if (const auto copy = a_job.keyed->Texture(*path, a_role, tint, a_nifRel); !copy.empty()) {
                a_slot = copy;
                ++a_job.totals.repointed;
            }
            return;
        }
        const std::string key = fmt::format("{}|{}", *path, std::lround(tint * static_cast<float>(kTintPercent)));
        auto found = a_job.done.find(key);
        if (found == a_job.done.end()) {
            // U8: found or missing, and a failure, are judged once per path; a copy is made per tint
            auto& state = a_job.paths[*path];
            if (!state.judged) {
                state.judged = true;
                state.source = FindSource(a_job.inputs, *path);
                if (!state.source) {
                    const auto why = a_job.notDumped.find(*path);
                    std::cout << fmt::format("TEX   MISS {} ({}, first in {}): in no --in folder{}; slot left\n", *path,
                        TextureRoleName(a_role), a_nifRel, why == a_job.notDumped.end() ? std::string() : " - the game: " + why->second);
                    ++a_job.totals.missing;
                }
            }
            TextureJob::Done done;
            done.found = state.source.has_value();
            if (state.source && !state.failed) {
                const std::string copy = TextureCopyPathFor(*path, tint);
                const auto result = RecolourDds(*state.source, a_job.out / std::filesystem::path(copy), tint);
                done.outcome = result.outcome;
                switch (result.outcome) {
                case RecolourOutcome::kRecoloured:
                    done.copy = copy;
                    ++a_job.totals.recoloured;
                    a_job.totals.bytes += result.bytes;
                    std::cout << fmt::format("TEX   OK   {} -> {} ({}, tint {}, {}, {} mips, chroma {:.3f}, coloured {:.2f}%{}, {} "
                                             "bytes)\n", *path, copy, TextureRoleName(a_role), tint, result.format, result.mips,
                        result.chroma, result.colouredShare * 100.0f, result.alphaIgnored ? ", alpha ignored" : "", result.bytes);
                    break;
                case RecolourOutcome::kGreyscale:
                    if (!state.greyCounted) {
                        state.greyCounted = true;
                        ++a_job.totals.greyscale;
                        std::cout << fmt::format("TEX   GREY {} ({}): {}; slot left\n", *path, result.format, result.reason);
                    }
                    break;
                default:
                    state.failed = true;
                    ++a_job.totals.failed;
                    std::cout << fmt::format("TEX   FAIL {} ({}, first in {}): {}; slot left\n", *path, TextureRoleName(a_role),
                        a_nifRel, result.reason);
                    break;
                }
            }
            found = a_job.done.emplace(key, std::move(done)).first;
        }
        if (!found->second.copy.empty()) {
            a_slot = found->second.copy;
            ++a_job.totals.repointed;
        }
    }

    void ForEachTextureSlot(nifly::NifFile& a_nif, const std::function<void(std::string&, TextureRole)>& a_visit)
    {
        auto& hdr = a_nif.GetHeader();
        std::set<std::pair<std::uint32_t, std::uint32_t>> slotsDone;  // (texture set, slot): a set two shaders share, once
        for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
            if (auto* lit = hdr.GetBlock<nifly::BSLightingShaderProperty>(id)) {
                const bool glow = IsGlowShader(*lit);
                auto* set = hdr.GetBlock<nifly::BSShaderTextureSet>(lit->textureSetRef);
                if (!set) {
                    continue;
                }
                const std::pair<std::uint32_t, TextureRole> slots[] = { { kSlotDiffuse, TextureRole::kDiffuse },
                    { kSlotGlow, TextureRole::kGlow } };
                for (const auto& [slot, role] : slots) {
                    if ((role == TextureRole::kGlow && !glow) || slot >= set->textures.size() ||
                        !slotsDone.insert({ lit->textureSetRef.index, slot }).second) {
                        continue;
                    }
                    a_visit(set->textures[slot].get(), role);
                }
            } else if (auto* effect = hdr.GetBlock<nifly::BSEffectShaderProperty>(id)) {
                if (!UsesPalette(*effect)) {  // a palette shader's texture is a greyscale map through the palette
                    a_visit(effect->sourceTexture.get(), TextureRole::kEffectSource);
                }
            }
        }
    }

    void RetargetTextures(nifly::NifFile& a_nif, TextureJob& a_job, const std::string& a_nifRel)
    {
        auto& hdr = a_nif.GetHeader();
        for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
            auto* lit = hdr.GetBlock<nifly::BSLightingShaderProperty>(id);
            if (lit && (IsGlowShader(*lit) || (lit->shaderFlags1 & kSlsf1OwnEmit) != 0)) {
                GreyEmissive(*lit, a_job);
            }
        }
        ForEachTextureSlot(a_nif, [&](std::string& a_slot, TextureRole a_role) { RetargetTextureSlot(a_slot, a_role, a_job, a_nifRel); });
    }
}
