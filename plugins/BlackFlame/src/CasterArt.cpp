#include "CasterArt.h"
#include "TrackedMutex.h"

#include "Config.h"
#include "FireSet.h"
#include "LogLimit.h"
#include "EffectLight.h"
#include "Recolor.h"
#include "SehGuard.h"

#include <array>
#include <atomic>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace BlackFlame
{
    namespace
    {
        // Vanilla skeleton node names (believed, not checked against every skeleton:
        // the per-cast log says whether each lookup found a node)
        constexpr const char* kLeftMagicNodeName = "NPC L MagicNode [LMag]";
        constexpr const char* kRightMagicNodeName = "NPC R MagicNode [RMag]";
        constexpr std::size_t kHandCount = 2;

        struct HandState
        {
            RE::FormID spell = 0;  // the fire spell being cast, 0 when none
            bool logged = false;   // this cast's first line written
        };

        struct Hand
        {
            RE::MagicSystem::CastingSource source;
            const char* label;
            const char* nodeName;
        };

        constexpr std::array<Hand, kHandCount> kHands{
            Hand{ RE::MagicSystem::CastingSource::kLeftHand, "left", kLeftMagicNodeName },
            Hand{ RE::MagicSystem::CastingSource::kRightHand, "right", kRightMagicNodeName },
        };

        // Every hand-art property we recoloured, with what it was, and every casting light we tinted,
        // with its old colour, for the restore when the buff ends. Holding them keeps them alive until
        // then (entries only we still hold are dropped each tick). All of it, and the hand states, under
        // g_mutex: the ticker task and the message handler (buff off, load) both reach it, and that they
        // run on one thread is not proven (first in-game run: tasks on six threads). Heap-held and never
        // freed: these NiPointers must not DecRef at DLL unload. Scene-graph work stays outside the lock.
        struct RecolouredProperty
        {
            RE::NiPointer<RE::BSEffectShaderProperty> property;
            PropertyOriginal original;
        };

        struct TintedLight
        {
            RE::NiPointer<RE::NiLight> light;
            RE::NiColor original;
        };

        struct HandArtState
        {
            std::unordered_map<RE::BSEffectShaderProperty*, RecolouredProperty> properties;
            std::unordered_map<RE::NiLight*, TintedLight> lights;
            std::array<HandState, kHandCount> hands{};
        };

        TrackedMutex g_mutex;
        std::atomic<std::uint32_t> g_restoreGeneration{ 0 };  // bumped by every restore, inside g_mutex
        thread_local std::optional<std::uint32_t> t_tickGeneration;  // set while a hand tick runs

        // RAII: the tick generation is set for one hand tick and the previous value comes back however it ends
        class TickScope
        {
        public:
            explicit TickScope(std::uint32_t a_generation) noexcept :
                m_previous(t_tickGeneration)
            {
                t_tickGeneration = a_generation;
            }
            ~TickScope() { t_tickGeneration = m_previous; }
            TickScope(const TickScope&) = delete;
            TickScope& operator=(const TickScope&) = delete;

        private:
            std::optional<std::uint32_t> m_previous;
        };

        HandArtState& State()
        {
            static auto* state = std::make_unique<HandArtState>().release();
            return *state;
        }

        // Remembers the first original of each property (a later recolour of the same one is ignored)
        const RecolorObserver& Remember()
        {
            return HandArtObserver();
        }
    }

    const RecolorObserver& HandArtObserver()
    {
        static const RecolorObserver* observer = std::make_unique<RecolorObserver>(
            [](RE::BSEffectShaderProperty* a_prop, const PropertyOriginal& a_original) {
                {
                    std::lock_guard lock(g_mutex);
                    // A restore that ran after this tick began: the recolour is put back at once instead of
                    // being remembered for a restore that already happened
                    if (!t_tickGeneration || *t_tickGeneration == g_restoreGeneration.load()) {
                        State().properties.try_emplace(a_prop,
                            RecolouredProperty{ RE::NiPointer<RE::BSEffectShaderProperty>(a_prop), a_original });
                        return;
                    }
                }
                (void)GuardedKeep("hand art restore (late)", [&]() { (void)RestoreProperty(a_prop, a_original); });
            }).release();
        return *observer;
    }

    bool TintRememberedLight(RE::NiLight* a_light)
    {
        if (!a_light) {
            return false;
        }
        {
            std::lock_guard lock(g_mutex);
            State().lights.try_emplace(a_light,
                TintedLight{ RE::NiPointer<RE::NiLight>(a_light), a_light->GetLightRuntimeData().diffuse });
        }
        return TintLight(a_light);
    }

    namespace
    {
        // The caster's light, through the RTTI check, tinted; its first old colour remembered
        bool TintCasterLight(RE::ActorMagicCaster* a_caster)
        {
            bool tinted = false;
            (void)Guarded("casting light", [&]() { tinted = TintRememberedLight(CheckedLight(a_caster->light.get(), "casting")); });
            return tinted;
        }

        // Entries only we still hold (the art or light is gone from the scene): restoring them is
        // pointless, keeping them would grow the lists for the whole buff
        void PruneDead()
        {
            std::lock_guard lock(g_mutex);
            auto& state = State();
            std::erase_if(state.properties, [](const auto& a_entry) { return a_entry.second.property->GetRefCount() <= 1; });
            std::erase_if(state.lights, [](const auto& a_entry) { return a_entry.second.light->GetRefCount() <= 1; });
        }

        // The fire spell this hand casts now, or else the one it holds readied
        RE::MagicItem* FireSpellOfHand(RE::PlayerCharacter* a_player, RE::ActorMagicCaster* a_caster, const Hand& a_hand)
        {
            if (a_caster && IsFireSpell(a_caster->currentSpell)) {
                return a_caster->currentSpell;
            }
            auto* equipped = a_player->GetEquippedObject(a_hand.source == RE::MagicSystem::CastingSource::kLeftHand);
            auto* spell = equipped ? equipped->As<RE::SpellItem>() : nullptr;
            return IsFireSpell(spell) ? spell : nullptr;
        }

        // What one place gave: whether it existed and what recolouring it did
        struct Place
        {
            bool found = false;
            bool sameAsMagicNode = false;
            RecolorStats stats;
        };

        Place RecolorNamedNode(RE::PlayerCharacter* a_player, bool a_firstPerson, const RE::BSFixedString& a_name,
            const RE::NiNode* a_magicNode)
        {
            Place place;
            auto* root = a_player->Get3D(a_firstPerson);
            auto* node = root ? root->GetObjectByName(a_name) : nullptr;
            place.found = node != nullptr;
            if (!node) {
                return place;
            }
            if (node == a_magicNode) {
                place.sameAsMagicNode = true;  // already done through magicNode
                return place;
            }
            place.stats = RecolorTree(node, Remember());
            return place;
        }

        // ProcessLists holds its spin lock while it calls back: collect inside, recolour after
        Place RecolorCastingArtEffects(RE::PlayerCharacter* a_player, const RE::BGSArtObject* a_art)
        {
            Place place;
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!a_art || !lists) {
                return place;
            }
            const auto playerHandle = a_player->GetHandle();
            std::vector<RE::NiPointer<RE::ModelReferenceEffect>> found;
            lists->ForEachModelEffect([&](RE::ModelReferenceEffect* a_effect) {
                if (a_effect && !a_effect->finished && a_effect->artObject == a_art && a_effect->target == playerHandle) {
                    found.emplace_back(a_effect);
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            place.found = !found.empty();
            for (const auto& effect : found) {
                place.stats.Add(RecolorTree(effect->Get3D(), Remember()));
            }
            return place;
        }

        std::string DescribePlace(const char* a_label, const Place& a_place)
        {
            if (!a_place.found) {
                return std::format("{}: none", a_label);
            }
            if (a_place.sameAsMagicNode) {
                return std::format("{}: same node as magicNode", a_label);
            }
            return std::format("{}: {}", a_label, a_place.stats.Describe());
        }

        // Vanilla skeleton node names as BSFixedStrings, made once on the game thread and never
        // freed: a static BSFixedString would release into the string cache at DLL unload,
        // after the game tore it down (intentional two-string leak)
        const RE::BSFixedString& MagicNodeName(RE::MagicSystem::CastingSource a_source)
        {
            static const RE::BSFixedString* const left = std::make_unique<RE::BSFixedString>(kLeftMagicNodeName).release();
            static const RE::BSFixedString* const right = std::make_unique<RE::BSFixedString>(kRightMagicNodeName).release();
            return a_source == RE::MagicSystem::CastingSource::kLeftHand ? *left : *right;
        }

        // The casting art: ActorMagicCaster::castingArtData.attachedArt (ActorMagicCaster.h:91,
        // AttachTechniqueInput.h:24). Only when it is null or holds no effect geometry, the old
        // places: the caster's magicNode and the hand's magic node by name in both skeletons, and
        // as the last resort the player's ModelReferenceEffects of the caster's castingArt.
        std::string RecolorHandArt(RE::PlayerCharacter* a_player, RE::ActorMagicCaster* a_caster, const Hand& a_hand,
            int& a_recolored)
        {
            std::string attachedNote = "attachedArt: null";
            if (auto* attached = a_caster->castingArtData.attachedArt.get()) {
                Place art;
                art.found = true;
                art.stats = RecolorTree(attached, Remember());
                if (art.stats.effectGeometries > 0) {
                    a_recolored = art.stats.Recolored();
                    return DescribePlace("attachedArt", art);
                }
                attachedNote = "attachedArt: no effect geometry";  // fall through to the old places
            }
            Place magicNode;
            magicNode.found = a_caster->magicNode != nullptr;
            magicNode.stats = RecolorTree(a_caster->magicNode, Remember());
            const auto& name = MagicNodeName(a_hand.source);
            const Place firstPerson = RecolorNamedNode(a_player, true, name, a_caster->magicNode);
            const Place thirdPerson = RecolorNamedNode(a_player, false, name, a_caster->magicNode);
            Place artEffects;
            const int seen = magicNode.stats.effectGeometries + firstPerson.stats.effectGeometries +
                             thirdPerson.stats.effectGeometries;
            if (seen == 0) {
                artEffects = RecolorCastingArtEffects(a_player, a_caster->castingArt);
            }
            a_recolored = magicNode.stats.Recolored() + firstPerson.stats.Recolored() + thirdPerson.stats.Recolored() +
                          artEffects.stats.Recolored();
            return std::format("{}, FALLBACK {} | {} | {} | {}", attachedNote, DescribePlace("magicNode", magicNode),
                DescribePlace(std::format("1st person '{}'", a_hand.nodeName).c_str(), firstPerson),
                DescribePlace(std::format("3rd person '{}'", a_hand.nodeName).c_str(), thirdPerson),
                seen == 0 ? DescribePlace("casting art effects", artEffects) : "casting art effects: not searched");
        }

        void TickHand(RE::PlayerCharacter* a_player, const Hand& a_hand, HandState& a_state)
        {
            const auto& config = GetConfig();
            auto* caster = skyrim_cast<RE::ActorMagicCaster*>(a_player->GetMagicCaster(a_hand.source));
            // Casting or only readied: the hand flames of a held fire spell turn grey too
            auto* spell = FireSpellOfHand(a_player, caster, a_hand);
            if (!spell || !caster) {
                a_state = {};
                return;
            }
            if (a_state.spell != spell->GetFormID()) {
                a_state = { spell->GetFormID(), false };
            }

            int recolored = 0;
            std::string places = "hand art off";
            if (config.handArt &&
                !Guarded("hand art recolour", [&]() { places = RecolorHandArt(a_player, caster, a_hand, recolored); })) {
                places = "hand art recolour faulted (step off)";
            }
            const bool lightTinted = config.light && TintCasterLight(caster);

            if (a_state.logged && recolored == 0) {
                return;
            }
            int suppressed = 0;
            if (a_state.logged && !LogLimit::Allow("hand art", spell->GetFormID(), suppressed)) {
                return;
            }
            logger::info("BlackFlame: hand art: spell {:08X} {} hand{}: {} | casting light {}{}", spell->GetFormID(),
                a_hand.label, a_state.logged ? " (more geometry)" : "", places,
                !caster->light ? "none" : (lightTinted ? "tinted" : (config.light ? "has no NiLight" : "left (light off)")),
                suppressed ? std::format(" ({} similar lines held back)", suppressed) : "");
            a_state.logged = true;
        }
    }

    int RestoreHandArt()
    {
        // Taken out under the lock, put back outside it; released here (by the caller's thread: the
        // message handler or a task)
        HandArtState taken;
        {
            std::lock_guard lock(g_mutex);
            g_restoreGeneration.fetch_add(1);  // inside the swap: a tick that started before this does not write back
            taken.properties.swap(State().properties);
            taken.lights.swap(State().lights);
            State().hands = {};
        }
        // Item by item: one bad property must not leave the rest grey, and a fault never turns restores off
        int restored = 0;
        for (auto& [raw, entry] : taken.properties) {
            bool ok = false;
            (void)GuardedKeep("hand art restore (property)",
                [&]() { ok = RestoreProperty(entry.property.get(), entry.original); });
            restored += ok ? 1 : 0;
        }
        for (auto& [raw, entry] : taken.lights) {
            (void)GuardedKeep("hand art restore (light)",
                [&]() { entry.light->GetLightRuntimeData().diffuse = entry.original; });
        }
        if (!taken.properties.empty() || !taken.lights.empty()) {
            logger::info("BlackFlame: hand art restored: {} of {} recoloured properties put back, {} casting light(s) "
                         "back to their colour", restored, taken.properties.size(), taken.lights.size());
        }
        return restored;
    }

    void OnBuffChanged(bool a_on)
    {
        if (a_on) {
            TickCasterArt();  // the readied hand flames turn grey at once, not at the next tick
        } else {
            (void)RestoreHandArt();
        }
    }

    void ResetCasterArt()
    {
        (void)RestoreHandArt();
    }

    void TickCasterArt()
    {
        const auto& config = GetConfig();
        if (!config.handArt && !config.light) {
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }
        PruneDead();
        for (std::size_t i = 0; i < kHandCount; ++i) {
            HandState hand;
            const auto generation = g_restoreGeneration.load();
            const TickScope scope(generation);
            {
                std::lock_guard lock(g_mutex);
                hand = State().hands[i];
            }
            TickHand(player, kHands[i], hand);
            std::lock_guard lock(g_mutex);
            if (generation == g_restoreGeneration.load()) {
                State().hands[i] = hand;  // a restore in between already reset the hands: nothing stale goes back
            }
        }
    }
}
