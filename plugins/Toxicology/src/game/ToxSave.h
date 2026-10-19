#pragma once

// =============================================================================
// Toxicology - co-save
// =============================================================================
// Spec 5.6: the Magicka counted toward the next bottle (ToxMagicka.h), plus the
// concentration Magicka not yet counted (PendingMagicka), is kept in
// the SKSE co-save. Unique id 'HMTX', one record 'ACCM' version 1 holding one
// float. Revert (before every load and on a new game): the total back to 0, the
// session generation bumped (queued hit and cast tasks dropped) and our poison
// application records cleared - so a save without our record starts at 0. Load
// then reads the record when the save has it; a non-finite or negative value is
// logged and the total stays 0.
//
// Spec 4a: a second record 'TOMG' version 1 holds one std::uint8_t, 1 when the
// tome reward is satisfied (ToxReward.h), else 0. It is written on its own: a
// failed ACCM write does not skip it, nor the other way round. Revert sets the
// flag to false, so a save without the record (made before this version, or
// without the addon) starts with the reward open. A record of another version
// or length, a short read or a byte other than 0/1 is logged and leaves the
// flag false; a second TOMG in one save is logged and the last one wins.
//
// Threads: the callbacks are assumed to run on the game's main thread (as save
// loads do); the thread each one runs on is logged once. The reward flag is an
// atomic, since that thread is only logged, not guaranteed.
// =============================================================================

#include <cstdint>

namespace ToxGame
{
    // Multi-character literals are implementation-defined; MSVC puts the first character in the
    // high byte: 'HMTX' = 0x484D5458 ('H' 0x48, 'M' 0x4D, 'T' 0x54, 'X' 0x58), 'ACCM' = 0x4143434D
    // ('A' 0x41, 'C' 0x43, 'C' 0x43, 'M' 0x4D), 'TOMG' = 0x544F4D47 ('T' 0x54, 'O' 0x4F, 'M' 0x4D,
    // 'G' 0x47) - the values SKSE stores
    inline constexpr std::uint32_t kCoSaveUniqueId = 'HMTX';
    inline constexpr std::uint32_t kAccumulatedRecord = 'ACCM';
    inline constexpr std::uint32_t kRewardRecord = 'TOMG';
    static_assert(kCoSaveUniqueId == 0x484D5458 && kAccumulatedRecord == 0x4143434D && kRewardRecord == 0x544F4D47,
        "co-save ids changed value");
    inline constexpr std::uint32_t kAccumulatedVersion = 1;
    inline constexpr std::uint32_t kRewardVersion = 1;

    // Registers the serialization callbacks. Call once from SKSEPluginLoad, after SKSE::Init.
    void RegisterCoSave();
}
