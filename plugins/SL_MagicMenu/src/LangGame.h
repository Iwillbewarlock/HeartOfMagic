#pragma once

#include "Common.h"

#include "LangFiles.h"

#include <array>
#include <filesystem>
#include <string>
#include <system_error>

// =============================================================================
// LANG GAME - the game side of LangFiles.h, shared by SL_MagicMenu and
// SL_MagicMenu_HoM: [Display] sLanguage from Data/SKSE/Plugins/SL_MagicMenu.ini
// (both DLLs read the same key), the game's sLanguage:General, a localized
// game string for auto's Hangul check (GameText), and one load of a DLL's
// Catalog with its log lines. Call at kDataLoaded (the game's INI settings and
// the plugins' strings are read by then), once per game session.
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

    /// [Display] sLanguage of SL_MagicMenu.ini as written, "" when not set (non-ASCII reads as '?', which is no code)
    inline std::string IniLanguage()
    {
        std::array<wchar_t, kValueChars> buffer{};
        const auto path = GamePath(kIniRelative).wstring();
        const std::wstring section(LangFiles::kIniSection.begin(), LangFiles::kIniSection.end());
        const std::wstring key(LangFiles::kIniKey.begin(), LangFiles::kIniKey.end());
        const auto length = REX::W32::GetPrivateProfileStringW(section.c_str(), key.c_str(), L"", buffer.data(), static_cast<std::uint32_t>(buffer.size()),
            path.c_str());
        std::string text;
        for (std::uint32_t i = 0; i < length; ++i) text.push_back(buffer[i] < 0x80 ? static_cast<char>(buffer[i]) : '?');
        if (const auto comment = text.find_first_of(";#"); comment != std::string::npos) text.resize(comment);
        return text;
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

    /// Loads a_catalog from <game>/<a_langDir>/<code>.json once and logs which language and files were used (and, for auto,
    /// which signal decided)
    template <class Catalog>
    void Load(Catalog& a_catalog, const char* a_langDir)
    {
        const auto dir = GamePath(a_langDir);
        const auto report = a_catalog.Load(dir, IniLanguage(), GameLanguage(), GameText());
        if (!report) return;  // loaded already this session
        const auto shown = SKSE::stl::utf16_to_utf8(dir.wstring()).value_or("(path not UTF-8)");
        for (const auto& [level, line] : LangFiles::ReportLines(*report, shown)) {
            switch (level) {
            case LangFiles::Level::kError: logger::error("{}", line); break;
            case LangFiles::Level::kWarn: logger::warn("{}", line); break;
            default: logger::info("{}", line); break;
            }
        }
    }
}
