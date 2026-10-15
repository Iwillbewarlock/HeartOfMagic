#include "SelfTestCtx.h"

#include "SelfTestLog.h"
#include "SelfTestProbe.h"
#include "SelfTestText.h"
#include "SelfTestWorld.h"

#include <format>

namespace BlackFlame::SelfTest
{
    namespace
    {
        using namespace SelfTestText;

        // Skyrim.esm XMarker (STAT): any non-actor reference can be a RemoteCast source; this vanilla
        // marker is only the test's prop, nothing in the layers knows it
        constexpr RE::FormID kXMarkerId = 0x0000003B;

        void RemoveMarker(Ctx& a_c)
        {
            if (a_c.remoteMarker.get()) {
                (void)SelfTestWorld::Remove(a_c.remoteMarker);
            }
            a_c.remoteMarker = {};
        }
    }

    // T3-remote: Spell.RemoteCast attribution. A placed marker (not an actor) casts Firebolt at the
    // target through its NonActorMagicCaster with the player as the blame actor - what RemoteCast
    // does. The shooter is the marker; the projectile must still count as the player's and be
    // recoloured (Attribution.h: actor cause or the caster's blame actor).
    bool StepRemoteCast(Ctx& a_c, bool a_first)
    {
        if (a_first) {
            a_c.remoteMarker = {};
            a_c.remoteMarkerId = 0;
            if (!NeedActors(a_c, "T3-remote")) {
                return true;
            }
        }
        if (a_c.phase == 0) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* markerBase = RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarkerId);
            const auto target = a_c.dummy.get();
            if (!player || !markerBase || !target || !a_c.forms.firebolt) {
                SelfTestLog::Info("T3-remote", "skipped: no player, XMarker, target or Firebolt");
                return true;
            }
            const auto marker = player->PlaceObjectAtMe(markerBase, false);
            auto* caster = marker ? marker->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant) : nullptr;
            if (!caster) {
                SelfTestLog::Fail("T3-remote", "the placed marker has no magic caster");
                if (marker) {
                    a_c.remoteMarker = marker->GetHandle();
                    RemoveMarker(a_c);
                }
                return true;
            }
            a_c.remoteMarker = marker->GetHandle();
            a_c.remoteMarkerId = marker->GetFormID();
            a_c.mark = SelfTestProbe::Mark();
            caster->CastSpellImmediate(a_c.forms.firebolt, false, target.get(), 1.0f, false, 0.0f, player);
            a_c.lastCast = Clock::now();
            a_c.phase = 1;
            a_c.waitingFor = "the marker's Firebolt projectile in the hook";
            return false;
        }
        const auto record = FindProjectileFrom(a_c.mark, a_c.remoteMarkerId, a_c.forms.firebolt);
        if (!record) {
            return false;  // the step's timeout ends it (logged as a timeout)
        }
        SelfTestLog::Check("T3-remote", record->byPlayer && record->recolored,
            std::format("projectile shot by the marker {:08X} with the player as blame: {}", a_c.remoteMarkerId,
                DescribeProjectile(*record)));
        RemoveMarker(a_c);
        return true;
    }
}
