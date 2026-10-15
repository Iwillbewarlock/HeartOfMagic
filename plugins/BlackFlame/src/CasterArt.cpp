#include "CasterArt.h"

#include "Config.h"
#include "FireSet.h"
#include "Recolor.h"

#include <array>
#include <format>
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

            Place magicNode;
            Place firstPerson;
            Place thirdPerson;
            Place artEffects;
            if (config.handArt) {
                static const RE::BSFixedString leftName{ kLeftMagicNodeName };
                static const RE::BSFixedString rightName{ kRightMagicNodeName };
                const auto& name = a_hand.source == RE::MagicSystem::CastingSource::kLeftHand ? leftName : rightName;
                magicNode.found = caster->magicNode != nullptr;
                magicNode.stats = RecolorTree(caster->magicNode);
                firstPerson = RecolorNamedNode(a_player, true, name, caster->magicNode);
                thirdPerson = RecolorNamedNode(a_player, false, name, caster->magicNode);
                artEffects = RecolorCastingArtEffects(a_player, caster->castingArt);
            }
            auto* light = caster->light ? caster->light->light.get() : nullptr;
            const bool lightTinted = config.light && TintLight(light);

            const int recolored = magicNode.stats.Recolored() + firstPerson.stats.Recolored() +
                                  thirdPerson.stats.Recolored() + artEffects.stats.Recolored();
            if (a_state.logged && recolored == 0) {
                return;
            }
            logger::info("BlackFlame: hand art: spell {:08X} {} hand{}: {} | {} | {} | {} | casting light {}",
                spell->GetFormID(), a_hand.label, a_state.logged ? " (more geometry)" : "",
                DescribePlace("magicNode", magicNode),
                DescribePlace(std::format("1st person '{}'", a_hand.nodeName).c_str(), firstPerson),
                DescribePlace(std::format("3rd person '{}'", a_hand.nodeName).c_str(), thirdPerson),
                DescribePlace("casting art effects", artEffects),
                !caster->light ? "none" : (lightTinted ? "tinted" : (config.light ? "has no NiLight" : "left (light off)")));
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
