#include "librarian/Librarian.h"
#include "librarian/LibrarianInternal.h"

#include <algorithm>

// =============================================================================
// LibrarianClassify - matching rules against one scanned spell
//
// Pure: reads the spell object the scanner produced, writes tags. No game
// state, no file access, so the plugin and the offline harness share it.
// =============================================================================

namespace Librarian
{
    namespace
    {
        bool StartsWith(const std::string& text, const std::string& prefix)
        {
            return text.size() >= prefix.size()
                && text.compare(0, prefix.size(), prefix) == 0;
        }

        bool EndsWith(const std::string& text, const std::string& suffix)
        {
            return text.size() >= suffix.size()
                && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        using Detail::ReadField;

        // A missing boolean cannot satisfy a rule that asks about it: the
        // narrower presets leave these fields out entirely.
        bool BoolFieldMatches(const json& object, const char* key, const std::optional<bool>& wanted)
        {
            if (!wanted.has_value()) {
                return true;
            }
            const auto found = object.find(key);
            return found != object.end()
                && found->is_boolean()
                && found->get<bool>() == *wanted;
        }

        bool HasKeyword(const json& owner, const std::string& keyword)
        {
            const auto keywords = owner.find("keywords");
            if (keywords == owner.end() || !keywords->is_array()) {
                return false;
            }

            for (const auto& entry : *keywords) {
                if (entry.is_string() && entry.get<std::string>() == keyword) {
                    return true;
                }
            }
            return false;
        }

        // Prefix and suffix describe one keyword, not two: ADAR_SPEL_Earth_Rune
        // is earth by its prefix and a trap by its suffix, and a rule naming
        // both wants both on that same keyword. An empty affix is not a
        // condition. Returns true when no affix was asked for at all.
        bool HasKeywordWithAffixes(const json& owner, const std::string& prefix,
            const std::string& suffix)
        {
            if (prefix.empty() && suffix.empty()) {
                return true;
            }

            const auto keywords = owner.find("keywords");
            if (keywords == owner.end() || !keywords->is_array()) {
                return false;
            }

            for (const auto& entry : *keywords) {
                if (!entry.is_string()) {
                    continue;
                }
                const std::string keyword = entry.get<std::string>();
                if ((prefix.empty() || StartsWith(keyword, prefix))
                    && (suffix.empty() || EndsWith(keyword, suffix))) {
                    return true;
                }
            }
            return false;
        }


        // Archetypes that change a value: the ones a hidden effect can deal real
        // harm through.
        constexpr std::string_view kHarmArchetypes[] = {
            "ValueModifier", "DualValueModifier", "PeakValueModifier", "Absorb",
        };

        // Real harm: detrimental, a magnitude, and an archetype that changes a
        // value. The test for a hidden effect that still counts as evidence, and
        // for the damage a cloak-like carrier applies (a heal it also applies
        // is not damage).
        bool DealsRealHarm(const json& effect)
        {
            const auto detrimental = effect.find("detrimental");
            if (detrimental == effect.end() || !detrimental->is_boolean() || !detrimental->get<bool>()) return false;
            const auto magnitude = effect.find("magnitude");
            if (magnitude == effect.end() || !magnitude->is_number() || magnitude->get<double>() <= 0) return false;
            const std::string archetype = ReadField(effect, "archetype");
            return std::find(std::begin(kHarmArchetypes), std::end(kHarmArchetypes), archetype) != std::end(kHarmArchetypes);
        }

        // A visible effect always counts. A hidden one (hideInUI) counts only
        // when it deals real harm - detrimental, a magnitude, a value it changes.
        // Hidden effects are mostly a perk's or a mod's helpers riding in the
        // spell: a screen shake that carries FrostResist, a stagger push, a perk
        // dummy; tagging from those put frost on Bane of the Undead. A spell that
        // hides its real damage (a venom, a frost stream) still counts.
        bool CountsAsEvidence(const json& effect)
        {
            const auto flags = effect.find("flags");
            const bool hidden = flags != effect.end() && flags->is_object()
                && flags->contains("hideInUI") && (*flags)["hideInUI"].is_boolean()
                && (*flags)["hideInUI"].get<bool>();
            return !hidden || DealsRealHarm(effect);
        }

        // A spell whose effects are all hidden (Mysticism's circles, a summon
        // that shows only its description) has nothing else to go on: then
        // every effect counts. Worked out once per spell, not per rule.
        bool EvidenceIsSifted(const json& spell)
        {
            const auto effects = spell.find("effects");
            if (effects == spell.end() || !effects->is_array()) return false;
            return std::any_of(effects->begin(), effects->end(),
                [](const json& effect) { return effect.is_object() && CountsAsEvidence(effect); });
        }

        constexpr std::size_t kNoParent = static_cast<std::size_t>(-1);

        // Every effect that counts as evidence, as visit(effect, id, parent):
        // the spell's own effects (sifted when EvidenceIsSifted) and what each
        // one applies to others (a cloak's, hazard's or explosion's own
        // effects), each with an id of its own in walk order and its carrier's
        // id as parent (kNoParent at the top). visit returns true to stop. The
        // rules and the conflict stage both walk effects through this, so the
        // ids agree.
        template <class Visit>
        void ForEachEvidenceEffect(const json& spell, bool sifted, Visit&& visit)
        {
            const auto effects = spell.find("effects");
            if (effects == spell.end() || !effects->is_array()) return;
            std::size_t next = 0;
            for (const auto& effect : *effects) {
                if (!effect.is_object()) continue;
                // A helper hidden in the spell is no evidence, and neither is
                // what it sets off (a stagger's explosion)
                if (sifted && !CountsAsEvidence(effect)) continue;
                const std::size_t carrier = next++;
                if (visit(effect, carrier, kNoParent)) return;
                // What a cloak, hazard or explosion puts on others is the
                // spell's real work, whatever its own record says
                const auto applies = effect.find("applies");
                if (applies == effect.end() || !applies->is_array()) continue;
                for (const auto& applied : *applies) {
                    const auto appliedEffects = applied.find("effects");
                    if (appliedEffects == applied.end() || !appliedEffects->is_array()) continue;
                    for (const auto& appliedEffect : *appliedEffects) {
                        if (!appliedEffect.is_object() || !CountsAsEvidence(appliedEffect)) continue;
                        if (visit(appliedEffect, next++, carrier)) return;
                    }
                }
            }
        }

        // Whether the rule holds for the spell. With `effectIds`, the id of
        // every effect it holds on is collected (none for a rule with no effect
        // condition: spell-level evidence); without, the first match settles it.
        bool SpellMatches(const json& spell, const RuleMatch& match, bool sifted,
            std::vector<std::size_t>* effectIds = nullptr)
        {
            if (!match.spells.empty()) {
                const std::string id = ReadField(spell, "persistentId");
                if (id.empty() || std::find(match.spells.begin(), match.spells.end(), id) == match.spells.end()) {
                    return false;
                }
            }
            if (!match.pluginContains.empty()) {
                // A scan without the plugin column still has it in the persistentId
                std::string plugin = ReadField(spell, "plugin");
                if (plugin.empty()) {
                    const std::string id = ReadField(spell, "persistentId");
                    plugin = id.substr(0, id.find('|'));
                }
                plugin = Detail::Lowered(plugin);
                const bool named = std::any_of(match.pluginContains.begin(), match.pluginContains.end(),
                    [&plugin](const std::string& part) { return plugin.find(part) != std::string::npos; });
                if (!named) return false;
            }
            if (match.castByVampires.has_value()) {
                const auto flag = spell.find("castByVampires");
                const bool cast = flag != spell.end() && flag->is_boolean() && flag->get<bool>();
                if (cast != *match.castByVampires) return false;
            }
            if (!match.spellKeyword.empty() && !HasKeyword(spell, match.spellKeyword)) {
                return false;
            }
            if (!HasKeywordWithAffixes(spell, match.spellKeywordPrefix, match.spellKeywordSuffix)) {
                return false;
            }

            if (!match.HasEffectCondition()) {
                return true;
            }

            // The effect conditions describe one effect, not a spell wide
            // union: an archetype from one effect and a resistance from another
            // are not evidence that either of them is what the rule describes.
            bool matched = false;
            ForEachEvidenceEffect(spell, sifted, [&](const json& effect, std::size_t id, std::size_t) {
                if (!EffectMatches(effect, match)) return false;
                matched = true;
                if (!effectIds) return true;
                effectIds->push_back(id);
                return false;
            });
            return matched;
        }

        // One rule's word for one tag: its tier, its place in the merged rule
        // list (ties between equal tiers go to the earlier rule) and the
        // effects it matched on (none for a spell-level rule).
        struct Evidence
        {
            std::string tier;
            std::size_t order = 0;
            std::vector<std::size_t> effects;
        };
        using EvidenceMap = std::map<std::string, std::vector<Evidence>>;

        void AddEvidence(const std::vector<std::string>& tags, const Rule& rule, std::size_t order,
            const std::vector<std::size_t>& effects, const std::set<std::string>& removed,
            std::set<std::string>& axis, EvidenceMap& evidence)
        {
            for (const auto& tag : tags) {
                if (removed.contains(tag)) continue;  // a removal wins over every add
                axis.insert(tag);
                evidence[tag].push_back({ rule.source, order, effects });
            }
        }

        void RemoveTags(const std::set<std::string>& tags, std::set<std::string>& axis, EvidenceMap& evidence)
        {
            for (const auto& tag : tags) {
                axis.erase(tag);
                evidence.erase(tag);
            }
        }

        // Ranks first, then rule order: whether `a` beats `b`.
        bool Stronger(const Evidence& a, const Evidence& b)
        {
            const int rankA = TierRank(a.tier);
            const int rankB = TierRank(b.tier);
            return rankA != rankB ? rankA > rankB : a.order < b.order;
        }

        // The strongest tier behind each tag, and behind the axis as a whole
        // (perTag may be null when only the axis is wanted).
        void SettleSources(const EvidenceMap& evidence, std::map<std::string, std::string>* perTag,
            std::string& axisSource)
        {
            if (perTag) perTag->clear();
            axisSource.clear();
            const Evidence* axisBest = nullptr;
            for (const auto& [tag, pieces] : evidence) {
                const Evidence* best = nullptr;
                for (const auto& piece : pieces) {
                    if (!best || Stronger(piece, *best)) best = &piece;
                }
                if (!best) continue;
                if (perTag) (*perTag)[tag] = best->tier;
                if (!axisBest || Stronger(*best, *axisBest)) axisBest = best;
            }
            if (axisBest) axisSource = axisBest->tier;
        }

        bool IsRecordTier(const std::string& tier)
        {
            return TierRank(tier) >= TIER_RANK_RECORD;
        }

        // What the conflict stage knows of each evidence effect, by id.
        struct EffectNode
        {
            bool unspecified = false;              // carries an unspecified-damage mark
            bool carrier = false;                  // a cloak, hazard or explosion body: deals no damage itself
            std::vector<std::size_t> damageChildren;  // its applied effects that do
        };

        std::vector<EffectNode> MapEffects(const json& spell, bool sifted, const RuleSet& rules)
        {
            std::vector<EffectNode> nodes;
            std::vector<bool> harm;
            ForEachEvidenceEffect(spell, sifted, [&](const json& effect, std::size_t id, std::size_t parent) {
                nodes.resize(id + 1);
                harm.resize(id + 1);
                harm[id] = DealsRealHarm(effect);
                nodes[id].unspecified = std::any_of(rules.unspecifiedDamageKeywords.begin(),
                    rules.unspecifiedDamageKeywords.end(), [&](const std::string& keyword) { return HasKeyword(effect, keyword); });
                if (parent != kNoParent && harm[id]) nodes[parent].damageChildren.push_back(id);
                return false;
            });
            for (std::size_t id = 0; id < nodes.size(); ++id) {
                nodes[id].carrier = !harm[id] && !nodes[id].damageChildren.empty();
            }
            return nodes;
        }

        // Whether the record of this one effect speaks against `element`: it
        // carries an unspecified-damage mark, or a record-tier rule gave this
        // same effect a different engine damage element. Sibling effects say
        // nothing about each other.
        bool EffectContradicts(std::size_t id, const std::string& element,
            const std::vector<EffectNode>& nodes, const EvidenceMap& evidence)
        {
            if (id < nodes.size() && nodes[id].unspecified) return true;
            for (const auto& [other, pieces] : evidence) {
                if (other == element || !IsEngineDamageElement(other)) continue;
                for (const auto& piece : pieces) {
                    if (IsRecordTier(piece.tier)
                        && std::find(piece.effects.begin(), piece.effects.end(), id) != piece.effects.end()) {
                        return true;
                    }
                }
            }
            return false;
        }

        // A keyword on an effect is contradicted by that effect's own record;
        // on a carrier that deals no damage itself (a cloak body), by the
        // damage it applies - every damaging effect it applies must say so.
        bool Contradicted(std::size_t id, const std::string& element,
            const std::vector<EffectNode>& nodes, const EvidenceMap& evidence)
        {
            if (EffectContradicts(id, element, nodes, evidence)) return true;
            if (id >= nodes.size() || !nodes[id].carrier) return false;
            const auto& children = nodes[id].damageChildren;
            return std::all_of(children.begin(), children.end(),
                [&](std::size_t child) { return EffectContradicts(child, element, nodes, evidence); });
        }

        // The conflict stage, after every add and remove: for an engine damage
        // element, every piece of evidence below the record tiers whose every
        // effect is contradicted is taken away, and the element goes when no
        // evidence is left. Record evidence, spell-level evidence and pieces on
        // an effect the record does not contradict stay. Returns what went.
        std::vector<std::string> DropContradicted(const json& spell, bool sifted, const RuleSet& rules,
            EvidenceMap& evidence, std::set<std::string>& elements)
        {
            const std::vector<EffectNode> nodes = MapEffects(spell, sifted, rules);
            std::map<std::string, std::vector<std::size_t>> contradictedPieces;
            for (const auto& [element, pieces] : evidence) {
                if (!IsEngineDamageElement(element)) continue;
                for (std::size_t i = 0; i < pieces.size(); ++i) {
                    const Evidence& piece = pieces[i];
                    if (IsRecordTier(piece.tier) || piece.effects.empty()) continue;
                    const bool all = std::all_of(piece.effects.begin(), piece.effects.end(),
                        [&](std::size_t id) { return Contradicted(id, element, nodes, evidence); });
                    if (all) contradictedPieces[element].push_back(i);
                }
            }
            // Judged against the evidence as it stood, then taken away
            std::vector<std::string> dropped;
            for (const auto& [element, indices] : contradictedPieces) {
                auto& pieces = evidence[element];
                for (auto it = indices.rbegin(); it != indices.rend(); ++it) pieces.erase(pieces.begin() + *it);
                if (pieces.empty()) {
                    evidence.erase(element);
                    elements.erase(element);
                    dropped.push_back(element);
                }
            }
            return dropped;
        }
    }

    int TierRank(const std::string& tier)
    {
        if (tier == SOURCE_MGEF) return TIER_RANK_MGEF;
        if (tier == SOURCE_MANUAL) return TIER_RANK_MANUAL;
        if (tier == SOURCE_FRAMEWORK) return TIER_RANK_FRAMEWORK;
        return TIER_RANK_UNKNOWN;
    }

    bool IsEngineDamageElement(std::string_view element)
    {
        return std::find(std::begin(ENGINE_DAMAGE_ELEMENTS), std::end(ENGINE_DAMAGE_ELEMENTS), element)
            != std::end(ENGINE_DAMAGE_ELEMENTS);
    }

    // Every effect level condition the rule names, against one effect. Public
    // because the keyword patch matches adapter conditions the same way.
    bool EffectMatches(const json& effect, const RuleMatch& match)
    {
        if (!match.mgefKeyword.empty() && !HasKeyword(effect, match.mgefKeyword)) {
            return false;
        }
        if (!HasKeywordWithAffixes(effect, match.mgefKeywordPrefix, match.mgefKeywordSuffix)) {
            return false;
        }
        if (!match.archetype.empty() && ReadField(effect, "archetype") != match.archetype) {
            return false;
        }
        if (!match.primaryAV.empty() && ReadField(effect, "primaryAV") != match.primaryAV) {
            return false;
        }
        if (!match.secondaryAV.empty() && ReadField(effect, "secondaryAV") != match.secondaryAV) {
            return false;
        }
        if (!match.resistance.empty() && ReadField(effect, "resistance") != match.resistance) {
            return false;
        }
        if (!match.magicSkill.empty() && ReadField(effect, "magicSkill") != match.magicSkill) {
            return false;
        }
        if (!BoolFieldMatches(effect, "hostile", match.hostile)) {
            return false;
        }
        if (!BoolFieldMatches(effect, "detrimental", match.detrimental)) {
            return false;
        }
        return Detail::SummonedMatches(effect, match.summonedAny, match.summonedNone)
            && Detail::TargetRequires(effect, match.targetRequires);
    }

    TagSet Classify(const json& spell, const RuleSet& rules)
    {
        TagSet tags;

        if (!spell.is_object()) {
            return tags;
        }

        // Removals wait until every rule has added: a rule taking a tag off
        // must win whatever file order put the rule that added it
        const bool sifted = EvidenceIsSifted(spell);
        const std::set<std::string> none;
        std::set<std::string> removeElements;
        std::set<std::string> removeTechniques;
        EvidenceMap elementEvidence;
        EvidenceMap techniqueEvidence;
        std::vector<std::size_t> effectIds;

        for (std::size_t order = 0; order < rules.rules.size(); ++order) {
            const Rule& rule = rules.rules[order];
            if (rule.match.noElement) {
                continue;  // waits for the last pass
            }
            effectIds.clear();
            if (!SpellMatches(spell, rule.match, sifted, &effectIds)) {
                continue;
            }

            removeElements.insert(rule.removeElements.begin(), rule.removeElements.end());
            removeTechniques.insert(rule.removeTechniques.begin(), rule.removeTechniques.end());
            AddEvidence(rule.addElements, rule, order, effectIds, none, tags.elements, elementEvidence);
            AddEvidence(rule.addTechniques, rule, order, effectIds, none, tags.techniques, techniqueEvidence);
        }

        RemoveTags(removeElements, tags.elements, elementEvidence);
        RemoveTags(removeTechniques, tags.techniques, techniqueEvidence);

        // A framework keyword the spell's own record speaks against
        tags.conflictDropped = DropContradicted(spell, sifted, rules, elementEvidence, tags.elements);

        // Rules for spells left without an element, once all else is settled
        // (the conflict stage too: a spell it emptied is a spell with no
        // element). The first that adds an element settles it: a second must
        // not stack. A tag a rule removed stays removed here too.
        if (tags.elements.empty()) {
            for (std::size_t order = 0; order < rules.rules.size(); ++order) {
                const Rule& rule = rules.rules[order];
                if (!tags.elements.empty()) break;
                effectIds.clear();
                if (!rule.match.noElement || !SpellMatches(spell, rule.match, sifted, &effectIds)) continue;
                AddEvidence(rule.addElements, rule, order, effectIds, removeElements, tags.elements, elementEvidence);
                AddEvidence(rule.addTechniques, rule, order, effectIds, removeTechniques, tags.techniques,
                    techniqueEvidence);
            }
        }

        SettleSources(elementEvidence, &tags.elementSources, tags.elementSource);
        SettleSources(techniqueEvidence, nullptr, tags.techniqueSource);

        return tags;
    }
}
