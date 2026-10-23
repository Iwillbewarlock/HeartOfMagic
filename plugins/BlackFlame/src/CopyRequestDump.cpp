// =============================================================================
// COPY REQUEST DUMP - the NIF and texture dump beside request.json and its manifest (CopyRequestDump.h)
// =============================================================================
// Split from CopyRequest.cpp (600-line rule); the behaviour is CopyRequest.h's.

#include "CopyRequestDump.h"

#include "EncodingUtils.h"

#include <nlohmann/json.hpp>

#include <format>
#include <fstream>

namespace BlackFlame::CopyRequestDump
{
    namespace
    {
        constexpr int kJsonIndent = 1;  // as request.json (CopyRequest.cpp)
        using Json = nlohmann::ordered_json;
        using CopyRequestData::Texture;
        using CopyRequestData::kMiB;

        std::string Utf8(std::string_view a_text)
        {
            return EncodingUtils::SanitizeToUTF8(std::string(a_text));
        }

        std::string PathText(const std::filesystem::path& a_path)
        {
            const auto text = a_path.u8string();
            return std::string(text.begin(), text.end());
        }
    }

    // A Data-relative path (DataPathRule::Normalize's result, ANSI bytes) as a path under a_root; the rule leaves no
    // drive, ':' or ".." token, and T11 checks the joined path still lies under a_root (throws otherwise)
    std::filesystem::path UnderRoot(const std::filesystem::path& a_root, const std::string& a_dataPath)
    {
        const auto utf8 = Utf8(a_dataPath);
        // AA4: a root given with a trailing separator compares as the folder itself (lexically_relative would see an empty
        // last element and answer "..\x")
        auto base = a_root.lexically_normal();
        if (!base.has_filename() && base.has_parent_path() && base != base.root_path()) {
            base = base.parent_path();
        }
        auto joined = base / std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
        const auto relative = joined.lexically_normal().lexically_relative(base);
        // AA4: "." is the root itself (an empty data path), never a file under it
        if (relative.empty() || relative == "." || relative.is_absolute() || *relative.begin() == "..") {
            throw std::runtime_error(std::format("dump path '{}' would leave the dump folder", utf8));
        }
        auto normal = joined.lexically_normal();
        if (!normal.has_filename() || normal.filename() == "." || normal.filename() == "..") {
            throw std::runtime_error(std::format("dump path '{}' names a folder, not a file", utf8));  // AB7
        }
        return normal;
    }

    // J6: \\?\ in front of an absolute path, so a dump path past MAX_PATH still opens
    std::filesystem::path LongPath(const std::filesystem::path& a_path)
    {
        std::error_code ec;
        auto full = std::filesystem::absolute(a_path, ec);
        if (ec) {
            return a_path;
        }
        std::wstring text = full.lexically_normal().wstring();
        if (text.starts_with(L"\\\\?\\")) {
            return full;
        }
        if (text.starts_with(L"\\\\")) {
            return std::filesystem::path(L"\\\\?\\UNC\\" + text.substr(2));
        }
        return std::filesystem::path(L"\\\\?\\" + text);
    }

    // a_text into a_file through a temporary file and a rename (no half-written request is ever read)
    void WriteWhole(const std::filesystem::path& a_file, const std::string& a_text)
    {
        auto temp = a_file;
        temp += L".tmp";
        const auto longTemp = LongPath(temp);  // AA4: as the dump's own files
        {
            std::ofstream out(longTemp, std::ios::binary | std::ios::trunc);
            out << a_text;
            out.close();
            if (!out) {
                std::error_code ec;
                std::filesystem::remove(longTemp, ec);
                throw std::runtime_error(std::format("writing {} failed", PathText(temp)));
            }
        }
        try {
            std::filesystem::rename(longTemp, LongPath(a_file));  // MSVC: MoveFileExW with MOVEFILE_REPLACE_EXISTING
        } catch (const std::filesystem::filesystem_error&) {
            std::error_code ec;
            std::filesystem::remove(longTemp, ec);  // AA4: no .tmp left behind
            throw;
        }
    }

    // J6: "unchanged" holds only while every dumped NIF and planned texture (T1) is there with its size; a texture the
    // last dump could not read (a_readFailed, UTF-8 paths) is not expected
    bool DumpIntact(const std::filesystem::path& a_folder, const std::vector<Nif>& a_nifs, const std::vector<Texture>& a_textures,
        const std::set<std::string>& a_readFailed)
    {
        const auto root = a_folder / kDumpRoot;
        const auto sizeIs = [&](const std::string& a_path, std::uint64_t a_size) {
            std::error_code ec;
            try {
                return std::filesystem::file_size(LongPath(UnderRoot(root, a_path)), ec) == a_size && !ec;
            } catch (const std::runtime_error&) {
                return false;  // AC2: a path the dump refuses is not there (the dump then counts it as a failure)
            }
        };
        for (const auto& nif : a_nifs) {
            if (nif.blob.ok && !sizeIs(nif.path, nif.blob.size)) {
                return false;  // missing or another size
            }
        }
        for (const auto& texture : a_textures) {
            if (texture.planned && !a_readFailed.contains(Utf8(texture.path)) && !sizeIs(texture.path, texture.probe.totalSize)) {
                return false;
            }
        }
        return true;
    }

    // a_bytes into a_file (its folder made); false on a failure
    bool WriteDumpFile(const std::filesystem::path& a_file, const std::vector<std::byte>& a_bytes)
    {
        std::error_code ec;
        std::filesystem::create_directories(a_file.parent_path(), ec);
        std::ofstream out(a_file, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(a_bytes.data()), static_cast<std::streamsize>(a_bytes.size()));
        out.close();
        return static_cast<bool>(out);
    }

    namespace
    {
        std::string Stamp(std::uint64_t a_size, std::uint64_t a_fnv64)
        {
            return std::format("{}:{:016x}", a_size, a_fnv64);
        }

        // (incremental dump) the dumped file is the one the last manifest stamped a_stamp, and it still has that size
        bool Kept(const Reuse* a_reuse, const std::string& a_utf8, const std::string& a_stamp, const std::filesystem::path& a_file,
            std::uint64_t a_size)
        {
            if (!a_reuse) {
                return false;
            }
            const auto it = a_reuse->previous.find(a_utf8);
            std::error_code ec;
            return it != a_reuse->previous.end() && it->second == a_stamp && std::filesystem::file_size(a_file, ec) == a_size && !ec;
        }
    }

    DumpedFiles DumpedFilesOf(const nlohmann::json& a_manifest)
    {
        DumpedFiles out;
        const auto it = a_manifest.is_object() ? a_manifest.find(kFilesKey) : a_manifest.end();
        if (it == a_manifest.end() || !it->is_object()) {
            return out;
        }
        for (const auto& [path, stamp] : it->items()) {
            if (stamp.is_string()) {
                out.emplace(path, stamp.get<std::string>());
            }
        }
        return out;
    }

    // T1: every planned texture read (one at a time) and written under data\. One not planned has its stale copy
    // removed; one that opens but cannot be read is marked readFailed (the manifest lists it, so the next start does
    // not dump everything again for it); one that cannot be written counts as a failure (the manifest is not updated:
    // a disk error may pass). Either way it leaves the plan with its error for the request. Returns the write failures.
    // (incremental dump, 2026-10-23) a planned texture whose stamp (size, the keys' fnv64) the last manifest holds, and
    // whose file has that size, is kept: neither read nor written (a_kept). a_files: the stamps for the new manifest
    int DumpTextures(const std::filesystem::path& a_root, std::vector<Texture>& a_textures, int& a_written,
        std::uint64_t& a_bytes, DumpedFiles& a_files, int& a_kept, const Reuse* a_reuse)
    {
        int failed = 0;
        for (auto& texture : a_textures) {
            std::filesystem::path file;
            try {
                file = LongPath(UnderRoot(a_root, texture.path));
            } catch (const std::runtime_error& e) {
                // AC2: one refused path is one failure, not the end of the dump
                if (texture.planned) {
                    ++failed;
                    texture.planned = false;
                    texture.error = e.what();
                    logger::warn("BlackFlame: copy request: texture {} not dumped: {}", Utf8(texture.path), e.what());
                }
                continue;
            }
            std::error_code ec;
            const auto utf8 = Utf8(texture.path);
            if (texture.planned && a_reuse) {
                const auto fnv = a_reuse->textureFnv.find(texture.path);
                if (fnv != a_reuse->textureFnv.end()) {
                    const auto stamp = Stamp(texture.probe.totalSize, fnv->second);
                    if (Kept(a_reuse, utf8, stamp, file, texture.probe.totalSize)) {
                        a_files[utf8] = stamp;
                        ++a_kept;
                        continue;
                    }
                }
            }
            std::vector<std::byte> bytes;
            NifDump::ModelBlob blob;
            if (texture.planned) {
                blob = NifDump::ReadModel(texture.path, &bytes);
                if (!blob.ok) {
                    texture.planned = false;
                    texture.readFailed = true;
                    texture.error = std::format("{} (tried again at the next start)", blob.error);
                }
            }
            if (!texture.planned) {
                std::filesystem::remove(file, ec);
                continue;
            }
            if (!WriteDumpFile(file, bytes)) {
                ++failed;
                texture.planned = false;
                texture.error = "writing the dump failed";
                std::filesystem::remove(file, ec);
                logger::warn("BlackFlame: copy request: writing the dump of texture {} failed", Utf8(texture.path));
                continue;
            }
            a_files[utf8] = Stamp(blob.size, blob.fnv64);  // the bytes written
            ++a_written;
            a_bytes += bytes.size();
        }
        return failed;
    }

    // The dump's manifest: both fingerprints, the textures that opened but could not be read, and each dumped file's stamp
    void WriteManifest(const std::filesystem::path& a_folder, const std::string& a_fingerprint,
        const std::string& a_textureFingerprint, const std::vector<Texture>& a_textures, const DumpedFiles& a_files)
    {
        Json readFailures = Json::array();
        for (const auto& texture : a_textures) {
            if (texture.readFailed) {
                readFailures.push_back(Utf8(texture.path));
            }
        }
        Json files = Json::object();
        for (const auto& [path, stamp] : a_files) {
            files[path] = stamp;
        }
        const Json manifest{ { kFingerprintKey, a_fingerprint }, { kTextureFingerprintKey, a_textureFingerprint },
            { kTextureReadFailuresKey, std::move(readFailures) }, { kFilesKey, std::move(files) } };
        WriteWhole(a_folder / kManifestName, manifest.dump(kJsonIndent) + "\n");
    }

    // U10: an unchanged dump tries once more the textures the last dump could not read (only those; a read failure may
    // pass, e.g. a busy archive), and rewrites the manifest when one reads now (the last stamps kept, the new ones added)
    std::string RetryReadFailures(const std::filesystem::path& a_folder, std::vector<Texture>& a_textures,
        const std::set<std::string>& a_readFailed, const std::string& a_fingerprint, const std::string& a_textureFingerprint,
        int& a_writeFailures, const DumpedFiles& a_previous)
    {
        std::vector<Texture*> retry;
        std::vector<Texture> batch;
        for (auto& texture : a_textures) {
            if (texture.planned && a_readFailed.contains(Utf8(texture.path))) {
                retry.push_back(&texture);
                batch.push_back(texture);
            }
        }
        if (batch.empty()) {
            return "unchanged";
        }
        int written = 0;
        int kept = 0;
        std::uint64_t bytes = 0;
        auto files = a_previous;
        const int failed = DumpTextures(a_folder / kDumpRoot, batch, written, bytes, files, kept, nullptr);
        a_writeFailures = failed;  // AC3
        for (std::size_t i = 0; i < batch.size(); ++i) {
            *retry[i] = std::move(batch[i]);
        }
        if (written > 0 && failed == 0) {
            WriteManifest(a_folder, a_fingerprint, a_textureFingerprint, a_textures, files);
        }
        return std::format("unchanged; {} of {} textures the last dump could not read read now", written, retry.size());
    }

    // Every NIF's bytes from the one read written under data\ (a failed one's stale copy removed), then the textures (T1);
    // (incremental dump, 2026-10-23) with a_reuse a file the last manifest stamped as it is now stays as it is
    std::string Dump(const std::filesystem::path& a_folder, std::vector<Nif>& a_nifs, std::vector<Texture>& a_textures,
        const std::string& a_fingerprint, const std::string& a_textureFingerprint, int& a_writeFailures, const Reuse* a_reuse)
    {
        const auto root = a_folder / kDumpRoot;
        int written = 0;
        int failed = 0;
        int kept = 0;
        DumpedFiles files;
        // review: the last manifest no longer vouches for the dump once a file of it may change - a pass cut short (a write
        // failure, a crash on the loading screen) must not leave stamps of bytes it replaced. Not removable: nothing kept
        std::error_code removeEc;
        std::filesystem::remove(LongPath(a_folder / kManifestName), removeEc);
        if (removeEc) {
            logger::warn("BlackFlame: copy request: the last dump manifest could not be removed before the dump: every file is "
                         "written again");
            a_reuse = nullptr;
        }
        for (auto& nif : a_nifs) {
            std::filesystem::path file;
            try {
                file = LongPath(UnderRoot(root, nif.path));
            } catch (const std::runtime_error& e) {
                // AC2: one refused path is one failure (named as not read), not the end of the dump
                if (nif.blob.ok) {
                    ++failed;
                    nif.blob.ok = false;
                    nif.blob.error = kDumpWriteFailed;
                    std::vector<std::byte>().swap(nif.bytes);
                    logger::warn("BlackFlame: copy request: NIF {} not dumped: {}", Utf8(nif.path), e.what());
                }
                continue;
            }
            std::error_code ec;
            if (!nif.blob.ok) {
                std::filesystem::remove(file, ec);
                continue;
            }
            const auto utf8 = Utf8(nif.path);
            const auto stamp = Stamp(nif.blob.size, nif.blob.fnv64);
            if (Kept(a_reuse, utf8, stamp, file, nif.blob.size)) {
                files[utf8] = stamp;
                ++kept;
                std::vector<std::byte>().swap(nif.bytes);
                continue;
            }
            if (!WriteDumpFile(file, nif.bytes)) {
                ++failed;
                // AA4: no partial file, and the request names it as not read (the builder then keeps the source's model)
                std::filesystem::remove(file, ec);
                nif.blob.ok = false;
                nif.blob.error = kDumpWriteFailed;
                std::vector<std::byte>().swap(nif.bytes);
                logger::warn("BlackFlame: copy request: writing the dump of {} failed", Utf8(nif.path));
                continue;
            }
            files[utf8] = stamp;
            ++written;
            std::vector<std::byte>().swap(nif.bytes);
        }
        int textures = 0;
        std::uint64_t textureBytes = 0;
        failed += DumpTextures(root, a_textures, textures, textureBytes, files, kept, a_reuse);
        if (failed == 0) {
            WriteManifest(a_folder, a_fingerprint, a_textureFingerprint, a_textures, files);
        }
        const auto counts = std::format("{} NIFs, {} textures ({} MiB){}", written, textures, textureBytes / kMiB,
            a_reuse ? std::format("; {} files kept as they were", kept) : std::string());
        a_writeFailures = failed;
        return failed == 0 ? std::format("written ({})", counts) :
                             std::format("written with {} failure(s) ({}; the manifest not updated, so the next start "
                                         "dumps again)", failed, counts);
    }
}
