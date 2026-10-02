#pragma once

// =============================================================================
// CounterPrompt - the counter key, shown and taken through SkyPrompt
// =============================================================================
// SkyPrompt is required. While IncomingWarning reports a spell about to hit,
// a SkyPrompt prompt "<promptText> [key]" sits on the player (near the head in
// third person) with a progress ring running down to the impact. Its key is
// the counter key (CounterKey: the game's block key unless counterKey is set);
// it needs a spell in a hand unless requireSpellInHand is off. While it shows, SkyPrompt takes that key, so a
// press does not block or cast; outside that moment the key works as usual.
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

        using KeyList = std::vector<std::pair<RE::INPUT_DEVICE, SkyPromptAPI::ButtonID>>;

        /// Render thread only (the Present queue): publishes and sends, or removes.
        /// a_keys: the counter key's bindings (CounterKey::PromptKeys).
        void Publish(bool a_send, float a_progress, std::string a_text, KeyList a_keys);

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
        KeyList m_keys;
        SkyPromptAPI::Prompt m_prompt;
        std::size_t m_promptCount = 0;  ///< render thread: 0 or 1
        bool m_shown = false;           ///< game thread
    };
}
