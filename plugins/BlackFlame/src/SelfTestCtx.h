#pragma once

#include "Common.h"
#include "SelfTestProbe.h"
#include "SelfTestWorld.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

// =============================================================================
// SELF-TEST STATE - shared by the state machine (SelfTest.cpp) and its steps
// (SelfTestSteps.cpp). Game thread only.
// =============================================================================

namespace BlackFlame::SelfTest
{
    using Clock = std::chrono::steady_clock;

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
        bool actorsOk = false;  // T2 passed: placed, passive, out of the explosion's reach
        RE::FormID casterId = 0;
        std::uint64_t mark = 0;
        std::uint64_t t3Mark = 0;
        bool buffWasTurnedOn = false;
        bool t3Recolored = false;
        int stokeBefore = 0;
        std::optional<Clock::time_point> greySeen;
        bool engineLiveAfterGrey = false;
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

    /// a_caster casts a_spell at the target, re-resolved now; a FAIL and false when the target
    /// or the cast is gone (nothing cast)
    [[nodiscard]] bool CastAtTarget(Ctx& a_c, RE::Actor* a_caster, RE::SpellItem* a_spell, const char* a_id);

    /// Both test actors still resolve and T2 passed; otherwise a FAIL "skipped" and false
    [[nodiscard]] bool NeedActors(Ctx& a_c, const char* a_id);

    inline constexpr auto kBurnWatch = std::chrono::seconds(1);             // engine shader watched after the grey shows
    inline constexpr auto kNpcBurnWait = std::chrono::milliseconds(1500);
    inline constexpr auto kInFlightWait = std::chrono::milliseconds(1500);  // after the last cast, before deleting
    // A colour-key component outside this range or NaN: the modifier layout does not match
    inline constexpr float kMinKey = 0.0f;
    inline constexpr float kMaxKey = 2.0f;

    // SelfTestStepsLate.cpp (T5 to T9); T0 to T3 are in SelfTestSteps.cpp with the step table
    bool StepPlayerBurn(Ctx& a_c, bool a_first);
    bool StepBurnExtend(Ctx& a_c, bool a_first);
    bool StepNpcProjectile(Ctx& a_c, bool a_first);
    bool StepNpcBurnInfo(Ctx& a_c, bool a_first);
    bool StepPlayerExplosion(Ctx& a_c, bool a_first);
    bool StepNpcExplosion(Ctx& a_c, bool a_first);
    bool StepModifiers(Ctx& a_c, bool a_first);
    bool StepBuffOff(Ctx& a_c, bool a_first);
    bool StepCastUnstoked(Ctx& a_c, bool a_first);
    bool StepCleanup(Ctx& a_c, bool a_first);
}
