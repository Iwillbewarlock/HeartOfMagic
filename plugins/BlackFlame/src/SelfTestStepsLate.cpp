#include "SelfTestCtx.h"

#include "BuffState.h"
#include "Config.h"
#include "Forms.h"
#include "ParticleColors.h"
#include "SelfTestLog.h"
#include "SelfTestText.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace BlackFlame::SelfTest
{
    namespace
    {
        using namespace SelfTestText;

        constexpr float kColorTolerance = 0.002f;

        RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

        bool Near(float a_lhs, float a_rhs) { return std::fabs(a_lhs - a_rhs) <= kColorTolerance; }

        // An engine hit shader the burn layer did not finish: unfinished and old enough
        bool EngineLive(const std::vector<SelfTestWorld::ShaderOn>& a_shaders, const std::vector<RE::FormID>& a_engines)
        {
            return std::ranges::any_of(a_shaders, [&](const auto& s) {
                return !s.finished && s.age >= kEngineAgeMin && std::ranges::find(a_engines, s.effectData) != a_engines.end();
            });
        }

        RE::FormID FormIdOf(RE::ObjectRefHandle a_handle)
        {
            auto* ref = RefOf(a_handle);
            return ref ? ref->GetFormID() : 0;
        }

        // Before every caster cast: still placed safely (it keeps its AI and may wander), turned to
        // the target; FAIL without casting otherwise
        bool PrepareCaster(Ctx& a_c, const char* a_id)
        {
            auto* caster = ActorOf(a_c.caster);
            auto* target = RefOf(a_c.dummy);
            auto* player = Player();
            if (!caster || !target || !player) {
                SelfTestLog::Fail(a_id, "the caster, the target or the player is gone: nothing cast");
                return false;
            }
            const float radius = a_c.forms.fireballExplosion ? a_c.forms.fireballExplosion->data.radius : 0.0f;
            const auto pos = caster->GetPosition();
            const float toTarget = pos.GetDistance(target->GetPosition());
            const float toPlayer = target->GetPosition().GetDistance(player->GetPosition());
            if (toTarget < radius + kRadiusMargin || toPlayer < radius + kRadiusMargin) {
                SelfTestLog::Fail(a_id, std::format("the caster or the target moved inside the explosion's reach (caster to "
                    "target {:.0f}, target to player {:.0f}, radius {:.0f} + {:.0f}): nothing cast", toTarget, toPlayer,
                    radius, kRadiusMargin));
                return false;
            }
            SelfTestWorld::FaceTarget(caster, target);
            SelfTestLog::Info(a_id, std::format("caster at ({:.0f}, {:.0f}, {:.0f}), {:.0f} from the target, turned to it",
                pos.x, pos.y, pos.z, toTarget));
            return true;
        }

        bool StepExplosion(Ctx& a_c, bool a_first, bool a_byPlayer)
        {
            const char* id = a_byPlayer ? "T6" : "T6-npc";
            const auto* expected = a_c.forms.fireballExplosion;
            if (a_first) {
                if (!NeedActors(a_c, id) || (!a_byPlayer && !PrepareCaster(a_c, id))) {
                    return true;
                }
                a_c.mark = SelfTestProbe::Mark();
                RE::Actor* caster = a_byPlayer ? Player() : ActorOf(a_c.caster);
                return !CastAtTarget(a_c, caster, a_c.forms.fireball, id);
            }
            std::string seen;
            for (const auto& r : SelfTestProbe::ExplosionsSince(a_c.mark)) {
                const bool fromCaster = a_c.casterId && (r.owner == a_c.casterId || r.causeActor == a_c.casterId);
                const bool matches = expected && r.base == expected->GetFormID() &&
                                     (a_byPlayer ? r.byPlayer : (!r.byPlayer && fromCaster));
                if (!matches) {
                    seen += " | " + DescribeExplosion(r);
                    continue;
                }
                const auto& a = r.after;
                const bool ok = a_byPlayer ?
                                    (r.recolored && r.stats.Recolored() > 0 && a.withPalettePath >= r.stats.Recolored()) :
                                    (!r.recolored && a.effectProperties > 0 && a.withGrey == 0);
                SelfTestLog::Check(id, ok, DescribeExplosion(r));
                return true;
            }
            a_c.waitingFor = std::format("Fireball's explosion {:08X} from the {} (others seen:{})",
                expected ? expected->GetFormID() : 0, a_byPlayer ? "player" : "caster", seen.empty() ? " none" : seen);
            return false;
        }

        // The live player projectile's light, read a tick or more after the tint was written
        void CheckLiveLight(RE::TESObjectREFR* a_projectileRef)
        {
            auto* projectile = skyrim_cast<RE::Projectile*>(a_projectileRef);
            auto& data = projectile->GetProjectileRuntimeData();
            auto* light = data.light ? data.light->light.get() : nullptr;
            if (!light || !GetConfig().light) {
                SelfTestLog::Info("T4-light", light ? "light false in the config" : "the projectile has no light");
                return;
            }
            const auto& got = light->GetLightRuntimeData().diffuse;
            const auto& want = GetConfig().lightColor;
            SelfTestLog::Check("T4-light", Near(got.red, want.red) && Near(got.green, want.green) && Near(got.blue, want.blue),
                std::format("live player projectile's light diffuse ({:.3f}, {:.3f}, {:.3f}), lightColor ({:.3f}, {:.3f}, "
                    "{:.3f})", got.red, got.green, got.blue, want.red, want.green, want.blue));
        }

        // The caster's colour keys at its hook vs now, per modifier pointer
        std::optional<bool> ModifiersChanged(const SelfTestProbe::Inspection& a_atHook, const SelfTestProbe::Inspection& a_now)
        {
            if (a_atHook.colorModifiers.empty() || a_now.colorModifiers.empty()) {
                return std::nullopt;
            }
            for (std::size_t i = 0; i < a_atHook.colorModifiers.size(); ++i) {
                const auto it = std::ranges::find(a_now.colorModifiers, a_atHook.colorModifiers[i]);
                if (it != a_now.colorModifiers.end() &&
                    a_now.modifierKeys[static_cast<std::size_t>(it - a_now.colorModifiers.begin())] != a_atHook.modifierKeys[i]) {
                    return true;
                }
            }
            return false;
        }
    }

    // ---------------------------------------------------------------------
    // T5: the player's burn, watched for a second after the grey shows
    // ---------------------------------------------------------------------

    bool StepPlayerBurn(Ctx& a_c, bool a_first)
    {
        if (a_first) {
            a_c.greySeen.reset();
            a_c.extendAttempted = false;
            a_c.extendCast = false;
            a_c.engineLiveAfterGrey = false;
            if (!NeedActors(a_c, "T5")) {
                return true;
            }
        }
        const auto* ours = GetForms().burnShader;
        const auto engines = EngineShadersOf(a_c.forms.firebolt);
        const auto shaders = SelfTestWorld::ShadersOn(a_c.dummy);
        const bool replace = GetConfig().burnMode == BurnMode::kReplace;
        if (!a_c.greySeen && ours &&
            std::ranges::any_of(shaders, [&](const auto& s) { return !s.finished && s.effectData == ours->GetFormID(); })) {
            a_c.greySeen = Clock::now();
            // T5-extend's second hit, cast now so it lands while this instant hit's short grey burn
            // still lives; T5-extend only judges its burn record
            a_c.extendAttempted = true;
            a_c.extendMark = SelfTestProbe::Mark();
            a_c.extendCast = CastAtTarget(a_c, Player(), a_c.forms.firebolt, "T5-extend");
        }
        if (a_c.greySeen && EngineLive(shaders, engines)) {
            a_c.engineLiveAfterGrey = true;
        }
        a_c.waitingFor = std::format("a live grey shader on the target, then {} s of watching; shaders seen: {}; burns: {}",
            kBurnWatch.count(), DescribeShaders(shaders), DescribeBurns(a_c.t3Mark));
        if (!a_c.greySeen || Clock::now() - *a_c.greySeen < kBurnWatch) {
            return false;
        }
        const auto window = std::chrono::duration<float>(Clock::now() - *a_c.greySeen).count();
        const auto target = FormIdOf(a_c.dummy);
        const auto burns = SelfTestProbe::BurnsSince(a_c.t3Mark);
        const bool applied = std::ranges::any_of(burns, [&](const auto& b) { return b.target == target && b.applied; });
        const bool engineOk = !replace || (!engines.empty() && !a_c.engineLiveAfterGrey);
        SelfTestLog::Check("T5", applied && engineOk, std::format("grey shader live on the target [{}]; a burn record "
            "applied it: {}; Firebolt's engine hit shaders known: {}; one of them unfinished at {:.1f} s or older in the "
            "{:.2f} s checked: {}; shaders now: {}; burns: {}", BurnModeName(GetConfig().burnMode), applied ? "yes" : "NO",
            engines.size(), kEngineAgeMin, window, a_c.engineLiveAfterGrey ? "YES" : "no", DescribeShaders(shaders),
            DescribeBurns(a_c.t3Mark)));
        return true;
    }

    // A second hit while the grey shader is live extends it instead of stacking a second one
    bool StepBurnExtend(Ctx& a_c, bool a_first)
    {
        if (a_first) {
            if (!a_c.extendAttempted) {
                SelfTestLog::Fail("T5-extend", "skipped: no second hit (T5 never saw the grey shader)");
                return true;
            }
            if (!a_c.extendCast) {
                return true;  // CastAtTarget already logged this step's FAIL
            }
            a_c.mark = a_c.extendMark;  // cast by T5 the moment the grey shader showed
            a_c.waitingFor = "a burn record for the second Firebolt hit";
        }
        const auto burns = SelfTestProbe::BurnsSince(a_c.mark);
        const auto it = std::ranges::find_if(burns, [](const auto& b) { return b.applied || b.alreadyLive; });
        if (it == burns.end()) {
            a_c.waitingFor = std::format("a burn record for the second Firebolt hit (seen: {})", DescribeBurns(a_c.mark));
            return false;
        }
        const auto* ours = GetForms().burnShader;
        const auto shaders = SelfTestWorld::ShadersOn(a_c.dummy);
        const auto liveGrey = std::ranges::count_if(shaders, [&](const auto& s) {
            return ours && !s.finished && s.effectData == ours->GetFormID();
        });
        SelfTestLog::Check("T5-extend", it->alreadyLive && liveGrey == 1, std::format("second hit {} the grey shader; live "
            "grey shaders on the target: {}; burns: {}", it->alreadyLive ? "extended" : "applied a new", liveGrey,
            DescribeBurns(a_c.mark)));
        return true;
    }

    // ---------------------------------------------------------------------
    // T4: the caster's projectile next to a live player one
    // ---------------------------------------------------------------------

    bool StepNpcProjectile(Ctx& a_c, bool a_first)
    {
        if (a_first && !NeedActors(a_c, "T4")) {
            return true;
        }
        if (a_c.phase == 0) {
            // T5-extend's projectile must be on record first, or it could pass for T4's player one
            const bool extendSeen = !a_c.extendCast || FindProjectile(a_c.extendMark, true, a_c.forms.firebolt);
            if (!extendSeen && Clock::now() - a_c.stepStart < kExtendRecordWait) {
                a_c.waitingFor = "T5-extend's projectile record before casting";
                return false;
            }
            if (!PrepareCaster(a_c, "T4")) {
                return true;
            }
            // The player casts too, in the same frame: two live projectiles, so equal property
            // pointers would mean sharing, not a freed one's memory reused
            a_c.mark = SelfTestProbe::Mark();
            a_c.phase = 1;
            a_c.waitingFor = "the caster's and the player's Firebolt projectiles in the hook";
            return !CastAtTarget(a_c, Player(), a_c.forms.firebolt, "T4") ||
                   !CastAtTarget(a_c, ActorOf(a_c.caster), a_c.forms.firebolt, "T4");
        }
        a_c.playerT4 = FindProjectile(a_c.mark, true, a_c.forms.firebolt);
        a_c.npcT4 = FindProjectileFrom(a_c.mark, a_c.casterId, a_c.forms.firebolt);
        if (!a_c.playerT4 || !a_c.npcT4) {
            a_c.waitingFor = std::format("the caster's and the player's Firebolt projectiles in the hook (player {}, "
                "caster {})", a_c.playerT4 ? "seen" : "not seen", a_c.npcT4 ? "seen" : "not seen");
            return false;
        }
        const auto& npc = *a_c.npcT4;
        const auto& player = *a_c.playerT4;
        const bool ok = !npc.recolored && npc.after.effectProperties > 0 && npc.after.withGrey == 0 &&
                        player.after.effectProperties > 0 && player.recolored;
        SelfTestLog::Check("T4", ok, std::format("caster: {} | player: recoloured={}, {} effect properties",
            DescribeProjectile(npc), player.recolored, player.after.effectProperties));

        // Compared only while both projectiles still exist, inspected now (a tick or more later)
        const auto npcRef = npc.projectile.get();
        const auto playerRef = player.projectile.get();
        if (npcRef && playerRef) {
            const auto npcNow = SelfTestProbe::Inspect(npcRef->Get3D());
            const auto playerNow = SelfTestProbe::Inspect(playerRef->Get3D());
            if (npcNow.effectProperties > 0 && playerNow.effectProperties > 0) {
                const int shared = CommonCount(npcNow.pointers.properties, playerNow.pointers.properties);
                SelfTestLog::Check("T4-shared", shared == 0 && npcNow.withGrey == 0, std::format("both live: {} effect "
                    "properties shared, caster grey {}", shared, npcNow.withGrey));
            } else {
                SelfTestLog::Info("T4-shared", "not comparable: a live projectile showed no effect properties");
            }
            a_c.npcModifiersChanged = ModifiersChanged(npc.after, npcNow);
            CheckLiveLight(playerRef.get());
        } else {
            SelfTestLog::Info("T4-shared", "not comparable: a projectile was gone before both could be inspected");
            SelfTestLog::Info("T4-light", "not checked: the player's projectile was gone");
        }
        const int pooled = CommonCount(npc.before.materials, player.before.materials);
        SelfTestLog::Info("T4", std::format("materials before any recolour: caster {} vs player {}, {} pointer(s) in "
            "common ({}pooled)", npc.before.materials.size(), player.before.materials.size(), pooled,
            pooled ? "" : "not "));
        return true;
    }

    // The caster's hit keeps its own engine shader: we never finish an NPC's shared hit shader
    bool StepNpcBurnInfo(Ctx& a_c, bool a_first)
    {
        if (a_first && !NeedActors(a_c, "T5-npc")) {
            return true;
        }
        const auto engines = EngineShadersOf(a_c.forms.firebolt);
        const auto shaders = SelfTestWorld::ShadersOn(a_c.dummy);
        a_c.waitingFor = "the caster's engine hit shader on the target";
        if (EngineLive(shaders, engines)) {
            SelfTestLog::Pass("T5-npc", std::format("an engine Firebolt hit shader unfinished at {:.1f} s or older on the "
                "target after the T4 casts: {}", kEngineAgeMin, DescribeShaders(shaders)));
            return true;
        }
        if (Clock::now() - a_c.stepStart < kNpcBurnWait) {
            return false;
        }
        SelfTestLog::Fail("T5-npc", std::format("no unfinished engine Firebolt hit shader on the target within {} ms of the "
            "T4 casts (the caster's should stay): {}", kNpcBurnWait.count(), DescribeShaders(shaders)));
        return true;
    }

    bool StepPlayerExplosion(Ctx& a_c, bool a_first) { return StepExplosion(a_c, a_first, true); }
    bool StepNpcExplosion(Ctx& a_c, bool a_first) { return StepExplosion(a_c, a_first, false); }

    // ---------------------------------------------------------------------
    // T7, T8, T9
    // ---------------------------------------------------------------------

    bool StepModifiers(Ctx& a_c, bool)
    {
        if (!a_c.playerT4 || !a_c.npcT4) {
            SelfTestLog::Info("T7", "nothing to compare: no T4 projectiles");
            return true;
        }
        const auto& player = a_c.playerT4->after;
        const auto& npc = a_c.npcT4->after;
        const auto& keysOf = !player.modifierKeys.empty() ? player.modifierKeys : npc.modifierKeys;
        if (keysOf.empty()) {
            SelfTestLog::Info("T7", "no BSPSysSimpleColorModifier found");
        } else {
            std::string dump;
            for (const float f : keysOf.front()) {
                dump += std::format("{}{:.3f}", dump.empty() ? "" : " ", f);
            }
            SelfTestLog::Info("T7", std::format("first BSPSysSimpleColorModifier colour keys (0x30-0x5F): {} -> {}", dump,
                KeysPlausible(keysOf.front()) ? "layout plausible" : "layout mismatch - keep particleColors off"));
        }
        if (!GetConfig().particleColors || player.colorModifiers.empty()) {
            SelfTestLog::Info("T7", std::format("nothing to compare (particleColors {}, player modifiers {})",
                GetConfig().particleColors, player.colorModifiers.size()));
            return true;
        }
        SelfTestLog::Check("T7-player", player.colorModifiersAllGrey == static_cast<int>(player.colorModifiers.size()),
            std::format("player modifiers desaturated: {} of {}", player.colorModifiersAllGrey, player.colorModifiers.size()));
        if (npc.colorModifiers.empty() || !a_c.npcModifiersChanged) {
            SelfTestLog::Info("T7", "caster modifiers not comparable (none, or the projectile was gone at T4)");
            return true;
        }
        const int shared = CommonCount(npc.colorModifiers, player.colorModifiers);
        SelfTestLog::Check("T7", shared == 0 && !*a_c.npcModifiersChanged, std::format("caster modifiers: {}, {} shared "
            "with the player's, keys changed between its hook and T4: {}", npc.colorModifiers.size(), shared,
            *a_c.npcModifiersChanged ? "YES" : "no"));
        return true;
    }

    bool StepBuffOff(Ctx& a_c, bool a_first)
    {
        if (a_first) {
            if (!a_c.buffWasTurnedOn) {
                SelfTestLog::Fail("T8", "skipped: the buff was never turned on (T1)");
                return true;
            }
            const int dispelled = SelfTestWorld::DispelStoke();
            if (dispelled == 0) {
                SelfTestLog::Fail("T8", "no Stoke effect left to dispel");
                return true;
            }
            SelfTestLog::Info("T8", std::format("dispelled {} Stoke effect(s) with ActiveEffect::Dispel(true)", dispelled));
            a_c.waitingFor = "IsStoked() false after dispelling the Stoke effect";
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
            if (!a_c.buffWasTurnedOn || IsStoked()) {
                SelfTestLog::Fail("T8-cast", a_c.buffWasTurnedOn ? "skipped: the buff is still on" :
                                                                   "skipped: the buff was never on");
                return true;
            }
            if (!NeedActors(a_c, "T8-cast")) {
                return true;
            }
            a_c.mark = SelfTestProbe::Mark();
            a_c.waitingFor = "the player's Firebolt projectile after the buff ended";
            return !CastAtTarget(a_c, Player(), a_c.forms.firebolt, "T8-cast");
        }
        const auto r = FindProjectile(a_c.mark, true, a_c.forms.firebolt);
        if (!r) {
            return false;
        }
        const bool ok = !r->recolored && r->after.effectProperties > 0 && r->after.withGrey == 0 && a_c.t3Recolored;
        SelfTestLog::Check("T8-cast", ok, std::format("T3 recoloured: {} | {}", a_c.t3Recolored ? "yes" : "NO",
            DescribeProjectile(*r)));
        return true;
    }

    // Every run ends here: the buff dispelled, then (projectiles in flight first) the actors deleted
    bool StepCleanup(Ctx& a_c, bool a_first)
    {
        if (a_first) {
            const int dispelled = SelfTestWorld::DispelStoke();
            if (dispelled > 0) {
                SelfTestLog::Info("T9", std::format("dispelled {} Stoke effect(s) left on the player", dispelled));
            }
            a_c.waitingFor = "projectiles in flight before deleting the actors";
        }
        if (a_c.lastCast && Clock::now() - *a_c.lastCast < kInFlightWait) {
            return false;
        }
        SelfTestLog::Info("T9", std::format("target: {}; caster: {}", SelfTestWorld::Remove(a_c.dummy),
            SelfTestWorld::Remove(a_c.caster)));
        SelfTestLog::Info("T-hand", "hand art: manual (CastSpellImmediate has no charge animation, so there is no "
            "casting art to inspect)");
        a_c.cleanedUp = true;
        return true;
    }
}
