#include "SelfTestCtx.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "Forms.h"
#include "GameThread.h"
#include "Palette.h"
#include "SelfTestLog.h"
#include "SelfTestText.h"
#include "Tint.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace BlackFlame::SelfTest
{
    namespace
    {
        using namespace SelfTestText;

        // Step timeouts, counted in unpaused play
        constexpr float kEnvTimeout = 2.0f;
        constexpr float kBuffOnTimeout = 3.0f;    // a dispel first when already stoked, then the cast
        constexpr float kSpawnTimeout = 1.0f;
        constexpr float kActorsTimeout = 5.0f;
        constexpr float kProjectileTimeout = 3.0f;
        constexpr float kBurnTimeout = 4.0f;
        constexpr float kExplosionTimeout = 6.0f;
        constexpr float kInfoTimeout = 3.0f;
        constexpr float kBuffOffTimeout = 1.0f;
        constexpr float kCleanupTimeout = 5.0f;

        // Placement: the target well ahead, the caster level with the player off to the side,
        // all beyond Fireball's explosion radius (320 units in Skyrim.esm; read at runtime)
        constexpr float kTargetForward = 1000.0f;
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
                "hazards {}; catalog {}: {} tagged fire, {} of them without a fire effect (left)", counts.total,
                counts.effects, counts.explosions, counts.hazards, counts.catalogRead ? "read" : "NOT read",
                counts.catalogFire, counts.catalogOnly));
            SelfTestLog::Info("T0", std::format("config: burnShader={} hitArt={} projectile={} explosion={} hazard={} "
                "handArt={} light={} forcePalette={} particleColors={} tintScale={}", cfg.burnShader, cfg.hitArt,
                cfg.projectile, cfg.explosion, cfg.hazard, cfg.handArt, cfg.light, cfg.forcePalette, cfg.particleColors, cfg.tintScale));
            if (a_c.forms.fireballExplosion) {
                SelfTestLog::Info("T0", std::format("Fireball explosion {:08X} radius {:.0f}",
                    a_c.forms.fireballExplosion->GetFormID(), a_c.forms.fireballExplosion->data.radius));
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
            // The caster keeps its AI (an actor without it may never run its cast)
            const auto done = SelfTestWorld::MakeTarget(dummy);
            const float health = SelfTestWorld::HealthOf(dummy);
            const float radius = a_c.forms.fireballExplosion ? a_c.forms.fireballExplosion->data.radius : 0.0f;
            const auto playerPos = Player()->GetPosition();
            const float toPlayer = dummy->GetPosition().GetDistance(playerPos);
            const float toCaster = dummy->GetPosition().GetDistance(caster->GetPosition());
            const bool farEnough = toPlayer >= radius + kRadiusMargin && toCaster >= radius + kRadiusMargin;
            const bool ok = !dummy->IsAIEnabled() && health >= SelfTestWorld::kDummyHealth * kHealthShare && farEnough;
            a_c.actorsOk = ok;
            SelfTestLog::Check("T2", ok, std::format("target {}; health {:.0f}; target to player {:.0f}, to caster {:.0f} "
                "(explosion radius {:.0f} + {:.0f}); caster AI {}", done, health, toPlayer, toCaster, radius, kRadiusMargin,
                caster->IsAIEnabled() ? "on" : "off"));
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
                a_c.waitingFor = "the player's Firebolt projectile in the hook";
                return !CastAtTarget(a_c, Player(), a_c.forms.firebolt, "T3");
            }
            const auto r = FindProjectile(a_c.mark, true, a_c.forms.firebolt);
            if (!r) {
                return false;
            }
            const auto& a = r->after;
            const bool ok = r->recolored && r->stats.Recolored() > 0 && a.withPalettePath >= r->stats.Recolored();
            a_c.t3Recolored = ok;
            a_c.t3ModifierKeys = r->before.modifierKeys;  // T7: contamination before T4 would show against these
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
            StepDef{ "T1", kBuffOnTimeout, &StepBuffOn },
            StepDef{ "T2", kSpawnTimeout, &StepSpawn },
            StepDef{ "T2", kActorsTimeout, &StepActorsReady },
            StepDef{ "T3", kProjectileTimeout, &StepPlayerProjectile },
            StepDef{ "T5", kBurnTimeout, &StepPlayerBurn },
            StepDef{ "T5-second", kProjectileTimeout, &StepSecondHit },
            StepDef{ "T4", kProjectileTimeout, &StepNpcProjectile },
            StepDef{ "T5-npc", kInfoTimeout, &StepNpcBurnInfo },
            StepDef{ "T6", kExplosionTimeout, &StepPlayerExplosion },
            StepDef{ "T6-npc", kExplosionTimeout, &StepNpcExplosion },
            StepDef{ "T3-remote", kProjectileTimeout, &StepRemoteCast },
            StepDef{ "T7", kEnvTimeout, &StepModifiers },
            StepDef{ "T8", kBuffOffTimeout, &StepBuffOff },
            StepDef{ "T8-cast", kProjectileTimeout, &StepCastUnstoked },
            StepDef{ "T9", kCleanupTimeout, &StepCleanup },
        };
        return kSteps;
    }

}
