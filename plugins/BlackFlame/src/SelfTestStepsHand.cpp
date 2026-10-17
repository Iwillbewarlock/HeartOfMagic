// =============================================================================
// SELF-TEST - E5b: the readied hand art follows the buff without a re-ready (HandArtRefresh.h)
// =============================================================================
// Runs stoked (after T1), only with selfTest on, on a hand of the player that holds a fire spell whose costliest
// effect's casting art has a copy (right hand first; nothing is equipped: SKIP without one). The hands are drawn
// when they are not (and sheathed again at the end when the step drew them). Judged by data on that hand's
// ActorMagicCaster: castingArt (+0xE0), flag kCastingArtAttached and the refresh counters.
//   1. stoked: castingArt is the copy and attached (by the hooked SetCastingArt or the refresh at T1)
//   2. the buff dispelled: castingArt is the original and attached again, with a refresh counted since the dispel
//   3. the buff cast again: castingArt is the copy and attached again, with a refresh counted since the cast
// The buff is on again afterwards (T8 dispels it), also after a FAIL or the harness's timeout (CleanupHandRefresh). The look is the player's eye check (README in-game checks).

#include "SelfTestCtx.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "Forms.h"
#include "FormCopies.h"
#include "HandArtRefresh.h"
#include "Patch.h"
#include "CopySwap.h"
#include "SelfTestLog.h"
#include "SelfTestWorld.h"

#include <array>
#include <format>

namespace BlackFlame::SelfTest
{
    namespace
    {
        // Calls of the step per phase (one per self-test tick, ~100 ms; paused frames do not tick it): ~5 s, then FAIL with
        // the hand's state, the buff left on and the hands put back (the step's own timeout is the backstop)
        constexpr int kPhaseCalls = 50;

        RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

        struct HandRun
        {
            int slot = -1;
            const RE::MagicItem* spell = nullptr;
            const RE::BGSArtObject* original = nullptr;
            const RE::BGSArtObject* copy = nullptr;
            bool drew = false;       // the step drew the hands: sheathed again at the end
            bool dispelled = false;  // the step dispelled the buff and has not cast it again
            std::uint64_t refreshesAt = 0;  // the refresh counters when the phase began
            std::string offLine;
            int calls = 0;  // in this phase
        };

        HandRun g_run;

        std::uint64_t Refreshes()
        {
            const auto status = GetHandArtRefreshStatus();
            return status.refreshed;
        }

        RE::ActorMagicCaster* CasterOf(int a_slot)
        {
            auto* player = Player();
            return player ? skyrim_cast<RE::ActorMagicCaster*>(player->GetMagicCaster(static_cast<RE::MagicSystem::CastingSource>(a_slot))) :
                            nullptr;
        }

        bool Drawn()
        {
            const auto* state = Player() ? Player()->AsActorState() : nullptr;
            return state && state->GetWeaponState() == RE::WEAPON_STATE::kDrawn;
        }

        std::string Describe(const RE::ActorMagicCaster* a_caster)
        {
            if (!a_caster) {
                return "no caster";
            }
            const auto* art = a_caster->castingArt;
            return std::format("hand {}: castingArt {:08X} ({}), attached {}, caster state {}, drawn {}, stoked {}, refreshes {}", g_run.slot,
                art ? art->GetFormID() : 0, art == g_run.copy ? "the copy" : (art == g_run.original ? "the original" : "other"),
                a_caster->flags.all(RE::ActorMagicCaster::Flags::kCastingArtAttached) ? "yes" : "no",
                static_cast<std::uint32_t>(a_caster->state.get()), Drawn() ? "yes" : "no", IsStoked() ? "yes" : "no", Refreshes());
        }

        // The hand holds a_want attached; with a_refreshed, a refresh was counted since the phase began
        bool Holds(const RE::ActorMagicCaster* a_caster, const RE::BGSArtObject* a_want, bool a_refreshed)
        {
            return a_caster && Drawn() && a_caster->castingArt == a_want &&
                   a_caster->flags.all(RE::ActorMagicCaster::Flags::kCastingArtAttached) && (!a_refreshed || Refreshes() > g_run.refreshesAt);
        }

        bool PickHand()
        {
            auto* player = Player();
            for (const int slot : { RE::Actor::SlotTypes::kRightHand, RE::Actor::SlotTypes::kLeftHand }) {
                const auto* spell = player ? player->GetActorRuntimeData().selectedSpells[slot] : nullptr;
                const auto* art = spell && IsFireSpell(spell) ? CostliestCastingArtOf(spell) : nullptr;
                const auto* copy = art ? CopyOfAs<RE::BGSArtObject>(art) : nullptr;
                if (copy) {
                    g_run.slot = slot;
                    g_run.spell = spell;
                    g_run.original = art;
                    g_run.copy = copy;
                    return true;
                }
            }
            return false;
        }

    }

    void CleanupHandRefresh(bool a_restoreBuff)
    {
        if (a_restoreBuff && g_run.dispelled && !IsStoked() && Player()) {
            (void)SelfTestWorld::Cast(Player(), GetForms().stokeSpell, Player());  // the later steps run stoked
        }
        g_run.dispelled = false;
        if (g_run.drew && Player()) {
            Player()->DrawWeaponMagicHands(false);
        }
        g_run.drew = false;
    }

    bool StepHandRefresh(Ctx& a_c, bool a_first)
    {
        if (a_first) {
            g_run = {};
            const auto status = GetHandArtRefreshStatus();
            if (!GetConfig().refreshCastingArt) {
                SelfTestLog::Skip("E5b", "refreshCastingArt is off (config, or switched off for the session without the main-thread "
                    "queue): " + status.note);
                return true;
            }
            SelfTestLog::Info("E5b", std::format("hand art refresh {} ({})", status.installed ? "on" : "OFF", status.note));
            if (!status.installed) {
                if (GetCopySwapStatus().castingArt.hooked && Patch::IsCodeReadBuild()) {
                    SelfTestLog::Fail("E5b", "the SetCastingArt site is hooked but the refresh is not installed: " + status.note);
                } else {
                    SelfTestLog::Skip("E5b", "not installed here (SE, a build whose code was not read, copyCastingArt off or no art "
                        "copies): " + status.note);
                }
                return true;
            }
            if (!IsStoked()) {
                SelfTestLog::Skip("E5b", "the buff is not on (T1 did not turn it on)");
                return true;
            }
            if (!PickHand()) {
                SelfTestLog::Skip("E5b", "no fire spell whose costliest effect's casting art has a copy in the player's hands (equip one, e.g. "
                    "Flames, to run E5b; the test equips nothing)");
                return true;
            }
            if (!Drawn()) {
                Player()->DrawWeaponMagicHands(true);
                g_run.drew = true;
            }
            a_c.phase = 1;
        }
        auto* caster = CasterOf(g_run.slot);
        if (++g_run.calls > kPhaseCalls) {
            SelfTestLog::Fail("E5b", std::format("phase {} not reached in {} ticks (1 the copy while stoked, 2 the original after the "
                "dispel, 3 the copy after the recast): {}{}", a_c.phase, kPhaseCalls, Describe(caster),
                g_run.offLine.empty() ? std::string() : "; buff off was: " + g_run.offLine));
            CleanupHandRefresh(true);
            return true;
        }
        a_c.waitingFor = std::format("phase {} (spell {:08X}, original {:08X}, copy {:08X}): {}", a_c.phase, g_run.spell->GetFormID(),
            g_run.original->GetFormID(), g_run.copy->GetFormID(), Describe(caster));
        switch (a_c.phase) {
        case 1:
            if (!Holds(caster, g_run.copy, false)) {
                return false;
            }
            g_run.refreshesAt = Refreshes();
            {
                const auto before = Describe(caster);  // read before the dispel (argument order is unspecified)
                const int dispelled = SelfTestWorld::DispelStoke();
                g_run.dispelled = true;
                SelfTestLog::Info("E5b", std::format("stoked: {}; dispelled the buff ({} effect(s))", before, dispelled));
            }
            a_c.phase = 2;
            g_run.calls = 0;
            return false;
        case 2:
            if (IsStoked() || !Holds(caster, g_run.original, true)) {
                return false;
            }
            g_run.offLine = Describe(caster);
            g_run.refreshesAt = Refreshes();
            if (!SelfTestWorld::Cast(Player(), GetForms().stokeSpell, Player())) {
                SelfTestLog::Fail("E5b", "buff off: " + g_run.offLine + "; the Stoke spell could not be cast again");
                CleanupHandRefresh(false);
                return true;
            }
            g_run.dispelled = false;
            a_c.phase = 3;
            g_run.calls = 0;
            return false;
        default:
            if (!IsStoked() || !Holds(caster, g_run.copy, true)) {
                return false;
            }
            SelfTestLog::Pass("E5b", std::format("the drawn hand's art followed the buff without a re-ready: buff off -> the original ({}); "
                "buff on -> the copy ({})", g_run.offLine, Describe(caster)));
            CleanupHandRefresh(false);
            return true;
        }
    }
}
