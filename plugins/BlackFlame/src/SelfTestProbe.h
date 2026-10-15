#pragma once

#include "Common.h"
#include "Recolor.h"

#include <cstdint>
#include <string>
#include <vector>

// =============================================================================
// SELF-TEST PROBE - what the layers saw, for the self-test (SelfTest.h)
// =============================================================================
// Off unless the self-test is running (Recording()): the layers then report each
// fire projectile, fire explosion and burn swap here, with a read-only look at the
// 3D taken on the game thread. The self-test reads the records back by sequence
// number. Every function is safe from any thread (one mutex); the 3D reads
// (Capture, Inspect) are game thread only.

namespace BlackFlame::SelfTestProbe
{
    [[nodiscard]] bool Recording() noexcept;
    void SetRecording(bool a_on) noexcept;

    /// The sequence number the next record will get: records "since a mark" are newer
    [[nodiscard]] std::uint64_t Mark() noexcept;

    // Pointers of a 3D's effect properties and their materials (read only)
    struct Snapshot
    {
        std::vector<std::uintptr_t> properties;
        std::vector<std::uintptr_t> materials;
    };

    struct Inspection
    {
        Snapshot pointers;
        int effectProperties = 0;
        int withPalettePath = 0;     // material's greyscaleTexturePath is our palette path
        int withPalettePointer = 0;  // material's greyscaleTexture is our palette texture
        int withBoth = 0;
        std::vector<std::uintptr_t> colorModifiers;  // BSPSysSimpleColorModifier pointers
        int colorModifiersAllGrey = 0;               // of those, every colour key has R == G == B
        std::vector<float> firstModifierRaw;         // bytes 0x30-0x7F of the first one, as floats
    };

    /// Game thread. Read-only walk of a_root (null: empty).
    [[nodiscard]] Snapshot Capture(RE::NiAVObject* a_root);
    [[nodiscard]] Inspection Inspect(RE::NiAVObject* a_root);

    struct ProjectileRecord
    {
        std::uint64_t seq = 0;
        RE::FormID spell = 0;
        const char* kind = "";
        bool byPlayer = false;
        bool recolored = false;  // the projectile layer ran on it
        RecolorStats stats;
        long long recolorMicros = 0;
        std::uint32_t hookThread = 0;
        bool hookOnGameThread = false;
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

    struct BurnRecord
    {
        std::uint64_t seq = 0;
        RE::FormID target = 0;
        RE::FormID spell = 0;
        RE::FormID engineShader = 0;
        std::string listState;
        int stopped = 0;
        int attempt = 0;
        bool applied = false;
        bool alreadyLive = false;
    };

    void RecordProjectile(ProjectileRecord a_record);
    void RecordExplosion(ExplosionRecord a_record);
    void RecordBurn(BurnRecord a_record);

    [[nodiscard]] std::vector<ProjectileRecord> ProjectilesSince(std::uint64_t a_mark);
    [[nodiscard]] std::vector<ExplosionRecord> ExplosionsSince(std::uint64_t a_mark);
    [[nodiscard]] std::vector<BurnRecord> BurnsSince(std::uint64_t a_mark);
}
