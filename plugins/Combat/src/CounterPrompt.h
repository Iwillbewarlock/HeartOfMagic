#pragma once

// =============================================================================
// CounterPrompt - the counter key, shown and taken through SkyPrompt
// =============================================================================
// SkyPrompt is required. While IncomingWarning reports a spell about to hit,
// a SkyPrompt prompt "<promptText> [key]" sits on the player (near the head in
// third person) with a progress ring running down to the impact: one prompt
// per counter key, on one row: each hand that holds a spell gets its cast
// button, and with a one-handed weapon, fists or a shield the left button
// (the block key) is one too (actionID 0 = left, 1 = right). While they show, SkyPrompt takes those
// keys, so a press does not cast; outside that moment the buttons cast as usual.
// The press (kDown) is the counter press; the counter itself is judged at
// impact (ProjectileCounter). kUp ends a held ward.
// =============================================================================

#include "CounterInput.h"

#include "SkyPrompt/API.hpp"

#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace SLCombat
{
    class CounterPrompt : public SkyPromptAPI::PromptSink
    {
    public:
        static CounterPrompt* GetSingleton();

        /// Called at kDataLoaded. False when SkyPrompt is not installed (or gave no client ID).
        static bool Initialize();

        /// A spell will hit in a_secondsToHit (any thread; one prompt update per frame).
        static void OnThreat(float a_secondsToHit);

        /// Render thread only (the Present queue): publishes and sends, or removes.
        using KeyList = std::vector<std::pair<RE::INPUT_DEVICE, SkyPromptAPI::ButtonID>>;

        /// Render thread only (the Present queue): publishes and sends, or removes.
        /// a_keys[0] / [1]: the left / right hand's keys, empty when that hand holds no spell.
        void Publish(bool a_send, float a_progress, std::string a_text, std::array<KeyList, 2> a_keys);

        std::span<const SkyPromptAPI::Prompt> GetPrompts() const override;
        void ProcessEvent(SkyPromptAPI::PromptEvent a_event) const override;

        CounterPrompt(const CounterPrompt&) = delete;
        CounterPrompt& operator=(const CounterPrompt&) = delete;

    private:
        CounterPrompt() = default;
        ~CounterPrompt() override = default;

        void Show(float a_secondsToHit);
        void Hide();
        void ScheduleHideCheck();

        SkyPromptAPI::ClientID m_client = 0;
        std::string m_text;
        std::string m_previousText;
        std::array<KeyList, 2> m_keys;
        std::array<SkyPromptAPI::Prompt, 2> m_prompts{ SkyPromptAPI::Prompt(), SkyPromptAPI::Prompt() };
        std::size_t m_promptCount = 0;  ///< render thread
        bool m_shown = false;           ///< game thread
        std::uint8_t m_shownHands = 0;  ///< game thread: bit 0 left, bit 1 right
    };
}
