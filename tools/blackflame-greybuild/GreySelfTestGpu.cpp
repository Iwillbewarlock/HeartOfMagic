// =============================================================================
// GreySelfTestGpu - --self-test of the GPU BC7 encoder's guards (GreySelfTest.h CheckGpu, GreyGpuEncoder.h)
// =============================================================================
// The crash marker's state machine (fresh, one marker, two starts in a row, another driver or output identity, a CPU
// run), the output check's limits (a good result, a zeroed mip, alpha lost, another shape), the over-cap retry through
// the Budget's memory-probe seam, the throttle event, and guard 5: with a hardware device present the BC7 and sRGB BC7
// fixtures through the GPU against the CPU's result (format, mips, size, PSNR within the check's limits; not bytes).
// Without a device guard 5 prints an INFO line, not a FAIL.

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include "GreyBudget.h"
#include "GreyGolden.h"
#include "GreyGpuEncoder.h"
#include "GreySelfTest.h"
#include "GreyTexture.h"

#include "../../plugins/BlackFlame/src/BuilderSignal.h"

#include <windows.h>

#include <DirectXTex.h>

#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#include <fstream>
#include <string_view>

namespace GreyBuild::SelfTest
{
    namespace
    {
        constexpr const char* kAdapterA = "Test GPU (vendor 0x10DE, device 0x0001, driver 32.0.15.1)";
        constexpr const char* kAdapterNewDriver = "Test GPU (vendor 0x10DE, device 0x0001, driver 32.0.15.2)";
        constexpr const char* kOutput = "1";
        constexpr std::size_t kSide = 64;
        constexpr std::size_t kZeroedMip = 2;
        constexpr std::uint8_t kSpeckAlpha = 120;  // over kGpuMaxAlphaDiff, on one texel of 4096 (well inside the MSE limit)
        static_assert(kSpeckAlpha > kGpuMaxAlphaDiff, "the alpha speck must be over the alpha limit");
        constexpr std::uint8_t kOpaque = 255;
        constexpr std::uint64_t kGuardCap = 4096 * kBytesPerMiB;
        constexpr std::uint32_t kTestPid = 0xFFFFFF01u;  // a name only: no process has this id

        [[nodiscard]] std::string ReadAll(const std::filesystem::path& a_file)
        {
            std::ifstream in(a_file, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        void WriteAll(const std::filesystem::path& a_file, const std::string& a_text)
        {
            std::ofstream(a_file, std::ios::binary | std::ios::trunc) << a_text;
        }

        [[nodiscard]] bool Exists(const std::filesystem::path& a_file)
        {
            std::error_code ec;
            return std::filesystem::exists(a_file, ec);
        }

        void CheckMarkers(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto staging = a_dir / "gpu-markers";
            std::filesystem::remove_all(staging);
            std::filesystem::create_directories(staging);
            GpuMarkers markers(staging);

            const auto fresh = markers.OnStart(kAdapterA, kOutput);
            a_check.Check(fresh.allowed && fresh.strikes == 0 && fresh.note.empty() && !Exists(markers.Inflight()) && !Exists(markers.Disabled()),
                "gpu markers: a fresh staging allows the GPU, no strike, no file");
            std::string why;
            a_check.Check(markers.Enter(fresh.strikes, kAdapterA, why) && ReadAll(markers.Inflight()) == fmt::format("1\n{}\n", kAdapterA),
                "gpu markers: before a GPU call gpu.inflight holds strike 1 and the adapter");
            markers.Leave();
            a_check.Check(!Exists(markers.Inflight()), "gpu markers: after the call returned gpu.inflight is gone");

            // a run killed inside a call: the next start keeps the GPU and carries the strike
            WriteAll(markers.Inflight(), fmt::format("1\n{}\n", kAdapterA));
            const auto one = markers.OnStart(kAdapterA, kOutput);
            const bool oneOk = one.allowed && one.strikes == 1 && one.note.find("found") != std::string::npos && !Exists(markers.Inflight()) &&
                               !Exists(markers.Disabled());
            a_check.Check(oneOk && markers.Enter(one.strikes, kAdapterA, why) && ReadAll(markers.Inflight()) == fmt::format("2\n{}\n", kAdapterA),
                fmt::format("gpu markers: one marker found keeps the GPU, its own marker is strike 2 ({})", one.note));
            // and that run was killed inside a call too: two starts in a row
            const auto two = markers.OnStart(kAdapterA, kOutput);
            const auto disabledText = ReadAll(markers.Disabled());
            a_check.Check(!two.allowed && !Exists(markers.Inflight()) &&
                              disabledText == fmt::format("adapter={}\noutput={}\n", kAdapterA, kOutput),
                fmt::format("gpu markers: a marker at two starts in a row writes gpu.disabled with the adapter, driver and output "
                            "identity ('{}')", disabledText));
            const auto again = markers.OnStart(kAdapterA, kOutput);
            a_check.Check(!again.allowed && Exists(markers.Disabled()), "gpu markers: gpu.disabled holds at the next start (same adapter, driver, output)");

            // a CPU run (GpuUse::kOff) neither reads nor clears a marker
            WriteAll(markers.Inflight(), fmt::format("1\n{}\n", kAdapterA));
            {
                GpuEncoder cpu(GpuUse::kOff, staging, kOutput, kGuardCap, []() { return std::optional<MemoryState>(MemoryState{ 0, UINT64_MAX }); });
                cpu.Start();
                a_check.Check(!cpu.Usable() && !cpu.ReleaseForMemory(), "gpu markers: a CPU run has no GPU and no device to release");
            }
            a_check.Check(Exists(markers.Disabled()) && ReadAll(markers.Disabled()) == disabledText && Exists(markers.Inflight()),
                "gpu markers: a CPU run leaves gpu.disabled and gpu.inflight as they were");
            std::filesystem::remove(markers.Inflight());

            const auto newDriver = markers.OnStart(kAdapterNewDriver, kOutput);
            a_check.Check(newDriver.allowed && !Exists(markers.Disabled()) && newDriver.note.find("cleared") != std::string::npos,
                fmt::format("gpu markers: another driver version clears gpu.disabled ({})", newDriver.note));
            WriteAll(markers.Disabled(), fmt::format("adapter={}\noutput={}\n", kAdapterA, kOutput));
            const auto newOutput = markers.OnStart(kAdapterA, "2");
            a_check.Check(newOutput.allowed && !Exists(markers.Disabled()), "gpu markers: another output identity clears gpu.disabled");

            WriteAll(markers.Inflight(), "not a number\n");
            const auto garbled = markers.OnStart(kAdapterA, kOutput);
            a_check.Check(garbled.allowed && garbled.strikes == 1, "gpu markers: an unreadable marker counts as one strike");
        }

        /// A 64 x 64 RGBA8 chain: a grey ramp along x, alpha the same ramp (a_opaque: alpha 255 everywhere)
        [[nodiscard]] DirectX::ScratchImage Ramp(bool a_opaque)
        {
            DirectX::ScratchImage base;
            (void)base.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, kSide, kSide, 1, 1);
            const DirectX::Image& image = *base.GetImage(0, 0, 0);
            for (std::size_t y = 0; y < kSide; ++y) {
                for (std::size_t x = 0; x < kSide; ++x) {
                    auto* p = image.pixels + y * image.rowPitch + x * 4;
                    const auto value = static_cast<std::uint8_t>(64 + 2 * x);
                    p[0] = p[1] = p[2] = value;
                    p[3] = a_opaque ? kOpaque : value;  // alpha with the colour: one index set holds both (BC7 mode 6)
                }
            }
            DirectX::ScratchImage chain;
            (void)DirectX::GenerateMipMaps(image, static_cast<DirectX::TEX_FILTER_FLAGS>(DirectX::TEX_FILTER_BOX | DirectX::TEX_FILTER_FORCE_NON_WIC), 0,
                chain);
            return chain;
        }

        [[nodiscard]] DirectX::ScratchImage CpuBc7(const DirectX::ScratchImage& a_chain)
        {
            DirectX::ScratchImage out;
            (void)DirectX::Compress(a_chain.GetImages(), a_chain.GetImageCount(), a_chain.GetMetadata(), DXGI_FORMAT_BC7_UNORM,
                static_cast<DirectX::TEX_COMPRESS_FLAGS>(DirectX::TEX_COMPRESS_DEFAULT | DirectX::TEX_COMPRESS_BC7_QUICK),
                DirectX::TEX_THRESHOLD_DEFAULT, out);
            return out;
        }

        void CheckOutputLimits(Checker& a_check)
        {
            const auto grey = Ramp(false);
            auto encoded = CpuBc7(grey);
            const auto good = CheckGpuOutput(grey, encoded);
            a_check.Check(good.ok && good.worstPsnr >= kGpuMinPsnrDb && good.maxAlphaDiff <= kGpuMaxAlphaDiff,
                fmt::format("gpu check: a good BC7 of a ramp passes (worst {:.1f} dB at subresource {}, alpha difference {})",
                    good.worstPsnr, good.worstImage, good.maxAlphaDiff));

            // one texel's alpha far off, everything else as encoded: the MSE stays under its limit, the alpha one fires
            DirectX::ScratchImage speck;
            (void)speck.InitializeFromImage(*grey.GetImage(0, 0, 0));
            DirectX::ScratchImage speckChain;
            (void)DirectX::GenerateMipMaps(*speck.GetImage(0, 0, 0), static_cast<DirectX::TEX_FILTER_FLAGS>(DirectX::TEX_FILTER_BOX |
                DirectX::TEX_FILTER_FORCE_NON_WIC), 0, speckChain);
            speckChain.GetImages()[0].pixels[3] = static_cast<std::uint8_t>(speckChain.GetImages()[0].pixels[3] + kSpeckAlpha);
            const auto alphaSpeck = CheckGpuOutput(speckChain, encoded);
            a_check.Check(!alphaSpeck.ok && alphaSpeck.worstPsnr >= kGpuMinPsnrDb && alphaSpeck.maxAlphaDiff > kGpuMaxAlphaDiff,
                fmt::format("gpu check: one texel's alpha off by {} fails on alpha alone ({:.1f} dB, {})", kSpeckAlpha, alphaSpeck.worstPsnr,
                    alphaSpeck.why));

            const DirectX::Image& mip = encoded.GetImages()[kZeroedMip];
            std::memset(mip.pixels, 0, mip.slicePitch);
            const auto zeroed = CheckGpuOutput(grey, encoded);
            a_check.Check(!zeroed.ok && zeroed.worstImage == kZeroedMip && zeroed.why.find("dB") != std::string::npos,
                fmt::format("gpu check: a zeroed mip {} fails ({})", kZeroedMip, zeroed.why));

            const auto opaque = CpuBc7(Ramp(true));
            const auto alphaOff = CheckGpuOutput(grey, opaque);
            a_check.Check(!alphaOff.ok && alphaOff.maxAlphaDiff > kGpuMaxAlphaDiff,
                fmt::format("gpu check: a result with its alpha lost fails (alpha difference {}, {})", alphaOff.maxAlphaDiff, alphaOff.why));

            DirectX::ScratchImage oneMip;
            (void)DirectX::Compress(*grey.GetImage(0, 0, 0), DXGI_FORMAT_BC7_UNORM, DirectX::TEX_COMPRESS_BC7_QUICK,
                DirectX::TEX_THRESHOLD_DEFAULT, oneMip);
            const auto shape = CheckGpuOutput(grey, oneMip);
            a_check.Check(!shape.ok && shape.why.find("shapes differ") != std::string::npos,
                fmt::format("gpu check: a result with another mip count fails ({})", shape.why));
        }

        void CheckOverCapRetry(Checker& a_check)
        {
            constexpr std::uint64_t kCap = 100;
            bool released = false;
            int calls = 0;
            Budget budget(kCap, 0, nullptr, nullptr, [&]() { return std::optional<MemoryState>(MemoryState{ released ? 10u : 90u, 1000 }); },
                [](unsigned) {});
            budget.SetDeviceRelease([&]() {
                ++calls;
                released = true;
                return true;
            });
            std::string why;
            a_check.Check(budget.Acquire(20, why) == Admission::kAdmit && calls == 1 && budget.DeviceReleases() == 1,
                "gpu budget: over the cap, the device is released and the texture admitted on the second measure");

            int heldCalls = 0;
            Budget held(kCap, 0, nullptr, nullptr, []() { return std::optional<MemoryState>(MemoryState{ 90, 1000 }); }, [](unsigned) {});
            held.SetDeviceRelease([&]() {
                ++heldCalls;
                return false;  // no device held
            });
            std::string overWhy;
            a_check.Check(held.Acquire(20, overWhy) == Admission::kOverCap && heldCalls == 1 && held.DeviceReleases() == 0 && !overWhy.empty(),
                "gpu budget: no device to release: still over the cap fails the object as before (retryable)");
            std::string largeWhy;
            a_check.Check(held.Acquire(200, largeWhy) == Admission::kTooLarge && heldCalls == 1,
                "gpu budget: too large is the estimate alone (no device release, never caused by the device)");
        }

        void CheckThrottle(Checker& a_check)
        {
            ThrottleSignal absent;
            a_check.Check(!absent.Open(kTestPid) && !absent.IsSet(), "gpu throttle: no event from the parent: never throttled");
            const auto name = BlackFlame::BuilderSignal::ThrottleEventName(kTestPid);
            HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, name.c_str());
            ThrottleSignal signal;
            const bool opened = event && signal.Open(kTestPid);
            const bool before = signal.IsSet();
            if (event) {
                ::SetEvent(event);
            }
            const bool after = signal.IsSet();
            a_check.Check(opened && !before && after && name == L"Local\\HoMBlackFlame_Throttle_4294967041",
                "gpu throttle: the parent's event opens by its pid's name, reads unset, then set once the DLL sets it");
            if (event) {
                ::CloseHandle(event);
            }
        }

        [[nodiscard]] std::optional<DirectX::ScratchImage> Load(const std::filesystem::path& a_file)
        {
            DirectX::ScratchImage image;
            if (FAILED(DirectX::LoadFromDDSFile(a_file.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image))) {
                return std::nullopt;
            }
            return image;
        }

        void CheckGuardFive(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_goldenDir)
        {
            const auto dir = a_dir / "gpu-guard5";
            std::filesystem::remove_all(dir);
            std::filesystem::create_directories(dir / "staging");
            struct Fixture
            {
                const char* name;
                const char* golden;
                DXGI_FORMAT format;
            };
            const Fixture fixtures[] = { { "bc7", Golden::kBc7, DXGI_FORMAT_BC7_UNORM }, { "bc7srgb", Golden::kBc7Srgb, DXGI_FORMAT_BC7_UNORM_SRGB },
                { "bc7wide", Golden::kBc7Wide, DXGI_FORMAT_BC7_UNORM } };
            GpuEncoder gpu(GpuUse::kAuto, dir / "staging", kOutput, kGuardCap, []() { return std::optional<MemoryState>(MemoryState{ 0, UINT64_MAX }); });
            gpu.Start();
            for (const auto& fixture : fixtures) {
                auto source = a_goldenDir / Golden::kInputFolder / fixture.golden;
                if (!Exists(source)) {  // a tester package has no fixtures: the self-test's own orange texture in that format
                    if (std::string_view(fixture.golden) == Golden::kBc7Wide) {
                        continue;  // no stand-in for the non-square one
                    }
                    source = dir / fmt::format("{}.dds", fixture.name);
                    if (!WriteTestTexture(source, fixture.format, true)) {
                        a_check.Check(false, fmt::format("gpu guard 5: {} not written", PathText(source)));
                        continue;
                    }
                }
                const auto cpuOut = dir / fmt::format("{}.cpu.dds", fixture.name);
                const auto gpuOut = dir / fmt::format("{}.gpu.dds", fixture.name);
                const auto cpu = RecolourDds(source, cpuOut, 1.0f);
                const auto onGpu = RecolourDds(source, gpuOut, 1.0f, &gpu);
                if (!onGpu.gpu && !gpu.DeviceMade()) {
                    std::cout << fmt::format("INFO gpu guard 5: {} not on the GPU, no hardware device here ({}): not a failure\n", fixture.name,
                        onGpu.gpuNote.empty() ? std::string("see the GPU line above") : onGpu.gpuNote);
                    continue;
                }
                const auto cpuImage = Load(cpuOut);
                const auto gpuImage = Load(gpuOut);
                DirectX::ScratchImage reference;
                const bool decoded = cpuImage && gpuImage &&
                                     SUCCEEDED(DirectX::Decompress(cpuImage->GetImages(), cpuImage->GetImageCount(), cpuImage->GetMetadata(),
                                         DirectX::IsSRGB(fixture.format) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, reference));
                const auto check = decoded ? CheckGpuOutput(reference, *gpuImage) : GpuOutputCheck{};
                a_check.Check(cpu.outcome == RecolourOutcome::kRecoloured && onGpu.outcome == RecolourOutcome::kRecoloured && onGpu.gpu &&
                                  !cpu.gpu && decoded && gpuImage->GetMetadata().format == fixture.format &&
                                  gpuImage->GetMetadata().mipLevels == cpuImage->GetMetadata().mipLevels && onGpu.bytes == cpu.bytes && check.ok,
                    fmt::format("gpu guard 5: {} on the GPU is {} with {} mips and {} bytes like the CPU's, within the check's limits of "
                                "it (worst {:.1f} dB, alpha difference {}; {})", fixture.name, onGpu.format, onGpu.mips, onGpu.bytes,
                        check.worstPsnr, check.maxAlphaDiff, onGpu.gpuNote));
            }
            a_check.Check(!Exists(dir / "staging" / kGpuInflightFile) && !Exists(dir / "staging" / kGpuDisabledFile),
                "gpu guard 5: no crash marker is left after the calls returned");
            std::cout << gpu.SummaryLine() << "\n";
        }
    }

    void CheckGpu(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_goldenDir)
    {
        try {
            CheckMarkers(a_check, a_dir);
            CheckOutputLimits(a_check);
            CheckOverCapRetry(a_check);
            CheckThrottle(a_check);
            CheckGuardFive(a_check, a_dir, a_goldenDir);
        } catch (const std::exception& e) {
            a_check.Check(false, fmt::format("gpu: exception: {}", e.what()));
        }
    }
}
