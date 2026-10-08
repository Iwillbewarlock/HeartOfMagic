#include "tomereach/TomeReachConfig.h"
#include "tomereach/TomeReachRecords.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

namespace TomeReach
{
    namespace
    {
        using namespace std::string_view_literals;

        // A config larger than this is not a distribution list (a dump, a log)
        constexpr std::uintmax_t kMaxConfigBytes = 4u * 1024u * 1024u;

        constexpr std::array kConfigSuffixes = {
            "_distr.ini"sv, "_kid.ini"sv, "_flm.ini"sv, "_swap.ini"sv, "_cid.ini"sv, "_cdf.ini"sv, "_lli.ini"sv,
        };
        constexpr std::array kConfigFolders = {
            "skypatcher"sv, "containerdistributionframework"sv, "llos"sv, "leveledlist"sv, "containeritemdistributor"sv,
        };
        constexpr std::array kConfigExtensions = { ".ini"sv, ".json"sv, ".toml"sv, ".yaml"sv, ".yml"sv };
        constexpr std::array kSkippedFolders = { "dynamicstringdistributor"sv, "fomod"sv };

        bool Word(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }
        bool Hex(unsigned char c) { return std::isxdigit(c) != 0; }
        bool Separator(char c) { return c == '~' || c == '|' || c == ':'; }
        bool Space(char c) { return c == ' ' || c == '\t'; }

        // "<hex> ~ plugin" ending at `end` (the plugin's first character)
        bool IdBefore(const std::string& text, std::size_t end, const std::string& hex)
        {
            std::size_t k = end;
            while (k > 0 && Space(text[k - 1])) --k;
            if (k == 0 || !Separator(text[k - 1])) return false;
            --k;
            while (k > 0 && Space(text[k - 1])) --k;
            if (k < hex.size() || text.compare(k - hex.size(), hex.size(), hex) != 0) return false;
            std::size_t m = k - hex.size();
            while (m > 0 && text[m - 1] == '0') --m;
            if (m >= 2 && text.compare(m - 2, 2, "0x") == 0) m -= 2;
            return m == 0 || !(Hex(static_cast<unsigned char>(text[m - 1])) || text[m - 1] == 'x');
        }

        // "plugin | <hex>" starting at `start` (just past the plugin's name)
        bool IdAfter(const std::string& text, std::size_t start, const std::string& hex)
        {
            std::size_t j = start;
            while (j < text.size() && Space(text[j])) ++j;
            if (j >= text.size() || !Separator(text[j])) return false;
            ++j;
            while (j < text.size() && Space(text[j])) ++j;
            if (text.compare(j, 2, "0x") == 0) j += 2;
            while (j < text.size() && text[j] == '0') ++j;
            if (text.compare(j, hex.size(), hex) != 0) return false;
            j += hex.size();
            return j >= text.size() || !Hex(static_cast<unsigned char>(text[j]));
        }

        bool Contains(std::string_view text, std::string_view part)
        {
            return text.find(part) != std::string_view::npos;
        }
    }

    std::string LowerUtf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return LowerAscii(std::string_view(reinterpret_cast<const char*>(u8.data()), u8.size()));
    }

    bool IsDistributionConfig(const std::filesystem::path& file)
    {
        const auto name = LowerUtf8(file.filename());
        const auto dir = LowerUtf8(file.parent_path());
        for (auto skipped : kSkippedFolders) {
            if (Contains(dir, skipped)) return false;
        }
        if (name == "meta.ini") return false;
        bool extension = false;
        for (auto ext : kConfigExtensions) extension = extension || name.ends_with(ext);
        if (!extension) return false;
        for (auto suffix : kConfigSuffixes) {
            if (name.ends_with(suffix)) return true;
        }
        for (auto folder : kConfigFolders) {
            if (Contains(dir, folder)) return true;
        }
        return false;
    }

    namespace
    {
        // One directory entry: an entry whose status cannot be read, or whose name
        // cannot be handled, is skipped alone; the scan goes on. No logger here (the
        // offline harness builds this file without one).
        void AddIfConfig(const std::filesystem::directory_entry& entry, std::vector<std::filesystem::path>& out)
        {
            try {
                std::error_code ec;
                if (entry.is_regular_file(ec) && !ec && IsDistributionConfig(entry.path())) out.push_back(entry.path());
            } catch (...) {
            }
        }

        // Every entry of a folder through AddIfConfig, or only names ending in one of `suffixes`
        void ListFolder(const std::filesystem::path& dir, std::span<const std::string_view> suffixes, bool every,
            std::vector<std::filesystem::path>& out)
        {
            std::error_code ec;
            for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
                try {
                    const auto name = LowerUtf8(it->path().filename());
                    if (every || std::ranges::any_of(suffixes, [&](auto s) { return name.ends_with(s); })) {
                        AddIfConfig(*it, out);
                    }
                } catch (...) {
                }
            }
        }

#ifdef _WIN32
        // Only the names "*<suffix>" (the OS match is case-insensitive, like IsDistributionConfig): under
        // MO2's USVFS the data folder's top level is every mod's files merged, thousands of entries, and
        // listing them all took ~3 s (2026-10-08, ~4,500 mods). Each hit still goes through
        // IsDistributionConfig on its long name, since a wildcard also matches 8.3 short names.
        void AddMatching(const std::filesystem::path& dataDir, std::string_view suffix,
            std::vector<std::filesystem::path>& out)
        {
            const auto pattern = (dataDir / ("*" + std::string(suffix))).wstring();
            WIN32_FIND_DATAW found{};
            const HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &found, FindExSearchNameMatch,
                nullptr, FIND_FIRST_EX_LARGE_FETCH);
            if (find == INVALID_HANDLE_VALUE) return;  // no match, or no such folder
            do {
                if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::error_code ec;  // the entry reads its status here: one stat per match, not per file
                    const std::filesystem::directory_entry entry(dataDir / std::filesystem::path(found.cFileName), ec);
                    if (!ec) AddIfConfig(entry, out);
                }
            } while (FindNextFileW(find, &found));
            FindClose(find);
        }
#endif
    }

    std::vector<std::filesystem::path> TopLevelConfigs(const std::filesystem::path& dataDir,
        std::span<const std::string_view> suffixes)
    {
        std::vector<std::filesystem::path> out;
#ifdef _WIN32
        for (auto suffix : suffixes) AddMatching(dataDir, suffix, out);
#else
        ListFolder(dataDir, suffixes, false, out);
#endif
        return out;
    }

    std::vector<std::filesystem::path> FindDistributionConfigs(const std::filesystem::path& dataDir)
    {
        namespace fs = std::filesystem;
        // A data folder whose own path holds a config folder's name (".../LLOS/Data"): there the folder
        // name, not the file name, decides (IsDistributionConfig), and only a full listing is exact
        const auto dataLower = LowerUtf8(dataDir);
        std::vector<fs::path> out;
        if (std::ranges::any_of(kConfigFolders, [&](auto folder) { return Contains(dataLower, folder); })) {
            ListFolder(dataDir, kConfigSuffixes, true, out);
        } else {
            out = TopLevelConfigs(dataDir, kConfigSuffixes);
        }
        const auto plugins = dataDir / "SKSE" / "Plugins";
        std::error_code ec;
        for (fs::recursive_directory_iterator it(plugins, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            AddIfConfig(*it, out);
        }
        return out;
    }

    void ConfigText::Add(const std::filesystem::path& file)
    {
        std::error_code ec;
        const auto size = std::filesystem::file_size(file, ec);
        if (ec || size > kMaxConfigBytes) return;
        std::ifstream in(file, std::ios::binary);
        if (!in) return;
        std::ostringstream buffer;
        buffer << in.rdbuf();
        m_text += LowerAscii(buffer.str());
        m_text += '\n';
        ++m_files;
    }

    bool IdNextTo(const std::string& text, std::size_t at, std::size_t pluginSize, const std::string& hex)
    {
        return IdBefore(text, at, hex) || IdAfter(text, at + pluginSize, hex);
    }

    bool ConfigText::Names(std::string_view pluginLower, std::uint32_t local, std::string_view editorId) const
    {
        if (!editorId.empty()) {
            const auto id = LowerAscii(editorId);
            for (auto at = m_text.find(id); at != std::string::npos; at = m_text.find(id, at + 1)) {
                const bool before = at == 0 || !Word(static_cast<unsigned char>(m_text[at - 1]));
                const std::size_t after = at + id.size();
                if (before && (after >= m_text.size() || !Word(static_cast<unsigned char>(m_text[after])))) return true;
            }
        }
        if (pluginLower.empty() || !local) return false;
        const auto hex = std::format("{:x}", local);
        const std::string plugin(pluginLower);
        for (auto at = m_text.find(plugin); at != std::string::npos; at = m_text.find(plugin, at + 1)) {
            if (IdNextTo(m_text, at, plugin.size(), hex)) return true;
        }
        return false;
    }
}
