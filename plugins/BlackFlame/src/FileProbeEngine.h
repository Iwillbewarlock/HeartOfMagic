#pragma once

#include "Common.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// =============================================================================
// FILE PROBE ENGINE - the engine's resource index and stream counts, for the dev file visibility probe only
// =============================================================================
// The pieces the probe's tests B and C need beyond CommonLib's own wrappers. Facts taken from Temp File Framework
// (GPL-3.0, HDharder, src/ArchiveBypass.cpp, tested there on 1.5.97 / 1.6.640 / 1.6.1170 / 1.7.99 / 1.7.104; only its
// ids, signatures and layouts are used, none of its code):
//   PathToID          68631 / 69971  ID* (ID* out, const char* dataRelativePath)
//   IndexGuard ctor   68321 / 69681  takes the archive index lock and starts a search; dtor 68322 / 69682.
//                                    Engine-sized buffer: 0x200 bytes, 16-aligned; the search state at +0x08
//   FindRecord        68329 / 69689  bool (search, const ID*, Record** out); reads the ArchiveManager static
//                                    523840 / 410404 (a `mov rcx, [rip+disp32]` near its start - checked here by scan)
//   RegisterLoose     68327 / 69687  void (guard, const ID*, BSTSmartPointer<Stream>*): a new loose record, or a loose
//                                    record's stream replaced, or an archive record made loose
//   Record            +0x0C int32: negative = an archive record, else a loose one
//   Stream count      the engine's own inlined Stream::IncRef in 68483 / 69839 (`lock cmpxchg [rbx+disp8], ecx`):
//                     disp8 0x0C on SE and AE up to at least 1.6.640, 0x10 on 1.6.1170 and later. Only the value expected
//                     for the running build is accepted (a scan that finds another is logged as a conflict). CommonLib's
//                     StreamBase::GetFlags() uses 0x10 on every AE build, so engine streams and our own streams are
//                     counted here at the offset the running engine uses, never through CommonLib's BSTSmartPointer.
//   GlobalLocations   no singleton id in CommonLib (RE/G/GlobalLocations.h): found by its vtable in the game's .data
//                     (a static buffer), checked by walking its entry list. DoCreateStream("data\\" + path, out, where,
//                     false) is the call the engine makes for a loose file (paths there are relative to the game folder).
// IndexGuard's ctor and dtor are called only when each touches the ArchiveManager (its static, or the guard's +0xB8) in its
// first bytes (a structural check; a wrong layout that passes it can still hang the game once the lock is taken).
// The in-function offsets are not trusted as fixed numbers on any build (Patch::IsCodeReadBuild): each is found by a scan
// of a bounded window, and a scan that finds nothing usable turns the test off with the reason.
// CommonLib's own ID::GenerateFromPath (68635 / 69979) is another function: the probe logs both ids for the same path.

namespace BlackFlame::FileProbe::Engine
{
    using Stream = RE::BSResource::Stream;
    using StreamPtr = RE::BSTSmartPointer<Stream>;

    /// Main thread, once before the tests: the ids, the scans, GlobalLocations. Every finding is appended to a_lines
    void Init(std::vector<std::string>& a_lines);

    /// The engine's Stream count offset (0x0C or 0x10), or nullopt when the scan did not find it
    [[nodiscard]] std::optional<std::uint32_t> StreamCountOffset() noexcept;

    /// Why the index tests (C) cannot run, or empty when they can (ids present, scans matched, the index exists)
    [[nodiscard]] std::string IndexUnavailable();

    /// Why GlobalLocations is not known, or empty
    [[nodiscard]] std::string GlobalsUnavailable();

    /// a_stream's count set to one reference at the engine's offset (a stream of ours, just made); false when unknown
    bool SetFirstReference(Stream* a_stream) noexcept;

    /// One reference given back at the engine's offset; the stream deleted through its virtual destructor at zero, as
    /// the engine's own release does (CommonLib's BSTSmartPointerIntrusiveRefCount::Release does `delete` the same way)
    void Release(Stream* a_stream) noexcept;

    /// An engine smart pointer slot the engine fills (DoCreateStream) and copies (RegisterLoose); released at the
    /// engine's offset. CommonLib's BSTSmartPointer destructor never runs on it
    class StreamSlot
    {
    public:
        StreamSlot() = default;
        ~StreamSlot() { Release(Get()); }
        StreamSlot(const StreamSlot&) = delete;
        StreamSlot& operator=(const StreamSlot&) = delete;

        [[nodiscard]] StreamPtr& Ptr() noexcept { return *reinterpret_cast<StreamPtr*>(&m_raw); }
        [[nodiscard]] Stream* Get() const noexcept { return m_raw; }

    private:
        Stream* m_raw = nullptr;  // the only member of a BSTSmartPointer (RE/B/BSTSmartPointer.h, _ptr at 0)
    };
    static_assert(sizeof(StreamPtr) == sizeof(Stream*));

    /// "file=0x... ext='nif' dir=0x..." from PathToID, and the same from CommonLib's GenerateFromPath with whether they agree
    [[nodiscard]] std::string DescribeIds(const std::string& a_dataPath);

    struct Lookup
    {
        bool ran = false;      // the guard and the lookup ran without a fault
        bool found = false;
        bool archive = false;  // the record's +0x0C was negative
        std::int32_t sizeFlags = 0;
    };

    /// FindRecord for a_dataPath ("meshes\\..." / "textures\\...") under its own IndexGuard
    [[nodiscard]] Lookup Find(const std::string& a_dataPath);

    [[nodiscard]] std::string Describe(const Lookup& a_lookup);

    /// GlobalLocations::DoCreateStream("data\\" + a_dataPath, ..., false): the loose stream the engine would make
    [[nodiscard]] RE::BSResource::ErrorCode OpenLoose(const std::string& a_dataPath, StreamSlot& a_out);

    /// Test C: the loose stream (OpenLoose) registered in the index under PathToID(a_dataPath); every step logged into a_note,
    /// the stream's size against a_expectedSize (the bytes written) included: a mismatch or an unreadable size registers nothing
    [[nodiscard]] bool RegisterLoose(const std::string& a_dataPath, std::uint64_t a_expectedSize, std::string& a_note);

    /// Prepare, after Init: the index lookup of one of a_shippedDataPaths (tried in order) must find it (FindRecord ran and
    /// found a record), or the index tests are turned off (IndexUnavailable says why): RegisterLoose is never called on a
    /// layout no lookup confirmed. Logs both ids of each path tried (PathToID and GenerateFromPath)
    void ConfirmIndex(const std::vector<std::string>& a_shippedDataPaths, std::vector<std::string>& a_lines);

    /// GlobalLocations' entry list (location vtable as module offset, its DoGetName, priority), read under its lock
    [[nodiscard]] std::vector<std::string> DescribeLocations();

    /// The priority of the first LooseFileLocation in GlobalLocations' list, if there is one
    [[nodiscard]] std::optional<std::uint32_t> LooseFilePriority();
}
