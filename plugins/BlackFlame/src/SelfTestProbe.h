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

    struct ProjectileRecord
    {
        std::uint64_t seq = 0;
        RE::ObjectRefHandle projectile;
        RE::FormID shooter = 0;
        RE::FormID spell = 0;
        const char* kind = "";
        bool byPlayer = false;
        bool recolored = false;  // the projectile layer ran on it
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
        RE::FormID base = 0;
        bool byPlayer = false;
        bool recolored = false;
        RecolorStats stats;
        RE::FormID owner = 0;
        RE::FormID causeActor = 0;
        bool hasMagicCaster = false;
        RE::FormID casterSpell = 0;  // runtime magicCaster->currentSpell, 0 when unset
        bool lightAtSet3D = false;
        std::uint32_t hookThread = 0;
        Inspection after;
    };

    // A hit shader the GetHitEffectShader hook swapped for ours (HitEffectHook.cpp)
    struct BurnRecord
    {
        std::uint64_t seq = 0;
        RE::FormID target = 0;
        RE::FormID spell = 0;
        RE::FormID engineShader = 0;  // what the engine asked for
        std::uint32_t thread = 0;     // the hook's thread
    };

    void RecordProjectile(ProjectileRecord a_record);
    void RecordExplosion(ExplosionRecord a_record);
    void RecordBurn(BurnRecord a_record);

    [[nodiscard]] std::vector<ProjectileRecord> ProjectilesSince(std::uint64_t a_mark);
    [[nodiscard]] std::vector<ExplosionRecord> ExplosionsSince(std::uint64_t a_mark);
    [[nodiscard]] std::vector<BurnRecord> BurnsSince(std::uint64_t a_mark);
}
