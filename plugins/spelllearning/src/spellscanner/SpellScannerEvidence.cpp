#include "Common.h"
#include "SpellScanner.h"
#include "librarian/PerkAdapterPatch.h"

#include <set>
#include <string>

// =============================================================================
// STRUCTURE EVIDENCE
// =============================================================================
//
// Everything here is copied straight out of the records: no value is judged,
// merged or guessed. A consumer that wants to decide "this is an area spell"
// does that itself from what is written down. That keeps the scan identical on
// every load order and in every language.

namespace SpellScanner
{
    namespace
    {
        std::string FormRef(const RE::TESForm* form)
        {
            return form ? GetPersistentFormId(form->GetFormID()) : std::string();
        }

        const char* ProjectileTypeName(const RE::BGSProjectileData& data)
        {
            using Type = RE::BGSProjectileData::Type;
            if (data.types.any(Type::kMissile)) return "Missile";
            if (data.types.any(Type::kGrenade)) return "Lobber";
            if (data.types.any(Type::kBeam)) return "Beam";
            if (data.types.any(Type::kFlamethrower)) return "Flame";
            if (data.types.any(Type::kCone)) return "Cone";
            if (data.types.any(Type::kBarrier)) return "Barrier";
            if (data.types.any(Type::kArrow)) return "Arrow";
            return "None";
        }

        // ---------------------------------------------------------------------
        // Hazard presence
        // ---------------------------------------------------------------------
        //
        // Only "is there one", never its radius or lifetime. A hazard hangs off a
        // spell in three places: the effect spawns it outright (SpawnHazard
        // archetype), an explosion drops it as its placed object, or an impact
        // data set carries one per surface material.

        bool ImpactSetHasHazard(const RE::BGSImpactDataSet* impactSet)
        {
            if (!impactSet) return false;
            for (const auto& [material, impact] : impactSet->impactMap) {
                if (impact && impact->hazard) return true;
            }
            return false;
        }

        bool PlacedObjectIsHazard(const RE::BGSExplosion* explosion)
        {
            if (!explosion) return false;

            // The header types the placed object as a reference, but the record
            // stores a base object. Asking any form for its type is safe either way.
            const RE::TESForm* placed = explosion->data.impactPlacedObject;
            return placed && placed->Is(RE::FormType::Hazard);
        }

        // Where the hazard hangs, strongest link first, or nullptr for none.
        // The three are not the same thing: "effect" and "explosion" are hazards
        // the spell is built around (walls, runes), while "impact" is the patch a
        // hit leaves on a surface, which a plain firebolt has too. Which one a
        // consumer cares about is its own call; the scan only says which it saw.
        const char* HazardSource(const RE::EffectSetting* baseEffect)
        {
            const auto& data = baseEffect->data;
            const RE::BGSExplosion* projectileExplosion =
                data.projectileBase ? data.projectileBase->data.explosionType : nullptr;

            if (data.associatedForm && data.associatedForm->Is(RE::FormType::Hazard)) return "effect";

            if (PlacedObjectIsHazard(data.explosion) || PlacedObjectIsHazard(projectileExplosion)) return "explosion";

            if (ImpactSetHasHazard(data.impactDataSet)) return "impact";
            if (data.explosion && ImpactSetHasHazard(data.explosion->data.impactDataSet)) return "impact";
            if (projectileExplosion && ImpactSetHasHazard(projectileExplosion->data.impactDataSet)) return "impact";

            return nullptr;
        }

        json BuildEffectFlagsJson(const RE::EffectSetting* baseEffect)
        {
            using Flag = RE::EffectSetting::EffectSettingData::Flag;
            const auto& flags = baseEffect->data.flags;

            json flagsJson;
            flagsJson["recover"] = flags.any(Flag::kRecover);
            flagsJson["snapToNavMesh"] = flags.any(Flag::kSnapToNavMesh);
            flagsJson["noHitEvent"] = flags.any(Flag::kNoHitEvent);
            flagsJson["dispelWithKeywords"] = flags.any(Flag::kDispelWithKeywords);
            flagsJson["noDuration"] = flags.any(Flag::kNoDuration);
            flagsJson["noMagnitude"] = flags.any(Flag::kNoMagnitude);
            flagsJson["noArea"] = flags.any(Flag::kNoArea);
            flagsJson["fxPersist"] = flags.any(Flag::kFXPersist);
            flagsJson["goryVisuals"] = flags.any(Flag::kGoryVisuals);
            flagsJson["hideInUI"] = flags.any(Flag::kHideInUI);
            flagsJson["noRecast"] = flags.any(Flag::kNoRecast);
            flagsJson["powerAffectsMagnitude"] = flags.any(Flag::kPowerAffectsMagnitude);
            flagsJson["powerAffectsDuration"] = flags.any(Flag::kPowerAffectsDuration);
            flagsJson["painless"] = flags.any(Flag::kPainless);
            flagsJson["noHitEffect"] = flags.any(Flag::kNoHitEffect);
            flagsJson["noDeathDispel"] = flags.any(Flag::kNoDeathDispel);
            return flagsJson;
        }

        json BuildProjectileJson(const RE::BGSProjectile* projectile)
        {
            const auto& data = projectile->data;

            json projectileJson;
            projectileJson["form"] = FormRef(projectile);
            projectileJson["type"] = ProjectileTypeName(data);
            projectileJson["speed"] = data.speed;
            projectileJson["range"] = data.range;
            projectileJson["gravity"] = data.gravity;
            projectileJson["explodes"] = data.flags.any(RE::BGSProjectileData::BGSProjectileFlags::kExplosion);
            return projectileJson;
        }

        json BuildExplosionJson(const RE::BGSExplosion* explosion, const char* source)
        {
            json explosionJson;
            explosionJson["form"] = FormRef(explosion);
            explosionJson["source"] = source;
            explosionJson["radius"] = explosion->data.radius;
            return explosionJson;
        }

        // ---------------------------------------------------------------------
        // What a summon calls up
        // ---------------------------------------------------------------------
        //
        // The race keywords of the actor a summon effect spawns (ActorTypeUndead,
        // ActorTypeDaedra, ActorTypeAnimal ...): what the thing IS, as its race
        // record says. A leveled list contributes every actor it can give, down
        // to a few levels of nesting.

        // ---------------------------------------------------------------------
        // What an effect puts on others
        // ---------------------------------------------------------------------
        //
        // A cloak, a hazard and an explosion do their work through another magic
        // item: the spell a cloak casts on whoever comes near, the spell a hazard
        // puts on whoever stands in it, the enchantment an explosion carries. The
        // effect itself often says nothing of what that work is (a frost cloak's
        // own record can have no frost in it at all). One level only.

        json SummarizeAppliedItem(const RE::MagicItem* item, const char* via)
        {
            json applied;
            applied["via"] = via;
            applied["form"] = FormRef(item);
            json effects = json::array();
            for (const auto* effect : item->effects) {
                if (!effect || !effect->baseEffect) continue;
                const auto* base = effect->baseEffect;
                json summary;
                summary["archetype"] = GetArchetypeName(base->data.archetype);
                summary["primaryAV"] = GetActorValueName(base->data.primaryAV);
                summary["resistance"] = GetActorValueName(base->data.resistVariable);
                summary["detrimental"] = base->IsDetrimental();
                summary["hostile"] = base->IsHostile();
                summary["magnitude"] = effect->effectItem.magnitude;
                summary["duration"] = effect->effectItem.duration;
                // The librarian weighs hidden helpers here as on the spell's own effects
                summary["flags"] = { { "hideInUI",
                    base->data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kHideInUI) } };
                json keywords = json::array();
                for (const auto* keyword : base->GetKeywords()) {
                    const char* editorId = keyword ? keyword->GetFormEditorID() : nullptr;
                    if (!editorId || editorId[0] == '\0') continue;
                    // As the plugins wrote it, like every keyword the scan records
                    if (Librarian::PerkAdapters::IsInjected(base->GetFormID(), keyword->GetFormID())) continue;
                    keywords.push_back(editorId);
                }
                summary["keywords"] = keywords;
                effects.push_back(summary);
            }
            applied["effects"] = effects;
            return applied;
        }

        json BuildAppliesJson(const RE::EffectSetting* baseEffect)
        {
            const auto& data = baseEffect->data;
            json applies = json::array();
            const auto add = [&](const RE::MagicItem* item, const char* via) {
                if (item && !item->effects.empty()) applies.push_back(SummarizeAppliedItem(item, via));
            };
            const auto addHazard = [&](const RE::TESForm* form, const char* via) {
                const auto* hazard = form ? form->As<RE::BGSHazard>() : nullptr;
                if (hazard) add(hazard->data.spell, via);
            };
            const auto addExplosion = [&](const RE::BGSExplosion* explosion) {
                if (!explosion) return;
                add(explosion->formEnchanting, "explosion");
                // Typed as a reference in the header, a base object in the record (see PlacedObjectIsHazard)
                addHazard(explosion->data.impactPlacedObject, "explosionHazard");
            };

            if (data.archetype == RE::EffectArchetype::kCloak && data.associatedForm) {
                add(data.associatedForm->As<RE::MagicItem>(), "cloak");
            }
            addHazard(data.associatedForm, "hazard");
            addExplosion(data.explosion);
            if (data.projectileBase) addExplosion(data.projectileBase->data.explosionType);
            return applies;
        }

        constexpr int kMaxLeveledDepth = 4;

        void CollectSummonedKeywords(RE::TESForm* form, std::set<std::string>& keywords, int depth)
        {
            if (!form || depth > kMaxLeveledDepth) return;

            if (auto* npc = form->As<RE::TESNPC>()) {
                const auto* race = npc->GetRace();
                if (!race) return;
                for (const auto* keyword : race->GetKeywords()) {
                    const char* editorId = keyword ? keyword->GetFormEditorID() : nullptr;
                    if (editorId && editorId[0] != '\0') keywords.insert(editorId);
                }
                return;
            }
            if (auto* leveled = form->As<RE::TESLevCharacter>()) {
                for (const auto& entry : leveled->entries) {
                    CollectSummonedKeywords(entry.form, keywords, depth + 1);
                }
            }
        }
    }

    // =============================================================================
    // MGEF EVIDENCE
    // =============================================================================

    void AppendBaseEffectEvidence(json& effectJson, const RE::EffectSetting* baseEffect)
    {
        const auto& data = baseEffect->data;

        // Which effect record this is. Editor ids repeat across plugins and are
        // empty without po3 Tweaks; the persistent id is the identity.
        effectJson["form"] = FormRef(baseEffect);
        effectJson["flags"] = BuildEffectFlagsJson(baseEffect);
        effectJson["baseCost"] = data.baseCost;
        effectJson["minimumSkill"] = data.minimumSkill;

        if (data.projectileBase) {
            effectJson["projectile"] = BuildProjectileJson(data.projectileBase);
        }

        // The effect's own explosion wins; otherwise the one its projectile sets off.
        if (data.explosion) {
            effectJson["explosion"] = BuildExplosionJson(data.explosion, "effect");
        } else if (data.projectileBase && data.projectileBase->data.explosionType) {
            effectJson["explosion"] = BuildExplosionJson(data.projectileBase->data.explosionType, "projectile");
        }

        const char* hazardSource = HazardSource(baseEffect);
        effectJson["hazard"] = (hazardSource != nullptr);
        if (hazardSource) {
            effectJson["hazardSource"] = hazardSource;
        }

        if (data.perk) {
            effectJson["perk"] = FormRef(data.perk);
        }
        if (data.equipAbility) {
            effectJson["equipAbility"] = FormRef(data.equipAbility);
        }

        json applies = BuildAppliesJson(baseEffect);
        if (!applies.empty()) {
            effectJson["applies"] = applies;
        }

        if (data.archetype == RE::EffectArchetype::kSummonCreature && data.associatedForm) {
            std::set<std::string> keywords;
            CollectSummonedKeywords(data.associatedForm, keywords, 0);
            if (!keywords.empty()) {
                effectJson["summonedKeywords"] = keywords;
            }
        }

    }

    // =============================================================================
    // EFFECT ITEM EVIDENCE
    // =============================================================================

    void AppendEffectItemEvidence(json& effectJson, const RE::Effect* effect, std::size_t index)
    {
        effectJson["index"] = index;
        effectJson["cost"] = effect->cost;

        // Conditions on this spell's use of the effect: a perk bonus ("only
        // with Bastion") rides in the spell as a conditioned effect item.
        std::size_t conditions = 0;
        for (const auto* item = effect->conditions.head; item; item = item->next) {
            ++conditions;
        }
        effectJson["conditions"] = conditions;
    }

    // =============================================================================
    // SPELL EVIDENCE
    // =============================================================================

    void AppendSpellEvidence(json& spellJson, RE::SpellItem* spell)
    {
        using SpellFlag = RE::SpellItem::SpellFlag;
        const auto& data = spell->data;

        // The half cost perk is what the game itself files the spell's tier under.
        if (data.castingPerk) {
            spellJson["castingPerk"] = FormRef(data.castingPerk);
        }

        if (const auto* equipSlot = spell->GetEquipSlot()) {
            spellJson["equipSlot"] = FormRef(equipSlot);
        }
        spellJson["twoHanded"] = spell->IsTwoHanded();

        spellJson["castDuration"] = data.castDuration;
        spellJson["range"] = data.range;

        json flagsJson;
        flagsJson["costOverride"] = data.flags.any(SpellFlag::kCostOverride);
        flagsJson["pcStartSpell"] = data.flags.any(SpellFlag::kPCStartSpell);
        flagsJson["instantCast"] = data.flags.any(SpellFlag::kInstantCast);
        flagsJson["ignoreLOSCheck"] = data.flags.any(SpellFlag::kIgnoreLOSCheck);
        flagsJson["ignoreResistance"] = data.flags.any(SpellFlag::kIgnoreResistance);
        flagsJson["noAbsorb"] = data.flags.any(SpellFlag::kNoAbsorb);
        flagsJson["noDualCastMods"] = data.flags.any(SpellFlag::kNoDualCastMods);
        spellJson["flags"] = flagsJson;
    }
}
