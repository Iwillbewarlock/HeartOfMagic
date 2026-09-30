#pragma once

// =============================================================================
// TOME REACH - PLUGIN RECORDS
// =============================================================================
//
// Which spell tomes can a player get? A tome counts when something in the load
// order refers to it: a placed reference, a leveled list, a container or NPC
// inventory, a form list, a crafting recipe, an item a quest alias creates or
// forces, or a script property. This reads the plugin files themselves: in game
// the placed references of cells that are not loaded are not in memory.
//
// Pure C++ (no RE types): the plugin and the offline harness tools/tome-reach-test
// run the same code. See docs/ARCHITECTURE.md, "Tomes nothing hands out".

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TomeReach
{
    // A form in the load order: the plugin's number in a NameTable (high half)
    // and the form's id inside that plugin (low half, 12 bits for a light plugin)
    using Key = std::uint64_t;

    constexpr unsigned kPluginShift = 32;

    constexpr Key MakeKey(std::uint32_t plugin, std::uint32_t local) noexcept
    {
        return (static_cast<Key>(plugin) << kPluginShift) | local;
    }

    // A form id's parts: the high byte picks the plugin (0xFE: a light plugin,
    // whose number is the next 12 bits); the rest is the id inside the plugin
    constexpr unsigned kFileIndexShift = 24;
    constexpr std::uint32_t kLightIndex = 0xFE;
    constexpr unsigned kLightFileShift = 12;
    constexpr std::uint32_t kLightFileMask = 0x00000FFF;
    constexpr std::uint32_t kLocalMask = 0x00FFFFFF;
    constexpr std::uint32_t kLightLocalMask = 0x00000FFF;

    // The game's data folder, relative to the game's working directory
    inline constexpr std::string_view kDataDir = "Data";

    constexpr std::uint32_t PluginOf(Key key) noexcept { return static_cast<std::uint32_t>(key >> kPluginShift); }
    constexpr std::uint32_t LocalOf(Key key) noexcept { return static_cast<std::uint32_t>(key); }

    // Plugin file names (ASCII case ignored) to small numbers, with whether each
    // is a light plugin (its form ids use 12 bits)
    class NameTable
    {
    public:
        std::uint32_t Id(std::string_view fileName);
        void SetLight(std::uint32_t id, bool light);
        [[nodiscard]] bool IsLight(std::uint32_t id) const;
        [[nodiscard]] const std::string& Name(std::uint32_t id) const { return m_names[id]; }

    private:
        std::unordered_map<std::string, std::uint32_t> m_ids;
        std::vector<std::string> m_names;
        std::vector<bool> m_light;
    };

    std::string LowerAscii(std::string_view text);

    // One BOOK record: the tome, the spell it teaches (0 when none) and its editor id
    struct BookRecord
    {
        Key book = 0;
        Key spell = 0;
        std::string editorId;
    };

    struct PluginFile
    {
        std::string name;             // as the load order names it
        std::filesystem::path path;
        std::filesystem::path fallback;  // tried when `path` cannot be opened (may be empty)
    };

    // What a plugin refers to among the wanted keys: by a record field that holds
    // a form (placed, listed, held, crafted), or only loosely (four bytes of a
    // script property or quest alias that happen to read as the key)
    struct Evidence
    {
        std::unordered_set<Key> found;
        std::unordered_set<Key> foundLoose;
        std::unordered_map<Key, std::uint32_t> looseFrom;  // the first plugin a loose hit came from
    };

    enum class ScanStatus
    {
        kRead,
        kDamaged,     // header read, then a record past the end or one that does not inflate
        kUnreadable,  // cannot be opened, or not a plugin
    };

    struct PluginScan
    {
        ScanStatus status = ScanStatus::kUnreadable;
        // kDamaged: the plugin and its masters, the only plugins whose forms it
        // could have referred to in the part that was not read
        std::vector<std::uint32_t> scope;
    };

    // Reads one plugin: every key of `wanted` it refers to goes into `evidence`,
    // and when `books` is given every BOOK record is appended to it. A damaged
    // plugin's silence is not "nothing refers to this tome": see PluginScan.
    PluginScan ScanPlugin(const PluginFile& file, NameTable& names, const std::unordered_set<Key>& wanted,
        Evidence& evidence, std::vector<BookRecord>* books);
}
