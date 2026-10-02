#pragma once

// =============================================================================
// CounterResponse - what a successful counter gives the player
// =============================================================================
//   - slow motion: the game's time-multiplier target is set (as Payload
//     Interpreter does) and put back to the value it had, never forced to 1.0,
//     after slowMoDuration real seconds, on a menu opening, or on a load;
//   - the next cast within freeCastWindow gets its magicka back;
//   - that next cast's spell gets learning XP through the Heart of Magic API
//     (XP source "combat"; Heart of Magic does the rest - docs/COMBAT.md).
// =============================================================================

#include "CounterInput.h"

namespace SpellLearning
{
    class ISpellLearningAPI;
}

namespace SLCombat
{
    class CounterResponse :
        public RE::BSTEventSink<RE::TESSpellCastEvent>,
        public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        static CounterResponse* GetSingleton();
        static void Register();

        /// a_hand: the hand whose press countered; still held a frame later, it keeps a ward up.
        /// a_bothHands: both keys were pressed (a dual-cast counter).
        static void OnCounter(RE::MagicItem* a_spell, RE::TESObjectREFR* a_shooter, Hand a_hand, bool a_bothHands);

        /// Put the time multiplier back if slow motion is running (menus).
        static void EndSlowMotion();

        /// Forget slow motion and the free cast without writing anything (loads).
        static void Reset();

        static void SetAPI(SpellLearning::ISpellLearningAPI* a_api);
        static void RegisterXPSource();

        RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* a_event,
            RE::BSTEventSource<RE::TESSpellCastEvent>* a_source) override;
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_source) override;

        CounterResponse(const CounterResponse&) = delete;
        CounterResponse& operator=(const CounterResponse&) = delete;

    private:
        CounterResponse() = default;

        static void StartSlowMotion();
        static void RefundLater(RE::FormID a_spellID);
    };
}
