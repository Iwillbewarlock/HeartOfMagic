// =============================================================================
// SELF-TEST AIM (v2 fix C3) - a cast at the target counts only when a TESHitEvent says it hit
// =============================================================================
// Run 11:11: the target was spawned at the player's height and fell about 300 units after T3's
// cast, so the Firebolt passed over it (TESHitEvent 0) and T5, E4 and E1a had nothing to judge.
// Now T2 waits for the target to come to rest, LaunchAt aims at its 3D bound centre, and the steps
// that judge a hit confirm it here (up to kMaxAimCasts casts) before judging.

#include "SelfTestCtx.h"

#include "FormCopies.h"
#include "SelfTestLog.h"
#include "SelfTestText.h"

#include <cmath>
#include <format>

namespace BlackFlame::SelfTest
{
    using namespace SelfTestText;

    namespace
    {
        constexpr float kHitMarginSeconds = 0.8f;        // after the flight, for the hit event to arrive
        constexpr float kUnknownFlightSeconds = 1.0f;    // when the projectile's speed is unknown
        constexpr float kCasterLeash = 200.0f;           // the caster moved further than this from home: put back

        RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

        // D3: the player's hit on the target by the spell, or by its projectile (the original base or its copy)
        bool IsOurHit(const SelfTestProbe::HitRecord& a_hit, RE::FormID a_player, RE::FormID a_target,
            const RE::SpellItem* a_spell)
        {
            if (a_hit.cause != a_player || a_hit.target != a_target || !a_spell) {
                return false;
            }
            const auto* effect = a_spell->GetAVEffect();
            const auto* base = effect ? effect->data.projectileBase : nullptr;
            const auto* copy = base ? CopyOf(base) : nullptr;
            return a_hit.source == a_spell->GetFormID() || (base && a_hit.projectile == base->GetFormID()) ||
                   (copy && a_hit.projectile == copy->GetFormID());
        }

        int HitsOnTarget(const Ctx& a_c, std::uint64_t a_mark, const RE::SpellItem* a_spell)
        {
            const auto* player = Player();
            const auto* target = RefOf(a_c.dummy);
            if (!player || !target || !a_spell) {
                return 0;
            }
            int hits = 0;
            for (const auto& hit : SelfTestProbe::HitsSince(a_mark)) {
                hits += IsOurHit(hit, player->GetFormID(), target->GetFormID(), a_spell) ? 1 : 0;
            }
            return hits;
        }

        // Every TESHitEvent on the target since a_mark, for the aim FAIL line
        std::string HitsListed(const Ctx& a_c, std::uint64_t a_mark)
        {
            const auto* target = RefOf(a_c.dummy);
            std::string out;
            for (const auto& hit : SelfTestProbe::HitsSince(a_mark)) {
                if (target && hit.target == target->GetFormID()) {
                    out += std::format("{}cause {:08X} source {:08X} projectile {:08X}", out.empty() ? "" : "; ", hit.cause,
                        hit.source, hit.projectile);
                }
            }
            return out.empty() ? std::string("none") : out;
        }

        // The flight from the player to the target at the spell's projectile speed, plus the margin
        Clock::duration HitWait(const Ctx& a_c, const RE::SpellItem* a_spell)
        {
            const auto* effect = a_spell ? a_spell->GetAVEffect() : nullptr;
            const auto* projectile = effect ? effect->data.projectileBase : nullptr;
            const auto* player = Player();
            const auto* target = RefOf(a_c.dummy);
            float seconds = kUnknownFlightSeconds;
            if (projectile && projectile->data.speed > 0.0f && player && target) {
                seconds = player->GetPosition().GetDistance(target->GetPosition()) / projectile->data.speed;
            }
            return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(seconds + kHitMarginSeconds));
        }

        std::string Point(const RE::NiPoint3& a_p)
        {
            return std::format("({:.0f}, {:.0f}, {:.0f})", a_p.x, a_p.y, a_p.z);
        }
    }

    void StartAim(Aim& a_aim, std::uint64_t a_mark)
    {
        a_aim = {};
        a_aim.mark = a_mark;
        a_aim.firstMark = a_mark;
        a_aim.casts = 1;
    }

    std::string AimGeometry(const Ctx& a_c)
    {
        const auto* player = Player();
        auto* target = RefOf(a_c.dummy);
        if (!player || !target) {
            return "geometry: the player or the target is gone";
        }
        const auto* root = target->Get3D();
        const auto bound = root ? std::format("3D bound centre {} radius {:.0f}", Point(root->worldBound.center),
                                      root->worldBound.radius) :
                                  std::string("no 3D");
        return std::format("geometry: player at {}, target at {} ({}), {:.0f} apart, target height {:.0f}, target z at rest "
                           "in T2 {:.0f}", Point(player->GetPosition()), Point(target->GetPosition()), bound,
            player->GetPosition().GetDistance(target->GetPosition()), target->GetHeight(), a_c.targetLastZ);
    }

    AimResult ConfirmHit(Ctx& a_c, Aim& a_aim, RE::SpellItem* a_spell, const char* a_id, bool a_mayRecast)
    {
        if (a_aim.hit) {
            return AimResult::kHit;
        }
        if (a_aim.gaveUp) {
            return AimResult::kGaveUp;
        }
        if (HitsOnTarget(a_c, a_aim.mark, a_spell) > 0) {
            a_aim.hit = true;
            a_aim.hitAt = Clock::now();
            SelfTestLog::Info(a_id, std::format("aim: hit confirmed by TESHitEvent (the player, {:08X}, the target) after {} "
                "cast(s)", a_spell ? a_spell->GetFormID() : 0, a_aim.casts));
            return AimResult::kHit;
        }
        const auto wait = HitWait(a_c, a_spell);
        if (!a_mayRecast || (a_c.lastCast && Clock::now() - *a_c.lastCast < wait)) {
            a_c.waitingFor = std::format("the hit of cast {} of {} (TESHitEvent)", a_aim.casts, kMaxAimCasts);
            return AimResult::kWaiting;
        }
        if (a_aim.casts >= kMaxAimCasts) {
            a_aim.gaveUp = true;
            SelfTestLog::Fail(a_id, std::format("aim: no hit after {} casts | hits on the target since the first cast: {} | {}",
                kMaxAimCasts, HitsListed(a_c, a_aim.firstMark), AimGeometry(a_c)));
            return AimResult::kGaveUp;
        }
        SelfTestLog::Info(a_id, std::format("aim: no TESHitEvent within {:.1f} s of cast {}; casting again | {}",
            std::chrono::duration<float>(wait).count(), a_aim.casts, AimGeometry(a_c)));
        // D3: the records are searched from this cast on, so the step judges the cast that hit
        a_aim.mark = SelfTestProbe::Mark();
        a_aim.newCast = true;
        if (!CastAtTarget(a_c, Player(), a_spell, a_id)) {
            a_aim.gaveUp = true;  // CastAtTarget logged this step's FAIL
            return AimResult::kGaveUp;
        }
        ++a_aim.casts;
        return AimResult::kWaiting;
    }

    void BringCasterHome(Ctx& a_c, const char* a_id)
    {
        auto* caster = ActorOf(a_c.caster);
        if (!caster || !a_c.casterHome) {
            return;
        }
        const float away = caster->GetPosition().GetDistance(*a_c.casterHome);
        if (away <= kCasterLeash) {
            return;
        }
        caster->SetPosition(*a_c.casterHome, true);  // Actor::SetPosition (RE/A/Actor.h:384): the character controller too
        SelfTestLog::Info(a_id, std::format("the caster wandered {:.0f} units from where T2 placed it: moved back to {}", away,
            Point(*a_c.casterHome)));
    }
}
