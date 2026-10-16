#include "NifDump.h"

#include "DataPathRule.h"

#include <atomic>
#include <format>

#include <Windows.h>

namespace BlackFlame::NifDump
{
    namespace
    {
        constexpr std::uint64_t kFnvPrime = 0x100000001B3ull;
        constexpr std::size_t kChunk = 64 * 1024;
        constexpr int kMaxChunks = 1 << 16;  // 4 GiB of 64 KiB reads: a stream that never ends is refused
        // A stream's own size reserved up front at most this (a bad totalSize must not ask for gigabytes; the vector grows)
        constexpr std::uint64_t kMaxReserve = 256ull * 1024ull * 1024ull;
        std::atomic<bool> g_seekWarned{ false };

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
        return DataPathRule::Normalize(a_model, DataPathRule::kMeshesRoot);
    }

    std::optional<std::string> NormalizeTexturePath(std::string_view a_texture)
    {
        return DataPathRule::Normalize(a_texture, DataPathRule::kTexturesRoot);
    }

    std::vector<std::string> TexturePathsIn(const std::vector<std::byte>& a_bytes)
    {
        return DataPathRule::TexturePathsIn(reinterpret_cast<const unsigned char*>(a_bytes.data()), a_bytes.size());
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

    ModelBlob ProbeModel(const std::string& a_dataPath)
    {
        ModelBlob blob;
        blob.dataPath = a_dataPath;
        std::optional<RE::BSResourceNiBinaryStream> file;
        file.emplace(a_dataPath.c_str());
        if (!file->good() && file->lastError == RE::BSResource::ErrorCode::kBusy) {
            file.emplace(a_dataPath.c_str());
        }
        if (auto* stream = file->good() ? file->stream.get() : nullptr) {
            blob.opened = true;
            blob.ok = true;
            blob.totalSize = stream->totalSize;
            blob.size = blob.totalSize;
        } else {
            blob.error = std::format("does not open (last error {})", ErrorText(file->lastError));
        }
        return blob;
    }

    ModelBlob ReadModel(const std::string& a_dataPath, std::vector<std::byte>* a_outBytes)
    {
        ModelBlob blob;
        blob.dataPath = a_dataPath;
        std::optional<RE::BSResourceNiBinaryStream> file;
        file.emplace(a_dataPath.c_str());
        if (!file->good() && file->lastError == RE::BSResource::ErrorCode::kBusy) {
            file.emplace(a_dataPath.c_str());  // J6: once more for a busy archive
        }
        auto* stream = file->good() ? file->stream.get() : nullptr;
        if (!stream) {
            blob.error = std::format("does not open (last error {})", ErrorText(file->lastError));
            return blob;
        }
        // StreamBase::totalSize is at 0x08 on SE and AE alike (RE/S/StreamBase.h)
        const std::uint64_t total = stream->totalSize;
        blob.opened = true;
        blob.totalSize = total;
        std::uint64_t sought = 0;
        if (const auto code = stream->DoSeek(0, RE::BSResource::SeekMode::kSet, sought); code == RE::BSResource::ErrorCode::kUnsupported) {
            if (!g_seekWarned.exchange(true)) {
                logger::warn("BlackFlame: copy request: a stream cannot seek ({}): read from where it opened (at 0); not "
                             "logged again", a_dataPath);
            }
        } else if (code != RE::BSResource::ErrorCode::kNone) {
            blob.error = std::format("seek to 0 failed ({})", ErrorText(code));
            return blob;
        }
        if (a_outBytes) {
            a_outBytes->reserve(static_cast<std::size_t>(std::min(total, kMaxReserve)));  // T14: one allocation for a texture
        }
        std::vector<std::byte> chunk(kChunk);
        std::uint64_t hash = kFnvOffset;
        for (int i = 0; i < kMaxChunks; ++i) {
            std::uint64_t read = 0;
            auto code = stream->DoRead(chunk.data(), chunk.size(), read);
            if (code == RE::BSResource::ErrorCode::kBusy) {
                read = 0;
                code = stream->DoRead(chunk.data(), chunk.size(), read);  // J6: once more
            }
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
