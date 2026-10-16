// =============================================================================
// SELF-TEST v2 STEP - E2: a concentration flame stream with the copy, across a buff toggle
// =============================================================================
// The player launches a Flames-type spell (MAG_Flames when its projectile is a flamethrower in this
// load order, else the Flame Atronach's Flames) at a target 400 units ahead every tick: stoked, then
// with the buff dispelled mid-stream, then the buff is cast again. Launch reuses a flame stream only
// for the same base, so the copy starts its own stream and the original another.

#include "SelfTestCtx.h"

#include "BuffState.h"
#include "CopySwap.h"
#include "FormCopies.h"
#include "Forms.h"
#include "SelfTestLog.h"
#include "SelfTestText.h"

#include <format>
#include <set>

namespace BlackFlame::SelfTest
{
    namespace
    {
        using namespace SelfTestText;

        constexpr float kE2TargetForward = 400.0f;  // inside the flame projectile's range (768 in the spike's load order)
        constexpr auto kStream = std::chrono::milliseconds(1000);       // launches while stoked
        constexpr auto kAfterToggle = std::chrono::milliseconds(600);   // launches after the buff went off
        constexpr auto kOffWait = std::chrono::milliseconds(1500);
        constexpr auto kRedispelAfter = std::chrono::milliseconds(500);  // C4: once more when the first did not hold
        constexpr auto kOnWait = std::chrono::milliseconds(2000);
        constexpr auto kHitSettle = std::chrono::milliseconds(500);

        enum Phase : int
        {
            kSpawn,
            kTargetReady,
            kStoked,
            kToggling,
            kAfter,
            kRecast,
            kSettleHits
        };

        struct FlameState
        {
            RE::SpellItem* spell = nullptr;
            std::uint64_t mark = 0;
            std::uint64_t toggleMark = 0;  // the dispel
            std::uint64_t afterMark = 0;   // the flag seen off
            Clock::time_point phaseStart{};
            int launches = 0;
            bool firstLogged = false;
            bool redispelled = false;
        };
        FlameState g_flames;

        RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

        void LaunchOnce(Ctx& a_c)
        {
            auto* target = RefOf(a_c.extraTarget);
            std::string detail;
            if (!target || !SelfTestWorld::LaunchAt(Player(), g_flames.spell, target, detail)) {
                return;
            }
            ++g_flames.launches;
            a_c.lastCast = Clock::now();
            if (!g_flames.firstLogged) {
                g_flames.firstLogged = true;
                SelfTestLog::Info("E2", "first launch: " + detail);
            }
        }

        void Next(int& a_phase, int a_to)
        {
            a_phase = a_to;
            g_flames.phaseStart = Clock::now();
        }

        void Judge(Ctx& a_c)
        {
            const auto playerId = Player()->GetFormID();
            const auto spellId = g_flames.spell->GetFormID();
            const auto* projectileBase = g_flames.spell->GetAVEffect() ? g_flames.spell->GetAVEffect()->data.projectileBase : nullptr;
            const auto* copy = CopyOf(projectileBase);
            int stoked = 0, stokedSwapped = 0, after = 0, afterOriginal = 0;
            std::set<std::uint32_t> stokedStreams, afterStreams;
            for (const auto& r : SelfTestProbe::LaunchesSince(g_flames.mark)) {
                if (r.shooter != playerId || r.spell != spellId) {
                    continue;
                }
                if (r.seq < g_flames.toggleMark) {
                    ++stoked;
                    stokedSwapped += r.copyBase != 0 ? 1 : 0;
                    stokedStreams.insert(r.projectile.native_handle());
                } else if (r.seq >= g_flames.afterMark) {
                    ++after;
                    afterOriginal += r.copyBase == 0 && !r.playerFire ? 1 : 0;
                    afterStreams.insert(r.projectile.native_handle());
                }
            }
            const auto* target = RefOf(a_c.extraTarget);
            int hitsStoked = 0, hitsCopy = 0, hitsAll = 0;
            for (const auto& hit : SelfTestProbe::HitsSince(g_flames.mark)) {
                if (hit.cause != playerId || hit.source != spellId || !target || hit.target != target->GetFormID()) {
                    continue;
                }
                ++hitsAll;
                hitsStoked += hit.seq < g_flames.toggleMark ? 1 : 0;
                hitsCopy += copy && hit.projectile == copy->GetFormID() ? 1 : 0;
            }
            SelfTestLog::Info("E2", std::format("spell {:08X}, projectile {:08X} (copy {:08X}); {} launches; streams (distinct "
                "projectiles) while stoked {}, after the toggle {} - one each means the stream held", spellId,
                projectileBase ? projectileBase->GetFormID() : 0, copy ? copy->GetFormID() : 0, g_flames.launches,
                stokedStreams.size(), afterStreams.size()));
            SelfTestLog::Check("E2", stoked > 0 && stokedSwapped == stoked && afterOriginal > 0 && hitsStoked > 0,
                std::format("stoked launches {} ({} with the copy); after the buff went off {} ({} with the original); hits on "
                    "the target {} ({} while stoked, {} naming the copy as the projectile)", stoked, stokedSwapped, after,
                    afterOriginal, hitsAll, hitsStoked, hitsCopy));
        }
    }

    void ShiftFlameClocks(Clock::duration a_paused)
    {
        g_flames.phaseStart += a_paused;
    }

    // E2: see the top. PASS: every stoked launch took the copy, launches after the dispel took the original, and the
    // damage path still ran with the copy (a TESHitEvent from the player with that spell while stoked)
    bool StepE2Flames(Ctx& a_c, bool a_first)
    {
        if (a_first) {
            g_flames = {};
            a_c.phase = kSpawn;
            std::string detail;
            g_flames.spell = SelfTestWorld::LookupV2(detail).flames;
            if (!g_flames.spell || !a_c.haveForms) {
                SelfTestLog::Fail("E2", "no Flames-type spell (a concentration spell whose projectile is a flamethrower): " + detail);
                return true;
            }
            if (!IsStoked()) {
                SelfTestLog::Fail("E2", "skipped: the buff is not on (T1)");
                return true;
            }
        }
        const auto since = Clock::now() - g_flames.phaseStart;
        switch (a_c.phase) {
        case kSpawn:
            a_c.extraTarget = SelfTestWorld::Spawn(a_c.forms.deer, kE2TargetForward, 0.0f);
            Next(a_c.phase, kTargetReady);
            return false;
        case kTargetReady:
            {
                auto* actor = ActorOf(a_c.extraTarget);
                a_c.waitingFor = "the E2 target's 3D";
                if (!actor || !actor->Is3DLoaded()) {
                    return false;
                }
                SelfTestLog::Info("E2", "target " + SelfTestWorld::MakeTarget(actor));
                g_flames.mark = SelfTestProbe::Mark();
                Next(a_c.phase, kStoked);
                return false;
            }
        case kStoked:
            LaunchOnce(a_c);
            if (since >= kStream) {
                g_flames.toggleMark = SelfTestProbe::Mark();
                SelfTestLog::Info("E2", std::format("mid-stream: dispelled {} Stoke effect(s)", SelfTestWorld::DispelStoke()));
                Next(a_c.phase, kToggling);
            }
            return false;
        case kToggling:
            LaunchOnce(a_c);
            a_c.waitingFor = "the buff flag off after the dispel";
            if (IsStoked() && !g_flames.redispelled && since >= kRedispelAfter) {
                g_flames.redispelled = true;
                const auto state = SelfTestWorld::DescribeStokeEffects();
                if (const int again = SelfTestWorld::DispelStoke(); again > 0) {
                    SelfTestLog::Info("E2", std::format("the dispel did not hold after {} ms ({}): dispelled {} again",
                        kRedispelAfter.count(), state, again));
                }
            }
            if (!IsStoked()) {
                g_flames.afterMark = SelfTestProbe::Mark();
                Next(a_c.phase, kAfter);
            } else if (since >= kOffWait) {
                SelfTestLog::Fail("E2", "the buff flag did not go off after the dispel: " +
                    SelfTestWorld::DescribeStokeEffects());
                return true;
            }
            return false;
        case kAfter:
            LaunchOnce(a_c);
            if (since >= kAfterToggle) {
                (void)SelfTestWorld::Cast(Player(), GetForms().stokeSpell, Player());  // the buff back for the later steps
                Next(a_c.phase, kRecast);
            }
            return false;
        case kRecast:
            a_c.waitingFor = "the buff back on";
            if (!IsStoked() && since < kOnWait) {
                return false;
            }
            if (!IsStoked()) {
                SelfTestLog::Info("E2", std::format("the buff did not come back within {} ms of the Stoke cast: later steps "
                    "that need it may skip", kOnWait.count()));
            }
            Next(a_c.phase, kSettleHits);
            return false;
        default:
            if (since < kHitSettle) {
                return false;
            }
            Judge(a_c);
            SelfTestLog::Info("E2", "target: " + SelfTestWorld::Remove(a_c.extraTarget));
            a_c.extraTarget = {};
            return true;
        }
    }
}
