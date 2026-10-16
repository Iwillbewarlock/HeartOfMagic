#include "game/ToxMagicka.h"

#include "game/ToxBottles.h"
#include "game/ToxCheck.h"
#include "game/ToxData.h"
#include "game/ToxHits.h"
#include "game/ToxSelection.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>

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

    // MagicSystem::CastingSource kLeftHand 0, kRightHand 1 (RE/M/MagicSystem.h:26-30) index the hands;
    // kNoHand marks a fire-and-forget cost no hand was remembered for
    constexpr std::size_t kLeftHand = 0;
    constexpr std::size_t kRightHand = 1;
    constexpr std::size_t kHandCount = 2;
    constexpr std::size_t kNoHand = ToxGame::kCheckNoHand;

    // Dual-cast concentration: both hand casters sit in the casting state on the same spell, while the
    // engine charges a dual cast once and one caster's GetCurrentSpellCost carries the dual multiplier.
    // With this on, only the right hand counts while the left hand casts the same spell with the dual
    // flag set on either caster. The Task 7 self test confirms it through the 5 s counted / drop ratio.
    constexpr bool kCountOneHandWhenDual = true;
    // The other hand's reading counts as the same frame for the dual check this long after it was taken
    constexpr double kDualPairSeconds = 0.25;

    // A hand's remembered fire-and-forget cost is used for a cast event only this long after it was seen
    constexpr double kCostMatchSeconds = 2.0;
    // A second cast event for the same fire-and-forget spell inside this window, while the spell was
    // dual cast, is the same release reported again and is not charged. Outside a dual cast every event
    // is charged.
    constexpr double kSameCastWindowSeconds = 0.05;

    using Clock = std::chrono::steady_clock;

    double NowSeconds()
    {
        return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    }

    // =========================================================================
    // State shared with the hook and the cast sink (g_sharedMutex)
    // =========================================================================

    // One hook reading of a player hand caster
    struct HandSample
    {
        RE::FormID spell = 0;
        std::uint32_t state = 0;          // MagicCaster::State value
        float currentSpellCost = 0.0f;    // MagicCaster::currentSpellCost (RE/M/MagicCaster.h:108)
        float spellCost = 0.0f;           // MagicCaster::GetCurrentSpellCost() (RE/M/MagicCaster.h:94)
        float costCharged = 0.0f;         // ActorMagicCaster::costCharged (RE/A/ActorMagicCaster.h:100)
        bool dualCasting = false;         // ActorMagicCaster::Flags::kDualCasting
        double seenAt = -1.0;
    };

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
        std::array<HandSample, kHandCount> fireHands{};           // last fire-and-forget reading with a cost, per hand
        bool drainQueued = false;
    };

    std::mutex g_sharedMutex;
    Shared g_shared;

    std::mutex g_accumulatedMutex;
    float g_accumulated = 0.0f;

    std::atomic<bool> g_hookThreadLogged{ false };
    std::atomic<bool> g_hookFailureLogged{ false };
    std::atomic<bool> g_castThreadLogged{ false };
    std::atomic<bool> g_noTaskLogged{ false };
    std::thread::id g_mainThread;  // written once by InstallMagickaTracking, before the hook and sink exist

    // =========================================================================
    // State of the main thread
    // =========================================================================

    RE::FormID g_lastCastSpell = 0;
    double g_lastCastAt = -1.0;
    double g_lastReleaseAt = -1.0;
    std::uint32_t g_poisonStatesLogged = 0;

    void LogThreadOnce(std::atomic<bool>& a_flag, const char* a_what)
    {
        if (a_flag.exchange(true)) return;
        const bool onMain = std::this_thread::get_id() == g_mainThread;
        logger::info("Toxicology: {} on the {} thread", a_what, onMain ? "main" : "a non-main");
    }

    bool QueueTask(std::function<void()> a_task)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            if (!g_noTaskLogged.exchange(true)) {
                logger::error("Toxicology: no SKSE task interface - Magicka spent on poison spells is not counted");
            }
            return false;
        }
        tasks->AddTask(std::move(a_task));  // SKSE/Interfaces.h:394; runs on the game's main thread
        return true;
    }

    // The body of AddSpentMagicka; true when the amount was counted
    bool CountSpent(float a_amount)
    {
        if (!std::isfinite(a_amount) || a_amount <= 0.0f) return false;
        if (!ToxGame::CurrentPoison()) return false;  // no ability, disabled or no favorited poison (spec 5.6)
        Toxicology::DoseResult result;
        {
            std::lock_guard lock(g_accumulatedMutex);
            result = Toxicology::ConsumeMagicka(g_accumulated, a_amount, ToxGame::GetSettings().magickaPerDose);
            g_accumulated = result.remainder;
        }
        if (result.doses > 0) ToxGame::UseDoses(result.doses);
        return true;
    }

    void CountCast(float a_amount, std::size_t a_hand)
    {
        if (CountSpent(a_amount)) ToxGame::NoteCountedForCheck(a_amount, a_hand);
    }

    const char* HandName(std::size_t a_hand)
    {
        return a_hand == kLeftHand ? "left" : (a_hand == kRightHand ? "right" : "no");
    }

    // =========================================================================
    // Concentration (tasks, main thread)
    // =========================================================================

    void LogPoisonStates(RE::FormID a_spell, std::uint32_t a_bits)
    {
        const auto fresh = a_bits & ~g_poisonStatesLogged;
        if (!fresh) return;
        g_poisonStatesLogged |= fresh;
        const auto* form = RE::TESForm::LookupByID(a_spell);
        for (std::uint32_t value = 0; value < 32; ++value) {
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
        logger::info("Toxicology: concentration poison spell {} {:08X}, {} hand: {}; state {}, GetCurrentSpellCost {:.2f}, "
            "currentSpellCost {:.2f}, costCharged {:.2f}, dual {}", form ? form->GetName() : "?", s.spell, HandName(a_noted.hand),
            note, s.state, s.spellCost, s.currentSpellCost, s.costCharged, s.dualCasting);
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
            g_shared.drainQueued = false;
        }
        if (amounts.empty() && states.empty() && samples.empty()) return;
        ToxGame::ReloadPoisonSpellsIfChanged();  // contract (ToxData.h): before every IsPoisonSpell
        for (const auto& [spell, bits] : states) {
            if (ToxGame::IsPoisonSpell(spell)) LogPoisonStates(spell, bits);
        }
        for (const auto& noted : samples) {
            if (ToxGame::IsPoisonSpell(noted.sample.spell)) LogSample(noted);
        }
        for (const auto& [key, amount] : amounts) {
            const auto spell = static_cast<RE::FormID>(key >> 1);
            if (ToxGame::IsPoisonSpell(spell)) CountCast(amount, static_cast<std::size_t>(key & 1u));
        }
    }

    void QueueDrain()
    {
        const bool queued = QueueTask([]() {
            try {
                DrainConcentration();
            } catch (const std::exception& e) {
                logger::error("Toxicology: concentration Magicka task failed: {}", e.what());
            }
        });
        if (!queued) {
            std::lock_guard lock(g_sharedMutex);
            g_shared.drainQueued = false;
            g_shared.concentration.clear();
            g_shared.newStates.clear();
            g_shared.samples.clear();
        }
    }

    // =========================================================================
    // Fire-and-forget (cast event task, main thread)
    // =========================================================================

    bool FreshFor(const HandSample& a_hand, RE::FormID a_spell, double a_now)
    {
        return a_hand.spell == a_spell && a_hand.seenAt >= 0.0 && a_now - a_hand.seenAt <= kCostMatchSeconds;
    }

    // Both hands' last fire-and-forget readings, once per stretch of releases (a pause over kCheckGapSeconds
    // starts a new stretch), so the self test can tell which value tracks the charge
    void LogReleaseHands(const RE::MagicItem& a_spell, RE::FormID a_spellId, const std::array<HandSample, kHandCount>& a_hands, double a_now)
    {
        const bool newStretch = g_lastReleaseAt < 0.0 || a_now - g_lastReleaseAt > ToxGame::kCheckGapSeconds;
        g_lastReleaseAt = a_now;
        if (!newStretch) return;
        for (std::size_t hand = 0; hand < kHandCount; ++hand) {
            const auto& s = a_hands[hand];
            logger::info("Toxicology: fire-and-forget release of {} {:08X}, {} hand reading: spell {:08X} ({:.2f} s old), "
                "currentSpellCost {:.2f}, GetCurrentSpellCost {:.2f}, costCharged {:.2f}, dual {}", a_spell.GetName(), a_spellId,
                HandName(hand), s.spell, s.seenAt >= 0.0 ? a_now - s.seenAt : -1.0, s.currentSpellCost, s.spellCost, s.costCharged,
                s.dualCasting);
        }
    }

    void OnPlayerCast(RE::FormID a_spellId)
    {
        ToxGame::ReloadPoisonSpellsIfChanged();  // contract (ToxData.h)
        if (!ToxGame::IsPoisonSpell(a_spellId)) return;
        auto* form = RE::TESForm::LookupByID(a_spellId);
        auto* spell = form ? form->As<RE::MagicItem>() : nullptr;
        // Concentration also raises the event when it starts; the hook counts it frame by frame
        if (!spell || spell->GetCastingType() != RE::MagicSystem::CastingType::kFireAndForget) return;  // RE/M/MagicItem.h:83
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        const double now = NowSeconds();
        std::array<HandSample, kHandCount> hands;
        {
            std::lock_guard lock(g_sharedMutex);
            hands = g_shared.fireHands;
        }
        // The freshest hand holding this spell; a dual flag on either hand marks a dual cast
        std::size_t matched = kNoHand;
        bool dual = player->IsDualCasting();  // RE/A/Actor.h:637
        for (std::size_t hand = 0; hand < kHandCount; ++hand) {
            if (!FreshFor(hands[hand], a_spellId, now)) continue;
            dual = dual || hands[hand].dualCasting;
            if (matched == kNoHand || hands[hand].seenAt > hands[matched].seenAt) matched = hand;
        }

        if (a_spellId == g_lastCastSpell && now - g_lastCastAt < kSameCastWindowSeconds) {
            if (dual) {
                logger::info("Toxicology: second cast event for {:08X} inside {} s of a dual cast - one release, not charged",
                    a_spellId, kSameCastWindowSeconds);
                return;
            }
            logger::info("Toxicology: second cast event for {:08X} inside {} s, not a dual cast - charged as its own release",
                a_spellId, kSameCastWindowSeconds);
        }
        g_lastCastSpell = a_spellId;
        g_lastCastAt = now;
        LogReleaseHands(*spell, a_spellId, hands, now);

        float cost = 0.0f;
        if (matched != kNoHand) {
            const auto& s = hands[matched];
            cost = s.currentSpellCost;
            logger::info("Toxicology: fire-and-forget poison spell {} {:08X}: {:.2f} Magicka ({} hand currentSpellCost; "
                "GetCurrentSpellCost {:.2f}, costCharged {:.2f}, dual {})", spell->GetName(), a_spellId, cost, HandName(matched),
                s.spellCost, s.costCharged, dual);
        } else {
            cost = spell->CalculateMagickaCost(player);  // RE/M/MagicItem.h:112
            if (dual) cost = RE::MagicFormulas::CalcDualCastCost(cost);  // RE/M/MagicFormulas.h:7
            logger::info("Toxicology: fire-and-forget poison spell {} {:08X}: {:.2f} Magicka (no hand reading; calculated, dual {})",
                spell->GetName(), a_spellId, cost, dual);
        }
        CountCast(cost, matched);
    }

    // =========================================================================
    // Cast event sink
    // =========================================================================

    class CastSink final : public RE::BSTEventSink<RE::TESSpellCastEvent>
    {
    public:
        static CastSink* GetSingleton()
        {
            static CastSink singleton;
            return &singleton;
        }

        CastSink(const CastSink&) = delete;
        CastSink& operator=(const CastSink&) = delete;

        // RE/B/BSTEvent.h:188; RE/T/TESSpellCastEvent.h:13-14. Any thread (plugins/spelllearning/src/
        // SpellCastHandler.cpp:63-66): only the spell id and the generation go into the task.
        RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* a_event,
            RE::BSTEventSource<RE::TESSpellCastEvent>*) override
        {
            if (!a_event || !a_event->object || !a_event->object->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;
            LogThreadOnce(g_castThreadLogged, "player spell cast events arrive");
            const RE::FormID spellId = a_event->spell;
            const auto generation = ToxGame::SessionGeneration();
            QueueTask([spellId, generation]() {
                if (generation != ToxGame::SessionGeneration()) return;  // cast before a load or new game
                try {
                    OnPlayerCast(spellId);
                } catch (const std::exception& e) {
                    logger::error("Toxicology: cast task for spell [{:08X}] failed: {}", spellId, e.what());
                }
            });
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        CastSink() = default;
        ~CastSink() override = default;
    };

    // =========================================================================
    // ActorMagicCaster::Update hook
    // =========================================================================

    std::uint32_t StateBit(RE::MagicCaster::State a_state)
    {
        const auto value = static_cast<std::uint32_t>(std::to_underlying(a_state));
        return value < 32 ? (1u << value) : 0u;
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

    // Under g_sharedMutex. Returns true when something new waits for the drain task.
    bool NoteConcentration(std::size_t a_hand, const HandSample& a_sample, float a_spent, std::uint32_t a_bit, double a_now)
    {
        bool handed = false;
        const bool casting = a_sample.state == static_cast<std::uint32_t>(std::to_underlying(kConcentratingState));
        if (!casting || g_shared.concentrationHands[a_hand].spell != a_sample.spell) {
            g_shared.stretchLogged[a_hand] = false;
            g_shared.skipLogged[a_hand] = false;
        }
        g_shared.concentrationHands[a_hand] = a_sample;
        if (casting && !g_shared.stretchLogged[a_hand]) {
            g_shared.stretchLogged[a_hand] = true;
            g_shared.samples.push_back({ a_hand, SampleNote::kStretchStart, a_sample });
            handed = true;
        }
        if (kCountOneHandWhenDual && a_spent > 0.0f && a_hand == kLeftHand && OtherHandSharesDualCast(a_hand, a_sample, a_now)) {
            if (!g_shared.skipLogged[a_hand]) {
                g_shared.skipLogged[a_hand] = true;
                g_shared.samples.push_back({ a_hand, SampleNote::kDualSkipped, a_sample });
                handed = true;
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
        return handed;
    }

    // Before the original Update, so the frame a cast is released in is still seen as it was
    void Observe(RE::ActorMagicCaster& a_caster, float a_delta)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || a_caster.actor != player) return;  // RE/A/ActorMagicCaster.h:93
        const auto source = a_caster.castingSource;       // RE/A/ActorMagicCaster.h:101; kInstant (our own poison casts) is left out
        if (source != RE::MagicSystem::CastingSource::kLeftHand && source != RE::MagicSystem::CastingSource::kRightHand) return;
        auto* spell = a_caster.currentSpell;  // RE/M/MagicCaster.h:105
        if (!spell) return;
        LogThreadOnce(g_hookThreadLogged, "the player's hand casters update");

        const auto type = spell->GetCastingType();
        const bool concentration = type == RE::MagicSystem::CastingType::kConcentration;
        if (!concentration && type != RE::MagicSystem::CastingType::kFireAndForget) return;
        const auto hand = std::to_underlying(source) == 0 ? kLeftHand : kRightHand;
        const auto state = a_caster.state.get();  // REX/REX/EnumSet.h:55
        HandSample sample{ spell->GetFormID(), static_cast<std::uint32_t>(std::to_underlying(state)), a_caster.currentSpellCost,
            0.0f, a_caster.costCharged, a_caster.flags.any(RE::ActorMagicCaster::Flags::kDualCasting), NowSeconds() };  // REX/REX/EnumSet.h:94

        if (concentration) {
            float spent = 0.0f;
            if (state == kConcentratingState) {
                sample.spellCost = a_caster.GetCurrentSpellCost();  // RE/M/MagicCaster.h:94
                if (std::isfinite(a_delta) && a_delta > 0.0f) spent = sample.spellCost * a_delta;
            }
            bool queue = false;
            {
                std::lock_guard lock(g_sharedMutex);
                if (NoteConcentration(hand, sample, spent, StateBit(state), sample.seenAt) && !g_shared.drainQueued) {
                    g_shared.drainQueued = true;
                    queue = true;
                }
            }
            if (queue) QueueDrain();
        } else {
            if (!std::isfinite(sample.currentSpellCost) || sample.currentSpellCost <= 0.0f) return;
            sample.spellCost = a_caster.GetCurrentSpellCost();  // beside currentSpellCost, for the self test
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
// Public
// =============================================================================

void ToxGame::AddSpentMagicka(float a_amount)
{
    CountSpent(a_amount);
}

float ToxGame::Accumulated()
{
    std::lock_guard lock(g_accumulatedMutex);
    return g_accumulated;
}

void ToxGame::SetAccumulated(float a_value)
{
    std::lock_guard lock(g_accumulatedMutex);
    g_accumulated = a_value;
}

void ToxGame::ResetMagicka()
{
    {
        std::lock_guard lock(g_accumulatedMutex);
        g_accumulated = 0.0f;
    }
    {
        std::lock_guard lock(g_sharedMutex);
        g_shared.concentration.clear();
        g_shared.newStates.clear();
        g_shared.samples.clear();
        g_shared.concentrationHands = {};
        g_shared.stretchLogged = {};
        g_shared.skipLogged = {};
        g_shared.fireHands = {};
        g_shared.drainQueued = false;  // a task still queued drains whatever is pending when it runs
    }
    ToxGame::ResetCheck();
    g_lastCastSpell = 0;
    g_lastCastAt = -1.0;
    g_lastReleaseAt = -1.0;
}

void ToxGame::InstallMagickaTracking()
{
    static bool installed = false;
    if (installed) return;
    installed = true;
    g_mainThread = std::this_thread::get_id();

    // plugins/spelllearning/src/SpellCastImmediateHook.cpp:56-57; [0] is the MagicCaster base's vtable
    REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ActorMagicCaster[0] };
    UpdateHook::func = vtbl.write_vfunc(kUpdateVfuncIndex, UpdateHook::thunk);
    logger::info("Toxicology: hooked ActorMagicCaster::Update (Magicka spent on poison spells)");

    auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
    if (!holder) {
        logger::error("Toxicology: no ScriptEventSourceHolder at kDataLoaded - fire-and-forget poison spells cost no bottles");
        return;
    }
    holder->AddEventSink<RE::TESSpellCastEvent>(CastSink::GetSingleton());  // RE/S/ScriptEventSourceHolder.h:107,147
    logger::info("Toxicology: cast sink registered (TESSpellCastEvent)");
}
