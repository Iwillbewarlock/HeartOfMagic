#pragma once

#include "Common.h"
#include <chrono>
#include <deque>
#include <unordered_map>

class SpellCastHandler : public RE::BSTEventSink<RE::TESSpellCastEvent>
{
public:
    static SpellCastHandler* GetSingleton();

    void Register();
    void Unregister();

    // Casts that skip the engine's cast event: MagicCaster::CastSpellImmediate on
    // the player's casters (SpellHotbar 2, Papyrus Spell.Cast). Installed at kPostLoad.
    static void InstallImmediateCastHook();
    void OnImmediateCast(RE::FormID spellId);

    // Event sink
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESSpellCastEvent* a_event,
        RE::BSTEventSource<RE::TESSpellCastEvent>* a_eventSource) override;

    // Settings for throttled notifications (configurable)
    void SetNotificationInterval(float seconds) { m_notificationIntervalSeconds = seconds; }
    float GetNotificationInterval() const { return m_notificationIntervalSeconds; }
    
    void SetWeakenedNotificationsEnabled(bool enabled) { m_weakenedNotificationsEnabled = enabled; }
    bool GetWeakenedNotificationsEnabled() const { return m_weakenedNotificationsEnabled; }

private:
    SpellCastHandler() = default;
    ~SpellCastHandler() = default;
    SpellCastHandler(const SpellCastHandler&) = delete;
    SpellCastHandler& operator=(const SpellCastHandler&) = delete;

    using Clock = std::chrono::steady_clock;

    // The work for one player cast, run on the game thread. a_immediate: seen by the
    // CastSpellImmediate hook rather than the cast event (only known spells count then).
    // a_castAt: when the engine reported it, not when the queued task runs.
    void HandlePlayerCast(RE::FormID spellId, bool a_immediate, Clock::time_point a_castAt);

    // One cast reported both ways (the cast event and the immediate-cast hook) is
    // credited once, whichever way comes first and however the reports interleave:
    // per spell, the credited casts of each way that the other way has not reported
    // yet. Only a report of the OTHER way within the window takes one of them and
    // earns nothing; repeats seen the same way always count (a spell in each hand).
    //
    // The window: the two reports need not come from one call chain or one thread
    // (the cast event arrives on whichever thread did the casting - four different
    // ones seen in game). The assumption is only that both are sent within a frame
    // or so of the one engine cast behind them. Their times are taken when the
    // engine reports them (ProcessEvent / OnImmediateCast), not when the queued task
    // runs - queue delay is not in the gap. 0.1 s is 3 frames at 30 fps, 6 at 60, so
    // a slow frame between the two still pairs; a real second cast the other way is
    // further apart than that (SpellHotbar 2's cooldown is 0.5 s, a hand cast's
    // animation longer still). It was 0.3 s, which could swallow a genuine second
    // cast. Each pairing logs its measured gap at debug level ("duplicate skipped
    // ... s apart") to check the assumption in game.
    // a_gapSeconds: set to the gap to the paired report when this returns true.
    static constexpr float kSameCastWindowSeconds = 0.1f;
    static constexpr std::size_t kMaxPendingPerWay = 8;
    struct PendingCasts
    {
        std::deque<Clock::time_point> byEvent;   // credited from the event, hook report still possible
        std::deque<Clock::time_point> byHook;    // credited from the hook, event report still possible
    };
    std::unordered_map<RE::FormID, PendingCasts> m_pendingCasts;
    // True when this report is the second report of a cast already credited
    bool IsSecondReport(RE::FormID spellId, bool a_immediate, Clock::time_point a_castAt, float& a_gapSeconds);
    void RememberCredited(RE::FormID spellId, bool a_immediate, Clock::time_point a_castAt);
    // A spell re-cast immediately again and again (a mod refreshing a ward every 0.25 s)
    // counts once, at the start of the stream, like a held concentration spell. Below
    // SpellHotbar 2's 0.5 s cooldown, so its casts each count.
    static constexpr float kImmediateStreamGapSeconds = 0.45f;
    std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> m_lastImmediateAt;

    bool m_registered = false;
    
    // Configurable notification settings
    float m_notificationIntervalSeconds = 10.0f;  // Default: Show weakened spell notification every 10 seconds
    bool m_weakenedNotificationsEnabled = true;   // Default: enabled
    
    // Throttled notification tracking
    std::chrono::steady_clock::time_point m_lastWeakenedNotifTime;
    float m_accumulatedXP = 0.0f;  // XP accumulated since last notification
    RE::FormID m_lastNotifiedSpell = 0;  // Track which spell we last notified about
};
