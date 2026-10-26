#include "Common.h"

#include "Settings.h"

#include "Layout.h"

#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <type_traits>

namespace Settings
{
    namespace
    {
        constexpr const wchar_t* kIniRelative = L"Data/SKSE/Plugins/SL_MagicMenu.ini";
        constexpr const wchar_t* kLayoutRelative = L"Data/SKSE/Plugins/SL_MagicMenu_Layout.json";
        // a layout file is a few hundred bytes; anything far bigger is not one (read no further)
        constexpr std::uintmax_t kMaxLayoutBytes = 64 * 1024;
        constexpr std::uint32_t kValueChars = 64;
        constexpr std::uint32_t kMaxScanCode = 255;
        // SKSE::InputMap: kMacro_GamepadOffset 266 .. kMaxMacros 282 (exclusive)
        constexpr int kMinGamepadCode = SKSE::InputMap::kMacro_GamepadOffset, kMaxGamepadCode = SKSE::InputMap::kMaxMacros - 1;
        constexpr double kMinTreeScale = 0.01, kMaxTreeScale = 10.0;
        constexpr double kMinMarkScale = 0.5, kMaxMarkScale = 3.0;
        constexpr int kMaxSteps = 6, kMaxNodes = 200;
        // two columns of Layout::kMinColumn (240) and the gap fit in the block area 648..1238 (590): 590 - 2 x 240 = 110
        constexpr double kMaxPanelGap = 110.0;
        constexpr double kMaxDebugExtraHeight = 2000.0;  // [Debug] fDebugExtraHeight: a few screens of overflow at most

        Values g_values;
        Layout::State g_layout;
        Layout::State g_defaultLayout;              // Layout::Default with the add-on blocks (AddBlocks)
        std::vector<Layout::BlockId> g_addonIds;   // the add-on blocks, in registration order

        // The built-in blocks: Repair appends any of them a layout file lacks
        std::set<Layout::BlockId> BuiltinBlocks()
        {
            return { Layout::kPreview, Layout::kTree, Layout::kProgress, Layout::kSpellCard, Layout::kNodeCard };
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
            constexpr double kStageMin = -4000.0, kStageMax = 4000.0;
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
            if (read.file == Layout::FileState::kMissing) {
                // final review: a save that moved the old file to .bak and then could not move the new one in leaves no file;
                // the .bak is read then (the state stays kMissing: the next save creates the file, the .bak is left as it is)
                const auto bak = WithSuffix(path, L".bak");
                std::error_code bakError;
                const bool bakThere = std::filesystem::exists(bak, bakError) && !bakError;
                if (Layout::ReadBackup(read.file, bakThere)) {
                    auto backup = ReadLayout(bak);
                    if (backup.state) {
                        logger::warn("Settings: layout {} not found - read {} instead (a save that lost the file left it there); the layout "
                                     "mode's next save writes {}",
                            Shown(path), Shown(bak), Shown(path));
                        return Layout::Repair(*backup.state, known, fallback);
                    }
                    logger::warn("Settings: layout {} not found, and {} {} - the default layout", Shown(path), Shown(bak), backup.why);
                    return fallback;
                }
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
        extern "C" __declspec(dllimport) int __stdcall ReplaceFileW(const wchar_t* a_replaced, const wchar_t* a_replacement, const wchar_t* a_backup,
            unsigned long a_flags, void* a_exclude, void* a_reserved);
        extern "C" __declspec(dllimport) int __stdcall MoveFileExW(const wchar_t* a_existing, const wchar_t* a_new, unsigned long a_flags);
        extern "C" __declspec(dllimport) int __stdcall CopyFileW(const wchar_t* a_existing, const wchar_t* a_new, int a_failIfExists);
        constexpr unsigned long kReplaceIgnoreMergeErrors = 0x2;                       // REPLACEFILE_IGNORE_MERGE_ERRORS
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

        // The written .tmp put in the file's place (the plan's replace: the old file kept as .bak)
        bool PutInPlace(const std::filesystem::path& a_path, const std::filesystem::path& a_tmp, bool a_replace, std::string* a_why)
        {
            const auto bak = WithSuffix(a_path, L".bak");
            if (a_replace) {
                if (ReplaceFileW(a_path.c_str(), a_tmp.c_str(), bak.c_str(), kReplaceIgnoreMergeErrors, nullptr, nullptr)) return true;
                const auto replaceError = REX::W32::GetLastError();
                // the fallback (D10): ReplaceFileW can fail on a virtual file system (MO2's usvfs, unverified) or after it moved the
                // old file to .bak already (ERROR_UNABLE_TO_MOVE_REPLACEMENT_2, 1177; RecoverFailedPut moves it back when the move fails too): the old file copied to .bak when it is still there,
                // then the move
                std::error_code ec;
                if (std::filesystem::exists(a_path, ec) && !CopyFileW(a_path.c_str(), bak.c_str(), 0)) {
                    *a_why = std::format("ReplaceFileW failed (error {}) and the old file could not be kept as {} (error {})", replaceError, Shown(bak),
                        REX::W32::GetLastError());
                    return false;
                }
                logger::warn("Settings: layout save - ReplaceFileW failed (error {}); the old file kept as {}, moving the new one in", replaceError,
                    Shown(bak));
            }
            if (!MoveFileExW(a_tmp.c_str(), a_path.c_str(), kMoveReplaceExisting | kMoveWriteThrough)) {
                *a_why = std::format("MoveFileExW {} -> {} failed (error {})", Shown(a_tmp), Shown(a_path), REX::W32::GetLastError());
                return false;
            }
            return true;
        }

        // After PutInPlace failed (final review): the old file moved back from .bak when the replace took it away and no file is
        // left (Layout::AfterFailedPut). False: the .tmp must be kept - .bak could not be moved back, the log and a_why name where
        // the old and the edited layout are. True: the .tmp may be removed (the caller does, as before)
        bool RecoverFailedPut(const std::filesystem::path& a_path, const std::filesystem::path& a_tmp, bool a_replace, std::string* a_why)
        {
            const auto bak = WithSuffix(a_path, L".bak");
            std::error_code fileError, bakError;
            // an exists() error counts as "there": nothing is moved over what could not be checked
            const bool fileThere = std::filesystem::exists(a_path, fileError) || fileError;
            const bool bakThere = std::filesystem::exists(bak, bakError) && !bakError;
            const auto put = Layout::AfterFailedPut(a_replace, fileThere, bakThere);
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
        ReadNumber(path, L"Keys", L"iNext", v.keyNext, 1u, kMaxScanCode);
        ReadNumber(path, L"Keys", L"iSetTarget", v.keySetTarget, 1u, kMaxScanCode);
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
        ReadRect(path, L"fTree", v.tree);
        ReadRect(path, L"fProgress", v.progress);
        ReadRect(path, L"fItemCard", v.itemCard);
        ReadRect(path, L"fNodeCard", v.nodeCard);
        ReadNumber(path, L"Layout", L"fTreeScale", v.treeScale, kMinTreeScale, kMaxTreeScale);
        ReadNumber(path, L"Layout", L"fTreeMarkScale", v.treeMarkScale, kMinMarkScale, kMaxMarkScale);
        ReadNumber(path, L"Layout", L"fPanelGap", v.panelGap, 0.0, kMaxPanelGap);
        ReadNumber(path, L"Layout", L"iMaxSteps", v.maxSteps, 1, kMaxSteps);
        ReadNumber(path, L"Layout", L"iMaxNodes", v.maxNodes, 1, kMaxNodes);
        ReadNumber(path, L"Debug", L"fDebugExtraHeight", v.debugExtraHeight, 0.0, kMaxDebugExtraHeight);
        ReadBool(path, L"Debug", L"bDebugFillerBlock", v.debugFillerBlock);
        g_values = v;

        logger::info("Settings: enabled {} selfTest {} movePreview {} moveItemCard {} drawPreviewOver {} previewUnlearned {} hideGamePass {} "
                     "clipPreview {} ownCard {} hideBackdrop {} hideGlow {} keepPanelAlpha {} keys next {} setTarget {} layout {} gamepad layout {}",
            v.enabled, v.selfTest, v.movePreview, v.moveItemCard, v.drawPreviewOver, v.previewUnlearned, v.hideGamePass, v.clipPreview,
            v.ownCard, v.hideBackdrop, v.hideGlow, v.keepPanelAlpha, v.keyNext, v.keySetTarget, v.keyLayout, v.gamepadLayout);
        LogRect("preview  ", v.preview);
        LogRect("  3D area", PanelContent(v.preview));
        LogRect("tree     ", v.tree);
        LogRect("progress ", v.progress);
        LogRect("item card", v.itemCard);
        LogRect("node card", v.nodeCard);
        logger::info("Settings:   panel gap {} (between the blocks of a column and between the columns)", v.panelGap);
        // the values applied (spec v10 T2: an ini from an earlier build keeps its own fTreeScale over the new default)
        logger::info("Settings:   tree scale {} (stage units per tree unit; default {}), mark scale {} (default {}), steps {}, max nodes {} "
                     "(default {})",
            v.treeScale, Values{}.treeScale, v.treeMarkScale, Values{}.treeMarkScale, v.maxSteps, v.maxNodes, Values{}.maxNodes);

        if (v.debugExtraHeight > 0) {
            logger::info("Settings:   [Debug] fDebugExtraHeight {} - the node card block is that much taller (a scroll test; 0 = off)", v.debugExtraHeight);
        }
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

    void AddBlocks(const std::vector<std::string>& a_ids)
    {
        g_addonIds = a_ids;
        // the add-ons at the end of the right column; [Debug] bDebugFillerBlock's test block in the left one after the spell card,
        // so the preview's column scrolls in game (RF3 test path; final review ruling)
        g_defaultLayout = Layout::AddAddons(Layout::Default(g_values), g_addonIds);
        auto known = BuiltinBlocks();
        known.insert(g_addonIds.begin(), g_addonIds.end());
        const auto before = g_layout;
        // a broken / newer file was replaced by the default at load: the add-ons go into that default the same way
        g_layout = Layout::Repair(g_layout, known, g_defaultLayout);
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
            if (!CopyFileW(path.c_str(), bad.c_str(), 0)) {
                why = std::format("the file that failed to read could not be kept as {} (error {})", Shown(bad), REX::W32::GetLastError());
                return done(SaveResult::kFailed);
            }
            g_badKept = true;
            logger::info("Settings: layout save - the file that failed to read at load kept as {}", Shown(bad));
        }
        const auto tmp = WithSuffix(path, L".tmp");
        const bool tmpWritten = WriteTemp(tmp, Layout::ToJson(a_state), &why);
        const bool written = tmpWritten && PutInPlace(path, tmp, plan.replace, &why);
        g_fileState = Layout::AfterSave(g_fileState, written);
        if (!written) {
            if (tmpWritten && !RecoverFailedPut(path, tmp, plan.replace, &why)) return done(SaveResult::kFailed);
            std::error_code removeError;
            if (std::filesystem::exists(tmp, removeError)) std::filesystem::remove(tmp, removeError);
            if (removeError) why += std::format("; {} left behind ({})", Shown(tmp), removeError.message());
            return done(SaveResult::kFailed);
        }
        g_layout = a_state;
        why = std::format("{} written{}", Shown(path), plan.replace ? " (the old file kept as .bak)" : " (a new file)");
        return done(SaveResult::kSaved);
    }

    std::optional<Layout::State> ReadLayoutFile(std::string* a_why)
    {
        auto read = ReadLayout(LayoutPath());
        if (a_why) *a_why = read.why;
        return read.state;
    }
}
