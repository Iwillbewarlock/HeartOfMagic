#pragma once

#include "Common.h"

#include "CopyRequestData.h"
#include "NifDump.h"

#include <cstddef>
#include <filesystem>
#include <set>
#include <string>
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
        const std::vector<CopyRequestData::Texture>& a_textures, const std::set<std::string>& a_readFailed);

    /// U10: an unchanged dump tries once more the textures the last dump could not read; its state text. a_writeFailures:
    /// the files it could not write (AC3)
    [[nodiscard]] std::string RetryReadFailures(const std::filesystem::path& a_folder, std::vector<CopyRequestData::Texture>& a_textures,
        const std::set<std::string>& a_readFailed, const std::string& a_fingerprint, const std::string& a_textureFingerprint,
        int& a_writeFailures);

    /// Every NIF's bytes and every planned texture written under data\, then the manifest; its state text. a_writeFailures:
    /// the files that could not be written or whose path was refused (AC1: the request then says dumpComplete false)
    [[nodiscard]] std::string Dump(const std::filesystem::path& a_folder, std::vector<Nif>& a_nifs,
        std::vector<CopyRequestData::Texture>& a_textures, const std::string& a_fingerprint, const std::string& a_textureFingerprint,
        int& a_writeFailures);
}
