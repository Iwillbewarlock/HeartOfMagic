// =============================================================================
// GreyBudget - memory admission per texture, the retained texture cache, priority and the parent watch (GreyBudget.h)
// =============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include "GreyBudget.h"

#include "GreyBuild.h"
#include "GreyProgress.h"
#include "GreyTexStats.h"
#include "GreyTexture.h"

#include "../../plugins/BlackFlame/src/BuilderSignal.h"

#include <windows.h>

#include <omp.h>

#include <DirectXTex.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <fmt/format.h>
#include <iostream>

namespace GreyBuild
{
    namespace
    {
        constexpr auto kHeaderFlags = static_cast<DirectX::DDS_FLAGS>(DirectX::DDS_FLAGS_PERMISSIVE | DirectX::DDS_FLAGS_EXPAND_LUMINANCE);

        constexpr unsigned kMsPerSecond = 1000;

        // a_a x a_b, UINT64_MAX when it overflows (a header with absurd sizes estimates as too large, never as small)
        [[nodiscard]] std::uint64_t SatMul(std::uint64_t a_a, std::uint64_t a_b) noexcept
        {
            return a_a != 0 && a_b > UINT64_MAX / a_a ? UINT64_MAX : a_a * a_b;
        }

        [[nodiscard]] std::uint64_t SatAdd(std::uint64_t a_a, std::uint64_t a_b) noexcept
        {
            return a_b > UINT64_MAX - a_a ? UINT64_MAX : a_a + a_b;
        }

        [[nodiscard]] double MiB(std::uint64_t a_bytes) noexcept
        {
            return static_cast<double>(a_bytes) / static_cast<double>(kBytesPerMiB);
        }
    }

    std::optional<std::uint64_t> EstimateTextureBytes(const std::filesystem::path& a_dds, std::string& a_why, bool* a_transient)
    {
        DirectX::TexMetadata meta{};
        if (const HRESULT hr = DirectX::GetMetadataFromDDSFile(a_dds.c_str(), kHeaderFlags, meta); FAILED(hr)) {
            if (a_transient) {
                *a_transient = IsTransientResult(hr);
            }
            a_why = fmt::format("the DDS header of {} does not read (HRESULT 0x{:08X})", PathText(a_dds), static_cast<std::uint32_t>(hr));
            return std::nullopt;
        }
        std::uint64_t chain = 0;
        std::uint64_t width = meta.width;
        std::uint64_t height = meta.height;
        std::uint64_t depth = std::max<std::uint64_t>(meta.depth, 1);
        // a header's mip count is not trusted: past kMaxMips every level is one texel (a broken header never loops long)
        const std::size_t mips = std::min<std::size_t>(std::max<std::size_t>(meta.mipLevels, 1), kMaxMips);
        for (std::size_t mip = 0; mip < mips; ++mip) {
            chain = SatAdd(chain, SatMul(SatMul(SatMul(width, height), depth), kDecodedBytesPerTexel));
            width = std::max<std::uint64_t>(width / 2, 1);
            height = std::max<std::uint64_t>(height / 2, 1);
            depth = std::max<std::uint64_t>(depth / 2, 1);
        }
        return SatMul(SatMul(chain, std::max<std::uint64_t>(meta.arraySize, 1)), kEncoderImages);
    }

    std::optional<std::uint64_t> EstimateReadBytes(const std::filesystem::path& a_dds, std::string& a_why, bool* a_transient)
    {
        DirectX::TexMetadata meta{};
        if (const HRESULT hr = DirectX::GetMetadataFromDDSFile(a_dds.c_str(), kHeaderFlags, meta); FAILED(hr)) {
            if (a_transient) {
                *a_transient = IsTransientResult(hr);
            }
            a_why = fmt::format("the DDS header of {} does not read (HRESULT 0x{:08X})", PathText(a_dds), static_cast<std::uint32_t>(hr));
            return std::nullopt;
        }
        std::error_code ec;
        const std::uint64_t file = std::filesystem::file_size(a_dds, ec);
        if (ec) {  // the header read and the size does not: the machine, not the file
            if (a_transient) {
                *a_transient = true;
            }
            a_why = fmt::format("the size of {} does not read: {}", PathText(a_dds), ec.message());
            return std::nullopt;
        }
        return SatAdd(file, SatMul(SatMul(SatMul(meta.width, meta.height), kDecodedBytesPerTexel), kReadImages));
    }

    Admission Admit(std::uint64_t a_estimate, std::uint64_t a_inUse, std::uint64_t a_cap, std::uint64_t a_availableCommit,
        std::uint64_t a_reserve) noexcept
    {
        if (a_estimate > a_cap) {
            return Admission::kTooLarge;
        }
        if (a_availableCommit < a_reserve || a_availableCommit - a_reserve < a_estimate) {
            return Admission::kPause;
        }
        if (a_inUse > a_cap - a_estimate) {
            return Admission::kOverCap;
        }
        return Admission::kAdmit;
    }

    std::optional<MemoryState> MeasureMemory()
    {
        PROCESS_MEMORY_COUNTERS_EX process{};
        PERFORMANCE_INFORMATION system{};
        if (!::K32GetProcessMemoryInfo(::GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process), sizeof(process)) ||
            !::K32GetPerformanceInfo(&system, sizeof(system))) {
            return std::nullopt;
        }
        MemoryState state;
        state.inUse = process.PrivateUsage;
        const std::uint64_t pages = system.CommitLimit > system.CommitTotal ? system.CommitLimit - system.CommitTotal : 0;
        state.availableCommit = pages * system.PageSize;
        return state;
    }

    Budget::Budget(std::uint64_t a_cap, std::uint64_t a_reserve, TextureSource* a_cache, const std::atomic<bool>* a_stop,
        MemoryProbe a_probe, Sleeper a_sleep) :
        m_cap(a_cap), m_reserve(a_reserve), m_cache(a_cache), m_stop(a_stop), m_probe(std::move(a_probe)), m_sleep(std::move(a_sleep))
    {
        if (!m_sleep) {
            m_sleep = [](unsigned a_ms) { std::this_thread::sleep_for(std::chrono::milliseconds(a_ms)); };
        }
    }

    Admission Budget::Acquire(std::uint64_t a_estimate, std::string& a_why, bool a_mayClear)
    {
        bool evicted = false;
        bool released = false;
        bool paused = false;
        for (unsigned waited = 0;;) {
            // Not measurable: the cap alone decides, with the cache this process holds (the job object still caps it)
            const MemoryState unknown{ m_cache ? m_cache->CachedBytes() : 0, UINT64_MAX };
            const MemoryState state = m_probe ? m_probe().value_or(unknown) : unknown;
            const Admission admission = Admit(a_estimate, state.inUse, m_cap, state.availableCommit, m_reserve);
            // over the cap: a GPU device this process holds goes first, with the cache, then it is measured again (the
            // device is made again later when there is room; GreyGpuEncoder.h). Never a too_large: that is the estimate alone
            if (admission == Admission::kOverCap && !released && m_releaseDevice) {
                released = true;
                if (m_releaseDevice()) {
                    ++m_deviceReleases;
                    if (a_mayClear && !evicted && m_cache && m_cache->CachedBytes() > 0) {
                        m_cache->Clear();
                        ++m_evictions;
                        evicted = true;
                    }
                    continue;
                }
            }
            // over the cap, or short of commit: the cache this process holds goes first (it is commit too)
            if ((admission == Admission::kOverCap || admission == Admission::kPause) && a_mayClear && !evicted && m_cache &&
                m_cache->CachedBytes() > 0) {
                m_cache->Clear();
                ++m_evictions;
                evicted = true;
                continue;
            }
            if (admission == Admission::kPause) {
                // a stop (the parent gone) ends a wait, never an admission that needs none (Gemini review)
                if (m_stop && m_stop->load()) {
                    a_why = "the run is stopping (the parent process is gone)";
                    return Admission::kOverCap;
                }
                if (waited >= kMaxPauseSteps || m_pausedSteps >= kMaxRunPauseSteps) {
                    a_why = fmt::format("waited {} s for the system's commit to come back above the reserve ({} s this run)",
                        waited * kPauseStepMs / kMsPerSecond, m_pausedSteps * kPauseStepMs / kMsPerSecond);
                    return Admission::kOverCap;
                }
                if (!paused) {
                    ++m_pauses;
                    paused = true;
                }
                if (waited % kPauseNoteEvery == 0) {
                    std::cout << fmt::format("WAIT  the system has {:.0f} MiB of commit left (reserve {:.0f} MiB, this texture needs "
                                             "about {:.0f} MiB): waiting\n", MiB(state.availableCommit), MiB(m_reserve), MiB(a_estimate));
                }
                m_sleep(kPauseStepMs);
                ++waited;
                ++m_pausedSteps;
                continue;
            }
            if (paused) {
                std::cout << "WAIT  done: commit is back above the reserve\n";
            }
            if (admission == Admission::kTooLarge) {
                a_why = fmt::format("the texture needs about {} bytes, over the job memory cap ({} bytes)", a_estimate, m_cap);
            } else if (admission == Admission::kOverCap) {
                a_why = fmt::format("{} bytes in use + about {} for this texture are over the job memory cap ({} bytes)", state.inUse,
                    a_estimate, m_cap);
            }
            return admission;
        }
    }

    void Budget::BetweenObjects(std::uint64_t a_nextCost)
    {
        if (m_cache && (m_cache->CachedBytes() > kTextureCacheKeepBytes || SatAdd(m_cache->CachedBytes(), a_nextCost) > m_cap)) {
            m_cache->Clear();
            ++m_evictions;
        }
    }

    void LowerOwnPriority()
    {
        if (!::SetPriorityClass(::GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS)) {
            std::cout << fmt::format("NOTE  the builder's priority could not be lowered (Windows error {})\n", ::GetLastError());
        }
    }

    void OneOpenMpThread()
    {
        ::omp_set_num_threads(1);
    }

    ThrottleSignal::~ThrottleSignal()
    {
        if (m_event) {
            ::CloseHandle(m_event);
        }
    }

    bool ThrottleSignal::Open(std::uint32_t a_parentPid)
    {
        if (m_event) {
            ::CloseHandle(m_event);
        }
        m_event = ::OpenEventW(SYNCHRONIZE, FALSE, BlackFlame::BuilderSignal::ThrottleEventName(a_parentPid).c_str());
        return m_event != nullptr;
    }

    bool ThrottleSignal::IsSet() const noexcept
    {
        return m_event && ::WaitForSingleObject(m_event, 0) == WAIT_OBJECT_0;
    }

    ParentWatch::~ParentWatch()
    {
        if (m_quit) {
            ::SetEvent(m_quit);
        }
        if (m_thread.joinable()) {
            m_thread.join();
        }
        for (void* handle : { m_process, m_quit }) {
            if (handle) {
                ::CloseHandle(handle);
            }
        }
    }

    bool ParentWatch::Start(std::uint32_t a_pid, unsigned a_graceMs, std::string& a_why)
    {
        if (m_process || m_thread.joinable()) {
            a_why = "the parent watch is started already";
            return false;
        }
        m_process = ::OpenProcess(SYNCHRONIZE, FALSE, a_pid);
        if (!m_process) {
            const DWORD error = ::GetLastError();
            m_noProcess = error == ERROR_INVALID_PARAMETER;  // what OpenProcess says for a pid that names no process
            a_why = m_noProcess ? fmt::format("the parent process {} is gone", a_pid) :
                                  fmt::format("the parent process {} does not open (Windows error {})", a_pid, error);
            return false;
        }
        if (::WaitForSingleObject(m_process, 0) == WAIT_OBJECT_0) {
            // ended already (another handle keeps its object): gone before the first object starts (Gemini review)
            ::CloseHandle(m_process);
            m_process = nullptr;
            m_noProcess = true;
            a_why = fmt::format("the parent process {} is gone", a_pid);
            return false;
        }
        m_quit = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_quit) {
            a_why = fmt::format("no event for the parent watch (Windows error {})", ::GetLastError());
            return false;
        }
        m_thread = std::thread([this, a_pid, a_graceMs]() {
            const HANDLE waits[] = { m_process, m_quit };
            const DWORD waited = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (waited == WAIT_OBJECT_0 + 1) {
                return;  // quit: the run ended
            }
            // the parent ended, or the wait failed (then nobody watches it any more: stop as if it ended)
            m_gone.store(true);
            // no CRT stream here: the main thread may hold std::cout's lock (a full pipe) and must not keep us from the exit
            const auto say = [](const std::string& a_text) {
                DWORD written = 0;
                ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), a_text.data(), static_cast<DWORD>(a_text.size()), &written, nullptr);
            };
            say(fmt::format("STOP  the parent process {} is gone: stopping after the object in hand\n", a_pid));
            if (::WaitForSingleObject(m_quit, a_graceMs) == WAIT_TIMEOUT) {
                say("STOP  still running after the grace time: exiting (temp files are cleared at the next start)\n");
                // TerminateProcess, not ExitProcess: no DLL detach while the main thread may hold the CRT or loader lock
                ::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(kExitParentGone));
            }
        });
        return true;
    }
}
