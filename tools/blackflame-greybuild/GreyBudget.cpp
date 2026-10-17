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

#include <windows.h>

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
        for (std::size_t mip = 0; mip < std::max<std::size_t>(meta.mipLevels, 1); ++mip) {
            chain += width * height * depth * kDecodedBytesPerTexel;
            width = std::max<std::uint64_t>(width / 2, 1);
            height = std::max<std::uint64_t>(height / 2, 1);
            depth = std::max<std::uint64_t>(depth / 2, 1);
        }
        return chain * std::max<std::uint64_t>(meta.arraySize, 1) * kEncoderImages;
    }

    std::optional<std::uint64_t> EstimateReadBytes(const std::filesystem::path& a_dds, std::string& a_why)
    {
        DirectX::TexMetadata meta{};
        if (const HRESULT hr = DirectX::GetMetadataFromDDSFile(a_dds.c_str(), kHeaderFlags, meta); FAILED(hr)) {
            a_why = fmt::format("the DDS header of {} does not read (HRESULT 0x{:08X})", PathText(a_dds), static_cast<std::uint32_t>(hr));
            return std::nullopt;
        }
        std::error_code ec;
        const std::uint64_t file = std::filesystem::file_size(a_dds, ec);
        return (ec ? 0 : file) + static_cast<std::uint64_t>(meta.width) * meta.height * kDecodedBytesPerTexel * kReadImages;
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
        bool paused = false;
        for (unsigned step = 0;; ++step) {
            if (m_stop && m_stop->load()) {
                a_why = "the run is stopping (the parent process is gone)";
                return Admission::kOverCap;
            }
            // Not measurable: the cap alone decides (the job object still holds the process to it)
            const MemoryState state = m_probe ? m_probe().value_or(MemoryState{ 0, UINT64_MAX }) : MemoryState{ 0, UINT64_MAX };
            const Admission admission = Admit(a_estimate, state.inUse, m_cap, state.availableCommit, m_reserve);
            if (admission == Admission::kOverCap && a_mayClear && !evicted && m_cache && m_cache->CachedBytes() > 0) {
                m_cache->Clear();
                ++m_evictions;
                evicted = true;
                continue;
            }
            if (admission == Admission::kPause && step >= kMaxPauseSteps) {
                a_why = fmt::format("waited {} s for the system's commit to come back above the reserve", step * kPauseStepMs / 1000);
                return Admission::kOverCap;
            }
            if (admission == Admission::kPause) {
                if (!paused) {
                    ++m_pauses;
                    paused = true;
                }
                if (step % kPauseNoteEvery == 0) {
                    std::cout << fmt::format("WAIT  the system has {:.0f} MiB of commit left (reserve {:.0f} MiB, this texture needs "
                                             "about {:.0f} MiB): waiting\n", MiB(state.availableCommit), MiB(m_reserve), MiB(a_estimate));
                }
                m_sleep(kPauseStepMs);
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

    void Budget::BetweenObjects()
    {
        if (m_cache && m_cache->CachedBytes() > kTextureCacheKeepBytes) {
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
        m_process = ::OpenProcess(SYNCHRONIZE, FALSE, a_pid);
        if (!m_process) {
            const DWORD error = ::GetLastError();
            m_noProcess = error == ERROR_INVALID_PARAMETER;  // what OpenProcess says for a pid that names no process
            a_why = m_noProcess ? fmt::format("the parent process {} is gone", a_pid) :
                                  fmt::format("the parent process {} does not open (Windows error {})", a_pid, error);
            return false;
        }
        m_quit = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_quit) {
            a_why = fmt::format("no event for the parent watch (Windows error {})", ::GetLastError());
            return false;
        }
        m_thread = std::thread([this, a_pid, a_graceMs]() {
            const HANDLE waits[] = { m_process, m_quit };
            if (::WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) {
                return;  // quit (the run ended) or a failed wait
            }
            m_gone.store(true);
            std::cout << fmt::format("STOP  the parent process {} is gone: stopping after the object in hand\n", a_pid);
            if (::WaitForSingleObject(m_quit, a_graceMs) == WAIT_TIMEOUT) {
                std::cout << "STOP  still running after the grace time: exiting (temp files are cleared at the next start)\n";
                std::cout.flush();
                // TerminateProcess, not ExitProcess: no DLL detach while the main thread may hold the CRT or loader lock
                ::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(kExitParentGone));
            }
        });
        return true;
    }
}
