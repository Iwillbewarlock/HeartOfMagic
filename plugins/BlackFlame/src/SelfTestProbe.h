#pragma once

#include "Common.h"
#include "ParticleColors.h"
#include "Recolor.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// =============================================================================
// SELF-TEST PROBE - what the layers saw, for the self-test (SelfTest.h)
// =============================================================================
// Off unless the self-test is running (Recording()): the layers then report each
// fire projectile, fire explosion and burn swap here, with a read-only look at the
// 3D taken on the game thread (the hooks defer to a game-thread task when they
// run elsewhere, and only then capture or inspect). The self-test reads the
// records back by sequence number. The record functions are safe from any thread
// (one mutex); Capture and Inspect are game thread only.

namespace BlackFlame::SelfTestProbe
{
    [[nodiscard]] bool Recording() noexcept;
    void SetRecording(bool a_on) noexcept;

    /// Drops every record and the paused total (the self-test's end)
    void Clear();

    /// The game time the self-test spent paused (menus, load screens): shader ages do not grow then,
    /// so identity checks take it out of the wall clock. Game thread only.
    void AddPausedSeconds(float a_seconds);
    [[nodiscard]] float PausedSeconds();

    /// The sequence number the next record will get: records "since a mark" are newer
    [[nodiscard]] std::uint64_t Mark();

    // The effect properties of a 3D, in traversal order (read only)
    struct Snapshot
    {
        std::vector<std::uintptr_t> properties;
        std::vector<std::uintptr_t> materials;
        std::vector<RE::NiColorA> baseColors;  // the material's baseColor
        std::vector<ColorKeys> modifierKeys;   // every simple colour modifier's keys (Capture only)
        std::vector<bool> grey;                // Recolor.h HasGreyMaterial
        std::vector<bool> colorControlled;     // Recolor.h HasColorController
    };

    struct Inspection
    {
        Snapshot pointers;
        int effectProperties = 0;
        int withPalettePath = 0;     // the material's greyscale path names our palette (Palette.h IsPalettePath)
        int withPalettePointer = 0;  // the material's greyscaleTexture is our palette texture
        int withBoth = 0;
        int withGrey = 0;            // HasGreyMaterial: pointer or path (the contamination test)
        std::vector<std::uintptr_t> colorModifiers;  // BSPSysSimpleColorModifier pointers
        int colorModifiersAllGrey = 0;               // of those, every colour key has R == G == B
        std::vector<ColorKeys> modifierKeys;         // each modifier's colour keys (ParticleColors.h), in order
    };

    /// Game thread. Read-only walk of a_root (null: empty).
    [[nodiscard]] Snapshot Capture(RE::NiAVObject* a_root);
    [[nodiscard]] Inspection Inspect(RE::NiAVObject* a_root);

    /// Game thread. The value of every BSValueNode (AddOnNode) under a_root, in traversal order (v2 E1a)
    [[nodiscard]] std::vector<std::uint32_t> ValueNodeValues(RE::NiAVObject* a_root);

    struct ProjectileRecord
    {
        std::uint64_t seq = 0;
        RE::ObjectRefHandle projectile;
        RE::FormID shooter = 0;
        RE::FormID spell = 0;
        RE::FormID base = 0;      // the projectile's base form (a v2 copy's source when it is a copy)
        RE::FormID copyBase = 0;  // the copy's own id when the base is a v2 copy, else 0
        const char* kind = "";
        bool byPlayer = false;
        const char* link = "none";  // the attribution rule that matched (PlayerLinkName)
        bool recolored = false;     // the projectile layer ran on it
        RecolorStats stats;
        long long recolorMicros = 0;
        std::uint32_t hookThread = 0;
        bool hookOnGameThread = false;
        bool hasLight = false;
        RE::NiColor lightDiffuse{};  // after the recolour
        Snapshot before;  // taken just before the recolour (or instead of it)
        Inspection after;
    };

    struct ExplosionRecord
    {
        std::uint64_t seq = 0;
        RE::FormID base = 0;      // the explosion's base form (a v2 copy's source when it is a copy)
        RE::FormID copyBase = 0;  // the copy's own id when the base is a v2 copy, else 0
        bool byPlayer = false;
        bool recolored = false;
        RecolorStats stats;
        RE::FormID owner = 0;
        RE::FormID causeActor = 0;
        bool hasMagicCaster = false;
        RE::FormID casterSpell = 0;  // runtime magicCaster->currentSpell, 0 when unset
        RE::FormID casterRef = 0;    // magicCaster->GetCasterObjectReference, 0 when none
        const char* link = "none";   // the attribution rule that matched (PlayerLinkName)
        bool lightAtSet3D = false;
        std::uint32_t hookThread = 0;
        Inspection after;
    };

    // A GetHitEffectShader call on a fire effect (HitEffectHook.cpp): swapped for ours (the player's),
    // or kept (someone else cast it)
    struct BurnRecord
    {
        std::uint64_t seq = 0;
        RE::FormID target = 0;
        RE::FormID spell = 0;
        RE::FormID engineShader = 0;  // what the engine asked for
        std::uint32_t thread = 0;     // the hook's thread
        std::uintptr_t controller = 0;
        std::uint16_t effectId = 0;  // the active effect's usUniqueID (with the controller: one hit)
        bool swapped = false;
    };

    // v2: a Projectile::Launch the detour saw (CopySwapLaunch.cpp), while the self-test records
    struct LaunchRecord
    {
        std::uint64_t seq = 0;
        RE::FormID shooter = 0;
        RE::FormID spell = 0;
        RE::FormID base = 0;       // the base the caller asked for
        RE::FormID copyBase = 0;   // the copy launched instead, 0 when not swapped
        RE::FormID explosionBefore = 0;  // the projectile's explosion as Launch set it
        RE::FormID explosionAfter = 0;   // after the post-write (a copy, or unchanged)
        RE::ObjectRefHandle projectile;
        bool playerFire = false;  // the swap rule matched (stoked, the player's, a fire launch)
        std::uint32_t thread = 0;
    };

    // v2: an explosion created through the creation function (42657 / 43829) the detour saw
    struct ExplosionCreateRecord
    {
        std::uint64_t seq = 0;
        RE::FormID baseBefore = 0;
        RE::FormID baseAfter = 0;      // what the creation was handed (a copy when swapped here)
        RE::FormID projectileBase = 0; // the source projectile's base at +0x20, 0 when none
        bool selfArea = false;         // no projectile; the caster at +0x38 decided
        std::uint32_t thread = 0;
    };

    // v2: a TESHitEvent (the damage path still runs with a copy)
    struct HitRecord
    {
        std::uint64_t seq = 0;
        RE::FormID target = 0;
        RE::FormID cause = 0;
        RE::FormID source = 0;      // the spell or weapon
        RE::FormID projectile = 0;  // the projectile base form (a copy's id when it was a copy)
    };

    void RecordLaunch(LaunchRecord a_record);
    void RecordExplosionCreate(ExplosionCreateRecord a_record);
    void RecordHit(HitRecord a_record);
    [[nodiscard]] std::vector<LaunchRecord> LaunchesSince(std::uint64_t a_mark);
    [[nodiscard]] std::vector<ExplosionCreateRecord> ExplosionCreatesSince(std::uint64_t a_mark);
    [[nodiscard]] std::vector<HitRecord> HitsSince(std::uint64_t a_mark);

    void RecordProjectile(ProjectileRecord a_record);
    void RecordExplosion(ExplosionRecord a_record);
    void RecordBurn(BurnRecord a_record);

    [[nodiscard]] std::vector<ProjectileRecord> ProjectilesSince(std::uint64_t a_mark);
    [[nodiscard]] std::vector<ExplosionRecord> ExplosionsSince(std::uint64_t a_mark);
    [[nodiscard]] std::vector<BurnRecord> BurnsSince(std::uint64_t a_mark);
}
