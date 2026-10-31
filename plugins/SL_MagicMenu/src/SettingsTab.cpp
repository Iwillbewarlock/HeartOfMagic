#include "Common.h"

#include "SettingsTab.h"

#include "BlockHiding.h"
#include "HiddenBlocks.h"
#include "LangFiles.h"
#include "LangGame.h"
#include "Widget.h"
#include "WidgetStrings.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The vendored header pulls in <windows.h> and trips a few MSVC warnings in its own code (as in Spell Research Prisma, whose
// SettingsTab.cpp this follows). No ImGui library is linked: the header resolves every call from SKSEMenuFramework.dll.
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#pragma warning(push)
#pragma warning(disable : 4099 5054 4996)
#include "SKSEMenuFramework.h"
#pragma warning(pop)
// <windows.h> may already have come in without NOMINMAX (through the PCH)
#undef min
#undef max

namespace SettingsTab
{
    namespace
    {
        using WidgetText::Str;
        using WidgetText::Text;

        constexpr const char* kSection = "Magic Menu Note";  // the mod's name: not translated
        constexpr ImVec4 kWarnColor{ 1.0f, 0.4f, 0.35f, 1.0f };
        constexpr float kWrapAtWindowEdge = 0.0f;  // PushTextWrapPos(0): wrap at the window's right edge
        // English on purpose (Spell Research Prisma's line): it has to be readable exactly when the Korean glyphs are missing
        constexpr const char* kKoreanFontWarning = "If Korean shows as boxes, set EnableKorean = true in SKSEMenuFramework.ini";

        // Every export the page reaches through the header, exactly the overloads called below. The header calls what
        // GetProcAddress returns without a null check, so a DLL older than one of these would crash on the first draw: Register
        // checks them all first (Spell Research Prisma's gate)
        constexpr std::array<const char*, 12> kExports{ "AddSectionItem", "igBeginCombo", "igCheckbox", "igEndCombo", "igPopID", "igPopTextWrapPos",
            "igPushID_Str", "igPushTextWrapPos", "igSelectable_Bool", "igSeparatorText", "igTextColoredV", "igTextDisabledV" };

        // The language combo: [Display] sLanguage's values. The language names are shown in their own language, not translated
        struct Language
        {
            const char* code;
            const char* name;  // nullptr: the table's auto label
        };
        constexpr std::array<Language, 3> kLanguages{ { { "auto", nullptr }, { "en", "English" }, { "ko", "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4" } } };  // 한국어

        // Taken at Register and again at each magic menu open when the ini changed (game thread), copied by the page (render thread)
        struct Setup
        {
            std::wstring iniPath;              // SL_MagicMenu_User.ini (fix round 1 I2: the tab writes only the player's file)
            std::string autoCode;
            std::string language;              // [Display] sLanguage as last read (LangGame::IniLanguage)
            std::vector<std::string> hidden;   // [Blocks] sHidden as last read (BlockHiding::Setting)
        };
        std::mutex g_setupLock;
        Setup g_setup;
        std::atomic<std::uint64_t> g_setupGeneration{ 1 };  // bumped when a menu open read other values (fix round 1 M2)
        bool g_registered = false;                          // game thread
        std::atomic<bool> g_added{ false };                 // the tab is in SKSE Menu Framework

        // The page's own copy of the two values (render thread only): the setup's, then what the page wrote
        struct Page
        {
            std::uint64_t generation = 0;  // the setup it was copied from (0: none yet)
            Setup values;
        };
        Page g_page;

        // fix round 1 M2: the setup again when a menu open read other values (a hand edit of the ini), so the next tick does not
        // write the page's older copy over it
        void EnsurePage()
        {
            const auto generation = g_setupGeneration.load(std::memory_order_acquire);
            if (g_page.generation == generation) return;
            const std::scoped_lock lock(g_setupLock);
            g_page.values = g_setup;
            g_page.generation = generation;
        }

        // One line of text in the page's width (ImGui's TextWrapped is not used: the wrap position works with TextDisabled too)
        void Note(const char* a_text)
        {
            ImGui::PushTextWrapPos(kWrapAtWindowEdge);
            ImGui::TextDisabled("%s", a_text);
            ImGui::PopTextWrapPos();
        }

        /// One key of SL_MagicMenu_User.ini written by the Win32 call (the readers' absolute path; the file is made when missing): the
        /// other lines stay as they are. True when written; a failure is logged and the page keeps the old value
        bool WriteIni(std::string_view a_section, std::string_view a_key, const std::string& a_value)
        {
            const std::wstring section(a_section.begin(), a_section.end()), key(a_key.begin(), a_key.end());
            const auto value = SKSE::stl::utf8_to_utf16(a_value).value_or(std::wstring{});
            if (::WritePrivateProfileStringW(section.c_str(), key.c_str(), value.c_str(), g_page.values.iniPath.c_str())) {
                logger::info("Settings tab: [{}] {} = '{}' written to SL_MagicMenu_User.ini - applied at the next magic menu open", a_section, a_key, a_value);
                return true;
            }
            logger::warn("Settings tab: [{}] {} = '{}' could NOT be written to SL_MagicMenu_User.ini (Windows error {}) - the file is unchanged; is it read-only?",
                a_section, a_key, a_value, ::GetLastError());
            return false;
        }

        std::string LanguageName(const Language& a_language)
        {
            return a_language.name ? a_language.name : Text(Str::kSetLangAuto);
        }

        void LanguageRow()
        {
            ImGui::SeparatorText(Text(Str::kSetLanguage));
            const auto current = LangFiles::NormalizedSetting(g_page.values.language);
            const auto known = std::ranges::find_if(kLanguages, [&](const Language& a_language) { return current == a_language.code; });
            const auto preview = known != kLanguages.end() ? LanguageName(*known) : WidgetText::Fill(Str::kSetLangOther, { { "{code}", current } });
            if (ImGui::BeginCombo("##language", preview.c_str())) {
                for (const auto& language : kLanguages) {
                    const bool selected = current == language.code;
                    if (ImGui::Selectable(LanguageName(language).c_str(), selected) && !selected &&
                        WriteIni(LangFiles::kIniSection, LangFiles::kIniKey, language.code)) {
                        g_page.values.language = language.code;
                    }
                }
                ImGui::EndCombo();
            }
            Note(Text(Str::kSetLangNote));
            Note(Text(Str::kSetLangTitles));
            // the chosen language's code: auto as this game resolves it (Korean also on an English game with Korean strings)
            const auto chosen = LangFiles::NormalizedSetting(g_page.values.language);
            if ((chosen == LangFiles::kAuto ? g_page.values.autoCode : chosen) == LangFiles::kKorean) ImGui::TextColored(kWarnColor, "%s", kKoreanFontWarning);
        }

        // A block's checkbox label: the built-in ones by the table, an add-on by its title and id (its id alone with no title)
        std::string BlockName(const Widget::BlockLabel& a_block)
        {
            if (a_block.id == Layout::kPreview) return Text(Str::kSetBlockPreview);
            if (a_block.id == Layout::kSpellCard) return Text(Str::kSetBlockSpellCard);
            return a_block.title.empty() ? a_block.id : a_block.title + " (" + a_block.id + ")";
        }

        bool Listed(const std::vector<std::string>& a_ids, const std::string& a_id)
        {
            return !a_id.empty() && std::ranges::find(a_ids, a_id) != a_ids.end();
        }

        void BlockRows()
        {
            ImGui::SeparatorText(Text(Str::kSetBlocks));
            Note(Text(Str::kSetBlocksNote));
            for (const auto& block : Widget::KnownBlocks()) {
                auto& hidden = g_page.values.hidden;
                // hidden by its id or by the retired id it took over (the host reads both, HiddenBlocks::Effective)
                bool shown = !Listed(hidden, block.id) && !Listed(hidden, block.legacyId);
                ImGui::PushID(block.id.c_str());
                if (ImGui::Checkbox(BlockName(block).c_str(), &shown)) {
                    auto next = HiddenBlocks::With(HiddenBlocks::With(hidden, block.legacyId, false), block.id, !shown);
                    if (WriteIni(HiddenBlocks::kSection, HiddenBlocks::kKey, HiddenBlocks::Join(next))) hidden = std::move(next);
                }
                ImGui::PopID();
            }
            Note(Text(Str::kSetBlocksBuiltIn));
        }

        void __stdcall RenderSettings()
        {
            EnsurePage();
            LanguageRow();
            BlockRows();
        }

        // Why the tab cannot be added ("" = it can): not loaded, or too old (an export missing). Fix round 1 M1: the loaded module
        // decides; the file check only words the line (a DLL loaded from elsewhere still gets the tab)
        std::string Refusal()
        {
            const auto module = SKSEMenuFramework_Module();
            if (!module) {
                return SKSEMenuFramework::IsInstalled() ? "SKSEMenuFramework.dll is installed but not loaded (wrong game version or a load error)" :
                                                          "SKSE Menu Framework is not installed";
            }
            std::string missing;
            for (const char* name : kExports) {
                if (!GetProcAddress(module, name)) missing += (missing.empty() ? "" : ", ") + std::string(name);
            }
            return missing.empty() ? std::string{} : "SKSEMenuFramework.dll has no " + missing + " - too old for the tab; update SKSE Menu Framework";
        }
    }

    void Register(const std::string& a_autoCode)
    {
        if (std::exchange(g_registered, true)) return;
        if (const auto why = Refusal(); !why.empty()) {
            logger::info("Settings tab: none - {}; [Display] sLanguage and [Blocks] sHidden in the ini files work as before", why);
            return;
        }
        {
            const std::scoped_lock lock(g_setupLock);
            g_setup = { LangGame::GamePath(LangGame::kUserIniRelative).wstring(), a_autoCode, LangGame::IniLanguage(), BlockHiding::Setting() };
        }
        g_added = true;
        // the names are fixed once added: the item's name is the language of this start (the page's texts follow later changes)
        const std::string item = Text(Str::kSetItem);
        SKSEMenuFramework::SetSection(kSection);
        SKSEMenuFramework::AddSectionItem(item, RenderSettings);
        logger::info("Settings tab: \"{}/{}\" added to SKSE Menu Framework (language and blocks, written to SL_MagicMenu_User.ini; auto is '{}' on "
                     "this game)",
            kSection, item, a_autoCode);
    }

    void OnMenuOpen()
    {
        if (!g_added) return;
        auto language = LangGame::IniLanguage();
        auto hidden = BlockHiding::Setting();
        const std::scoped_lock lock(g_setupLock);
        if (language == g_setup.language && hidden == g_setup.hidden) return;
        g_setup.language = std::move(language);
        g_setup.hidden = std::move(hidden);
        g_setupGeneration.fetch_add(1, std::memory_order_release);
        logger::info("Settings tab: the ini's language or hidden blocks changed outside the page - the page shows them from now on");
    }
}
