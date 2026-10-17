#pragma once

#include "Common.h"

#include "FireClosure.h"
#include "NifDump.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
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

    /// { source, type, editorId, model, [muzzleModel], [bounds], data }; TXST also "textures" (TX00..TX07 as the record
    /// stores them, UTF-8 for people), "texturesRaw" (AB1: each slot's own bytes in hex, what the copy record gets) and
    /// "hasDecal" (the builder's GreyFormsTxst.h)
    [[nodiscard]] nlohmann::ordered_json FormJson(const RE::TESForm* a_form, CopyKind a_kind);

    /// The fingerprint's model column of a closure form: its model, or for a TXST its eight slots and whether it has DODT
    /// (Codex 2: a DODT presence change makes the copies stale), normalised
    [[nodiscard]] std::string FingerprintModelOf(const RE::TESForm* a_form);

    /// TX00 and TX03 (the colour slots the builder recolours) of every TXST in the closure, as their records store them
    [[nodiscard]] std::vector<std::string> FormTexturePaths(const FireClosure& a_closure);

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
        bool readFailed = false;   // T1: planned, but it opened and could not be read (kept in the manifest; U10: tried again
                                   // at the next start, alone, when the dump is otherwise unchanged)
        bool own = false;          // one of our two fixed palettes (DataPathRule kOurPalette*): the builder's risk check
        std::uint64_t fnv64 = 0;   // own: its bytes' FNV-1a 64, read at every plan (Codex 3: a same-size variant re-dumps)
    };

    /// Every texture path a_nifBytes name inline (NifDump::TexturePathsIn: the builder picks the slots it recolours) and
    /// a_formTextures (TXST colour slots), our own (textures\heartofmagic\blackflame\) left out, each opened for its size
    /// and planned in path order within the caps; reads no texture byte of those. Then our two fixed palettes (own, sized
    /// first so the caps never drop them), each read whole for its FNV. kDataLoaded thread (BSResource).
    [[nodiscard]] std::vector<Texture> PlanTextures(const std::vector<const std::vector<std::byte>*>& a_nifBytes,
        const std::vector<std::string>& a_formTextures = {});

    /// The dump's texture part (path, size, planned): sizes only, so a texture replaced by one of the same size is not dumped
    /// again. Kept apart from the request fingerprint, whose formula the players' freshness check shares.
    [[nodiscard]] std::string TextureFingerprint(const std::vector<Texture>& a_textures);

    /// [{ path, size, ok (in the dump), error (when not), own (our palettes) }]
    [[nodiscard]] nlohmann::ordered_json TexturesJson(const std::vector<Texture>& a_textures);

    /// The JSON object in a_file (the dump manifest), or an empty object when it is missing or not an object
    [[nodiscard]] nlohmann::json ReadJsonObject(const std::filesystem::path& a_file);

    /// a_object[a_key] when it is a string, else ""
    [[nodiscard]] std::string JsonString(const nlohmann::json& a_object, const char* a_key);

    /// The strings of a_object[a_key] when it is an array (others skipped), else none
    [[nodiscard]] std::set<std::string> JsonStringSet(const nlohmann::json& a_object, const char* a_key);
}
