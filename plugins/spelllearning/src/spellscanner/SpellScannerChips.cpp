#include "Common.h"
#include "SpellScanner.h"

// =============================================================================
// SPELL CARD CHIPS
// =============================================================================
//
// The short keyword line on the spell card ("fire - projectile - destruction").
// Every chip comes from a closed set the engine defines - resist actor values,
// projectile types, delivery, archetypes - so there is nothing here that a new
// mod can bring that the table has not seen. No names, no text, no mod keywords:
// those differ by language and by load order.
//
// Chips are stable ids ("element.fire"). The panel translates them through its
// language files (chips.* keys), so this file never holds display text.

namespace SpellScanner
{
    namespace
    {
        // The card has one line for these; past this they wrap and push the
        // description off the bottom bar.
        constexpr std::size_t kMaxChips = 6;

        // What the base game's own magic keywords say about an effect. Only
        // keywords the base game or an official DLC defines are looked up here
        // (IsVanillaKeyword), so this list cannot grow: it is the vanilla
        // vocabulary, the same in every load order. It fills what the engine
        // values above cannot see - a summon has no resist value, but vanilla
        // marks Flame Atronach with MagicSummonFire.
        struct VanillaKeywordTraits
        {
            std::string_view keyword;
            const char* first;
            const char* second;  // may be null
        };

        constexpr VanillaKeywordTraits kVanillaKeywordTraits[] = {
            { "MagicDamageFire", "element.fire", nullptr },
            { "MagicDamageFrost", "element.frost", nullptr },
            { "MagicDamageShock", "element.shock", nullptr },
            { "MagicSummonFire", "element.fire", "kind.summon" },
            { "MagicSummonFrost", "element.frost", "kind.summon" },
            { "MagicSummonShock", "element.shock", "kind.summon" },
            { "MagicSummonUndead", "kind.undead", "kind.summon" },
            { "MagicSummonFamiliar", "kind.familiar", "kind.summon" },
            { "MagicRune", "kind.rune", nullptr },
            { "MagicCloak", "kind.cloak", nullptr },
            { "MagicWard", "kind.ward", nullptr },
            { "MagicArmorSpell", "kind.armor", nullptr },
            { "MagicRestoreHealth", "kind.heal", nullptr },
            { "MagicTurnUndead", "kind.turnUndead", nullptr },
            { "MagicParalysis", "kind.paralysis", nullptr },
            { "MagicInvisibility", "kind.invisibility", nullptr },
            { "MagicNightEye", "kind.nightEye", nullptr },
            { "MagicTelekinesis", "kind.telekinesis", nullptr },
            { "MagicInfluenceFear", "kind.fear", nullptr },
            { "MagicInfluenceFrenzy", "kind.frenzy", nullptr },
            { "MagicInfluenceCharm", "kind.calm", nullptr },
            { "MagicVampireDrain", "kind.absorb", nullptr },
            { "MagicSlow", "kind.slow", nullptr },
        };

        // Set for the duration of one build: the card line is capped, the full
        // trait list the icon rules read is not.
        thread_local std::size_t t_chipLimit = kMaxChips;

        void AddChip(json& chips, const char* chip)
        {
            if (!chip || chips.size() >= t_chipLimit) return;
            for (const auto& existing : chips) {
                if (existing.get_ref<const std::string&>() == chip) return;
            }
            chips.push_back(chip);
        }

        const char* ElementChip(RE::ActorValue resist)
        {
            switch (resist) {
                case RE::ActorValue::kResistFire: return "element.fire";
                case RE::ActorValue::kResistFrost: return "element.frost";
                case RE::ActorValue::kResistShock: return "element.shock";
                case RE::ActorValue::kPoisonResist: return "element.poison";
                case RE::ActorValue::kResistDisease: return "element.disease";
                default: return nullptr;  // magic resist says nothing about the element
            }
        }

        const char* ProjectileChip(const RE::BGSProjectile* projectile)
        {
            using Type = RE::BGSProjectileData::Type;
            const auto& types = projectile->data.types;
            if (types.any(Type::kMissile) || types.any(Type::kArrow)) return "form.projectile";
            if (types.any(Type::kGrenade)) return "form.lobbed";
            if (types.any(Type::kBeam)) return "form.beam";
            if (types.any(Type::kFlamethrower)) return "form.spray";
            if (types.any(Type::kCone)) return "form.cone";
            return nullptr;
        }

        const char* DeliveryChip(RE::MagicSystem::Delivery delivery)
        {
            switch (delivery) {
                case RE::MagicSystem::Delivery::kSelf: return "form.self";
                case RE::MagicSystem::Delivery::kTouch: return "form.touch";
                case RE::MagicSystem::Delivery::kTargetActor: return "form.target";
                case RE::MagicSystem::Delivery::kTargetLocation: return "form.location";
                default: return nullptr;  // aimed without a projectile has nothing to show
            }
        }

        const char* ArchetypeChip(RE::EffectArchetype archetype)
        {
            using A = RE::EffectArchetype;
            switch (archetype) {
                case A::kSummonCreature: return "kind.summon";
                case A::kBoundWeapon: return "kind.bound";
                case A::kCloak: return "kind.cloak";
                case A::kParalysis: return "kind.paralysis";
                case A::kInvisibility: return "kind.invisibility";
                case A::kLight: return "kind.light";
                case A::kNightEye: return "kind.nightEye";
                case A::kDetectLife: return "kind.detect";
                case A::kTelekinesis: return "kind.telekinesis";
                case A::kReanimate: return "kind.reanimate";
                case A::kSoulTrap: return "kind.soulTrap";
                case A::kTurnUndead: return "kind.turnUndead";
                case A::kCalm: return "kind.calm";
                case A::kFrenzy: return "kind.frenzy";
                case A::kDemoralize: return "kind.fear";
                case A::kRally: return "kind.rally";
                case A::kAbsorb: return "kind.absorb";
                case A::kBanish: return "kind.banish";
                case A::kEtherealize: return "kind.ethereal";
                case A::kSlowTime: return "kind.slowTime";
                case A::kLock: return "kind.lock";
                case A::kOpen: return "kind.open";
                case A::kCommandSummoned: return "kind.command";
                case A::kDisarm: return "kind.disarm";
                case A::kStagger: return "kind.stagger";
                case A::kDispel: return "kind.dispel";
                case A::kGuide: return "kind.guide";
                case A::kGrabActor: return "kind.grab";
                case A::kCureDisease:
                case A::kCurePoison:
                case A::kCureParalysis: return "kind.cure";
                default: return nullptr;
            }
        }

        bool ModifiesActorValue(RE::EffectArchetype archetype)
        {
            using A = RE::EffectArchetype;
            return archetype == A::kValueModifier || archetype == A::kPeakValueModifier ||
                   archetype == A::kDualValueModifier;
        }

        const char* ActorValueChip(const RE::EffectSetting* baseEffect)
        {
            if (!ModifiesActorValue(baseEffect->data.archetype)) return nullptr;

            const bool harmful = baseEffect->IsDetrimental();
            switch (baseEffect->data.primaryAV) {
                case RE::ActorValue::kHealth: return harmful ? "kind.damage" : "kind.heal";
                case RE::ActorValue::kDamageResist: return harmful ? nullptr : "kind.armor";
                case RE::ActorValue::kWardPower: return harmful ? nullptr : "kind.ward";
                default: return nullptr;
            }
        }

        const char* SchoolChip(RE::ActorValue school)
        {
            switch (school) {
                case RE::ActorValue::kAlteration: return "school.alteration";
                case RE::ActorValue::kConjuration: return "school.conjuration";
                case RE::ActorValue::kDestruction: return "school.destruction";
                case RE::ActorValue::kIllusion: return "school.illusion";
                case RE::ActorValue::kRestoration: return "school.restoration";
                default: return nullptr;
            }
        }

        bool LeavesHazard(const RE::EffectSetting* baseEffect)
        {
            // Only hazards the spell is built around. Impact data hazards are
            // surface decoration and would put this chip on every firebolt.
            const auto& data = baseEffect->data;
            if (data.archetype == RE::EffectArchetype::kSpawnHazard) return true;
            return data.associatedForm && data.associatedForm->Is(RE::FormType::Hazard);
        }
    }

    namespace
    {
        // The effects the chips are read from. Mods hang helper effects on a
        // spell - screen shake, perk staggers, script controllers - and build
        // them from whatever MGEF was at hand, so vanilla Fire Storm ends up
        // carrying a hidden "ScreenShake" that resists frost. The record itself
        // says which effects are not for the player: the Hide in UI flag. Those
        // are skipped, unless that would leave nothing to read.
        std::vector<const RE::Effect*> PlayerFacingEffects(RE::SpellItem* spell)
        {
            using Flag = RE::EffectSetting::EffectSettingData::Flag;

            std::vector<const RE::Effect*> shown;
            std::vector<const RE::Effect*> all;
            for (const auto* effect : spell->effects) {
                if (!effect || !effect->baseEffect) continue;
                all.push_back(effect);
                if (!effect->baseEffect->data.flags.any(Flag::kHideInUI)) {
                    shown.push_back(effect);
                }
            }
            return shown.empty() ? all : shown;
        }
    }

    namespace
    {
        json BuildChipsImpl(RE::SpellItem* spell, std::size_t limit);
    }

    json BuildSpellChips(RE::SpellItem* spell)
    {
        return BuildChipsImpl(spell, kMaxChips);
    }

    json BuildSpellTraits(RE::SpellItem* spell)
    {
        return BuildChipsImpl(spell, std::numeric_limits<std::size_t>::max());
    }

    namespace
    {
    json BuildChipsImpl(RE::SpellItem* spell, std::size_t limit)
    {
        json chips = json::array();
        if (!spell) return chips;

        t_chipLimit = limit;

        const auto effects = PlayerFacingEffects(spell);

        // Traits the vanilla keywords give, gathered once and placed below.
        std::vector<const char*> keywordElements;
        std::vector<const char*> keywordKinds;
        for (const auto* effect : effects) {
            for (const auto* keyword : effect->baseEffect->GetKeywords()) {
                if (!keyword || !IsVanillaKeyword(keyword)) continue;
                const char* editorId = keyword->GetFormEditorID();
                if (!editorId) continue;

                for (const auto& entry : kVanillaKeywordTraits) {
                    if (entry.keyword != editorId) continue;
                    for (const char* trait : { entry.first, entry.second }) {
                        if (!trait) continue;
                        const bool isElement = std::string_view(trait).starts_with("element.");
                        (isElement ? keywordElements : keywordKinds).push_back(trait);
                    }
                }
            }
        }

        // Elements first: they are what a player sorts spells by.
        for (const auto* effect : effects) {
            AddChip(chips, ElementChip(effect->baseEffect->data.resistVariable));
        }
        for (const char* element : keywordElements) {
            AddChip(chips, element);
        }

        // How it leaves the hand: the first projectile, otherwise the delivery.
        const char* formChip = nullptr;
        bool blast = false;
        bool hazard = false;
        for (const auto* effect : effects) {
            const auto& data = effect->baseEffect->data;

            if (!formChip && data.projectileBase) {
                formChip = ProjectileChip(data.projectileBase);
            }
            if (effect->effectItem.area > 0 || data.explosion ||
                (data.projectileBase && data.projectileBase->data.explosionType)) {
                blast = true;
            }
            if (LeavesHazard(effect->baseEffect)) {
                hazard = true;
            }
        }
        AddChip(chips, formChip ? formChip : DeliveryChip(spell->data.delivery));
        if (hazard) AddChip(chips, "area.hazard");
        if (blast) AddChip(chips, "area.blast");

        // What it does. An element already says "this hurts", so plain damage
        // only shows when there is no element to say it.
        const bool hasElement = !chips.empty() &&
            chips[0].get_ref<const std::string&>().starts_with("element.");
        for (const auto* effect : effects) {
            AddChip(chips, ArchetypeChip(effect->baseEffect->data.archetype));

            const char* valueChip = ActorValueChip(effect->baseEffect);
            if (valueChip && !(hasElement && std::string_view(valueChip) == "kind.damage")) {
                AddChip(chips, valueChip);
            }
        }

        for (const char* kind : keywordKinds) {
            AddChip(chips, kind);
        }

        if (spell->data.castingType == RE::MagicSystem::CastingType::kConcentration) {
            AddChip(chips, "cast.concentration");
        }
        if (spell->IsTwoHanded()) {
            AddChip(chips, "cast.twoHanded");
        }

        // The school always makes it onto the line, at the cost of the last chip.
        if (const char* schoolChip = SchoolChip(GetSpellSchool(spell))) {
            if (chips.size() >= limit) {
                chips.erase(chips.size() - 1);
            }
            chips.push_back(schoolChip);
        }

        return chips;
    }
    }
}
