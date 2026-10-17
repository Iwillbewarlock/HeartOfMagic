// =============================================================================
// Toxicology - ActorMagicCaster::Update hook and the concentration drain task
// =============================================================================
// See game/ToxMagicka.h for what is measured and game/ToxMagickaShared.h for what
// this file shares with game/ToxMagicka.cpp.

#include "game/ToxMagickaShared.h"

#include "core/ToxCore.h"
#include "game/ToxData.h"
#include "game/ToxHits.h"

#include <cmath>
#include <exception>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace ToxGame::MagickaDetail;

namespace
{
    // =========================================================================
    // Constants
    // =========================================================================

    // ActorMagicCaster::Update(float) on SE/AE (RE/A/ActorMagicCaster.h:70-81; this DLL is built
    // without VR, plugins/cmake/commonlibsse.cmake:40, so EXCLUSIVE_SKYRIM_FLAT and 0x1D)
    constexpr std::size_t kUpdateVfuncIndex = 0x1D;

    // The caster state a concentration spell is counted in. RE/M/MagicCaster.h:30-42 names none
    // "concentrating"; the states seen with a poison concentration spell are logged once per value,
    // and kCasting (6) stands until that log says otherwise.
    constexpr auto kConcentratingState = RE::MagicCaster::State::kCasting;

    // Dual-cast concentration: both hand casters sit in the casting state on the same spell, while the
    // engine charges a dual cast once and one caster's cost carries the dual multiplier. With this on,
    // only the right hand counts while the left hand casts the same spell with the dual flag set on
    // either caster. The in-game 5 s counted / drop ratio (game/ToxCheck.h) confirms it.
    constexpr bool kCountOneHandWhenDual = true;
    // The other hand's reading counts as the same frame for the dual check this long after it was taken
    constexpr double kDualPairSeconds = 0.25;

    // At most one concentration drain task is queued per this many seconds; the amounts wait in
    // g_shared meanwhile (nothing is dropped, the charge is only later)
    constexpr float kDrainQueueGapSeconds = 0.25f;
    constexpr double kNeverQueued = -1.0;  // IntervalElapsed: < 0 means never

    // =========================================================================
    // State shared by the hook and the drain task (g_sharedMutex)
    // =========================================================================

    enum class SampleNote
    {
        kStretchStart,  // the hand entered the counted state: logged once per cast stretch
        kDualSkipped,   // the hand is the dual cast's second caster and is not counted (kCountOneHandWhenDual)
    };

    struct NotedSample
    {
        std::size_t hand = kLeftHand;
        SampleNote note = SampleNote::kStretchStart;
        HandSample sample;
    };

    // (spell FormID << 1) | hand
    std::uint64_t HandKey(RE::FormID a_spell, std::size_t a_hand)
    {
        return (static_cast<std::uint64_t>(a_spell) << 1) | (a_hand & 1u);
    }

    struct Shared
    {
        std::unordered_map<std::uint64_t, float> concentration;   // HandKey -> Magicka not yet handed to a task
        std::unordered_map<RE::FormID, std::uint32_t> newStates;  // spell -> state bits not yet handed to a task
        std::unordered_map<RE::FormID, std::uint32_t> statesSeen; // spell -> state bits already handed (whole session)
        std::vector<NotedSample> samples;                         // concentration readings for the task to log
        std::array<HandSample, kHandCount> concentrationHands{};  // last concentration reading per hand
        std::array<bool, kHandCount> stretchLogged{};
        std::array<bool, kHandCount> skipLogged{};
        std::array<HandSample, kHandCount> fireHands{};           // last fire-and-forget reading, per hand
    };

    std::mutex g_sharedMutex;
    Shared g_shared;

    // Read by the hook without the lock. g_drainPending is written under g_sharedMutex only, so it
    // stays true exactly while g_shared holds something the drain task has not taken.
    std::atomic<bool> g_tracking{ false };
    std::atomic<bool> g_drainPending{ false };
    std::atomic<bool> g_drainQueued{ false };
    std::atomic<double> g_lastDrainQueuedAt{ kNeverQueued };

    std::atomic<bool> g_hookThreadLogged{ false };
    std::atomic<bool> g_hookFailureLogged{ false };

    // Main thread only (the drain task)
    std::uint32_t g_poisonStatesLogged = 0;

    // =========================================================================
    // Drain task (main thread)
    // =========================================================================

    void LogPoisonStates(RE::FormID a_spell, std::uint32_t a_bits)
    {
        const auto fresh = a_bits & ~g_poisonStatesLogged;
        if (!fresh) return;
        g_poisonStatesLogged |= fresh;
        const auto* form = RE::TESForm::LookupByID(a_spell);
        for (std::uint32_t value = 0; value < kStateBitCount; ++value) {
            if (fresh & (1u << value)) {
                logger::info("Toxicology: a hand caster holding poison concentration spell {} {:08X} is in state {} "
                    "(counted in state {})", form ? form->GetName() : "?", a_spell, value, std::to_underlying(kConcentratingState));
            }
        }
    }

    void LogSample(const NotedSample& a_noted)
    {
        const auto& s = a_noted.sample;
        const auto* form = RE::TESForm::LookupByID(s.spell);
        const char* note = a_noted.note == SampleNote::kStretchStart ?
            "casting starts" : "not counted - the other hand counts this dual cast (kCountOneHandWhenDual)";
        logger::info("Toxicology: concentration poison spell {} {:08X}, {} hand: {}; state {}, currentSpellCost {:.2f} (counted), "
            "GetCurrentSpellCost {:.2f}, costCharged {:.2f}, dual {}", form ? form->GetName() : "?", s.spell, HandName(a_noted.hand),
            note, s.state, s.currentSpellCost, s.spellCost, s.costCharged, s.dualCasting);
    }

    void DrainConcentration()
    {
        std::unordered_map<std::uint64_t, float> amounts;
        std::unordered_map<RE::FormID, std::uint32_t> states;
        std::vector<NotedSample> samples;
        {
            std::lock_guard lock(g_sharedMutex);
            amounts.swap(g_shared.concentration);
            states.swap(g_shared.newStates);
            samples.swap(g_shared.samples);
            g_drainPending.store(false);
            g_drainQueued.store(false);
        }
        if (amounts.empty() && states.empty() && samples.empty()) return;
        ToxGame::ReloadPoisonSpellsIfChanged();  // contract (ToxData.h): before every IsPoisonCostSpell
        for (const auto& [spell, bits] : states) {
            if (ToxGame::IsPoisonCostSpell(spell)) LogPoisonStates(spell, bits);
        }
        for (const auto& noted : samples) {
            if (ToxGame::IsPoisonCostSpell(noted.sample.spell)) LogSample(noted);
        }
        for (const auto& [key, amount] : amounts) {
            const auto spell = static_cast<RE::FormID>(key >> 1);
            if (ToxGame::IsPoisonCostSpell(spell)) CountCast(amount, static_cast<std::size_t>(key & 1u));
        }
    }

    void QueueDrain()
    {
        const auto generation = ToxGame::SessionGeneration();
        const bool queued = QueueTask([generation]() {
            // Queued before a load or new game: dropped. ResetHookState (co-save Revert, new game) has
            // already forgotten what it was for; only the queued flag is given back, so the new
            // session's amounts are queued again by the next poison frame.
            if (generation != ToxGame::SessionGeneration()) {
                g_drainQueued.store(false);
                return;
            }
            try {
                DrainConcentration();
            } catch (const std::exception& e) {
                logger::error("Toxicology: concentration Magicka task failed: {}", e.what());
            }
        });
        if (!queued) {
            std::lock_guard lock(g_sharedMutex);
            g_shared.concentration.clear();
            g_shared.newStates.clear();
            g_shared.samples.clear();
            g_drainPending.store(false);
            g_drainQueued.store(false);
        }
    }

    // Any thread: a drain task when something waits for one, none is queued and the last was queued
    // kDrainQueueGapSeconds ago or more
    void MaybeQueueDrain(double a_now)
    {
        if (!g_drainPending.load() || g_drainQueued.load()) return;
        if (!Toxicology::IntervalElapsed(g_lastDrainQueuedAt.load(), a_now, kDrainQueueGapSeconds)) return;
        if (g_drainQueued.exchange(true)) return;
        g_lastDrainQueuedAt.store(a_now);
        QueueDrain();
    }

    // =========================================================================
    // Hook (whichever thread updates the player's casters)
    // =========================================================================

    std::uint32_t StateBit(RE::MagicCaster::State a_state)
    {
        const auto value = static_cast<std::uint32_t>(std::to_underlying(a_state));
        return value < kStateBitCount ? (1u << value) : 0u;
    }

    // Under g_sharedMutex: the other hand casts the same spell in the counted state right now, and either
    // caster carries the dual flag - one dual cast seen from both hands
    bool OtherHandSharesDualCast(std::size_t a_hand, const HandSample& a_sample, double a_now)
    {
        const auto& other = g_shared.concentrationHands[1 - a_hand];
        const auto counted = static_cast<std::uint32_t>(std::to_underlying(kConcentratingState));
        return other.spell == a_sample.spell && other.state == counted && other.seenAt >= 0.0 &&
               a_now - other.seenAt <= kDualPairSeconds && (other.dualCasting || a_sample.dualCasting);
    }

    // Under g_sharedMutex. A reading handed to the drain task's log gets GetCurrentSpellCost beside
    // currentSpellCost (once per stretch, not every frame). Returns true when something new waits.
    bool NoteConcentration(RE::ActorMagicCaster& a_caster, std::size_t a_hand, const HandSample& a_sample, float a_spent,
        std::uint32_t a_bit, double a_now)
    {
        bool handed = false;
        const auto noteForLog = [&](SampleNote a_note) {
            NotedSample noted{ a_hand, a_note, a_sample };
            noted.sample.spellCost = a_caster.GetCurrentSpellCost();  // RE/M/MagicCaster.h:94
            g_shared.samples.push_back(noted);
            handed = true;
        };
        const bool casting = a_sample.state == static_cast<std::uint32_t>(std::to_underlying(kConcentratingState));
        if (!casting || g_shared.concentrationHands[a_hand].spell != a_sample.spell) {
            g_shared.stretchLogged[a_hand] = false;
            g_shared.skipLogged[a_hand] = false;
        }
        g_shared.concentrationHands[a_hand] = a_sample;
        if (casting && !g_shared.stretchLogged[a_hand]) {
            g_shared.stretchLogged[a_hand] = true;
            noteForLog(SampleNote::kStretchStart);
        }
        if (kCountOneHandWhenDual && a_spent > 0.0f && a_hand == kLeftHand && OtherHandSharesDualCast(a_hand, a_sample, a_now)) {
            if (!g_shared.skipLogged[a_hand]) {
                g_shared.skipLogged[a_hand] = true;
                noteForLog(SampleNote::kDualSkipped);
            }
            a_spent = 0.0f;
        }
        auto& seen = g_shared.statesSeen[a_sample.spell];
        if (a_bit && !(seen & a_bit)) {
            seen |= a_bit;
            g_shared.newStates[a_sample.spell] |= a_bit;
            handed = true;
        }
        if (std::isfinite(a_spent) && a_spent > 0.0f) {
            g_shared.concentration[HandKey(a_sample.spell, a_hand)] += a_spent;
            handed = true;
        }
        if (handed) g_drainPending.store(true);
        return handed;
    }

    // Before the original Update, so the frame a cast is released in is still seen as it was. Runs for
    // every actor's casters every frame: everything before the cost set test is lock-free.
    void Observe(RE::ActorMagicCaster& a_caster, float a_delta)
    {
        if (!g_tracking.load(std::memory_order_relaxed)) return;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || a_caster.actor != player) return;  // RE/A/ActorMagicCaster.h:93
        const auto source = a_caster.castingSource;       // RE/A/ActorMagicCaster.h:101; kInstant (our own poison casts) is left out
        if (source != RE::MagicSystem::CastingSource::kLeftHand && source != RE::MagicSystem::CastingSource::kRightHand) return;
        LogThreadOnce(g_hookThreadLogged, "the player's hand casters update");
        const double now = NowSeconds();
        MaybeQueueDrain(now);  // what an ended stretch left, once the queue gap is over
        auto* spell = a_caster.currentSpell;  // RE/M/MagicCaster.h:105
        if (!spell || !ToxGame::PoisonCostSnapshot()->contains(spell->GetFormID())) return;

        const auto type = spell->GetCastingType();
        const bool concentration = type == RE::MagicSystem::CastingType::kConcentration;
        if (!concentration && type != RE::MagicSystem::CastingType::kFireAndForget) return;
        const auto hand = std::to_underlying(source) == 0 ? kLeftHand : kRightHand;
        const auto state = a_caster.state.get();  // REX/REX/EnumSet.h:55
        HandSample sample{ spell->GetFormID(), static_cast<std::uint32_t>(std::to_underlying(state)), a_caster.currentSpellCost,
            0.0f, a_caster.costCharged, a_caster.flags.any(RE::ActorMagicCaster::Flags::kDualCasting), now };  // REX/REX/EnumSet.h:94

        if (concentration) {
            // currentSpellCost per second (spec 12: the plan's choice until the in-game 5 s ratio decides)
            float spent = 0.0f;
            if (state == kConcentratingState && std::isfinite(a_delta) && a_delta > 0.0f) spent = sample.currentSpellCost * a_delta;
            {
                std::lock_guard lock(g_sharedMutex);
                NoteConcentration(a_caster, hand, sample, spent, StateBit(state), now);
            }
            MaybeQueueDrain(now);
        } else {
            // A zero cost is kept too (it charges nothing); "no spell" above keeps the last reading for the cast task
            if (!std::isfinite(sample.currentSpellCost)) return;
            sample.spellCost = a_caster.GetCurrentSpellCost();  // beside currentSpellCost, for the release log
            std::lock_guard lock(g_sharedMutex);
            g_shared.fireHands[hand] = sample;
        }
    }

    struct UpdateHook
    {
        static void thunk(RE::ActorMagicCaster* a_this, float a_delta)
        {
            // Entered from the engine's vtable: nothing may throw out of here
            try {
                if (a_this) Observe(*a_this, a_delta);
            } catch (...) {
                if (!g_hookFailureLogged.exchange(true)) {
                    logger::error("Toxicology: caster update hook threw; further failures are not logged");
                }
            }
            func(a_this, a_delta);
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };
}

// =============================================================================
// ToxGame::MagickaDetail (ToxMagickaShared.h)
// =============================================================================

std::array<HandSample, kHandCount> ToxGame::MagickaDetail::FireHands()
{
    std::lock_guard lock(g_sharedMutex);
    return g_shared.fireHands;
}

void ToxGame::MagickaDetail::SetTracking(bool a_on)
{
    if (g_tracking.exchange(a_on) == a_on) return;
    logger::info("Toxicology: caster hook {} Magicka spent on poison spells", a_on ? "now counts" : "no longer counts");
}

void ToxGame::MagickaDetail::ResetHookState()
{
    std::lock_guard lock(g_sharedMutex);
    g_shared.concentration.clear();
    g_shared.newStates.clear();
    g_shared.samples.clear();
    g_shared.concentrationHands = {};
    g_shared.stretchLogged = {};
    g_shared.skipLogged = {};
    g_shared.fireHands = {};
    g_drainPending.store(false);
    g_drainQueued.store(false);  // a task still queued is dropped by its generation
    g_lastDrainQueuedAt.store(kNeverQueued);
}

void ToxGame::MagickaDetail::InstallCasterHook()
{
    // plugins/spelllearning/src/SpellCastImmediateHook.cpp:56-57; [0] is the MagicCaster base's vtable
    REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ActorMagicCaster[0] };
    UpdateHook::func = vtbl.write_vfunc(kUpdateVfuncIndex, UpdateHook::thunk);
    logger::info("Toxicology: hooked ActorMagicCaster::Update (Magicka spent on poison spells)");
}
