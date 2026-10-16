#include "NifDump.h"

#include <format>

namespace BlackFlame::NifDump
{
    namespace
    {
        constexpr std::uint64_t kFnvPrime = 0x100000001B3ull;
        constexpr std::size_t kChunk = 64 * 1024;
        constexpr std::string_view kMeshesRoot = "meshes\\";
        constexpr std::string_view kDataRoot = "data\\";
        constexpr int kMaxChunks = 1 << 16;  // 4 GiB of 64 KiB reads: a stream that never ends is refused

        const char* ErrorText(RE::BSResource::ErrorCode a_code)
        {
            using E = RE::BSResource::ErrorCode;
            switch (a_code) {
            case E::kNone:
                return "none";
            case E::kNotExist:
                return "not exist";
            case E::kInvalidPath:
                return "invalid path";
            case E::kFileError:
                return "file error";
            case E::kInvalidType:
                return "invalid type";
            case E::kMemoryError:
                return "memory error";
            case E::kBusy:
                return "busy";
            case E::kInvalidParam:
                return "invalid parameter";
            default:
                return "unsupported";
            }
        }
    }

    std::optional<std::string> NormalizeModelPath(std::string_view a_model)
    {
        std::string path;
        path.reserve(a_model.size() + kMeshesRoot.size());
        for (const char raw : a_model) {
            const auto c = static_cast<unsigned char>(raw);
            if (c == ':') {
                return std::nullopt;  // a drive letter or a stream name
            }
            const char out = c == '/' ? '\\' : static_cast<char>(c < 0x80 ? std::tolower(c) : c);
            if (out == '\\' && (path.empty() || path.back() == '\\')) {
                continue;  // a leading or repeated separator
            }
            path.push_back(out);
        }
        for (const auto root : { kDataRoot, kMeshesRoot }) {
            if (path.starts_with(root)) {
                path.erase(0, root.size());
            }
        }
        if (path.empty() || path.back() == '\\') {
            return std::nullopt;
        }
        for (std::size_t at = 0; at < path.size();) {
            const auto end = std::min(path.find('\\', at), path.size());
            if (std::string_view(path).substr(at, end - at) == "..") {
                return std::nullopt;
            }
            at = end + 1;
        }
        return std::string(kMeshesRoot) + path;
    }

    std::uint64_t Fnv64(const void* a_bytes, std::size_t a_size, std::uint64_t a_hash) noexcept
    {
        const auto* bytes = static_cast<const unsigned char*>(a_bytes);
        for (std::size_t i = 0; i < a_size; ++i) {
            a_hash ^= bytes[i];
            a_hash *= kFnvPrime;
        }
        return a_hash;
    }

    ModelBlob ReadModel(const std::string& a_dataPath, std::vector<std::byte>* a_outBytes)
    {
        ModelBlob blob;
        blob.dataPath = a_dataPath;
        RE::BSResourceNiBinaryStream file(a_dataPath.c_str());
        auto* stream = file.good() ? file.stream.get() : nullptr;
        if (!stream) {
            blob.error = std::format("does not open (last error {})", ErrorText(file.lastError));
            return blob;
        }
        // StreamBase::totalSize is at 0x08 on SE and AE alike (RE/S/StreamBase.h)
        const std::uint64_t total = stream->totalSize;
        std::uint64_t sought = 0;
        if (const auto code = stream->DoSeek(0, RE::BSResource::SeekMode::kSet, sought); code != RE::BSResource::ErrorCode::kNone) {
            blob.error = std::format("seek to 0 failed ({})", ErrorText(code));
            return blob;
        }
        std::vector<std::byte> chunk(kChunk);
        std::uint64_t hash = kFnvOffset;
        for (int i = 0; i < kMaxChunks; ++i) {
            std::uint64_t read = 0;
            const auto code = stream->DoRead(chunk.data(), chunk.size(), read);
            if (code != RE::BSResource::ErrorCode::kNone) {
                blob.error = std::format("read failed at byte {} ({})", blob.size, ErrorText(code));
                return blob;
            }
            if (read == 0) {
                break;
            }
            if (read > chunk.size()) {
                blob.error = std::format("a read reported {} bytes for a {} byte buffer", read, chunk.size());
                return blob;
            }
            hash = Fnv64(chunk.data(), static_cast<std::size_t>(read), hash);
            if (a_outBytes) {
                a_outBytes->insert(a_outBytes->end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(read));
            }
            blob.size += read;
        }
        if (blob.size != total) {
            blob.error = std::format("read {} bytes, the stream says {}", blob.size, total);
            return blob;
        }
        blob.fnv64 = hash;
        blob.ok = true;
        return blob;
    }
}
