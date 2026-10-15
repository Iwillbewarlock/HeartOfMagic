#include "CasterArt.h"

#include "Config.h"
#include "FireSet.h"
#include "LogLimit.h"
#include "Recolor.h"

#include <array>
#include <format>
#include <memory>
#include <string>
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

        std::array<HandState, kHandCount> g_handStates{};  // game thread only

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
            place.stats = RecolorTree(node);
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
                place.stats.Add(RecolorTree(effect->Get3D()));
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
        // AttachTechniqueInput.h:24). Only when it is null, the old places: the caster's
        // magicNode and the hand's magic node by name in both skeletons, and as the last
        // resort the player's ModelReferenceEffects of the caster's castingArt.
        std::string RecolorHandArt(RE::PlayerCharacter* a_player, RE::ActorMagicCaster* a_caster, const Hand& a_hand,
            int& a_recolored)
        {
            if (auto* attached = a_caster->castingArtData.attachedArt.get()) {
                Place art;
                art.found = true;
                art.stats = RecolorTree(attached);
                a_recolored = art.stats.Recolored();
                return DescribePlace("attachedArt", art);
            }
            Place magicNode;
            magicNode.found = a_caster->magicNode != nullptr;
            magicNode.stats = RecolorTree(a_caster->magicNode);
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
            return std::format("attachedArt: null, FALLBACK {} | {} | {} | {}", DescribePlace("magicNode", magicNode),
                DescribePlace(std::format("1st person '{}'", a_hand.nodeName).c_str(), firstPerson),
                DescribePlace(std::format("3rd person '{}'", a_hand.nodeName).c_str(), thirdPerson),
                seen == 0 ? DescribePlace("casting art effects", artEffects) : "casting art effects: not searched");
        }

        void TickHand(RE::PlayerCharacter* a_player, const Hand& a_hand, HandState& a_state)
        {
            const auto& config = GetConfig();
            auto* caster = skyrim_cast<RE::ActorMagicCaster*>(a_player->GetMagicCaster(a_hand.source));
            auto* spell = caster ? caster->currentSpell : nullptr;
            if (!spell || !IsFireSpell(spell)) {
                a_state = {};
                return;
            }
            if (a_state.spell != spell->GetFormID()) {
                a_state = { spell->GetFormID(), false };
            }

            int recolored = 0;
            const std::string places = config.handArt ? RecolorHandArt(a_player, caster, a_hand, recolored) : "hand art off";
            auto* light = caster->light ? caster->light->light.get() : nullptr;
            const bool lightTinted = config.light && TintLight(light);

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
        for (std::size_t i = 0; i < kHandCount; ++i) {
            TickHand(player, kHands[i], g_handStates[i]);
        }
    }
}
