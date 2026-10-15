#pragma once

#include "Common.h"
#include "SelfTestProbe.h"
#include "SelfTestWorld.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// =============================================================================
// SELF-TEST STATE - shared by the state machine (SelfTest.cpp) and its steps
// (SelfTestSteps.cpp). Game thread only.
// =============================================================================

namespace BlackFlame::SelfTest
{
    using Clock = std::chrono::steady_clock;

    // A shader pointer seen at a time with an age: a later sighting is the same object only if its
    // age grew with the clock (an address can be reused by a new shader)
    struct SeenShader
    {
        std::uintptr_t ptr = 0;
        float age = 0.0f;
        Clock::time_point at{};
        float paused = 0.0f;  // SelfTestProbe::PausedSeconds() when it was seen
        bool finished = false;
    };

    struct Ctx
    {
        Clock::time_point stepStart;
        int phase = 0;  // a step's own progress, 0 when it starts
        std::string waitingFor;
        std::optional<Clock::time_point> lastCast;

        SelfTestWorld::VanillaForms forms;
        bool haveForms = false;
        RE::ObjectRefHandle dummy;
        RE::ObjectRefHandle caster;
        bool cleanedUp = false;  // the cleanup step dispelled the buff and deleted the actors
        bool actorsOk = false;  // T2 passed: placed, passive, out of the explosion's reach
        RE::FormID casterId = 0;
        std::uint64_t mark = 0;
        std::uint64_t t3Mark = 0;
        bool buffWasTurnedOn = false;
        bool t3Recolored = false;
        int stokeBefore = 0;
        std::optional<Clock::time_point> greySeen;
        bool engineLiveAfterGrey = false;
        std::uint64_t secondMark = 0;
        bool secondAttempted = false;
        bool secondCast = false;
        std::uintptr_t firstSwapController = 0;           // the controller T5's swap record named
        RE::ObjectRefHandle remoteMarker;                 // T3-remote's tagged casting marker
        RE::FormID remoteMarkerId = 0;
        RE::ObjectRefHandle blameMarker;                  // T3-blame's casting marker
        RE::FormID blameMarkerId = 0;
        std::optional<Clock::time_point> secondSwapSeen;  // T5-second's swap record first seen
        std::optional<bool> npcModifiersChanged;  // the caster's colour keys moved between its hook and T4
        std::vector<SeenShader> engineBeforeT4;    // engine hit shaders on the target before the T4 casts
        std::optional<Clock::time_point> npcHitEta;  // when the caster's T4 projectile can have arrived
        std::vector<ColorKeys> t3ModifierKeys;     // the player's T3 projectile's colour keys before its recolour
        std::optional<SelfTestProbe::ProjectileRecord> playerT4;
        std::optional<SelfTestProbe::ProjectileRecord> npcT4;
    };

    using Handler = bool (*)(Ctx&, bool);  // true when the step is finished

    struct StepDef
    {
        const char* id;
        float timeoutSeconds;  // counted only while the game runs unpaused
        Handler run;
    };

    /// The steps in order; the last is the cleanup every run ends with
    [[nodiscard]] std::span<const StepDef> Steps();

    /// a_caster launches a_spell's projectile at the target (SelfTestWorld::LaunchAt), the target
    /// re-resolved now; a FAIL and false when the target
    /// or the cast is gone (nothing cast)
    [[nodiscard]] bool CastAtTarget(Ctx& a_c, RE::Actor* a_caster, RE::SpellItem* a_spell, const char* a_id);

    /// Both test actors still resolve and T2 passed; otherwise a FAIL "skipped" and false
    [[nodiscard]] bool NeedActors(Ctx& a_c, const char* a_id);

    inline constexpr auto kBurnWatch = std::chrono::seconds(1);             // engine shader watched after the grey shows
    // An engine hit shader counts as alight from this age on (a younger one may be a frame's flash)
    inline constexpr float kEngineAgeMin = 0.5f;
    // T5-second judges this long after its swap record, so an engine shader made by that hit would
    // be old enough to count
    inline constexpr auto kSecondJudgeDelay = std::chrono::milliseconds(800);
    inline constexpr float kRadiusMargin = 100.0f;  // beyond Fireball's explosion radius
    inline constexpr auto kSecondRecordWait = std::chrono::seconds(1);  // T4 waits for T5-second's projectile record
    inline constexpr auto kNpcBurnWait = std::chrono::milliseconds(1500);
    inline constexpr auto kInFlightWait = std::chrono::milliseconds(1500);  // after the last cast, before deleting

    // SelfTestStepsLate.cpp (T5 to T9); T0 to T3 are in SelfTestSteps.cpp with the step table
    bool StepPlayerBurn(Ctx& a_c, bool a_first);
    bool StepSecondHit(Ctx& a_c, bool a_first);
    bool StepRemoteCast(Ctx& a_c, bool a_first);
    void RemoveRemoteMarkers(Ctx& a_c);  // T3-remote's markers, also at cleanup
    bool StepNpcProjectile(Ctx& a_c, bool a_first);
    bool StepNpcBurnInfo(Ctx& a_c, bool a_first);
    bool StepPlayerExplosion(Ctx& a_c, bool a_first);
    bool StepNpcExplosion(Ctx& a_c, bool a_first);
    bool StepModifiers(Ctx& a_c, bool a_first);
    bool StepBuffOff(Ctx& a_c, bool a_first);
    bool StepCastUnstoked(Ctx& a_c, bool a_first);
    bool StepCleanup(Ctx& a_c, bool a_first);
}
