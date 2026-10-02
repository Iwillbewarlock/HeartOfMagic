#pragma once

// =============================================================================
// CounterKey - the one counter key (counterKey in SL_Combat.json)
// =============================================================================
// The code is the one SkyUI MCMs and Papyrus's Input script use: 0-255 a
// keyboard scan code (DirectInput), 256 + n mouse button n (256 left, 257
// right, 258 middle, 259 / 260 the side buttons). -1, the default, is the
// game's block key (the "Left Attack/Block" binding, on every device), so it
// follows the player's own control map. A set code is a keyboard or mouse key;
// a gamepad always uses its block binding.
// =============================================================================

#include <string>
#include <utility>
#include <vector>

namespace SLCombat::CounterKey
{
    inline constexpr int kGameBlockKey = -1;
    inline constexpr int kMouseOffset = 256;
    inline constexpr int kMaxCode = kMouseOffset + 7;  ///< mouse button 7, the last one the game reads

    /// The keys a prompt is bound to now: the set key and the gamepad's block
    /// binding, or the block binding on keyboard, mouse and gamepad.
    std::vector<std::pair<RE::INPUT_DEVICE, std::uint32_t>> PromptKeys();

    /// The button event is the counter key (its up event ends a held ward).
    bool Matches(const RE::ButtonEvent* a_button);

    /// "Block key (Right Mouse Button)", "Mouse 4", "G", "key 0x4F"...
    std::string Name(int a_code);

    /// The code is -1 or a keyboard / mouse key the game reads.
    bool Valid(int a_code);
}
