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

    // C3: a cast at the target counts as a hit only on a TESHitEvent (the player, the spell, the target)
    inline constexpr int kMaxAimCasts = 3;

    struct Aim
    {
        std::uint64_t mark = 0;       // hits and records searched since (the latest cast)
        std::uint64_t firstMark = 0;  // the first cast (the FAIL line lists every hit since)
        int casts = 0;
        bool newCast = false;         // a recast was made: the step resets its own search from mark
        bool hit = false;
        bool gaveUp = false;
        std::optional<Clock::time_point> hitAt;
    };

    enum class AimResult
    {
        kHit,
        kWaiting,
        kGaveUp  // a FAIL "aim: no hit after 3 casts" with the geometry is logged
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
        RE::ObjectRefHandle extraTarget;  // E2's flame target (removed by E2, else by the cleanup)
        bool cleanedUp = false;  // the cleanup step dispelled the buff and deleted the actors
        bool actorsOk = false;  // T2 passed: placed, passive, at rest 300..1200 from the player (I1)
        bool t6Skipped = false;     // I1: T6 skipped for the explosion margin (T6-swap not judged)
        bool t6NpcSkipped = false;  // and T6-npc (T6-npc-swap not judged)
        bool t1Dispelled = false;                    // I2: T1 found the buff on and dispelled it first
        std::chrono::milliseconds t1OffAfter{ 0 };   // I2: T1's dispel -> flag off, from the step start
        std::chrono::milliseconds t1CastAfter{ 0 };  // and -> the cast
        RE::FormID casterId = 0;
        std::uint64_t mark = 0;
        std::uint64_t t3Mark = 0;
        bool buffWasTurnedOn = false;
        bool t3Recolored = false;
        bool t3Copy = false;  // W5: T3's Firebolt launched with the projectile copy (recolour off)
        bool redispelled = false;   // C4: T8's second dispel dispelled something (the first did not hold)
        bool redispelTried = false; // E2: T8 tried the second dispel
        bool t3Judged = false;      // E4: T3 judged grey or not (a copy or the recolour), not INFO
        int stokeBefore = 0;
        std::optional<Clock::time_point> greySeen;
        bool engineLiveAfterGrey = false;
        std::uint64_t secondMark = 0;
        bool secondAttempted = false;
        bool secondCast = false;
        RE::ObjectRefHandle remoteMarker;                 // T3-remote's tagged casting marker
        RE::FormID remoteMarkerId = 0;
        std::uint64_t swapsAtT3 = 0;     // ImpactSwapCount when T3 cast (T3-swap: the Firebolt impacts)
        std::uint64_t mappedAtT3 = 0;    // ImpactMappedSeenCount then
        std::uint64_t mappedAtT6 = 0;    // and when T6 cast
        std::uint64_t swapsAtT6 = 0;     // ImpactSwapCount when T6 cast
        std::uint64_t hazardSwapsAtT6 = 0;  // HazardSwapCount when T6 cast
        std::uint64_t swapsAtT6npc = 0;  // and when T6-npc cast
        RE::ObjectRefHandle blameMarker;                  // T3-blame's casting marker
        RE::FormID blameMarkerId = 0;
        std::optional<Clock::time_point> secondSwapSeen;  // T5-second's swap record first seen
        std::optional<bool> npcModifiersChanged;  // the caster's colour keys moved between its hook and T4
        std::vector<SeenShader> engineBeforeT4;    // engine hit shaders on the target before the T4 casts
        std::optional<Clock::time_point> npcHitEta;  // when the caster's T4 projectile can have arrived
        std::vector<ColorKeys> t3ModifierKeys;     // the player's T3 projectile's colour keys before its recolour
        std::optional<SelfTestProbe::ProjectileRecord> playerT4;
        std::optional<SelfTestProbe::ProjectileRecord> npcT4;
        Aim aim;                                       // C3: T5's confirmation of the T3 Firebolt's hit
        bool targetMade = false;                       // T2 turned the target's AI off
        std::string targetMadeText;                    // what MakeTarget said
        float targetLastZ = 0.0f;                      // T2: the target's height at rest
        // T2 rest (H1): XY and Z each anchored where the target was last seen moving on that axis, since then still
        std::optional<RE::NiPoint3> targetXYAnchor;
        std::optional<Clock::time_point> targetXYSince;
        std::optional<float> targetZAnchor;
        std::optional<Clock::time_point> targetZSince;
        std::optional<RE::NiPoint3> targetWaitStart;   // where the rest wait began (the T2 line says how far it moved)
        float targetMaxDrift = 0.0f;                   // the most XY distance from targetWaitStart seen during the wait
        float targetZMin = 0.0f;
        float targetZMax = 0.0f;
        RE::NiPoint3 casterSpawn{};                    // where Spawn put the caster
        std::optional<RE::NiPoint3> casterHome;        // its planned spot at the height it stood at in T2
    };

    using Handler = bool (*)(Ctx&, bool);  // true when the step is finished

    struct StepDef
    {
        const char* id;
        float timeoutSeconds;  // counted only while the game runs unpaused
        Handler run;
    };

    /// The steps in order; the cleanup (T9) every run ends with comes before E3, which saves and reloads the game
    [[nodiscard]] std::span<const StepDef> Steps();

    /// a_caster launches a_spell's projectile at the target (SelfTestWorld::LaunchAt), the target
    /// re-resolved now; a FAIL and false when the target
    /// or the cast is gone (nothing cast)
    [[nodiscard]] bool CastAtTarget(Ctx& a_c, RE::Actor* a_caster, RE::SpellItem* a_spell, const char* a_id);

    /// Both test actors still resolve and T2 passed; otherwise a FAIL "skipped" and false
    [[nodiscard]] bool NeedActors(Ctx& a_c, const char* a_id);

    // SelfTestAim.cpp (C3)
    /// A new aim whose first cast was just made; hits are counted from a_mark
    void StartAim(Aim& a_aim, std::uint64_t a_mark);
    /// The hit of the last cast by TESHitEvent; after its flight and a margin without one, the player casts again,
    /// up to kMaxAimCasts casts, then a FAIL "aim: no hit after 3 casts" with the geometry
    /// a_before (E1): when not 0, only hits recorded before that mark count (T5-second's cast must not fill T5's gate)
    [[nodiscard]] AimResult ConfirmHit(Ctx& a_c, Aim& a_aim, RE::SpellItem* a_spell, const char* a_id, bool a_mayRecast = true,
        std::uint64_t a_before = 0);
    /// Every TESHitEvent on the target since a_mark (cause, source, projectile)
    [[nodiscard]] std::string HitsListed(const Ctx& a_c, std::uint64_t a_mark);
    /// The player, the target, its 3D bound and their distance, for FAIL lines
    [[nodiscard]] std::string AimGeometry(const Ctx& a_c);
    /// The caster moved back home when it wandered off (it keeps its AI); true when it was moved - the caller casts on a
    /// later tick (G2), after the move has taken effect
    [[nodiscard]] bool BringCasterHome(Ctx& a_c, const char* a_id);

    enum class Prepared
    {
        kReady,
        kWait,    // the caster was just moved home: cast on a later tick
        kFailed,  // FAIL logged, nothing cast
        kSkipped  // I1: INFO logged (no explosion margin), nothing cast
    };

    /// I1: the explosion steps' margin from where the actors are now: false (INFO logged, the step skipped, not failed)
    /// when the target is within the explosion radius + kRadiusMargin of the player, or with a_caster of the caster
    [[nodiscard]] bool ExplosionRoom(Ctx& a_c, const char* a_id, bool a_caster);

    inline constexpr float kCasterLeash = 200.0f;  // the caster moved further than this from home: put back (E3: and T2)

    // D3: one gap for the self-test's clocks and E3's ready time: a longer gap between ticks is paused time
    inline constexpr auto kMaxTickGap = std::chrono::milliseconds(500);

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
    bool StepImpactSwap(Ctx& a_c, bool a_first);
    bool StepNpcImpactSwap(Ctx& a_c, bool a_first);
    bool StepProjectileImpactSwap(Ctx& a_c, bool a_first);
    bool StepNpcProjectile(Ctx& a_c, bool a_first);
    bool StepNpcBurnInfo(Ctx& a_c, bool a_first);
    bool StepPlayerExplosion(Ctx& a_c, bool a_first);
    bool StepNpcExplosion(Ctx& a_c, bool a_first);
    bool StepModifiers(Ctx& a_c, bool a_first);
    bool StepBuffOff(Ctx& a_c, bool a_first);
    bool StepCastUnstoked(Ctx& a_c, bool a_first);
    bool StepCleanup(Ctx& a_c, bool a_first);

    // The v2 copy framework spike: SelfTestStepsV2.cpp, SelfTestStepsV2Fire.cpp, SelfTestStepsV2Flames.cpp
    bool StepV2Static(Ctx& a_c, bool a_first);
    bool StepHitShader(Ctx& a_c, bool a_first);
    bool StepE1Firebolt(Ctx& a_c, bool a_first);
    bool StepE1Fireball(Ctx& a_c, bool a_first);
    bool StepE1Npc(Ctx& a_c, bool a_first);
    bool StepE2Flames(Ctx& a_c, bool a_first);
    bool StepThreads(Ctx& a_c, bool a_first);
    bool StepV2Cover(Ctx& a_c, bool a_first);   // SelfTestStepsV2Cover.cpp: T0-fresh, T0-cover
    bool StepE1Modded(Ctx& a_c, bool a_first);  // E1m
    bool StepE6Player(Ctx& a_c, bool a_first);  // SelfTestStepsV2Art.cpp (ARTO copies)
    bool StepE6Npc(Ctx& a_c, bool a_first);
    bool StepE5(Ctx& a_c, bool a_first);  // PickCastingArtImpl's rule, called directly
    void CleanupArtSpells();  // T9: E6's cloak and its associated spell dispelled on every high-process actor

    /// Paused time added to the v2 steps' own clocks (SelfTestStepsV2Fire.cpp, SelfTestStepsV2Flames.cpp,
    /// SelfTestReload.cpp); SelfTest.cpp ShiftClocks calls it
    void ShiftV2Clocks(Clock::duration a_paused);

    /// V9: the player's launch of a_spell should take a copy (copyLaunch on, the Launch detour hooked, its projectile
    /// has a copy); recolour-off steps judge copies with it
    [[nodiscard]] bool CopyLaunchExpected(const RE::SpellItem* a_spell);
    void ShiftFireClocks(Clock::duration a_paused);
    void ShiftFlameClocks(Clock::duration a_paused);
}
