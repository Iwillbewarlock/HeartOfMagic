#include "tomereach/TomeReachConfig.h"
#include "tomereach/TomeReachRecords.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <system_error>
#include <type_traits>

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
        // One directory entry: an entry whose status cannot be read (an error code), or whose name cannot be
        // converted (std::system_error, which std::filesystem::filesystem_error derives from), is skipped alone
        // and counted in `skipped`; the scan goes on. No logger here (the offline harness builds this file
        // without one): the callers log the count.
        void AddIfConfig(const std::filesystem::directory_entry& entry, std::vector<std::filesystem::path>& out,
            std::size_t& skipped)
        {
            try {
                std::error_code ec;
                const bool regular = entry.is_regular_file(ec);
                if (ec) {
                    ++skipped;
                } else if (regular && IsDistributionConfig(entry.path())) {
                    out.push_back(entry.path());
                }
            } catch (const std::system_error&) {
                ++skipped;
            }
        }

        // Every entry `Iterator` (directory_iterator or recursive_directory_iterator) gives for `dir`, through `visit`.
        // A folder that cannot be opened (a missing one) lists nothing and is not counted; a failed step to the next
        // entry counts one skipped entry and ends the listing there (the iterator is unusable after it).
        template <class Iterator, class Visit>
        void ForEachEntry(const std::filesystem::path& dir, std::filesystem::directory_options options, std::size_t& skipped,
            Visit&& visit)
        {
            std::error_code ec;
            Iterator it(dir, options, ec);
            if (ec) return;
            for (const Iterator end{}; it != end;) {
                visit(*it);
                it.increment(ec);
                if (ec) {
                    ++skipped;
                    return;
                }
            }
        }

        // Every entry of a folder through AddIfConfig, or only names ending in one of `suffixes`
        void ListFolder(const std::filesystem::path& dir, std::span<const std::string_view> suffixes, bool every,
            std::vector<std::filesystem::path>& out, std::size_t& skipped)
        {
            ForEachEntry<std::filesystem::directory_iterator>(dir, std::filesystem::directory_options::none, skipped,
                [&](const std::filesystem::directory_entry& entry) {
                    try {
                        const auto name = LowerUtf8(entry.path().filename());
                        if (every || std::ranges::any_of(suffixes, [&](auto s) { return name.ends_with(s); })) {
                            AddIfConfig(entry, out, skipped);
                        }
                    } catch (const std::system_error&) {
                        ++skipped;
                    }
                });
        }

#ifdef _WIN32
        // A FindFirstFileExW search handle, closed by FindClose on every path out of its scope
        struct FindCloser
        {
            void operator()(HANDLE handle) const noexcept { FindClose(handle); }
        };
        using FindHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, FindCloser>;

        // The search for `pattern`, or an empty handle when nothing matches or the folder does not exist
        // (INVALID_HANDLE_VALUE is not null, so it is never put in the unique_ptr)
        FindHandle FindFirst(const std::wstring& pattern, WIN32_FIND_DATAW& found)
        {
            const HANDLE handle = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &found, FindExSearchNameMatch,
                nullptr, FIND_FIRST_EX_LARGE_FETCH);
            return FindHandle(handle == INVALID_HANDLE_VALUE ? nullptr : handle);
        }

        // Called right after FindNextFileW returned false: anything but the normal end means entries were not listed
        void CountFindEnd(std::size_t& skipped)
        {
            if (GetLastError() != ERROR_NO_MORE_FILES) ++skipped;
        }

        // Only the names "*<suffix>" (the OS match is case-insensitive, like IsDistributionConfig): under
        // MO2's USVFS the data folder's top level is every mod's files merged, thousands of entries, and
        // listing them all took ~3 s (2026-10-08, ~4,500 mods). Each hit still goes through
        // IsDistributionConfig on its long name, since a wildcard also matches 8.3 short names.
        void AddMatching(const std::filesystem::path& dataDir, std::string_view suffix,
            std::vector<std::filesystem::path>& out, std::size_t& skipped)
        {
            WIN32_FIND_DATAW found{};
            const auto find = FindFirst((dataDir / ("*" + std::string(suffix))).wstring(), found);
            if (!find) return;
            do {
                if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::error_code ec;  // the entry reads its status here: one stat per match, not per file
                    const std::filesystem::directory_entry entry(dataDir / std::filesystem::path(found.cFileName), ec);
                    if (ec) {
                        ++skipped;
                    } else {
                        AddIfConfig(entry, out, skipped);
                    }
                }
            } while (FindNextFileW(find.get(), &found));
            CountFindEnd(skipped);
        }
#endif

        // The direct child folders of SKSE/Plugins whose name holds a config folder's name. Under
        // _WIN32 one "*<name>*" wildcard per name (USVFS answers it without listing every mod's
        // folder); each hit is checked again on its long name, since a wildcard also matches 8.3 names.
        std::vector<std::filesystem::path> ConfigFolders(const std::filesystem::path& plugins, std::size_t& skipped)
        {
            std::vector<std::filesystem::path> out;
            const auto add = [&](const std::filesystem::path& dir) {
                try {
                    const auto name = LowerUtf8(dir.filename());
                    if (std::ranges::any_of(kConfigFolders, [&](auto folder) { return Contains(name, folder); }) &&
                        std::ranges::find(out, dir) == out.end()) {
                        out.push_back(dir);  // a name holding two config names is found twice, kept once
                    }
                } catch (const std::system_error&) {
                    ++skipped;
                }
            };
#ifdef _WIN32
            for (auto folder : kConfigFolders) {
                WIN32_FIND_DATAW found{};
                const auto find = FindFirst((plugins / ("*" + std::string(folder) + "*")).wstring(), found);
                if (!find) continue;
                do {
                    if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        add(plugins / std::filesystem::path(found.cFileName));
                    }
                } while (FindNextFileW(find.get(), &found));
                CountFindEnd(skipped);
            }
#else
            ForEachEntry<std::filesystem::directory_iterator>(plugins, std::filesystem::directory_options::none, skipped,
                [&](const std::filesystem::directory_entry& entry) {
                    std::error_code dirEc;
                    if (entry.is_directory(dirEc) && !dirEc) add(entry.path());
                });
#endif
            return out;
        }

        // Writes the count to the caller's out-param when it gave one
        void ReportSkipped(std::size_t* skippedEntries, std::size_t skipped)
        {
            if (skippedEntries) *skippedEntries = skipped;
        }
    }

    std::vector<std::filesystem::path> TopLevelConfigs(const std::filesystem::path& dataDir,
        std::span<const std::string_view> suffixes, std::size_t* skippedEntries)
    {
        std::vector<std::filesystem::path> out;
        std::size_t skipped = 0;
#ifdef _WIN32
        for (auto suffix : suffixes) AddMatching(dataDir, suffix, out, skipped);
#else
        ListFolder(dataDir, suffixes, false, out, skipped);
#endif
        ReportSkipped(skippedEntries, skipped);
        return out;
    }

    std::vector<std::filesystem::path> FindDistributionConfigs(const std::filesystem::path& dataDir,
        std::size_t* skippedEntries)
    {
        namespace fs = std::filesystem;
        // A data folder whose own path holds a config folder's name (".../LLOS/Data"): there the folder
        // name, not the file name, decides (IsDistributionConfig), and only a full listing is exact
        const auto dataLower = LowerUtf8(dataDir);
        std::vector<fs::path> out;
        std::size_t skipped = 0;
        if (std::ranges::any_of(kConfigFolders, [&](auto folder) { return Contains(dataLower, folder); })) {
            ListFolder(dataDir, kConfigSuffixes, true, out, skipped);
        } else {
            out = TopLevelConfigs(dataDir, kConfigSuffixes, &skipped);
        }
        // Under SKSE/Plugins only the config folders' files (SkyPatcher, LLOS, ...): the suffix files
        // (_DISTR.ini, _KID.ini, ...) are read by their frameworks from the data folder's top level only.
        // Known limit (2026-10-08): a suffix file anywhere under SKSE/Plugins, and a config folder nested
        // deeper than a direct child of SKSE/Plugins (SKSE/Plugins/Other/SkyPatcher), are not read. The
        // full recursive walk that found them took 1.4 s under MO2's USVFS on a big load order and found
        // none there; if a framework turns out to read such files, the upgrade is that full walk again.
        for (const auto& folder : ConfigFolders(dataDir / "SKSE" / "Plugins", skipped)) {
            ForEachEntry<fs::recursive_directory_iterator>(folder, fs::directory_options::skip_permission_denied, skipped,
                [&](const fs::directory_entry& entry) { AddIfConfig(entry, out, skipped); });
        }
        ReportSkipped(skippedEntries, skipped);
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
