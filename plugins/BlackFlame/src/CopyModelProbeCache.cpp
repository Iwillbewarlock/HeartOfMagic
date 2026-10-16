// =============================================================================
// COPY MODEL PROBE CACHE - the probe cache's key and its id text (CopyModelProbeCache.h)
// =============================================================================
// X19: split from CopyModelProbe.cpp (the 600-line limit).

#include "CopyModelProbeCache.h"

#include "CopyRequest.h"
#include "FormCopies.h"
#include "NifDump.h"

#include <format>
#include <fstream>
#include <sstream>

namespace BlackFlame::ProbeCache
{
    namespace
    {
        constexpr std::uint32_t kLightLocalMask = 0xFFF;  // the copies plugin is an ESL
        constexpr int kHexBase = 16;
    }

    std::string MapIdentity()
    {
        std::ifstream in(kCopiesMapPath, std::ios::binary);
        if (!in) {
            return {};
        }
        std::stringstream bytes;
        bytes << in.rdbuf();
        const auto text = bytes.str();
        std::error_code ec;
        const auto written = std::filesystem::last_write_time(kCopiesMapPath, ec);
        const auto version = SKSE::PluginDeclaration::GetSingleton()->GetVersion().string(".");
        return std::format("map fnv64:{:016x} size {} written {} plugin {}", NifDump::Fnv64(text.data(), text.size()), text.size(),
            ec ? 0 : written.time_since_epoch().count(), version);
    }

    std::optional<std::filesystem::path> CachePath()
    {
        const auto folder = RequestFolder();
        return folder ? std::optional(*folder / kCacheName) : std::nullopt;
    }

    std::string LocalHex(RE::FormID a_copy)
    {
        return std::format("{:03X}", a_copy & kLightLocalMask);
    }

    RE::FormID CopyFromLocal(const std::string& a_hex)
    {
        std::uint32_t local = 0;
        try {
            local = static_cast<std::uint32_t>(std::stoul(a_hex, nullptr, kHexBase));
        } catch (const std::exception&) {
            return 0;
        }
        auto* dh = RE::TESDataHandler::GetSingleton();
        return dh ? dh->LookupFormID(local, kCopiesPluginName) : 0;
    }
}
