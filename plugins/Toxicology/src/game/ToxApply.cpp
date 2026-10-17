#include "game/ToxApply.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace
{
    // =========================================================================
    // Fallback decided by the in-game self test (Task 7)
    // =========================================================================

    // Self test step 2: when the poison cast by the player's instant caster lands on the player
    // (a potion's delivery is Self), set this to cast from the target's own instant caster with
    // the player as blame actor.
    constexpr bool kCastFromTarget = false;

    // =========================================================================
    // Constants
    // =========================================================================

    // MagicCaster::CastSpellImmediate arguments (RE/M/MagicCaster.h:62); spec 5.4
    constexpr bool kNoHitEffectArt = false;
    constexpr float kFullEffectiveness = 1.0f;
    constexpr bool kHostileEffectivenessOnly = false;
    constexpr float kNoMagnitudeOverride = 0.0f;

    // =========================================================================
    // State (game main thread only, ToxApply.h)
    // =========================================================================

    using EffectList = RE::BSSimpleList<RE::ActiveEffect*>;

    // An effect seen in a list before our cast: the address plus usUniqueID, so a freed address
    // reused by the cast's new effect is not mistaken for the old one
    struct SeenEffect
    {
        const RE::ActiveEffect* effect = nullptr;
        std::uint16_t uniqueId = 0;
    };

    // Why a cast showed no new effect on the target (kCastNoEffect)
    enum class NoEffectReason
    {
        kInstantOnly,   // no effect of the poison has a duration > 0: nothing lasting to see
        kNoTargetList,  // the target still had no active effect list after the cast
        kOnCaster,      // a new effect of the poison showed up on the player (Self delivery)
        kNowhere,       // neither on the target nor on the player
        kCount
    };

    // (target FormID << 32 | poison FormID) of every pair cast on this session, emptied on every
    // load and new game. A long session holds a few hundred 8-byte keys.
    std::unordered_set<std::uint64_t> g_applied;
    std::array<bool, static_cast<std::size_t>(NoEffectReason::kCount)> g_warnedNoEffect{};
    // Steady-state outcome lines: info (warn for kNoCaster) the first time in the DLL's life, debug after
    bool g_alreadyLogged = false;
    bool g_castLogged = false;
    bool g_instantOnlyLogged = false;
    bool g_noCasterLogged = false;

    // =========================================================================
    // Helpers
    // =========================================================================

    std::uint64_t ApplicationKey(RE::FormID a_target, RE::FormID a_poison)
    {
        return (static_cast<std::uint64_t>(a_target) << 32) | a_poison;
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

    std::size_t CountNewEffectsOf(EffectList* a_list, const RE::MagicItem* a_poison, const std::vector<SeenEffect>& a_before)
    {
        if (!a_list) return 0;
        return static_cast<std::size_t>(std::count_if(a_list->begin(), a_list->end(), [&](const RE::ActiveEffect* a_effect) {
            return a_effect && a_effect->spell == a_poison && !WasSeen(a_before, a_effect);
        }));
    }

    // RE/E/Effect.h:34 (GetDuration reads baseEffect without a null check, src/RE/E/Effect.cpp:57-62)
    bool HasLastingEffect(const RE::MagicItem* a_poison)
    {
        return std::any_of(a_poison->effects.begin(), a_poison->effects.end(), [](const RE::Effect* a_effect) {
            return a_effect && a_effect->baseEffect && a_effect->GetDuration() > 0;
        });
    }

    // Spec 5.4; the caster per kCastFromTarget (self test step 2)
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

    struct ApplyResult
    {
        ToxGame::ApplyOutcome outcome = ToxGame::ApplyOutcome::kAlreadyApplied;
        std::size_t count = 0;                              // new effects on the target (kCast)
        NoEffectReason noEffect = NoEffectReason::kNowhere;  // meaningful for kCastNoEffect only
    };

    // Info (warn) the first time a_logged is false, debug after
    spdlog::level::level_enum FirstThenDebug(bool& a_logged, spdlog::level::level_enum a_first)
    {
        const auto level = a_logged ? spdlog::level::debug : a_first;
        a_logged = true;
        return level;
    }

    void LogOutcome(const ApplyResult& a_result, const RE::Actor* a_target, const RE::AlchemyItem* a_poison)
    {
        const char* targetRaw = a_target->GetName();
        const char* poisonRaw = a_poison->GetName();
        const char* targetName = targetRaw ? targetRaw : "";
        const char* poisonName = poisonRaw ? poisonRaw : "";
        const auto targetId = a_target->GetFormID();
        const auto poisonId = a_poison->GetFormID();
        const char* casterText = kCastFromTarget ? "target's" : "player's";
        switch (a_result.outcome) {
        case ToxGame::ApplyOutcome::kAlreadyApplied:
            spdlog::log(FirstThenDebug(g_alreadyLogged, spdlog::level::info), "Toxicology: '{}' [{:08X}] was already applied "
                "to '{}' [{:08X}] this session - nothing done (once per target and poison; repeats at debug level)",
                poisonName, poisonId, targetName, targetId);
            break;
        case ToxGame::ApplyOutcome::kCast:
            spdlog::log(FirstThenDebug(g_castLogged, spdlog::level::info), "Toxicology: cast '{}' [{:08X}] on '{}' [{:08X}] "
                "({} caster), {} new effect(s) of it on the target (repeats at debug level)", poisonName, poisonId, targetName,
                targetId, casterText, a_result.count);
            break;
        case ToxGame::ApplyOutcome::kCastNoEffect:
            if (a_result.noEffect == NoEffectReason::kInstantOnly) {
                spdlog::log(FirstThenDebug(g_instantOnlyLogged, spdlog::level::info), "Toxicology: cast '{}' [{:08X}] on '{}' "
                    "[{:08X}] ({} caster): instant-only poison, nothing lasting on the target (repeats at debug level)",
                    poisonName, poisonId, targetName, targetId, casterText);
                break;
            }
            if (auto& warned = g_warnedNoEffect[static_cast<std::size_t>(a_result.noEffect)]; !warned) {
                warned = true;
                logger::warn("Toxicology: cast '{}' [{:08X}] on '{}' [{:08X}] ({} caster), no new effect of it on the target: "
                             "{}; repeats of this case are logged at debug level",
                    poisonName, poisonId, targetName, targetId, casterText, NoEffectText(a_result.noEffect));
            } else {
                logger::debug("Toxicology: cast '{}' [{:08X}] on [{:08X}], no new effect of it on the target: {}",
                    poisonName, poisonId, targetId, NoEffectText(a_result.noEffect));
            }
            break;
        case ToxGame::ApplyOutcome::kNoCaster:
            spdlog::log(FirstThenDebug(g_noCasterLogged, spdlog::level::warn), "Toxicology: no instant magic caster on the {} - "
                "'{}' [{:08X}] not cast on [{:08X}] (repeats at debug level)", kCastFromTarget ? "target" : "player", poisonName,
                poisonId, targetId);
            break;
        }
    }

    // Spec 5.4: cast, record the pair, and see whether a new effect of the poison appeared on the target
    ApplyResult CastAndRecord(RE::Actor* a_target, RE::AlchemyItem* a_poison, RE::Actor* a_player, std::uint64_t a_key)
    {
        ApplyResult result;
        const auto before = EffectsOf(ToxGame::ActiveEffectsOf(a_target), a_poison);
        const auto playerBefore = EffectsOf(ToxGame::ActiveEffectsOf(a_player), a_poison);
        if (!CastPoison(a_target, a_poison, a_player)) {
            result.outcome = ToxGame::ApplyOutcome::kNoCaster;
            return result;
        }
        g_applied.insert(a_key);
        // Fetched again: the list may only exist once the cast added something
        auto* after = ToxGame::ActiveEffectsOf(a_target);
        result.count = CountNewEffectsOf(after, a_poison, before);
        if (result.count > 0) {
            result.outcome = ToxGame::ApplyOutcome::kCast;
            return result;
        }
        result.outcome = ToxGame::ApplyOutcome::kCastNoEffect;
        if (!HasLastingEffect(a_poison)) {
            result.noEffect = NoEffectReason::kInstantOnly;
        } else if (!after) {
            result.noEffect = NoEffectReason::kNoTargetList;
        } else if (CountNewEffectsOf(ToxGame::ActiveEffectsOf(a_player), a_poison, playerBefore) > 0) {
            result.noEffect = NoEffectReason::kOnCaster;
        } else {
            result.noEffect = NoEffectReason::kNowhere;
        }
        return result;
    }
}

// =============================================================================
// Public
// =============================================================================

ToxGame::ApplyOutcome ToxGame::ApplyOnce(RE::Actor* a_target, RE::AlchemyItem* a_poison, RE::Actor* a_player)
{
    const auto key = ApplicationKey(a_target->GetFormID(), a_poison->GetFormID());
    const auto result = g_applied.contains(key) ? ApplyResult{} : CastAndRecord(a_target, a_poison, a_player, key);
    LogOutcome(result, a_target, a_poison);
    return result.outcome;
}

void ToxGame::ClearApplications()
{
    g_applied.clear();
    g_warnedNoEffect.fill(false);
}

RE::BSSimpleList<RE::ActiveEffect*>* ToxGame::ActiveEffectsOf(RE::Actor* a_actor)
{
    auto* target = a_actor ? a_actor->AsMagicTarget() : nullptr;  // RE/A/Actor.h:777 (versioned base offset)
    return target ? target->GetActiveEffectList() : nullptr;      // RE/M/MagicTarget.h:122 (vfunc 07)
}
