#pragma once

// =============================================================================
// GreyGpuEncoder - BC7 targets on the GPU (DirectXTex's DirectCompute encoder) with their guards
// =============================================================================
// Key mode only (RunKeyed): RecolourDds hands a BC7_UNORM / BC7_UNORM_SRGB target here first; anything else, a refusal
// or a failure uses today's CPU encoder on the same grey image. The guards (bf-no-wait-proposal.md 3.1):
//   adapter     IDXGIFactory6::EnumAdapterByGpuPreference(HIGH_PERFORMANCE), else IDXGIFactory1::EnumAdapters1; the
//               first hardware adapter (no DXGI_ADAPTER_FLAG_SOFTWARE); logged once at the start
//   device      D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, feature level 11_0), made at the first BC7 texture
//               and only while cap - PrivateUsage >= kGpuDeviceShareBytes; SetGPUThreadPriority(kGpuThreadPriority) so
//               the game's GPU work goes first (whether WDDM 2.x honours it is not known; harmless if not)
//   release     device and context: at the first failure (the rest of the run is CPU), before an over-cap failure
//               (Budget's device release, then the device may be made again later), and for the rest of the run on
//               the throttle event (GreyBudget.h ThrottleSignal)
//   crash guard staging\gpu.inflight holds "<strikes>\n<adapter>\n" only while device creation or a GPU Compress runs
//               (written before, removed after: a kill outside those calls leaves nothing). A start that finds it
//               logs it and keeps the GPU, carrying the strike into its own markers; a marker of strike 2 (two starts
//               in a row ended inside a GPU call) writes staging\gpu.disabled ("adapter=...\noutput=...\n") and the
//               CPU encodes from then on. gpu.disabled is cleared only at a start whose adapter (description, ids,
//               driver version) or output identity differs; a CPU run (GpuUse::kOff) never reads or writes a marker
//   output      each GPU result is decoded and compared with the grey image on every subresource (CheckGpuOutput):
//               over a limit, that texture is encoded again on the CPU (RecolourResult::gpuNote says why). A small
//               local defect under the limits is not caught (known limit)
// The adapter LUID changes at every boot: it is logged, never compared.

#include "GreyBudget.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace DirectX
{
    class ScratchImage;
}

namespace GreyBuild
{
    // What the GPU path adds at its peak (the device, DirectXTex's per-mip GPU buffers, the output check's decode),
    // measured on the real run-2 request in a 1536 MiB job: 979 MiB peak with the GPU against 563 MiB forced to the CPU,
    // one exe (2026-10-19): 416 MiB, rounded up. (Run E2 against run B, two different exes without the output check, had
    // given 336.) Used for the creation decision only: once made, its commit is in PrivateUsage, which Budget::Acquire
    // already reads
    inline constexpr std::uint64_t kGpuDeviceShareBytes = 448 * kBytesPerMiB;
    inline constexpr int kGpuThreadPriority = -7;  // IDXGIDevice::SetGPUThreadPriority: -7 is the lowest
    // The GPU overload's float is the BC7 alpha weight (DirectX::TEX_ALPHA_WEIGHT_DEFAULT); the CPU overload's float is
    // BC1's alpha threshold (kCpuEncodeThreshold, GreyTexture.cpp): two different things in the same place
    inline constexpr float kGpuAlphaWeight = 1.0f;
    // Output check limits, per subresource (every mip and array item), all four channels on 0..1: PSNR = 10 log10(1 / MSE).
    // Measured on the real run-2 request's 52 BC7 textures (RTX 3070 Laptop, driver 32.0.16.1664, 2026-10-18): mip 0 at
    // 37.6 dB or more (run D), every subresource at 30.1 dB or more, the lowest mostly on small mips (8 x 8 and below,
    // where the downsampled detail is densest); the CPU's BC7_QUICK went down to 25.5 dB on mip 0. The limit sits 6 dB
    // under that floor, so an honest but detailed texture is not sent to the CPU encoder for nothing, while a black,
    // zeroed or wrong mip (5.7 dB, the self-test's) or lost alpha (11.7 dB) is caught. Known limit: another GPU or driver
    // whose small mips land under it sends that texture to the CPU (slower, never wrong)
    inline constexpr double kGpuMinPsnrDb = 24.0;
    // The largest alpha difference of one texel, in 8-bit steps: at most 50 on the same 52 textures (same GPU and
    // driver), so about twice that; an alpha channel lost (190) or one texel far off (the self-test's 120) is over it
    inline constexpr unsigned kGpuMaxAlphaDiff = 96;
    inline constexpr int kGpuDisableStrikes = 2;  // a marker of this strike count at a start disables the GPU
    inline constexpr const char* kGpuInflightFile = "gpu.inflight";
    inline constexpr const char* kGpuDisabledFile = "gpu.disabled";
    // Tests only: BLACKFLAME_GREYBUILD_GPU=off runs a key mode run on the CPU encoder alone (a forced-CPU measurement)
    inline constexpr const wchar_t* kGpuEnvironmentVariable = L"BLACKFLAME_GREYBUILD_GPU";

    enum class GpuUse
    {
        kOff,   // the CPU encoder only, no adapter, device or marker (the golden test, the variable above)
        kAuto   // the GPU for BC7 when its guards allow
    };

    /// kOff when kGpuEnvironmentVariable is "off" (any case), else a_default
    [[nodiscard]] GpuUse GpuUseFromEnvironment(GpuUse a_default) noexcept;

    struct GpuOutputCheck
    {
        bool ok = false;
        double worstPsnr = 0.0;       // dB; the subresource with the largest MSE (infinity when every one is exact)
        std::size_t worstImage = 0;   // its index in the chain (DirectX::TexMetadata::ComputeIndex order)
        unsigned maxAlphaDiff = 0;    // over every subresource, 8-bit steps
        std::string why;              // !ok: what was over or did not match
    };

    /// a_encoded (a BC7 chain) decoded one subresource at a time into a_reference's format and compared with it (an
    /// R8G8B8A8_UNORM or _SRGB chain of the same shape): MSE per subresource against kGpuMinPsnrDb, the largest alpha
    /// difference against kGpuMaxAlphaDiff
    [[nodiscard]] GpuOutputCheck CheckGpuOutput(const DirectX::ScratchImage& a_reference, const DirectX::ScratchImage& a_encoded);

    /// The crash marker's state machine on a staging folder (header comment), apart from any device
    class GpuMarkers
    {
    public:
        explicit GpuMarkers(std::filesystem::path a_staging);

        struct Start
        {
            bool allowed = true;  // false: gpu.disabled holds (or was just written) for this adapter and output
            int strikes = 0;      // carried into this run's markers (the marker found, else 0)
            std::string note;     // what was found and done, "" when nothing was there
        };

        /// At a start that may use the GPU: a_adapter is this start's adapter identity ("" when none is usable), a_output
        /// the output identity. Reads and settles both files (the marker found is removed: this run writes its own)
        [[nodiscard]] Start OnStart(const std::string& a_adapter, const std::string& a_output);

        /// gpu.inflight written (strikes + 1, a_adapter) before a GPU call; false with a_why when it is not written (then
        /// the call is not made: no crash guard, no GPU)
        [[nodiscard]] bool Enter(int a_strikes, const std::string& a_adapter, std::string& a_why);
        /// gpu.inflight removed after the call returned
        void Leave() noexcept;

        [[nodiscard]] std::filesystem::path Inflight() const { return m_staging / kGpuInflightFile; }
        [[nodiscard]] std::filesystem::path Disabled() const { return m_staging / kGpuDisabledFile; }

    private:
        std::filesystem::path m_staging;
    };

    /// One key mode run's GPU encoder (header comment); not thread-safe (one worker)
    class GpuEncoder
    {
    public:
        /// a_probe: Budget's memory probe (the device's creation decision); a_staging holds the markers
        GpuEncoder(GpuUse a_use, std::filesystem::path a_staging, std::string a_outputIdentity, std::uint64_t a_cap, MemoryProbe a_probe);
        ~GpuEncoder();
        GpuEncoder(const GpuEncoder&) = delete;
        GpuEncoder& operator=(const GpuEncoder&) = delete;

        /// The adapter found and logged, the markers settled; prints one "GPU   ..." line with the decision
        void Start();

        /// a_grey (an R8G8B8A8 chain) encoded to a_format (BC7) into a_out: true. false: encode on the CPU; a_why is
        /// set only when the GPU was tried and failed (a refusal by state or memory leaves it empty and prints its own
        /// line once)
        [[nodiscard]] bool Encode(const DirectX::ScratchImage& a_grey, unsigned a_dxgiFormat, DirectX::ScratchImage& a_out,
            std::string& a_why);

        /// Over the cap (Budget): true when a device was held and is now released (it may be made again later)
        [[nodiscard]] bool ReleaseForMemory();

        /// The throttle event: the device released and never made again this run
        void Throttle();

        /// A result CheckGpuOutput refused (counted for the run's summary)
        void NoteRejected() noexcept { ++m_rejected; }

        /// "gpu: <adapter> - N textures, M refused by the output check; ..." for the run's end
        [[nodiscard]] std::string SummaryLine() const;

        [[nodiscard]] bool Usable() const noexcept { return m_use == GpuUse::kAuto && m_allowed && !m_stopped; }
        [[nodiscard]] int Encoded() const noexcept { return m_encoded; }
        [[nodiscard]] bool DeviceMade() const noexcept { return m_devices > 0; }

    private:
        struct Device;  // the D3D11 and DXGI objects (GreyGpuEncoder.cpp)

        [[nodiscard]] bool MakeDevice();
        void Release() noexcept;
        void Stop(const std::string& a_why);

        GpuUse m_use;
        GpuMarkers m_markers;
        std::string m_output;
        std::uint64_t m_cap;
        MemoryProbe m_probe;
        std::unique_ptr<Device> m_device;
        std::string m_adapter;      // the identity the markers compare ("" none)
        std::string m_adapterLine;  // the logged description
        int m_strikes = 0;
        bool m_allowed = false;     // Start found an adapter and no gpu.disabled for it
        bool m_stopped = false;     // a failure or the throttle: CPU for the rest of the run
        bool m_admissionNoted = false;
        std::string m_stopWhy;
        int m_encoded = 0;
        int m_rejected = 0;
        int m_devices = 0;          // devices made (more than one after an over-cap release)
        int m_releases = 0;         // over-cap releases
    };
}
