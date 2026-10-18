// =============================================================================
// GreyGpuEncoder - BC7 on the GPU with its guards (GreyGpuEncoder.h)
// =============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include "GreyGpuEncoder.h"

#include "GreyBuild.h"

#include "../../plugins/BlackFlame/src/CopyContractKeys.h"

#include <windows.h>

// d3d11.h before DirectXTex.h: DirectXTex declares its GPU overloads only when it sees __d3d11_h__
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <DirectXTex.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <fmt/format.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <system_error>

namespace GreyBuild
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        constexpr double kByteMax = 255.0;
        constexpr int kChannels = 4;
        constexpr unsigned kDriverFieldBits = 16;
        constexpr unsigned kDriverFieldMask = 0xFFFF;
        constexpr const char* kAdapterKey = "adapter=";
        constexpr const char* kOutputKey = "output=";

        [[nodiscard]] std::string HrText(HRESULT a_result)
        {
            return fmt::format("HRESULT 0x{:08X} ({})", static_cast<std::uint32_t>(a_result), std::system_category().message(a_result));
        }

        [[nodiscard]] std::string Utf8(const wchar_t* a_text)
        {
            const int length = ::WideCharToMultiByte(CP_UTF8, 0, a_text, -1, nullptr, 0, nullptr, nullptr);
            if (length <= 1) {
                return {};
            }
            std::string out(static_cast<std::size_t>(length - 1), '\0');
            ::WideCharToMultiByte(CP_UTF8, 0, a_text, -1, out.data(), length, nullptr, nullptr);
            return out;
        }

        struct AdapterInfo
        {
            ComPtr<IDXGIAdapter1> adapter;
            std::string identity;  // what the markers compare: description, ids, driver version
            std::string line;      // what is logged: the identity, the LUID and the dedicated memory
        };

        [[nodiscard]] std::string DriverVersion(IDXGIAdapter1* a_adapter)
        {
            LARGE_INTEGER umd{};
            if (const HRESULT hr = a_adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd); FAILED(hr)) {
                return fmt::format("unknown ({})", HrText(hr));
            }
            const auto high = static_cast<std::uint32_t>(umd.HighPart);
            const auto low = static_cast<std::uint32_t>(umd.LowPart);
            return fmt::format("{}.{}.{}.{}", high >> kDriverFieldBits, high & kDriverFieldMask, low >> kDriverFieldBits, low & kDriverFieldMask);
        }

        /// a_software: a software adapter is taken too (the self-test's WARP only)
        [[nodiscard]] std::optional<AdapterInfo> Describe(ComPtr<IDXGIAdapter1> a_adapter, bool a_software = false)
        {
            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(a_adapter->GetDesc1(&desc)) || ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 && !a_software)) {
                return std::nullopt;
            }
            AdapterInfo info;
            info.identity = fmt::format("{} (vendor 0x{:04X}, device 0x{:04X}, driver {})", Utf8(desc.Description), desc.VendorId,
                desc.DeviceId, DriverVersion(a_adapter.Get()));
            info.line = fmt::format("{}, LUID {:08X}:{:08X}, dedicated memory {} MiB", info.identity,
                static_cast<std::uint32_t>(desc.AdapterLuid.HighPart), desc.AdapterLuid.LowPart,
                desc.DedicatedVideoMemory / kBytesPerMiB);
            info.adapter = std::move(a_adapter);
            return info;
        }

        /// The self-test's software adapter (GpuUse::kTestWarp): IDXGIFactory4::EnumWarpAdapter
        [[nodiscard]] std::optional<AdapterInfo> FindWarpAdapter(IDXGIFactory1* a_factory, std::string& a_why)
        {
            ComPtr<IDXGIFactory4> factory4;
            ComPtr<IDXGIAdapter1> adapter;
            HRESULT hr = a_factory->QueryInterface(IID_PPV_ARGS(factory4.GetAddressOf()));
            if (SUCCEEDED(hr)) {
                hr = factory4->EnumWarpAdapter(IID_PPV_ARGS(adapter.GetAddressOf()));
            }
            if (FAILED(hr)) {
                a_why = fmt::format("no WARP adapter: {}", HrText(hr));
                return std::nullopt;
            }
            auto info = Describe(adapter, true);
            if (!info) {
                a_why = "the WARP adapter does not describe itself";
            }
            return info;
        }

        /// The first hardware adapter by high-performance preference (IDXGIFactory6), else in DXGI's own order. Each list
        /// ends at its first failed call: DXGI_ERROR_NOT_FOUND past the last adapter, or any other failure
        [[nodiscard]] std::optional<AdapterInfo> FindAdapter(bool a_warp, std::string& a_why)
        {
            ComPtr<IDXGIFactory1> factory;
            if (const HRESULT hr = ::CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())); FAILED(hr)) {
                a_why = fmt::format("no DXGI factory: {}", HrText(hr));
                return std::nullopt;
            }
            if (a_warp) {
                return FindWarpAdapter(factory.Get(), a_why);
            }
            ComPtr<IDXGIFactory6> factory6;
            if (SUCCEEDED(factory.As(&factory6))) {
                ComPtr<IDXGIAdapter1> adapter;
                for (UINT i = 0; SUCCEEDED(factory6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                     IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf())));
                     ++i) {
                    if (adapter) {
                        if (auto info = Describe(adapter)) {
                            return info;
                        }
                    }
                }
            }
            ComPtr<IDXGIAdapter1> adapter;
            HRESULT ended = S_OK;
            for (UINT i = 0; SUCCEEDED(ended = factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf())); ++i) {
                if (adapter) {
                    if (auto info = Describe(adapter)) {
                        return info;
                    }
                }
            }
            a_why = ended == DXGI_ERROR_NOT_FOUND ? std::string("no hardware adapter (only software ones, or none)") :
                                                    fmt::format("no hardware adapter: the adapter list ended with {}", HrText(ended));
            return std::nullopt;
        }

        [[nodiscard]] MarkerText ReadMarkerFile(const std::filesystem::path& a_file)
        {
            MarkerText out;
            std::error_code ec;
            const auto status = std::filesystem::status(a_file, ec);
            if (status.type() == std::filesystem::file_type::not_found) {
                return out;  // kMissing
            }
            out.state = MarkerRead::kUnreadable;
            if (ec) {
                out.why = fmt::format("its state does not read: {}", ec.message());
                return out;
            }
            if (!std::filesystem::is_regular_file(status)) {
                out.why = "it is not a file";
                return out;
            }
            std::ifstream in(a_file, std::ios::binary);
            if (!in.is_open()) {
                out.why = "it does not open";
                return out;
            }
            out.text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            if (in.bad()) {
                out.text.clear();
                out.why = "reading it failed";
                return out;
            }
            out.state = MarkerRead::kRead;
            return out;
        }

        [[nodiscard]] std::filesystem::path MarkerTemp(const std::filesystem::path& a_file)
        {
            auto temp = a_file;
            temp += ".tmp";
            return temp;
        }

        /// a_text into a_temp (made anew) and flushed to the disk: a GPU call that takes the machine down still leaves the
        /// marker it was written before
        [[nodiscard]] bool WriteMarkerTemp(const std::filesystem::path& a_temp, const std::string& a_text, std::string& a_why)
        {
            const HANDLE file = ::CreateFileW(a_temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                a_why = fmt::format("{} could not be made (Windows error {})", PathText(a_temp), ::GetLastError());
                return false;
            }
            DWORD written = 0;
            const bool ok = ::WriteFile(file, a_text.data(), static_cast<DWORD>(a_text.size()), &written, nullptr) &&
                            written == a_text.size() && ::FlushFileBuffers(file);
            const DWORD error = ok ? ERROR_SUCCESS : ::GetLastError();
            ::CloseHandle(file);
            if (!ok) {
                a_why = fmt::format("{} could not be written ({} of {} bytes, Windows error {})", PathText(a_temp), written, a_text.size(), error);
            }
            return ok;
        }

        /// A marker written whole or not at all: <file>.tmp (a_writeTemp: the seam, else WriteMarkerTemp), then renamed over
        /// <file> (MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH). Either step failing removes the temp file and leaves
        /// <file> as it was
        [[nodiscard]] bool WriteMarkerFile(const std::filesystem::path& a_file, const std::string& a_text, const MarkerIo::WriteTemp& a_writeTemp,
            std::string& a_why)
        {
            const auto temp = MarkerTemp(a_file);
            bool ok = a_writeTemp ? a_writeTemp(temp, a_text, a_why) : WriteMarkerTemp(temp, a_text, a_why);
            if (ok && !::MoveFileExW(temp.c_str(), a_file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                a_why = fmt::format("{} could not replace {} (Windows error {})", PathText(temp), PathText(a_file), ::GetLastError());
                ok = false;
            }
            if (!ok) {
                std::error_code ec;
                std::filesystem::remove(temp, ec);
            }
            return ok;
        }

        /// The value after a_key on its own line of a_text; none when no line starts with a_key
        [[nodiscard]] std::optional<std::string> Field(const std::string& a_text, const char* a_key)
        {
            const std::string key = std::string("\n") + a_key;
            const std::string text = "\n" + a_text;
            const auto at = text.find(key);
            if (at == std::string::npos) {
                return std::nullopt;
            }
            const auto start = at + key.size();
            const auto end = text.find('\n', start);
            return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        }

        [[nodiscard]] std::uint64_t SatAdd(std::uint64_t a_a, std::uint64_t a_b) noexcept
        {
            return a_b > UINT64_MAX - a_a ? UINT64_MAX : a_a + a_b;
        }

        /// The bytes a_grey's chain takes in a_format, every subresource (UINT64_MAX when a pitch does not compute)
        [[nodiscard]] std::uint64_t EncodedChainBytes(const DirectX::ScratchImage& a_grey, DXGI_FORMAT a_format) noexcept
        {
            std::uint64_t total = 0;
            for (std::size_t i = 0; i < a_grey.GetImageCount(); ++i) {
                const DirectX::Image& image = a_grey.GetImages()[i];
                std::size_t row = 0;
                std::size_t slice = 0;
                if (FAILED(DirectX::ComputePitch(a_format, image.width, image.height, row, slice))) {
                    return UINT64_MAX;
                }
                total = SatAdd(total, slice);
            }
            return total;
        }
    }

    GpuUse GpuUseFromEnvironment(GpuUse a_default) noexcept
    {
        wchar_t value[8]{};
        const DWORD length = ::GetEnvironmentVariableW(kGpuEnvironmentVariable, value, static_cast<DWORD>(std::size(value)));
        if (length == 3 && std::towlower(value[0]) == L'o' && std::towlower(value[1]) == L'f' && std::towlower(value[2]) == L'f') {
            return GpuUse::kOff;
        }
        return a_default;
    }

    GpuOutputCheck CheckGpuOutput(const DirectX::ScratchImage& a_reference, const DirectX::ScratchImage& a_encoded, unsigned a_target)
    {
        GpuOutputCheck check;
        const auto& want = a_reference.GetMetadata();
        const auto& got = a_encoded.GetMetadata();
        const DXGI_FORMAT work = want.format;
        if (a_target != 0 && static_cast<unsigned>(got.format) != a_target) {
            check.why = fmt::format("the format differs (encoded {}, the target {})", static_cast<int>(got.format), a_target);
            return check;
        }
        if ((work != DXGI_FORMAT_R8G8B8A8_UNORM && work != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) || !DirectX::IsCompressed(got.format) ||
            got.width != want.width || got.height != want.height || got.mipLevels != want.mipLevels || got.arraySize != want.arraySize ||
            got.depth != want.depth || a_encoded.GetImageCount() != a_reference.GetImageCount()) {
            check.why = fmt::format("the shapes differ (grey {} x {}, {} mips, {} items, format {}; encoded {} x {}, {} mips, {} items, format {})",
                want.width, want.height, want.mipLevels, want.arraySize, static_cast<int>(want.format), got.width, got.height, got.mipLevels,
                got.arraySize, static_cast<int>(got.format));
            return check;
        }
        double worstMse = 0.0;
        for (std::size_t i = 0; i < a_reference.GetImageCount(); ++i) {
            const DirectX::Image& ref = a_reference.GetImages()[i];
            DirectX::ScratchImage decoded;  // one subresource at a time: never a second decoded chain
            if (const HRESULT hr = DirectX::Decompress(a_encoded.GetImages()[i], work, decoded); FAILED(hr)) {
                check.why = fmt::format("subresource {} does not decode: {}", i, HrText(hr));
                return check;
            }
            const DirectX::Image& out = *decoded.GetImage(0, 0, 0);
            double sum = 0.0;
            for (std::size_t y = 0; y < ref.height; ++y) {
                const std::uint8_t* a = ref.pixels + y * ref.rowPitch;
                const std::uint8_t* b = out.pixels + y * out.rowPitch;
                for (std::size_t x = 0; x < ref.width * kChannels; ++x) {
                    const int diff = static_cast<int>(a[x]) - static_cast<int>(b[x]);
                    sum += static_cast<double>(diff * diff);
                    if (x % kChannels == kChannels - 1) {
                        check.maxAlphaDiff = std::max(check.maxAlphaDiff, static_cast<unsigned>(std::abs(diff)));
                    }
                }
            }
            const double texels = static_cast<double>(ref.width * ref.height * kChannels);
            const double mse = texels > 0.0 ? sum / (texels * kByteMax * kByteMax) : 0.0;
            if (i == 0 || mse > worstMse) {
                worstMse = mse;
                check.worstImage = i;
            }
        }
        check.worstPsnr = worstMse > 0.0 ? 10.0 * std::log10(1.0 / worstMse) : std::numeric_limits<double>::infinity();
        const bool psnrOk = check.worstPsnr >= kGpuMinPsnrDb;
        const bool alphaOk = check.maxAlphaDiff <= kGpuMaxAlphaDiff;
        check.ok = psnrOk && alphaOk;
        if (!psnrOk) {
            check.why = fmt::format("subresource {} at {:.1f} dB, under {:.1f} dB", check.worstImage, check.worstPsnr, kGpuMinPsnrDb);
        } else if (!alphaOk) {
            check.why = fmt::format("an alpha difference of {} steps, over {}", check.maxAlphaDiff, kGpuMaxAlphaDiff);
        }
        return check;
    }

    GpuMarkers::GpuMarkers(std::filesystem::path a_staging, MarkerIo a_io) :
        m_staging(std::move(a_staging)), m_io(std::move(a_io))
    {}

    MarkerText GpuMarkers::Read(const std::filesystem::path& a_file) const
    {
        return m_io.read ? m_io.read(a_file) : ReadMarkerFile(a_file);
    }

    bool GpuMarkers::Write(const std::filesystem::path& a_file, const std::string& a_text, std::string& a_why) const
    {
        return m_io.write ? m_io.write(a_file, a_text, a_why) : WriteMarkerFile(a_file, a_text, m_io.writeTemp, a_why);
    }

    GpuMarkers::Start GpuMarkers::OnStart(const std::string& a_adapter, const std::string& a_output)
    {
        Start start;
        std::error_code ec;
        for (const auto& marker : { Disabled(), Inflight() }) {
            std::error_code tempEc;
            std::filesystem::remove(MarkerTemp(marker), tempEc);  // a write killed before its rename: never read
        }
        const auto disabled = Read(Disabled());
        if (disabled.state == MarkerRead::kUnreadable) {
            // never deleted: it may hold the very adapter this start has, and a file that does not read cannot say
            start.allowed = false;
            start.note = fmt::format("{} is there but does not read ({}): BC7 on the CPU, the file is left as it is (delete it to "
                                     "try the GPU again)", kGpuDisabledFile, disabled.why);
            return start;
        }
        if (disabled.state == MarkerRead::kRead) {
            const auto adapter = Field(disabled.text, kAdapterKey);
            const auto output = Field(disabled.text, kOutputKey);
            // stale only when it is whole: both fields there, neither empty, the output a valid identity; anything else is
            // malformed (a hand edit, another tool) and says nothing about which adapter it was for
            if (!adapter || !output || adapter->empty() || output->empty() || !BlackFlame::CopyContract::OutputIdentityVersion(*output)) {
                start.allowed = false;
                start.note = fmt::format("{} is malformed (it needs a non-empty {} line and an {} line with a valid output identity): "
                                         "BC7 on the CPU, the file is left as it is (delete it to try the GPU again)", kGpuDisabledFile,
                    kAdapterKey, kOutputKey);
                return start;
            }
            if (*adapter == a_adapter && *output == a_output) {
                start.allowed = false;
                start.note = fmt::format("{} holds for this adapter and output identity ({}; {}): BC7 on the CPU", kGpuDisabledFile,
                    *adapter, *output);
                std::filesystem::remove(Inflight(), ec);  // gpu.disabled is written: the strikes are kept by it
                return start;
            }
            // another adapter, driver or output: stale, cleared with any marker (whose strikes were counted for that one)
            const char* const what = *adapter != a_adapter ? "adapter or driver" : "output identity";
            std::filesystem::remove(Disabled(), ec);
            if (ec) {
                // still there: this run stays on the CPU (no zero-strike start past a file that may yet hold); tried again
                // at the next start
                start.allowed = false;
                start.note = fmt::format("{} was written for another {} ('{}', output '{}'; now '{}', '{}') but could not be removed "
                                         "({}): BC7 on the CPU for this run, it is tried again at the next start", kGpuDisabledFile, what,
                    *adapter, *output, a_adapter, a_output, ec.message());
                return start;
            }
            std::error_code inflightEc;
            std::filesystem::remove(Inflight(), inflightEc);
            start.note = fmt::format("{} was written for another {} ('{}', output '{}'; now '{}', '{}'): cleared with any {}, the GPU is "
                                     "tried again", kGpuDisabledFile, what, *adapter, *output, a_adapter, a_output, kGpuInflightFile);
            return start;
        }
        const auto inflight = Read(Inflight());
        if (inflight.state == MarkerRead::kMissing) {
            return start;
        }
        if (inflight.state == MarkerRead::kUnreadable) {
            // one strike, as a garbled count; left in place, so a later start that can read it still has its count (this
            // run's own marker overwrites it with strike 2 when it can write it)
            start.strikes = 1;
            start.note = fmt::format("{} is there but does not read ({}): counted as one strike and left in place; the GPU is kept, "
                                     "{} more such start(s) turn it off", kGpuInflightFile, inflight.why, kGpuDisableStrikes - 1);
            return start;
        }
        // "<strikes>\n<adapter>\n"; an unparseable count is one strike (the file is there, so a call did not return)
        int found = 1;
        try {
            found = std::max(1, std::stoi(inflight.text));
        } catch (const std::exception&) {
            found = 1;
        }
        const auto firstEnd = inflight.text.find('\n');
        const auto secondEnd = firstEnd == std::string::npos ? std::string::npos : inflight.text.find('\n', firstEnd + 1);
        const std::string inCall = firstEnd == std::string::npos ? std::string() :
                                       inflight.text.substr(firstEnd + 1, secondEnd == std::string::npos ? std::string::npos : secondEnd - firstEnd - 1);
        if (!inCall.empty() && inCall != a_adapter) {
            std::filesystem::remove(Inflight(), ec);
            start.note = fmt::format("{} was written for another adapter or driver ('{}'; now '{}'): its {} strike(s) are dropped, the "
                                     "GPU is kept", kGpuInflightFile, inCall, a_adapter, found);
            return start;
        }
        if (found >= kGpuDisableStrikes) {
            std::string why;
            start.allowed = false;
            if (!Write(Disabled(), fmt::format("{}{}\n{}{}\n", kAdapterKey, a_adapter, kOutputKey, a_output), why)) {
                // the marker stays: the next start sees these strikes again and tries the write again
                start.note = fmt::format("{} found at {} starts in a row (the process ended inside a GPU call on {}), but {}: BC7 on "
                                         "the CPU for this run, {} is kept for the next start", kGpuInflightFile, found, a_adapter, why,
                    kGpuInflightFile);
                return start;
            }
            std::filesystem::remove(Inflight(), ec);
            start.note = fmt::format("{} found at {} starts in a row (the process ended inside a GPU call on {}): {} written, BC7 "
                                     "on the CPU until the adapter, its driver or the output identity changes", kGpuInflightFile, found,
                a_adapter, kGpuDisabledFile);
            return start;
        }
        std::filesystem::remove(Inflight(), ec);  // this run writes its own, with the strike carried
        start.strikes = found;
        start.note = fmt::format("{} found (the last run ended inside a GPU call on {}; a game exit can do that): the GPU is "
                                 "kept, {} more such start(s) turn it off", kGpuInflightFile, inCall.empty() ? "an unnamed adapter" : inCall,
            kGpuDisableStrikes - found);
        return start;
    }

    bool GpuMarkers::Enter(int a_strikes, const std::string& a_adapter, std::string& a_why)
    {
        return Write(Inflight(), fmt::format("{}\n{}\n", a_strikes + 1, a_adapter), a_why);
    }

    void GpuMarkers::Leave() noexcept
    {
        std::error_code ec;
        std::filesystem::remove(Inflight(), ec);
    }

    struct GpuEncoder::Device
    {
        ComPtr<IDXGIAdapter1> adapter;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
    };

    GpuEncoder::GpuEncoder(GpuUse a_use, std::filesystem::path a_staging, std::string a_outputIdentity, std::uint64_t a_cap,
        MemoryProbe a_probe) :
        m_use(a_use), m_markers(std::move(a_staging)), m_output(std::move(a_outputIdentity)), m_cap(a_cap), m_probe(std::move(a_probe))
    {}

    GpuEncoder::~GpuEncoder()
    {
        Release();
    }

    void GpuEncoder::Start()
    {
        if (m_use == GpuUse::kOff) {
            std::cout << "GPU   off for this run: BC7 is encoded on the CPU (no adapter, device or marker)\n";
            return;
        }
        std::string why;
        auto adapter = FindAdapter(m_use == GpuUse::kTestWarp, why);
        if (!adapter) {
            std::cout << fmt::format("GPU   {}: BC7 is encoded on the CPU\n", why);
            return;
        }
        m_device = std::make_unique<Device>();
        m_device->adapter = adapter->adapter;
        m_adapter = adapter->identity;
        m_adapterLine = adapter->line;
        std::cout << fmt::format("GPU   adapter {}\n", m_adapterLine);
        const auto start = m_markers.OnStart(m_adapter, m_output);
        if (!start.note.empty()) {
            std::cout << "GPU   " << start.note << "\n";
        }
        m_allowed = start.allowed;
        m_strikes = start.strikes;
    }

    bool GpuEncoder::MakeDevice()
    {
        std::string why;
        if (!m_markers.Enter(m_strikes, m_adapter, why)) {
            Stop(why + ": no crash marker, so no device");
            return false;
        }
        const D3D_FEATURE_LEVEL wanted = D3D_FEATURE_LEVEL_11_0;
        D3D_FEATURE_LEVEL got{};
        const HRESULT hr = ::D3D11CreateDevice(m_device->adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &wanted, 1, D3D11_SDK_VERSION,
            m_device->device.ReleaseAndGetAddressOf(), &got, m_device->context.ReleaseAndGetAddressOf());
        m_markers.Leave();
        if (FAILED(hr)) {
            Stop(fmt::format("D3D11CreateDevice (feature level 11_0) failed: {}", HrText(hr)));
            return false;
        }
        ++m_devices;
        ComPtr<IDXGIDevice> dxgi;
        HRESULT priority = m_device->device.As(&dxgi);
        if (SUCCEEDED(priority)) {
            priority = dxgi->SetGPUThreadPriority(kGpuThreadPriority);
        }
        std::cout << fmt::format("GPU   device made{} (feature level 11_0){}\n", m_devices == 1 ? "" : " again (once a run at most)",
            SUCCEEDED(priority) ?
            fmt::format(", GPU thread priority {}", kGpuThreadPriority) :
            fmt::format("; the GPU thread priority was not lowered: {}", HrText(priority)));
        return true;
    }

    bool GpuEncoder::DeviceHeld() const noexcept
    {
        return m_device && m_device->device;
    }

    bool GpuEncoder::Encode(const DirectX::ScratchImage& a_grey, unsigned a_dxgiFormat, DirectX::ScratchImage& a_out, std::string& a_why)
    {
        const auto format = static_cast<DXGI_FORMAT>(a_dxgiFormat);
        if (!Usable() || !m_device || (format != DXGI_FORMAT_BC7_UNORM && format != DXGI_FORMAT_BC7_UNORM_SRGB)) {
            return false;
        }
        if (!m_device->device) {
            // the device's share and what this texture still allocates (its encoded chain): the rest of the texture is
            // already in PrivateUsage
            const std::uint64_t need = SatAdd(kGpuDeviceShareBytes, EncodedChainBytes(a_grey, format));
            const auto state = m_probe ? m_probe() : std::nullopt;
            if (!state || state->inUse > m_cap || m_cap - state->inUse < need) {
                if (!m_admissionNoted) {
                    m_admissionNoted = true;
                    std::cout << fmt::format("GPU   no device yet: {} under the job memory cap, the device and this texture's encoded "
                                             "chain take about {} MiB: BC7 on the CPU until there is room (noted once)\n", state ?
                                             fmt::format("{} MiB free", state->inUse > m_cap ? 0 : (m_cap - state->inUse) / kBytesPerMiB) :
                                             std::string("the memory in use is not measurable, so nothing is known"),
                        need / kBytesPerMiB);
                }
                return false;
            }
            if (!MakeDevice()) {
                return false;
            }
        }
        std::string why;
        if (!m_markers.Enter(m_strikes, m_adapter, why)) {
            Stop(why + ": no crash marker, so no GPU call");
            return false;
        }
        const auto flags = static_cast<DirectX::TEX_COMPRESS_FLAGS>(DirectX::TEX_COMPRESS_DEFAULT | DirectX::TEX_COMPRESS_BC7_QUICK);
        const HRESULT hr = DirectX::Compress(m_device->device.Get(), a_grey.GetImages(), a_grey.GetImageCount(), a_grey.GetMetadata(),
            format, flags, kGpuAlphaWeight, a_out);
        // DirectXTex's buffers for this call are freed at the flush (deferred destruction), before the output check and the
        // next memory probe; inside the marker, as GPU work
        m_device->context->ClearState();
        m_device->context->Flush();
        m_markers.Leave();
        if (FAILED(hr)) {
            a_why = fmt::format("the GPU encode failed: {}", HrText(hr));
            a_out.Release();
            Stop(a_why);
            return false;
        }
        return true;
    }

    void GpuEncoder::Release() noexcept
    {
        if (m_device && m_device->device) {
            if (m_device->context) {
                m_device->context->ClearState();  // deferred destruction: freed at the flush, not at the last reference
                m_device->context->Flush();
            }
            m_device->context.Reset();
            m_device->device.Reset();
        }
    }

    void GpuEncoder::Stop(const std::string& a_why)
    {
        Release();
        if (!m_stopped) {
            m_stopped = true;
            m_stopWhy = a_why;
            std::cout << fmt::format("GPU   {}: the device is released, the rest of the run encodes BC7 on the CPU\n", a_why);
        }
    }

    bool GpuEncoder::ReleaseForMemory()
    {
        if (!DeviceHeld()) {
            return false;
        }
        Release();
        ++m_releases;
        if (m_devices >= kGpuMaxDevicesPerRun) {
            m_stopped = true;
            m_stopWhy = fmt::format("released for memory after {} devices this run (at most {} a run): not made again", m_devices,
                kGpuMaxDevicesPerRun);
            std::cout << fmt::format("GPU   over the job memory cap: the device is released; it was made {} times this run, so it is "
                                     "not made again and the rest of the run encodes BC7 on the CPU\n", m_devices);
        } else {
            std::cout << "GPU   over the job memory cap: the device is released (made again once this run, when there is room)\n";
        }
        return true;
    }

    void GpuEncoder::Throttle()
    {
        Release();
        if (!m_stopped) {
            m_stopped = true;
            m_stopWhy = "throttled (the publish window closed)";
        }
    }

    std::string GpuEncoder::SummaryLine() const
    {
        if (m_use == GpuUse::kOff) {
            return "gpu    off for this run";
        }
        std::string state;
        if (m_stopped) {
            state = "; stopped: " + m_stopWhy;
        } else if (!m_adapter.empty() && !m_allowed) {
            state = "; disabled";
        }
        return fmt::format("gpu    {}: {} BC7 texture(s) encoded on the GPU, {} refused by the output check (encoded on the CPU); "
                           "devices made {}, released for memory {}{}", m_adapterLine.empty() ? std::string("no adapter") : m_adapterLine,
            m_encoded, m_rejected, m_devices, m_releases, state);
    }
}
