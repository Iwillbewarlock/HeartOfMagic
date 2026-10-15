#include "SelfTestCtx.h"

#include "Attribution.h"
#include "ScriptTags.h"
#include "SelfTestLog.h"
#include "SelfTestProbe.h"
#include "SelfTestText.h"
#include "SelfTestWorld.h"

#include <format>
#include <string_view>

namespace BlackFlame::SelfTest
{
    namespace
    {
        using namespace SelfTestText;

        // Skyrim.esm XMarker (STAT): any non-actor reference can be a RemoteCast source; this vanilla
        // marker is only the test's prop, nothing in the layers knows it
        constexpr RE::FormID kXMarkerId = 0x0000003B;

        enum Phase : int
        {
            kCastTagged = 0,
            kWaitTagged,
            kCastBlamed,
            kWaitBlamed,
            kSettle
        };

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

        // Why the blame marker's projectile was or was not the player's, from the marker's own caster
        std::string DescribeBlame(Ctx& a_c, const SelfTestProbe::ProjectileRecord& a_record)
        {
            if (a_record.byPlayer) {
                return std::format("yes, through its {}", a_record.link);
            }
            const auto marker = a_c.blameMarker.get();
            auto* caster = marker ? NonActorCasterOf(marker.get()) : nullptr;
            if (!caster) {
                return "no - the marker has no NonActorMagicCaster to read a blame actor from";
            }
            const auto blame = caster->blameActor.get();
            return blame ? std::format("no - the marker's caster blames {:08X}, not the player", blame->GetFormID()) :
                           "no - the engine did not keep the blame actor on the marker's caster";
        }

        // A14: whether the engine fills an explosion caster's object reference (checked against the tags)
        void LogExplosionCasters(const Ctx& a_c)
        {
            std::string seen;
            for (const auto& r : SelfTestProbe::ExplosionsSince(a_c.mark)) {
                seen += std::format("{}[base {:08X}, caster object {:08X}, the player's through {}]", seen.empty() ? "" : " ",
                    r.base, r.casterRef, r.link);
            }
            SelfTestLog::Info("T3-remote", std::format("explosions since the marker casts: {}",
                seen.empty() ? "none recorded" : seen));
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

    // T3-remote: the script-tag rule. A placed marker (not an actor), tagged as the native wrapper tags
    // a Cast's akSource, casts Firebolt at the target with no blame actor: PASS only when the projectile
    // it shot was the player's through the script tag and was recoloured. T3-blame (INFO), cast only
    // after that one is on record: a second marker with the player as blame actor and no tag. The step
    // ends once both projectiles can have finished, so no extra hit leaks into a later step.
    bool StepRemoteCast(Ctx& a_c, bool a_first)
    {
        if (a_first) {
            RemoveRemoteMarkers(a_c);
            a_c.remoteMarkerId = 0;
            a_c.blameMarkerId = 0;
            if (!NeedActors(a_c, "T3-remote")) {
                return true;
            }
            a_c.mark = SelfTestProbe::Mark();
        }
        switch (a_c.phase) {
        case kCastTagged:
            if (!CastFromMarker(a_c, true, false, a_c.remoteMarker, a_c.remoteMarkerId)) {
                SelfTestLog::Fail("T3-remote", "the tagged marker could not be placed or has no magic caster");
                RemoveRemoteMarkers(a_c);
                return true;
            }
            a_c.phase = kWaitTagged;
            a_c.waitingFor = "the tagged marker's Firebolt projectile in the hook";
            return false;
        case kWaitTagged:
            {
                const auto tagged = FindProjectileFrom(a_c.mark, a_c.remoteMarkerId, a_c.forms.firebolt);
                if (!tagged) {
                    return false;  // the step's timeout ends it (logged as a timeout)
                }
                const bool byTag = std::string_view(tagged->link) == PlayerLinkName(PlayerLink::kTagged);
                SelfTestLog::Check("T3-remote", tagged->byPlayer && byTag && tagged->recolored,
                    std::format("projectile shot by the tagged marker {:08X}, the player's through its {}: {}",
                        a_c.remoteMarkerId, tagged->link, DescribeProjectile(*tagged)));
                a_c.phase = kCastBlamed;
                return false;
            }
        case kCastBlamed:
            if (!CastFromMarker(a_c, false, true, a_c.blameMarker, a_c.blameMarkerId)) {
                SelfTestLog::Info("T3-blame", "not run: the second marker could not be placed or has no magic caster");
                a_c.phase = kSettle;
                return false;
            }
            a_c.phase = kWaitBlamed;
            a_c.waitingFor = "the blame marker's Firebolt projectile in the hook";
            return false;
        case kWaitBlamed:
            {
                const auto blamed = FindProjectileFrom(a_c.mark, a_c.blameMarkerId, a_c.forms.firebolt);
                if (!blamed && Clock::now() - *a_c.lastCast < kInFlightWait) {
                    return false;
                }
                SelfTestLog::Info("T3-blame", blamed ? std::format("projectile shot by the marker {:08X} with the player "
                    "as blame: {} (the player's: {})", a_c.blameMarkerId, DescribeProjectile(*blamed),
                    DescribeBlame(a_c, *blamed)) : "no projectile recorded from the blame marker");
                a_c.phase = kSettle;
                return false;
            }
        default:
            if (a_c.lastCast && Clock::now() - *a_c.lastCast < kInFlightWait) {
                a_c.waitingFor = "the marker projectiles to finish";
                return false;
            }
            LogExplosionCasters(a_c);
            RemoveRemoteMarkers(a_c);
            return true;
        }
    }
}
