#pragma once

#include "Common.h"
#include "Patch.h"

#include <cstdint>

// =============================================================================
// COPY SWAP (v2 spike S2) - the player's fire launches with the grey copies
// =============================================================================
// Only while stoked, only for a launch that is the player's (LinkToPlayer: shooter,
// blame, confirmed script tag) and fire (IsFireLaunch), and only where a copy exists
// (FormCopies.h). NPC launches keep their forms. Devices, each with its own config
// key and counters so the self-test can tell which one worked:
//   1. Projectile::Launch (SE 42928 / AE 44108) prologue detour: LaunchData
//      projectileBase -> its copy for the call (restored after); after the call the
//      new projectile's explosion (GetProjectileRuntimeData().explosion, set by
//      Launch from the MGEF when the area is above 0, else from the base) -> its copy.
//   2. Explosion creation (SE 42657 / AE 43829) prologue detour: the create data's
//      base (+0x00) -> its copy when its source projectile (+0x20) has a copy as its
//      base, or (self area, no projectile) its MagicCaster (+0x38, an ActorMagicCaster
//      by vtable) is the player's and its spell (+0x28) a fire spell, while stoked.
//      Restored after the call. Offsets read on 1.7.104 code (unwind-equal on
//      1.6.1170, unverified on SE): every pointer is checked (form type, vtable)
//      before use, so a different layout swaps nothing.
//   3. Impacts: inside a copy window - opened around SpawnCollisionEffects (the impact
//      call sites, ImpactSwap.cpp) when the projectile's base is a copy, and around
//      Explosion::Update when the explosion's base is a copy - the E8 calls to
//      BGSImpactDataSet's GetImpactData (SE 20408 / AE 20860) inside
//      SpawnCollisionEffects (43022 / 44213) and the explosion's impact function
//      (42679 / 43851) return the impact copy for the impact they would return. No
//      IPDS copy and no hash map insert. An impact copy's hazard is the hazard copy.

namespace BlackFlame
{
    /// RAII: a copy window on this thread (saved and restored: the innermost scope decides)
    class CopyWindow
    {
    public:
        explicit CopyWindow(bool a_open) noexcept;
        ~CopyWindow();
        CopyWindow(const CopyWindow&) = delete;
        CopyWindow& operator=(const CopyWindow&) = delete;

    private:
        bool m_previous;
    };

    [[nodiscard]] bool CopyWindowOpen() noexcept;

    /// The launch counts as fire: a spell decides alone (a fire spell by the FireSet rule, kSpell only); with no
    /// spell, a bound fire weapon of the player's as the weapon source (its arrows). An enchantment alone does not
    /// count (staffs, scrolls and enchantments are excluded by the kSpell rule). Any thread.
    [[nodiscard]] bool IsFireLaunch(const RE::MagicItem* a_spell, const RE::EnchantmentItem* a_enchantment,
        const RE::TESObjectWEAP* a_weapon);

    /// kDataLoaded, after LoadAndFillCopies and the fire sets: the Launch and explosion creation detours and the
    /// GetImpactData call sites (only when copies exist; each by its config key)
    void InstallCopySwaps();

    struct CopySwapStatus
    {
        bool copies = false;  // the copies map had any copy
        Patch::Detour launch;
        Patch::Detour explosionCreate;
        int impactSites = 0;    // GetImpactData call sites hooked (of 2)
        int impactChained = 0;  // of those, onto another plugin's redirect
        std::string impactNote;
    };

    [[nodiscard]] const CopySwapStatus& GetCopySwapStatus() noexcept;

    struct CopySwapCounters
    {
        std::uint64_t launchCalls = 0;        // Launch calls through the detour
        std::uint64_t launchPlayerFire = 0;   // of those, the swap rule matched
        std::uint64_t launchSwaps = 0;        // projectileBase swapped for its copy
        std::uint64_t launchNoCopy = 0;       // the rule matched but the base has no copy (stays orange)
        std::uint64_t launchPostWrites = 0;   // the projectile's explosion set to its copy after Launch
        std::uint64_t createCalls = 0;        // explosion creations through the detour
        std::uint64_t createAlreadyCopy = 0;  // the base was a copy already (Launch post-write or a copy PROJ's link)
        std::uint64_t createSwapsProjectile = 0;  // swapped here: the source projectile's base is a copy
        std::uint64_t createSwapsSelfArea = 0;    // swapped here: the player's self-area cast
        std::uint64_t createLayoutRejected = 0;   // a pointer in the create data failed its check
        std::uint64_t createNotSpell = 0;         // the self-area spell slot held another MagicItem (valid, not judged)
        std::uint64_t impactWindowCalls = 0;  // GetImpactData calls inside a copy window
        std::uint64_t impactCopies = 0;       // of those, the impact copy returned
    };

    [[nodiscard]] CopySwapCounters GetCopySwapCounters() noexcept;

    struct SyntheticImpact
    {
        bool ran = false;            // a hooked site and an impact with a copy in the set were there
        bool closedOriginal = false; // window closed: the engine's impact came back
        bool openCopy = false;       // window open: its copy came back
        std::string detail;
    };

    /// Self-test T0-v2: calls the GetImpactData thunk of a hooked site directly (through the engine's own lookup)
    /// with the copy window closed, then open, for the first impact of a_set that has a copy. Counted once in the
    /// impact counters. Game thread.
    [[nodiscard]] SyntheticImpact RunSyntheticImpactCheck(const RE::BGSImpactDataSet* a_set);

    // Counters and recording for the two detours (CopySwapLaunch.cpp) and the impact sites (CopySwapImpact.cpp)
    namespace CopySwapDetail
    {
        void InstallLaunchDetours(CopySwapStatus& a_status);
        void InstallImpactSites(CopySwapStatus& a_status);
        void AddLaunchCounters(CopySwapCounters& a_out) noexcept;
        void AddImpactCounters(CopySwapCounters& a_out) noexcept;
    }
}
