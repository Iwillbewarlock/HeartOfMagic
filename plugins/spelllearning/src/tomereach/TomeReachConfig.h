#pragma once

// =============================================================================
// TOME REACH - DISTRIBUTION CONFIGS
// =============================================================================
//
// Tomes handed out by the SKSE distribution frameworks, which change forms at
// load time from text files: SPID (_DISTR.ini), KID (_KID.ini), FormList
// Manipulator (_FLM.ini), Base Object Swapper (_SWAP.ini), Container Item
// Distributor (_CID.ini), CDF, LLI, SkyPatcher and LLOS folders. Only these:
// inventory display lists (GridInventory) and icon lists name every book and
// would make every tome count. Shared with tools/tome-reach-test and the NPC
// Spells addon; the data folder listing uses Win32 under _WIN32 (std::filesystem
// otherwise).

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace TomeReach
{
    // A path's UTF-8 bytes, ASCII lowered. Never throws on names outside the ANSI
    // code page, as path::string() does.
    std::string LowerUtf8(const std::filesystem::path& path);

    // A file one of the distribution frameworks reads
    bool IsDistributionConfig(const std::filesystem::path& file);

    // The files at a data folder's top level whose name ends in one of `suffixes` (ASCII lower case,
    // matched case-insensitively) and that IsDistributionConfig accepts. Lists only the matching names
    // (an OS wildcard), not the whole folder: under MO2's USVFS that holds every mod's files.
    std::vector<std::filesystem::path> TopLevelConfigs(const std::filesystem::path& dataDir,
        std::span<const std::string_view> suffixes);

    // The distribution configs under a data folder: its top level and SKSE/Plugins
    std::vector<std::filesystem::path> FindDistributionConfigs(const std::filesystem::path& dataDir);

    // Is the plugin name at text[at, at + pluginSize) joined to the id `hex` (lower case, no leading zeros):
    // "<hex> ~ plugin" or "plugin | <hex>" (any of ~ | :), the test ConfigText::Names makes at each mention
    bool IdNextTo(const std::string& text, std::size_t at, std::size_t pluginSize, const std::string& hex);

    class ConfigText
    {
    public:
        void Add(const std::filesystem::path& file);

        // Does a config name this form, by editor id or as plugin and form id
        // ("0x800~Plugin.esp", "Plugin.esp|0x800", "Plugin.esp:800")?
        [[nodiscard]] bool Names(std::string_view pluginLower, std::uint32_t local, std::string_view editorId) const;

        [[nodiscard]] std::size_t FileCount() const { return m_files; }

        // Every config read so far, ASCII lower case, one newline after each
        [[nodiscard]] const std::string& Text() const { return m_text; }

    private:
        std::string m_text;  // every config, lower case (ASCII)
        std::size_t m_files = 0;
    };
}
