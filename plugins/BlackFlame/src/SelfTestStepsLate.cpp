#include "SelfTestCtx.h"

#include "BuffState.h"
#include "Config.h"
#include "Forms.h"
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

        RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

        bool LiveIn(const std::vector<SelfTestWorld::ShaderOn>& a_shaders, RE::FormID a_shader)
        {
            return std::ranges::any_of(a_shaders, [&](const auto& s) { return !s.finished && s.effectData == a_shader; });
        }

        bool KeysPlausible(const std::vector<float>& a_keys)
        {
            return !a_keys.empty() && std::ranges::all_of(a_keys, [](float a_v) {
                return !std::isnan(a_v) && a_v >= kMinKey && a_v <= kMaxKey;
            });
        }

        RE::FormID FormIdOf(RE::ObjectRefHandle a_handle)
        {
            auto* ref = RefOf(a_handle);
            return ref ? ref->GetFormID() : 0;
        }

        bool StepExplosion(Ctx& a_c, bool a_first, bool a_byPlayer)
        {
            const char* id = a_byPlayer ? "T6" : "T6-npc";
            const auto* expected = a_c.forms.fireballExplosion;
            if (a_first) {
                if (!NeedActors(a_c, id)) {
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
                expected ? expected->GetFormID() : 0, a_byPlayer ? "player" : "caster",
                seen.empty() ? " none" : seen);
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
            a_c.engineLiveAfterGrey = false;
            if (!NeedActors(a_c, "T5")) {
                return true;
            }
        }
        const auto* ours = GetForms().burnShader;
        const auto engines = EngineShadersOf(a_c.forms.firebolt);
        const auto shaders = SelfTestWorld::ShadersOn(a_c.dummy);
        const bool replace = GetConfig().burnMode == BurnMode::kReplace;
        if (!a_c.greySeen && ours && LiveIn(shaders, ours->GetFormID())) {
            a_c.greySeen = Clock::now();
        }
        if (a_c.greySeen && std::ranges::any_of(engines, [&](RE::FormID e) { return LiveIn(shaders, e); })) {
            a_c.engineLiveAfterGrey = true;
        }
        a_c.waitingFor = std::format("a live grey shader on the target, then {} s of watching; shaders seen: {}; burns: {}",
            kBurnWatch.count(), DescribeShaders(shaders), DescribeBurns(a_c.t3Mark));
        if (!a_c.greySeen || Clock::now() - *a_c.greySeen < kBurnWatch) {
            return false;
        }
        const auto target = FormIdOf(a_c.dummy);
        const auto burns = SelfTestProbe::BurnsSince(a_c.t3Mark);
        const bool applied = std::ranges::any_of(burns, [&](const auto& b) { return b.target == target && b.applied; });
        SelfTestLog::Check("T5", applied && (!replace || !a_c.engineLiveAfterGrey), std::format("grey shader live on the "
            "target [{}]; a burn record applied it: {}; an engine hit shader of Firebolt's effects live in the {} s "
            "after: {}; shaders now: {}; burns: {}", BurnModeName(GetConfig().burnMode), applied ? "yes" : "NO",
            kBurnWatch.count(), a_c.engineLiveAfterGrey ? "YES" : "no", DescribeShaders(shaders),
            DescribeBurns(a_c.t3Mark)));
        return true;
    }

    // A second hit while the grey shader is live extends it instead of stacking a second one
    bool StepBurnExtend(Ctx& a_c, bool a_first)
    {
        if (a_first) {
            if (!NeedActors(a_c, "T5-extend")) {
                return true;
            }
            a_c.mark = SelfTestProbe::Mark();
            a_c.waitingFor = "a burn record for a second Firebolt hit";
            return !CastAtTarget(a_c, Player(), a_c.forms.firebolt, "T5-extend");
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
        if (a_first) {
            if (!NeedActors(a_c, "T4")) {
                return true;
            }
            // The player casts too, in the same frame: two live projectiles, so equal property
            // pointers would mean sharing, not a freed one's memory reused
            a_c.mark = SelfTestProbe::Mark();
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

        // Property pointers compared only while both projectiles still exist, inspected now
        const auto npcRef = npc.projectile.get();
        const auto playerRef = player.projectile.get();
        if (npcRef && playerRef) {
            const auto npcNow = SelfTestProbe::Inspect(npcRef->Get3D());
            const auto playerNow = SelfTestProbe::Inspect(playerRef->Get3D());
            const int shared = CommonCount(npcNow.pointers.properties, playerNow.pointers.properties);
            SelfTestLog::Check("T4-shared", shared == 0 && npcNow.withGrey == 0 && npcNow.effectProperties > 0,
                std::format("both live: {} effect properties shared, caster grey {}", shared, npcNow.withGrey));
        } else {
            SelfTestLog::Info("T4-shared", "not comparable: a projectile was gone before both could be inspected");
        }
        const int pooled = CommonCount(npc.before.materials, player.before.materials);
        SelfTestLog::Info("T4", std::format("materials before any recolour: caster {} vs player {}, {} pointer(s) in "
            "common ({}pooled)", npc.before.materials.size(), player.before.materials.size(), pooled,
            pooled ? "" : "not "));
        return true;
    }

    bool StepNpcBurnInfo(Ctx& a_c, bool)
    {
        a_c.waitingFor = "the caster burn report";
        if (Clock::now() - a_c.stepStart < kNpcBurnWait) {
            return false;
        }
        std::string engines;
        for (const auto e : EngineShadersOf(a_c.forms.firebolt)) {
            engines += std::format("{}{:08X}", engines.empty() ? "" : ", ", e);
        }
        SelfTestLog::Info("T5-npc", std::format("target shaders after the T4 casts (player and caster Firebolt): {}; "
            "engine hit shaders {}, grey {:08X}", DescribeShaders(SelfTestWorld::ShadersOn(a_c.dummy)), engines,
            GetForms().burnShader ? GetForms().burnShader->GetFormID() : 0));
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
        const auto& keys = !player.firstModifierKeys.empty() ? player.firstModifierKeys : npc.firstModifierKeys;
        std::string dump;
        for (const float f : keys) {
            dump += std::format("{}{:.3f}", dump.empty() ? "" : " ", f);
        }
        SelfTestLog::Info("T7", keys.empty() ? std::string("no BSPSysSimpleColorModifier found") :
            std::format("first BSPSysSimpleColorModifier colour keys (0x30-0x5F): {} -> {}", dump,
                KeysPlausible(keys) ? "layout plausible" : "layout mismatch - keep particleColors off"));
        const bool comparable = GetConfig().particleColors && !player.colorModifiers.empty() &&
                                !npc.colorModifiers.empty() &&
                                player.colorModifiersAllGrey == static_cast<int>(player.colorModifiers.size());
        if (!comparable) {
            SelfTestLog::Info("T7", std::format("nothing to compare (particleColors {}, player modifiers {} with {} grey, "
                "caster modifiers {})", GetConfig().particleColors, player.colorModifiers.size(), player.colorModifiersAllGrey,
                npc.colorModifiers.size()));
            return true;
        }
        const int shared = CommonCount(npc.colorModifiers, player.colorModifiers);
        SelfTestLog::Check("T7", shared == 0 && npc.colorModifiersAllGrey == 0, std::format("modifiers: player {} (all "
            "grey), caster {} ({} grey), {} shared", player.colorModifiers.size(), npc.colorModifiers.size(),
            npc.colorModifiersAllGrey, shared));
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
        return true;
    }
}
