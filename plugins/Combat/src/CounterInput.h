#pragma once

// =============================================================================
// CounterInput - remembers when the counter key went down, and whether it is held
// =============================================================================
// The counter is judged at impact: a projectile that reaches the player is
// countered when the counter key (CounterKey; the SkyPrompt prompt's key,
// CounterPrompt) went down at most counterWindow seconds earlier. Presses
// outside the prompt are not counter presses; this sink only follows whether
// the key is held, so letting it go ends a held ward.
// =============================================================================

#include <atomic>
#include <chrono>

namespace SLCombat
{
    struct PressCheck
    {
        bool  inWindow = false;
        bool  ward = false;        ///< a hand held a ward spell at the press (the longer window)
        float secondsAgo = -1.0f;  ///< -1 when no press was recorded
    };

    class CounterInput : public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        static CounterInput* GetSingleton();
        static void Register();

        /// The last press, measured against the window.
        PressCheck Check() const;

        /// Forget the press, so one press counters one projectile.
        void Consume();

        /// The counter key is down right now.
        bool IsHeld() const { return m_held.load(); }

        /// The counter key went down (CounterPrompt, SkyPrompt's thread): atomics only.
        void RecordPress(bool a_ward);

        /// The counter key is down / up (CounterPrompt).
        void SetHeld(bool a_held) { m_held.store(a_held); }

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
            RE::BSTEventSource<RE::InputEvent*>* a_source) override;

        CounterInput(const CounterInput&) = delete;
        CounterInput& operator=(const CounterInput&) = delete;

    private:
        CounterInput() = default;

        using Clock = std::chrono::steady_clock;

        std::atomic<std::int64_t> m_pressedAt{ 0 };  ///< Clock ticks, 0 = none
        std::atomic<bool>         m_ward{ false };
        std::atomic<bool>         m_held{ false };
    };

    /// True when the spell has an effect on Ward Power (a ward).
    bool IsWardSpell(const RE::SpellItem* a_spell);
}
