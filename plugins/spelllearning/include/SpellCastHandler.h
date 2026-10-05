#pragma once

#include "Common.h"
#include <chrono>
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
    
    // XP alerts (notifications.weakenedSpellNotifications, "XP Alerts" in the panel): the
    // weakened cast line here, the power step line (ProgressionManagerXP) and a tome's
    // study lines (SpellTomeHook, DESTIntegration). Learned, mastered and blocked
    // messages are not XP alerts and always show.
    void SetXPNotificationsEnabled(bool enabled) { m_weakenedNotificationsEnabled = enabled; }
    bool GetXPNotificationsEnabled() const { return m_weakenedNotificationsEnabled; }

private:
    SpellCastHandler() = default;
    ~SpellCastHandler() = default;
    SpellCastHandler(const SpellCastHandler&) = delete;
    SpellCastHandler& operator=(const SpellCastHandler&) = delete;

    // The work for one player cast, run on the game thread. a_immediate: seen by the
    // CastSpellImmediate hook rather than the cast event (only known spells count then).
    void HandlePlayerCast(RE::FormID spellId, bool a_immediate);

    // One cast reported by both ways (event and immediate cast) is credited once;
    // repeats seen the same way always count (a spell in each hand)
    static constexpr float kSameCastWindowSeconds = 0.3f;
    struct Credited
    {
        std::chrono::steady_clock::time_point at;
        bool immediate = false;
        bool matched = false;   // already paired with its other-way report: pairs once
    };
    std::unordered_map<RE::FormID, Credited> m_lastCredited;
    // A spell re-cast immediately again and again (a mod refreshing a ward every 0.25 s)
    // counts once, at the start of the stream, like a held concentration spell. Below
    // SpellHotbar 2's 0.5 s cooldown, so its casts each count.
    static constexpr float kImmediateStreamGapSeconds = 0.45f;
    std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> m_lastImmediateAt;
    bool m_loggedImmediate = false;   // first immediate-cast credit logged
    bool m_loggedDuplicate = false;   // first cast seen both ways logged

    bool m_registered = false;
    
    // Configurable notification settings
    float m_notificationIntervalSeconds = 10.0f;  // Default: Show weakened spell notification every 10 seconds
    bool m_weakenedNotificationsEnabled = true;   // Default: enabled
    
    // Throttled notification tracking
    std::chrono::steady_clock::time_point m_lastWeakenedNotifTime;
    float m_accumulatedXP = 0.0f;  // XP accumulated since last notification
    RE::FormID m_lastNotifiedSpell = 0;  // Track which spell we last notified about
};
