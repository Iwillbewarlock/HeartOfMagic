#pragma once

// =============================================================================
// GAME THREAD - which thread is the game's main thread (the one SKSEPluginLoad
// runs on; kDataLoaded can run on another, game run 2026-10-25). Moved out of
// Hom.h/.cpp (HoM add-on plan Task 3): the core asks it too.
// =============================================================================

#include <cstdint>

namespace GameThread
{
    /// Remembers the calling thread as the game (main) thread (call in SKSEPluginLoad).
    void Set();

    /// The calling thread is the main thread.
    bool On();

    std::uint32_t Id();
}
