#pragma once

#include "Common.h"

#include "LangFiles.h"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// =============================================================================
// LANG GAME - the game side of LangFiles.h, shared by SL_MagicMenu and
// SL_MagicMenu_HoM: [Display] sLanguage from Data/SKSE/Plugins/SL_MagicMenu.ini
// (both DLLs read the same key), the game's sLanguage:General, a localized
// game string for auto's Hangul check (GameText), the load of a DLL's Catalog
// with its log lines, and its refresh. Load at kDataLoaded (the game's INI
// settings and the plugins' strings are read by then); Refresh at each magic
// menu open (game thread): the settings tab writes sLanguage while the game runs.
// =============================================================================

namespace LangGame
{
    inline constexpr const char* kIniRelative = "Data/SKSE/Plugins/SL_MagicMenu.ini";
    inline constexpr std::uint32_t kValueChars = 64;

    /// A path under the game folder (GetPrivateProfileString looks a bare relative name up in the Windows folder)
    inline std::filesystem::path GamePath(const char* a_relative)
    {
        std::error_code ec;
        const auto dir = std::filesystem::current_path(ec);
        return ec ? std::filesystem::path(a_relative) : dir / a_relative;
    }

    /// a_key of a_section in SL_MagicMenu.ini as written (up to a_maxChars), "" when not set; non-ASCII reads as '?' (no code or
    /// id holds it), a ';' or '#' and what follows is a comment
    inline std::string IniString(std::string_view a_section, std::string_view a_key, std::uint32_t a_maxChars = kValueChars)
    {
        std::vector<wchar_t> buffer(a_maxChars + 1, L'\0');
        const auto path = GamePath(kIniRelative).wstring();
        const std::wstring section(a_section.begin(), a_section.end());
        const std::wstring key(a_key.begin(), a_key.end());
        const auto length = REX::W32::GetPrivateProfileStringW(section.c_str(), key.c_str(), L"", buffer.data(), static_cast<std::uint32_t>(buffer.size()),
            path.c_str());
        std::string text;
        for (std::uint32_t i = 0; i < length; ++i) text.push_back(buffer[i] < 0x80 ? static_cast<char>(buffer[i]) : '?');
        if (const auto comment = text.find_first_of(";#"); comment != std::string::npos) text.resize(comment);
        return text;
    }

    /// [Display] sLanguage of SL_MagicMenu.ini as written, "" when not set
    inline std::string IniLanguage()
    {
        return IniString(LangFiles::kIniSection, LangFiles::kIniKey);
    }

    /// Skyrim.ini [General] sLanguage (ENGLISH, KOREAN ...), "" when the game has no such setting
    inline std::string GameLanguage()
    {
        const auto* setting = RE::GetINISetting("sLanguage:General");
        if (!setting || setting->GetType() != RE::Setting::Type::kString || !setting->GetString()) return {};
        return setting->GetString();
    }

    /// The five magic skills' names as the game shows them (the ActorValueInfo full names, localized from Skyrim.esm's strings,
    /// loaded by kDataLoaded), joined by ", " - auto's Hangul check (LangFiles::AutoCode): the Korean patch makes them Korean on
    /// a game whose sLanguage is ENGLISH. "" when none reads
    inline std::string GameText()
    {
        constexpr std::array kSchools{ RE::ActorValue::kAlteration, RE::ActorValue::kConjuration, RE::ActorValue::kDestruction, RE::ActorValue::kIllusion,
            RE::ActorValue::kRestoration };
        std::string text;
        for (const auto school : kSchools) {
            const auto* info = RE::ActorValueList::GetActorValueInfo(school);
            const char* name = info ? info->GetFullName() : nullptr;
            if (name && *name) text += (text.empty() ? "" : ", ") + std::string(name);
        }
        return text;
    }

    namespace Detail
    {
        inline void LogReport(const LangFiles::LoadReport& a_report, const std::filesystem::path& a_dir)
        {
            const auto shown = SKSE::stl::utf16_to_utf8(a_dir.wstring()).value_or("(path not UTF-8)");
            for (const auto& [level, line] : LangFiles::ReportLines(a_report, shown)) {
                switch (level) {
                case LangFiles::Level::kError: logger::error("{}", line); break;
                case LangFiles::Level::kWarn: logger::warn("{}", line); break;
                default: logger::info("{}", line); break;
                }
            }
        }
    }

    /// Loads a_catalog from <game>/<a_langDir>/<code>.json once and logs which language and files were used (and, for auto,
    /// which signal decided)
    template <class Catalog>
    void Load(Catalog& a_catalog, const char* a_langDir)
    {
        const auto dir = GamePath(a_langDir);
        const auto report = a_catalog.Load(dir, IniLanguage(), GameLanguage(), GameText());
        if (report) Detail::LogReport(*report, dir);  // nullopt: loaded already this session
    }

    /// At a magic menu open (game thread): [Display] sLanguage read again; a value other than the one a_catalog was resolved with
    /// resolves it again (logged as Load does). True when the texts changed (the caller sends what it copied out again)
    template <class Catalog>
    bool Refresh(Catalog& a_catalog, const char* a_langDir)
    {
        const auto dir = GamePath(a_langDir);
        const auto setting = IniLanguage();
        const auto report = a_catalog.Refresh(dir, setting, GameLanguage(), GameText());
        if (!report) return false;
        logger::info("Lang: [Display] sLanguage is now '{}' - the texts resolved again at this menu open", setting);
        Detail::LogReport(*report, dir);
        return true;
    }

    /// The code auto chooses on this game (KOREAN, or Hangul in GameText: ko; else en). Game thread (reads game settings and forms)
    inline std::string AutoCode()
    {
        return LangFiles::AutoCode(GameLanguage(), GameText()).first;
    }
}
