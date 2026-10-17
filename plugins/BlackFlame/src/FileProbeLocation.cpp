#include "FileProbeLocation.h"

#include "FileProbeEngine.h"
#include "SehGuard.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string_view>

#include <Windows.h>

namespace BlackFlame::FileProbe::VirtualFiles
{
    namespace
    {
        using RE::BSResource::ErrorCode;
        using Bytes = std::vector<std::byte>;

        constexpr std::string_view kDataPrefix = "data\\";
        constexpr const char* kLocationName = "HeartOfMagic BlackFlame file probe (memory)";
        constexpr std::uint64_t kAnyPathCallsLogged = 8;   // the first calls of any path: the path form the engine passes
        constexpr std::uint64_t kOurCallsLogged = 64;      // calls naming our folder: every one, up to this
        // StreamBase's count field sits at 0x0C (SE, AE before 1.6.1170) or 0x10 (later AE): both zeroed in our streams
        constexpr std::size_t kCountFieldsBegin = 0x0C;
        constexpr std::size_t kCountFieldsSize = 0x08;
        static_assert(sizeof(RE::BSResource::StreamBase) == 0x18, "built for SE and AE: StreamBase holds both count fields");
        static_assert(sizeof(FILETIME) == sizeof(REX::W32::FILETIME));

        std::atomic<std::uint64_t> g_calls{ 0 };
        std::atomic<std::uint64_t> g_ourCalls{ 0 };
        std::atomic<std::uint64_t> g_createStream{ 0 };
        std::atomic<std::uint64_t> g_createAsync{ 0 };
        std::atomic<std::uint64_t> g_served{ 0 };
        std::atomic<std::uint64_t> g_getInfo{ 0 };
        std::atomic<std::uint64_t> g_traverse{ 0 };
        std::atomic<std::uint64_t> g_reads{ 0 };
        constexpr std::size_t kMaxLocations = 4;  // B and B-neg; a few spare

        std::string Normalize(const char* a_path)
        {
            std::string out;
            if (!a_path) {
                return out;
            }
            for (const char* c = a_path; *c; ++c) {
                const auto byte = static_cast<unsigned char>(*c);
                out.push_back(byte == '/' ? '\\' : static_cast<char>(byte < 0x80 ? std::tolower(byte) : byte));
            }
            while (!out.empty() && out.front() == '\\') {
                out.erase(0, 1);
            }
            if (out.starts_with(kDataPrefix)) {
                out.erase(0, kDataPrefix.size());
            }
            return out;
        }

        // Counted always; logged for the first calls of any path and for every call naming our folder
        void Note(const char* a_call, const char* a_raw, const std::string& a_key, const char* a_answer)
        {
            const auto call = g_calls.fetch_add(1);
            const bool ours = a_key.find(kFolder) != std::string::npos;
            const auto ourCall = ours ? g_ourCalls.fetch_add(1) : 0;
            if (call < kAnyPathCallsLogged || (ours && ourCall < kOurCallsLogged)) {
                logger::info("BlackFlame: file visibility probe: location {}(\"{}\") -> {} (thread {})", a_call, a_raw ? a_raw : "(null)",
                    a_answer, REX::W32::GetCurrentThreadId());
            }
        }

        void FillInfo(RE::BSResource::Info& a_info, std::uint64_t a_size)
        {
            FILETIME now{};
            ::GetSystemTimeAsFileTime(&now);
            std::memcpy(&a_info.modifyTime, &now, sizeof(now));
            std::memcpy(&a_info.createTime, &now, sizeof(now));
            a_info.fileSize = a_size;
        }

        // Puts a_stream (one reference, ours) into the engine's out slot; what was there is given back
        void Hand(RE::BSTSmartPointer<RE::BSResource::Stream>& a_out, RE::BSResource::Stream* a_stream)
        {
            auto** slot = reinterpret_cast<RE::BSResource::Stream**>(&a_out);
            auto* old = *slot;
            *slot = a_stream;
            Engine::Release(old);
        }

        class MemoryStream final : public RE::BSResource::Stream
        {
        public:
            MemoryStream(std::shared_ptr<const Bytes> a_bytes, std::string a_name, std::uint64_t a_position) :
                Stream(static_cast<std::uint32_t>(a_bytes->size())),
                m_bytes(std::move(a_bytes)),
                m_name(std::move(a_name)),
                m_position(a_position)
            {
                std::memset(reinterpret_cast<std::byte*>(this) + kCountFieldsBegin, 0, kCountFieldsSize);
            }

            // A new stream with one reference at the engine's count offset (Register checked that it is known)
            static MemoryStream* Make(std::shared_ptr<const Bytes> a_bytes, std::string a_name, std::uint64_t a_position)
            {
                auto* stream = new MemoryStream(std::move(a_bytes), std::move(a_name), a_position);  // game heap (StreamBase)
                (void)Engine::SetFirstReference(stream);
                return stream;
            }

            ErrorCode DoOpen() override
            {
                m_position = 0;
                return ErrorCode::kNone;
            }

            void DoClose() override {}

            ErrorCode DoGetInfo(RE::BSResource::Info& a_info) override
            {
                FillInfo(a_info, m_bytes->size());
                return ErrorCode::kNone;
            }

            void DoClone(RE::BSTSmartPointer<RE::BSResource::Stream>& a_out) const override
            {
                Hand(a_out, Make(m_bytes, m_name, m_position));
            }

            ErrorCode DoRead(void* a_buffer, std::uint64_t a_toRead, std::uint64_t& a_read) const override
            {
                g_reads.fetch_add(1);
                const std::uint64_t size = m_bytes->size();
                const std::uint64_t count = m_position < size ? (std::min)(a_toRead, size - m_position) : 0;
                if (count > 0 && !a_buffer) {
                    a_read = 0;
                    return ErrorCode::kInvalidParam;
                }
                if (count > 0) {
                    std::memcpy(a_buffer, m_bytes->data() + m_position, static_cast<std::size_t>(count));
                }
                m_position += count;
                a_read = count;
                return ErrorCode::kNone;
            }

            ErrorCode DoWrite(const void*, std::uint64_t, std::uint64_t& a_written) const override
            {
                a_written = 0;
                return ErrorCode::kUnsupported;
            }

            ErrorCode DoSeek(std::uint64_t a_toSeek, RE::BSResource::SeekMode a_mode, std::uint64_t& a_sought) const override
            {
                const auto size = static_cast<std::int64_t>(m_bytes->size());
                const auto delta = static_cast<std::int64_t>(a_toSeek);
                std::int64_t target = 0;
                switch (a_mode) {
                case RE::BSResource::SeekMode::kSet:
                    target = delta;
                    break;
                case RE::BSResource::SeekMode::kCur:
                    target = static_cast<std::int64_t>(m_position) + delta;
                    break;
                case RE::BSResource::SeekMode::kEnd:
                    target = size + delta;
                    break;
                default:
                    return ErrorCode::kInvalidParam;
                }
                if (target < 0 || target > size) {
                    return ErrorCode::kInvalidParam;
                }
                m_position = static_cast<std::uint64_t>(target);
                a_sought = m_position;
                return ErrorCode::kNone;
            }

            bool DoGetName(RE::BSFixedString& a_dst) const override
            {
                a_dst = m_name.c_str();
                return true;
            }

        private:
            std::shared_ptr<const Bytes> m_bytes;
            std::string m_name;
            mutable std::uint64_t m_position = 0;  // DoRead and DoSeek are const in the engine's interface
        };

        class MemoryLocation final : public RE::BSResource::Location
        {
        public:
            explicit MemoryLocation(std::vector<File> a_files) :
                m_files(std::move(a_files))
            {}

            // Main thread; the engine's threads read the list under the shared lock
            void Add(File a_file)
            {
                std::unique_lock lock(m_mutex);
                m_files.push_back(std::move(a_file));
            }

            ErrorCode DoCreateStream(const char* a_path, RE::BSTSmartPointer<RE::BSResource::Stream>& a_stream,
                RE::BSResource::Location*& a_location, bool a_readOnly) override
            {
                try {
                    g_createStream.fetch_add(1);
                    const auto key = Normalize(a_path);
                    const auto file = Lookup(key);
                    if (!file) {
                        Note("DoCreateStream", a_path, key, "not ours (kNotExist)");
                        return ErrorCode::kNotExist;
                    }
                    // Served whatever a_readOnly says: the engine's own loose-file open passes false (FileProbeEngine.h);
                    // our stream refuses writes anyway
                    Hand(a_stream, MemoryStream::Make(file->bytes, file->key, 0));
                    a_location = this;
                    g_served.fetch_add(1);
                    Note("DoCreateStream", a_path, key, a_readOnly ? "SERVED from memory (readOnly true)" : "SERVED from memory (readOnly false)");
                    return ErrorCode::kNone;
                } catch (...) {
                    return ErrorCode::kMemoryError;  // nothing may throw into the engine
                }
            }

            // Location's default answers kUnsupported for every path (CommonLib Location.cpp), which could stop a lookup
            // that walks the locations at ours: a path not ours is kNotExist, as in DoCreateStream. Ours has no asynchronous
            // stream (no AsyncStream class of ours): kUnsupported, logged
            ErrorCode DoCreateAsyncStream(const char* a_path, RE::BSTSmartPointer<RE::BSResource::AsyncStream>&,
                RE::BSResource::Location*&, bool) override
            {
                try {
                    g_createAsync.fetch_add(1);
                    const auto key = Normalize(a_path);
                    const bool ours = Lookup(key).has_value();
                    Note("DoCreateAsyncStream", a_path, key, ours ? "ours, no async stream (kUnsupported)" : "not ours (kNotExist)");
                    return ours ? ErrorCode::kUnsupported : ErrorCode::kNotExist;
                } catch (...) {
                    return ErrorCode::kNotExist;
                }
            }

            ErrorCode DoDelete(const char* a_path) override
            {
                try {
                    const auto key = Normalize(a_path);
                    const bool ours = Lookup(key).has_value();
                    Note("DoDelete", a_path, key, ours ? "ours, not deleted (kUnsupported)" : "not ours (kNotExist)");
                    return ours ? ErrorCode::kUnsupported : ErrorCode::kNotExist;
                } catch (...) {
                    return ErrorCode::kNotExist;
                }
            }

            ErrorCode DoTraversePrefix(const char* a_path, RE::BSResource::LocationTraverser& a_traverser) override
            {
                try {
                    g_traverse.fetch_add(1);
                    const auto prefix = Normalize(a_path);
                    std::vector<std::string> names;
                    {
                        std::shared_lock lock(m_mutex);
                        for (const auto& file : m_files) {
                            if (file.key.starts_with(prefix)) {
                                names.push_back(file.key);
                            }
                        }
                    }
                    for (const auto& name : names) {  // outside the lock: the traverser may call back in
                        a_traverser.ProcessName(name.c_str(), *this);
                        logger::info("BlackFlame: file visibility probe: location DoTraversePrefix(\"{}\"): reported '{}'",
                            a_path ? a_path : "(null)", name);
                    }
                    Note("DoTraversePrefix", a_path, prefix, names.empty() ? "nothing under it" : "reported our files");
                    return ErrorCode::kNone;
                } catch (...) {
                    return ErrorCode::kMemoryError;
                }
            }

            ErrorCode DoGetInfo1(const char* a_path, RE::BSResource::Info& a_info, RE::BSResource::Location*& a_location) override
            {
                return Answer("DoGetInfo1", a_path, a_info, &a_location);
            }

            ErrorCode DoGetInfo2(const char* a_path, RE::BSResource::Info& a_info, RE::BSResource::LocationTraverser*) override
            {
                return Answer("DoGetInfo2", a_path, a_info, nullptr);
            }

            const char* DoGetName() const override { return kLocationName; }

        private:
            std::optional<File> Lookup(const std::string& a_key) const
            {
                std::shared_lock lock(m_mutex);
                const auto it = std::ranges::find(m_files, a_key, &File::key);
                return it == m_files.end() ? std::nullopt : std::optional<File>(*it);
            }

            ErrorCode Answer(const char* a_call, const char* a_path, RE::BSResource::Info& a_info, RE::BSResource::Location** a_location)
            {
                try {
                    g_getInfo.fetch_add(1);
                    const auto key = Normalize(a_path);
                    const auto file = Lookup(key);
                    if (!file) {
                        Note(a_call, a_path, key, "not ours (kNotExist)");
                        return ErrorCode::kNotExist;
                    }
                    FillInfo(a_info, file->bytes->size());
                    if (a_location) {
                        *a_location = this;
                    }
                    Note(a_call, a_path, key, "ours: size given");
                    return ErrorCode::kNone;
                } catch (...) {
                    return ErrorCode::kMemoryError;
                }
            }

            mutable std::shared_mutex m_mutex;
            std::vector<File> m_files;  // under m_mutex: the engine reads it from any thread
        };

        std::mutex g_locationsMutex;
        std::vector<MemoryLocation*> g_locations;  // kept for the process: the engine holds them (under g_locationsMutex)
    }

    std::optional<std::size_t> Register(std::vector<File> a_files, std::uint32_t a_priority, std::string& a_note)
    {
        if (!Engine::StreamCountOffset()) {
            a_note = "not registered: the engine's stream count offset is unknown (our streams could not be counted)";
            return std::nullopt;
        }
        std::lock_guard guard(g_locationsMutex);
        if (g_locations.size() >= kMaxLocations) {
            a_note = std::format("not registered: {} memory locations registered already this session", g_locations.size());
            return std::nullopt;
        }
        std::string files;
        for (const auto& file : a_files) {
            files += std::format("{}{} ({} bytes)", files.empty() ? "" : ", ", file.key, file.bytes->size());
        }
        // Kept for the rest of the process on purpose: the engine holds this pointer and nothing unregisters it
        auto* location = new MemoryLocation(std::move(a_files));  // game heap (Location's TES_HEAP_REDEFINE_NEW)
        const bool ok = GuardedKeep("file probe RegisterLocation", [&]() { RE::BSResource::RegisterLocation(location, a_priority); });
        a_note = std::format("RegisterLocation(memory location {}, priority {}) {}; serving {}", g_locations.size(), a_priority,
            ok ? "returned" : "FAULTED (logged)", files.empty() ? "nothing yet" : files);
        if (!ok) {
            return std::nullopt;  // what the engine did with it is unknown: never touched again
        }
        g_locations.push_back(location);
        return g_locations.size() - 1;
    }

    bool AddFile(std::size_t a_location, File a_file)
    {
        std::lock_guard guard(g_locationsMutex);
        if (a_location >= g_locations.size()) {
            return false;
        }
        g_locations[a_location]->Add(std::move(a_file));
        return true;
    }

    std::string CountersText()
    {
        return std::format("location calls: {} in all, {} naming {}; DoCreateStream {} (served {}), DoCreateAsyncStream {}, DoGetInfo {}, DoTraversePrefix {}, "
                           "reads from our streams {}",
            g_calls.load(), g_ourCalls.load(), kFolder, g_createStream.load(), g_served.load(), g_createAsync.load(), g_getInfo.load(), g_traverse.load(),
            g_reads.load());
    }
}