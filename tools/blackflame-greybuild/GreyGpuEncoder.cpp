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

        [[nodiscard]] std::optional<AdapterInfo> Describe(ComPtr<IDXGIAdapter1> a_adapter)
        {
            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(a_adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
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

        /// The first hardware adapter by high-performance preference (IDXGIFactory6), else in DXGI's own order
        [[nodiscard]] std::optional<AdapterInfo> FindAdapter(std::string& a_why)
        {
            ComPtr<IDXGIFactory1> factory;
            if (const HRESULT hr = ::CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())); FAILED(hr)) {
                a_why = fmt::format("no DXGI factory: {}", HrText(hr));
                return std::nullopt;
            }
            ComPtr<IDXGIFactory6> factory6;
            if (SUCCEEDED(factory.As(&factory6))) {
                ComPtr<IDXGIAdapter1> adapter;
                for (UINT i = 0; factory6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                     IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf())) != DXGI_ERROR_NOT_FOUND;
                     ++i) {
                    if (adapter) {
                        if (auto info = Describe(adapter)) {
                            return info;
                        }
                    }
                }
            }
            ComPtr<IDXGIAdapter1> adapter;
            for (UINT i = 0; factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i) {
                if (auto info = Describe(adapter)) {
                    return info;
                }
            }
            a_why = "no hardware adapter (only software ones, or none)";
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::string> ReadText(const std::filesystem::path& a_file)
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(a_file, ec)) {
                return std::nullopt;
            }
            std::ifstream in(a_file, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        [[nodiscard]] bool WriteText(const std::filesystem::path& a_file, const std::string& a_text, std::string& a_why)
        {
            std::ofstream out(a_file, std::ios::binary | std::ios::trunc);
            out << a_text;
            out.close();
            if (!out) {
                a_why = fmt::format("{} could not be written", PathText(a_file));
                return false;
            }
            return true;
        }

        /// The value after a_key on its own line of a_text, "" when there is none
        [[nodiscard]] std::string Field(const std::string& a_text, const char* a_key)
        {
            const std::string key = std::string("\n") + a_key;
            const std::string text = "\n" + a_text;
            const auto at = text.find(key);
            if (at == std::string::npos) {
                return {};
            }
            const auto start = at + key.size();
            const auto end = text.find('\n', start);
            return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
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

    GpuOutputCheck CheckGpuOutput(const DirectX::ScratchImage& a_reference, const DirectX::ScratchImage& a_encoded)
    {
        GpuOutputCheck check;
        const auto& want = a_reference.GetMetadata();
        const auto& got = a_encoded.GetMetadata();
        const DXGI_FORMAT work = want.format;
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

    GpuMarkers::GpuMarkers(std::filesystem::path a_staging) :
        m_staging(std::move(a_staging))
    {}

    GpuMarkers::Start GpuMarkers::OnStart(const std::string& a_adapter, const std::string& a_output)
    {
        Start start;
        std::error_code ec;
        if (const auto disabled = ReadText(Disabled())) {
            const std::string adapter = Field(*disabled, kAdapterKey);
            const std::string output = Field(*disabled, kOutputKey);
            if (adapter == a_adapter && output == a_output) {
                start.allowed = false;
                start.note = fmt::format("{} holds for this adapter and output identity ({}; {}): BC7 on the CPU", kGpuDisabledFile,
                    adapter, output);
                std::filesystem::remove(Inflight(), ec);
                return start;
            }
            std::filesystem::remove(Disabled(), ec);
            start.note = fmt::format("{} was written for another {} ('{}', output '{}'; now '{}', '{}'): cleared, the GPU is tried "
                                     "again", kGpuDisabledFile, adapter != a_adapter ? "adapter or driver" : "output identity", adapter,
                output, a_adapter, a_output);
        }
        const auto inflight = ReadText(Inflight());
        if (!inflight) {
            return start;
        }
        std::filesystem::remove(Inflight(), ec);  // this run writes its own, with the strike carried
        // "<strikes>\n<adapter>\n"; an unreadable count is one strike (the file is there, so a call did not return)
        int found = 1;
        try {
            found = std::max(1, std::stoi(*inflight));
        } catch (const std::exception&) {
            found = 1;
        }
        const auto firstEnd = inflight->find('\n');
        const auto secondEnd = firstEnd == std::string::npos ? std::string::npos : inflight->find('\n', firstEnd + 1);
        const std::string inCall = firstEnd == std::string::npos ? std::string() :
                                       inflight->substr(firstEnd + 1, secondEnd == std::string::npos ? std::string::npos : secondEnd - firstEnd - 1);
        if (!start.note.empty()) {
            start.note += "; ";
        }
        if (found >= kGpuDisableStrikes) {
            std::string why;
            const std::string named = inCall.empty() ? a_adapter : inCall;
            if (!WriteText(Disabled(), fmt::format("{}{}\n{}{}\n", kAdapterKey, named, kOutputKey, a_output), why)) {
                start.note += why + " (the GPU is off for this run all the same)";
            }
            start.allowed = false;
            start.note += fmt::format("{} found at {} starts in a row (the process ended inside a GPU call on {}): {} written, BC7 "
                                      "on the CPU until the adapter, its driver or the output identity changes", kGpuInflightFile, found,
                named, kGpuDisabledFile);
            return start;
        }
        start.strikes = found;
        start.note += fmt::format("{} found (the last run ended inside a GPU call on {}; a game exit can do that): the GPU is "
                                  "kept, {} more such start(s) turn it off", kGpuInflightFile, inCall.empty() ? "an unnamed adapter" : inCall,
            kGpuDisableStrikes - found);
        return start;
    }

    bool GpuMarkers::Enter(int a_strikes, const std::string& a_adapter, std::string& a_why)
    {
        return WriteText(Inflight(), fmt::format("{}\n{}\n", a_strikes + 1, a_adapter), a_why);
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
        auto adapter = FindAdapter(why);
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
        if (m_devices == 1) {
            std::cout << fmt::format("GPU   device made (feature level 11_0){}\n", SUCCEEDED(priority) ?
                fmt::format(", GPU thread priority {}", kGpuThreadPriority) :
                fmt::format("; the GPU thread priority was not lowered: {}", HrText(priority)));
        }
        return true;
    }

    bool GpuEncoder::Encode(const DirectX::ScratchImage& a_grey, unsigned a_dxgiFormat, DirectX::ScratchImage& a_out, std::string& a_why)
    {
        const auto format = static_cast<DXGI_FORMAT>(a_dxgiFormat);
        if (!Usable() || !m_device || (format != DXGI_FORMAT_BC7_UNORM && format != DXGI_FORMAT_BC7_UNORM_SRGB)) {
            return false;
        }
        if (!m_device->device) {
            const auto state = m_probe ? m_probe() : std::nullopt;
            if (!state || state->inUse > m_cap || m_cap - state->inUse < kGpuDeviceShareBytes) {
                if (!m_admissionNoted) {
                    m_admissionNoted = true;
                    std::cout << fmt::format("GPU   no device yet: {} under the job memory cap, the device takes about {} MiB: BC7 "
                                             "on the CPU until there is room (noted once)\n", state ? fmt::format("{} MiB free",
                                             state->inUse > m_cap ? 0 : (m_cap - state->inUse) / kBytesPerMiB) :
                                             std::string("the memory in use is not measurable, so nothing is known"),
                        kGpuDeviceShareBytes / kBytesPerMiB);
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
        m_markers.Leave();
        if (FAILED(hr)) {
            a_why = fmt::format("the GPU encode failed: {}", HrText(hr));
            a_out.Release();
            Stop(a_why);
            return false;
        }
        ++m_encoded;
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
        if (!m_device || !m_device->device) {
            return false;
        }
        Release();
        ++m_releases;
        std::cout << "GPU   over the job memory cap: the device is released (made again when there is room)\n";
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
        return fmt::format("gpu    {}: {} BC7 texture(s) encoded on the GPU, {} refused by the output check (encoded on the CPU); "
                           "devices made {}, released for memory {}{}", m_adapterLine.empty() ? std::string("no adapter") : m_adapterLine,
            m_encoded, m_rejected, m_devices, m_releases, m_stopped ? "; stopped: " + m_stopWhy : (m_allowed ? "" : "; disabled"));
    }
}
