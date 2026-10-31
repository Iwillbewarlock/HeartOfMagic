#include "Common.h"

#include "Settings.h"

#include "Layout.h"

#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <type_traits>

namespace Settings
{
    namespace
    {
        constexpr const wchar_t* kIniRelative = L"Data/SKSE/Plugins/MagicMenuRewrite.ini";
        constexpr const wchar_t* kLayoutRelative = L"Data/SKSE/Plugins/MagicMenuRewrite_Layout.json";
        // a layout file is a few hundred bytes; anything far bigger is not one (read no further)
        constexpr std::uintmax_t kMaxLayoutBytes = 64 * 1024;
        constexpr std::uint32_t kValueChars = 64;
        constexpr std::uint32_t kMaxScanCode = 255;
        // SKSE::InputMap: kMacro_GamepadOffset 266 .. kMaxMacros 282 (exclusive)
        constexpr int kMinGamepadCode = SKSE::InputMap::kMacro_GamepadOffset, kMaxGamepadCode = SKSE::InputMap::kMaxMacros - 1;
        constexpr double kStageMin = -4000.0, kStageMax = 4000.0;  // an ini stage coordinate (ReadRect, fTreeRight)
        // two columns of Layout::kMinColumn (240) and the gap fit in the block area 648..1238 (590): 590 - 2 x 240 = 110
        constexpr double kMaxPanelGap = 110.0;

        Values g_values;
        Layout::State g_layout;
        Layout::State g_defaultLayout;              // Layout::Default with the add-on blocks (AddBlocks)
        std::vector<Layout::BlockId> g_addonIds;   // the add-on blocks, in registration order
        // HoM add-on design G-e: retired id -> the final id of the v3 block that took it over (SetLegacyAliases; empty until the
        // first magic menu open). Every layout read goes through Layout::ToFinal with it, every write through Layout::ToLegacy
        std::map<Layout::BlockId, Layout::BlockId> g_legacyAliases;

        // The built-in blocks: Repair appends any of them a layout file lacks. HoM add-on plan Task 5: not the retired ids (tree,
        // progress, nodecard) - Repair keeps them in place as unknown ids, so an add-on that takes one over finds its place
        std::set<Layout::BlockId> BuiltinBlocks()
        {
            return { Layout::kPreview, Layout::kSpellCard };
        }

        // An absolute path: GetPrivateProfileString looks a bare relative name up in the Windows folder
        std::wstring GameDataPath(const wchar_t* a_relative)
        {
            std::error_code ec;
            auto dir = std::filesystem::current_path(ec);
            if (ec) {
                logger::warn("Settings: no current folder ({}), reading {} by its relative name", ec.message(),
                    SKSE::stl::utf16_to_utf8(a_relative).value_or("?"));
                return a_relative;
            }
            return (dir / a_relative).wstring();
        }

        std::wstring IniPath()
        {
            return GameDataPath(kIniRelative);
        }

        // The raw text of a key, nullopt when the key is missing or empty (ASCII values only)
        std::optional<std::string> Raw(const std::wstring& a_path, const wchar_t* a_section, const wchar_t* a_key)
        {
            std::array<wchar_t, kValueChars> buffer{};
            const auto length = REX::W32::GetPrivateProfileStringW(a_section, a_key, L"", buffer.data(),
                static_cast<std::uint32_t>(buffer.size()), a_path.c_str());
            if (length == 0) return std::nullopt;
            std::string text;
            for (std::uint32_t i = 0; i < length; ++i) {
                text.push_back(buffer[i] < 0x80 ? static_cast<char>(buffer[i]) : '?');
            }
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
            const auto comment = text.find_first_of(";#");
            if (comment != std::string::npos) text.resize(comment);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
            if (text.empty()) return std::nullopt;
            return text;
        }

        template <class T>
        std::optional<T> Parse(const std::string& a_text)
        {
            T value{};
            const auto* end = a_text.data() + a_text.size();
            const auto [ptr, ec] = std::from_chars(a_text.data(), end, value);
            if (ec != std::errc{} || ptr != end) return std::nullopt;
            // from_chars reads "nan" and "inf"; a NaN would pass every range check (all comparisons false)
            if constexpr (std::is_floating_point_v<T>) {
                if (!std::isfinite(value)) return std::nullopt;
            }
            return value;
        }

        void ReadBool(const std::wstring& a_path, const wchar_t* a_section, const wchar_t* a_key, bool& a_out)
        {
            const auto raw = Raw(a_path, a_section, a_key);
            if (!raw) return;
            const auto value = Parse<int>(*raw);
            if (!value || (*value != 0 && *value != 1)) {
                logger::warn("Settings: [{}] {} = '{}' is not 0 or 1 - keeping {}", SKSE::stl::utf16_to_utf8(a_section).value_or("?"),
                    SKSE::stl::utf16_to_utf8(a_key).value_or("?"), *raw, a_out);
                return;
            }
            a_out = *value == 1;
        }

        template <class T>
        void ReadNumber(const std::wstring& a_path, const wchar_t* a_section, const wchar_t* a_key, T& a_out, T a_min, T a_max)
        {
            const auto raw = Raw(a_path, a_section, a_key);
            if (!raw) return;
            const auto value = Parse<T>(*raw);
            if (!value || *value < a_min || *value > a_max) {
                logger::warn("Settings: [{}] {} = '{}' is not a number in {}..{} - keeping {}",
                    SKSE::stl::utf16_to_utf8(a_section).value_or("?"), SKSE::stl::utf16_to_utf8(a_key).value_or("?"), *raw, a_min,
                    a_max, a_out);
                return;
            }
            a_out = *value;
        }

        // Four keys <prefix>Left/Top/Right/Bottom; a rect with no area keeps the default
        void ReadRect(const std::wstring& a_path, const wchar_t* a_prefix, Rect& a_out)
        {
            const std::wstring prefix(a_prefix);
            Rect rect = a_out;
            ReadNumber(a_path, L"Layout", (prefix + L"Left").c_str(), rect.left, kStageMin, kStageMax);
            ReadNumber(a_path, L"Layout", (prefix + L"Top").c_str(), rect.top, kStageMin, kStageMax);
            ReadNumber(a_path, L"Layout", (prefix + L"Right").c_str(), rect.right, kStageMin, kStageMax);
            ReadNumber(a_path, L"Layout", (prefix + L"Bottom").c_str(), rect.bottom, kStageMin, kStageMax);
            if (rect.Width() <= 0 || rect.Height() <= 0) {
                logger::warn("Settings: [Layout] {} rect {},{} .. {},{} has no area - keeping the default",
                    SKSE::stl::utf16_to_utf8(a_prefix).value_or("?"), rect.left, rect.top, rect.right, rect.bottom);
                return;
            }
            a_out = rect;
        }

        void LogRect(const char* a_name, const Rect& a_rect)
        {
            logger::info("Settings:   {} x {:.0f}..{:.0f} y {:.0f}..{:.0f}", a_name, a_rect.left, a_rect.right, a_rect.top, a_rect.bottom);
        }

        std::string Columns(const Layout::State& a_state)
        {
            std::string text;
            for (const auto& column : a_state.columns) {
                text += text.empty() ? "{" : " / {";
                for (std::size_t i = 0; i < column.size(); ++i) text += (i > 0 ? ", " : "") + column[i];
                text += "}";
            }
            return text;
        }

        std::filesystem::path LayoutPath()
        {
            return GameDataPath(kLayoutRelative);
        }

        std::string Shown(const std::filesystem::path& a_path)
        {
            return SKSE::stl::utf16_to_utf8(a_path.wstring()).value_or("(path not UTF-8)");
        }

        std::filesystem::path WithSuffix(const std::filesystem::path& a_path, const wchar_t* a_suffix)
        {
            return std::filesystem::path(a_path.wstring() + a_suffix);
        }

        // The layout file as it is: parsed (not repaired), or nullopt with the file's state and a reason
        struct FileRead
        {
            std::optional<Layout::State> state;
            Layout::FileState file = Layout::FileState::kMissing;
            std::string why;
        };

        FileRead ReadLayout(const std::filesystem::path& a_path)
        {
            FileRead read;
            std::error_code ec;
            const bool exists = std::filesystem::exists(a_path, ec);
            if (ec) {
                // Task 2 review: an exists() error is not "no file" - a save must not overwrite what could not be checked
                read.file = Layout::FileState::kBroken;
                read.why = std::format("could not be checked ({})", ec.message());
                return read;
            }
            if (!exists) {
                read.why = "not found";
                return read;
            }
            read.file = Layout::FileState::kBroken;
            const auto bytes = std::filesystem::file_size(a_path, ec);
            if (ec || bytes > kMaxLayoutBytes) {
                read.why = ec ? std::format("has no readable size ({})", ec.message()) : std::format("is {} bytes, more than {}", bytes, kMaxLayoutBytes);
                return read;
            }
            std::ifstream file(a_path, std::ios::binary);
            std::string text(static_cast<std::size_t>(bytes), char{});
            if (!file || !file.read(text.data(), static_cast<std::streamsize>(text.size()))) {
                read.why = "could not be read";
                return read;
            }
            bool newer = false;
            read.state = Layout::FromJson(text, &read.why, &newer);
            if (read.state) {
                read.state = Layout::ToFinal(std::move(*read.state), g_legacyAliases);
                read.file = Layout::FileState::kRead;
            } else {
                read.file = newer ? Layout::FileState::kNewer : Layout::FileState::kBroken;
                read.why = (newer ? "is from a newer build: " : "is not a layout: ") + read.why;
            }
            return read;
        }

        Layout::FileState g_fileState = Layout::FileState::kMissing;
        bool g_badKept = false;  // the unreadable file was copied to .bad this game session (D10)

        // blocks plan Task 2: the layout file, read once at load. Missing, unreadable, not a layout or from a newer build: the
        // default layout (one log line each; the file's state is kept for SaveLayout's plan, D10); otherwise repaired against the
        // default
        Layout::State LoadLayout(const Values& a_values)
        {
            const auto fallback = Layout::Default(a_values);
            const auto known = BuiltinBlocks();
            const auto path = LayoutPath();
            auto read = ReadLayout(path);
            g_fileState = read.file;
            // final review: a save that lost the file leaves only its .bak; run 3 review: so does a file left empty or garbled
            // (a power loss mid-write). The .bak is read then; the state stays as read (kMissing: the next save creates the
            // file; kBroken: it is kept as .bad first), the .bak is left as it is
            const auto bak = WithSuffix(path, L".bak");
            std::error_code bakError;
            const bool bakThere = !read.state && std::filesystem::exists(bak, bakError) && !bakError;
            const bool missing = read.file == Layout::FileState::kMissing;
            if (Layout::ReadBackup(read.file, bakThere)) {
                const auto what = missing ? std::string("not found") : read.why;
                auto backup = ReadLayout(bak);
                if (backup.state) {
                    logger::warn("Settings: layout {} {} - read {} instead; the layout mode's next save writes {}{}", Shown(path), what, Shown(bak),
                        Shown(path), missing ? "" : " (the unreadable file kept as .bad first)");
                    return Layout::Repair(*backup.state, known, fallback);
                }
                logger::warn("Settings: layout {} {}, and {} {} - the default layout", Shown(path), what, Shown(bak), backup.why);
                return fallback;
            }
            if (missing) {
                logger::info("Settings: layout {} not found - the default layout", Shown(path));
                return fallback;
            }
            if (!read.state) {
                logger::warn("Settings: layout {} {} - the default layout{}", Shown(path), read.why,
                    read.file == Layout::FileState::kNewer ? " (the file is left as it is: the layout mode will not save over it)" :
                                                             " (kept as .bad before the layout mode saves over it)");
                return fallback;
            }
            return Layout::Repair(*read.state, known, fallback);
        }

        // Win32 file moves (kernel32; REX::W32 has none of them), declared here so this file needs no <Windows.h>. The types are
        // the SDK's (BOOL = int, DWORD = unsigned long, LPCWSTR, LPVOID)
        extern "C" __declspec(dllimport) int __stdcall MoveFileExW(const wchar_t* a_existing, const wchar_t* a_new, unsigned long a_flags);
        extern "C" __declspec(dllimport) int __stdcall CopyFileW(const wchar_t* a_existing, const wchar_t* a_new, int a_failIfExists);
        constexpr int kCopyOverwrite = 0;                                               // CopyFileW bFailIfExists FALSE
        constexpr unsigned long kMoveReplaceExisting = 0x1, kMoveWriteThrough = 0x8;  // MOVEFILE_REPLACE_EXISTING, MOVEFILE_WRITE_THROUGH

        // The JSON written to a_tmp; every step checked (std::ofstream: flush() is the stream's, not FlushFileBuffers - D10 asks for
        // each step to be checked, not for a disk sync)
        bool WriteTemp(const std::filesystem::path& a_tmp, const std::string& a_text, std::string* a_why)
        {
            std::ofstream file(a_tmp, std::ios::binary | std::ios::trunc);
            if (!file) {
                *a_why = std::format("could not open {} for writing", Shown(a_tmp));
                return false;
            }
            if (!file.write(a_text.data(), static_cast<std::streamsize>(a_text.size()))) {
                *a_why = std::format("writing {} failed", Shown(a_tmp));
                return false;
            }
            if (!file.flush()) {
                *a_why = std::format("flushing {} failed", Shown(a_tmp));
                return false;
            }
            file.close();
            if (file.fail()) {
                *a_why = std::format("closing {} failed", Shown(a_tmp));
                return false;
            }
            return true;
        }

        // The written .tmp put in the file's place. a_keepBak (the plan's: a read file is replaced) copies the old file to .bak
        // (overwriting the last one) first; an unreadable file is not copied there (it is kept as .bad; GPT P1 on a3b810a5:
        // the .bak may be the layout loaded from). Then the .tmp is moved over the file. Not ReplaceFileW (D10's first choice until
        // 2026-10-31): under MO2's usvfs it made the file vanish - game run 2026-10-31, the saves logged "written (the old file
        // kept as .bak)" at 11:01:43 and 11:02:50 read back "not found" in the same session (SelfTest L3 FAIL) while the file
        // and its .bak on disk were right; the first save's plain MoveFileExW (11:02:36) read back fine. The old file is never
        // moved away here: a failed copy fails the save with the file as it was, and a failed move leaves it in place too
        bool PutInPlace(const std::filesystem::path& a_path, const std::filesystem::path& a_tmp, bool a_keepBak, std::string* a_why)
        {
            const auto bak = WithSuffix(a_path, L".bak");
            if (a_keepBak && !CopyFileW(a_path.c_str(), bak.c_str(), kCopyOverwrite)) {
                *a_why = std::format("the old file could not be kept as {} (CopyFileW error {}) - nothing written", Shown(bak), REX::W32::GetLastError());
                return false;
            }
            if (!MoveFileExW(a_tmp.c_str(), a_path.c_str(), kMoveReplaceExisting | kMoveWriteThrough)) {
                *a_why = std::format("MoveFileExW {} -> {} failed (error {})", Shown(a_tmp), Shown(a_path), REX::W32::GetLastError());
                return false;
            }
            return true;
        }

        // After PutInPlace failed (final review): the old file moved back from .bak when no file is left (Layout::AfterFailedPut;
        // the copy-then-move replace never moves the old file away, so this is kept for a file gone some other way). False: the .tmp must be kept - .bak could not be moved back, the log and a_why name where
        // the old and the edited layout are. True: the .tmp may be removed (the caller does, as before)
        bool RecoverFailedPut(const std::filesystem::path& a_path, const std::filesystem::path& a_tmp, bool a_replace, bool a_fileGood,
            std::string* a_why)
        {
            const auto bak = WithSuffix(a_path, L".bak");
            std::error_code fileError, bakError;
            // an exists() error counts as "there": nothing is moved over what could not be checked
            const bool fileThere = std::filesystem::exists(a_path, fileError) || fileError;
            const bool bakThere = std::filesystem::exists(bak, bakError) && !bakError;
            const auto put = Layout::AfterFailedPut(a_replace, fileThere, bakThere, a_fileGood);
            bool restored = false;
            if (put == Layout::FailedPut::kRestoreBak) {
                restored = MoveFileExW(bak.c_str(), a_path.c_str(), kMoveReplaceExisting | kMoveWriteThrough) != 0;
                if (restored) {
                    *a_why += std::format("; the old file was moved back from {} (the layout file is as it was)", Shown(bak));
                } else {
                    *a_why += std::format("; moving {} back to {} failed too (error {})", Shown(bak), Shown(a_path), REX::W32::GetLastError());
                }
            }
            if (!Layout::KeepTmp(put, restored)) return true;
            if (put == Layout::FailedPut::kFileBroken) {
                logger::error("Settings: layout save - {} is still the unreadable file: the edited layout is kept in {}, the .bak is left as "
                              "it is (the next save tries again; or rename {} to {} by hand)",
                    Shown(a_path), Shown(a_tmp), Shown(a_tmp), Shown(a_path));
                *a_why += std::format("; the edited layout kept in {} ({} is still the unreadable file)", Shown(a_tmp), Shown(a_path));
                return false;
            }
            logger::error("Settings: layout save - {} is gone: the saved layout is in {}, the edited one in {} (kept; the next load reads the "
                          ".bak, or rename either to {} by hand)",
                Shown(a_path), Shown(bak), Shown(a_tmp), Shown(a_path));
            *a_why += std::format("; the edited layout kept in {}, the saved one in {}", Shown(a_tmp), Shown(bak));
            return false;
        }
    }

    void Load()
    {
        const auto path = IniPath();
        const bool exists = std::filesystem::exists(path);
        logger::info("Settings: {} {}", SKSE::stl::utf16_to_utf8(path).value_or("(path not UTF-8)"),
            exists ? "found" : "not found - defaults");

        Values v;
        ReadBool(path, L"General", L"bEnabled", v.enabled);
        ReadBool(path, L"General", L"bSelfTest", v.selfTest);
        ReadBool(path, L"General", L"bMovePreview", v.movePreview);
        ReadBool(path, L"General", L"bMoveItemCard", v.moveItemCard);
        ReadBool(path, L"General", L"bDrawPreviewOver", v.drawPreviewOver);
        ReadBool(path, L"General", L"bPreviewUnlearned", v.previewUnlearned);
        ReadBool(path, L"General", L"bHideGamePass", v.hideGamePass);
        ReadBool(path, L"General", L"bClipPreview", v.clipPreview);
        ReadBool(path, L"General", L"bOwnCard", v.ownCard);
        ReadBool(path, L"General", L"bHideBackdrop", v.hideBackdrop);
        ReadBool(path, L"General", L"bHideGlow", v.hideGlow);
        ReadBool(path, L"Preview", L"bKeepPanelAlpha", v.keepPanelAlpha);
        ReadBool(path, L"Preview", L"bHidePowers3D", v.hidePowers3D);
        ReadNumber(path, L"Keys", L"iLayout", v.keyLayout, 1u, kMaxScanCode);
        // SKSE's gamepad codes (266 D-pad up .. 281 Y, SKSE InputMap), or -1 for none
        ReadNumber(path, L"Keys", L"iGamepadLayout", v.gamepadLayout, -1, kMaxGamepadCode);
        if (v.gamepadLayout != -1 && v.gamepadLayout < kMinGamepadCode) {
            logger::warn("Settings: [Keys] iGamepadLayout = {} is not a gamepad button ({}..{}) or -1 - keeping {}", v.gamepadLayout, kMinGamepadCode,
                kMaxGamepadCode, Values{}.gamepadLayout);
            v.gamepadLayout = Values{}.gamepadLayout;
        }
        ReadRect(path, L"fPreview", v.preview);
        // the 3D is placed in the panel's content area (PanelContent: the title strip and padding off), which has to have room
        // too (second review X3: an outer height of 38 left 0 and made the preview's scale 0)
        if (const auto content = PanelContent(v.preview); content.Width() <= 0 || content.Height() <= 0) {
            logger::warn("Settings: [Layout] fPreview rect {},{} .. {},{} leaves no room under its title ({:.0f} x {:.0f}; it needs more than "
                         "{:.0f} x {:.0f}) - keeping the default",
                v.preview.left, v.preview.top, v.preview.right, v.preview.bottom, content.Width(), content.Height(), 2 * kPanelPad,
                kPanelTitleH + 2 * kPanelPad);
            v.preview = Values{}.preview;
        }
        ReadRect(path, L"fItemCard", v.itemCard);
        ReadNumber(path, L"Layout", L"fTreeRight", v.treeRight, kStageMin, kStageMax);
        if (v.treeRight <= v.preview.right) {
            logger::warn("Settings: [Layout] fTreeRight = {} is not right of the preview ({}) - keeping {}", v.treeRight, v.preview.right,
                Values{}.treeRight);
            v.treeRight = Values{}.treeRight;
        }
        ReadNumber(path, L"Layout", L"fPanelGap", v.panelGap, 0.0, kMaxPanelGap);
        ReadBool(path, L"Debug", L"bDebugFillerBlock", v.debugFillerBlock);
        g_values = v;

        logger::info("Settings: enabled {} selfTest {} movePreview {} moveItemCard {} drawPreviewOver {} previewUnlearned {} hideGamePass {} "
                     "clipPreview {} ownCard {} hideBackdrop {} hideGlow {} keepPanelAlpha {} hidePowers3D {} keys layout {} gamepad layout {}",
            v.enabled, v.selfTest, v.movePreview, v.moveItemCard, v.drawPreviewOver, v.previewUnlearned, v.hideGamePass, v.clipPreview,
            v.ownCard, v.hideBackdrop, v.hideGlow, v.keepPanelAlpha, v.hidePowers3D, v.keyLayout, v.gamepadLayout);
        LogRect("preview  ", v.preview);
        LogRect("  3D area", PanelContent(v.preview));
        LogRect("item card", v.itemCard);
        logger::info("Settings:   columns end at x {:.0f} (fTreeRight), panel gap {} (between the blocks of a column and between the columns)",
            v.treeRight, v.panelGap);
        if (v.debugFillerBlock) {
            logger::info("Settings:   [Debug] bDebugFillerBlock 1 - the add-on test block {} (300 high) is registered at kDataLoaded; without a saved "
                         "place it goes to the left column after the spell card (the preview's column scrolls)",
                Layout::kDebugFillerId);
        }

        g_layout = LoadLayout(v);
        g_defaultLayout = Layout::Default(v);
        logger::info("Settings:   layout columns {} border {:.0f}{} (the rects above are the blocks' default sizes and places)", Columns(g_layout),
            g_layout.border, g_layout == Layout::Default(v) ? " - the default layout" : "");
    }

    const Values& Get()
    {
        return g_values;
    }

    const Layout::State& SavedLayout()
    {
        return g_layout;
    }

    Layout::FileState LayoutFileState()
    {
        return g_fileState;
    }

    void SetSavedLayout(const Layout::State& a_state)
    {
        g_layout = a_state;
    }

    void SetLegacyAliases(const std::map<std::string, std::string>& a_legacyToFinal)
    {
        g_legacyAliases = a_legacyToFinal;
    }

    void AddBlocks(const std::vector<std::string>& a_ids)
    {
        g_addonIds = a_ids;
        // the layout was read at load, before the aliases were known (the registry closes at the first menu open): renamed now.
        // The default is renamed too, so an add-on that took a retired id over has that id's place there and is not appended again;
        // the add-ons at the end of the right column, [Debug] bDebugFillerBlock's test block in the left one after the spell card,
        // so the preview's column scrolls in game (RF3 test path; final review ruling). A broken / newer file was replaced by the
        // default at load: the add-ons go into that default the same way (Layout::WithAddons)
        const auto read = g_layout;
        auto with = Layout::WithAddons(read, BuiltinBlocks(), g_legacyAliases, g_addonIds, Layout::Default(g_values));
        if (!(read == with.renamed)) {
            std::string renames;
            for (const auto& [legacyId, finalId] : g_legacyAliases) renames += std::format("{}{} -> {}", renames.empty() ? "" : ", ", legacyId, finalId);
            logger::info("Settings: retired block ids taken over by add-on blocks ({}) - layout columns {} read as {} (the file keeps the retired ids)",
                renames, Columns(read), Columns(with.renamed));
        }
        const auto before = with.renamed;
        g_defaultLayout = std::move(with.fallback);
        g_layout = std::move(with.layout);
        logger::info("Settings: {} add-on block(s){} - layout columns {}{}", g_addonIds.size(), g_addonIds.empty() ? "" : " known", Columns(g_layout),
            before == g_layout ? " (unchanged: each has its saved place)" : " (the ones without a saved place put at the end of the right column, the debug filler at the end of the left one)");
    }

    const Layout::State& DefaultLayout()
    {
        return g_defaultLayout;
    }

    SaveResult SaveLayout(const Layout::State& a_state, std::string* a_why)
    {
        std::string why;
        const auto done = [&](SaveResult a_result) {
            if (a_why) *a_why = why;
            return a_result;
        };
        const auto path = LayoutPath();
        std::error_code ec;
        const bool exists = std::filesystem::exists(path, ec);
        if (ec) {
            why = std::format("{} could not be checked ({})", Shown(path), ec.message());
            return done(SaveResult::kFailed);
        }
        const auto plan = Layout::PlanSave(g_fileState, g_badKept, exists);
        if (plan.refuse) {
            why = std::format("{} is from a newer build - not written over", Shown(path));
            return done(SaveResult::kRefused);
        }
        if (plan.copyBadFirst) {
            const auto bad = WithSuffix(path, L".bad");
            if (!CopyFileW(path.c_str(), bad.c_str(), kCopyOverwrite)) {
                why = std::format("the file that failed to read could not be kept as {} (error {})", Shown(bad), REX::W32::GetLastError());
                return done(SaveResult::kFailed);
            }
            g_badKept = true;
            logger::info("Settings: layout save - the file that failed to read at load kept as {}", Shown(bad));
        }
        const auto tmp = WithSuffix(path, L".tmp");
        // the file keeps the retired ids (design G-e): an older MagicMenuRewrite reads it as before
        const bool tmpWritten = WriteTemp(tmp, Layout::ToJson(Layout::ToLegacy(a_state, g_legacyAliases)), &why);
        const bool written = tmpWritten && PutInPlace(path, tmp, plan.keepBak, &why);
        const bool fileGood = g_fileState != Layout::FileState::kBroken;  // a failed put leaves the file as it was
        g_fileState = Layout::AfterSave(g_fileState, written);
        if (!written) {
            if (tmpWritten && !RecoverFailedPut(path, tmp, plan.replace, fileGood, &why)) return done(SaveResult::kFailed);
            std::error_code removeError;
            if (std::filesystem::exists(tmp, removeError)) std::filesystem::remove(tmp, removeError);
            if (removeError) why += std::format("; {} left behind ({})", Shown(tmp), removeError.message());
            return done(SaveResult::kFailed);
        }
        g_layout = a_state;
        why = std::format("{} written{}", Shown(path), plan.keepBak ? " (the old file kept as .bak)" :
                                                       plan.replace  ? " (over the unreadable file, kept as .bad; the .bak left as it was)" :
                                                                       " (a new file)");
        return done(SaveResult::kSaved);
    }

    std::optional<Layout::State> ReadLayoutFile(std::string* a_why)
    {
        auto read = ReadLayout(LayoutPath());
        if (a_why) *a_why = read.why;
        return read.state;
    }
}
