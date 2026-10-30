#include "Common.h"

#include "HomSettings.h"

#include <array>
#include <filesystem>
#include <system_error>

namespace HomSettings
{
    namespace
    {
        constexpr const wchar_t* kIniRelative = L"Data/SKSE/Plugins/SL_MagicMenu.ini";  // the host's ini: the block's keys kept their place
        constexpr std::uint32_t kValueChars = 64;

        Values g_values;

        // An absolute path: GetPrivateProfileString looks a bare relative name up in the Windows folder
        std::wstring IniPath()
        {
            std::error_code ec;
            const auto dir = std::filesystem::current_path(ec);
            if (ec) {
                logger::warn("HomSettings: no current folder ({}), reading SL_MagicMenu.ini by its relative name", ec.message());
                return kIniRelative;
            }
            return (dir / kIniRelative).wstring();
        }

        std::wstring Wide(std::string_view a_text)
        {
            return std::wstring(a_text.begin(), a_text.end());  // section and key names are ASCII
        }

        // The raw text of a key (ASCII values only; another character reads as '?', which no number parses)
        std::optional<std::string> Raw(const std::wstring& a_path, std::string_view a_section, std::string_view a_key)
        {
            std::array<wchar_t, kValueChars> buffer{};
            const auto section = Wide(a_section), key = Wide(a_key);
            const auto length = REX::W32::GetPrivateProfileStringW(section.c_str(), key.c_str(), L"", buffer.data(),
                static_cast<std::uint32_t>(buffer.size()), a_path.c_str());
            if (length == 0) return std::nullopt;
            std::string text;
            for (std::uint32_t i = 0; i < length; ++i) text.push_back(buffer[i] < 0x80 ? static_cast<char>(buffer[i]) : '?');
            return CleanValue(std::move(text));
        }

        void LogRect(const char* a_name, const Rect& a_rect)
        {
            logger::info("HomSettings:   {} x {:.0f}..{:.0f} y {:.0f}..{:.0f}", a_name, a_rect.left, a_rect.right, a_rect.top, a_rect.bottom);
        }
    }

    void Load()
    {
        const auto path = IniPath();
        auto read = Read([&](std::string_view a_section, std::string_view a_key) { return Raw(path, a_section, a_key); });
        for (const auto& warning : read.warnings) logger::warn("HomSettings: {}", warning);
        g_values = read.values;
        const auto& v = g_values;
        logger::info("HomSettings: {} - selfTest {} keyNext {} keySetTarget {}", SKSE::stl::utf16_to_utf8(path).value_or("(path not UTF-8)"), v.selfTest,
            v.keyNext, v.keySetTarget);
        LogRect("tree", v.tree);
        LogRect("progress", v.progress);
        LogRect("node card", v.nodeCard);
        logger::info("HomSettings:   tree scale {} (default {}), mark scale {} (default {}), max steps {}, max nodes {} (default {})", v.treeScale,
            Values{}.treeScale, v.treeMarkScale, Values{}.treeMarkScale, v.maxSteps, v.maxNodes, Values{}.maxNodes);
        if (v.debugExtraHeight > 0) {
            logger::info("HomSettings:   [Debug] fDebugExtraHeight {} - the node card block is that much taller (a scroll test; 0 = off)", v.debugExtraHeight);
        }
    }

    const Values& Get()
    {
        return g_values;
    }
}
