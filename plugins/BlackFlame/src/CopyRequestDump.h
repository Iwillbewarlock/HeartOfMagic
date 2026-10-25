#pragma once

#include "Common.h"

#include "CopyRequestData.h"
#include "NifDump.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// =============================================================================
// COPY REQUEST DUMP - the NIF and texture dump of the copy request (CopyRequest.h)
// =============================================================================

namespace BlackFlame::CopyRequestDump
{
    inline constexpr const char* kManifestName = "dump-manifest.json";
    inline constexpr const char* kDumpRoot = "data";
    inline constexpr const char* kFingerprintKey = "fingerprint";
    inline constexpr const char* kTextureFingerprintKey = "textureFingerprint";
    inline constexpr const char* kTextureReadFailuresKey = "textureReadFailures";
    inline constexpr const char* kDumpWriteFailed = "writing the dump failed";  // a NIF's blob error when its dump write failed
    inline constexpr const char* kFilesKey = "files";  // the manifest's per-file stamps (DumpedFiles), 2026-10-23
    inline constexpr const char* kNeededKey = "needed";  // (disk trim) the hash of the files the dump was limited to, or "all"
    inline constexpr const char* kNeededRetry = "retry";  // a left-out file would not delete: never "unchanged" next start

    /// (disk trim, 2026-10-26) The files a dump is limited to (DiskTrimRules::Needed); null: every NIF and planned texture.
    /// A file outside it is not written, and its earlier dump file is deleted; the request still lists it as dumped
    using Only = std::set<std::string>;

    /// The manifest's kNeededKey value for a_only
    [[nodiscard]] std::string NeededText(const Only* a_only);

    /// UTF-8 dump path -> "<size>:<fnv64 hex>" of the bytes the dump holds for it (the manifest's "files")
    using DumpedFiles = std::map<std::string, std::string>;

    /// (incremental dump, 2026-10-23) What lets Dump keep a file instead of reading and writing it again: the last manifest's
    /// stamps, and each planned texture's FNV-1a 64 as the keys hashed it this start (SlotKeys). A NIF is kept when its
    /// stamp (size and the one read's fnv64) is the last manifest's and the dumped file has that size; a texture when the
    /// stamp of its size and the keys' fnv64 is, and the file has that size - neither read nor written again
    struct Reuse
    {
        DumpedFiles previous;
        std::unordered_map<std::string, std::uint64_t> textureFnv;  // Data path (as Texture::path) -> fnv64
    };

    /// The manifest's "files" (an older manifest without them: none, so everything is written)
    [[nodiscard]] DumpedFiles DumpedFilesOf(const nlohmann::json& a_manifest);

    struct Nif
    {
        std::string path;  // Data-relative, normalised
        NifDump::ModelBlob blob;
        std::vector<std::byte> bytes;  // J7: kept from the one read, for the dump (released after it)
    };

    /// A Data-relative path (DataPathRule::Normalize's result, ANSI bytes) under a_root; throws when it would leave it
    [[nodiscard]] std::filesystem::path UnderRoot(const std::filesystem::path& a_root, const std::string& a_dataPath);

    /// J6: \\?\ in front of an absolute path, so a dump path past MAX_PATH still opens
    [[nodiscard]] std::filesystem::path LongPath(const std::filesystem::path& a_path);

    /// a_text into a_file through a temporary file and a rename (no half-written request is ever read)
    void WriteWhole(const std::filesystem::path& a_file, const std::string& a_text);

    /// J6: every dumped NIF and planned texture is there with its size (a_readFailed: textures the last dump could not read)
    [[nodiscard]] bool DumpIntact(const std::filesystem::path& a_folder, const std::vector<Nif>& a_nifs,
        const std::vector<CopyRequestData::Texture>& a_textures, const std::set<std::string>& a_readFailed, const Only* a_only = nullptr);

    /// U10: an unchanged dump tries once more the textures the last dump could not read; its state text. a_writeFailures:
    /// the files it could not write (AC3)
    [[nodiscard]] std::string RetryReadFailures(const std::filesystem::path& a_folder, std::vector<CopyRequestData::Texture>& a_textures,
        const std::set<std::string>& a_readFailed, const std::string& a_fingerprint, const std::string& a_textureFingerprint,
        int& a_writeFailures, const DumpedFiles& a_previous = {}, const Only* a_only = nullptr);

    /// Every NIF's bytes and every planned texture written under data\, then the manifest; its state text. a_writeFailures:
    /// the files that could not be written or whose path was refused (AC1: the request then says dumpComplete false).
    /// a_reuse (the slot route): the files whose stamps match are kept as they are (Reuse)
    [[nodiscard]] std::string Dump(const std::filesystem::path& a_folder, std::vector<Nif>& a_nifs,
        std::vector<CopyRequestData::Texture>& a_textures, const std::string& a_fingerprint, const std::string& a_textureFingerprint,
        int& a_writeFailures, const Reuse* a_reuse = nullptr, const Only* a_only = nullptr);
}
