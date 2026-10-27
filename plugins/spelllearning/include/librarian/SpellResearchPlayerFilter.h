#pragma once

#include "Common.h"

#include <string>
#include <unordered_set>

// =============================================================================
// SpellResearchPlayerFilter - the spells the player left out of the tree, for the
// Spell Research registrations (LibrarianSpellResearchFilter.cpp)
// =============================================================================

namespace Librarian::SpellResearchPatch
{
    // What the panel's builder leaves out on the player's say (proceduralTreeBuilder.js:
    // the spell blacklist and the plugins turned off in the plugin whitelist), read from
    // config.json where the panel saves them, so SR gets the builder's spells exactly
    struct PlayerFilter
    {
        std::unordered_set<std::string> blacklisted;      // "plugin.esp|<local id>", lowercase plugin
        std::unordered_set<RE::FormID> blacklistedIds;     // entries that only carry a formId
        std::unordered_set<std::string> pluginsOff;        // lowercase
    };

    PlayerFilter ReadPlayerFilter();

    bool PlayerLeftOut(const std::string& persistentId, RE::FormID formId, const PlayerFilter& filter);
}
