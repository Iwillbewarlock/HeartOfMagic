#include "MagicApplySink.h"

#include "BuffState.h"
#include "FireSet.h"
#include "GameClock.h"
#include "HitPathObserve.h"
#include "SehGuard.h"

#include <atomic>
#include <format>

namespace BlackFlame::HitPath
{
    namespace
    {
        using Detail::Element;
        using Detail::FireSource;

        // "magic effect apply" lines a session, two caps: hits on living targets that are the player's stoked fire, and any hit
        // on a down (dying or dead) target, so a concentration spell cannot use up the ordering evidence before the first kill
        constexpr int kMaxStokedLines = 40;
        constexpr int kMaxDownLines = 24;
        constexpr const char* kFrostKeywordId = "MagicDamageFrost";
        constexpr const char* kShockKeywordId = "MagicDamageShock";

        // Written once at kDataLoaded (RegisterMagicApplySink) before the hooks and the sink can read them
        std::atomic<const RE::BGSKeyword*> g_frostKeyword{ nullptr };
        std::atomic<const RE::BGSKeyword*> g_shockKeyword{ nullptr };

        std::atomic<int> g_stokedLines{ 0 }, g_downLines{ 0 };  // never reset
        std::atomic<std::uint64_t> g_events{ 0 }, g_elemental{ 0 }, g_recorded{ 0 }, g_kept{ 0 }, g_notRecorded{ 0 };
        std::atomic<bool> g_failureReported{ false };

        // The first keyword with this editor id (keywords keep theirs: BGSKeyword::formEditorID), or null
        const RE::BGSKeyword* FindKeyword(RE::TESDataHandler& a_dh, const char* a_editorId)
        {
            for (const auto* keyword : a_dh.GetFormArray<RE::BGSKeyword>()) {
                const char* id = keyword ? keyword->GetFormEditorID() : nullptr;
                if (id && _stricmp(id, a_editorId) == 0) {
                    return keyword;
                }
            }
            return nullptr;
        }

        bool HasElement(const RE::EffectSetting& a_effect, RE::ActorValue a_resist, const RE::BGSKeyword* a_keyword)
        {
            return a_effect.data.resistVariable == a_resist || (a_keyword && a_effect.HasKeyword(a_keyword));
        }

        const char* LifeText(const RE::Actor& a_actor)
        {
            const auto* state = a_actor.AsActorState();
            const auto life = state ? state->GetLifeState() : RE::ACTOR_LIFE_STATE::kAlive;
            return life == RE::ACTOR_LIFE_STATE::kDying ? "dying" : life == RE::ACTOR_LIFE_STATE::kDead ? "dead" : "not down";
        }

        void Handle(const RE::TESMagicEffectApplyEvent& a_event)
        {
            g_events.fetch_add(1, std::memory_order_relaxed);
            auto* target = a_event.target.get();
            auto* actor = target ? target->As<RE::Actor>() : nullptr;
            if (!actor) {
                return;
            }
            const auto* mgef = RE::TESForm::LookupByID<RE::EffectSetting>(a_event.magicEffect);
            // a catalog-tagged fire spell's MGEF may carry no fire keyword or resist value: the fire set says fire too, as
            // NoteFireEffect's IsBurnable does on the effect-start path (Codex round 2)
            auto element = ElementOf(mgef);
            if (element == Element::kNone && mgef && IsFireEffect(mgef)) {
                element = Element::kFire;
            }
            if (element == Element::kNone) {
                return;
            }
            g_elemental.fetch_add(1, std::memory_order_relaxed);
            const auto* caster = a_event.caster.get();
            const bool byPlayer = caster && caster == RE::PlayerCharacter::GetSingleton();
            const char* notRecorded = byPlayer ? nullptr : Detail::WhyNotRecorded(caster, target);
            // no spell in the event: the player's fire is stoked when its MGEF is a fire effect of some fire spell (the top)
            const auto source = !byPlayer ? FireSource::kNpc :
                                element == Element::kFire && IsFireEffect(mgef) && IsStoked() ? FireSource::kPlayerStoked :
                                                                                                 FireSource::kPlayerPlain;
            const bool down = Detail::IsDown(actor);
            const char* outcome = notRecorded;
            if (!notRecorded) {
                const bool recorded = Detail::RecordElementalHit(target->GetFormID(), element, source, down,
                    Detail::HitOrigin::kApplyEvent, mgef->GetFormID());
                (recorded ? g_recorded : g_kept).fetch_add(1, std::memory_order_relaxed);
                outcome = recorded ? "recorded" : "kept stoked (not recorded)";
            } else {
                g_notRecorded.fetch_add(1, std::memory_order_relaxed);
            }
            if (source != FireSource::kPlayerStoked && !down) {
                return;  // only what the corpse mark's ordering question needs is logged
            }
            auto& lines = down ? g_downLines : g_stokedLines;
            const int cap = down ? kMaxDownLines : kMaxStokedLines;
            if (lines.load(std::memory_order_relaxed) >= cap) {
                return;
            }
            const int line = lines.fetch_add(1) + 1;
            if (line > cap) {
                return;
            }
            logger::info("BlackFlame: magic effect apply [{}{}]: MGEF {:08X} '{}' ({}), caster {:08X} ({}), target {:08X} ({}): "
                         "{}, game {:.1f} s, thread {}", down ? "down " : "", line, mgef->GetFormID(), Detail::EditorIdOf(mgef),
                Detail::ElementName(element), caster ? caster->GetFormID() : 0, Detail::FireSourceName(source),
                target->GetFormID(), LifeText(*actor), outcome, GameClock::Seconds(), REX::W32::GetCurrentThreadId());
        }

        class Sink final : public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* a_event,
                RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override
            {
                // Called from engine code on the dispatching thread: nothing may throw out of here, and a fault skips this
                // event only (the guard counts the rest)
                try {
                    if (a_event && !GuardedKeep("magic apply sink", [&]() { Handle(*a_event); }) &&
                        !g_failureReported.exchange(true)) {
                        logger::warn("BlackFlame: magic effect apply sink: a read faulted (logged above); this event was not "
                                     "recorded; further faults are counted by the guard");
                    }
                } catch (const std::exception& e) {
                    if (!g_failureReported.exchange(true)) {
                        logger::error("BlackFlame: magic effect apply sink threw ({}); further failures are not logged", e.what());
                    }
                } catch (...) {
                    if (!g_failureReported.exchange(true)) {
                        logger::error("BlackFlame: magic effect apply sink threw an unknown exception; further failures are not logged");
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void RegisterMagicApplySink()
    {
        static bool registered = false;  // kDataLoaded only (one thread)
        if (registered) {
            return;
        }
        if (auto* dh = RE::TESDataHandler::GetSingleton()) {
            g_frostKeyword.store(FindKeyword(*dh, kFrostKeywordId));
            g_shockKeyword.store(FindKeyword(*dh, kShockKeywordId));
        }
        const auto* frost = g_frostKeyword.load();
        const auto* shock = g_shockKeyword.load();
        if (!CorpseMarkInstalled()) {
            registered = true;
            logger::info("BlackFlame: magic effect apply sink not registered (the corpse mark is not installed: AE 1.6.1170 / 1.7.104 "
                         "and HitPath::Install's ApplyEffectShader hook only); keywords {} {:08X}, {} {:08X}", kFrostKeywordId,
                frost ? frost->GetFormID() : 0, kShockKeywordId, shock ? shock->GetFormID() : 0);
            return;
        }
        auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
        if (!holder) {
            logger::error("BlackFlame: magic effect apply sink: ScriptEventSourceHolder unavailable - the elemental hits are "
                          "recorded at the effect start only (an NPC's re-burn may be read late by the corpse mark)");
            return;
        }
        static auto* sink = new Sink();  // never destroyed: the event source may dispatch at exit
        holder->AddEventSink<RE::TESMagicEffectApplyEvent>(sink);
        registered = true;
        logger::info("BlackFlame: magic effect apply sink added (elemental hits before the Papyrus handlers); keywords {} {:08X}, "
                     "{} {:08X} (0: not found, the resist value alone decides that element)", kFrostKeywordId,
            frost ? frost->GetFormID() : 0, kShockKeywordId, shock ? shock->GetFormID() : 0);
    }

    Detail::Element ElementOf(const RE::EffectSetting* a_effect)
    {
        if (!a_effect) {
            return Element::kNone;
        }
        if (IsFireMgefAny(a_effect)) {
            return Element::kFire;
        }
        if (HasElement(*a_effect, RE::ActorValue::kResistFrost, g_frostKeyword.load(std::memory_order_relaxed))) {
            return Element::kFrost;
        }
        if (HasElement(*a_effect, RE::ActorValue::kResistShock, g_shockKeyword.load(std::memory_order_relaxed))) {
            return Element::kShock;
        }
        return Element::kNone;
    }

    std::string MagicApplyStats()
    {
        return std::format("magic effect apply (session): {} events, {} elemental on actors: recorded {}, kept stoked {}, not "
                           "recorded {} ({} of {} stoked lines, {} of {} down lines)", g_events.load(), g_elemental.load(), g_recorded.load(),
            g_kept.load(), g_notRecorded.load(), std::min(g_stokedLines.load(), kMaxStokedLines), kMaxStokedLines,
            std::min(g_downLines.load(), kMaxDownLines), kMaxDownLines);
    }
}
