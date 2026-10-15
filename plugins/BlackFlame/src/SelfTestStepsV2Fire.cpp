// =============================================================================
// SELF-TEST v2 STEPS - E1a, E1b, E1c: the player's Firebolt and Fireball take the copies,
// the caster's Fireball keeps the originals (before, after and together with the player's)
// =============================================================================
// Meant to observe: a record that never came is a FAIL, never an INFO.

#include "SelfTestCtx.h"

#include "CopySwap.h"
#include "FormCopies.h"
#include "GreyMap.h"
#include "SelfTestLog.h"
#include "SelfTestText.h"

#include <algorithm>
#include <format>
#include <unordered_set>

namespace BlackFlame::SelfTest
{
    namespace
    {
        using namespace SelfTestText;

        constexpr auto kImpactWait = std::chrono::milliseconds(1500);   // after the launch, for the impact copies
        constexpr auto kSettle = std::chrono::milliseconds(2500);       // an earlier Fireball's explosion has ended
        constexpr auto kRecordWait = std::chrono::milliseconds(4000);   // a phase's records

        RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

        // Game thread only (the self-test); reset by each step's first call
        struct FireState
        {
            std::uint64_t mark = 0;
            CopySwapCounters at{};
            std::optional<SelfTestProbe::LaunchRecord> launch;
            std::optional<SelfTestProbe::ProjectileRecord> projectile;
            int phase = 0;
            Clock::time_point phaseStart{};
        };
        FireState g_fire;

        std::optional<SelfTestProbe::LaunchRecord> FindLaunch(std::uint64_t a_mark, RE::FormID a_shooter, const RE::SpellItem* a_spell)
        {
            for (const auto& r : SelfTestProbe::LaunchesSince(a_mark)) {
                if (r.shooter == a_shooter && a_spell && r.spell == a_spell->GetFormID()) {
                    return r;
                }
            }
            return std::nullopt;
        }

        std::optional<SelfTestProbe::ExplosionRecord> FindExplosion(std::uint64_t a_mark, bool a_byPlayer, RE::FormID a_caster,
            const RE::BGSExplosion* a_expected)
        {
            for (const auto& r : SelfTestProbe::ExplosionsSince(a_mark)) {
                const bool fromCaster = a_caster && (r.owner == a_caster || r.causeActor == a_caster);
                if (a_expected && r.base == a_expected->GetFormID() && (a_byPlayer ? r.byPlayer : (!r.byPlayer && fromCaster))) {
                    return r;
                }
            }
            return std::nullopt;
        }

        std::unordered_set<std::uint32_t> GreyIndices()
        {
            std::unordered_set<std::uint32_t> out;
            for (const auto& addon : GreyAddons()) {
                out.insert(addon.grey);
            }
            return out;
        }

        std::string Values(const std::vector<std::uint32_t>& a_values)
        {
            std::string out;
            for (const auto v : a_values) {
                out += std::format("{}{}", out.empty() ? "" : ",", v);
            }
            return out.empty() ? "none" : out;
        }

        void Reset(FireState& a_state)
        {
            a_state = {};
            a_state.mark = SelfTestProbe::Mark();
            a_state.at = GetCopySwapCounters();
            a_state.phaseStart = Clock::now();
        }

        void JudgeFirebolt(Ctx& a_c)
        {
            const auto now = GetCopySwapCounters();
            const auto impactCopies = now.impactCopies - g_fire.at.impactCopies;
            const auto& launch = g_fire.launch;
            const auto& projectile = g_fire.projectile;
            const auto grey = GreyIndices();
            // From the projectile hook's own walk of the 3D (the record): no 3D then = not inspected = FAIL; no
            // AddOnNode in it = INFO, not a basis for the PASS
            const bool inspected = projectile && projectile->inspected3D;
            const auto& values = projectile ? projectile->valueNodes : std::vector<std::uint32_t>{};
            const bool valuesGrey = std::ranges::all_of(values, [&](std::uint32_t v) { return grey.contains(v); });
            const bool launchOk = launch && launch->copyBase != 0;
            const bool projectileOk = projectile && projectile->copyBase != 0 && projectile->after.withPalettePath > 0;
            SelfTestLog::Info("E1a", std::format("launch: {}; projectile: {}; AddOnNode values in its 3D at the hook: {} ({}); impact "
                "copies since the launch: {} (GetImpactData calls in a copy window {})",
                launch ? std::format("base {:08X} -> copy {:08X}, rule matched {}", launch->base, launch->copyBase, launch->playerFire) :
                         std::string("NOT RECORDED"),
                projectile ? DescribeProjectile(*projectile) : std::string("NOT RECORDED"), Values(values),
                !inspected ? "NOT inspected: no 3D at the hook" :
                values.empty() ? "this NIF has none: the ADDN part is not judged" : (valuesGrey ? "all grey indices" : "SOME NOT GREY"),
                impactCopies, now.impactWindowCalls - g_fire.at.impactWindowCalls));
            SelfTestLog::Check("E1a", launchOk && projectileOk && inspected && valuesGrey && impactCopies >= 1, std::format("player Firebolt: "
                "launched with the copy {}, its 3D grey (palette path {} shapes) {}, AddOnNodes on grey ADDNs {}, impact copies {} "
                "(at least 1)", launchOk ? "yes" : "NO", projectile ? projectile->after.withPalettePath : 0,
                projectileOk ? "yes" : "NO", !inspected ? "NOT INSPECTED" : (values.empty() ? "none (not judged)" : (valuesGrey ? "yes" : "NO")), impactCopies));
            (void)a_c;
        }
    }

    void ShiftFireClocks(Clock::duration a_paused)
    {
        g_fire.phaseStart += a_paused;
    }

    // E1a: the player's Firebolt launches with its projectile copy, its 3D at the projectile hook carries the grey palette path and
    // AddOnNodes on grey indices, and its impact took at least one impact copy
    bool StepE1Firebolt(Ctx& a_c, bool a_first)
    {
        if (a_first) {
            Reset(g_fire);
            if (!NeedActors(a_c, "E1a")) {
                return true;
            }
            return !CastAtTarget(a_c, Player(), a_c.forms.firebolt, "E1a");
        }
        if (!g_fire.launch) {
            g_fire.launch = FindLaunch(g_fire.mark, Player()->GetFormID(), a_c.forms.firebolt);
        }
        if (!g_fire.projectile) {
            g_fire.projectile = FindProjectile(g_fire.mark, true, a_c.forms.firebolt);
        }
        a_c.waitingFor = "the player's Firebolt launch and projectile records";
        const bool impactsDone = GetCopySwapCounters().impactCopies > g_fire.at.impactCopies;
        if ((!g_fire.launch || !g_fire.projectile || !impactsDone) && Clock::now() - g_fire.phaseStart < kImpactWait) {
            return false;
        }
        JudgeFirebolt(a_c);
        return true;
    }

    // E1b: the player's Fireball explosion is the explosion copy; which device made it (Launch post-write or the
    // creation detour) comes from the counters
    bool StepE1Fireball(Ctx& a_c, bool a_first)
    {
        if (a_first) {
            Reset(g_fire);
            if (!NeedActors(a_c, "E1b")) {
                return true;
            }
            return !CastAtTarget(a_c, Player(), a_c.forms.fireball, "E1b");
        }
        const auto record = FindExplosion(g_fire.mark, true, 0, a_c.forms.fireballExplosion);
        if (!record) {
            a_c.waitingFor = "the player's Fireball explosion record";
            return false;  // the step's timeout makes "0 observed" a FAIL
        }
        const auto now = GetCopySwapCounters();
        const auto launch = FindLaunch(g_fire.mark, Player()->GetFormID(), a_c.forms.fireball);
        const auto postWrites = now.launchPostWrites - g_fire.at.launchPostWrites;
        const auto alreadyCopy = now.createAlreadyCopy - g_fire.at.createAlreadyCopy;
        const auto createSwaps = now.createSwapsProjectile - g_fire.at.createSwapsProjectile;
        const char* device = postWrites > 0 && alreadyCopy > 0 ? "the Launch post-write (the creation saw a copy)" :
                             createSwaps > 0                 ? "the explosion creation detour" :
                             postWrites > 0                  ? "the Launch post-write (the creation detour saw nothing)" :
                                                               "neither device counted it";
        SelfTestLog::Info("E1b", std::format("launch: {}; counters since the cast: Launch post-writes {}, creations given a copy "
            "already {}, swapped by the creation detour {}", launch ? std::format("base {:08X} -> copy {:08X}, explosion {:08X} -> "
            "{:08X}", launch->base, launch->copyBase, launch->explosionBefore, launch->explosionAfter) : std::string("NOT RECORDED"),
            postWrites, alreadyCopy, createSwaps));
        SelfTestLog::Check("E1b", record->copyBase != 0 && record->after.withPalettePath > 0, std::format("player Fireball "
            "explosion: base {:08X} copy {:08X}, palette path on {} of {} shapes; made by {} | {}", record->base, record->copyBase,
            record->after.withPalettePath, record->after.effectProperties, device, DescribeExplosion(*record)));
        return true;
    }

    namespace
    {
        // One E1c phase: the caster's projectile and explosion each seen at least once, with original bases and no grey
        void JudgeNpc(Ctx& a_c, const char* a_phase)
        {
            const auto launch = FindLaunch(g_fire.mark, a_c.casterId, a_c.forms.fireball);
            const auto projectile = FindProjectileFrom(g_fire.mark, a_c.casterId, a_c.forms.fireball);
            const auto explosion = FindExplosion(g_fire.mark, false, a_c.casterId, a_c.forms.fireballExplosion);
            const bool launchOk = launch && launch->copyBase == 0 && !launch->playerFire;
            const bool projectileOk = projectile && projectile->copyBase == 0 && projectile->after.withGrey == 0 &&
                                      projectile->after.withPalettePath == 0;
            const bool explosionOk = explosion && explosion->copyBase == 0 && explosion->after.withGrey == 0 &&
                                     explosion->after.withPalettePath == 0;
            SelfTestLog::Check("E1c", launchOk && projectileOk && explosionOk, std::format("{}: caster's launch {}, projectile {}, "
                "explosion {}", a_phase,
                launch ? std::format("base {:08X} copy {:08X} rule {}", launch->base, launch->copyBase, launch->playerFire) :
                         std::string("NOT OBSERVED"),
                projectile ? DescribeProjectile(*projectile) : std::string("NOT OBSERVED"),
                explosion ? DescribeExplosion(*explosion) : std::string("NOT OBSERVED")));
        }

        bool CasterRecordsIn(Ctx& a_c)
        {
            return FindProjectileFrom(g_fire.mark, a_c.casterId, a_c.forms.fireball) &&
                   FindExplosion(g_fire.mark, false, a_c.casterId, a_c.forms.fireballExplosion);
        }
    }

    // E1c: the caster's Fireball before the player's (A), after it (B) and in the same frame (C) keeps the original
    // projectile and explosion, none grey; in C the player's explosion is the copy
    bool StepE1Npc(Ctx& a_c, bool a_first)
    {
        enum : int { kStartA, kWaitA, kPlayerB, kNpcB, kWaitB, kStartC, kWaitC };
        if (a_first) {
            Reset(g_fire);
            if (!NeedActors(a_c, "E1c")) {
                return true;
            }
        }
        const auto since = Clock::now() - g_fire.phaseStart;
        auto* caster = ActorOf(a_c.caster);
        auto* target = RefOf(a_c.dummy);
        const auto next = [&](int a_phase) {
            g_fire.phase = a_phase;
            g_fire.phaseStart = Clock::now();
        };
        switch (g_fire.phase) {
        case kStartA:
            if (a_c.lastCast && Clock::now() - *a_c.lastCast < kSettle) {
                a_c.waitingFor = "the earlier Fireball explosion to end";
                return false;
            }
            SelfTestWorld::FaceTarget(caster, target);
            g_fire.mark = SelfTestProbe::Mark();
            next(kWaitA);
            return !CastAtTarget(a_c, caster, a_c.forms.fireball, "E1c");
        case kWaitA:
            a_c.waitingFor = "phase A: the caster's Fireball records";
            if (!CasterRecordsIn(a_c) && since < kRecordWait) {
                return false;
            }
            JudgeNpc(a_c, "A (the caster first)");
            next(kPlayerB);
            return false;
        case kPlayerB:
            if (a_c.lastCast && Clock::now() - *a_c.lastCast < kSettle) {
                return false;
            }
            next(kNpcB);
            return !CastAtTarget(a_c, Player(), a_c.forms.fireball, "E1c");
        case kNpcB:
            a_c.waitingFor = "phase B: the player's Fireball explosion to end";
            if (since < kSettle) {
                return false;
            }
            SelfTestWorld::FaceTarget(caster, target);
            g_fire.mark = SelfTestProbe::Mark();
            next(kWaitB);
            return !CastAtTarget(a_c, caster, a_c.forms.fireball, "E1c");
        case kWaitB:
            a_c.waitingFor = "phase B: the caster's Fireball records";
            if (!CasterRecordsIn(a_c) && since < kRecordWait) {
                return false;
            }
            JudgeNpc(a_c, "B (after the player's)");
            next(kStartC);
            return false;
        case kStartC:
            if (a_c.lastCast && Clock::now() - *a_c.lastCast < kSettle) {
                return false;
            }
            SelfTestWorld::FaceTarget(caster, target);
            g_fire.mark = SelfTestProbe::Mark();
            next(kWaitC);
            // The caster first, the player in the same frame
            if (!CastAtTarget(a_c, caster, a_c.forms.fireball, "E1c")) {
                return true;
            }
            return !CastAtTarget(a_c, Player(), a_c.forms.fireball, "E1c");
        default:
            {
                a_c.waitingFor = "phase C: both Fireball records";
                const auto player = FindExplosion(g_fire.mark, true, 0, a_c.forms.fireballExplosion);
                if ((!CasterRecordsIn(a_c) || !player) && since < kRecordWait) {
                    return false;
                }
                JudgeNpc(a_c, "C (in the same frame as the player's)");
                SelfTestLog::Check("E1c", player && player->copyBase != 0, std::format("C: the player's explosion in the same "
                    "frame: {}", player ? DescribeExplosion(*player) : std::string("NOT OBSERVED")));
                return true;
            }
        }
    }
}
