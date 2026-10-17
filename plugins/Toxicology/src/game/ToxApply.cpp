#include "game/ToxApply.h"

#include "core/ToxCore.h"
#include "game/ToxData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
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

    constexpr double kHoursPerDay = 24.0;        // what Calendar::GetHoursPassed multiplies by (src/RE/C/Calendar.cpp:86)
    constexpr double kSecondsPerHour = 3600.0;
    constexpr std::size_t kRecordSweepSize = 256;  // the records are swept of ended windows past this many
    constexpr std::uint32_t kCastLogsAtInfo = 10;  // spec 5.4: the first casts of a session log at info, then debug

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

    // One cast pair's window, in game hours (see Clock)
    struct PairRecord
    {
        double appliedAtHours = 0.0;
        double reapplyAtHours = 0.0;
        double durationSeconds = 0.0;
        double immunitySeconds = 0.0;
    };

    // (target FormID << 32 | poison FormID) -> its window, emptied on every load and new game
    std::unordered_map<std::uint64_t, PairRecord> g_records;
    std::array<bool, static_cast<std::size_t>(NoEffectReason::kCount)> g_warnedNoEffect{};
    std::uint32_t g_castLogs = 0;  // casts logged this session (kCastLogsAtInfo of them at info)
    bool g_noClockWarned = false;  // this session
    // Steady-state outcome lines: info (warn for kNoCaster) the first time in the DLL's life, debug after
    bool g_immuneLogged = false;
    bool g_instantOnlyLogged = false;
    bool g_noCasterLogged = false;

    // =========================================================================
    // Clock
    // =========================================================================
    // Game time: Calendar::gameDaysPassed (RE/C/Calendar.h:91, the GameDaysPassed global that
    // Calendar::GetDaysPassed reads, src/RE/C/Calendar.cpp:67-70) in hours, and Calendar::timeScale
    // (Calendar.h:92) to turn seconds into game hours. Game time stands still while a menu pauses the
    // game, and runs at the global time multiplier like the effects' own timers. The other candidates
    // were not used: BSTimer::runTimeMS (RE/B/BSTimer.h:50) stops only while BSTimer::pauseCount is
    // non-zero (BSTimer.h:34), and no header ties that to the menu pause (UI::numPausesGame, RE/U/UI.h:131);
    // Main::QFrameAnimTime (RE/M/Main.h:77) has no documented pause behaviour. The menu pause itself is
    // not proven by a header either: self test step 3 compares this clock with an effect's elapsedSeconds.
    // A window is stored in game hours, converted from seconds with the timescale at the cast, so a
    // later timescale change stretches the rest of it as it stretches game time instead of moving its
    // end. Wait, sleep and fast travel move game time forward and end windows early. gameDaysPassed is a
    // float: at day 1000 one step is about 0.3 s at timescale 20 (5 s at timescale 1).

    std::optional<double> GameHours()
    {
        const auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar || !calendar->gameDaysPassed) return std::nullopt;
        const double hours = static_cast<double>(calendar->gameDaysPassed->value) * kHoursPerDay;
        return std::isfinite(hours) ? std::optional<double>(hours) : std::nullopt;
    }

    // Game seconds per real second; nullopt when missing, 0 or less (game time stopped) or not finite
    std::optional<double> Timescale()
    {
        const auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar || !calendar->timeScale) return std::nullopt;
        const double scale = calendar->timeScale->value;
        return (std::isfinite(scale) && scale > 0.0) ? std::optional<double>(scale) : std::nullopt;
    }

    struct ClockNow
    {
        double hours = 0.0;
        double timescale = 0.0;
    };

    std::optional<ClockNow> Now()
    {
        const auto hours = GameHours();
        const auto scale = Timescale();
        if (!hours || !scale) return std::nullopt;
        return ClockNow{ *hours, *scale };
    }

    double SecondsToHours(double a_seconds, double a_timescale)
    {
        return a_seconds * a_timescale / kSecondsPerHour;
    }

    double HoursToSeconds(double a_hours, double a_timescale)
    {
        return a_hours * kSecondsPerHour / a_timescale;
    }

    // =========================================================================
    // Effects
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

    std::vector<const RE::ActiveEffect*> NewEffectsOf(EffectList* a_list, const RE::MagicItem* a_poison,
        const std::vector<SeenEffect>& a_before)
    {
        std::vector<const RE::ActiveEffect*> found;
        if (!a_list) return found;
        for (const auto* effect : *a_list) {
            if (effect && effect->spell == a_poison && !WasSeen(a_before, effect)) found.push_back(effect);
        }
        return found;
    }

    // The record's longest duration in seconds (0: instant only). RE/E/Effect.h:34; GetDuration reads
    // baseEffect without a null check (src/RE/E/Effect.cpp:57-62).
    std::uint32_t RecordDuration(const RE::MagicItem* a_poison)
    {
        std::uint32_t longest = 0;
        for (const auto* effect : a_poison->effects) {
            if (effect && effect->baseEffect) longest = std::max(longest, effect->GetDuration());
        }
        return longest;
    }

    // Spec 5.4: D = the longest actual duration of the new effects (perks included), else the record's
    double DurationSeconds(const std::vector<const RE::ActiveEffect*>& a_new, const RE::MagicItem* a_poison)
    {
        if (a_new.empty()) return static_cast<double>(RecordDuration(a_poison));
        double longest = 0.0;
        for (const auto* effect : a_new) {
            if (std::isfinite(effect->duration)) longest = std::max(longest, static_cast<double>(effect->duration));  // ActiveEffect.h:111
        }
        return longest;
    }

    // The poison's record effect an active effect came from: the same Effect, else the same base effect
    const RE::Effect* RecordEffectOf(const RE::MagicItem* a_poison, const RE::ActiveEffect* a_active, bool& a_byBase)
    {
        a_byBase = false;
        for (const auto* effect : a_poison->effects) {
            if (effect && effect == a_active->effect) return effect;  // ActiveEffect.h:105
        }
        const auto* base = a_active->GetBaseObject();  // ActiveEffect.h:94
        if (!base) return nullptr;
        a_byBase = true;
        for (const auto* effect : a_poison->effects) {
            if (effect && effect->baseEffect == base) return effect;
        }
        return nullptr;
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

    // =========================================================================
    // Log
    // =========================================================================

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
        ToxGame::ApplyOutcome outcome = ToxGame::ApplyOutcome::kImmune;
        std::vector<const RE::ActiveEffect*> added;          // new effects on the target (kCast)
        NoEffectReason noEffect = NoEffectReason::kNowhere;  // meaningful for kCastNoEffect only
        PairRecord window;                                    // kImmune: the record; a cast: the new one
        double secondsLeft = 0.0;                             // kImmune: until the next hit casts again
        bool recorded = false;                                // a cast: a window was recorded (the clock was there)
    };

    // Info (warn) the first time a_logged is false, debug after
    spdlog::level::level_enum FirstThenDebug(bool& a_logged, spdlog::level::level_enum a_first)
    {
        const auto level = a_logged ? spdlog::level::debug : a_first;
        a_logged = true;
        return level;
    }

    std::string_view NameOf(const RE::TESForm* a_form)
    {
        const char* name = a_form ? a_form->GetName() : nullptr;  // RE/T/TESForm.h:308
        return name ? name : "";
    }

    // Spec 5.4 (perk check, user request): each new effect's actual values beside the record's, then D
    // and the window. The first kCastLogsAtInfo casts of a session at info, the rest at debug.
    void LogCast(const ApplyResult& a_result, const RE::Actor* a_target, const RE::AlchemyItem* a_poison)
    {
        const auto level = g_castLogs < kCastLogsAtInfo ? spdlog::level::info : spdlog::level::debug;
        ++g_castLogs;
        const auto& w = a_result.window;
        const char* source = !a_result.added.empty() ? "longest new effect" : (w.durationSeconds > 0.0 ? "poison record, no new effect seen" : "instant only");
        spdlog::log(level, "Toxicology: cast #{} '{}' [{:08X}] on '{}' [{:08X}] ({} caster): {} new effect(s); D {:.2f} s ({}), "
            "immunity {:.2f} s, reapply in {}", g_castLogs, NameOf(a_poison), a_poison->GetFormID(), NameOf(a_target),
            a_target->GetFormID(), kCastFromTarget ? "target's" : "player's", a_result.added.size(), w.durationSeconds, source,
            w.immunitySeconds, a_result.recorded ? std::format("{:.2f} s", w.durationSeconds + w.immunitySeconds) : std::string("- (no game clock, not recorded)"));
        for (const auto* active : a_result.added) {
            bool byBase = false;
            const auto* record = RecordEffectOf(a_poison, active, byBase);
            const auto* base = active->GetBaseObject();
            if (record && record->baseEffect) {
                spdlog::log(level, "Toxicology:   effect '{}' [{:08X}]: actual magnitude {:.2f}, duration {:.2f} s; record magnitude "
                    "{:.2f}, duration {} s{}", NameOf(base), base ? base->GetFormID() : 0, active->magnitude, active->duration,
                    record->GetMagnitude(), record->GetDuration(), byBase ? " (record matched by base effect)" : "");
            } else {
                spdlog::log(level, "Toxicology:   effect '{}' [{:08X}]: actual magnitude {:.2f}, duration {:.2f} s; no matching "
                    "effect in the poison record", NameOf(base), base ? base->GetFormID() : 0, active->magnitude, active->duration);
            }
        }
    }

    void LogOutcome(const ApplyResult& a_result, const RE::Actor* a_target, const RE::AlchemyItem* a_poison)
    {
        const auto targetName = NameOf(a_target);
        const auto poisonName = NameOf(a_poison);
        const auto targetId = a_target->GetFormID();
        const auto poisonId = a_poison->GetFormID();
        const char* casterText = kCastFromTarget ? "target's" : "player's";
        switch (a_result.outcome) {
        case ToxGame::ApplyOutcome::kImmune:
            spdlog::log(FirstThenDebug(g_immuneLogged, spdlog::level::info), "Toxicology: '{}' [{:08X}] on '{}' [{:08X}] is immune "
                "for {:.2f} s more (D {:.2f} s + immunity {:.2f} s from the cast) - nothing done (repeats at debug level)",
                poisonName, poisonId, targetName, targetId, a_result.secondsLeft, a_result.window.durationSeconds,
                a_result.window.immunitySeconds);
            return;
        case ToxGame::ApplyOutcome::kNoCaster:
            spdlog::log(FirstThenDebug(g_noCasterLogged, spdlog::level::warn), "Toxicology: no instant magic caster on the {} - "
                "'{}' [{:08X}] not cast on [{:08X}] (repeats at debug level)", kCastFromTarget ? "target" : "player", poisonName,
                poisonId, targetId);
            return;
        case ToxGame::ApplyOutcome::kCast:
            break;
        case ToxGame::ApplyOutcome::kCastNoEffect:
            if (a_result.noEffect == NoEffectReason::kInstantOnly) {
                spdlog::log(FirstThenDebug(g_instantOnlyLogged, spdlog::level::info), "Toxicology: cast '{}' [{:08X}] on '{}' "
                    "[{:08X}] ({} caster): instant-only poison, nothing lasting on the target (repeats at debug level)",
                    poisonName, poisonId, targetName, targetId, casterText);
            } else if (auto& warned = g_warnedNoEffect[static_cast<std::size_t>(a_result.noEffect)]; !warned) {
                warned = true;
                logger::warn("Toxicology: cast '{}' [{:08X}] on '{}' [{:08X}] ({} caster), no new effect of it on the target: "
                             "{}; repeats of this case are logged at debug level",
                    poisonName, poisonId, targetName, targetId, casterText, NoEffectText(a_result.noEffect));
            } else {
                logger::debug("Toxicology: cast '{}' [{:08X}] on [{:08X}], no new effect of it on the target: {}",
                    poisonName, poisonId, targetId, NoEffectText(a_result.noEffect));
            }
            break;
        }
        LogCast(a_result, a_target, a_poison);
    }

    // =========================================================================
    // Apply
    // =========================================================================

    // Spec 5.4: cast, see which new effects of the poison appeared on the target, and record the window
    ApplyResult CastAndRecord(RE::Actor* a_target, RE::AlchemyItem* a_poison, RE::Actor* a_player, std::uint64_t a_key,
        const std::optional<ClockNow>& a_now)
    {
        ApplyResult result;
        const auto before = EffectsOf(ToxGame::ActiveEffectsOf(a_target), a_poison);
        const auto playerBefore = EffectsOf(ToxGame::ActiveEffectsOf(a_player), a_poison);
        if (!CastPoison(a_target, a_poison, a_player)) {
            result.outcome = ToxGame::ApplyOutcome::kNoCaster;
            return result;
        }
        // Fetched again: the list may only exist once the cast added something
        auto* after = ToxGame::ActiveEffectsOf(a_target);
        result.added = NewEffectsOf(after, a_poison, before);

        const auto& settings = ToxGame::GetSettings();
        auto& w = result.window;
        w.durationSeconds = DurationSeconds(result.added, a_poison);
        w.immunitySeconds = Toxicology::ImmunitySeconds(w.durationSeconds, settings.immunityMultiplier, settings.minImmunitySeconds);
        if (a_now) {
            const double windowSeconds = Toxicology::ReapplyAt(0.0, w.durationSeconds, settings.immunityMultiplier, settings.minImmunitySeconds);
            w.appliedAtHours = a_now->hours;
            w.reapplyAtHours = a_now->hours + SecondsToHours(windowSeconds, a_now->timescale);
            g_records[a_key] = w;
            result.recorded = true;
        }

        if (!result.added.empty()) {
            result.outcome = ToxGame::ApplyOutcome::kCast;
            return result;
        }
        result.outcome = ToxGame::ApplyOutcome::kCastNoEffect;
        if (RecordDuration(a_poison) == 0) {
            result.noEffect = NoEffectReason::kInstantOnly;
        } else if (!after) {
            result.noEffect = NoEffectReason::kNoTargetList;
        } else if (!NewEffectsOf(ToxGame::ActiveEffectsOf(a_player), a_poison, playerBefore).empty()) {
            result.noEffect = NoEffectReason::kOnCaster;
        } else {
            result.noEffect = NoEffectReason::kNowhere;
        }
        return result;
    }

    // The pair's record while its window runs (now in [appliedAt, reapplyAt)); an ended record, or one
    // from a later time than now (game time set back), is dropped
    const PairRecord* RunningWindow(std::uint64_t a_key, double a_nowHours)
    {
        if (g_records.size() > kRecordSweepSize) {
            std::erase_if(g_records, [a_nowHours](const auto& a_entry) { return a_nowHours >= a_entry.second.reapplyAtHours; });
        }
        const auto it = g_records.find(a_key);
        if (it == g_records.end()) return nullptr;
        if (a_nowHours >= it->second.appliedAtHours && a_nowHours < it->second.reapplyAtHours) return &it->second;
        g_records.erase(it);
        return nullptr;
    }
}

// =============================================================================
// Public
// =============================================================================

ToxGame::ApplyOutcome ToxGame::ApplyPoison(RE::Actor* a_target, RE::AlchemyItem* a_poison, RE::Actor* a_player)
{
    const auto key = ApplicationKey(a_target->GetFormID(), a_poison->GetFormID());
    const auto now = Now();
    if (!now && !g_noClockWarned) {
        g_noClockWarned = true;
        logger::warn("Toxicology: no game clock (calendar missing, or timescale 0 or less) - poisons are cast on every hit "
                     "with no immunity window until it is back (logged once per session)");
    }
    ApplyResult result;
    if (const auto* running = now ? RunningWindow(key, now->hours) : nullptr) {
        result.window = *running;
        result.secondsLeft = HoursToSeconds(running->reapplyAtHours - now->hours, now->timescale);
    } else {
        result = CastAndRecord(a_target, a_poison, a_player, key, now);
    }
    LogOutcome(result, a_target, a_poison);
    return result.outcome;
}

std::optional<ToxGame::ImmunityWindow> ToxGame::ImmunityOf(RE::FormID a_target, RE::FormID a_poison)
{
    const auto now = Now();
    if (!now) return std::nullopt;
    const auto it = g_records.find(ApplicationKey(a_target, a_poison));
    if (it == g_records.end()) return std::nullopt;
    return ImmunityWindow{ it->second.durationSeconds, it->second.immunitySeconds,
        HoursToSeconds(it->second.reapplyAtHours - now->hours, now->timescale) };
}

std::optional<double> ToxGame::GameClockSeconds()
{
    const auto now = Now();
    return now ? std::optional<double>(HoursToSeconds(now->hours, now->timescale)) : std::nullopt;
}

void ToxGame::ClearApplications()
{
    g_records.clear();
    g_warnedNoEffect.fill(false);
    g_castLogs = 0;
    g_noClockWarned = false;
}

RE::BSSimpleList<RE::ActiveEffect*>* ToxGame::ActiveEffectsOf(RE::Actor* a_actor)
{
    auto* target = a_actor ? a_actor->AsMagicTarget() : nullptr;  // RE/A/Actor.h:777 (versioned base offset)
    return target ? target->GetActiveEffectList() : nullptr;      // RE/M/MagicTarget.h:122 (vfunc 07)
}
