#include "SelfTestCtx.h"

#include "BuffState.h"
#include "Config.h"
#include "Forms.h"
#include "ParticleColors.h"
#include "HazardSwap.h"
#include "ImpactSwap.h"
#include "ProjectileHook.h"
#include "SelfTestJudge.h"
#include "SelfTestLog.h"
#include "SelfTestJudge.h"
#include "SelfTestText.h"
#include "Tint.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>

namespace BlackFlame::SelfTest
{
    namespace
    {
        using namespace SelfTestText;

        constexpr float kColorTolerance = 0.002f;
        constexpr auto kRedispelAfter = std::chrono::milliseconds(500);  // C4: inside T8's 1 s limit

        RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

        bool Near(float a_lhs, float a_rhs) { return std::fabs(a_lhs - a_rhs) <= kColorTolerance; }

        // How long the caster's Firebolt needs to reach the target: distance / the projectile's speed,
        // plus a margin (a fixed second when the speed is unknown)
        Clock::duration FlightTime(const Ctx& a_c)
        {
            constexpr auto kUnknownFlight = std::chrono::seconds(1);
            constexpr float kFlightMarginSeconds = 0.3f;
            const auto* avEffect = a_c.forms.firebolt ? a_c.forms.firebolt->GetAVEffect() : nullptr;
            const auto* projectile = avEffect ? avEffect->data.projectileBase : nullptr;
            auto* caster = RefOf(a_c.caster);
            auto* target = RefOf(a_c.dummy);
            if (!projectile || projectile->data.speed <= 0.0f || !caster || !target) {
                return kUnknownFlight;
            }
            const float seconds = caster->GetPosition().GetDistance(target->GetPosition()) / projectile->data.speed +
                                  kFlightMarginSeconds;
            return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(seconds));
        }

        RE::FormID FormIdOf(RE::ObjectRefHandle a_handle)
        {
            auto* ref = RefOf(a_handle);
            return ref ? ref->GetFormID() : 0;
        }

        // Before every caster cast: still placed safely (it keeps its AI and may wander), turned to
        // the target; FAIL without casting otherwise
        // a_explosion (I1): the caster's cast explodes at the target, so the margin is checked (skipped, not failed)
        Prepared PrepareCaster(Ctx& a_c, const char* a_id, bool a_explosion)
        {
            if (BringCasterHome(a_c, a_id)) {  // C3: it keeps its AI and wandered up to 8,400 units off in run 11:11
                a_c.waitingFor = "the caster's move back home to take effect";
                return Prepared::kWait;  // G2: cast on a later tick
            }
            auto* caster = ActorOf(a_c.caster);
            auto* target = RefOf(a_c.dummy);
            auto* player = Player();
            if (!caster || !target || !player) {
                SelfTestLog::Fail(a_id, "the caster, the target or the player is gone: nothing cast");
                return Prepared::kFailed;
            }
            const auto pos = caster->GetPosition();
            const float toTarget = pos.GetDistance(target->GetPosition());
            if (a_explosion && !ExplosionRoom(a_c, a_id, true)) {
                return Prepared::kSkipped;
            }
            SelfTestWorld::FaceTarget(caster, target);
            SelfTestLog::Info(a_id, std::format("caster at ({:.0f}, {:.0f}, {:.0f}), {:.0f} from the target, turned to it",
                pos.x, pos.y, pos.z, toTarget));
            return Prepared::kReady;
        }

        bool StepExplosion(Ctx& a_c, bool a_first, bool a_byPlayer)
        {
            const char* id = a_byPlayer ? "T6" : "T6-npc";
            const auto* expected = a_c.forms.fireballExplosion;
            if (a_first) {
                if (!NeedActors(a_c, id)) {
                    return true;
                }
                a_c.phase = 0;
            }
            if (a_c.phase == 0) {
                // T6-npc: the player's Fireball explosion must have ended first, so its impact swaps cannot land in
                // the caster's count (T6-npc-swap)
                constexpr auto kPlayerExplosionSettle = std::chrono::milliseconds(2500);
                if (!a_byPlayer && a_c.lastCast && Clock::now() - *a_c.lastCast < kPlayerExplosionSettle) {
                    a_c.waitingFor = "the player's Fireball explosion to end";
                    return false;
                }
                if (a_byPlayer && !ExplosionRoom(a_c, id, false)) {
                    a_c.t6Skipped = true;
                    return true;
                }
                if (!a_byPlayer) {
                    const auto prepared = PrepareCaster(a_c, id, true);
                    a_c.t6NpcSkipped = prepared == Prepared::kSkipped;
                    if (prepared != Prepared::kReady) {
                        return prepared != Prepared::kWait;
                    }
                }
                a_c.phase = 1;
                a_c.mark = SelfTestProbe::Mark();
                (a_byPlayer ? a_c.swapsAtT6 : a_c.swapsAtT6npc) = ImpactSwapCount();
                if (a_byPlayer) {
                    a_c.hazardSwapsAtT6 = HazardSwapCount();
                    a_c.mappedAtT6 = ImpactMappedSeenCount();
                }
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
                if (a_byPlayer) {
                    // C2: a copy by the copy and the palette in its 3D, a non-copy by the runtime recolour (when on)
                    const auto verdict = JudgeGrey(r.copyBase, r.recolored, r.stats.Recolored(), a, GetConfig().explosion);
                    if (!verdict.judged) {
                        SelfTestLog::Info(id, verdict.how + " | " + DescribeExplosion(r));
                    } else {
                        SelfTestLog::Check(id, verdict.pass, verdict.how + " | " + DescribeExplosion(r));
                    }
                    return true;
                }
                SelfTestLog::Check(id, !r.recolored && a.effectProperties > 0 && a.withGrey == 0, std::format("{} effect "
                    "shapes in its 3D, {} grey | {}", a.effectProperties, a.withGrey, DescribeExplosion(r)));
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
            if (!projectile) {
                SelfTestLog::Info("T4-light", "not checked: the reference is not a projectile any more");
                return;
            }
            auto* light = ProjectileLight(projectile);
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

    }

    // ---------------------------------------------------------------------
    // T5: the player's hit gets our shader from the engine, watched for a second after it shows
    // ---------------------------------------------------------------------

    bool StepPlayerBurn(Ctx& a_c, bool a_first)
    {
        if (a_first) {
            a_c.greySeen.reset();
            a_c.secondAttempted = false;
            a_c.secondCast = false;
            a_c.engineLiveAfterGrey = false;
            if (!NeedActors(a_c, "T5")) {
                return true;
            }
            StartAim(a_c.aim, a_c.t3Mark);  // T3's cast is the first
        }
        const auto* ours = GetForms().burnShader;
        const auto engines = EngineShadersOf(a_c.forms.firebolt);
        const auto shaders = SelfTestWorld::ShadersOn(a_c.dummy);
        if (!a_c.greySeen && ours &&
            std::ranges::any_of(shaders, [&](const auto& s) { return !s.finished && s.effectData == ours->GetFormID(); })) {
            a_c.greySeen = Clock::now();
            // T5-second's hit, cast now; T5-second judges its swap
            a_c.secondAttempted = true;
            a_c.secondMark = SelfTestProbe::Mark();
            a_c.secondCast = CastAtTarget(a_c, Player(), a_c.forms.firebolt, "T5-second");
        }
        if (a_c.greySeen && EngineLive(shaders, engines)) {
            a_c.engineLiveAfterGrey = true;
        }
        // C3/D3: judged only after a hit confirmed by TESHitEvent (up to three casts, then FAIL "aim: no hit after 3
        // casts"). E1: once our shader is seen, no recast, only hits from before T5-second's cast count, and the shader
        // itself is the evidence of the hit (the line lists every hit event on the target)
        // F1: the bound applies whenever T5-second's cast exists (it is made the moment our shader is first seen)
        const auto aim = ConfirmHit(a_c, a_c.aim, a_c.forms.firebolt, "T5", !a_c.greySeen,
            a_c.secondAttempted ? a_c.secondMark : 0);
        if (aim == AimResult::kGaveUp) {
            return true;
        }
        if (aim != AimResult::kHit && !a_c.greySeen) {
            a_c.waitingFor += " | hits on the target: " + HitsListed(a_c, a_c.aim.firstMark);
            return false;
        }
        a_c.waitingFor = std::format("our shader alight on the target, then {} s of watching; shaders seen: {}; swaps: {}; hits on the "
            "target: {}", kBurnWatch.count(), DescribeShaders(shaders), DescribeBurns(a_c.t3Mark), HitsListed(a_c, a_c.aim.firstMark));
        if (!a_c.greySeen || Clock::now() - *a_c.greySeen < kBurnWatch) {
            return false;
        }
        const auto window = std::chrono::duration<float>(Clock::now() - *a_c.greySeen).count();
        const auto target = FormIdOf(a_c.dummy);
        const auto burns = SelfTestProbe::BurnsSince(a_c.t3Mark);
        const auto swap = std::ranges::find_if(burns, [&](const auto& b) { return b.swapped && b.target == target; });
        const bool swapped = swap != burns.end();
        const bool engineOk = !engines.empty() && !a_c.engineLiveAfterGrey;
        SelfTestLog::Check("T5", swapped && engineOk, std::format("our shader alight on the target; the hook swapped the "
            "player's hit: {}; Firebolt's engine hit shaders known: {}; one of them unfinished at {:.1f} s or older in "
            "the {:.2f} s checked: {}; engine shaders now: {}; swaps: {}", swapped ? "yes" : "NO", engines.size(),
            kEngineAgeMin, window, a_c.engineLiveAfterGrey ? "YES" : "no", DescribeEngineShaders(shaders, engines),
            DescribeBurns(a_c.t3Mark)) + std::format("; hit event before T5-second's cast: {}; hits on the target: {}",
            a_c.aim.hit ? "yes" : "none - our shader on the target is the evidence", HitsListed(a_c, a_c.aim.firstMark)));
        return true;
    }

    // A second hit while ours is alight is swapped too, and no engine hit shader comes up
    bool StepSecondHit(Ctx& a_c, bool a_first)
    {
        if (a_first) {
            if (!a_c.secondAttempted) {
                SelfTestLog::Fail("T5-second", "skipped: no second hit (T5 never saw our shader)");
                return true;
            }
            if (!a_c.secondCast) {
                return true;  // CastAtTarget already logged this step's FAIL
            }
            a_c.mark = a_c.secondMark;  // cast by T5 the moment our shader showed
            a_c.secondSwapSeen.reset();
        }
        // A swap of a NEW controller: none of the controllers recorded before the second cast (asked
        // again) may end the wait
        const auto target = FormIdOf(a_c.dummy);
        // One hit = (controller, ActiveEffect address): a controller address reused by a later effect is a new hit (H3: not
        // the effect's usUniqueID, 0 on most effects)
        std::vector<std::pair<std::uintptr_t, std::uintptr_t>> before;
        for (const auto& b : SelfTestProbe::BurnsSince(a_c.t3Mark)) {
            if (b.seq < a_c.mark) {
                before.emplace_back(b.controller, b.effect);
            }
        }
        const auto burns = SelfTestProbe::BurnsSince(a_c.mark);
        const bool newSwap = std::ranges::any_of(burns, [&](const auto& b) {
            return b.swapped && b.target == target &&
                   std::ranges::find(before, std::pair{ b.controller, b.effect }) == before.end();
        });
        if (!newSwap) {
            a_c.waitingFor = std::format("the hook's swap for the second Firebolt hit (seen: {})", DescribeBurns(a_c.mark));
            return false;
        }
        if (!a_c.secondSwapSeen) {
            a_c.secondSwapSeen = Clock::now();
        }
        if (Clock::now() - *a_c.secondSwapSeen < kSecondJudgeDelay) {
            a_c.waitingFor = "the judging delay after the second swap";
            return false;
        }
        const auto* ours = GetForms().burnShader;
        const auto engines = EngineShadersOf(a_c.forms.firebolt);
        const auto shaders = SelfTestWorld::ShadersOn(a_c.dummy);
        const auto liveOurs = std::ranges::count_if(shaders, [&](const auto& s) {
            return ours && !s.finished && s.effectData == ours->GetFormID();
        });
        SelfTestLog::Check("T5-second", liveOurs >= 1 && !engines.empty() && !EngineLive(shaders, engines),
            std::format("second hit swapped (a new controller); our shaders alight on the target: {}; Firebolt's engine "
                "shaders known: {}; engine shaders: {}; calls: {}", liveOurs, engines.size(),
                DescribeEngineShaders(shaders, engines), DescribeBurns(a_c.mark)));
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
            // T5-second's projectile must be on record first, or it could pass for T4's player one
            const bool secondSeen = !a_c.secondCast || FindProjectile(a_c.secondMark, true, a_c.forms.firebolt);
            const bool waited = Clock::now() - a_c.stepStart >= kSecondRecordWait;
            if (!secondSeen && !waited) {
                a_c.waitingFor = "T5-second's projectile record before casting";
                return false;
            }
            if (!secondSeen) {
                SelfTestLog::Info("T4", "T5-second's projectile never recorded: the player record below may be it");
            }
            if (const auto prepared = PrepareCaster(a_c, "T4", false); prepared != Prepared::kReady) {  // Firebolt: no margin
                return prepared != Prepared::kWait;
            }
            // The engine hit shaders already on the target (T3, T5-second): not the caster's
            a_c.engineBeforeT4.clear();
            const auto engines = EngineShadersOf(a_c.forms.firebolt);
            for (const auto& s : SelfTestWorld::ShadersOn(a_c.dummy)) {
                if (std::ranges::find(engines, s.effectData) != engines.end()) {
                    a_c.engineBeforeT4.push_back(SeenNow(s));
                }
            }
            // The caster first, so its colour keys are on record before the player's projectile
            // exists; then the player in the same frame: two live projectiles, so equal property
            // pointers would mean sharing, not a freed one's memory reused
            a_c.mark = SelfTestProbe::Mark();
            a_c.phase = 1;
            a_c.waitingFor = "the caster's and the player's Firebolt projectiles in the hook";
            if (!CastAtTarget(a_c, ActorOf(a_c.caster), a_c.forms.firebolt, "T4")) {
                return true;
            }
            a_c.npcHitEta = Clock::now() + FlightTime(a_c);
            return !CastAtTarget(a_c, Player(), a_c.forms.firebolt, "T4");
        }
        if (a_c.phase == 1) {
            a_c.playerT4 = FindProjectile(a_c.mark, true, a_c.forms.firebolt);
            a_c.npcT4 = FindProjectileFrom(a_c.mark, a_c.casterId, a_c.forms.firebolt);
            if (!a_c.playerT4 || !a_c.npcT4) {
                a_c.waitingFor = std::format("the caster's and the player's Firebolt projectiles in the hook (player {}, "
                    "caster {})", a_c.playerT4 ? "seen" : "not seen", a_c.npcT4 ? "seen" : "not seen");
                return false;
            }
            a_c.phase = 2;  // judged on a later tick, a tick or more after the recolour
            return false;
        }
        const auto& npc = *a_c.npcT4;
        const auto& player = *a_c.playerT4;
        // V9/W5: with the recolour off the copies are judged instead (the caster's original, the player's copy); with no
        // copy expected (copyLaunch off, the Launch detour not hooked, no copy) there is nothing to judge: INFO
        const bool judgeCopies = !GetConfig().projectile;
        if (judgeCopies && !CopyLaunchExpected(a_c.forms.firebolt)) {
            SelfTestLog::Info("T4", std::format("not judged: recolour off and no projectile copy expected | caster: {} | player: {}",
                DescribeProjectile(npc), DescribeProjectile(player)));
        } else {
            const bool copyOk = !judgeCopies || (npc.copyBase == 0 && player.copyBase != 0);
            const bool ok = !npc.recolored && npc.after.effectProperties > 0 && npc.after.withGrey == 0 &&
                            player.after.effectProperties > 0 && (player.recolored || judgeCopies) && copyOk;
            SelfTestLog::Check("T4", ok, std::format("caster: {} | player: recoloured={}, copy {:08X}, {} effect properties",
                DescribeProjectile(npc), player.recolored, player.copyBase, player.after.effectProperties));
        }

        // Compared only while both projectiles still exist, inspected now
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

    // The caster's hit keeps its own engine shader: the hook recorded it KEPT (a fire effect someone
    // else cast) for the target, and an engine Firebolt shader that was not there before T4 is alight.
    bool StepNpcBurnInfo(Ctx& a_c, bool a_first)
    {
        if (a_first && !NeedActors(a_c, "T5-npc")) {
            return true;
        }
        if (a_c.npcHitEta && Clock::now() < *a_c.npcHitEta) {
            a_c.waitingFor = "the caster's projectile to arrive";
            return false;
        }
        const auto since = a_c.npcHitEta ? Clock::now() - *a_c.npcHitEta : Clock::now() - a_c.stepStart;
        const auto engines = EngineShadersOf(a_c.forms.firebolt);
        const auto shaders = SelfTestWorld::ShadersOn(a_c.dummy);
        const auto target = FormIdOf(a_c.dummy);
        const auto burns = SelfTestProbe::BurnsSince(a_c.mark);
        const bool kept = std::ranges::any_of(burns, [&](const auto& b) { return !b.swapped && b.target == target; });
        a_c.waitingFor = "the hook's KEPT record for the caster's hit and its engine hit shader on the target";
        const bool casterShader = EngineLive(shaders, engines, [&](const SelfTestWorld::ShaderOn& a_shader) {
            return SeenBefore(a_shader, a_c.engineBeforeT4);
        });
        if (!kept) {
            if (since < kNpcBurnWait) {
                return false;
            }
            SelfTestLog::Info("T5-npc", std::format("cannot judge: the hook recorded no kept call for the caster's hit "
                "within {} ms (calls: {})", kNpcBurnWait.count(), DescribeBurns(a_c.mark)));
            return true;
        }
        if (casterShader) {
            SelfTestLog::Pass("T5-npc", std::format("the caster's engine Firebolt hit shader is still unfinished at {:.1f} s "
                "or older: {}", kEngineAgeMin, DescribeShaders(shaders)));
            return true;
        }
        if (since < kNpcBurnWait) {
            return false;
        }
        SelfTestLog::Fail("T5-npc", std::format("no engine Firebolt hit shader of the caster's (new since T4) unfinished "
            "on the target within {} ms of its arrival: {}", kNpcBurnWait.count(), DescribeShaders(shaders)));
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
            SelfTestLog::Info("T7", std::format("first BSPSysSimpleColorModifier colour keys (0x30-0x5F): {} -> {}",
                DescribeColorKeys(keysOf.front()),
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
        if (a_c.npcT4->seq >= a_c.playerT4->seq) {
            SelfTestLog::Info("T7", "order not guaranteed: the caster's projectile was recorded after the player's, so "
                "its keys at its hook may already be the player's desaturation - not judged");
            return true;
        }
        // Already contaminated before T4: a shared modifier desaturated at T3 would show the
        // desaturated T3 keys at the caster's hook
        const bool desaturatedAtHook = std::ranges::any_of(npc.modifierKeys, [&](const ColorKeys& a_keys) {
            return std::ranges::any_of(a_c.t3ModifierKeys, [&](const ColorKeys& a_original) {
                return MatchesDesaturated(a_keys, a_original);
            });
        });
        const bool t3Comparable = std::ranges::any_of(a_c.t3ModifierKeys, [](const ColorKeys& a_k) { return HasColour(a_k); });
        const int shared = CommonCount(npc.colorModifiers, player.colorModifiers);
        SelfTestLog::Check("T7", shared == 0 && !*a_c.npcModifiersChanged && !desaturatedAtHook, std::format("caster "
            "modifiers: {}, {} shared with the player's, keys changed between its hook and T4: {}, keys at its hook equal "
            "to the desaturated T3 keys: {}", npc.colorModifiers.size(), shared, *a_c.npcModifiersChanged ? "YES" : "no",
            !t3Comparable ? "not compared (no coloured T3 keys)" : (desaturatedAtHook ? "YES" : "no")));
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
            SelfTestLog::Info("T8", std::format("dispelled {} Stoke effect(s) with MagicTarget::DispelEffect", dispelled));
            a_c.waitingFor = "IsStoked() false after dispelling the Stoke effect";
            a_c.redispelled = false;
            a_c.redispelTried = false;
            return false;
        }
        if (IsStoked() && !a_c.redispelTried && Clock::now() - a_c.stepStart >= kRedispelAfter) {
            // C4: a dispel that did not hold leaves a live effect; once more, inside the same 1 s limit. E2: it counts as
            // "did not hold" only when the second dispel found a live effect
            a_c.redispelTried = true;
            const auto state = SelfTestWorld::DescribeStokeEffects();
            const int again = SelfTestWorld::DispelStoke();
            a_c.redispelled = again > 0;
            SelfTestLog::Info("T8", again > 0 ? std::format("the dispel did not hold after {} ms ({}): dispelled {} again",
                                                    kRedispelAfter.count(), state, again) :
                                                std::format("after {} ms the first dispel held (no live Stoke effect: {}); the "
                                                    "flag recompute is late", kRedispelAfter.count(), state));
        }
        if (!IsStoked()) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - a_c.stepStart);
            // D3: PASS only when the first dispel held; a needed second one is a FAIL (the flags are in the log lines)
            SelfTestLog::Check("T8", !a_c.redispelled, std::format("buff flag off {} ms after the dispel{}", ms.count(),
                a_c.redispelled ? " - only after a SECOND dispel at 500 ms: the first did not hold (flags before and after in the "
                                  "'self-test dispel' log lines)" : ", the first dispel held"));
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
        // D3: the unbuffed launch never takes a copy, whatever the recolour setting. E4: T3's part counts only when T3 was
        // judged; otherwise it is "not judged" and the unbuffed launch is still judged
        const bool ok = !r->recolored && r->after.effectProperties > 0 && r->after.withGrey == 0 && r->copyBase == 0 &&
                        (!a_c.t3Judged || a_c.t3Recolored);
        SelfTestLog::Check("T8-cast", ok, std::format("T3 grey (copy/recolour): {} | unbuffed launch: copy {:08X}, {} effect "
            "shapes, {} grey | {}", !a_c.t3Judged ? "not judged" : (a_c.t3Recolored ? "yes" : "NO"), r->copyBase,
            r->after.effectProperties, r->after.withGrey, DescribeProjectile(*r)));
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
            CleanupArtSpells();  // J3: whatever path E6 took
            a_c.waitingFor = "projectiles in flight before deleting the actors";
        }
        if (a_c.lastCast && Clock::now() - *a_c.lastCast < kInFlightWait) {
            return false;
        }
        RemoveRemoteMarkers(a_c);  // left behind when T3-remote timed out
        if (a_c.extraTarget.get()) {
            SelfTestLog::Info("T9", "E2 target: " + SelfTestWorld::Remove(a_c.extraTarget));  // E2 timed out
        }
        SelfTestLog::Info("T9", std::format("target: {}; caster: {}", SelfTestWorld::Remove(a_c.dummy),
            SelfTestWorld::Remove(a_c.caster)));
        SelfTestLog::Info("T-hand", "hand art: manual (CastSpellImmediate has no charge animation, so there is no "
            "casting art to inspect)");
        SelfTestLog::Info("T-bound", "bound fire weapon and its arrows: manual (vanilla has no bound weapon with a fire "
            "effect)");
        SelfTestLog::Info("T-placed", "script-placed fire (PlaceAtMe, Cast, RemoteCast from a script of the player's "
            "fire effect): manual (no test script ships)");
        a_c.cleanedUp = true;
        return true;
    }
}
