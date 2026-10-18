#pragma once

// =============================================================================
// GreyBudget - memory admission of the key mode's texture work, and the builder process's own limits
// =============================================================================
// The DLL runs the builder in a job object whose JOB_OBJECT_LIMIT_JOB_MEMORY is the cap (--job-memory, default 1.5 GiB;
// the same value both sides). Before a texture is recoloured its peak is estimated from the DDS header: the decoded
// RGBA chain x kEncoderImages. RecolourDds (GreyTexture.cpp:227-360) holds at once the source (BCn: a quarter to an
// eighth of that), the decoded chain, the risk check's RGBA copy of mip 0, the grey chain and the encoded chain; a BC7
// target encoded on the GPU adds, one subresource at a time, the output check's decode (at most mip 0's RGBA), and the
// GPU device's own commit (GreyGpuEncoder.h kGpuDeviceShareBytes) is in PrivateUsage once it is made. Then (Admit):
//   - the estimate alone over the cap: too_large, terminal for that texture's key while the cap stays (never because of
//     the GPU device: it is not in the estimate);
//   - the system's available commit (GetPerformanceInfo: commit limit - commit total, not the job's own remainder) under
//     the reserve (--commit-reserve, default 4 GiB) plus the estimate: the texture cache is cleared once, then a GPU
//     device the run holds is released, each followed by a new measure; still short: the builder waits (kPauseStepMs
//     steps) - the game keeps its commit;
//   - the process's committed bytes (PrivateUsage, what the job limit counts) plus the estimate over the cap: a GPU
//     device the run holds is released with the texture cache (SetDeviceRelease), else the texture cache (TextureSource,
//     decoded mip 0 images the glow rules keep) is cleared once, and it is measured again; still over: that object fails
//     (retryable).
// The cache is also cleared between objects once it holds over kTextureCacheKeepBytes. Clearing happens only where no
// TextureLook is alive: between objects, and inside a texture slot's recolour (the glow rules are done by then).
// One worker: objects are built one at a time (TextureJob, PaletteJob, TextureSource and the progress log are not
// thread-safe). Inside one object OpenMP runs: the builder links OpenMP::OpenMP_CXX itself (tools/CMakeLists.txt; vcpkg
// "directxtex" links it too), so it is compiled with /openmp and the exe imports VCOMP140.DLL, and TEX_COMPRESS_PARALLEL
// in RecolourDds's CPU encode and ComputeStats's row loops (GreyTexStats.cpp) use every logical CPU. After the throttle
// event (ThrottleSignal) OneOpenMpThread caps them at one thread. Upgrade path for more workers: a lock round those four,
// one GPU submitter, then cap the workers at min(cores / 2 - 1, cap / the largest estimate).

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <thread>

namespace GreyBuild
{
    class TextureSource;

    inline constexpr std::uint64_t kBytesPerMiB = 1024ull * 1024ull;
    inline constexpr std::uint64_t kDefaultJobMemoryMiB = 1536;    // = the DLL's job object memory cap
    inline constexpr std::uint64_t kDefaultCommitReserveMiB = 4096;
    inline constexpr std::uint64_t kEncoderImages = 5;
    inline constexpr std::uint64_t kDecodedBytesPerTexel = 4;      // RGBA8, as RecolourDds decodes
    inline constexpr std::uint64_t kTextureCacheKeepBytes = 256 * kBytesPerMiB;
    inline constexpr unsigned kPauseStepMs = 1000;
    inline constexpr unsigned kPauseNoteEvery = 60;                 // one "still waiting" line a minute
    inline constexpr unsigned kMaxPauseSteps = 1800;                // 30 minutes: then the texture fails (a run by hand never hangs)
    inline constexpr unsigned kMaxRunPauseSteps = 3600;             // 60 minutes of waits in a run: then every later wait fails at once
    inline constexpr std::size_t kMaxMips = 32;                     // a header's mip count is cut here (no level past it holds a texel)
    inline constexpr std::uint64_t kReadImages = 2;                 // ReadRgba: mip 0 decoded, then copied out (plus the file)
    inline constexpr unsigned kParentGraceMs = 30000;               // after the parent died: the object in hand, then out
    inline constexpr int kExitParentGone = 3;

    /// The decoded chain's bytes (every mip and array item, RGBA8) x kEncoderImages from a_dds's header; nullopt with a_why
    /// when the header does not read (a_transient, when given: because of the machine, IsTransientResult, not the file)
    [[nodiscard]] std::optional<std::uint64_t> EstimateTextureBytes(const std::filesystem::path& a_dds, std::string& a_why,
        bool* a_transient = nullptr);

    /// What a TextureSource read (ReadRgba) holds at once: the file's bytes as loaded plus mip 0 decoded x kReadImages;
    /// nullopt with a_why when the header does not read
    [[nodiscard]] std::optional<std::uint64_t> EstimateReadBytes(const std::filesystem::path& a_dds, std::string& a_why,
        bool* a_transient = nullptr);

    enum class Admission
    {
        kAdmit,
        kPause,     // the system's available commit is short: wait
        kOverCap,   // the process plus the estimate is over the cap: clear caches, else fail the object
        kTooLarge   // the estimate alone is over the cap: terminal
    };

    /// The admission rule alone (header comment), in that order: too large, pause, over the cap, admit
    [[nodiscard]] Admission Admit(std::uint64_t a_estimate, std::uint64_t a_inUse, std::uint64_t a_cap, std::uint64_t a_availableCommit,
        std::uint64_t a_reserve) noexcept;

    struct MemoryState
    {
        std::uint64_t inUse = 0;            // this process's committed bytes (PrivateUsage)
        std::uint64_t availableCommit = 0;  // the system's (commit limit - commit total)
    };
    using MemoryProbe = std::function<std::optional<MemoryState>()>;
    using Sleeper = std::function<void(unsigned)>;

    /// The process's and the system's memory now; nullopt when Windows does not say
    [[nodiscard]] std::optional<MemoryState> MeasureMemory();

    class Budget
    {
    public:
        /// a_cache may be null; a_stop (may be null) ends a pause; a_probe and a_sleep are the self-test's seams
        Budget(std::uint64_t a_cap, std::uint64_t a_reserve, TextureSource* a_cache, const std::atomic<bool>* a_stop,
            MemoryProbe a_probe = MeasureMemory, Sleeper a_sleep = {});

        /// The rule for one texture of a_estimate bytes, waiting while it says pause; a_why for too large and over the cap
        /// (the too_large text ends in "cap=<bytes>", GreyProgress.h)
        /// a_mayClear false: the texture cache is never cleared here (a TextureLook may be alive: the glow rules' reads)
        [[nodiscard]] Admission Acquire(std::uint64_t a_estimate, std::string& a_why, bool a_mayClear = true);

        /// Between objects: the texture cache cleared once it holds over kTextureCacheKeepBytes, or when it and the next
        /// object's texture estimate a_nextCost would not fit the cap together (OCR review 6)
        void BetweenObjects(std::uint64_t a_nextCost = 0);

        /// Over the cap, before an object fails: a_release frees a GPU device the run holds (true when one was held and
        /// is now released; GreyGpuEncoder.h) and the texture cache is cleared with it, then it is measured again
        void SetDeviceRelease(std::function<bool()> a_release) { m_releaseDevice = std::move(a_release); }

        [[nodiscard]] std::uint64_t Cap() const noexcept { return m_cap; }
        [[nodiscard]] int Pauses() const noexcept { return m_pauses; }
        [[nodiscard]] int Evictions() const noexcept { return m_evictions; }
        [[nodiscard]] int DeviceReleases() const noexcept { return m_deviceReleases; }

    private:
        std::uint64_t m_cap;
        std::uint64_t m_reserve;
        TextureSource* m_cache;
        const std::atomic<bool>* m_stop;
        MemoryProbe m_probe;
        Sleeper m_sleep;
        std::function<bool()> m_releaseDevice;
        int m_pauses = 0;
        int m_deviceReleases = 0;
        int m_evictions = 0;
        unsigned m_pausedSteps = 0;  // every wait step of the run (kMaxRunPauseSteps)
    };

    /// BELOW_NORMAL_PRIORITY_CLASS (no background mode: it would also lower I/O and memory priority)
    void LowerOwnPriority();

    /// Every later OpenMP parallel region of this process on one thread (omp_set_num_threads(1) on the thread that runs
    /// them: the object loop's; vcomp140 is process-wide, so DirectXTex's PARALLEL encoder and ComputeStats both follow)
    void OneOpenMpThread();

    /// The DLL's throttle event (plugins/BlackFlame/src/BuilderSignal.h), opened with SYNCHRONIZE and read between objects and
    /// at each GPU encode (GpuEncoder::SetThrottleCheck)
    class ThrottleSignal
    {
    public:
        ThrottleSignal() = default;
        ~ThrottleSignal();
        ThrottleSignal(const ThrottleSignal&) = delete;
        ThrottleSignal& operator=(const ThrottleSignal&) = delete;

        /// Local\HoMBlackFlame_Throttle_<a_parentPid>; false when there is none (the run is never throttled: a run by hand,
        /// or a DLL without the event)
        [[nodiscard]] bool Open(std::uint32_t a_parentPid);
        [[nodiscard]] bool Opened() const noexcept { return m_event != nullptr; }
        /// true while the event is set (manual reset: once the DLL sets it, it stays set)
        [[nodiscard]] bool IsSet() const noexcept;

    private:
        void* m_event = nullptr;  // HANDLE
    };

    /// --parent-pid: a thread waits on the parent; when it ends, Gone() turns true (the run stops after the object in
    /// hand) and, should the run still be going kParentGraceMs later, the process exits with kExitParentGone. Known limit:
    /// a pid that Windows gave to another process before the builder opened it is watched instead (the job object's
    /// kill-on-close is the first guard).
    class ParentWatch
    {
    public:
        ParentWatch() = default;
        ~ParentWatch();
        ParentWatch(const ParentWatch&) = delete;
        ParentWatch& operator=(const ParentWatch&) = delete;

        /// false with a_why when the process does not open (already gone, or no access): the caller stops at once
        [[nodiscard]] bool Start(std::uint32_t a_pid, unsigned a_graceMs, std::string& a_why);

        [[nodiscard]] bool Gone() const noexcept { return m_gone.load(); }
        /// After a Start that failed: true when the pid names no process (the parent is gone), false for another failure
        [[nodiscard]] bool NoProcess() const noexcept { return m_noProcess; }
        [[nodiscard]] const std::atomic<bool>* Flag() const noexcept { return &m_gone; }

    private:
        void* m_process = nullptr;  // HANDLE (SYNCHRONIZE)
        void* m_quit = nullptr;     // HANDLE, an event the destructor sets
        std::thread m_thread;
        std::atomic<bool> m_gone{ false };
        bool m_noProcess = false;
    };
}
