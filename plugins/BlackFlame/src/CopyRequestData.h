#pragma once

#include "Common.h"

#include "FireClosure.h"
#include "NifDump.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// =============================================================================
// COPY REQUEST DATA - one form or ADDN as the request writes it (CopyRequest.h)
// =============================================================================
// The scalar keys are the builder's (tools/blackflame-greybuild GreyFormsEsl.cpp Fields): the values only make a
// valid placeholder record - the DLL fills every copy from the live form at kDataLoaded.

namespace BlackFlame::CopyRequestData
{
    /// The projectile's muzzle flash model as its record stores it, or empty
    [[nodiscard]] std::string MuzzleModelOf(const RE::BGSProjectile& a_projectile);

    /// { source, type, editorId, model, [muzzleModel], [bounds], data }
    [[nodiscard]] nlohmann::ordered_json FormJson(const RE::TESForm* a_form, CopyKind a_kind);

    /// Every ADDN record: { index, model, editorId, source, masterParticleCap, dnamFlags, own }
    [[nodiscard]] nlohmann::ordered_json AddonsJson();

    // =========================================================================
    // T1: textures the read NIFs name, dumped beside them for the builder's texture copies
    // =========================================================================

    inline constexpr std::uint64_t kMiB = 1024ull * 1024ull;
    // One texture the dump takes at most (a 4096 BC7 with mips is about 22 MiB; an uncompressed 4096 one, 85 MiB, is left
    // out and named in the request)
    inline constexpr std::uint64_t kMaxTextureBytes = 64 * kMiB;
    // Every texture of one dump together at most; a texture that no longer fits is left out (named in the request), smaller
    // ones after it in path order still go in
    inline constexpr std::uint64_t kMaxTextureDumpBytes = 768 * kMiB;

    struct Texture
    {
        std::string path;          // Data-relative, normalised (textures\...)
        NifDump::ModelBlob probe;  // opened and totalSize; no byte read until the dump
        bool planned = false;      // opened and within both caps: the dump holds it
        std::string error;         // why it is not in the dump ("" when planned)
    };

    /// Every texture path a_nifBytes name inline (NifDump::TexturePathsIn: the builder picks the slots it recolours), our
    /// own (textures\heartofmagic\blackflame\) left out, each opened for its size and planned in path order within the
    /// caps. Reads no texture byte. kDataLoaded thread (BSResource).
    [[nodiscard]] std::vector<Texture> PlanTextures(const std::vector<const std::vector<std::byte>*>& a_nifBytes);

    /// The dump's texture part (path, size, planned): sizes only, so a texture replaced by one of the same size is not dumped
    /// again. Kept apart from the request fingerprint, whose formula the players' freshness check shares.
    [[nodiscard]] std::string TextureFingerprint(const std::vector<Texture>& a_textures);

    /// [{ path, size, ok (in the dump), error (when not) }]
    [[nodiscard]] nlohmann::ordered_json TexturesJson(const std::vector<Texture>& a_textures);
}
