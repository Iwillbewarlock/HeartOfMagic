#include "FileProbeEngine.h"
#include "FileProbeGuardCheck.h"

#include "Patch.h"
#include "SehGuard.h"

#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <set>

#include <intrin.h>

namespace BlackFlame::FileProbe::Engine
{
    namespace
    {
        using namespace GuardCheck;  // IndexGuard's structural checks (FileProbeGuardCheck.h)

        using ID = RE::BSResource::ID;
        using PathToIdFn = ID* (*)(ID*, const char*);
        using GuardCtorFn = void* (*)(void*);
        using GuardDtorFn = void (*)(void*);
        using FindRecordFn = bool (*)(void*, const ID*, std::byte**);
        using RegisterLooseFn = void (*)(void*, const ID*, StreamPtr*);

        constexpr std::size_t kGuardSize = 0x200;      // the engine's guard fits in this (Temp File Framework's buffer)
        constexpr std::size_t kSearchOffset = 0x08;    // the search state inside the guard
        constexpr std::size_t kRecordSizeFlags = 0x0C; // int32: negative = archive record
        constexpr std::size_t kIncRefWindow = 0x200;   // bytes of 68483 / 69839 scanned for the inlined IncRef
        constexpr std::size_t kFindWindow = 0x40;      // bytes of FindRecord scanned for the ArchiveManager load
        constexpr std::uint32_t kSeCountOffset = 0x0C;
        constexpr std::uint32_t kAeLateCountOffset = 0x10;
        constexpr std::uint32_t kRefOne = 0x1000;          // StreamBase::kRefCountBeg
        constexpr std::uint32_t kRefMask = 0xFFFFF000u;    // StreamBase::kRefCountMask
        constexpr std::size_t kMaxLocations = 64;          // a longer entry list is not GlobalLocations
        constexpr std::array<std::uint8_t, 4> kLockCmpxchgRbx{ 0xF0, 0x0F, 0xB1, 0x4B };  // lock cmpxchg [rbx+disp8], ecx
        constexpr std::array<std::uint8_t, 3> kMovRcxRip{ 0x48, 0x8B, 0x0D };            // mov rcx, [rip+disp32]
        constexpr std::size_t kMovRcxRipSize = 7;

        struct State
        {
            PathToIdFn pathToId = nullptr;
            GuardCtorFn guardCtor = nullptr;
            GuardDtorFn guardDtor = nullptr;
            FindRecordFn find = nullptr;
            RegisterLooseFn registerLoose = nullptr;
            void** manager = nullptr;
            std::string indexWhy = "not initialised";
            RE::BSResource::GlobalLocations* globals = nullptr;
            std::string globalsWhy = "not initialised";
        };
        State g_state;                               // written by Init (main thread) before any test reads it
        std::atomic<std::uint32_t> g_countOffset{ 0 };  // 0 = unknown; read by our streams on IO threads

        std::uintptr_t Base() { return REL::Module::get().base(); }

        // The count offset this build is expected to use: 0x0C on SE and on AE before 1.6.1170, 0x10 from 1.6.1170 on (Temp File
        // Framework's reading of the engines). CommonLib's StreamBase::GetFlags says 0x10 on every AE build: before 1.6.1170 the
        // two disagree, and the offset the running engine's code uses (the scan) must equal this one
        std::uint32_t ExpectedCountOffset()
        {
            return REL::Module::IsAE() && REL::Module::get().version() >= REL::Version(1, 6, 1170, 0) ? kAeLateCountOffset : kSeCountOffset;
        }

        // The inlined Stream::IncRef in the engine's CreateStream: every `lock cmpxchg [rbx+disp8], ecx` in the window
        std::optional<std::uint32_t> ScanCountOffset(std::vector<std::string>& a_lines)
        {
            const auto function = Patch::AddressOf(Patch::IdFor(68483, 69839));
            if (!Patch::IsExecutable(function, kIncRefWindow)) {
                a_lines.push_back(std::format("stream count: CreateStream at +0x{:X} is not readable code", function - Base()));
                return std::nullopt;
            }
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(function);
            std::set<std::uint32_t> found;
            std::string where;
            for (std::size_t i = 0; i + kLockCmpxchgRbx.size() < kIncRefWindow; ++i) {
                if (std::memcmp(bytes + i, kLockCmpxchgRbx.data(), kLockCmpxchgRbx.size()) == 0) {
                    const std::uint32_t disp = bytes[i + kLockCmpxchgRbx.size()];
                    found.insert(disp);
                    where += std::format("{}+0x{:X}:0x{:X}", where.empty() ? "" : ", ", i, disp);
                }
            }
            a_lines.push_back(std::format("stream count: CreateStream at SkyrimSE+0x{:X}, lock cmpxchg [rbx+disp8] in the first "
                                          "0x{:X} bytes: {}", function - Base(), kIncRefWindow, where.empty() ? "none" : where));
            if (found.size() != 1) {
                a_lines.push_back("stream count: not one offset found - streams cannot be counted, tests B and C off");
                return std::nullopt;
            }
            const auto offset = *found.begin();
            const auto expected = ExpectedCountOffset();
            if (offset != expected) {
                a_lines.push_back(std::format("stream count: CONFLICT: the scan found 0x{:X}, this build ({}) is expected to use 0x{:X} - "
                                              "tests B and C off", offset, REL::Module::get().version().string(), expected));
                return std::nullopt;
            }
            return offset;
        }

        bool FindLoadsManager(std::uintptr_t a_find, std::uintptr_t a_manager)
        {
            if (!Patch::IsExecutable(a_find, kFindWindow + kMovRcxRipSize)) {
                return false;
            }
            for (std::size_t i = 0; i < kFindWindow; ++i) {
                const auto site = a_find + i;
                if (std::memcmp(reinterpret_cast<const void*>(site), kMovRcxRip.data(), kMovRcxRip.size()) != 0) {
                    continue;
                }
                std::int32_t rel = 0;
                std::memcpy(&rel, reinterpret_cast<const void*>(site + kMovRcxRip.size()), sizeof(rel));
                if (site + kMovRcxRipSize + rel == a_manager) {
                    return true;
                }
            }
            return false;
        }

        void InitIndex(std::vector<std::string>& a_lines)
        {
            if (!Patch::HasIds({ Patch::IdFor(68631, 69971), Patch::IdFor(68321, 69681), Patch::IdFor(68322, 69682),
                                   Patch::IdFor(68329, 69689), Patch::IdFor(68327, 69687), Patch::IdFor(523840, 410404),
                                   Patch::IdFor(68635, 69979) },
                    "file probe index (PathToID, IndexGuard, FindRecord, RegisterLoose, ArchiveManager, GenerateFromPath)")) {
                g_state.indexWhy = "an Address Library id is missing (logged above)";
                return;
            }
            const auto find = Patch::AddressOf(Patch::IdFor(68329, 69689));
            const auto manager = Patch::AddressOf(Patch::IdFor(523840, 410404));
            if (!FindLoadsManager(find, manager)) {
                g_state.indexWhy = std::format("FindRecord (SkyrimSE+0x{:X}) does not load the ArchiveManager static "
                                               "(SkyrimSE+0x{:X}) in its first 0x{:X} bytes", find - Base(), manager - Base(), kFindWindow);
                return;
            }
            const auto ctor = Patch::AddressOf(Patch::IdFor(68321, 69681));
            const auto dtor = Patch::AddressOf(Patch::IdFor(68322, 69682));
            const auto ctorBody = FollowJump(ctor);
            const auto dtorBody = FollowJump(dtor);
            const bool ctorOk = TouchesManager(ctorBody, manager);
            const bool dtorManager = TouchesManager(dtorBody, manager);
            const bool dtorReadsGuard = ReadsOwnGuard(dtorBody);
            const bool dtorOk = dtorManager || (dtorReadsGuard && ctorOk);
            const char* dtorRule = dtorManager ? "touches the manager" :
                                   (dtorReadsGuard ? (ctorOk ? "reads its own guard field (ctor touched the manager)" :
                                                               "reads its own guard field but the ctor did not pass") :
                                                     "neither touches the manager nor reads its own guard field");
            a_lines.push_back(std::format("index: IndexGuard ctor {} touches the manager: {}; dtor {}: {} -> {}",
                BodyText(ctor, ctorBody), ctorOk ? "yes" : "NO", BodyText(dtor, dtorBody), dtorRule, dtorOk ? "accepted" : "NO"));
            if (!ctorOk || !dtorOk) {
                g_state.indexWhy = std::format("IndexGuard's layout is not confirmed (ctor touches the manager: {}; dtor: {}): the "
                                               "guard is not called", ctorOk ? "yes" : "no", dtorRule);
                return;
            }
            g_state.pathToId = reinterpret_cast<PathToIdFn>(Patch::AddressOf(Patch::IdFor(68631, 69971)));
            g_state.guardCtor = reinterpret_cast<GuardCtorFn>(Patch::AddressOf(Patch::IdFor(68321, 69681)));
            g_state.guardDtor = reinterpret_cast<GuardDtorFn>(Patch::AddressOf(Patch::IdFor(68322, 69682)));
            g_state.find = reinterpret_cast<FindRecordFn>(find);
            g_state.registerLoose = reinterpret_cast<RegisterLooseFn>(Patch::AddressOf(Patch::IdFor(68327, 69687)));
            g_state.manager = reinterpret_cast<void**>(manager);
            a_lines.push_back(std::format("index: PathToID +0x{:X}, IndexGuard +0x{:X}/+0x{:X} (buffer 0x{:X}, search at +0x{:X}), "
                                          "FindRecord +0x{:X} (loads the manager: yes), RegisterLoose +0x{:X}, manager +0x{:X} = {}",
                reinterpret_cast<std::uintptr_t>(g_state.pathToId) - Base(), reinterpret_cast<std::uintptr_t>(g_state.guardCtor) - Base(),
                reinterpret_cast<std::uintptr_t>(g_state.guardDtor) - Base(), kGuardSize, kSearchOffset, find - Base(),
                reinterpret_cast<std::uintptr_t>(g_state.registerLoose) - Base(), manager - Base(),
                *g_state.manager ? "set" : "NULL"));
            g_state.indexWhy = *g_state.manager ? "" : "the ArchiveManager is null (the archives have not loaded)";
        }

        // A list walk that faults or runs long means the hit is not GlobalLocations (SEH-guarded by the caller)
        std::size_t CountEntries(const RE::BSResource::GlobalLocations* a_globals)
        {
            std::size_t count = 0;
            for (auto* entry = a_globals->head; entry; entry = entry->next) {
                if (!entry->location || ++count > kMaxLocations) {
                    return 0;
                }
            }
            return count;
        }

        void InitGlobals(std::vector<std::string>& a_lines)
        {
            if (!Patch::HasIds({ Patch::IdFor(285855, 237007), Patch::IdFor(232012, 188191) },
                    "file probe GlobalLocations / LooseFileLocation vtables")) {
                g_state.globalsWhy = "a vtable id is missing (logged above)";
                return;
            }
            const auto vtable = RE::VTABLE_BSResource____GlobalLocations[0].address();
            const auto data = REL::Module::get().segment(REL::Segment::data);
            const auto* words = data.pointer<std::uintptr_t>();
            const std::size_t count = data.size() / sizeof(std::uintptr_t);
            int hits = 0;
            for (std::size_t i = 0; i < count; ++i) {
                if (words[i] != vtable) {
                    continue;
                }
                ++hits;
                auto* candidate = reinterpret_cast<RE::BSResource::GlobalLocations*>(const_cast<std::uintptr_t*>(words + i));
                std::size_t entries = 0;
                const bool ok = GuardedKeep("file probe GlobalLocations check", [&]() { entries = CountEntries(candidate); });
                a_lines.push_back(std::format("GlobalLocations candidate at SkyrimSE+0x{:X}: {} entries{}",
                    reinterpret_cast<std::uintptr_t>(candidate) - Base(), entries, ok ? "" : " (the walk faulted)"));
                if (ok && entries > 0) {
                    g_state.globals = candidate;
                    g_state.globalsWhy.clear();
                    return;
                }
            }
            g_state.globalsWhy = std::format("no object with GlobalLocations' vtable and a sane entry list in .data ({} vtable hits)", hits);
        }

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

        // The engine's guard: lock and search state, released by its destructor however the scope ends (/EHa)
        class IndexGuard
        {
        public:
            IndexGuard() { g_state.guardCtor(m_buffer.data()); }
            ~IndexGuard() { g_state.guardDtor(m_buffer.data()); }
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
            result.found = g_state.find(a_guard.Search(), &a_id, &record) && record;
            if (result.found) {
                std::memcpy(&result.sizeFlags, record + kRecordSizeFlags, sizeof(result.sizeFlags));
                result.archive = result.sizeFlags < 0;
            }
            result.ran = true;
            return result;
        }
    }

    void Init(std::vector<std::string>& a_lines)
    {
        if (const auto offset = ScanCountOffset(a_lines)) {
            g_countOffset.store(*offset);
            const auto commonLib = REL::Module::IsAE() ? kAeLateCountOffset : kSeCountOffset;
            a_lines.push_back(std::format("stream count at +0x{:X}, as expected for this build (CommonLib's StreamBase::GetFlags uses "
                                          "+0x{:X} here{})", *offset, commonLib, commonLib == *offset ? ": the same" : ": CONFLICT, CommonLib is wrong here"));
        }
        InitIndex(a_lines);
        if (!g_countOffset.load() && g_state.indexWhy.empty()) {
            g_state.indexWhy = "the engine's stream count offset is unknown";
        }
        InitGlobals(a_lines);
        if (!g_state.globalsWhy.empty()) {
            a_lines.push_back("GlobalLocations: " + g_state.globalsWhy);
        }
        if (!g_state.indexWhy.empty()) {
            a_lines.push_back("index: " + g_state.indexWhy);
        }
    }

    std::optional<std::uint32_t> StreamCountOffset() noexcept
    {
        const auto offset = g_countOffset.load();
        return offset ? std::optional<std::uint32_t>(offset) : std::nullopt;
    }

    void ConfirmIndex(const std::vector<std::string>& a_shippedDataPaths, std::vector<std::string>& a_lines)
    {
        if (!g_state.indexWhy.empty() || !g_state.globalsWhy.empty()) {
            return;
        }
        for (const auto& path : a_shippedDataPaths) {  // FQ5: the first one the index knows confirms the layout
            const auto lookup = Find(path);
            a_lines.push_back(std::format("index check on the shipped '{}': {}; {}", path, Describe(lookup), DescribeIds(path)));
            if (lookup.ran && lookup.found) {
                return;
            }
        }
        g_state.indexWhy = std::format("no index lookup of {} shipped file(s) found its record: the guard / FindRecord layout is not "
                                       "confirmed, so IndexGuard and RegisterLoose are not used", a_shippedDataPaths.size());
        a_lines.push_back("index: " + g_state.indexWhy);
    }

    std::string IndexUnavailable()
    {
        if (!g_state.globalsWhy.empty()) {
            return "GlobalLocations: " + g_state.globalsWhy;
        }
        return g_state.indexWhy;
    }

    std::string GlobalsUnavailable()
    {
        return g_state.globalsWhy;
    }

    bool SetFirstReference(Stream* a_stream) noexcept
    {
        const auto offset = g_countOffset.load();
        if (!a_stream || !offset) {
            return false;
        }
        auto* flags = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::byte*>(a_stream) + offset);
        *flags = (*flags & ~kRefMask) + kRefOne;
        return true;
    }

    void Release(Stream* a_stream) noexcept
    {
        const auto offset = g_countOffset.load();
        if (!a_stream || !offset) {
            return;  // never counted by us: nothing of ours to give back
        }
        auto* flags = reinterpret_cast<volatile long*>(reinterpret_cast<std::byte*>(a_stream) + offset);
        const auto after = static_cast<std::uint32_t>(_InterlockedExchangeAdd(flags, -static_cast<long>(kRefOne))) - kRefOne;
        if ((after & kRefMask) == 0) {
            delete a_stream;  // the engine's release protocol: the virtual (scalar deleting) destructor, its own heap
        }
    }

    std::string DescribeIds(const std::string& a_dataPath)
    {
        if (!g_state.pathToId) {
            return "ids: PathToID not available";
        }
        ID fromPathToId{};
        ID fromCommonLib{};
        const bool ok = GuardedKeep("file probe path ids", [&]() {
            g_state.pathToId(&fromPathToId, a_dataPath.c_str());
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

    Lookup Find(const std::string& a_dataPath)
    {
        Lookup result;
        if (!g_state.find || !g_state.manager || !*g_state.manager) {
            return result;
        }
        (void)GuardedKeep("file probe index lookup", [&]() {
            ID id{};
            g_state.pathToId(&id, a_dataPath.c_str());
            IndexGuard guard;
            result = FindUnder(guard, id);
        });
        return result;
    }

    std::string Describe(const Lookup& a_lookup)
    {
        if (!a_lookup.ran) {
            return "index lookup not run";
        }
        if (!a_lookup.found) {
            return "index: no record";
        }
        return std::format("index: {} record (+0x0C = 0x{:08X})", a_lookup.archive ? "archive" : "loose",
            static_cast<std::uint32_t>(a_lookup.sizeFlags));
    }

    RE::BSResource::ErrorCode OpenLoose(const std::string& a_dataPath, StreamSlot& a_out)
    {
        auto code = RE::BSResource::ErrorCode::kUnsupported;
        if (!g_state.globals || !g_countOffset.load()) {
            return code;
        }
        const std::string path = "data\\" + a_dataPath;
        (void)GuardedKeep("file probe loose stream", [&]() {
            RE::BSResource::Location* where = nullptr;
            code = g_state.globals->DoCreateStream(path.c_str(), a_out.Ptr(), where, false);
        });
        return code;
    }

    bool RegisterLoose(const std::string& a_dataPath, std::uint64_t a_expectedSize, std::string& a_note)
    {
        if (const auto why = IndexUnavailable(); !why.empty()) {
            a_note = "not registered: " + why;
            return false;
        }
        StreamSlot stream;
        const auto code = OpenLoose(a_dataPath, stream);
        if (code != RE::BSResource::ErrorCode::kNone || !stream.Get()) {
            a_note = std::format("not registered: GlobalLocations::DoCreateStream(\"data\\{}\", readOnly false) gave error {}{}",
                a_dataPath, static_cast<int>(code), stream.Get() ? "" : ", no stream");
            return false;
        }
        std::uint64_t streamSize = 0;
        const bool sizeRead = GuardedKeep("file probe loose stream size", [&]() { streamSize = stream.Get()->totalSize; });
        const auto sizeText = !sizeRead ? std::string("the stream's size could not be read (FAULT, logged)") :
                              (streamSize == a_expectedSize ?
                                  std::format("stream size {} = the bytes written", streamSize) :
                                  std::format("SIZE MISMATCH: the stream says {} bytes, {} were written", streamSize, a_expectedSize));
        if (!sizeRead || streamSize != a_expectedSize) {  // FQ3: not the file we wrote: never registered
            logger::warn("BlackFlame: file visibility probe: '{}': {}", a_dataPath, sizeText);
            a_note = "not registered: " + sizeText;
            return false;
        }
        const auto offset = g_countOffset.load();
        Lookup before;
        std::uint32_t countBefore = CountOf(stream.Get(), offset);
        std::uint32_t countAfter = 0;
        bool registered = false;
        const bool ok = GuardedKeep("file probe RegisterLoose", [&]() {
            ID id{};
            g_state.pathToId(&id, a_dataPath.c_str());
            IndexGuard guard;
            before = FindUnder(guard, id);
            if (before.found && before.archive) {
                return;  // an archive record of this path: never converted by the probe (nothing would restore it)
            }
            g_state.registerLoose(guard.Guard(), &id, &stream.Ptr());
            registered = true;
            countAfter = CountOf(stream.Get(), offset);
        });
        const auto after = Find(a_dataPath);
        a_note = std::format("{}; loose stream opened (references {}, {}); before: {}; RegisterLoose {}; references after {}; after: {}",
            DescribeIds(a_dataPath), countBefore, sizeText, Describe(before),
            !ok ? "FAULTED (logged)" : (registered ? "called" : "not called (an archive record)"), countAfter, Describe(after));
        return ok && registered;
    }

    std::vector<std::string> DescribeLocations()
    {
        std::vector<std::string> out;
        if (!g_state.globals) {
            out.push_back("GlobalLocations not known: " + g_state.globalsWhy);
            return out;
        }
        std::vector<std::pair<RE::BSResource::Location*, std::uint32_t>> entries;
        std::size_t pending = 0;
        {
            RE::BSSpinLockGuard lock(g_state.globals->lock);
            for (auto* entry = g_state.globals->head; entry && entries.size() < kMaxLocations; entry = entry->next) {
                entries.emplace_back(entry->location, entry->priority);
            }
            for (auto* entry = g_state.globals->pendingMount; entry && pending < kMaxLocations; entry = entry->next) {
                ++pending;
            }
        }
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto* location = entries[i].first;
            const char* name = nullptr;
            std::uintptr_t vtable = 0;
            (void)GuardedKeep("file probe location name", [&]() {
                vtable = *reinterpret_cast<const std::uintptr_t*>(location);
                name = location->DoGetName();
            });
            out.push_back(std::format("location {}: vtable SkyrimSE+0x{:X}, name '{}', priority {}", i, vtable - Base(),
                name ? name : "(none)", entries[i].second));
        }
        out.push_back(std::format("{} location(s), {} waiting to mount", entries.size(), pending));
        return out;
    }

    std::optional<std::uint32_t> LooseFilePriority()
    {
        if (!g_state.globals) {
            return std::nullopt;
        }
        const auto loose = RE::VTABLE_BSResource__LooseFileLocation[0].address();
        RE::BSSpinLockGuard lock(g_state.globals->lock);
        for (auto* entry = g_state.globals->head; entry; entry = entry->next) {
            if (entry->location && *reinterpret_cast<const std::uintptr_t*>(entry->location) == loose) {
                return entry->priority;
            }
        }
        return std::nullopt;
    }
}
