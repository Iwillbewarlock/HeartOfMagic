#pragma once

// =============================================================================
// CounterInput - remembers when the player pressed cast with a spell in hand
// =============================================================================
// The counter is judged at impact: a projectile that reaches the player is
// countered when the counter key (the SkyPrompt prompt's key, CounterPrompt)
// went down at most counterWindow seconds earlier. Plain cast presses are not
// counter presses; this sink only follows whether the cast buttons are held.
// =============================================================================

#include <atomic>
#include <chrono>

namespace SLCombat
{
    enum class Hand : std::uint8_t
    {
        kNone,
        kLeft,
        kRight
    };

    struct PressCheck
    {
        bool  inWindow = false;
        Hand  hand = Hand::kNone;
        bool  ward = false;      ///< the pressed hand held a ward spell
        float secondsAgo = -1.0f;  ///< -1 when no press was recorded
        bool  bothHands = false;   ///< both hands' keys were pressed within their windows (dual cast)
    };

    class CounterInput : public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        static CounterInput* GetSingleton();
        static void Register();

        /// The most recent press on either hand, measured against the window.
        PressCheck Check() const;

        /// Forget the presses, so one press counters one projectile.
        void Consume();

        /// The hand's cast button is down right now.
        bool IsHeld(Hand a_hand) const;

        /// The counter key went down (CounterPrompt, SkyPrompt's thread): atomics only.
        void RecordPress(Hand a_hand, bool a_ward);

        /// The counter key is down / up (CounterPrompt).
        void SetHeld(Hand a_hand, bool a_held);

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
            RE::BSTEventSource<RE::InputEvent*>* a_source) override;

        CounterInput(const CounterInput&) = delete;
        CounterInput& operator=(const CounterInput&) = delete;

    private:
        CounterInput() = default;

        using Clock = std::chrono::steady_clock;

        struct HandState
        {
            std::atomic<std::int64_t> pressedAt{ 0 };  ///< Clock ticks, 0 = none
            std::atomic<bool>         ward{ false };
            std::atomic<bool>         held{ false };
        };

        void Record(HandState& a_state, bool a_leftHand);

        HandState m_left;
        HandState m_right;
    };

    /// True when the spell has an effect on Ward Power (a ward).
    bool IsWardSpell(const RE::SpellItem* a_spell);
}
