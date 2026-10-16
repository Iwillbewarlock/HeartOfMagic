#pragma once

// =============================================================================
// Toxicology - co-save
// =============================================================================
// Spec 5.6: the Magicka counted toward the next bottle (ToxMagicka.h) is kept in
// the SKSE co-save. Unique id 'HMTX', one record 'ACCM' version 1 holding one
// float. Revert (before every load and on a new game): the total back to 0, the
// session generation bumped (queued hit and cast tasks dropped) and our poison
// application records cleared - so a save without our record starts at 0. Load
// then reads the record when the save has it; a non-finite or negative value is
// logged and the total stays 0.
//
// Threads: the callbacks are assumed to run on the game's main thread (as save
// loads do); the thread each one runs on is logged once.
// =============================================================================

#include <cstdint>

namespace ToxGame
{
    inline constexpr std::uint32_t kCoSaveUniqueId = 'HMTX';
    inline constexpr std::uint32_t kAccumulatedRecord = 'ACCM';
    inline constexpr std::uint32_t kAccumulatedVersion = 1;

    // Registers the serialization callbacks. Call once from SKSEPluginLoad, after SKSE::Init.
    void RegisterCoSave();
}
