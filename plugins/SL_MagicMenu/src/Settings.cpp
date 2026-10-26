#include "Common.h"

#include "Settings.h"

#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <type_traits>

namespace Settings
{
    namespace
    {
        constexpr const wchar_t* kIniRelative = L"Data/SKSE/Plugins/SL_MagicMenu.ini";
        constexpr std::uint32_t kValueChars = 64;
        constexpr std::uint32_t kMaxScanCode = 255;
        constexpr double kMinTreeScale = 0.01, kMaxTreeScale = 10.0;
        constexpr double kMinMarkScale = 0.5, kMaxMarkScale = 3.0;
        constexpr int kMaxSteps = 6, kMaxNodes = 200;
        // two columns of Layout::kMinColumn (240) and the gap fit in the block area 648..1238 (590): 590 - 2 x 240 = 110
        constexpr double kMaxPanelGap = 110.0;

        Values g_values;

        // An absolute path: GetPrivateProfileString looks a bare relative name up in the Windows folder
        std::wstring IniPath()
        {
            std::error_code ec;
            auto dir = std::filesystem::current_path(ec);
            if (ec) {
                logger::warn("Settings: no current folder ({}), reading the ini by its relative name", ec.message());
                return kIniRelative;
            }
            return (dir / kIniRelative).wstring();
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
        g_values = v;

        logger::info("Settings: enabled {} selfTest {} movePreview {} moveItemCard {} drawPreviewOver {} previewUnlearned {} hideGamePass {} "
                     "clipPreview {} ownCard {} hideBackdrop {} hideGlow {} keepPanelAlpha {} keys next {} setTarget {}",
            v.enabled, v.selfTest, v.movePreview, v.moveItemCard, v.drawPreviewOver, v.previewUnlearned, v.hideGamePass, v.clipPreview,
            v.ownCard, v.hideBackdrop, v.hideGlow, v.keepPanelAlpha, v.keyNext, v.keySetTarget);
        LogRect("preview  ", v.preview);
        LogRect("  3D area", PanelContent(v.preview));
        LogRect("tree     ", v.tree);
        LogRect("progress ", v.progress);
        LogRect("item card", v.itemCard);
        LogRect("node card", v.nodeCard);
        logger::info("Settings:   panel gap {} (the item card under the preview on Powers/Shouts)", v.panelGap);
        // the values applied (spec v10 T2: an ini from an earlier build keeps its own fTreeScale over the new default)
        logger::info("Settings:   tree scale {} (stage units per tree unit; default {}), mark scale {} (default {}), steps {}, max nodes {} "
                     "(default {})",
            v.treeScale, Values{}.treeScale, v.treeMarkScale, Values{}.treeMarkScale, v.maxSteps, v.maxNodes, Values{}.maxNodes);
    }

    const Values& Get()
    {
        return g_values;
    }
}
