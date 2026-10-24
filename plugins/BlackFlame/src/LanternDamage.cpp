#include "LanternDamage.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "Forms.h"
#include "GameThread.h"
#include "LanternDamageRules.h"
#include "TrackedMutex.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace BlackFlame::LanternDamage
{
    namespace
    {
        namespace L = LanternRules;
        using Clock = L::IcdTable::Clock;

        constexpr auto kKillGrace = std::chrono::seconds(1);  // past the burn's end, for the death event to arrive
        constexpr float kEffectiveness = 1.0f;
        constexpr bool kShowHitArt = false;                   // CastSpellImmediate's noHitEffectArt: the burn shader must show
        constexpr std::uint32_t kNoCastingType = 0xFFFFFFFF;  // the rejected line's type for a source that is no spell

        std::atomic<bool> g_enabled{ false };
        std::unordered_set<RE::FormID> g_hazardSpells;  // kDataLoaded, read-only afterwards
        L::Settings g_settings;                         // kDataLoaded, read-only afterwards
        Clock::duration g_killWindow{};                 // kDataLoaded: burn seconds + kKillGrace
        Clock::time_point g_epoch;                      // the hit lines' t=<ms> counts from Init

        struct Cached
        {
            L::Reject why = L::Reject::kNone;  // kNone: accepted
            std::uint32_t castingType = kNoCastingType;
        };

        // Guarded by g_mutex; heap-held like the other layers' tables
        TrackedMutex g_mutex;
        struct State
        {
            std::unordered_map<RE::FormID, Cached> sources;
            L::IcdTable reapply;
            std::unordered_map<RE::FormID, Clock::time_point> recent;  // target -> last burn (the kill line)
        };

        State& Shared()
        {
            static auto* state = std::make_unique<State>().release();
            return *state;
        }

        // Outside g_mutex: LookupByID takes the engine's form map lock
        Cached ClassifySource(RE::FormID a_source)
        {
            L::SourceFacts facts;
            const auto& forms = GetForms();
            facts.ownSpell = forms.lanternBurnSpell && a_source == forms.lanternBurnSpell->GetFormID();
            const auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_source);
            facts.spellItem = spell != nullptr;
            Cached out;
            if (spell) {
                out.castingType = static_cast<std::uint32_t>(spell->GetCastingType());
                facts.cloak = IsCloakSpell(a_source);
                facts.hazardSpell = g_hazardSpells.contains(a_source);
                facts.fireSpell = IsFireSpell(spell);
            }
            out.why = L::Classify(facts);
            return out;
        }

        // The cache first; a new source is classified outside the lock and logged once when rejected
        Cached Lookup(RE::FormID a_source)
        {
            {
                std::lock_guard lock(g_mutex);
                const auto it = Shared().sources.find(a_source);
                if (it != Shared().sources.end()) {
                    return it->second;
                }
            }
            const Cached fresh = ClassifySource(a_source);
            bool inserted = false;
            {
                std::lock_guard lock(g_mutex);
                inserted = Shared().sources.emplace(a_source, fresh).second;
            }
            if (inserted && fresh.why != L::Reject::kNone) {
                if (fresh.castingType == kNoCastingType) {
                    logger::info("BlackFlame: lantern hit: rejected src={:08X} type=none ({})", a_source, L::RejectName(fresh.why));
                } else {
                    logger::info("BlackFlame: lantern hit: rejected src={:08X} type={} ({})", a_source, fresh.castingType,
                        L::RejectName(fresh.why));
                }
            }
            return fresh;
        }

        long long SinceEpochMs(Clock::time_point a_now)
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(a_now - g_epoch).count();
        }

        // Before the cast (review 1 R5): a kill inside CastSpellImmediate raises its death event before the cast returns
        void NoteBurn(RE::FormID a_target)
        {
            const auto now = Clock::now();
            std::lock_guard lock(g_mutex);
            auto& recent = Shared().recent;
            for (auto it = recent.begin(); it != recent.end();) {
                it = now - it->second > g_killWindow ? recent.erase(it) : std::next(it);
            }
            recent[a_target] = now;
        }

        struct BurnEffects
        {
            int count = 0;
            float magnitude = 0.0f;  // the newest one's (smallest elapsed time)
        };

        // The 0x813 effects on the target that are neither dispelled nor inactive (expected 1: no stacking)
        BurnEffects CountBurnEffects(RE::Actor& a_actor, const RE::EffectSetting* a_burnEffect)
        {
            BurnEffects out;
            auto* target = a_actor.AsMagicTarget();
            auto* effects = target ? target->GetActiveEffectList() : nullptr;
            if (!effects || !a_burnEffect) {
                return out;
            }
            float newest = 0.0f;
            for (const auto* effect : *effects) {
                if (!effect || effect->GetBaseObject() != a_burnEffect ||
                    effect->flags.any(RE::ActiveEffect::Flag::kDispelled, RE::ActiveEffect::Flag::kInactive)) {
                    continue;
                }
                if (out.count == 0 || effect->elapsedSeconds <= newest) {
                    newest = effect->elapsedSeconds;
                    out.magnitude = effect->magnitude;
                }
                ++out.count;
            }
            return out;
        }

        struct HitFacts
        {
            RE::FormID source = 0;
            RE::FormID projectile = 0;
            Clock::time_point at;
        };

        // Game thread (the task): the checks that read the world, then the hit line, the burn noted, the cast
        void Burn(RE::ObjectRefHandle a_target, HitFacts a_hit, std::uint32_t a_generation)
        {
            if (a_generation != WorldGeneration() || !IsOnGameThread()) {
                return;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            const auto ref = a_target.get();
            auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
            const auto& forms = GetForms();
            auto* spell = forms.lanternBurnSpell;
            if (!player || !actor || actor == player || actor->IsDead() || !spell) {
                return;
            }
            // The flag alone could be the console stoke spell: the lantern's own effect must be the reason
            if (!IsStoked() || !PlayerHasLanternEffect()) {
                return;
            }
            auto* caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
            if (!caster) {
                return;
            }
            const float max = actor->GetActorValueMax(RE::ActorValue::kHealth);
            const auto rate = static_cast<float>(L::RateFor(max, g_settings.burnPct, g_settings.burnSeconds));
            if (rate <= 0.0f) {
                return;  // no positive percentage damage
            }
            const bool log = GetConfig().lanternLog;
            if (log) {
                logger::info("BlackFlame: lantern hit: src={:08X} proj={:08X} tgt={:08X} t={}", a_hit.source, a_hit.projectile,
                    actor->GetFormID(), SinceEpochMs(a_hit.at));
            }
            NoteBurn(actor->GetFormID());
            caster->CastSpellImmediate(spell, kShowHitArt, actor, kEffectiveness, false, rate, player);
            if (log) {
                const auto found = CountBurnEffects(*actor, forms.lanternBurnEffect);
                logger::info("BlackFlame: lantern burn: tgt={:08X} max={} rate={}/s for {} s effects={} magnitude={} difficulty={}",
                    actor->GetFormID(), max, rate, g_settings.burnSeconds, found.count, found.magnitude,
                    player->GetGameStatsData().difficulty);
            }
        }

        // The spells of the fire hazards (BGSHazard data.spell): a hazard's spell need not be a fire spell itself
        void BuildHazardSpells()
        {
            g_hazardSpells.clear();
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!dh) {
                return;
            }
            for (const auto* hazard : dh->GetFormArray<RE::BGSHazard>()) {
                if (hazard && hazard->data.spell && IsFireHazard(hazard)) {
                    g_hazardSpells.insert(hazard->data.spell->GetFormID());
                }
            }
        }

        // The burn's length: HoMBF_LanternBurnSpell's HoMBF_LanternBurnEffect entry (0 when it is missing)
        std::uint32_t BurnSecondsFromRecord()
        {
            const auto& forms = GetForms();
            if (!forms.lanternBurnSpell || !forms.lanternBurnEffect) {
                return 0;
            }
            for (const auto* effect : forms.lanternBurnSpell->effects) {
                if (effect && effect->baseEffect == forms.lanternBurnEffect) {
                    return effect->effectItem.duration;
                }
            }
            return 0;
        }

        class DeathSink final : public RE::BSTEventSink<RE::TESDeathEvent>
        {
        public:
            static DeathSink* GetSingleton()
            {
                static DeathSink instance;
                return &instance;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override
            {
                // Called from engine code: nothing may throw out of here. The event comes twice per death (dead false
                // while dying, then true): the first one with a recent burn logs, and its entry goes
                try {
                    if (!a_event || !a_event->actorDying) {
                        return RE::BSEventNotifyControl::kContinue;
                    }
                    const auto victim = a_event->actorDying->GetFormID();
                    bool recent = false;
                    {
                        std::lock_guard lock(g_mutex);
                        auto& table = Shared().recent;
                        const auto it = table.find(victim);
                        if (it != table.end()) {
                            recent = Clock::now() - it->second <= g_killWindow;
                            table.erase(it);
                        }
                    }
                    if (recent && GetConfig().lanternLog) {
                        logger::info("BlackFlame: lantern kill: tgt={:08X} killer={:08X}", victim,
                            a_event->actorKiller ? a_event->actorKiller->GetFormID() : 0);
                    }
                } catch (...) {
                    // a kill line lost
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            DeathSink() = default;
            ~DeathSink() override = default;
        };
    }

    void Init()
    {
        const auto& config = GetConfig();
        if (!GetForms().lanternForms || !config.lanternDamage) {
            logger::info("BlackFlame: lantern damage off ({})", !GetForms().lanternForms ? "lantern records missing" : "lanternDamage false");
            return;
        }
        const auto burnSeconds = BurnSecondsFromRecord();
        logger::info("BlackFlame: lantern burn spell {:08X}: effect duration {} s", GetForms().lanternBurnSpell->GetFormID(), burnSeconds);
        if (burnSeconds == 0) {
            logger::warn("BlackFlame: lantern damage off (HoMBF_LanternBurnSpell's HoMBF_LanternBurnEffect entry has no duration "
                         "above 0: the burn rate is per second over that duration - check the ESP record 0x812)");
            return;
        }
        g_settings = { config.lanternBurnPct, config.lanternReapply, static_cast<double>(burnSeconds) };
        g_killWindow = std::chrono::duration_cast<Clock::duration>(std::chrono::seconds(burnSeconds) + kKillGrace);
        g_epoch = Clock::now();
        BuildHazardSpells();
        if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
            holder->AddEventSink<RE::TESDeathEvent>(DeathSink::GetSingleton());
        } else {
            logger::warn("BlackFlame: ScriptEventSourceHolder unavailable - no lantern kill lines");
        }
        g_enabled.store(true);
        logger::info("BlackFlame: lantern damage on: black flame {} of max health over {} s per hit, re-applied at most every "
                     "{} s per target; {} fire hazard spell(s) count as fire",
            g_settings.burnPct, g_settings.burnSeconds, g_settings.reapplySeconds, g_hazardSpells.size());
    }

    void OnHit(const RE::TESHitEvent& a_event)
    {
        // The buff flag is an atomic read: without it nothing is classified, throttled or queued (the task checks the lantern)
        if (!g_enabled.load(std::memory_order_acquire) || !a_event.cause || !a_event.cause->IsPlayerRef() || !a_event.target ||
            a_event.source == 0 || !IsStoked()) {
            return;
        }
        if (Lookup(a_event.source).why != L::Reject::kNone) {
            return;
        }
        const auto target = a_event.target->GetFormID();
        const auto now = Clock::now();
        {
            std::lock_guard lock(g_mutex);
            if (!Shared().reapply.TryAccept(target, now, g_settings.reapplySeconds)) {
                return;
            }
        }
        const auto handle = a_event.target->GetHandle();
        const auto generation = WorldGeneration();
        const HitFacts hit{ a_event.source, a_event.projectile, now };
        (void)QueueTask("BlackFlameLanternBurn", [handle, hit, generation]() { Burn(handle, hit, generation); });
    }

    void Reset()
    {
        std::lock_guard lock(g_mutex);
        Shared().reapply.Clear();
        Shared().recent.clear();
    }
}
