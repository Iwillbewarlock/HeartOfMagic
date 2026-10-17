#include "game/ToxApply.h"

#include "core/ToxCore.h"
#include "game/ToxData.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace
{
    // =========================================================================
    // Fallbacks decided by the in-game self test (Task 7)
    // =========================================================================

    // Self test step 2: when the poison cast by the player's instant caster lands on the player
    // (a potion's delivery is Self), set this to cast from the target's own instant caster with
    // the player as blame actor. Effects whose caster is the target then count as ours too.
    constexpr bool kCastFromTarget = false;

    // Self test step 3: when elapsedSeconds = 0 does not lengthen the effect (or breaks a fading
    // or scripted effect), set this to refresh by dispelling our effects and casting again.
    constexpr bool kRefreshByRecast = false;

    // =========================================================================
    // Constants
    // =========================================================================

    // MagicCaster::CastSpellImmediate arguments (RE/M/MagicCaster.h:62); spec 5.4 step 3
    constexpr bool kNoHitEffectArt = false;
    constexpr float kFullEffectiveness = 1.0f;
    constexpr bool kHostileEffectivenessOnly = false;
    constexpr float kNoMagnitudeOverride = 0.0f;
    constexpr float kRestartedElapsedSeconds = 0.0f;
    constexpr double kNeverActed = -1.0;  // IntervalElapsed: < 0 means never seen

    // =========================================================================
    // State (game main thread only, ToxApply.h)
    // =========================================================================

    using Clock = std::chrono::steady_clock;
    using EffectFlag = RE::ActiveEffect::Flag;
    using EffectList = RE::BSSimpleList<RE::ActiveEffect*>;

    // An effect our cast created. The pointer is only an identity: it is dereferenced only after
    // it was found again in the target's current list (the engine frees ended effects).
    struct OwnedEffect
    {
        RE::ActiveEffect* effect = nullptr;
        RE::ActorHandle caster;
        std::uint16_t uniqueId = 0;
    };

    // An effect seen in a list before our cast: the address plus usUniqueID, so a freed address
    // reused by the cast's new effect is not mistaken for the old one
    struct SeenEffect
    {
        const RE::ActiveEffect* effect = nullptr;
        std::uint16_t uniqueId = 0;
    };

    struct Application
    {
        double lastActionSeconds = kNeverActed;
        // kForeignPresent / kNoCaster are not interval-limited: each is logged once until the
        // outcome changes or the next refresh or cast
        std::optional<ToxGame::ApplyOutcome> quietLogged;
        std::vector<OwnedEffect> effects;
    };

    // Why a cast left nothing to record (kCastNoEffect), for Task 7's fallback decisions
    enum class NoEffectReason
    {
        kInstantOnly,   // no effect of the poison has a duration > 0: nothing lasting to record
        kNoTargetList,  // the target still had no active effect list after the cast
        kOnCaster,      // a new effect of the poison showed up on the player (Self delivery)
        kNowhere,       // neither on the target nor on the player
        kCount
    };

    // One entry per (target, poison) pair acted on this session, emptied on every load and new
    // game. A long session holds a few hundred small entries; prune by age if that ever matters.
    std::unordered_map<std::uint64_t, Application> g_applications;
    std::array<bool, static_cast<std::size_t>(NoEffectReason::kCount)> g_warnedNoEffect{};
    bool g_refreshLogged = false;

    // =========================================================================
    // Helpers
    // =========================================================================

    double NowSeconds()
    {
        return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    }

    std::uint64_t ApplicationKey(RE::FormID a_target, RE::FormID a_poison)
    {
        return (static_cast<std::uint64_t>(a_target) << 32) | a_poison;
    }

    // Spec 12: kInactive and kDispelled count as gone (RE/A/ActiveEffect.h:45-54)
    bool IsLive(const RE::ActiveEffect& a_effect)
    {
        return a_effect.flags.none(EffectFlag::kInactive, EffectFlag::kDispelled);
    }

    bool IsInList(EffectList& a_list, const RE::ActiveEffect* a_effect)
    {
        return std::find(a_list.begin(), a_list.end(), a_effect) != a_list.end();
    }

    // Drops the records whose effect left the list, changed, or is no longer live; returns the rest
    std::vector<RE::ActiveEffect*> TakeLiveOwned(EffectList& a_list, const RE::MagicItem* a_poison,
        std::vector<OwnedEffect>& a_owned)
    {
        std::vector<RE::ActiveEffect*> live;
        std::erase_if(a_owned, [&](const OwnedEffect& a_record) {
            if (!a_record.effect || !IsInList(a_list, a_record.effect)) return true;
            const auto& effect = *a_record.effect;
            if (effect.spell != a_poison || effect.caster != a_record.caster ||
                effect.usUniqueID != a_record.uniqueId || !IsLive(effect)) {
                return true;
            }
            live.push_back(a_record.effect);
            return false;
        });
        return live;
    }

    bool HasLiveEffectOf(EffectList& a_list, const RE::MagicItem* a_poison)
    {
        return std::any_of(a_list.begin(), a_list.end(), [&](const RE::ActiveEffect* a_effect) {
            return a_effect && a_effect->spell == a_poison && IsLive(*a_effect);
        });
    }

    // The poison's effects in a list (null list = none)
    std::vector<SeenEffect> EffectsOf(EffectList* a_list, const RE::MagicItem* a_poison)
    {
        std::vector<SeenEffect> found;
        if (!a_list) return found;
        for (const auto* effect : *a_list) {
            if (effect && effect->spell == a_poison) found.push_back({ effect, effect->usUniqueID });
        }
        return found;
    }

    bool WasSeen(const std::vector<SeenEffect>& a_before, const RE::ActiveEffect* a_effect)
    {
        return std::any_of(a_before.begin(), a_before.end(), [&](const SeenEffect& a_seen) {
            return a_seen.effect == a_effect && a_seen.uniqueId == a_effect->usUniqueID;
        });
    }

    bool HasNewEffectOf(EffectList* a_list, const RE::MagicItem* a_poison, const std::vector<SeenEffect>& a_before)
    {
        if (!a_list) return false;
        return std::any_of(a_list->begin(), a_list->end(), [&](const RE::ActiveEffect* a_effect) {
            return a_effect && a_effect->spell == a_poison && !WasSeen(a_before, a_effect);
        });
    }

    // RE/E/Effect.h:34 (GetDuration reads baseEffect without a null check, src/RE/E/Effect.cpp:57-62)
    bool HasLastingEffect(const RE::MagicItem* a_poison)
    {
        return std::any_of(a_poison->effects.begin(), a_poison->effects.end(), [](const RE::Effect* a_effect) {
            return a_effect && a_effect->baseEffect && a_effect->GetDuration() > 0;
        });
    }

    // A live effect whose record has a duration (ActiveEffect::effect, RE/A/ActiveEffect.h:105;
    // GetDuration, RE/E/Effect.h:34): an instant one is nothing to renew
    bool IsLiveLasting(const RE::ActiveEffect& a_effect)
    {
        return IsLive(a_effect) && a_effect.effect && a_effect.effect->baseEffect && a_effect.effect->GetDuration() > 0;
    }

    // The live, lasting effects of a_poison in the list now that were not there before the cast and
    // whose caster is the player (or, casting from the target, the target)
    std::vector<OwnedEffect> NewOwnedEffects(EffectList& a_list, const RE::MagicItem* a_poison,
        const std::vector<SeenEffect>& a_before, const RE::ActorHandle& a_player,
        const RE::ActorHandle& a_target)
    {
        std::vector<OwnedEffect> owned;
        for (auto* effect : a_list) {
            if (!effect || effect->spell != a_poison) continue;
            if (WasSeen(a_before, effect) || !IsLiveLasting(*effect)) continue;
            const bool ourCaster = effect->caster == a_player || (kCastFromTarget && effect->caster == a_target);
            if (!ourCaster) continue;
            owned.push_back({ effect, effect->caster, effect->usUniqueID });
        }
        return owned;
    }

    // Spec 5.4 step 3; the caster per kCastFromTarget (self test step 2)
    bool CastPoison(RE::Actor* a_target, RE::AlchemyItem* a_poison, RE::Actor* a_player)
    {
        RE::Actor* casterActor = a_player;
        if constexpr (kCastFromTarget) {
            casterActor = a_target;
        }
        // RE/A/Actor.h:324, RE/M/MagicSystem.h:26-34
        auto* caster = casterActor->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
        if (!caster) return false;
        caster->CastSpellImmediate(a_poison, kNoHitEffectArt, a_target, kFullEffectiveness,
            kHostileEffectivenessOnly, kNoMagnitudeOverride, a_player);
        return true;
    }

    const char* NoEffectText(NoEffectReason a_reason)
    {
        switch (a_reason) {
        case NoEffectReason::kNoTargetList:
            return "the target has no active effect list after the cast";
        case NoEffectReason::kOnCaster:
            return "a new effect of it IS on the player (Self delivery - self test step 2, kCastFromTarget)";
        case NoEffectReason::kNowhere:
            return "no new effect of it on the target or on the player (effects added later, or resisted)";
        default:
            return "";
        }
    }

    struct DecideResult
    {
        ToxGame::ApplyOutcome outcome = ToxGame::ApplyOutcome::kTooSoon;
        std::size_t count = 0;
        NoEffectReason noEffect = NoEffectReason::kNowhere;  // meaningful for kCastNoEffect only
    };

    void LogOutcome(const DecideResult& a_result, const RE::Actor* a_target, const RE::AlchemyItem* a_poison)
    {
        const char* targetRaw = a_target->GetName();
        const char* poisonRaw = a_poison->GetName();
        const char* targetName = targetRaw ? targetRaw : "";
        const char* poisonName = poisonRaw ? poisonRaw : "";
        const auto targetId = a_target->GetFormID();
        const auto poisonId = a_poison->GetFormID();
        const char* casterText = kCastFromTarget ? "target's" : "player's";
        switch (a_result.outcome) {
        case ToxGame::ApplyOutcome::kRefreshed:
            // Every renewing hit: info the first time in the DLL's life, debug after
            spdlog::log(g_refreshLogged ? spdlog::level::debug : spdlog::level::info, "Toxicology: refreshed {} of our effect(s) "
                "of '{}' [{:08X}] on '{}' [{:08X}] (repeats at debug level)", a_result.count, poisonName, poisonId, targetName, targetId);
            g_refreshLogged = true;
            break;
        case ToxGame::ApplyOutcome::kForeignPresent:
            logger::info("Toxicology: '{}' [{:08X}] already on '{}' [{:08X}] from another source - nothing done",
                poisonName, poisonId, targetName, targetId);
            break;
        case ToxGame::ApplyOutcome::kCast:
            logger::info("Toxicology: cast '{}' [{:08X}] on '{}' [{:08X}] ({} caster), {} new effect(s) recorded",
                poisonName, poisonId, targetName, targetId, casterText, a_result.count);
            break;
        case ToxGame::ApplyOutcome::kCastNoEffect:
            if (a_result.noEffect == NoEffectReason::kInstantOnly) {
                logger::info("Toxicology: cast '{}' [{:08X}] on '{}' [{:08X}] ({} caster): instant-only poison, "
                             "nothing lasting to record", poisonName, poisonId, targetName, targetId, casterText);
                break;
            }
            if (auto& warned = g_warnedNoEffect[static_cast<std::size_t>(a_result.noEffect)]; !warned) {
                warned = true;
                logger::warn("Toxicology: cast '{}' [{:08X}] on '{}' [{:08X}] ({} caster), nothing recorded: {}; "
                             "repeats of this case are logged at debug level",
                    poisonName, poisonId, targetName, targetId, casterText, NoEffectText(a_result.noEffect));
            } else {
                logger::debug("Toxicology: cast '{}' [{:08X}] on [{:08X}], nothing recorded: {}",
                    poisonName, poisonId, targetId, NoEffectText(a_result.noEffect));
            }
            break;
        case ToxGame::ApplyOutcome::kNoCaster:
            logger::warn("Toxicology: no instant magic caster on the {} - '{}' [{:08X}] not cast on [{:08X}]",
                kCastFromTarget ? "target" : "player", poisonName, poisonId, targetId);
            break;
        case ToxGame::ApplyOutcome::kTooSoon:
            break;
        }
    }

    // Spec 5.4 step 3: cast, and remember the effects it created
    DecideResult CastAndRecord(RE::Actor* a_target, RE::AlchemyItem* a_poison, RE::Actor* a_player,
        EffectList* a_targetList, Application& a_app)
    {
        DecideResult result;
        const auto before = EffectsOf(a_targetList, a_poison);
        const auto playerBefore = EffectsOf(a_player->GetActiveEffectList(), a_poison);
        if (!CastPoison(a_target, a_poison, a_player)) {
            result.outcome = ToxGame::ApplyOutcome::kNoCaster;
            return result;
        }
        // Fetched again: the list may only exist once the cast added something
        auto* after = a_target->GetActiveEffectList();
        auto created = after ? NewOwnedEffects(*after, a_poison, before, a_player->GetHandle(), a_target->GetHandle())
                             : std::vector<OwnedEffect>{};
        result.count = created.size();
        a_app.effects.insert(a_app.effects.end(), created.begin(), created.end());
        if (!created.empty()) {
            result.outcome = ToxGame::ApplyOutcome::kCast;
            return result;
        }
        result.outcome = ToxGame::ApplyOutcome::kCastNoEffect;
        if (!HasLastingEffect(a_poison)) {
            result.noEffect = NoEffectReason::kInstantOnly;
        } else if (!after) {
            result.noEffect = NoEffectReason::kNoTargetList;
        } else if (HasNewEffectOf(a_player->GetActiveEffectList(), a_poison, playerBefore)) {
            result.noEffect = NoEffectReason::kOnCaster;
        } else {
            result.noEffect = NoEffectReason::kNowhere;
        }
        return result;
    }

    DecideResult Decide(RE::Actor* a_target, RE::AlchemyItem* a_poison, RE::Actor* a_player, Application& a_app)
    {
        // RE/A/Actor.h:373 (SE/AE vtable, no VR shim). Null = no effects yet (CommonLib's own
        // MagicTarget::HasEffectWithArchetype checks it the same way): nothing to refresh, cast.
        auto* list = a_target->GetActiveEffectList();
        if (!list) a_app.effects.clear();

        // Step 1: our effects still on the target
        auto live = list ? TakeLiveOwned(*list, a_poison, a_app.effects) : std::vector<RE::ActiveEffect*>{};
        if (!live.empty()) {
            if constexpr (kRefreshByRecast) {
                // Self test step 3 fallback: dispel ours, then cast again below
                for (auto* effect : live) effect->Dispel(true);  // ActiveEffect.h:92
                a_app.effects.clear();
            } else {
                for (auto* effect : live) effect->elapsedSeconds = kRestartedElapsedSeconds;  // ActiveEffect.h:110
                return { ToxGame::ApplyOutcome::kRefreshed, live.size() };
            }
        } else if (list && HasLiveEffectOf(*list, a_poison)) {
            // Step 2: the same poison from someone else (a coated weapon, an effect from before the load)
            return { ToxGame::ApplyOutcome::kForeignPresent };
        }
        return CastAndRecord(a_target, a_poison, a_player, list, a_app);
    }
}

// =============================================================================
// Public
// =============================================================================

ToxGame::ApplyOutcome ToxGame::ApplyOrRefresh(RE::Actor* a_target, RE::AlchemyItem* a_poison, RE::Actor* a_player)
{
    auto& app = g_applications[ApplicationKey(a_target->GetFormID(), a_poison->GetFormID())];
    const double now = NowSeconds();
    if (!Toxicology::IntervalElapsed(app.lastActionSeconds, now, GetSettings().minIntervalSeconds)) {
        return ApplyOutcome::kTooSoon;
    }
    const auto result = Decide(a_target, a_poison, a_player, app);
    const auto outcome = result.outcome;
    // Spec 5.4: the interval runs from the last refresh or cast (a cast without a visible effect
    // included, so a missing effect is not cast again on every hit)
    if (outcome == ApplyOutcome::kRefreshed || outcome == ApplyOutcome::kCast || outcome == ApplyOutcome::kCastNoEffect) {
        app.lastActionSeconds = now;
        app.quietLogged.reset();
    }
    if (outcome == ApplyOutcome::kForeignPresent || outcome == ApplyOutcome::kNoCaster) {
        if (app.quietLogged == outcome) return outcome;
        app.quietLogged = outcome;
    }
    LogOutcome(result, a_target, a_poison);
    return outcome;
}

void ToxGame::ClearApplications()
{
    g_applications.clear();
    g_warnedNoEffect.fill(false);
}
