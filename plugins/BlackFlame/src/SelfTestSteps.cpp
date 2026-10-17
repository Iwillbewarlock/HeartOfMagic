#include "SelfTestCtx.h"

#include "BuffState.h"
#include "Config.h"
#include "CopyModelProbe.h"
#include "FireSet.h"
#include "FormCopies.h"
#include "Forms.h"
#include "GameThread.h"
#include "Palette.h"
#include "GreyCheck.h"
#include "ImpactSwap.h"
#include "PapyrusWrap.h"
#include "SelfTestJudge.h"
#include "SelfTestLog.h"
#include "SelfTestText.h"
#include "SelfTestReload.h"
#include "Tint.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace BlackFlame::SelfTest
{
    namespace
    {
        // T0-wrap: the native wrappers, no game script needed: the script type holds our proxy and the proxy
        // answers the original's parameter count and types, static, latent and tasklet flags
        void LogWrapperChecks()
        {
            if (!GetConfig().scriptPlaced) {
                SelfTestLog::Info("T0-wrap", "scriptPlaced false: the natives are not wrapped");
                return;
            }
            for (const auto& w : CheckNativeWrappers()) {
                SelfTestLog::Check("T0-wrap", w.bound && w.matches, std::format("{}.{}: bound {}, values copied from the original {} "
                    "({} params, latent {})", w.className, w.functionName, w.bound ? "yes" : "NO", w.matches ? "yes" : "NO",
                    w.params, w.latent));
            }
        }

        using namespace SelfTestText;

        // Step timeouts, counted in unpaused play
        constexpr float kEnvTimeout = 2.0f;
        // J2, W21, X18: T0 waits for the copy model probe to end, so T0-cover counts every drop. The probe always ends by its
        // own budget (20 s of probing in 8 ms slices, one per frame: 2500 frames, about 170 s at the 15 fps measured on the
        // heavy load order, longer below that), and a finished probe ends the wait at once; this cap (unpaused play) only
        // guards a probe that never ends, and the INFO line below says when it was hit
        constexpr auto kProbeWait = std::chrono::seconds(600);
        constexpr float kT0Timeout = 605.0f;                    // kProbeWait and the step itself
        constexpr float kBuffOnTimeout = 6.0f;    // I2: a dispel first when already stoked, flag off, then the cast, flag on
        constexpr auto kOldBuffWait = std::chrono::milliseconds(3000);  // H2: then T1 goes on with the buff it found
        constexpr float kSpawnTimeout = 1.0f;
        constexpr float kActorsTimeout = 10.0f;  // C3: their 3D, then the target at rest (H1: the step itself gives up at 8 s)
        constexpr auto kRestCap = std::chrono::milliseconds(8000);  // H1: then judged by its height alone
        constexpr float kProjectileTimeout = 3.0f;
        constexpr float kBurnTimeout = 9.0f;     // C3: up to three casts until one hits, then the watch
        constexpr float kExplosionTimeout = 6.0f;
        constexpr float kInfoTimeout = 3.0f;
        constexpr float kBuffOffTimeout = 1.0f;
        constexpr float kCleanupTimeout = 5.0f;
        constexpr float kRemoteTimeout = 8.0f;     // two marker casts one after the other, then they finish
        constexpr float kE1aTimeout = 8.0f;        // C3: up to three casts until one hits, then 1.5 s for the impact
        constexpr float kE1cTimeout = 30.0f;       // three phases, each waiting for an earlier explosion to end
        // G2: 2.5 s settle + three casts x HitWait (flight to 1200 units + 0.8 s: 3.8 s at a projectile speed of 400) + 1.5 s
        constexpr float kE1mTimeout = 16.0f;
        constexpr float kEBoundTimeout = 6.0f;     // the bound weapon on the list (3 s at most), the launches, the records
        constexpr float kE6Timeout = 23.0f;        // J3: kWatch (10 s) for the art, the dispel, kWatch for the art to go, and slack
        constexpr float kE2Timeout = 10.0f;        // spawn, 1 s stream, toggle, 0.6 s, recast, hits
        constexpr float kE3Timeout = 20.0f;        // buff, rune, marker file
        constexpr float kE5bTimeout = 30.0f;       // three phases of at most 50 ticks each (the step ends itself first)

        // Placement: the target ahead (C3: close enough to hit, 550 units), the caster level with the player off to
        // the side. I1: wherever the target comes to rest 300..1200 from the player (ground plane) will do; the explosion
        // steps check Fireball's radius (320 units in Skyrim.esm; read at runtime) against the actual distances
        constexpr float kTargetForward = 550.0f;
        constexpr float kRestXY = 5.0f;  // H1: units the target may slide on the ground plane at rest (sub-unit jitter ignored)
        constexpr float kRestZ = 5.0f;   // and up or down (a falling actor moves far more in 0.5 s)
        // I1/J2: the target's rest distance from the player on the ground plane: at least Fireball's explosion radius (read at
        // runtime) + kRadiusMargin, at most this
        constexpr float kMaxTargetDistance = 1200.0f;
        constexpr auto kRestTime = std::chrono::milliseconds(500);  // that long within both
        constexpr float kCasterRight = 700.0f;
        constexpr float kHealthShare = 0.99f;  // the target's health must reach this share of kDummyHealth

        constexpr float kColorTolerance = 0.002f;

        bool Near(float a_lhs, float a_rhs) { return std::fabs(a_lhs - a_rhs) <= kColorTolerance; }

        RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

        // ---------------------------------------------------------------------
        // T0, T1, T2
        // ---------------------------------------------------------------------

        bool StepEnvironment(Ctx& a_c, bool)
        {
            const auto& cfg = GetConfig();
            // J2: T0, T0-cover and T0-bound read the copies after the copy model probe (it runs in slices over frames); waited
            // for up to kProbeWait of unpaused play, then judged as they are
            if (cfg.copyModelProbe != ProbeMode::kOff && !CopyModelProbeDone()) {
                if (Clock::now() - a_c.stepStart < kProbeWait) {
                    a_c.waitingFor = "the copy model probe to finish";
                    return false;
                }
                SelfTestLog::Info("T0", std::format("copy model probe not finished after {} s ({}): the copies are judged as they "
                    "are now", std::chrono::duration_cast<std::chrono::seconds>(kProbeWait).count(), CopyModelProbeStatus()));
            } else {
                SelfTestLog::Info("T0", "copy model probe: " + CopyModelProbeStatus());
            }
            const auto& forms = GetForms();
            const auto counts = GetFireSetCounts();
            std::string missing;
            a_c.haveForms = SelfTestWorld::LookupVanilla(a_c.forms, missing);
            SelfTestLog::Info("T0", std::format("gameThread={}, palette {} ({})", g_gameThreadNativeId.load(),
                PaletteReady() ? "loaded" : "NOT loaded", GetPalettePath() ? GetPalettePath() : "-"));
            SelfTestLog::Info("T0", std::format("forms: burn shader {:08X}, keyword {:08X}, stoke spell {:08X}",
                forms.burnShader ? forms.burnShader->GetFormID() : 0,
                forms.stokedKeyword ? forms.stokedKeyword->GetFormID() : 0,
                forms.stokeSpell ? forms.stokeSpell->GetFormID() : 0));
            SelfTestLog::Info("T0", std::format("fire spells (a fire MGEF) {}; fire effects {}, fire explosions {}, fire "
                "hazards {}; catalog {}: {} tagged fire, {} of them fire by the tag alone", counts.total,
                counts.effects, counts.explosions, counts.hazards, counts.catalogRead ? "read" : "NOT read",
                counts.catalogFire, counts.catalogOnly));
            SelfTestLog::Info("T0", std::format("config: burnShader={} hitArt={} handArt={} boundWeapon={} light={}",
                cfg.burnShader, cfg.hitArt, cfg.handArt, cfg.boundWeapon, cfg.light));
            if (a_c.forms.fireballExplosion) {
                SelfTestLog::Info("T0", std::format("Fireball explosion {:08X} radius {:.0f}",
                    a_c.forms.fireballExplosion->GetFormID(), a_c.forms.fireballExplosion->data.radius));
            }
            LogWrapperChecks();
            for (const auto& line : GreyCheckResults()) {
                SelfTestLog::Check("T0-grey", line.pass, line.text);
            }
            // C5: what the refresh at this load found and re-filled (field by field), then the compare after it
            for (const auto& note : CopyRefreshNotes()) {
                SelfTestLog::Info("T0-v2", "refresh: " + note);
            }
            // v2: every copy against its live source now (fill counts, fields, links); no map read is a FAIL
            for (const auto& line : CheckCopies()) {
                if (line.info) {
                    SelfTestLog::Info("T0-v2", line.text);  // H1: source mesh absent in this load order
                } else {
                    SelfTestLog::Check("T0-v2", line.pass, line.text);
                }
            }
            const bool fireOk = a_c.haveForms && IsFireSpell(a_c.forms.firebolt) && IsFireSpell(a_c.forms.fireball);
            const bool ok = PaletteReady() && forms.burnShader && forms.stokeSpell && forms.stokedKeyword && fireOk;
            SelfTestLog::Check("T0", ok, std::format("palette, ESP forms and vanilla forms{}; Firebolt and Fireball "
                "counted as fire: {}", missing.empty() ? "" : " (missing: " + missing + ")", fireOk ? "yes" : "NO"));
            return true;
        }

        bool StepBuffOn(Ctx& a_c, bool)
        {
            switch (a_c.phase) {
            case 0:
                if (IsStoked()) {
                    SelfTestLog::Info("T1", std::format("the buff was already on: dispelled {} effect(s) first",
                        SelfTestWorld::DispelStoke()));
                    a_c.t1Dispelled = true;
                    a_c.waitingFor = "the old buff to go off before casting";
                    a_c.phase = 1;
                    return false;
                }
                a_c.phase = 2;
                [[fallthrough]];
            case 2:
                a_c.stokeBefore = SelfTestWorld::StokeEffectCount();
                if (!SelfTestWorld::Cast(Player(), GetForms().stokeSpell, Player())) {
                    SelfTestLog::Fail("T1", "could not cast the Stoke spell (no player caster or spell)");
                    return true;
                }
                a_c.lastCast = Clock::now();
                a_c.t1CastAfter = std::chrono::duration_cast<std::chrono::milliseconds>(*a_c.lastCast - a_c.stepStart);
                a_c.waitingFor = "IsStoked() true and one more Stoke effect after the cast";
                a_c.phase = 3;
                return false;
            case 1:
                if (!IsStoked()) {
                    a_c.t1OffAfter = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - a_c.stepStart);
                    a_c.phase = 2;
                    return false;
                }
                if (Clock::now() - a_c.stepStart >= kOldBuffWait) {
                    // H2: the dispel did not take (looked into apart): go on with the buff the player has, so the later
                    // steps run stoked; T1's own cast is not judged, and T8 dispels this buff
                    a_c.buffWasTurnedOn = true;
                    SelfTestLog::Skip("T1", std::format("cast not judged: the old buff did not go off {} ms after the dispel ({}): going on with it; "
                        "T1's cast is not judged this run", kOldBuffWait.count(), SelfTestWorld::DescribeStokeEffects()));
                    return true;
                }
                return false;
            default:
                if (IsStoked() && SelfTestWorld::StokeEffectCount() > a_c.stokeBefore) {
                    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - *a_c.lastCast);
                    a_c.buffWasTurnedOn = true;
                    SelfTestLog::Pass("T1", std::format("buff flag on {} ms after CastSpellImmediate (Stoke effects {} -> "
                        "{}); {}", ms.count(), a_c.stokeBefore, SelfTestWorld::StokeEffectCount(), !a_c.t1Dispelled ?
                        std::string("no old buff to dispel") : std::format("old buff dispelled, flag off after {} ms, cast at {} ms",
                        a_c.t1OffAfter.count(), a_c.t1CastAfter.count())));
                    return true;
                }
                return false;
            }
        }

        bool StepSpawn(Ctx& a_c, bool)
        {
            if (!a_c.haveForms) {
                SelfTestLog::Fail("T2", "skipped: vanilla forms missing");
                return true;
            }
            a_c.dummy = SelfTestWorld::Spawn(a_c.forms.deer, kTargetForward, 0.0f);
            a_c.caster = SelfTestWorld::Spawn(a_c.forms.deer, 0.0f, kCasterRight);
            auto* dummy = RefOf(a_c.dummy);
            auto* caster = RefOf(a_c.caster);
            if (!dummy || !caster) {
                SelfTestLog::Fail("T2", std::format("PlaceObjectAtMe failed (target {}, caster {})", dummy ? "ok" : "null",
                    caster ? "ok" : "null"));
                return true;
            }
            a_c.casterId = caster->GetFormID();
            a_c.casterSpawn = caster->GetPosition();  // the planned spot (the player's height)
            SelfTestLog::Info("T2", std::format("spawned target {:08X} and caster {:08X} (both EncDeer)", dummy->GetFormID(),
                caster->GetFormID()));
            return true;
        }

        bool StepActorsReady(Ctx& a_c, bool)
        {
            auto* dummy = ActorOf(a_c.dummy);
            auto* caster = ActorOf(a_c.caster);
            if (!a_c.dummy || !a_c.caster || !Player()) {
                return true;  // T2 already failed (StepSpawn logged it): no second FAIL
            }
            a_c.waitingFor = "both spawned actors' 3D loaded";
            if (!dummy || !caster || !dummy->Is3DLoaded() || !caster->Is3DLoaded()) {
                return false;
            }
            // The target is made passive at once (D3's "AI off only after rest" refuted: in run 11:11 the target fell
            // 300 units with its AI off, and the caster, AI on, wandered 3,548-8,443 units). The caster keeps its AI (an
            // actor without it may never run its cast).
            if (!a_c.targetMade) {
                a_c.targetMadeText = SelfTestWorld::MakeTarget(dummy);
                a_c.targetMade = true;
            }
            // C3: spawned at the player's height, the target may still be falling: wait until it is at rest - its whole
            // position (D3), not only its height
            const auto pos = dummy->GetPosition();
            const auto now = Clock::now();
            if (!a_c.targetWaitStart) {
                a_c.targetWaitStart = pos;
                a_c.targetZMin = a_c.targetZMax = pos.z;
            }
            a_c.targetMaxDrift = std::max(a_c.targetMaxDrift, std::hypot(pos.x - a_c.targetWaitStart->x, pos.y - a_c.targetWaitStart->y));
            a_c.targetZMin = std::min(a_c.targetZMin, pos.z);
            a_c.targetZMax = std::max(a_c.targetZMax, pos.z);
            // H1: XY and Z apart - a target with its AI off may keep sliding on a slope while its height holds
            if (!a_c.targetXYAnchor || std::hypot(pos.x - a_c.targetXYAnchor->x, pos.y - a_c.targetXYAnchor->y) > kRestXY) {
                a_c.targetXYAnchor = pos;
                a_c.targetXYSince = now;
            }
            if (!a_c.targetZAnchor || std::fabs(pos.z - *a_c.targetZAnchor) > kRestZ) {
                a_c.targetZAnchor = pos.z;
                a_c.targetZSince = now;
            }
            const bool zStill = now - *a_c.targetZSince >= kRestTime;
            const bool atRest = zStill && now - *a_c.targetXYSince >= kRestTime;
            const auto playerPos = Player()->GetPosition();
            const float groundToPlayer = std::hypot(pos.x - playerPos.x, pos.y - playerPos.y);
            const float radius = a_c.forms.fireballExplosion ? a_c.forms.fireballExplosion->data.radius : 0.0f;
            const float minDistance = radius + kRadiusMargin;  // J2: the runtime radius, not a fixed number
            const bool inRange = groundToPlayer >= minDistance && groundToPlayer <= kMaxTargetDistance;
            const auto moved = std::format("moved {:.0f} on the ground plane at most and {:.0f} - {:.0f} in height since the "
                "wait began at ({:.0f}, {:.0f}, {:.0f})", a_c.targetMaxDrift, a_c.targetZMin, a_c.targetZMax, a_c.targetWaitStart->x,
                a_c.targetWaitStart->y, a_c.targetWaitStart->z);
            if (!atRest) {
                if (now - a_c.stepStart < kRestCap) {
                    a_c.waitingFor = std::format("the target to come to rest (at ({:.0f}, {:.0f}, {:.0f}), player height {:.0f}; {})",
                        pos.x, pos.y, pos.z, playerPos.z, moved);
                    return false;
                }
                // H1: at the cap, a target whose height holds will do (it may slide on a slope with its AI off)
                if (!zStill) {
                    SelfTestLog::Fail("T2", std::format("the target did not come to rest in {} ms: at ({:.0f}, {:.0f}, {:.0f}), its "
                        "height still changing; {}", kRestCap.count(), pos.x, pos.y, pos.z, moved));
                    return true;
                }
                SelfTestLog::Info("T2", std::format("the target still slides on the ground plane after {} ms but its height holds "
                    "within {:.0f} units: going on ({})", kRestCap.count(), kRestZ, moved));
            }
            a_c.targetLastZ = pos.z;
            // The caster's home: its planned spot at the height it stands at now (O-L1 refuted: the caster wanders hundreds
            // to thousands of units with its AI on, so its position at T2 may already be a drift point)
            const auto casterNow = caster->GetPosition();
            const float casterDrift = std::hypot(casterNow.x - a_c.casterSpawn.x, casterNow.y - a_c.casterSpawn.y);
            // E3/G2: its present height only while it is still near the spot; wandered off, the target's rest height (a real
            // ground height nearby; the spawn height is the player's)
            a_c.casterHome = RE::NiPoint3{ a_c.casterSpawn.x, a_c.casterSpawn.y,
                casterDrift <= kCasterLeash ? casterNow.z : a_c.targetLastZ };
            const auto& done = a_c.targetMadeText;
            const float health = SelfTestWorld::HealthOf(dummy);
            const float toPlayer = dummy->GetPosition().GetDistance(playerPos);
            const float toCaster = dummy->GetPosition().GetDistance(caster->GetPosition());
            // J2: on the ground plane at least the explosion's reach from the player; the explosion steps check the 3D distances
            const bool ok = !dummy->IsAIEnabled() && health >= SelfTestWorld::kDummyHealth * kHealthShare && inRange;
            a_c.actorsOk = ok;
            SelfTestLog::Check("T2", ok, std::format("target {}; health {:.0f}; target at rest {:.0f} from the player on the ground "
                "plane ({:.0f} - {:.0f}: explosion radius {:.0f} + {:.0f} to the limit; {:.0f} in 3D, planned {:.0f}), {:.0f} from the "
                "caster (checked again by each explosion step); caster AI {}, {:.0f} units from its planned spot; target at rest at "
                "height {:.0f} (player {:.0f}); it {}", done, health, groundToPlayer, minDistance, kMaxTargetDistance, radius,
                kRadiusMargin, toPlayer, kTargetForward, toCaster, caster->IsAIEnabled() ? "on" : "off", casterDrift, pos.z,
                playerPos.z, moved));
            return true;
        }

        // ---------------------------------------------------------------------
        // T3 and its numeric checks
        // ---------------------------------------------------------------------

        bool StepPlayerProjectile(Ctx& a_c, bool a_first)
        {
            if (a_first) {
                if (!NeedActors(a_c, "T3")) {
                    return true;
                }
                a_c.mark = SelfTestProbe::Mark();
                a_c.t3Mark = a_c.mark;
                a_c.swapsAtT3 = ImpactSwapCount();
                a_c.mappedAtT3 = ImpactMappedSeenCount();
                a_c.waitingFor = "the player's Firebolt projectile in the hook";
                return !CastAtTarget(a_c, Player(), a_c.forms.firebolt, "T3");
            }
            const auto r = FindProjectile(a_c.mark, true, a_c.forms.firebolt);
            if (!r) {
                return false;
            }
            const auto& a = r->after;
            a_c.t3Copy = r->copyBase != 0;
            // C2: a copy is grey by construction: judged by the copy and the copies' rule in its 3D. C6: a launch without a
            // copy is not judged (no runtime recolour; T3-pointer and T3-tint went with it)
            const auto verdict = JudgeGrey(r->copyBase, a);
            const bool observed = r->inspected3D;
            a_c.t3Judged = verdict.judged;
            if (verdict.judged) {
                a_c.t3Recolored = verdict.pass && observed;
                SelfTestLog::Check("T3", verdict.pass && observed, std::format("{}{} | {}", verdict.how,
                    observed ? "" : "; NOT INSPECTED: no 3D at the hook", DescribeProjectile(*r)));
            } else {
                SelfTestLog::Info("T3", verdict.how + " | " + DescribeProjectile(*r));
            }
            return true;
        }
    }

    // ---------------------------------------------------------------------
    // Shared helpers (SelfTestCtx.h)
    // ---------------------------------------------------------------------

    bool NeedActors(Ctx& a_c, const char* a_id)
    {
        if (a_c.actorsOk && a_c.haveForms && RefOf(a_c.dummy) && RefOf(a_c.caster)) {
            return true;
        }
        SelfTestLog::Fail(a_id, "skipped: the test actors are gone or T2 did not pass");
        return false;
    }

    bool ExplosionRoom(Ctx& a_c, const char* a_id, bool a_caster)
    {
        auto* target = RefOf(a_c.dummy);
        auto* caster = RefOf(a_c.caster);
        auto* player = Player();
        if (!target || !player || (a_caster && !caster)) {
            return true;  // NeedActors / PrepareCaster report a missing actor
        }
        const float reach = (a_c.forms.fireballExplosion ? a_c.forms.fireballExplosion->data.radius : 0.0f) + kRadiusMargin;
        const auto at = target->GetPosition();
        const float toPlayer = at.GetDistance(player->GetPosition());
        const float toCaster = a_caster ? at.GetDistance(caster->GetPosition()) : 0.0f;
        if (toPlayer >= reach && (!a_caster || toCaster >= reach)) {
            return true;
        }
        SelfTestLog::Skip(a_id, std::format("the target is inside the explosion's reach ({:.0f}: radius + {:.0f}) - {:.0f} "
            "from the player{}", reach, kRadiusMargin, toPlayer, a_caster ? std::format(", {:.0f} from the caster", toCaster) : ""));
        return false;
    }

    bool CastAtTarget(Ctx& a_c, RE::Actor* a_caster, RE::SpellItem* a_spell, const char* a_id)
    {
        auto* target = RefOf(a_c.dummy);
        if (!target) {
            SelfTestLog::Fail(a_id, "the target is gone: nothing cast");
            return false;
        }
        std::string launch;
        if (!SelfTestWorld::LaunchAt(a_caster, a_spell, target, launch)) {
            SelfTestLog::Fail(a_id, "the projectile could not be launched: " + launch);
            return false;
        }
        SelfTestLog::Info(a_id, launch);
        a_c.lastCast = Clock::now();
        return true;
    }

    std::span<const StepDef> Steps()
    {
        // T5 runs right after T3 (before the NPC casts) so the target carries only the player's hit shader
        static constexpr std::array kSteps{
            StepDef{ "T0", kT0Timeout, &StepEnvironment },
            StepDef{ "T0-hooks", kEnvTimeout, &StepV2Static },
            StepDef{ "T0-cover", kEnvTimeout, &StepV2Cover },
            StepDef{ "T0-decal", kEnvTimeout, &StepDecalCover },
            StepDef{ "T0-bound", kEnvTimeout, &StepBoundCover },
            StepDef{ "T0-slots", kEnvTimeout, &StepSlotsT0 },
            StepDef{ "T0-W2", kEnvTimeout, &StepSlotsW2 },
            StepDef{ "T1", kBuffOnTimeout, &StepBuffOn },
            StepDef{ "T2", kSpawnTimeout, &StepSpawn },
            StepDef{ "T2", kActorsTimeout, &StepActorsReady },
            StepDef{ "T3", kProjectileTimeout, &StepPlayerProjectile },
            StepDef{ "T5", kBurnTimeout, &StepPlayerBurn },
            StepDef{ "E4", kInfoTimeout, &StepHitShader },
            StepDef{ "T5-second", kProjectileTimeout, &StepSecondHit },
            StepDef{ "T3-swap", kInfoTimeout, &StepProjectileImpactSwap },
            StepDef{ "T4", kProjectileTimeout, &StepNpcProjectile },
            StepDef{ "T5-npc", kInfoTimeout, &StepNpcBurnInfo },
            StepDef{ "T6", kExplosionTimeout, &StepPlayerExplosion },
            StepDef{ "T6-swap", kInfoTimeout, &StepImpactSwap },
            StepDef{ "T6-npc", kExplosionTimeout, &StepNpcExplosion },
            StepDef{ "T6-npc-swap", kInfoTimeout, &StepNpcImpactSwap },
            StepDef{ "T3-remote", kRemoteTimeout, &StepRemoteCast },
            StepDef{ "E1a", kE1aTimeout, &StepE1Firebolt },
            StepDef{ "E1b", kExplosionTimeout, &StepE1Fireball },
            StepDef{ "E1c", kE1cTimeout, &StepE1Npc },
            StepDef{ "E1m", kE1mTimeout, &StepE1Modded },
            StepDef{ "E2", kE2Timeout, &StepE2Flames },
            StepDef{ "E6", kE6Timeout, &StepE6Player },
            StepDef{ "E6-npc", kE6Timeout, &StepE6Npc },
            StepDef{ "E5", kEnvTimeout, &StepE5 },
            StepDef{ "E5b", kE5bTimeout, &StepHandRefresh, [] { CleanupHandRefresh(true); } },
            StepDef{ "E-bound-launch", kEBoundTimeout, &StepBoundLaunch },
            StepDef{ "T8", kBuffOffTimeout, &StepBuffOff },
            StepDef{ "T8-cast", kProjectileTimeout, &StepCastUnstoked },
            StepDef{ "E-bound-off", kProjectileTimeout, &StepBoundOff },
            StepDef{ "T-threads", kEnvTimeout, &StepThreads },
            StepDef{ "T9", kCleanupTimeout, &StepCleanup },
            StepDef{ "E3", kE3Timeout, &Reload::StepE3 },  // after the cleanup: asks the player for a quicksave and quickload
        };
        return kSteps;
    }

}
