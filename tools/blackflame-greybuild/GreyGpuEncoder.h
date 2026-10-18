#pragma once

// =============================================================================
// GreyGpuEncoder - BC7 targets on the GPU (DirectXTex's DirectCompute encoder) with their guards
// =============================================================================
// Key mode only (RunKeyed): RecolourDds hands a BC7_UNORM / BC7_UNORM_SRGB target here first; anything else, a refusal
// or a failure uses today's CPU encoder on the same grey image. The guards (bf-no-wait-proposal.md 3.1):
//   adapter     IDXGIFactory6::EnumAdapterByGpuPreference(HIGH_PERFORMANCE), else IDXGIFactory1::EnumAdapters1; the
//               first hardware adapter (no DXGI_ADAPTER_FLAG_SOFTWARE); logged once at the start. Each list ends at
//               its first failed call (DXGI_ERROR_NOT_FOUND or any other)
//   device      D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, feature level 11_0), made at the first BC7 texture
//               and only while Budget's rule (Admit: the job cap and the system commit reserve) admits
//               kGpuDeviceShareBytes + that texture's encoded chain + mip 0's RGBA twice (the output check's decode and
//               the upload or readback copies of the call; review round 1); else noted once, CPU until there is room;
//               SetGPUThreadPriority(kGpuThreadPriority) so the game's GPU work goes first (whether WDDM 2.x honours
//               it is not known; harmless if not). After each Compress the context is cleared and flushed, so the
//               call's buffers are freed before the output check and the next memory probe
//   release     device and context: at the first failure (the rest of the run is CPU), before an over-cap failure or
//               a wait for commit (Budget's device release, then the device may be made again later), and for the rest
//               of the run on the throttle event (GreyBudget.h ThrottleSignal), read before each object and at each
//               Encode (an object's later textures; review round 1)
//   crash guard staging\gpu.inflight holds "<strikes>\n<adapter>\n<texture>\n" only while device creation or a GPU
//               Compress runs (written before, removed after: a kill outside those calls leaves nothing). A start that
//               finds it logs it and keeps the GPU, carrying the strike into its own markers only for the same texture
//               (a game exit's kill lands in whatever call runs: two in a row on different textures say nothing about
//               the GPU; review round 1): another texture's call writes strike 1. A marker of the old form (no texture
//               line) reads, and its strike is never carried. A marker of strike 2 (two starts in a row ended inside a
//               GPU call on the same texture) writes staging\gpu.disabled ("adapter=...\noutput=...\n") and the
//               CPU encodes from then on; the marker stays until gpu.disabled is written (a failed write leaves both
//               strikes for the next start). A marker that names another adapter or driver is dropped with its strikes;
//               one that exists but does not read counts as one strike and is left in place (its count is not lost).
//               gpu.disabled is cleared, with any marker, only at a start whose adapter (description, ids, driver
//               version) or output identity differs and only when it is whole (both fields there and non-empty, the
//               output a valid identity); one that exists but does not read, or is malformed, keeps the GPU off and is
//               never deleted; a stale one that cannot be removed keeps the GPU off for that run. An adapter whose
//               driver version does not read ("driver unknown (...)": a driver update could not clear it) never gets a
//               gpu.disabled: its strike 2 puts this run alone on the CPU and drops the marker, and a gpu.disabled
//               written for such an identity is removed at a start like a stale one (review round 1). Both files are written
//               to <file>.tmp, flushed, and renamed over the file (MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH):
//               a failed write leaves no partial marker (the temp file is removed; a start removes a leftover one). A
//               CPU run (GpuUse::kOff) never reads or writes a marker
//   re-creation after a release for memory the device is made again at most once a run (kGpuMaxDevicesPerRun); after
//               the throttle, never
//   output      each GPU result is decoded and compared with the grey image on every subresource (CheckGpuOutput):
//               over a limit, that texture is encoded again on the CPU (RecolourResult::gpuNote says why);
//               kGpuMaxRejectsInRow refusals in a row stop the GPU for the run (review round 1). A small local defect
//               under the limits is not caught (known limit)
// The adapter LUID changes at every boot: it is logged, never compared.
// The GPU's bytes are not in the golden test (it runs GpuUse::kOff; guard 5 checks the GPU by tolerance), so a change to
// Encode's settings (its TEX_COMPRESS flags, kGpuAlphaWeight) changes shipped output unseen: it must bump
// kBuilderOutputVersion (CopyContractKeys.h) like any other change of output.

#include "GreyBudget.h"

#include <cstdint>
#include <filesystem>
#include <functional>
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
    // BC1's alpha threshold (kCpuEncodeThreshold, GreyTexture.cpp): two different things in the same place. A change
    // bumps kBuilderOutputVersion (header comment)
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
    inline constexpr int kGpuMaxRejectsInRow = 3;  // output check refusals in a row that stop the GPU for the run
    inline constexpr int kGpuMaxDevicesPerRun = 2;  // the first device and one re-creation after a release for memory
    inline constexpr const char* kGpuInflightFile = "gpu.inflight";
    inline constexpr const char* kGpuDisabledFile = "gpu.disabled";
    // Tests only: BLACKFLAME_GREYBUILD_GPU=off runs a key mode run on the CPU encoder alone (a forced-CPU measurement)
    inline constexpr const wchar_t* kGpuEnvironmentVariable = L"BLACKFLAME_GREYBUILD_GPU";

    enum class GpuUse
    {
        kOff,      // the CPU encoder only, no adapter, device or marker (the golden test, the variable above)
        kAuto,     // the GPU for BC7 when its guards allow
        kTestWarp  // the self-test only: kAuto on Microsoft's software adapter (WARP), so the encoder's state machine runs
                   // on any machine; never chosen by a run
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
    /// difference against kGpuMaxAlphaDiff. a_target (a DXGI_FORMAT; 0: not checked): a_encoded in another format fails
    /// like another shape
    [[nodiscard]] GpuOutputCheck CheckGpuOutput(const DirectX::ScratchImage& a_reference, const DirectX::ScratchImage& a_encoded,
        unsigned a_target = 0);

    enum class MarkerRead
    {
        kMissing,    // no such file
        kRead,       // read whole: text
        kUnreadable  // there (or not known not to be) and does not open or read: why
    };

    struct MarkerText
    {
        MarkerRead state = MarkerRead::kMissing;
        std::string text;
        std::string why;
    };

    /// The markers' file I/O; an empty function is the real one (the self-test's seam for read and write failures)
    struct MarkerIo
    {
        /// The real write's first step: the bytes into <marker>.tmp (true: all written); the rename over the marker follows
        using WriteTemp = std::function<bool(const std::filesystem::path& a_temp, const std::string& a_text, std::string& a_why)>;

        std::function<MarkerText(const std::filesystem::path&)> read;
        std::function<bool(const std::filesystem::path&, const std::string&, std::string&)> write;  // the whole write replaced
        WriteTemp writeTemp;  // only the temp file step replaced (a write that fails part way): the real rename and cleanup run
    };

    /// The crash marker's state machine on a staging folder (header comment), apart from any device
    class GpuMarkers
    {
    public:
        explicit GpuMarkers(std::filesystem::path a_staging, MarkerIo a_io = {});

        struct Start
        {
            bool allowed = true;  // false: gpu.disabled holds (or was just written) for this adapter and output
            int strikes = 0;      // carried into this run's markers (the marker found, else 0)
            std::string texture;  // the texture whose call the marker found was written for ("" none, or the old form)
            std::string note;     // what was found and done, "" when nothing was there
        };

        /// At a start that may use the GPU: a_adapter is this start's adapter identity ("" when none is usable), a_output
        /// the output identity. Reads and settles both files (the marker found is removed: this run writes its own)
        [[nodiscard]] Start OnStart(const std::string& a_adapter, const std::string& a_output);

        /// gpu.inflight written before a GPU call on a_texture: strike a_strikes + 1 when a_texture is a_carriedTexture
        /// (Start::texture, non-empty), else strike 1; false with a_why when it is not written (then the call is not made:
        /// no crash guard, no GPU)
        [[nodiscard]] bool Enter(int a_strikes, const std::string& a_carriedTexture, const std::string& a_adapter,
            const std::string& a_texture, std::string& a_why);
        /// gpu.inflight removed after the call returned
        void Leave() noexcept;

        [[nodiscard]] std::filesystem::path Inflight() const { return m_staging / kGpuInflightFile; }
        [[nodiscard]] std::filesystem::path Disabled() const { return m_staging / kGpuDisabledFile; }

    private:
        [[nodiscard]] MarkerText Read(const std::filesystem::path& a_file) const;
        [[nodiscard]] bool Write(const std::filesystem::path& a_file, const std::string& a_text, std::string& a_why) const;

        std::filesystem::path m_staging;
        MarkerIo m_io;
    };

    /// One key mode run's GPU encoder (header comment); not thread-safe (one worker)
    class GpuEncoder
    {
    public:
        /// a_probe: Budget's memory probe and a_cap, a_reserve its cap and commit reserve (the device's creation decision);
        /// a_staging holds the markers
        GpuEncoder(GpuUse a_use, std::filesystem::path a_staging, std::string a_outputIdentity, std::uint64_t a_cap, std::uint64_t a_reserve,
            MemoryProbe a_probe);
        ~GpuEncoder();
        GpuEncoder(const GpuEncoder&) = delete;
        GpuEncoder& operator=(const GpuEncoder&) = delete;

        /// The adapter found and logged, the markers settled; prints one "GPU   ..." line with the decision
        void Start();

        /// a_grey (an R8G8B8A8 chain) encoded to a_format (BC7) into a_out: true. false: encode on the CPU; a_why is
        /// set only when the GPU was tried and failed (a refusal by state or memory leaves it empty and prints its own
        /// line once). A device is made only while Admit (the cap and the commit reserve) admits kGpuDeviceShareBytes plus
        /// what the call still allocates (the encoded chain from a_grey's shape in a_format, and mip 0's RGBA twice). A
        /// throttle check that reads set (SetThrottleCheck) throttles first: false, the CPU. a_texture names the texture in
        /// the crash marker (the strike rule). The caller checks the result (CheckGpuOutput) and reports it with NoteResult
        [[nodiscard]] bool Encode(const DirectX::ScratchImage& a_grey, unsigned a_dxgiFormat, DirectX::ScratchImage& a_out,
            std::string& a_why, const std::string& a_texture = {});

        /// Over the cap (Budget): true when a device was held and is now released (it may be made again later)
        [[nodiscard]] bool ReleaseForMemory();

        /// The throttle event: the device released and never made again this run
        void Throttle();

        /// The throttle event's read for Encode (true: set); none: only the object loop's Throttle call throttles
        void SetThrottleCheck(std::function<bool()> a_isSet) { m_throttleCheck = std::move(a_isSet); }

        /// The output check's verdict on a result Encode returned: accepted ones are the run's GPU encodes, refused ones
        /// were encoded again on the CPU (both counted for the summary); kGpuMaxRejectsInRow refusals in a row stop the GPU
        void NoteResult(bool a_accepted);

        /// "gpu: <adapter> - N textures, M refused by the output check; ..." for the run's end ("no adapter" when none was
        /// found; "disabled" only when one was found and gpu.disabled holds for it)
        [[nodiscard]] std::string SummaryLine() const;

        [[nodiscard]] bool Usable() const noexcept { return m_use != GpuUse::kOff && m_allowed && !m_stopped; }
        [[nodiscard]] int Encoded() const noexcept { return m_encoded; }
        [[nodiscard]] bool DeviceMade() const noexcept { return m_devices > 0; }
        [[nodiscard]] bool DeviceHeld() const noexcept;
        [[nodiscard]] int Devices() const noexcept { return m_devices; }
        [[nodiscard]] int Rejected() const noexcept { return m_rejected; }
        [[nodiscard]] int Strikes() const noexcept { return m_strikes; }
        [[nodiscard]] bool HeadroomNoted() const noexcept { return m_admissionNoted; }
        [[nodiscard]] bool AdapterFound() const noexcept { return !m_adapter.empty(); }
        [[nodiscard]] const std::string& Adapter() const noexcept { return m_adapter; }  // the markers' identity
        [[nodiscard]] bool Stopped() const noexcept { return m_stopped; }

    private:
        struct Device;  // the D3D11 and DXGI objects (GreyGpuEncoder.cpp)

        [[nodiscard]] bool MakeDevice(const std::string& a_texture);
        void Release() noexcept;
        void Stop(const std::string& a_why);

        GpuUse m_use;
        GpuMarkers m_markers;
        std::string m_output;
        std::uint64_t m_cap;
        std::uint64_t m_reserve;
        MemoryProbe m_probe;
        std::function<bool()> m_throttleCheck;
        std::unique_ptr<Device> m_device;
        std::string m_adapter;      // the identity the markers compare ("" none)
        std::string m_adapterLine;  // the logged description
        int m_strikes = 0;
        std::string m_strikeTexture;  // the texture the found marker's strike was for (Start::texture)
        bool m_allowed = false;     // Start found an adapter and no gpu.disabled for it
        bool m_stopped = false;     // a failure or the throttle: CPU for the rest of the run
        bool m_throttled = false;
        bool m_admissionNoted = false;
        std::string m_stopWhy;
        int m_encoded = 0;
        int m_rejected = 0;
        int m_rejectedInRow = 0;
        int m_devices = 0;          // devices made (more than one after an over-cap release)
        int m_releases = 0;         // releases for memory
    };
}
