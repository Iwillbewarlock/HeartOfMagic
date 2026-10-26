#pragma once

#include "Common.h"

#include <functional>
#include <string>

// =============================================================================
// ProgressionNotify - what the progression side tells the panel (H1-d)
//
// Progression code (ProgressionManager, SpellEffectivenessHook, SpellTomeHook,
// ISLIntegration, the librarian) calls these instead of UIManager. Each one
// passes straight on to the UIManager function of the same meaning, unless a
// Batch is open on this thread: then it is held and sent when the outermost
// Batch closes, in the order it was made. A function that changes state in
// several steps opens a Batch on its first line, so a notice made halfway
// (a clear before the new target is stored) reaches the panel only after the
// whole change; the order of the notices stays what it was
// (drop/clear -> set/grant -> unlocked -> progress, which the panel relies on).
// The panel's own rules (nothing while the view is invalid, no progress or
// ready while it is hidden) stay in UIManager. ModEvents are not routed here and
// keep their old timing: Papyrus listeners run them later, an SKSE plugin's
// ModCallbackEvent sink runs inside SendModEvent, as before. A UIManager throw is
// now logged here, not passed up to the caller.
// Design: docs/superpowers/specs/2026-10-26-h1d-notify-design.md
// =============================================================================

namespace ProgressionNotify
{
    enum class Kind
    {
        kProgress,          // formId, currentXP, requiredXP
        kProgressRefresh,   // text = formId as the caller had it; XP read when sent
        kSpellReady,        // formId
        kSpellUnlocked,     // formId, flag = success
        kTargetSet,         // school, formId, text = spell name
        kTargetCleared,     // formId
        kTargetDropped,     // school, formId
        kSpellState,        // formId, text = state ("weakened", "mastered", ...)
        kSourceRegistered,  // text = source id, name, multiplier, cap, flag = enabled
        kSpellResearchStatus
    };

    struct Event
    {
        Kind kind = Kind::kProgressRefresh;
        RE::FormID formId = 0;
        std::string school;
        std::string text;
        std::string name;
        float a = 0.0f;
        float b = 0.0f;
        bool flag = false;
    };

    const char* KindName(Kind kind);

    // Holds this thread's notices until the outermost Batch goes out of scope.
    // Put it before anything else in the function, so an early return sends too.
    class Batch
    {
    public:
        Batch();
        ~Batch();
        Batch(const Batch&) = delete;
        Batch& operator=(const Batch&) = delete;
    };

    void Progress(RE::FormID formId, float currentXP, float requiredXP);
    void ProgressRefresh(const std::string& formIdText);
    void SpellReady(RE::FormID formId);
    void SpellUnlocked(RE::FormID formId, bool success);
    void TargetSet(const std::string& school, RE::FormID formId, const std::string& spellName);
    void TargetCleared(RE::FormID formId);
    void TargetDropped(const std::string& school, RE::FormID formId);
    void SpellState(RE::FormID formId, const std::string& state);
    void SourceRegistered(const std::string& sourceId, const std::string& displayName,
        float multiplier, float cap, bool enabled = true);
    void SpellResearchStatus();

    // Self-test only (ProgressionNotifySelfTest.cpp): called with each notice at
    // the moment it would be sent, on the sending thread; it returns whether the
    // notice still goes to the panel (false keeps a test's steps off the panel,
    // whose own state the test's cleanup could not put back). Empty to stop.
    using Observer = std::function<bool(const Event&)>;
    void SetObserver(Observer observer);
}
