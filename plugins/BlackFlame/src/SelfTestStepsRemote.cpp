#include "SelfTestCtx.h"

#include "ScriptTags.h"
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

        // Places a marker at the player and casts Firebolt from it at the target; a_tag: tagged as a
        // source first (what the native wrapper does for a script of the player's fire effect);
        // a_blame: the player as blame actor (what Spell.RemoteCast passes)
        bool CastFromMarker(Ctx& a_c, bool a_tag, bool a_blame, RE::ObjectRefHandle& a_marker, RE::FormID& a_markerId)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* markerBase = RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarkerId);
            const auto target = a_c.dummy.get();
            if (!player || !markerBase || !target || !a_c.forms.firebolt) {
                return false;
            }
            const auto marker = player->PlaceObjectAtMe(markerBase, false);
            if (!marker) {
                return false;
            }
            a_marker = marker->GetHandle();
            a_markerId = marker->GetFormID();
            auto* caster = marker->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
            if (!caster) {
                return false;
            }
            if (a_tag) {
                TagSource(marker.get());
            }
            caster->CastSpellImmediate(a_c.forms.firebolt, false, target.get(), 1.0f, false, 0.0f, a_blame ? player : nullptr);
            a_c.lastCast = Clock::now();
            return true;
        }
    }

    void RemoveRemoteMarkers(Ctx& a_c)
    {
        for (auto* marker : { &a_c.remoteMarker, &a_c.blameMarker }) {
            if (marker->get()) {
                (void)SelfTestWorld::Remove(*marker);
            }
            *marker = {};
        }
    }

    // T3-remote: a script-tagged source. A placed marker (not an actor), tagged as the native wrapper
    // tags the akSource of a Cast from the player's fire effect, casts Firebolt at the target with no
    // blame actor: only the script-tag rule can make it the player's, and it must be recoloured.
    // T3-blame (INFO): a second marker casts with the player as blame actor and no tag - whether the
    // engine keeps that blame on the marker's NonActorMagicCaster is not known, so it is reported only.
    bool StepRemoteCast(Ctx& a_c, bool a_first)
    {
        if (a_first) {
            RemoveRemoteMarkers(a_c);
            a_c.remoteMarkerId = 0;
            a_c.blameMarkerId = 0;
            if (!NeedActors(a_c, "T3-remote")) {
                return true;
            }
        }
        if (a_c.phase == 0) {
            a_c.mark = SelfTestProbe::Mark();
            const bool tagged = CastFromMarker(a_c, true, false, a_c.remoteMarker, a_c.remoteMarkerId);
            const bool blamed = CastFromMarker(a_c, false, true, a_c.blameMarker, a_c.blameMarkerId);
            if (!tagged) {
                SelfTestLog::Fail("T3-remote", "the tagged marker could not be placed or has no magic caster");
                RemoveRemoteMarkers(a_c);
                return true;
            }
            if (!blamed) {
                SelfTestLog::Info("T3-blame", "not run: the second marker could not be placed or has no magic caster");
            }
            a_c.phase = 1;
            a_c.waitingFor = "the tagged marker's Firebolt projectile in the hook";
            return false;
        }
        const auto tagged = FindProjectileFrom(a_c.mark, a_c.remoteMarkerId, a_c.forms.firebolt);
        if (!tagged) {
            return false;  // the step's timeout ends it (logged as a timeout)
        }
        SelfTestLog::Check("T3-remote", tagged->byPlayer && tagged->recolored,
            std::format("projectile shot by the tagged marker {:08X}: {}", a_c.remoteMarkerId, DescribeProjectile(*tagged)));
        if (a_c.blameMarkerId != 0) {
            const auto blamed = FindProjectileFrom(a_c.mark, a_c.blameMarkerId, a_c.forms.firebolt);
            SelfTestLog::Info("T3-blame", blamed ? std::format("projectile shot by the marker {:08X} with the player as "
                "blame: {} (the player's: {})", a_c.blameMarkerId, DescribeProjectile(*blamed),
                blamed->byPlayer ? "yes - the blame rule matched" : "no - the engine did not keep the blame there") :
                "no projectile recorded from the blame marker yet");
        }
        RemoveRemoteMarkers(a_c);
        return true;
    }
}
