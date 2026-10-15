#include "SelfTest.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "Forms.h"
#include "GameThread.h"
#include "Palette.h"
#include "SelfTestLog.h"
#include "SelfTestProbe.h"
#include "SelfTestText.h"
#include "SelfTestWorld.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace BlackFlame::SelfTest
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        using ProjectileRecord = SelfTestProbe::ProjectileRecord;
        using ExplosionRecord = SelfTestProbe::ExplosionRecord;
        using namespace SelfTestText;

        constexpr auto kReadyDelay = std::chrono::seconds(3);
        constexpr auto kNpcBurnWait = std::chrono::milliseconds(1500);
        constexpr float kDummyForward = 350.0f;  // units ahead of the player (player Z kept: flat ground)
        constexpr float kCasterForward = 150.0f;
        constexpr float kCasterRight = 200.0f;

        std::atomic<bool> g_active{ false };  // armed or running
        bool g_armed = false;                 // game thread only, from here on
        bool g_ran = false;                   // once per game session

        struct Ctx
        {
            std::size_t index = 0;
            bool running = false;
            bool entered = false;
            Clock::time_point stepStart;
            std::optional<Clock::time_point> readySince;
            std::string waitingFor;

            SelfTestWorld::VanillaForms forms;
            bool haveForms = false;
            RE::ObjectRefHandle dummy;
            RE::ObjectRefHandle caster;
            bool actorsReady = false;
            bool npcCastCalled = false;
            std::uint64_t mark = 0;
            std::optional<ProjectileRecord> playerT4;
            std::optional<ProjectileRecord> npcT4;
        };
        Ctx g_ctx;  // game thread only

        bool NeedActors(Ctx& a_c, const char* a_id)
        {
            if (a_c.actorsReady && a_c.haveForms) {
                return true;
            }
            SelfTestLog::Fail(a_id, "skipped: the test actors or vanilla forms are missing (see T2/T0)");
            return false;
        }

        // ---------------------------------------------------------------------
        // Steps: run(ctx, first) returns true when the step is finished
        // ---------------------------------------------------------------------

        bool StepEnvironment(Ctx& a_c, bool)
        {
            const auto& cfg = GetConfig();
            const auto& forms = GetForms();
            const auto counts = GetFireSetCounts();
            std::string missing;
            a_c.haveForms = SelfTestWorld::LookupVanilla(a_c.forms, missing);
            SelfTestLog::Info("T0", std::format("game thread {}, palette {} ({})", g_gameThreadNativeId.load(),
                PaletteReady() ? "loaded" : "NOT loaded", GetPalettePath() ? GetPalettePath() : "-"));
            SelfTestLog::Info("T0", std::format("forms: burn shader {:08X}, keyword {:08X}, stoke spell {:08X}",
                forms.burnShader ? forms.burnShader->GetFormID() : 0, forms.stokedKeyword ? forms.stokedKeyword->GetFormID() : 0,
                forms.stokeSpell ? forms.stokeSpell->GetFormID() : 0));
            SelfTestLog::Info("T0", std::format("fire spells: catalog {} ({}), keyword {}, total {}; fire effects {}, fire explosions {}",
                counts.catalogFire, counts.catalogRead ? "read" : "NOT read", counts.keywordFire, counts.total,
                counts.effects, counts.explosions));
            SelfTestLog::Info("T0", std::format("config: burnShader={} burnMode={} projectile={} explosion={} handArt={} "
                "light={} forcePalette={} particleColors={} tintScale={}", cfg.burnShader, BurnModeName(cfg.burnMode),
                cfg.projectile, cfg.explosion, cfg.handArt, cfg.light, cfg.forcePalette, cfg.particleColors, cfg.tintScale));
            const bool fireOk = a_c.haveForms && IsFireSpell(a_c.forms.firebolt) && IsFireSpell(a_c.forms.fireball);
            const bool ok = PaletteReady() && forms.burnShader && forms.stokeSpell && forms.stokedKeyword && fireOk;
            SelfTestLog::Check("T0", ok, std::format("palette, ESP forms and vanilla forms{}; Firebolt and Fireball "
                "counted as fire: {}", missing.empty() ? "" : " (missing: " + missing + ")", fireOk ? "yes" : "NO"));
            return true;
        }

        bool StepBuffOn(Ctx& a_c, bool a_first)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (a_first) {
                a_c.waitingFor = "IsStoked() true after casting the Stoke spell on the player";
                if (!SelfTestWorld::Cast(player, GetForms().stokeSpell, player)) {
                    SelfTestLog::Fail("T1", "could not cast the Stoke spell (no player caster or spell)");
                    return true;
                }
                return false;
            }
            if (IsStoked()) {
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - a_c.stepStart);
                SelfTestLog::Pass("T1", std::format("buff flag on {} ms after CastSpellImmediate", ms.count()));
                return true;
            }
            return false;
        }

        bool StepSpawn(Ctx& a_c, bool)
        {
            if (!a_c.haveForms) {
                SelfTestLog::Fail("T2", "skipped: vanilla forms missing");
                return true;
            }
            a_c.dummy = SelfTestWorld::Spawn(a_c.forms.dummy, kDummyForward, 0.0f);
            a_c.caster = SelfTestWorld::Spawn(a_c.forms.caster, kCasterForward, kCasterRight);
            if (!a_c.dummy || !a_c.caster) {
                SelfTestLog::Fail("T2", std::format("PlaceObjectAtMe failed (dummy {}, caster {})", a_c.dummy ? "ok" : "null",
                    a_c.caster ? "ok" : "null"));
                return true;
            }
            SelfTestLog::Info("T2", std::format("spawned dummy {:08X} (EncDeer) and caster {:08X} (WEFarmerFemale)",
                RefOf(a_c.dummy)->GetFormID(), RefOf(a_c.caster)->GetFormID()));
            return true;
        }

        bool StepActorsReady(Ctx& a_c, bool a_first)
        {
            if (!a_c.dummy || !a_c.caster) {
                return true;  // T2 already failed
            }
            if (a_first) {
                a_c.waitingFor = "both spawned actors' 3D loaded";
            }
            auto* dummy = ActorOf(a_c.dummy);
            auto* caster = ActorOf(a_c.caster);
            if (!dummy || !caster || !dummy->Is3DLoaded() || !caster->Is3DLoaded()) {
                return false;
            }
            // The caster keeps its AI (an actor without it may never run its cast); it is
            // unaggressive and cowardly from Skyrim.esm and may flee once the target is hit
            const auto dummyDone = SelfTestWorld::MakePassive(dummy, true);
            const auto dummyPos = dummy->GetPosition();
            const auto playerPos = RE::PlayerCharacter::GetSingleton()->GetPosition();
            a_c.actorsReady = true;
            SelfTestLog::Pass("T2", std::format("actors ready: dummy {} at ({:.0f}, {:.0f}, {:.0f}), player at ({:.0f}, "
                "{:.0f}, {:.0f}); caster AI {}", dummyDone, dummyPos.x, dummyPos.y, dummyPos.z, playerPos.x, playerPos.y,
                playerPos.z, caster->IsAIEnabled() ? "on" : "off"));
            return true;
        }

        bool StepPlayerProjectile(Ctx& a_c, bool a_first)
        {
            if (a_first) {
                if (!NeedActors(a_c, "T3")) {
                    return true;
                }
                a_c.waitingFor = "the player's Firebolt projectile in the hook";
                a_c.mark = SelfTestProbe::Mark();
                (void)SelfTestWorld::Cast(RE::PlayerCharacter::GetSingleton(), a_c.forms.firebolt, RefOf(a_c.dummy));
                return false;
            }
            const auto r = FindProjectile(a_c.mark, true, a_c.forms.firebolt);
            if (!r) {
                return false;
            }
            // The palette path marks a recoloured material; whether the property's (engine-made)
            // copy also kept our texture pointer is a separate question, checked apart
            const bool ok = r->recolored && r->stats.Recolored() > 0 && r->after.withPalettePath >= r->stats.Recolored();
            SelfTestLog::Check("T3", ok, DescribeProjectile(*r));
            if (r->after.withPalettePath > 0) {
                SelfTestLog::Check("T3-pointer", r->after.withBoth == r->after.withPalettePath, std::format(
                    "engine copy kept our texture pointer: {} of {} palette materials", r->after.withBoth,
                    r->after.withPalettePath));
            }
            return true;
        }

        bool StepPlayerBurn(Ctx& a_c, bool a_first)
        {
            if (a_first && !NeedActors(a_c, "T5")) {
                return true;
            }
            const auto* ours = GetForms().burnShader;
            const auto* engine = EngineShaderOf(a_c.forms.firebolt);
            const auto shaders = SelfTestWorld::ShadersOn(a_c.dummy);
            const auto live = [&](const RE::TESEffectShader* a_shader) {
                return a_shader && std::ranges::any_of(shaders, [&](const auto& s) {
                    return !s.finished && s.effectData == a_shader->GetFormID();
                });
            };
            a_c.waitingFor = std::format("a live grey shader on the dummy; shaders seen: {}; burns: {}",
                DescribeShaders(shaders), DescribeBurns(a_c.mark));
            if (!live(ours)) {
                return false;
            }
            const bool replace = GetConfig().burnMode == BurnMode::kReplace;
            const bool engineLive = live(engine);
            SelfTestLog::Check("T5", !replace || !engineLive, std::format("grey shader live on the dummy [{}]; engine "
                "hit shader {:08X} {}; shaders: {}; burns: {}", BurnModeName(GetConfig().burnMode),
                engine ? engine->GetFormID() : 0, engineLive ? "still live" : "not live", DescribeShaders(shaders),
                DescribeBurns(a_c.mark)));
            return true;
        }

        // A second hit while the grey shader is live extends it instead of stacking a second one
        bool StepBurnExtend(Ctx& a_c, bool a_first)
        {
            if (a_first) {
                if (!NeedActors(a_c, "T5-extend")) {
                    return true;
                }
                a_c.waitingFor = "a burn record for a second Firebolt hit";
                a_c.mark = SelfTestProbe::Mark();
                (void)SelfTestWorld::Cast(RE::PlayerCharacter::GetSingleton(), a_c.forms.firebolt, RefOf(a_c.dummy));
                return false;
            }
            const auto burns = SelfTestProbe::BurnsSince(a_c.mark);
            const auto it = std::ranges::find_if(burns, [](const auto& b) { return b.applied || b.alreadyLive; });
            if (it == burns.end()) {
                a_c.waitingFor = std::format("a burn record for a second Firebolt hit (seen: {})", DescribeBurns(a_c.mark));
                return false;
            }
            const auto* ours = GetForms().burnShader;
            const auto shaders = SelfTestWorld::ShadersOn(a_c.dummy);
            const auto liveGrey = std::ranges::count_if(shaders, [&](const auto& s) {
                return ours && !s.finished && s.effectData == ours->GetFormID();
            });
            SelfTestLog::Check("T5-extend", it->alreadyLive && liveGrey == 1, std::format("second hit {} the grey shader; "
                "live grey shaders on the target: {}; burns: {}", it->alreadyLive ? "extended" : "applied a new",
                liveGrey, DescribeBurns(a_c.mark)));
            return true;
        }

        bool StepNpcProjectile(Ctx& a_c, bool a_first)
        {
            if (a_first) {
                if (!NeedActors(a_c, "T4")) {
                    return true;
                }
                // The player casts too, in the same frame: two live projectiles, so equal
                // property pointers would mean sharing, not a freed one's memory reused
                a_c.waitingFor = "the NPC's and the player's Firebolt projectiles in the hook";
                a_c.mark = SelfTestProbe::Mark();
                (void)SelfTestWorld::Cast(RE::PlayerCharacter::GetSingleton(), a_c.forms.firebolt, RefOf(a_c.dummy));
                a_c.npcCastCalled = SelfTestWorld::Cast(ActorOf(a_c.caster), a_c.forms.firebolt, RefOf(a_c.dummy));
                return false;
            }
            a_c.playerT4 = FindProjectile(a_c.mark, true, a_c.forms.firebolt);
            a_c.npcT4 = FindProjectile(a_c.mark, false, a_c.forms.firebolt);
            if (!a_c.playerT4 || !a_c.npcT4) {
                // Tells "the NPC did not cast" apart from "it cast but no projectile reached the hook"
                auto* caster = ActorOf(a_c.caster);
                a_c.waitingFor = std::format("the NPC's and the player's Firebolt projectiles in the hook (player {}, NPC "
                    "{}; NPC cast call {}, NPC {}, NPC AI {})", a_c.playerT4 ? "seen" : "not seen",
                    a_c.npcT4 ? "seen" : "not seen", a_c.npcCastCalled ? "made" : "NOT made (no caster)",
                    caster ? (caster->IsDead() ? "dead" : "alive") : "gone", caster && caster->IsAIEnabled() ? "on" : "off");
                return false;
            }
            const auto& npc = *a_c.npcT4;
            const auto& player = *a_c.playerT4;
            const int sharedProps = CommonCount(npc.after.pointers.properties, player.after.pointers.properties);
            const bool ok = !npc.recolored && npc.after.withGrey == 0 && sharedProps == 0;
            SelfTestLog::Check("T4", ok, std::format("NPC: {} | properties shared with the player's live projectile: {}",
                DescribeProjectile(npc), sharedProps));
            SelfTestLog::Info("T4", std::format("materials before any recolour: NPC {} vs player {}, {} pointer(s) in common "
                "({}pooled)", npc.before.materials.size(), player.before.materials.size(),
                CommonCount(npc.before.materials, player.before.materials),
                CommonCount(npc.before.materials, player.before.materials) ? "" : "not "));
            return true;
        }

        bool StepNpcBurnInfo(Ctx& a_c, bool a_first)
        {
            if (a_first && !a_c.actorsReady) {
                return true;
            }
            a_c.waitingFor = "the NPC burn report";
            if (Clock::now() - a_c.stepStart < kNpcBurnWait) {
                return false;
            }
            SelfTestLog::Info("T5-npc", std::format("dummy shaders after the T4 casts (player and NPC Firebolt): {}; engine "
                "hit shader {:08X}, grey {:08X}", DescribeShaders(SelfTestWorld::ShadersOn(a_c.dummy)),
                EngineShaderOf(a_c.forms.firebolt) ? EngineShaderOf(a_c.forms.firebolt)->GetFormID() : 0,
                GetForms().burnShader ? GetForms().burnShader->GetFormID() : 0));
            return true;
        }

        bool StepExplosion(Ctx& a_c, bool a_first, bool a_byPlayer)
        {
            const char* id = a_byPlayer ? "T6" : "T6-npc";
            if (a_first) {
                if (!NeedActors(a_c, id)) {
                    return true;
                }
                a_c.mark = SelfTestProbe::Mark();
                RE::Actor* caster = a_byPlayer ? RE::PlayerCharacter::GetSingleton() : ActorOf(a_c.caster);
                (void)SelfTestWorld::Cast(caster, a_c.forms.fireball, RefOf(a_c.dummy));
            }
            std::string seen;
            for (const auto& r : SelfTestProbe::ExplosionsSince(a_c.mark)) {
                if (r.byPlayer == a_byPlayer) {
                    const bool ok = a_byPlayer ? (r.recolored && r.stats.Recolored() > 0) :
                                                 (!r.recolored && r.after.withGrey == 0);
                    SelfTestLog::Check(id, ok, DescribeExplosion(r));
                    return true;
                }
                seen += " | " + DescribeExplosion(r);
            }
            a_c.waitingFor = std::format("a fire explosion attributed to the {} (others seen:{})",
                a_byPlayer ? "player" : "NPC (not the player)", seen.empty() ? " none" : seen);
            return false;
        }

        bool StepPlayerExplosion(Ctx& a_c, bool a_first) { return StepExplosion(a_c, a_first, true); }
        bool StepNpcExplosion(Ctx& a_c, bool a_first) { return StepExplosion(a_c, a_first, false); }

        bool StepModifiers(Ctx& a_c, bool)
        {
            if (!a_c.playerT4 || !a_c.npcT4) {
                SelfTestLog::Info("T7", "skipped: no T4 projectiles to compare");
                return true;
            }
            const auto& player = a_c.playerT4->after;
            const auto& npc = a_c.npcT4->after;
            const auto& raw = !player.firstModifierRaw.empty() ? player.firstModifierRaw : npc.firstModifierRaw;
            std::string dump;
            for (const float f : raw) {
                dump += std::format("{}{:.3f}", dump.empty() ? "" : " ", f);
            }
            SelfTestLog::Info("T7", std::format("first BSPSysSimpleColorModifier bytes 0x30-0x7F as floats (keys should "
                "read as 0..1 RGBA): {}", dump.empty() ? "no modifier found" : dump));
            if (!GetConfig().particleColors) {
                SelfTestLog::Info("T7", "particleColors false: modifier sharing not checked");
                return true;
            }
            const int shared = CommonCount(npc.colorModifiers, player.colorModifiers);
            const bool npcGrey = !npc.colorModifiers.empty() &&
                                 npc.colorModifiersAllGrey == static_cast<int>(npc.colorModifiers.size());
            SelfTestLog::Check("T7", shared == 0 && !npcGrey, std::format("modifiers: player {} ({} all grey), NPC {} ({} "
                "all grey), {} shared", player.colorModifiers.size(), player.colorModifiersAllGrey, npc.colorModifiers.size(),
                npc.colorModifiersAllGrey, shared));
            return true;
        }

        bool StepBuffOff(Ctx& a_c, bool a_first)
        {
            if (a_first) {
                a_c.waitingFor = "IsStoked() false after dispelling the Stoke effect";
                SelfTestLog::Info("T8", std::format("dispelled {} Stoke effect(s) with ActiveEffect::Dispel(true)",
                    SelfTestWorld::DispelStoke()));
                return false;
            }
            if (!IsStoked()) {
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - a_c.stepStart);
                SelfTestLog::Pass("T8", std::format("buff flag off {} ms after the dispel", ms.count()));
                return true;
            }
            return false;
        }

        bool StepCastUnstoked(Ctx& a_c, bool a_first)
        {
            if (a_first) {
                if (!NeedActors(a_c, "T8-cast")) {
                    return true;
                }
                a_c.waitingFor = "the player's Firebolt projectile after the buff ended";
                a_c.mark = SelfTestProbe::Mark();
                (void)SelfTestWorld::Cast(RE::PlayerCharacter::GetSingleton(), a_c.forms.firebolt, RefOf(a_c.dummy));
                return false;
            }
            const auto r = FindProjectile(a_c.mark, true, a_c.forms.firebolt);
            if (!r) {
                return false;
            }
            SelfTestLog::Check("T8-cast", !r->recolored && r->after.withGrey == 0, DescribeProjectile(*r));
            return true;
        }

        bool StepCleanup(Ctx& a_c, bool)
        {
            SelfTestLog::Info("T9", std::format("dummy: {}; caster: {}", SelfTestWorld::Remove(a_c.dummy),
                SelfTestWorld::Remove(a_c.caster)));
            SelfTestLog::Info("T-hand", "hand art: manual (CastSpellImmediate has no charge animation, so there is no "
                "casting art to inspect)");
            return true;
        }

        using Handler = bool (*)(Ctx&, bool);
        struct StepDef
        {
            const char* id;
            float timeoutSeconds;
            Handler run;
        };

        // T5 runs right after T3 (before the NPC casts) so the dummy carries only the player's burn
        constexpr std::array kSteps{
            StepDef{ "T0", 2.0f, &StepEnvironment },
            StepDef{ "T1", 1.0f, &StepBuffOn },
            StepDef{ "T2", 1.0f, &StepSpawn },
            StepDef{ "T2", 5.0f, &StepActorsReady },
            StepDef{ "T3", 3.0f, &StepPlayerProjectile },
            StepDef{ "T5", 3.0f, &StepPlayerBurn },
            StepDef{ "T5-extend", 3.0f, &StepBurnExtend },
            StepDef{ "T4", 3.0f, &StepNpcProjectile },
            StepDef{ "T5-npc", 3.0f, &StepNpcBurnInfo },
            StepDef{ "T6", 6.0f, &StepPlayerExplosion },
            StepDef{ "T6-npc", 6.0f, &StepNpcExplosion },
            StepDef{ "T7", 1.0f, &StepModifiers },
            StepDef{ "T8", 1.0f, &StepBuffOff },
            StepDef{ "T8-cast", 3.0f, &StepCastUnstoked },
            StepDef{ "T9", 1.0f, &StepCleanup },
        };

        bool PlayerReady()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* ui = RE::UI::GetSingleton();
            return player && ui && player->Is3DLoaded() && player->GetParentCell() && !ui->GameIsPaused();
        }

        void Finish()
        {
            SelfTestLog::Done();
            SelfTestProbe::SetRecording(false);
            g_ctx.running = false;
            g_active.store(false);
        }

        void RunStep()
        {
            const auto& step = kSteps[g_ctx.index];
            const bool first = !g_ctx.entered;
            if (first) {
                g_ctx.entered = true;
                g_ctx.stepStart = Clock::now();
                g_ctx.waitingFor.clear();
            }
            bool done = step.run(g_ctx, first);
            const auto elapsed = std::chrono::duration<float>(Clock::now() - g_ctx.stepStart).count();
            if (!done && elapsed > step.timeoutSeconds) {
                SelfTestLog::Fail(step.id, std::format("timeout: {:.2f} s passed (limit {:.1f} s) waiting for {}",
                    elapsed, step.timeoutSeconds,
                    g_ctx.waitingFor.empty() ? "the step" : g_ctx.waitingFor));
                done = true;
            }
            if (!done) {
                return;
            }
            g_ctx.entered = false;
            if (++g_ctx.index >= kSteps.size()) {
                Finish();
            }
        }
    }

    void OnGameLoaded()
    {
        if (!GetConfig().selfTest || g_ran || g_armed) {
            return;
        }
        g_armed = true;
        g_active.store(true);
        logger::info("BlackFlame: self-test armed (starts when the player is in a loaded cell and the game is not paused)");
    }

    void Tick()
    {
        if (!g_active.load()) {
            return;
        }
        // Entered from a game-thread task: a throw would only be logged there, so finish cleanly here
        try {
            if (!g_ctx.running) {
                if (!PlayerReady()) {
                    g_ctx.readySince.reset();
                    return;
                }
                if (!g_ctx.readySince) {
                    g_ctx.readySince = Clock::now();
                }
                if (Clock::now() - *g_ctx.readySince < kReadyDelay) {
                    return;
                }
                g_armed = false;
                g_ran = true;
                g_ctx.running = true;
                SelfTestLog::Open();
                SelfTestProbe::SetRecording(true);
                SelfTestLog::Info("START", "Black Flame self-test");
            }
            RunStep();
        } catch (const std::exception& e) {
            SelfTestLog::Fail("ABORT", std::format("exception in step {}: {}", g_ctx.index, e.what()));
            SelfTestLog::Info("T9", std::format("dummy: {}; caster: {}", SelfTestWorld::Remove(g_ctx.dummy),
                SelfTestWorld::Remove(g_ctx.caster)));
            Finish();
        }
    }

    bool WantsFastTicks() noexcept
    {
        return g_active.load();
    }
}
