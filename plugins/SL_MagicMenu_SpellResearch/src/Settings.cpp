#include "Common.h"

#include "Settings.h"

#include "SrBlockText.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>

namespace Settings
{
    namespace
    {
        constexpr const wchar_t* kIniRelative = L"Data/SKSE/Plugins/SL_MagicMenu_SpellResearch.ini";
        constexpr const char* kSrPrismaIniRelative = "Data/SKSE/Plugins/SpellResearchPrismaUI.ini";
        constexpr std::uintmax_t kMaxIniBytes = 256 * 1024;  // SR Prisma's ini is a few KB; anything far bigger is not read
        constexpr std::uint32_t kValueChars = 16;

        std::atomic<bool> g_selfTest{ false };
        std::atomic<SrBlock::LayoutKind> g_layout{ SrBlock::LayoutKind::kList };

        // An absolute path: GetPrivateProfileString looks a bare relative name up in the Windows folder
        std::filesystem::path GamePath(const std::filesystem::path& a_relative)
        {
            std::error_code ec;
            const auto dir = std::filesystem::current_path(ec);
            if (ec) {
                logger::warn("Settings: no current folder ({}), using the relative path {}", ec.message(), a_relative.string());
                return a_relative;
            }
            return dir / a_relative;
        }

        const char* LayoutName(SrBlock::LayoutKind a_kind)
        {
            return a_kind == SrBlock::LayoutKind::kLine ? "line" : "list";
        }
    }

    void Load()
    {
        const auto path = GamePath(kIniRelative).wstring();
        std::array<wchar_t, kValueChars> buffer{};
        const auto length = REX::W32::GetPrivateProfileStringW(L"General", L"bSelfTest", L"", buffer.data(), static_cast<std::uint32_t>(buffer.size()),
            path.c_str());
        const std::wstring_view value(buffer.data(), length);
        if (value == L"1") {
            g_selfTest = true;
        } else if (!value.empty() && value != L"0") {
            logger::warn("Settings: [General] bSelfTest = '{}' is not 0 or 1 - self-test off", SKSE::stl::utf16_to_utf8(value).value_or("?"));
        }
        logger::info("Settings: {} - bSelfTest {}", SKSE::stl::utf16_to_utf8(path).value_or("(path not UTF-8)"), g_selfTest.load() ? 1 : 0);
    }

    bool SelfTest()
    {
        return g_selfTest.load();
    }

    namespace
    {
        // SpellResearchPrismaUI.ini as text; nullopt (logged with a_what) when it is missing, too big or unreadable
        std::optional<std::string> ReadSrPrismaIni(const char* a_what, bool a_log = true)
        {
            const auto path = GamePath(kSrPrismaIniRelative);
            const auto shown = SKSE::stl::utf16_to_utf8(path.wstring()).value_or("(path not UTF-8)");
            std::error_code ec;
            const auto size = std::filesystem::file_size(path, ec);
            if (ec) {
                if (a_log) logger::info("Settings: {} - {} not read ({})", a_what, shown, ec.message());
                return std::nullopt;
            }
            if (size > kMaxIniBytes) {
                logger::warn("Settings: {} - {} is {} bytes, more than an ini this size ({}): not read", a_what, shown, size, kMaxIniBytes);
                return std::nullopt;
            }
            std::ifstream file(path, std::ios::binary);
            std::stringstream text;
            if (file.is_open()) text << file.rdbuf();
            if (!file.is_open() || file.bad()) {
                logger::warn("Settings: {} - {} could not be read", a_what, shown);
                return std::nullopt;
            }
            return text.str();
        }

        // Skyrim.ini [General] sLanguage (ENGLISH, KOREAN ...), "" when the game has no such setting
        std::string GameLanguage()
        {
            const auto* setting = RE::GetINISetting("sLanguage:General");
            if (!setting || setting->GetType() != RE::Setting::Type::kString || !setting->GetString()) return {};
            return setting->GetString();
        }
    }

    SrBlock::LayoutKind ResolveLayout()
    {
        SrBlock::LayoutRead read;
        if (const auto text = ReadSrPrismaIni("layout list")) {
            read = SrBlock::ParseLayout(*text);
            if (!read.found) {
                logger::info("Settings: layout list - no [{}] {} in SpellResearchPrismaUI.ini", SrBlock::kLayoutSection, SrBlock::kLayoutKey);
            } else if (!read.known) {
                logger::warn("Settings: layout list - [{}] {} = '{}' is not list or line", SrBlock::kLayoutSection, SrBlock::kLayoutKey, read.value);
            } else {
                logger::info("Settings: layout {} ([{}] {})", LayoutName(read.kind), SrBlock::kLayoutSection, SrBlock::kLayoutKey);
            }
        }
        g_layout = read.kind;
        return read.kind;
    }

    SrBlock::LayoutKind CurrentLayout()
    {
        return g_layout.load();
    }

    std::string ResolveLanguage(bool a_log)
    {
        const auto game = GameLanguage();
        const auto text = ReadSrPrismaIni("language from the game", a_log);
        const auto read = SrBlock::ParseLanguage(text.value_or(std::string{}), game);
        if (!a_log) return read.code;
        if (read.fromSetting) {
            logger::info("Settings: language '{}' ([{}] {} in SpellResearchPrismaUI.ini)", read.code, SrBlock::kLanguageSection, SrBlock::kLanguageKey);
        } else {
            logger::info("Settings: language '{}' from the game's sLanguage '{}' ([{}] {} {})", read.code, game, SrBlock::kLanguageSection,
                SrBlock::kLanguageKey, read.found ? std::format("= '{}'", read.value) : std::string("not set"));
        }
        return read.code;
    }
}
