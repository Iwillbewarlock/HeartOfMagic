#include "tomereach/TomeReachConfig.h"
#include "tomereach/TomeReachRecords.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>

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

    bool IsDistributionConfig(const std::filesystem::path& file)
    {
        const auto name = LowerAscii(file.filename().string());
        const auto dir = LowerAscii(file.parent_path().string());
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

    std::vector<std::filesystem::path> FindDistributionConfigs(const std::filesystem::path& dataDir)
    {
        namespace fs = std::filesystem;
        std::vector<fs::path> out;
        std::error_code ec;
        for (fs::directory_iterator it(dataDir, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec) && IsDistributionConfig(it->path())) out.push_back(it->path());
        }
        const auto plugins = dataDir / "SKSE" / "Plugins";
        ec.clear();
        for (fs::recursive_directory_iterator it(plugins, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec) && IsDistributionConfig(it->path())) out.push_back(it->path());
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
            if (IdBefore(m_text, at, hex) || IdAfter(m_text, at + plugin.size(), hex)) return true;
        }
        return false;
    }
}
