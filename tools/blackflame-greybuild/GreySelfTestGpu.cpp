// =============================================================================
// GreySelfTestGpu - --self-test of the GPU BC7 encoder's guards (GreySelfTest.h CheckGpu, GreyGpuEncoder.h)
// =============================================================================
// The crash marker's state machine (fresh, one marker, two starts in a row, another driver or output identity, a CPU
// run) and its file I/O failures through GpuMarkers' MarkerIo seam (a gpu.disabled or gpu.inflight that does not read, a
// gpu.disabled that cannot be written, a malformed gpu.disabled, a marker of another adapter) and on real files (a write
// cut after some bytes, a rename that fails, a leftover temp file, a stale gpu.disabled that cannot be removed), the
// output check's limits
// (a good result, a zeroed mip, alpha lost, another shape, another format), the over-cap and short-of-commit device
// release through the Budget's memory-probe seam, the throttle event, and the encoder's own state on Microsoft's software
// adapter (GpuUse::kTestWarp, on every machine): Start's strikes, the device's headroom (share plus the encoded chain),
// the one re-creation after a release for memory, none after the throttle, the summary's counts and words, and the
// wiring (RecolourDds hands a BC7 target to a usable encoder: the regression check that runs on every machine). WARP that
// cannot be found or made FAILs only on a Windows that has it: d3d10warp.dll in System32 and an IDXGIFactory4 (the
// interface EnumWarpAdapter is on); otherwise INFO. Not injectable: a hardware adapter that is absent (FindAdapter has no
// seam; an encoder that was never started is that state for the summary line).
// Guard 5: with a hardware adapter the BC7 and sRGB BC7 fixtures through the GPU against the CPU's result (format, mips,
// size, PSNR within the check's limits; not bytes). It FAILs only when the encoder is usable and was not tried (wiring).
// A hardware adapter that gives no device (an FL 10.x iGPU, a TDR, no headroom), an output-check refusal or a failed GPU
// encode is the documented CPU fallback: INFO. No adapter: INFO.

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

#include <dxgi1_4.h>
#include <wrl/client.h>

#include <DirectXTex.h>

#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#include <fstream>
#include <sstream>
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
        constexpr std::uint32_t kNamePid = 0xFFFFFF01u;  // the event name's format only: no event is made for it

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

        /// This Windows has WARP for the encoder: d3d10warp.dll in System32 and an IDXGIFactory4 (EnumWarpAdapter's
        /// interface). a_what says which part is there or missing
        [[nodiscard]] bool WarpExpected(std::string& a_what)
        {
            const HMODULE warp = ::LoadLibraryExW(L"d3d10warp.dll", nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!warp) {
                a_what = fmt::format("no d3d10warp.dll in System32 (Windows error {})", ::GetLastError());
                return false;
            }
            ::FreeLibrary(warp);
            Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
            Microsoft::WRL::ComPtr<IDXGIFactory4> factory4;
            if (FAILED(::CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))) || FAILED(factory.As(&factory4))) {
                a_what = "d3d10warp.dll is in System32 but DXGI has no IDXGIFactory4 (before Windows 10)";
                return false;
            }
            a_what = "d3d10warp.dll in System32 and DXGI 1.4";
            return true;
        }

        [[nodiscard]] MemoryProbe Free(std::uint64_t a_cap, std::uint64_t a_free)
        {
            return [a_cap, a_free]() { return std::optional<MemoryState>(MemoryState{ a_cap - a_free, UINT64_MAX }); };
        }

        /// a_run with std::cout captured: what it printed
        template <class F>
        [[nodiscard]] std::string Captured(F&& a_run)
        {
            std::ostringstream text;
            auto* const saved = std::cout.rdbuf(text.rdbuf());
            try {
                a_run();
            } catch (...) {
                std::cout.rdbuf(saved);
                throw;
            }
            std::cout.rdbuf(saved);
            return text.str();
        }

        [[nodiscard]] std::size_t Count(const std::string& a_text, std::string_view a_what)
        {
            std::size_t n = 0;
            for (auto at = a_text.find(a_what); at != std::string::npos; at = a_text.find(a_what, at + a_what.size())) {
                ++n;
            }
            return n;
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
                GpuEncoder cpu(GpuUse::kOff, staging, kOutput, kGuardCap, Free(kGuardCap, kGuardCap));
                cpu.Start();
                a_check.Check(!cpu.Usable() && !cpu.ReleaseForMemory(), "gpu markers: a CPU run has no GPU and no device to release");
            }
            a_check.Check(Exists(markers.Disabled()) && ReadAll(markers.Disabled()) == disabledText && Exists(markers.Inflight()),
                "gpu markers: a CPU run leaves gpu.disabled and gpu.inflight as they were");

            // another driver clears gpu.disabled, and with it the marker (its strikes were the old driver's)
            const auto newDriver = markers.OnStart(kAdapterNewDriver, kOutput);
            a_check.Check(newDriver.allowed && newDriver.strikes == 0 && !Exists(markers.Disabled()) && !Exists(markers.Inflight()) &&
                              newDriver.note.find("cleared") != std::string::npos,
                fmt::format("gpu markers: another driver version clears gpu.disabled and gpu.inflight ({})", newDriver.note));
            WriteAll(markers.Disabled(), fmt::format("adapter={}\noutput={}\n", kAdapterA, kOutput));
            const auto newOutput = markers.OnStart(kAdapterA, "2");
            a_check.Check(newOutput.allowed && !Exists(markers.Disabled()), "gpu markers: another output identity clears gpu.disabled");

            WriteAll(markers.Inflight(), "not a number\n");
            const auto garbled = markers.OnStart(kAdapterA, kOutput);
            a_check.Check(garbled.allowed && garbled.strikes == 1, "gpu markers: a marker whose count does not parse counts as one strike");

            // a marker of another adapter or driver: its strikes are dropped, never carried or disabled on
            WriteAll(markers.Inflight(), fmt::format("2\n{}\n", kAdapterNewDriver));
            const auto foreign = markers.OnStart(kAdapterA, kOutput);
            a_check.Check(foreign.allowed && foreign.strikes == 0 && !Exists(markers.Inflight()) && !Exists(markers.Disabled()),
                fmt::format("gpu markers: a strike-2 marker of another driver is dropped, the GPU kept ({})", foreign.note));

            // a malformed gpu.disabled (no adapter= or output= line, an empty one, an output that is no identity) keeps the GPU
            // off and is never deleted, even when its adapter is another one (it would look stale)
            for (const std::string text : { std::string("garbage\n"), fmt::format("adapter=\noutput={}\n", kOutput),
                     fmt::format("adapter={}\noutput=\n", kAdapterNewDriver), fmt::format("adapter={}\noutput=not an identity\n", kAdapterNewDriver) }) {
                WriteAll(markers.Disabled(), text);
                const auto malformed = markers.OnStart(kAdapterA, kOutput);
                a_check.Check(!malformed.allowed && ReadAll(markers.Disabled()) == text && malformed.note.find("malformed") != std::string::npos,
                    fmt::format("gpu markers: a malformed gpu.disabled ('{}') keeps the GPU off and stays ({})", text, malformed.note));
            }
            std::filesystem::remove(markers.Disabled());
        }

        /// The real write (temp file, then a replacing rename) when it fails part way, and a stale gpu.disabled that cannot be
        /// removed: real files, the temp step through the seam only where a real failure after some bytes cannot be made
        void CheckMarkerWrites(Checker& a_check, const std::filesystem::path& a_staging)
        {
            std::filesystem::remove_all(a_staging);
            std::filesystem::create_directories(a_staging);
            const auto disabled = a_staging / kGpuDisabledFile;
            const auto inflight = a_staging / kGpuInflightFile;
            const auto temp = [](const std::filesystem::path& a_file) {
                auto out = a_file;
                out += ".tmp";
                return out;
            };
            // some bytes into the temp file, then a failure: neither marker is a partial file, the temp file is gone
            const auto partial = [](const std::filesystem::path& a_temp, const std::string& a_text, std::string& a_why) {
                WriteAll(a_temp, a_text.substr(0, a_text.size() / 2));
                a_why = "the test's failure after half the bytes";
                return false;
            };
            const auto oldInflight = fmt::format("2\n{}\n", kAdapterA);
            WriteAll(inflight, oldInflight);
            MarkerIo partialIo;
            partialIo.writeTemp = partial;
            GpuMarkers cut(a_staging, partialIo);
            const auto notWritten = cut.OnStart(kAdapterA, kOutput);
            a_check.Check(!notWritten.allowed && !Exists(disabled) && !Exists(temp(disabled)) && ReadAll(inflight) == oldInflight,
                fmt::format("gpu markers write: gpu.disabled cut after half its bytes: no gpu.disabled, no temp file, both strikes kept "
                            "({})", notWritten.note));
            std::string why;
            const bool entered = cut.Enter(0, kAdapterA, why);
            a_check.Check(!entered && ReadAll(inflight) == oldInflight && !Exists(temp(inflight)) && why.find("half") != std::string::npos,
                "gpu markers write: gpu.inflight cut after half its bytes: the call is refused, the marker there is unchanged, no temp file");
            // the rename fails (a folder where the marker goes): the temp file is removed, the folder stays
            std::filesystem::remove(inflight);
            std::filesystem::create_directories(inflight);
            GpuMarkers real(a_staging);
            const bool renamed = real.Enter(0, kAdapterA, why);
            a_check.Check(!renamed && !Exists(temp(inflight)) && std::filesystem::is_directory(inflight) && why.find("replace") != std::string::npos,
                fmt::format("gpu markers write: a rename that fails leaves no temp file ({})", why));
            std::filesystem::remove(inflight);
            // a leftover temp file of a killed write is removed at the next start, never read
            WriteAll(temp(disabled), "adapter=");
            const auto clean = real.OnStart(kAdapterA, kOutput);
            a_check.Check(clean.allowed && !Exists(temp(disabled)) && !Exists(disabled), "gpu markers write: a start removes a leftover gpu.disabled.tmp");
            // real writes: whole files
            a_check.Check(real.Enter(0, kAdapterA, why) && ReadAll(inflight) == fmt::format("1\n{}\n", kAdapterA) && !Exists(temp(inflight)),
                "gpu markers write: a real write leaves the whole marker and no temp file");
            real.Leave();

            // a stale gpu.disabled that cannot be removed (open without delete sharing): the GPU off for this run, nothing
            // else touched; once it can be removed, the next start clears it
            const auto stale = fmt::format("adapter={}\noutput={}\n", kAdapterNewDriver, kOutput);
            WriteAll(disabled, stale);
            WriteAll(inflight, fmt::format("1\n{}\n", kAdapterNewDriver));
            const HANDLE held = ::CreateFileW(disabled.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            const auto kept = real.OnStart(kAdapterA, kOutput);
            if (held != INVALID_HANDLE_VALUE) {
                ::CloseHandle(held);
            }
            a_check.Check(held != INVALID_HANDLE_VALUE && !kept.allowed && kept.strikes == 0 && ReadAll(disabled) == stale && Exists(inflight) &&
                              kept.note.find("could not be removed") != std::string::npos,
                fmt::format("gpu markers write: a stale gpu.disabled that cannot be removed keeps the GPU off this run ({})", kept.note));
            const auto cleared = real.OnStart(kAdapterA, kOutput);
            a_check.Check(cleared.allowed && !Exists(disabled) && !Exists(inflight),
                "gpu markers write: the next start, the file removable again, clears it and the GPU is tried");
        }

        /// The marker file I/O failures, through the seam (GpuMarkers' MarkerIo); removal stays the real one, so a file a
        /// failing read must not delete is a real file the test looks for afterwards
        void CheckMarkerIoFailures(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto staging = a_dir / "gpu-markers-io";
            std::filesystem::remove_all(staging);
            std::filesystem::create_directories(staging);
            const auto disabled = staging / kGpuDisabledFile;
            const auto inflight = staging / kGpuInflightFile;
            const auto unreadable = [](const std::filesystem::path& a_which) {
                return [a_which](const std::filesystem::path& a_file) {
                    if (a_file == a_which) {
                        return MarkerText{ MarkerRead::kUnreadable, {}, "the test's read failure" };
                    }
                    MarkerText out;
                    std::error_code ec;
                    if (std::filesystem::exists(a_file, ec)) {
                        out.state = MarkerRead::kRead;
                        std::ifstream in(a_file, std::ios::binary);
                        out.text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                    }
                    return out;
                };
            };

            // gpu.disabled that does not read: off, kept (a stale-looking content underneath is not looked at)
            WriteAll(disabled, fmt::format("adapter={}\noutput={}\n", kAdapterNewDriver, kOutput));
            WriteAll(inflight, fmt::format("1\n{}\n", kAdapterA));
            GpuMarkers readFails(staging, MarkerIo{ unreadable(disabled), {} });
            const auto offRead = readFails.OnStart(kAdapterA, kOutput);
            a_check.Check(!offRead.allowed && Exists(disabled) && Exists(inflight) && offRead.note.find("does not read") != std::string::npos,
                fmt::format("gpu markers io: a gpu.disabled that does not read keeps the GPU off and deletes nothing ({})", offRead.note));
            std::filesystem::remove(disabled);

            // gpu.inflight that does not read: one strike, left in place
            GpuMarkers inflightFails(staging, MarkerIo{ unreadable(inflight), {} });
            const auto struck = inflightFails.OnStart(kAdapterA, kOutput);
            a_check.Check(struck.allowed && struck.strikes == 1 && ReadAll(inflight) == fmt::format("1\n{}\n", kAdapterA),
                fmt::format("gpu markers io: a gpu.inflight that does not read is one strike and stays as it was ({})", struck.note));

            // strike 2 and gpu.disabled cannot be written: off for this run, the marker kept, so the next start disables
            WriteAll(inflight, fmt::format("2\n{}\n", kAdapterA));
            const auto failWrite = [disabled](const std::filesystem::path& a_file, const std::string&, std::string& a_why) {
                a_why = a_file == disabled ? "the test's write failure" : "unexpected write";
                return false;
            };
            GpuMarkers writeFails(staging, MarkerIo{ {}, failWrite });
            const auto notWritten = writeFails.OnStart(kAdapterA, kOutput);
            a_check.Check(!notWritten.allowed && !Exists(disabled) && ReadAll(inflight) == fmt::format("2\n{}\n", kAdapterA),
                fmt::format("gpu markers io: gpu.disabled not written: the GPU off, gpu.inflight kept with both strikes ({})", notWritten.note));
            GpuMarkers real(staging);
            const auto next = real.OnStart(kAdapterA, kOutput);
            a_check.Check(!next.allowed && Exists(disabled) && !Exists(inflight),
                "gpu markers io: the next start still sees two strikes and writes gpu.disabled");
            std::string why;
            GpuMarkers enterFails(staging, MarkerIo{ {}, failWrite });
            a_check.Check(!enterFails.Enter(0, kAdapterA, why) && !why.empty(), "gpu markers io: a marker that cannot be written refuses the call");
            CheckMarkerWrites(a_check, staging);
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

        [[nodiscard]] DirectX::ScratchImage CpuEncode(const DirectX::ScratchImage& a_chain, DXGI_FORMAT a_format = DXGI_FORMAT_BC7_UNORM)
        {
            DirectX::ScratchImage out;
            (void)DirectX::Compress(a_chain.GetImages(), a_chain.GetImageCount(), a_chain.GetMetadata(), a_format,
                static_cast<DirectX::TEX_COMPRESS_FLAGS>(DirectX::TEX_COMPRESS_DEFAULT | DirectX::TEX_COMPRESS_BC7_QUICK),
                DirectX::TEX_THRESHOLD_DEFAULT, out);
            return out;
        }

        void CheckOutputLimits(Checker& a_check)
        {
            const auto grey = Ramp(false);
            auto encoded = CpuEncode(grey);
            const auto good = CheckGpuOutput(grey, encoded, DXGI_FORMAT_BC7_UNORM);
            a_check.Check(good.ok && good.worstPsnr >= kGpuMinPsnrDb && good.maxAlphaDiff <= kGpuMaxAlphaDiff,
                fmt::format("gpu check: a good BC7 of a ramp passes (worst {:.1f} dB at subresource {}, alpha difference {})",
                    good.worstPsnr, good.worstImage, good.maxAlphaDiff));
            const auto otherTarget = CheckGpuOutput(grey, encoded, DXGI_FORMAT_BC7_UNORM_SRGB);
            const auto bc3 = CheckGpuOutput(grey, CpuEncode(grey, DXGI_FORMAT_BC3_UNORM), DXGI_FORMAT_BC7_UNORM);
            a_check.Check(!otherTarget.ok && otherTarget.why.find("format differs") != std::string::npos && !bc3.ok &&
                              bc3.why.find("format differs") != std::string::npos,
                fmt::format("gpu check: a result in another format than the target fails like another shape ({}; {})", otherTarget.why,
                    bc3.why));

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

            const auto opaque = CpuEncode(Ramp(true));
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

            // short of commit (a pause): no cache here, so the device goes before any wait
            constexpr std::uint64_t kReserve = 50;
            bool pauseReleased = false;
            int sleeps = 0;
            Budget pause(kCap, kReserve, nullptr, nullptr,
                [&]() { return std::optional<MemoryState>(MemoryState{ 10, pauseReleased ? 1000u : kReserve }); },
                [&](unsigned) { ++sleeps; });
            pause.SetDeviceRelease([&]() {
                pauseReleased = true;
                return true;
            });
            std::string pauseWhy;
            a_check.Check(pause.Acquire(20, pauseWhy) == Admission::kAdmit && pause.DeviceReleases() == 1 && sleeps == 0 && pause.Pauses() == 0,
                "gpu budget: short of commit, the device is released before any wait and the texture admitted");
        }

        void CheckThrottle(Checker& a_check)
        {
            // this process's own pid: a concurrent self-test (another worktree's build) has another, so another event
            const auto pid = static_cast<std::uint32_t>(::GetCurrentProcessId());
            ThrottleSignal absent;
            a_check.Check(!absent.Open(pid) && !absent.IsSet(), "gpu throttle: no event from the parent: never throttled");
            const auto name = BlackFlame::BuilderSignal::ThrottleEventName(pid);
            HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, name.c_str());
            ThrottleSignal signal;
            const bool opened = event && signal.Open(pid);
            const bool before = signal.IsSet();
            if (event) {
                ::SetEvent(event);
            }
            const bool after = signal.IsSet();
            a_check.Check(opened && !before && after, "gpu throttle: the parent's event opens by its pid's name, reads unset, then set once the DLL sets it");
            if (event) {
                ::CloseHandle(event);
            }
            a_check.Check(BlackFlame::BuilderSignal::ThrottleEventName(kNamePid) == L"Local\\HoMBlackFlame_Throttle_4294967041",
                "gpu throttle: the event name is Local\\HoMBlackFlame_Throttle_<pid in decimal>");
        }

        /// The wiring on WARP: RecolourDds hands a BC7 target to a usable encoder, which makes its device and encodes (or
        /// refuses the result, which is reached too). FAIL: usable and not reached, or WARP's device not made on a Windows
        /// that has WARP
        void CheckWiring(Checker& a_check, const std::filesystem::path& a_staging)
        {
            std::filesystem::remove_all(a_staging);
            std::filesystem::create_directories(a_staging);
            const auto source = a_staging / "wiring.dds";
            if (!WriteTestTexture(source, DXGI_FORMAT_BC7_UNORM, true)) {
                a_check.Check(false, fmt::format("gpu wiring: {} not written", PathText(source)));
                return;
            }
            GpuEncoder wired(GpuUse::kTestWarp, a_staging, kOutput, kGuardCap, Free(kGuardCap, kGuardCap));
            (void)Captured([&]() { wired.Start(); });
            const bool usable = wired.Usable();
            RecolourResult onGpu;
            const auto printed = Captured([&]() { onGpu = RecolourDds(source, a_staging / "wiring.out.dds", 1.0f, &wired); });
            const bool reached = onGpu.gpu || !onGpu.gpuNote.empty();
            if (usable && !reached && !wired.DeviceMade() && !wired.Usable()) {
                std::string what;
                const bool expected = WarpExpected(what);
                if (expected) {
                    a_check.Check(false, fmt::format("gpu wiring: WARP's device was not made on a Windows that has WARP ({}): {}", what, printed));
                } else {
                    std::cout << fmt::format("INFO gpu wiring: WARP's device was not made ({}): not run\n", what);
                }
                return;
            }
            a_check.Check(usable && reached && wired.DeviceMade() && onGpu.outcome == RecolourOutcome::kRecoloured,
                fmt::format("gpu wiring: RecolourDds hands a BC7 target to the usable encoder, which makes its device and encodes it "
                            "(usable {}, reached {}, device made {}, on the GPU {}; {})", usable, reached, wired.DeviceMade(), onGpu.gpu,
                    onGpu.gpuNote.empty() ? onGpu.reason : onGpu.gpuNote));
        }

        /// The encoder's own state machine on the WARP adapter (GpuUse::kTestWarp): every machine has one
        void CheckEncoderState(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto staging = a_dir / "gpu-warp";
            std::filesystem::remove_all(staging);
            std::filesystem::create_directories(staging);
            const auto grey = Ramp(false);
            const auto plenty = Free(kGuardCap, kGuardCap);
            std::string identity;
            {
                GpuEncoder probe(GpuUse::kTestWarp, staging, kOutput, kGuardCap, plenty);
                (void)Captured([&]() { probe.Start(); });
                identity = probe.Adapter();
            }
            if (identity.empty()) {
                std::string what;
                if (WarpExpected(what)) {
                    a_check.Check(false, fmt::format("gpu encoder: this Windows has WARP ({}) but the encoder found no WARP adapter (see the "
                                                     "GPU line): the encoder's state and wiring checks cannot run", what));
                } else {
                    std::cout << fmt::format("INFO gpu encoder: no WARP on this machine ({}): the encoder's state and wiring checks are not "
                                             "run\n", what);
                }
                return;
            }
            CheckWiring(a_check, staging / "wiring");

            // Start carries the strike it found into the run (m_strikes), and Encode's own marker builds on it
            WriteAll(staging / kGpuInflightFile, fmt::format("1\n{}\n", identity));
            {
                GpuEncoder carried(GpuUse::kTestWarp, staging, kOutput, kGuardCap, plenty);
                (void)Captured([&]() { carried.Start(); });
                DirectX::ScratchImage out;
                std::string why;
                bool encoded = false;
                (void)Captured([&]() { encoded = carried.Encode(grey, DXGI_FORMAT_BC7_UNORM, out, why); });
                a_check.Check(carried.Strikes() == 1 && carried.Usable() && encoded && !Exists(staging / kGpuInflightFile),
                    fmt::format("gpu encoder: Start carries the marker's strike ({}) and the GPU stays usable; the call's marker is gone after it",
                        carried.Strikes()));
                carried.NoteResult(false);
                a_check.Check(carried.Encoded() == 0 && carried.Rejected() == 1,
                    "gpu encoder: a result the output check refuses is not counted as a GPU encode");
            }

            // headroom: the device's share alone is no longer enough, the texture's encoded chain is counted too
            {
                GpuEncoder tight(GpuUse::kTestWarp, staging, kOutput, kGuardCap, Free(kGuardCap, kGpuDeviceShareBytes));
                (void)Captured([&]() { tight.Start(); });
                DirectX::ScratchImage out;
                std::string why;
                bool first = true;
                bool second = true;
                const auto printed = Captured([&]() {
                    first = tight.Encode(grey, DXGI_FORMAT_BC7_UNORM, out, why);
                    second = tight.Encode(grey, DXGI_FORMAT_BC7_UNORM, out, why);
                });
                a_check.Check(!first && !second && !tight.DeviceMade() && tight.HeadroomNoted() && why.empty() && Count(printed, "no device yet") == 1,
                    "gpu encoder: free memory of exactly the device's share (no room for the encoded chain): no device, noted once");
                GpuEncoder roomy(GpuUse::kTestWarp, staging, kOutput, kGuardCap, Free(kGuardCap, kGpuDeviceShareBytes + kBytesPerMiB));
                (void)Captured([&]() { roomy.Start(); });
                bool made = false;
                (void)Captured([&]() { made = roomy.Encode(grey, DXGI_FORMAT_BC7_UNORM, out, why); });
                a_check.Check(made && roomy.DeviceMade() && !roomy.HeadroomNoted(), "gpu encoder: the share plus room for the chain: the device is made");
            }

            // a release for memory: made again once, then not again
            {
                GpuEncoder gpu(GpuUse::kTestWarp, staging, kOutput, kGuardCap, plenty);
                (void)Captured([&]() { gpu.Start(); });
                DirectX::ScratchImage out;
                std::string why;
                bool firstEncode = false;
                bool firstRelease = false;
                bool secondEncode = false;
                bool secondRelease = false;
                bool thirdEncode = true;
                const auto printed = Captured([&]() {
                    firstEncode = gpu.Encode(grey, DXGI_FORMAT_BC7_UNORM, out, why);
                    firstRelease = gpu.ReleaseForMemory();
                    secondEncode = gpu.Encode(grey, DXGI_FORMAT_BC7_UNORM, out, why);
                    secondRelease = gpu.ReleaseForMemory();
                    thirdEncode = gpu.Encode(grey, DXGI_FORMAT_BC7_UNORM, out, why);
                });
                a_check.Check(firstEncode && firstRelease && secondEncode && gpu.Devices() == kGpuMaxDevicesPerRun && secondRelease && !thirdEncode &&
                                  !gpu.DeviceHeld() && !gpu.Usable() && Count(printed, "not made again") == 1,
                    fmt::format("gpu encoder: after a release for memory the device is made again once ({} devices), after the second "
                                "release not again (logged)", gpu.Devices()));
            }

            // the throttle: never made again
            {
                GpuEncoder gpu(GpuUse::kTestWarp, staging, kOutput, kGuardCap, plenty);
                (void)Captured([&]() { gpu.Start(); });
                DirectX::ScratchImage out;
                std::string why;
                bool before = false;
                bool after = true;
                (void)Captured([&]() {
                    before = gpu.Encode(grey, DXGI_FORMAT_BC7_UNORM, out, why);
                    gpu.Throttle();
                    after = gpu.Encode(grey, DXGI_FORMAT_BC7_UNORM, out, why);
                });
                a_check.Check(before && !after && gpu.Devices() == 1 && !gpu.DeviceHeld() && gpu.Stopped(),
                    "gpu encoder: after the throttle the device is released and never made again");
            }

            // the summary's words: "disabled" only with an adapter whose gpu.disabled holds; "no adapter" without one
            WriteAll(staging / kGpuDisabledFile, fmt::format("adapter={}\noutput={}\n", identity, kOutput));
            {
                GpuEncoder disabled(GpuUse::kTestWarp, staging, kOutput, kGuardCap, plenty);
                (void)Captured([&]() { disabled.Start(); });
                GpuEncoder notStarted(GpuUse::kAuto, staging, kOutput, kGuardCap, plenty);  // what Start leaves when no adapter is found
                const auto off = disabled.SummaryLine();
                const auto none = notStarted.SummaryLine();
                a_check.Check(!disabled.Usable() && off.find("; disabled") != std::string::npos && none.find("no adapter") != std::string::npos &&
                                  none.find("disabled") == std::string::npos,
                    fmt::format("gpu encoder: the summary says disabled for an adapter under gpu.disabled, no adapter otherwise ('{}'; '{}')", off,
                        none));
            }
            a_check.Check(!Exists(staging / kGpuInflightFile), "gpu encoder: no crash marker is left after the calls returned");
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
            GpuEncoder gpu(GpuUse::kAuto, dir / "staging", kOutput, kGuardCap, Free(kGuardCap, kGuardCap));
            gpu.Start();
            if (!gpu.AdapterFound()) {
                std::cout << "INFO gpu guard 5: no hardware adapter here (see the GPU line above): not a failure\n";
                return;
            }
            for (const auto& fixture : fixtures) {
                auto source = a_goldenDir.empty() ? std::filesystem::path() : a_goldenDir / Golden::kInputFolder / fixture.golden;
                if (source.empty() || !Exists(source)) {  // a tester package has no fixtures: the self-test's own orange texture
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
                a_check.Check(cpu.outcome == RecolourOutcome::kRecoloured && onGpu.outcome == RecolourOutcome::kRecoloured && !cpu.gpu,
                    fmt::format("gpu guard 5: {} is recoloured with and without the GPU encoder{}", fixture.name,
                        cpu.reason.empty() && onGpu.reason.empty() ? std::string() : fmt::format(" ({}; {})", cpu.reason, onGpu.reason)));
                if (!onGpu.gpu) {
                    if (!gpu.DeviceMade()) {
                        // the documented CPU fallback (an FL 10.x iGPU, no headroom, the GPU off): the wiring is checked on WARP
                        std::cout << fmt::format("INFO gpu guard 5: {}: a hardware adapter is here but gave no device (see the GPU lines "
                                                 "above): encoded on the CPU, the documented fallback\n", fixture.name);
                    } else if (onGpu.gpuNote.empty() && gpu.Usable()) {
                        a_check.Check(false, fmt::format("gpu guard 5: {}: the GPU is usable but the encoder was not tried (wiring)", fixture.name));
                    } else {
                        std::cout << fmt::format("INFO gpu guard 5: {} encoded on the CPU, the documented fallback: {}\n", fixture.name,
                            onGpu.gpuNote.empty() ? std::string("the GPU was stopped earlier this run") : onGpu.gpuNote);
                    }
                    continue;
                }
                const auto cpuImage = Load(cpuOut);
                const auto gpuImage = Load(gpuOut);
                a_check.Check(cpuImage && gpuImage && gpuImage->GetMetadata().format == fixture.format &&
                                  gpuImage->GetMetadata().mipLevels == cpuImage->GetMetadata().mipLevels && onGpu.bytes == cpu.bytes,
                    fmt::format("gpu guard 5: {} on the GPU is {} with {} mips and {} bytes like the CPU's ({})", fixture.name, onGpu.format,
                        onGpu.mips, onGpu.bytes, onGpu.gpuNote));
                if (!cpuImage || !gpuImage) {
                    continue;
                }
                DirectX::ScratchImage reference;
                const bool decoded = SUCCEEDED(DirectX::Decompress(cpuImage->GetImages(), cpuImage->GetImageCount(), cpuImage->GetMetadata(),
                    DirectX::IsSRGB(fixture.format) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, reference));
                const auto check = decoded ? CheckGpuOutput(reference, *gpuImage, fixture.format) : GpuOutputCheck{};
                if (check.ok) {
                    a_check.Check(true, fmt::format("gpu guard 5: {} on the GPU is within the check's limits of the CPU's (worst {:.1f} dB, "
                                                    "alpha difference {})", fixture.name, check.worstPsnr, check.maxAlphaDiff));
                } else {
                    std::cout << fmt::format("INFO gpu guard 5: {} on the GPU against the CPU's result is over a limit on this GPU ({}): "
                                             "quality, not a regression of the build\n", fixture.name, decoded ? check.why : "the CPU's copy does not decode");
                }
            }
            a_check.Check(!Exists(dir / "staging" / kGpuInflightFile) && !Exists(dir / "staging" / kGpuDisabledFile),
                "gpu guard 5: no crash marker is left after the calls returned");
            std::cout << gpu.SummaryLine() << "\n";
        }
    }

    bool WarpAvailable(std::string& a_what)
    {
        return WarpExpected(a_what);
    }

    void CheckGpu(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_goldenDir)
    {
        try {
            CheckMarkers(a_check, a_dir);
            CheckMarkerIoFailures(a_check, a_dir);
            CheckOutputLimits(a_check);
            CheckOverCapRetry(a_check);
            CheckThrottle(a_check);
            CheckEncoderState(a_check, a_dir);
            CheckGuardFive(a_check, a_dir, a_goldenDir);
        } catch (const std::exception& e) {
            a_check.Check(false, fmt::format("gpu: exception: {}", e.what()));
        }
    }
}
