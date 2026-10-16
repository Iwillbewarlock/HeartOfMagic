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
//   5. Casting art (CopySwapArt.cpp): the one E8 to ActorMagicCaster::SetCastingArt
//      (SE 33369 / AE 34150, in SE 37830 / AE 38784): the art becomes its copy when the
//      caster is the player's, a hand, the hand's selected spell is fire, and the art is
//      that spell's costliest effect's castingArt (PickCastingArtImpl).
//   6. Hazard creation (CopySwapHazard.cpp, SE 42786 / AE 43954): the create data's base
//      to its copy inside a copy window, or from the player's fire spawn-hazard effect
//      (SE 33580 / AE 34360, whose prologue opens a thread-local window).

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
        struct CallSite
        {
            bool hooked = false;
            bool chained = false;
            std::uintptr_t offset = 0;  // in its caller
            std::string note;
        } castingArt;
        Patch::Detour hazardCreate;
        Patch::Detour spawnHazard;
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
        std::uint64_t castingArtCalls = 0;    // SetCastingArt calls through the site
        std::uint64_t castingArtSwaps = 0;    // of those, the art's copy handed on
        std::uint64_t castingArtRejected = 0; // a caster or art that failed its vtable check
        std::uint64_t hazardCreateCalls = 0;
        std::uint64_t hazardSwapsWindow = 0;  // swapped inside a copy window
        std::uint64_t hazardSwapsSpawn = 0;   // swapped from the player's spawn-hazard effect
        std::uint64_t hazardCreateRejected = 0;  // a create data pointer failed its check
        std::uint64_t spawnWindowCalls = 0;   // spawn-hazard effect calls through its detour
        std::uint64_t spawnWindowOther = 0;   // of those, not a SpawnHazardEffect by vtable (no window)
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

    /// E5: the casting art SetCastingArt should get - a_art's copy when a_actor is the player, a_slot a hand, a_spell (that
    /// hand's selected spell) a fire spell whose costliest effect's castingArt is a_art, while a_stoked and a copy exists;
    /// else a_art. No engine state is read or changed (the self-test calls it directly).
    [[nodiscard]] RE::BGSArtObject* PickCastingArtImpl(const RE::Actor* a_actor, int a_slot, const RE::MagicItem* a_spell,
        RE::BGSArtObject* a_art, bool a_stoked);

    // Counters and recording for the detours (CopySwapLaunch.cpp, CopySwapHazard.cpp) and the call sites (CopySwapImpact.cpp,
    // CopySwapArt.cpp)
    namespace CopySwapDetail
    {
        /// F1: why a device is off on SE: its SE id is an attempted AE->SE match, never derived from SE code
        inline constexpr const char* kSeNotVerified = "SE: function identity not verified";

        /// F1: records a_name as skipped on SE; in testerMode also the first 32 bytes at the SE candidate a_seId (read
        /// only, no hook) so its identity can be checked offline later (CopySwapLaunch.cpp)
        void RecordSeCandidate(const char* a_name, std::uint64_t a_seId);

        /// F3: *a_slot holds a_to for this scope and gets its old value back however the scope ends (no swap when a_to is null)
        template <class T>
        class SlotSwap
        {
        public:
            SlotSwap(T** a_slot, T* a_to) noexcept : m_slot(a_to ? a_slot : nullptr), m_before(a_to ? *a_slot : nullptr)
            {
                if (m_slot) {
                    *m_slot = a_to;
                }
            }
            ~SlotSwap()
            {
                if (m_slot) {
                    *m_slot = m_before;
                }
            }
            SlotSwap(const SlotSwap&) = delete;
            SlotSwap& operator=(const SlotSwap&) = delete;

        private:
            T** m_slot;
            T* m_before;
        };

        void InstallLaunchDetours(CopySwapStatus& a_status);
        void InstallImpactSites(CopySwapStatus& a_status);
        void InstallArtSite(CopySwapStatus& a_status);
        void InstallHazardDetours(CopySwapStatus& a_status);
        void AddLaunchCounters(CopySwapCounters& a_out) noexcept;
        void AddImpactCounters(CopySwapCounters& a_out) noexcept;
        void AddArtCounters(CopySwapCounters& a_out) noexcept;
        void AddHazardCounters(CopySwapCounters& a_out) noexcept;
    }
}
