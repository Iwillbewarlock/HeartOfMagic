#include "Attribution.h"

#include "ScriptTags.h"

#include <memory>
#include <mutex>
#include <set>
#include <unordered_set>
#include <utility>

namespace BlackFlame
{
    namespace
    {
        // Diagnostic only: a cause naming the player where no rule matched (a trap the player set
        // off, or a cast we cannot see), once per layer
        void LogCauseOnce(const char* a_layer, RE::TESObjectREFR* a_ref)
        {
            static std::mutex mutex;
            static auto* logged = std::make_unique<std::unordered_set<const char*>>().release();
            {
                std::lock_guard lock(mutex);
                if (!logged->insert(a_layer).second) {
                    return;
                }
            }
            logger::info("BlackFlame: {} {:08X}: its actor cause is the player but no rule matched (owner, blame, "
                         "script tag) - left as it is (a trap the player set off looks like this)",
                a_layer, a_ref ? a_ref->GetFormID() : 0);
        }
    }

    RE::NonActorMagicCaster* NonActorCasterOf(RE::TESObjectREFR* a_ref)
    {
        if (!a_ref || a_ref->As<RE::Actor>()) {
            return nullptr;
        }
        auto* extra = a_ref->extraList.GetByType<RE::ExtraMagicCaster>();
        return extra ? skyrim_cast<RE::NonActorMagicCaster*>(extra) : nullptr;
    }

    PlayerLink LinkToPlayer(const char* a_layer, RE::TESObjectREFR* a_ref, RE::TESObjectREFR* a_owner,
        RE::NonActorMagicCaster* a_caster)
    {
        const auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return PlayerLink::kNone;
        }
        if (a_owner == player) {
            return PlayerLink::kOwner;
        }
        if (a_caster && a_caster->blameActor.get().get() == player) {
            return PlayerLink::kBlame;
        }
        if (IsTaggedSource(a_ref) || IsTaggedSource(a_owner)) {
            return PlayerLink::kTagged;
        }
        if (auto* cause = a_ref ? a_ref->GetActorCause() : nullptr; cause && cause->actor.get().get() == player) {
            LogCauseOnce(a_layer, a_ref);
        }
        return PlayerLink::kNone;
    }

    const char* PlayerLinkName(PlayerLink a_link) noexcept
    {
        switch (a_link) {
        case PlayerLink::kOwner:
            return "owner/shooter";
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
