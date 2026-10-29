#pragma once

#include <string>

// =============================================================================
// LANG - every player-visible string of the block, from
// Data/SKSE/Plugins/SL_MagicMenu_SpellResearch/lang/<code>.json (the code from
// Settings::ResolveLanguage). A key the language lacks falls back to en.json, a
// key en.json lacks too to the key itself (logged once per key).
// =============================================================================

namespace Lang
{
    /// Reads the language's file and en.json (call once, at kDataLoaded, before any Get). Logs what was read
    void Load();

    /// The text of a_key (SrBlock::LookUp: language, English, the key). Thread-safe after Load
    std::string Get(const std::string& a_key);
}
