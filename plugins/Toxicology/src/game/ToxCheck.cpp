#include "game/ToxCheck.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace
{
    // =========================================================================
    // State (main thread)
    // =========================================================================

    struct CheckWindow
    {
        bool open = false;
        double openedAt = 0.0;
        double lastAt = 0.0;
        float startMagicka = 0.0f;
        std::array<float, ToxGame::kCheckNoHand + 1> counted{};  // left, right, no hand
    };

    CheckWindow g_check;

    double NowSeconds()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    float PlayerMagicka(RE::PlayerCharacter& a_player)
    {
        auto* owner = a_player.AsActorValueOwner();  // RE/A/Actor.h:778
        return owner ? owner->GetActorValue(RE::ActorValue::kMagicka) : 0.0f;  // RE/A/ActorValueOwner.h:16
    }
}

// =============================================================================
// Public
// =============================================================================

void ToxGame::NoteCountedForCheck(float a_amount, std::size_t a_hand)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;
    const double now = NowSeconds();
    const float magicka = PlayerMagicka(*player);
    if (!g_check.open || now - g_check.lastAt > kCheckGapSeconds) {
        g_check = { true, now, now, magicka, {} };
        return;
    }
    g_check.counted[std::min(a_hand, kCheckNoHand)] += a_amount;
    g_check.lastAt = now;
    if (now - g_check.openedAt < kCheckWindowSeconds) return;
    const auto& c = g_check.counted;
    logger::info("Toxicology: magicka counted {:.2f} (left {:.2f}, right {:.2f}, no hand {:.2f}) / player magicka drop {:.2f} over {:.1f} s",
        c[0] + c[1] + c[2], c[0], c[1], c[2], g_check.startMagicka - magicka, now - g_check.openedAt);
    g_check.open = false;
}

void ToxGame::ResetCheck()
{
    g_check = {};
}
