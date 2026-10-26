#pragma once

#include <cstddef>
#include <string>

// =============================================================================
// LOAD ORDER FINGERPRINT (docs/ARCHITECTURE.md, "Tree built at game start")
// =============================================================================
//
// FNV-1a 64 over the loaded plugin file names in load order (full plugins,
// then light ones) and the number of SpellItem forms: the same for the same
// load order on every start, different when a plugin is added, removed or
// moved, or a mod update adds spells under the same file name.
// Limit: the count is taken when first asked for (at the main menu, normally);
// a mod that creates SpellItems at run time before that can change it on its
// own and look like a load order that changed.

namespace LoadOrderFingerprint
{
    struct Value
    {
        std::string hash;          // 16 hex digits; empty before kDataLoaded
        std::size_t plugins = 0;
        std::size_t spells = 0;
    };

    // Game thread. Worked out once: the load order does not change while the game runs.
    const Value& Current();

    // Any thread: the value Current() worked out, or nullptr before it has.
    // Never works it out itself (that reads the game's form arrays).
    const Value* Known() noexcept;
}
