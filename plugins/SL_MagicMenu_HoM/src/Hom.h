#pragma once

// =============================================================================
// PROTOTYPE ONLY - replaced by the I1 contract.
// HOM - Heart of Magic's public API v2 (SpellLearningAPI.h) for the magic menu:
// the pointer HoM broadcasts at kPostPostLoad and thin, null-safe wrappers.
// API v2 calls are game-thread only: every wrapper checks the calling thread
// against the main thread (the one SKSEPluginLoad runs on; kDataLoaded can run
// on another, game run 2026-10-25; GameThread.h), and returns the "nothing" value (logged
// once) when it is called from another thread or before the API arrived.
// =============================================================================

#include <cstdint>
#include <string>

namespace SpellLearning
{
    class ISpellLearningAPI;
}

namespace Hom
{
    /// Stores the API pointer from HoM's kMessageType_APIReady broadcast.
    void SetApi(SpellLearning::ISpellLearningAPI* a_api);

    /// The API arrived.
    bool Ready();

    RE::FormID Target(const std::string& a_school);
    float ProgressPercent(RE::FormID a_spell);  // 0..100 (HoM's GetProgress multiplies by 100)
    float RequiredXP(RE::FormID a_spell);
    bool Mastered(RE::FormID a_spell);

    /// Queues SetLearningTarget on the game thread (SKSE task) and logs whether the school's target
    /// is a_spell afterwards; logs the old target first. The task skips a spell that is already the target. False
    /// (nothing queued) while a request for the same spell has not finished. a_school and a_name are for the log only.
    bool RequestTarget(RE::FormID a_spell, const std::string& a_school, const std::string& a_name);

    /// True once after a queued SetLearningTarget ran (HoM's state changed under the menu).
    bool TakeStateChanged();
}
