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
        using CT = RE::MagicSystem::CastingType;

        static_assert(static_cast<std::uint32_t>(CT::kConstantEffect) == static_cast<std::uint32_t>(L::CastingType::kConstantEffect));
        static_assert(static_cast<std::uint32_t>(CT::kFireAndForget) == static_cast<std::uint32_t>(L::CastingType::kFireAndForget));
        static_assert(static_cast<std::uint32_t>(CT::kConcentration) == static_cast<std::uint32_t>(L::CastingType::kConcentration));
        static_assert(static_cast<std::uint32_t>(CT::kScroll) == static_cast<std::uint32_t>(L::CastingType::kScroll));

        constexpr auto kKillWindow = std::chrono::seconds(2);  // lantern damage this recent makes a death a "lantern kill"
        constexpr float kEffectiveness = 1.0f;
        constexpr std::uint32_t kNoCastingType = 0xFFFFFFFF;  // the rejected line's type for a source that is no spell

        std::atomic<bool> g_enabled{ false };
        std::unordered_set<RE::FormID> g_hazardSpells;  // kDataLoaded, read-only afterwards
        L::Settings g_settings;                         // kDataLoaded, read-only afterwards
        Clock::time_point g_epoch;                      // the hit lines' t=<ms> counts from Init

        struct Cached
        {
            L::Classified classified;
            std::uint32_t castingType = kNoCastingType;
        };

        // Guarded by g_mutex; heap-held like the other layers' tables
        TrackedMutex g_mutex;
        struct State
        {
            std::unordered_map<RE::FormID, Cached> sources;
            L::IcdTable icd;
            std::unordered_map<RE::FormID, Clock::time_point> recent;  // target -> last lantern damage (the kill line)
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
                facts.castingType = out.castingType;
                facts.cloak = IsCloakSpell(a_source);
                facts.hazardSpell = g_hazardSpells.contains(a_source);
                facts.fireSpell = IsFireSpell(spell);
            }
            out.classified = L::Classify(facts);
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
            if (inserted && fresh.classified.bucket == L::Bucket::kNone) {
                if (fresh.castingType == kNoCastingType) {
                    logger::info("BlackFlame: lantern hit: rejected src={:08X} type=none ({})", a_source, L::RejectName(fresh.classified.why));
                } else {
                    logger::info("BlackFlame: lantern hit: rejected src={:08X} type={} ({})", a_source, fresh.castingType,
                        L::RejectName(fresh.classified.why));
                }
            }
            return fresh;
        }

        long long SinceEpochMs(Clock::time_point a_now)
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(a_now - g_epoch).count();
        }

        void NoteDamage(RE::FormID a_target)
        {
            const auto now = Clock::now();
            std::lock_guard lock(g_mutex);
            auto& recent = Shared().recent;
            for (auto it = recent.begin(); it != recent.end();) {
                it = now - it->second > kKillWindow ? recent.erase(it) : std::next(it);
            }
            recent[a_target] = now;
        }

        // Game thread (the task): the checks that read the world, then the cast
        void Burn(RE::ObjectRefHandle a_target, L::Bucket a_bucket, std::uint32_t a_generation)
        {
            if (a_generation != WorldGeneration() || !IsOnGameThread()) {
                return;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            const auto ref = a_target.get();
            auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
            auto* spell = GetForms().lanternBurnSpell;
            if (!player || !actor || actor == player || actor->IsDead() || !spell) {
                return;
            }
            // The flag alone could be the console stoke spell: the lantern's own effect must be the reason
            if (!IsStoked() || !PlayerHasLanternEffect()) {
                return;
            }
            auto* caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
            auto* values = actor->AsActorValueOwner();
            if (!caster || !values) {
                return;
            }
            const float max = actor->GetActorValueMax(RE::ActorValue::kHealth);
            const auto intended = static_cast<float>(L::DamageFor(max, L::PctFor(a_bucket, g_settings)));
            if (intended <= 0.0f) {
                return;
            }
            const float before = values->GetActorValue(RE::ActorValue::kHealth);
            caster->CastSpellImmediate(spell, true, actor, kEffectiveness, false, intended, player);
            const float after = values->GetActorValue(RE::ActorValue::kHealth);
            NoteDamage(actor->GetFormID());
            if (GetConfig().lanternLog) {
                logger::info("BlackFlame: lantern damage: tgt={:08X} max={} intended={} before={} after={} difficulty={}",
                    actor->GetFormID(), max, intended, before, after, player->GetGameStatsData().difficulty);
            }
        }

        // The spells of the fire hazards (BGSHazard data.spell): the concentration bucket (design review R5)
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
                // while dying, then true): the first one with a recent lantern hit logs, and its entry goes
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
                            recent = Clock::now() - it->second <= kKillWindow;
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
        g_settings = { config.lanternSinglePct, config.lanternSingleIcd, config.lanternConcPct, config.lanternConcIcd };
        g_epoch = Clock::now();
        BuildHazardSpells();
        if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
            holder->AddEventSink<RE::TESDeathEvent>(DeathSink::GetSingleton());
        } else {
            logger::warn("BlackFlame: ScriptEventSourceHolder unavailable - no lantern kill lines");
        }
        g_enabled.store(true);
        logger::info("BlackFlame: lantern damage on: single {} per hit every {} s, concentration and hazards {} every {} s; {} fire "
                     "hazard spell(s) in the concentration bucket",
            g_settings.singlePct, g_settings.singleIcd, g_settings.concPct, g_settings.concIcd, g_hazardSpells.size());
    }

    void OnHit(const RE::TESHitEvent& a_event)
    {
        // The buff flag is an atomic read: without it nothing is classified, timed or queued (the task checks the lantern)
        if (!g_enabled.load(std::memory_order_relaxed) || !a_event.cause || !a_event.cause->IsPlayerRef() || !a_event.target ||
            a_event.source == 0 || !IsStoked()) {
            return;
        }
        const auto cached = Lookup(a_event.source);
        const auto bucket = cached.classified.bucket;
        if (bucket == L::Bucket::kNone) {
            return;
        }
        const auto target = a_event.target->GetFormID();
        const auto now = Clock::now();
        {
            std::lock_guard lock(g_mutex);
            if (!Shared().icd.TryAccept(target, bucket, now, L::IcdFor(bucket, g_settings))) {
                return;
            }
        }
        if (GetConfig().lanternLog) {
            logger::info("BlackFlame: lantern hit: src={:08X} proj={:08X} tgt={:08X} bucket={} t={}", a_event.source,
                a_event.projectile, target, L::BucketName(bucket), SinceEpochMs(now));
        }
        const auto handle = a_event.target->GetHandle();
        const auto generation = WorldGeneration();
        (void)QueueTask("BlackFlameLanternBurn", [handle, bucket, generation]() { Burn(handle, bucket, generation); });
    }

    void Reset()
    {
        std::lock_guard lock(g_mutex);
        Shared().icd.Clear();
        Shared().recent.clear();
    }
}
