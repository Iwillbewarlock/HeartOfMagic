// =============================================================================
// FileIndex - engine file index: Register (RegisterLoose) and Find (FindRecord), lifted from FileProbeEngine
// =============================================================================
// The guard, the lookup, the loose stream and its count at the engine's offset, as the spike's FileProbeEngine.cpp had
// them; Register adds the single-launch rule (FindRecord first, a loose record kept, an archive record a clash).

#include "FileIndex.h"
#include "FileIndexEngine.h"

#include "SehGuard.h"

#include <array>
#include <chrono>
#include <cstring>
#include <format>

#include <intrin.h>

namespace BlackFlame::FileIndex
{
    namespace
    {
        using namespace Engine;

        std::atomic<std::uint32_t> g_registered{ 0 };
        std::atomic<std::uint32_t> g_alreadyLoose{ 0 };
        std::atomic<std::uint32_t> g_clashes{ 0 };
        std::atomic<std::uint32_t> g_failed{ 0 };
        std::atomic<std::int64_t> g_micros{ 0 };
        std::atomic<bool> g_firstLogged{ false };

        std::string IdText(const ID& a_id)
        {
            std::string ext;
            for (const char c : a_id.ext) {
                ext.push_back(c ? c : '.');
            }
            return std::format("file=0x{:08X} ext='{}' dir=0x{:08X}", a_id.file, ext, a_id.dir);
        }

        std::uint32_t CountOf(const Stream* a_stream, std::uint32_t a_offset)
        {
            const auto* flags = reinterpret_cast<const volatile std::uint32_t*>(reinterpret_cast<const std::byte*>(a_stream) + a_offset);
            return (*flags & kRefMask) / kRefOne;
        }

        /// One reference given back at the engine's offset; the stream deleted through its virtual destructor at zero, as
        /// the engine's own release does
        void Release(Stream* a_stream) noexcept
        {
            const auto offset = CountOffset().load();
            if (!a_stream || !offset) {
                return;  // never counted by us: nothing of ours to give back
            }
            auto* flags = reinterpret_cast<volatile long*>(reinterpret_cast<std::byte*>(a_stream) + offset);
            const auto after = static_cast<std::uint32_t>(_InterlockedExchangeAdd(flags, -static_cast<long>(kRefOne))) - kRefOne;
            if ((after & kRefMask) == 0) {
                delete a_stream;  // the engine's release protocol: the virtual (scalar deleting) destructor, its own heap
            }
        }

        /// An engine smart pointer slot the engine fills (DoCreateStream) and copies (RegisterLoose); released at the
        /// engine's offset. CommonLib's BSTSmartPointer destructor never runs on it
        class StreamSlot
        {
        public:
            StreamSlot() = default;
            ~StreamSlot() { Release(m_raw); }
            StreamSlot(const StreamSlot&) = delete;
            StreamSlot& operator=(const StreamSlot&) = delete;

            [[nodiscard]] StreamPtr& Ptr() noexcept { return *reinterpret_cast<StreamPtr*>(&m_raw); }
            [[nodiscard]] Stream* Get() const noexcept { return m_raw; }

        private:
            Stream* m_raw = nullptr;  // the only member of a BSTSmartPointer (RE/B/BSTSmartPointer.h, _ptr at 0)
        };
        static_assert(sizeof(StreamPtr) == sizeof(Stream*));

        // The engine's guard: lock and search state, released by its destructor however the scope ends (/EHa)
        class IndexGuard
        {
        public:
            IndexGuard() { TheState().guardCtor(m_buffer.data()); }
            ~IndexGuard() { TheState().guardDtor(m_buffer.data()); }
            IndexGuard(const IndexGuard&) = delete;
            IndexGuard& operator=(const IndexGuard&) = delete;

            [[nodiscard]] void* Guard() noexcept { return m_buffer.data(); }
            [[nodiscard]] void* Search() noexcept { return m_buffer.data() + kSearchOffset; }

        private:
            alignas(16) std::array<std::byte, kGuardSize> m_buffer{};
        };

        Lookup FindUnder(IndexGuard& a_guard, const ID& a_id)
        {
            Lookup result;
            std::byte* record = nullptr;
            result.found = TheState().find(a_guard.Search(), &a_id, &record) && record;
            if (result.found) {
                std::memcpy(&result.sizeFlags, record + kRecordSizeFlags, sizeof(result.sizeFlags));
                result.archive = result.sizeFlags < 0;
            }
            result.ran = true;
            return result;
        }

        bool CanLookUp()
        {
            const auto& state = TheState();
            return state.find && state.pathToId && state.guardCtor && state.guardDtor && state.manager && *state.manager;
        }

        Lookup FindChecked(const std::string& a_dataPath)
        {
            Lookup result;
            if (!CanLookUp()) {
                return result;
            }
            (void)GuardedKeep("file index lookup", [&]() {
                ID id{};
                TheState().pathToId(&id, a_dataPath.c_str());
                IndexGuard guard;
                result = FindUnder(guard, id);
            });
            return result;
        }

        RE::BSResource::ErrorCode OpenLoose(const std::string& a_dataPath, StreamSlot& a_out)
        {
            auto code = RE::BSResource::ErrorCode::kUnsupported;
            auto* globals = TheState().globals;
            if (!globals || !CountOffset().load()) {
                return code;
            }
            const std::string path = "data\\" + a_dataPath;
            (void)GuardedKeep("file index loose stream", [&]() {
                RE::BSResource::Location* where = nullptr;
                code = globals->DoCreateStream(path.c_str(), a_out.Ptr(), where, false);
            });
            return code;
        }

        Registration RegisterChecked(const std::string& a_dataPath, std::uint64_t a_expectedSize)
        {
            if (!Available()) {
                return { Outcome::kUnavailable, "not registered: " + Unavailable() };
            }
            Lookup before;
            {
                before = FindChecked(a_dataPath);
                if (!before.ran) {
                    return { Outcome::kFault, "not registered: the lookup before faulted (logged)" };
                }
                if (before.found) {
                    return before.archive ? Registration{ Outcome::kArchiveClash, "an archive record of this path: a name clash, never registered" } :
                                            Registration{ Outcome::kAlreadyLoose, "a loose record is there: RegisterLoose not called" };
                }
            }
            StreamSlot stream;
            const auto code = OpenLoose(a_dataPath, stream);
            if (code != RE::BSResource::ErrorCode::kNone || !stream.Get()) {
                return { Outcome::kOpenFailed, std::format("not registered: DoCreateStream(\"data\\{}\", readOnly false) gave error {}{}",
                    a_dataPath, static_cast<int>(code), stream.Get() ? "" : ", no stream") };
            }
            std::uint64_t streamSize = 0;
            if (!GuardedKeep("file index loose stream size", [&]() { streamSize = stream.Get()->totalSize; })) {
                return { Outcome::kFault, "not registered: the stream's size could not be read (FAULT, logged)" };
            }
            if (streamSize != a_expectedSize) {  // FQ3: not the file we wrote: never registered
                return { Outcome::kSizeMismatch, std::format("not registered: the stream says {} bytes, {} were expected", streamSize, a_expectedSize) };
            }
            const auto offset = CountOffset().load();
            const auto countBefore = CountOf(stream.Get(), offset);
            std::uint32_t countAfter = 0;
            Lookup underGuard;
            bool registered = false;
            const bool ok = GuardedKeep("file index RegisterLoose", [&]() {
                ID id{};
                TheState().pathToId(&id, a_dataPath.c_str());
                IndexGuard guard;
                underGuard = FindUnder(guard, id);
                if (underGuard.found) {
                    return;  // registered between the two lookups (one caller, the main thread: not expected)
                }
                TheState().registerLoose(guard.Guard(), &id, &stream.Ptr());
                registered = true;
                countAfter = CountOf(stream.Get(), offset);
            });
            if (!ok) {
                return { Outcome::kFault, "RegisterLoose FAULTED (logged)" };
            }
            if (!registered) {
                return { underGuard.archive ? Outcome::kArchiveClash : Outcome::kAlreadyLoose, "a record appeared before RegisterLoose" };
            }
            const auto after = FindChecked(a_dataPath);
            const auto note = std::format("{}; stream size {} = the bytes expected, references {} -> {}; before: {}; after: {}",
                DescribeIds(a_dataPath), streamSize, countBefore, countAfter, Describe(before), Describe(after));
            if (!after.found || after.archive) {
                return { Outcome::kNotFoundAfter, "RegisterLoose ran but FindRecord does not find a loose record: " + note };
            }
            return { Outcome::kRegistered, note };
        }
    }

    namespace Engine
    {
        bool RawFind(const std::string& a_dataPath, bool& a_found, bool& a_archive)
        {
            const auto lookup = FindChecked(a_dataPath);
            a_found = lookup.found;
            a_archive = lookup.archive;
            return lookup.ran;
        }

        std::string DescribeIds(const std::string& a_dataPath)
        {
            const auto pathToId = TheState().pathToId;
            if (!pathToId) {
                return "ids: PathToID not available";
            }
            ID fromPathToId{};
            ID fromCommonLib{};
            const bool ok = GuardedKeep("file index path ids", [&]() {
                pathToId(&fromPathToId, a_dataPath.c_str());
                fromCommonLib.GenerateFromPath(a_dataPath.c_str());
            });
            if (!ok) {
                return "ids: FAULT (logged)";
            }
            const bool same = fromPathToId.file == fromCommonLib.file && fromPathToId.dir == fromCommonLib.dir &&
                              std::memcmp(fromPathToId.ext, fromCommonLib.ext, sizeof(fromPathToId.ext)) == 0;
            return std::format("ids of '{}': PathToID {} | GenerateFromPath {} ({})", a_dataPath, IdText(fromPathToId),
                IdText(fromCommonLib), same ? "the same" : "DIFFERENT");
        }
    }

    Lookup Find(const std::string& a_dataPath)
    {
        return Available() ? FindChecked(a_dataPath) : Lookup{};
    }

    std::string Describe(const Lookup& a_lookup)
    {
        if (!a_lookup.ran) {
            return "index lookup not run";
        }
        if (!a_lookup.found) {
            return "no record";
        }
        return std::format("{} record (+0x0C = 0x{:08X})", a_lookup.archive ? "archive" : "loose",
            static_cast<std::uint32_t>(a_lookup.sizeFlags));
    }

    const char* OutcomeName(Outcome a_outcome) noexcept
    {
        switch (a_outcome) {
        case Outcome::kRegistered: return "registered";
        case Outcome::kAlreadyLoose: return "already loose";
        case Outcome::kArchiveClash: return "archive clash";
        case Outcome::kUnavailable: return "index not usable";
        case Outcome::kOpenFailed: return "loose stream did not open";
        case Outcome::kSizeMismatch: return "size mismatch";
        case Outcome::kFault: return "fault";
        case Outcome::kNotFoundAfter: return "not found after RegisterLoose";
        }
        return "unknown";
    }

    Registration Register(const std::string& a_dataPath, std::uint64_t a_expectedSize)
    {
        const auto start = std::chrono::steady_clock::now();
        auto result = RegisterChecked(a_dataPath, a_expectedSize);
        g_micros.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
        switch (result.outcome) {
        case Outcome::kRegistered:
            g_registered.fetch_add(1);
            if (!g_firstLogged.exchange(true)) {
                logger::info("BlackFlame: file index: first registration of the session, '{}': {}", a_dataPath, result.note);
            }
            break;
        case Outcome::kAlreadyLoose:
            g_alreadyLoose.fetch_add(1);
            break;
        case Outcome::kArchiveClash:
            g_clashes.fetch_add(1);
            logger::warn("BlackFlame: file index: '{}' is an archive record: a name clash, not registered", a_dataPath);
            break;
        default:
            g_failed.fetch_add(1);
            logger::warn("BlackFlame: file index: '{}' {}: {}", a_dataPath, OutcomeName(result.outcome), result.note);
            break;
        }
        return result;
    }

    Counters GetCounters() noexcept
    {
        return { g_registered.load(), g_alreadyLoose.load(), g_clashes.load(), g_failed.load(), g_micros.load() };
    }
}
