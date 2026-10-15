#include "Attribution.h"

#include "ScriptTags.h"

#include <memory>
#include <mutex>
#include <set>
#include <utility>

namespace BlackFlame
{
    PlayerLink LinkToPlayer(RE::TESObjectREFR* a_ref, const RE::TESObjectREFR* a_owner, RE::NonActorMagicCaster* a_caster)
    {
        const auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return PlayerLink::kNone;
        }
        if (a_owner == player) {
            return PlayerLink::kOwner;
        }
        if (auto* cause = a_ref ? a_ref->GetActorCause() : nullptr) {
            if (cause->actor.get().get() == player) {
                return PlayerLink::kCause;
            }
        }
        if (a_caster && a_caster->blameActor.get().get() == player) {
            return PlayerLink::kBlame;
        }
        if (IsTaggedSource(a_ref) || IsTaggedSource(a_owner)) {
            return PlayerLink::kTagged;
        }
        return PlayerLink::kNone;
    }

    const char* PlayerLinkName(PlayerLink a_link) noexcept
    {
        switch (a_link) {
        case PlayerLink::kOwner:
            return "owner/shooter";
        case PlayerLink::kCause:
            return "actor cause";
        case PlayerLink::kBlame:
            return "caster's blame actor";
        case PlayerLink::kTagged:
            return "script tag";
        default:
            return "none";
        }
    }

    void LogLinkOnce(const char* a_layer, PlayerLink a_link)
    {
        if (a_link == PlayerLink::kNone) {
            return;
        }
        static std::mutex mutex;
        static auto* logged = std::make_unique<std::set<std::pair<const char*, PlayerLink>>>().release();
        {
            std::lock_guard lock(mutex);
            if (!logged->emplace(a_layer, a_link).second) {
                return;
            }
        }
        logger::info("BlackFlame: {}: first one attributed to the player through its {}", a_layer, PlayerLinkName(a_link));
    }
}
