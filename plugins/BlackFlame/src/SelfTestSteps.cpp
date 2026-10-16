#include "SelfTestCtx.h"

#include "BuffState.h"
#include "Config.h"
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
        constexpr float kBuffOnTimeout = 3.0f;    // a dispel first when already stoked, then the cast
        constexpr float kSpawnTimeout = 1.0f;
        constexpr float kActorsTimeout = 8.0f;   // C3: their 3D, then the target at rest
        constexpr float kProjectileTimeout = 3.0f;
        constexpr float kBurnTimeout = 9.0f;     // C3: up to three casts until one hits, then the watch
        constexpr float kExplosionTimeout = 6.0f;
        constexpr float kInfoTimeout = 3.0f;
        constexpr float kBuffOffTimeout = 1.0f;
        constexpr float kCleanupTimeout = 5.0f;
        constexpr float kRemoteTimeout = 8.0f;     // two marker casts one after the other, then they finish
        constexpr float kE1aTimeout = 8.0f;        // C3: up to three casts until one hits, then 1.5 s for the impact
        constexpr float kE1cTimeout = 30.0f;       // three phases, each waiting for an earlier explosion to end
        constexpr float kE2Timeout = 10.0f;        // spawn, 1 s stream, toggle, 0.6 s, recast, hits
        constexpr float kE3Timeout = 20.0f;        // buff, rune, marker file

        // Placement: the target ahead (C3: close enough to hit, 550 units), the caster level with the player off to
        // the side, all beyond Fireball's explosion radius (320 units in Skyrim.esm; read at runtime, T2 checks it)
        constexpr float kTargetForward = 550.0f;
        constexpr float kRestTolerance = 2.0f;                      // units the target may move at rest
        constexpr float kPlacementSlack = 200.0f;                   // the target may end this much further than planned
        constexpr auto kRestTime = std::chrono::milliseconds(500);  // that long without moving
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
            SelfTestLog::Info("T0", std::format("config: burnShader={} hitArt={} projectile={} explosion={} hazard={} "
                "handArt={} light={} forcePalette={} particleColors={} tintScale={}", cfg.burnShader, cfg.hitArt,
                cfg.projectile, cfg.explosion, cfg.hazard, cfg.handArt, cfg.light, cfg.forcePalette, cfg.particleColors, cfg.tintScale));
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
                SelfTestLog::Check("T0-v2", line.pass, line.text);
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
                a_c.waitingFor = "IsStoked() true and one more Stoke effect after the cast";
                a_c.phase = 3;
                return false;
            case 1:
                if (!IsStoked()) {
                    a_c.phase = 2;
                }
                return false;
            default:
                if (IsStoked() && SelfTestWorld::StokeEffectCount() > a_c.stokeBefore) {
                    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - *a_c.lastCast);
                    a_c.buffWasTurnedOn = true;
                    SelfTestLog::Pass("T1", std::format("buff flag on {} ms after CastSpellImmediate (Stoke effects {} -> "
                        "{})", ms.count(), a_c.stokeBefore, SelfTestWorld::StokeEffectCount()));
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
            if (!a_c.targetRestPos || pos.GetDistance(*a_c.targetRestPos) > kRestTolerance) {
                a_c.targetRestPos = pos;
                a_c.targetStillSince = Clock::now();
            }
            if (Clock::now() - *a_c.targetStillSince < kRestTime) {
                a_c.waitingFor = std::format("the target to come to rest (at ({:.0f}, {:.0f}, {:.0f}), player height {:.0f})", pos.x,
                    pos.y, pos.z, Player()->GetPositionZ());
                return false;
            }
            a_c.targetLastZ = pos.z;
            // The caster's home: its planned spot at the height it stands at now (O-L1 refuted: the caster wanders hundreds
            // to thousands of units with its AI on, so its position at T2 may already be a drift point)
            const auto casterNow = caster->GetPosition();
            const float casterDrift = std::hypot(casterNow.x - a_c.casterSpawn.x, casterNow.y - a_c.casterSpawn.y);
            // E3: its present height only while it is still near the spot; wandered off, the spawn height (it falls from there)
            a_c.casterHome = RE::NiPoint3{ a_c.casterSpawn.x, a_c.casterSpawn.y,
                casterDrift <= kCasterLeash ? casterNow.z : a_c.casterSpawn.z };
            const auto& done = a_c.targetMadeText;
            const float health = SelfTestWorld::HealthOf(dummy);
            const float radius = a_c.forms.fireballExplosion ? a_c.forms.fireballExplosion->data.radius : 0.0f;
            const auto playerPos = Player()->GetPosition();
            const float toPlayer = dummy->GetPosition().GetDistance(playerPos);
            const float toCaster = dummy->GetPosition().GetDistance(caster->GetPosition());
            const bool farEnough = toPlayer >= radius + kRadiusMargin && toCaster >= radius + kRadiusMargin;
            const bool closeEnough = toPlayer <= kTargetForward + kPlacementSlack;  // D3: placed where planned, not slid off
            const bool ok = !dummy->IsAIEnabled() && health >= SelfTestWorld::kDummyHealth * kHealthShare && farEnough &&
                            closeEnough;
            a_c.actorsOk = ok;
            SelfTestLog::Check("T2", ok, std::format("target {}; health {:.0f}; target to player {:.0f}, to caster {:.0f} "
                "(explosion radius {:.0f} + {:.0f}; at most {:.0f} to the player); caster AI {}, {:.0f} units from its planned spot; "
                "target at rest at height {:.0f} (player {:.0f})", done, health, toPlayer, toCaster, radius, kRadiusMargin,
                kTargetForward + kPlacementSlack, caster->IsAIEnabled() ? "on" : "off", casterDrift, pos.z, playerPos.z));
            return true;
        }

        // ---------------------------------------------------------------------
        // T3 and its numeric checks
        // ---------------------------------------------------------------------

        void CheckTint(const SelfTestProbe::ProjectileRecord& a_r)
        {
            int grey = 0;
            int notGrey = 0;
            int lumChecked = 0;
            int lumOff = 0;
            int controlled = 0;
            const auto& after = a_r.after.pointers;
            for (std::size_t i = 0; i < after.properties.size(); ++i) {
                if (!after.grey[i]) {
                    continue;
                }
                const auto& c = after.baseColors[i];
                (Near(c.red, c.green) && Near(c.red, c.blue) ? grey : notGrey)++;
                // The same property before the recolour (and not grey yet): its luminance x tintScale
                const auto& before = a_r.before;
                const auto it = std::ranges::find(before.properties, after.properties[i]);
                if (it == before.properties.end()) {
                    continue;
                }
                const auto j = static_cast<std::size_t>(it - before.properties.begin());
                if (before.grey[j]) {
                    continue;
                }
                if (after.colorControlled[i]) {
                    ++controlled;  // its controller rewrites the colour per frame: no luminance compare
                    continue;
                }
                ++lumChecked;
                lumOff += Near(c.red, Luminance(before.baseColors[j]) * GetConfig().tintScale) ? 0 : 1;
            }
            const auto detail = std::format("recoloured base colours: {} grey (R=G=B), {} not; luminance x tintScale "
                "checked on {}, {} off; {} with a colour controller left out of that compare", grey, notGrey, lumChecked,
                lumOff, controlled);
            if (grey > 0 && lumChecked == 0 && controlled > 0 && notGrey == 0) {
                SelfTestLog::Info("T3-tint", "every comparable shape has a colour controller, nothing to compare: " + detail);
                return;
            }
            SelfTestLog::Check("T3-tint", grey > 0 && notGrey == 0 && lumChecked > 0 && lumOff == 0, detail);
        }

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
            a_c.t3ModifierKeys = r->before.modifierKeys;  // T7: contamination before T4 would show against these
            // C2: a copy is grey by construction: judged by the copy and the palette in its 3D; the recolour checks
            // (T3-pointer, T3-tint) only for a launch without a copy
            if (r->copyBase != 0 || !GetConfig().projectile) {
                const auto verdict = JudgeGrey(r->copyBase, r->recolored, r->stats.Recolored(), a, GetConfig().projectile);
                const bool observed = r->inspected3D;
                a_c.t3Judged = verdict.judged;
                if (verdict.judged) {
                    a_c.t3Recolored = verdict.pass && observed;
                    SelfTestLog::Check("T3", verdict.pass && observed, std::format("{}{} | {}", verdict.how,
                        observed ? "" : "; NOT INSPECTED: no 3D at the hook", DescribeProjectile(*r)));
                } else {
                    SelfTestLog::Info("T3", verdict.how + " | " + DescribeProjectile(*r));
                }
                const char* why = verdict.copy ? "not judged: a copy (grey by construction, the recolour changes nothing)" :
                                                 "not judged: runtime recolour off";
                SelfTestLog::Info("T3-pointer", why);
                SelfTestLog::Info("T3-tint", why);
                return true;
            }
            const bool ok = r->recolored && r->stats.Recolored() > 0 && a.withPalettePath >= r->stats.Recolored();
            a_c.t3Recolored = ok;
            a_c.t3Judged = true;
            SelfTestLog::Check("T3", ok, std::format("{} effect shapes with the palette path of {} | {}", a.withPalettePath,
                a.effectProperties, DescribeProjectile(*r)));
            SelfTestLog::Check("T3-pointer", a.withPalettePath > 0 && a.withBoth == a.withPalettePath, std::format(
                "engine copy kept our texture pointer: {} of {} palette materials", a.withBoth, a.withPalettePath));
            if (GetConfig().forcePalette && a.withPalettePath < a.effectProperties) {
                SelfTestLog::Info("T3", std::format("partial: {} of {} effect shapes recoloured", a.withPalettePath,
                    a.effectProperties));
            }
            CheckTint(*r);  // the light is judged later on a live projectile (T4-light): reading it back here
                            // right after writing it would prove nothing
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
            StepDef{ "T0", kEnvTimeout, &StepEnvironment },
            StepDef{ "T0-hooks", kEnvTimeout, &StepV2Static },
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
            StepDef{ "T7", kEnvTimeout, &StepModifiers },
            StepDef{ "E1a", kE1aTimeout, &StepE1Firebolt },
            StepDef{ "E1b", kExplosionTimeout, &StepE1Fireball },
            StepDef{ "E1c", kE1cTimeout, &StepE1Npc },
            StepDef{ "E2", kE2Timeout, &StepE2Flames },
            StepDef{ "T8", kBuffOffTimeout, &StepBuffOff },
            StepDef{ "T8-cast", kProjectileTimeout, &StepCastUnstoked },
            StepDef{ "T-threads", kEnvTimeout, &StepThreads },
            StepDef{ "T9", kCleanupTimeout, &StepCleanup },
            StepDef{ "E3", kE3Timeout, &Reload::StepE3 },  // after the cleanup: asks the player for a quicksave and quickload
        };
        return kSteps;
    }

}
