#pragma once

// =============================================================================
// Toxicology - Magicka count internals
// =============================================================================
// Shared by game/ToxMagicka.cpp (the running total, the cast event and the
// fire-and-forget charge) and game/ToxMagickaHook.cpp (the ActorMagicCaster::Update
// hook and the concentration drain task). No other file includes this; the
// public interface is game/ToxMagicka.h.
// =============================================================================

#include "game/ToxCheck.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace ToxGame::MagickaDetail
{
    // MagicSystem::CastingSource kLeftHand 0, kRightHand 1 (RE/M/MagicSystem.h:26-30) index the hands;
    // kNoHand marks a reading taken from no hand caster
    inline constexpr std::size_t kLeftHand = 0;
    inline constexpr std::size_t kRightHand = 1;
    inline constexpr std::size_t kHandCount = 2;
    inline constexpr std::size_t kNoHand = ToxGame::kCheckNoHand;

    // MagicCaster::State values (RE/M/MagicCaster.h:30-42) are kept as bits of a 32-bit mask
    inline constexpr std::uint32_t kStateBitCount = 32;

    // One hook reading of a player hand caster
    struct HandSample
    {
        RE::FormID spell = 0;
        std::uint32_t state = 0;          // MagicCaster::State value
        float currentSpellCost = 0.0f;    // MagicCaster::currentSpellCost (RE/M/MagicCaster.h:108)
        float spellCost = 0.0f;           // MagicCaster::GetCurrentSpellCost() (RE/M/MagicCaster.h:94), when read
        float costCharged = 0.0f;         // ActorMagicCaster::costCharged (RE/A/ActorMagicCaster.h:100)
        bool dualCasting = false;         // ActorMagicCaster::Flags::kDualCasting
        double seenAt = -1.0;
    };

    // ToxMagicka.cpp
    double NowSeconds();
    // Logs once which thread a_what runs on (the main thread is the one InstallMagickaTracking ran on)
    void LogThreadOnce(std::atomic<bool>& a_flag, const char* a_what);
    // SKSE task (main thread); false, logged once, without the task interface
    bool QueueTask(std::function<void()> a_task);
    // Main thread: a_amount counted toward the next bottle and noted for the 5 s check
    void CountCast(float a_amount, std::size_t a_hand);
    const char* HandName(std::size_t a_hand);

    // ToxMagickaHook.cpp
    // Both hands' last fire-and-forget readings (copied under the hook's lock)
    std::array<HandSample, kHandCount> FireHands();
    // Main thread: whether the hook looks at casters at all (ability, "enabled", a non-empty cost set)
    void SetTracking(bool a_on);
    // Co-save Revert and a new game: amounts, readings and the queue flags forgotten
    void ResetHookState();
    void InstallCasterHook();
}
