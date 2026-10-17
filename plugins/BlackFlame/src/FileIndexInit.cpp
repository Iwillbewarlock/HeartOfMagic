// =============================================================================
// FileIndexInit - engine file index set-up: ids, scans, GlobalLocations (FileIndex.h)
// =============================================================================
// Lifted from the spike's FileProbeEngine.cpp (Init, InitIndex, InitGlobals, ConfirmIndex). Every finding is one log line;
// the first failing check leaves State::why set and the index unused for the session.

#include "FileIndex.h"
#include "FileIndexEngine.h"
#include "FileIndexGuardCheck.h"

#include "DataPathRule.h"
#include "Patch.h"
#include "SehGuard.h"

#include <array>
#include <cstring>
#include <format>
#include <set>

#include "FileIndex.h"

namespace BlackFlame::FileIndex
{
    namespace
    {
        using namespace Engine;
        using namespace FileIndexGuardCheck;

        constexpr std::size_t kIncRefWindow = 0x200;   // bytes of 68483 / 69839 scanned for the inlined IncRef
        constexpr std::size_t kFindWindow = 0x40;      // bytes of FindRecord scanned for the ArchiveManager load
        constexpr std::uint32_t kSeCountOffset = 0x0C;
        constexpr std::uint32_t kAeLateCountOffset = 0x10;
        constexpr std::size_t kMaxLocations = 64;      // a longer entry list is not GlobalLocations
        constexpr std::size_t kExePathChars = 32768;   // the longest Windows path
        constexpr std::array<std::uint8_t, 4> kLockCmpxchgRbx{ 0xF0, 0x0F, 0xB1, 0x4B };  // lock cmpxchg [rbx+disp8], ecx
        constexpr std::array<std::uint8_t, 3> kMovRcxRip{ 0x48, 0x8B, 0x0D };            // mov rcx, [rip+disp32]
        constexpr std::size_t kMovRcxRipSize = 7;
        // A FindRecord of one of these must find its record before RegisterLoose is ever called: our two shipped palettes
        // (loose files of the mod) and a vanilla mesh in Skyrim - Meshes0.bsa (an archive record)
        constexpr std::array<std::string_view, 3> kShippedPaths{ DataPathRule::kOurPalette, DataPathRule::kOurPaletteAdd,
            "meshes\\actors\\character\\character assets\\skeleton.nif" };

        void Log(const std::string& a_line) { logger::info("BlackFlame: file index: {}", a_line); }

        std::uintptr_t Base() { return REL::Module::get().base(); }

        // 0x0C on SE and on AE before 1.6.1170, 0x10 from 1.6.1170 on (Temp File Framework's reading of the engines)
        std::uint32_t ExpectedCountOffset()
        {
            return REL::Module::IsAE() && REL::Module::get().version() >= REL::Version(1, 6, 1170, 0) ? kAeLateCountOffset : kSeCountOffset;
        }

        // The inlined Stream::IncRef in the engine's CreateStream: every `lock cmpxchg [rbx+disp8], ecx` in the window
        std::optional<std::uint32_t> ScanCountOffset(std::string& a_why)
        {
            const auto function = Patch::AddressOf(Patch::IdFor(68483, 69839));
            if (!Patch::IsExecutable(function, kIncRefWindow)) {
                a_why = std::format("CreateStream at +0x{:X} is not readable code", function - Base());
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
            Log(std::format("stream count: CreateStream at SkyrimSE+0x{:X}, lock cmpxchg [rbx+disp8] in the first 0x{:X} bytes: {}",
                function - Base(), kIncRefWindow, where.empty() ? "none" : where));
            if (found.size() != 1) {
                a_why = "the stream count offset scan did not find exactly one offset";
                return std::nullopt;
            }
            const auto offset = *found.begin();
            if (offset != ExpectedCountOffset()) {
                a_why = std::format("CONFLICT: the stream count scan found 0x{:X}, this build ({}) is expected to use 0x{:X}", offset,
                    REL::Module::get().version().string(), ExpectedCountOffset());
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

        std::string InitIndex()
        {
            auto& state = TheState();
            if (!Patch::HasIds({ Patch::IdFor(68631, 69971), Patch::IdFor(68321, 69681), Patch::IdFor(68322, 69682),
                                   Patch::IdFor(68329, 69689), Patch::IdFor(68327, 69687), Patch::IdFor(523840, 410404),
                                   Patch::IdFor(68635, 69979), Patch::IdFor(68483, 69839) },
                    "file index (PathToID, IndexGuard, FindRecord, RegisterLoose, ArchiveManager, GenerateFromPath, CreateStream)")) {
                return "an Address Library id is missing (logged above)";
            }
            const auto find = Patch::AddressOf(Patch::IdFor(68329, 69689));
            const auto manager = Patch::AddressOf(Patch::IdFor(523840, 410404));
            if (!FindLoadsManager(find, manager)) {
                return std::format("FindRecord (SkyrimSE+0x{:X}) does not load the ArchiveManager static (SkyrimSE+0x{:X}) in its "
                                   "first 0x{:X} bytes", find - Base(), manager - Base(), kFindWindow);
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
            Log(std::format("IndexGuard ctor {} touches the manager: {}; dtor {}: {} -> {}", BodyText(ctor, ctorBody),
                ctorOk ? "yes" : "NO", BodyText(dtor, dtorBody), dtorRule, dtorOk ? "accepted" : "NO"));
            if (!ctorOk || !dtorOk) {
                return std::format("IndexGuard's layout is not confirmed (ctor touches the manager: {}; dtor: {})",
                    ctorOk ? "yes" : "no", dtorRule);
            }
            state.pathToId = reinterpret_cast<PathToIdFn>(Patch::AddressOf(Patch::IdFor(68631, 69971)));
            state.guardCtor = reinterpret_cast<GuardCtorFn>(ctor);
            state.guardDtor = reinterpret_cast<GuardDtorFn>(dtor);
            state.find = reinterpret_cast<FindRecordFn>(find);
            state.registerLoose = reinterpret_cast<RegisterLooseFn>(Patch::AddressOf(Patch::IdFor(68327, 69687)));
            state.manager = reinterpret_cast<void**>(manager);
            Log(std::format("PathToID +0x{:X}, IndexGuard +0x{:X}/+0x{:X} (buffer 0x{:X}, search at +0x{:X}), FindRecord +0x{:X} "
                            "(loads the manager: yes), RegisterLoose +0x{:X}, manager +0x{:X} = {}",
                reinterpret_cast<std::uintptr_t>(state.pathToId) - Base(), ctor - Base(), dtor - Base(), kGuardSize, kSearchOffset,
                find - Base(), reinterpret_cast<std::uintptr_t>(state.registerLoose) - Base(), manager - Base(),
                *state.manager ? "set" : "NULL"));
            return *state.manager ? "" : "the ArchiveManager is null (the archives have not loaded)";
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

        std::string InitGlobals()
        {
            if (!Patch::HasIds({ Patch::IdFor(285855, 237007) }, "file index GlobalLocations vtable")) {
                return "the GlobalLocations vtable id is missing (logged above)";
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
                const bool ok = GuardedKeep("file index GlobalLocations check", [&]() { entries = CountEntries(candidate); });
                Log(std::format("GlobalLocations candidate at SkyrimSE+0x{:X}: {} entries{}",
                    reinterpret_cast<std::uintptr_t>(candidate) - Base(), entries, ok ? "" : " (the walk faulted)"));
                if (ok && entries > 0) {
                    TheState().globals = candidate;
                    return "";
                }
            }
            return std::format("no object with GlobalLocations' vtable and a sane entry list in .data ({} vtable hits)", hits);
        }

        // FQ5: the first shipped path the index knows confirms the guard / FindRecord layout
        std::string ConfirmIndex()
        {
            for (const auto path : kShippedPaths) {
                bool found = false;
                bool archive = false;
                const bool ran = RawFind(std::string(path), found, archive);
                Log(std::format("check on the shipped '{}': {}; {}", path,
                    !ran ? "lookup FAULTED" : (found ? (archive ? "archive record" : "loose record") : "no record"),
                    DescribeIds(std::string(path))));
                if (ran && found) {
                    return "";
                }
            }
            return std::format("no lookup of {} shipped file(s) found its record: the guard / FindRecord layout is not confirmed",
                kShippedPaths.size());
        }
    }

    std::optional<std::filesystem::path> DataFolder()
    {
        std::wstring buffer(kExePathChars, L'\0');
        const auto length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) {
            return std::nullopt;
        }
        buffer.resize(length);
        return std::filesystem::path(buffer).parent_path() / L"Data";
    }

    void Init()
    {
        auto& state = TheState();
        if (state.initialised) {
            return;
        }
        state.initialised = true;
        Log(std::format("usvfs_x64.dll {} (MO2's VFS; Data writes then land in Overwrite)",
            ::GetModuleHandleW(L"usvfs_x64.dll") ? "is loaded" : "is NOT loaded"));
        if (const auto data = DataFolder()) {
            const auto text = data->u8string();
            Log(std::format("Data folder (from the game's exe): '{}'", std::string(text.begin(), text.end())));
        } else {
            Log("the game's exe path could not be read whole: no Data folder");
        }
        std::string why;
        if (!REL::Module::IsAE()) {  // design v3 #6: SE stays on the next-launch fallback until an SE probe run passes
            state.why = "SE: the file probe has not run on SE (the next-launch fallback)";
            logger::warn("BlackFlame: file index: not used this session ({})", state.why);
            return;
        }
        const auto offset = ScanCountOffset(why);
        if (offset) {
            CountOffset().store(*offset);
        }
        if (why.empty()) {
            why = InitIndex();
        }
        if (why.empty()) {
            why = InitGlobals();
        }
        if (why.empty()) {
            why = ConfirmIndex();
        }
        state.why = why;
        if (why.empty()) {
            Log("every structural check passed: RegisterLoose is used for the copies this session");
        } else {
            logger::warn("BlackFlame: file index: not used this session ({}): copies written into Data are seen only by the "
                         "next start's index", why);
        }
    }

    bool Available() noexcept
    {
        const auto& state = TheState();
        return state.initialised && state.why.empty();
    }

    std::string Unavailable()
    {
        return TheState().why;
    }
}
