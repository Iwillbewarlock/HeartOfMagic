#include "game/ToxApply.h"

#include "core/ToxCore.h"
#include "game/ToxData.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
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

    struct Application
    {
        double lastActionSeconds = kNeverActed;
        std::vector<OwnedEffect> effects;
    };

    // One entry per (target, poison) pair acted on this session, emptied on every load and new
    // game. A long session holds a few hundred small entries; prune by age if that ever matters.
    std::unordered_map<std::uint64_t, Application> g_applications;
    bool g_warnedNoEffect = false;

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

    std::vector<const RE::ActiveEffect*> EffectsOf(EffectList& a_list, const RE::MagicItem* a_poison)
    {
        std::vector<const RE::ActiveEffect*> found;
        for (const auto* effect : a_list) {
            if (effect && effect->spell == a_poison) found.push_back(effect);
        }
        return found;
    }

    // The effects of a_poison in the list now that were not there before the cast and whose
    // caster is the player (or, casting from the target, the target)
    std::vector<OwnedEffect> NewOwnedEffects(EffectList& a_list, const RE::MagicItem* a_poison,
        const std::vector<const RE::ActiveEffect*>& a_before, const RE::ActorHandle& a_player,
        const RE::ActorHandle& a_target)
    {
        std::vector<OwnedEffect> owned;
        for (auto* effect : a_list) {
            if (!effect || effect->spell != a_poison) continue;
            if (std::find(a_before.begin(), a_before.end(), effect) != a_before.end()) continue;
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

    void LogOutcome(ToxGame::ApplyOutcome a_outcome, const RE::Actor* a_target, const RE::AlchemyItem* a_poison,
        std::size_t a_count)
    {
        const char* targetName = a_target->GetName();
        const char* poisonName = a_poison->GetName();
        const auto targetId = a_target->GetFormID();
        const auto poisonId = a_poison->GetFormID();
        switch (a_outcome) {
        case ToxGame::ApplyOutcome::kRefreshed:
            logger::info("Toxicology: refreshed {} of our effect(s) of '{}' [{:08X}] on '{}' [{:08X}]",
                a_count, poisonName ? poisonName : "", poisonId, targetName ? targetName : "", targetId);
            break;
        case ToxGame::ApplyOutcome::kForeignPresent:
            logger::info("Toxicology: '{}' [{:08X}] already on '{}' [{:08X}] from another source - nothing done",
                poisonName ? poisonName : "", poisonId, targetName ? targetName : "", targetId);
            break;
        case ToxGame::ApplyOutcome::kCast:
            logger::info("Toxicology: cast '{}' [{:08X}] on '{}' [{:08X}] ({} caster), {} new effect(s) recorded",
                poisonName ? poisonName : "", poisonId, targetName ? targetName : "", targetId,
                kCastFromTarget ? "target's" : "player's", a_count);
            break;
        case ToxGame::ApplyOutcome::kCastNoEffect:
            if (!g_warnedNoEffect) {
                g_warnedNoEffect = true;
                logger::warn("Toxicology: cast '{}' [{:08X}] on '{}' [{:08X}] ({} caster) but no new effect of it showed "
                             "up on the target (it may have landed on the caster - self test step 2); "
                             "later casts with no effect are logged at debug level",
                    poisonName ? poisonName : "", poisonId, targetName ? targetName : "", targetId,
                    kCastFromTarget ? "target's" : "player's");
            } else {
                logger::debug("Toxicology: cast '{}' [{:08X}] on [{:08X}], no new effect on the target",
                    poisonName ? poisonName : "", poisonId, targetId);
            }
            break;
        case ToxGame::ApplyOutcome::kNoCaster:
            logger::warn("Toxicology: no instant magic caster on the {} - '{}' [{:08X}] not cast on [{:08X}]",
                kCastFromTarget ? "target" : "player", poisonName ? poisonName : "", poisonId, targetId);
            break;
        case ToxGame::ApplyOutcome::kTooSoon:
            break;
        }
    }

    ToxGame::ApplyOutcome Decide(RE::Actor* a_target, RE::AlchemyItem* a_poison, RE::Actor* a_player,
        Application& a_app, std::size_t& a_count)
    {
        auto* list = a_target->GetActiveEffectList();  // RE/A/Actor.h:373 (SE/AE vtable, no VR shim)
        if (!list) return ToxGame::ApplyOutcome::kForeignPresent;

        // Step 1: our effects still on the target
        auto live = TakeLiveOwned(*list, a_poison, a_app.effects);
        if (!live.empty()) {
            if constexpr (kRefreshByRecast) {
                // Self test step 3 fallback: dispel ours, then cast again below
                for (auto* effect : live) effect->Dispel(true);  // ActiveEffect.h:92
                a_app.effects.clear();
            } else {
                for (auto* effect : live) effect->elapsedSeconds = kRestartedElapsedSeconds;  // ActiveEffect.h:110
                a_count = live.size();
                return ToxGame::ApplyOutcome::kRefreshed;
            }
        } else if (HasLiveEffectOf(*list, a_poison)) {
            // Step 2: the same poison from someone else (a coated weapon, an effect from before the load)
            return ToxGame::ApplyOutcome::kForeignPresent;
        }

        // Step 3: cast, and remember the effects it created
        const auto before = EffectsOf(*list, a_poison);
        if (!CastPoison(a_target, a_poison, a_player)) return ToxGame::ApplyOutcome::kNoCaster;
        auto created = NewOwnedEffects(*list, a_poison, before, a_player->GetHandle(), a_target->GetHandle());
        a_count = created.size();
        a_app.effects.insert(a_app.effects.end(), created.begin(), created.end());
        return created.empty() ? ToxGame::ApplyOutcome::kCastNoEffect : ToxGame::ApplyOutcome::kCast;
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
    std::size_t count = 0;
    const auto outcome = Decide(a_target, a_poison, a_player, app, count);
    // Spec 5.4: the interval runs from the last refresh or cast (a cast without a visible effect
    // included, so a missing effect is not cast again on every hit)
    if (outcome == ApplyOutcome::kRefreshed || outcome == ApplyOutcome::kCast || outcome == ApplyOutcome::kCastNoEffect) {
        app.lastActionSeconds = now;
    }
    LogOutcome(outcome, a_target, a_poison, count);
    return outcome;
}

void ToxGame::ClearApplications()
{
    g_applications.clear();
    g_warnedNoEffect = false;
}
