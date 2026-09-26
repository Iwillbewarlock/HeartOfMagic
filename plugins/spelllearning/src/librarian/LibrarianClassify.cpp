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


        bool SpellMatches(const json& spell, const RuleMatch& match)
        {
            if (!match.spells.empty()) {
                const std::string id = ReadField(spell, "persistentId");
                if (id.empty() || std::find(match.spells.begin(), match.spells.end(), id) == match.spells.end()) {
                    return false;
                }
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

            const auto effects = spell.find("effects");
            if (effects == spell.end() || !effects->is_array()) {
                return false;
            }

            // The effect conditions describe one effect, not a spell wide
            // union: an archetype from one effect and a resistance from another
            // are not evidence that either of them is what the rule describes.
            for (const auto& effect : *effects) {
                if (effect.is_object() && EffectMatches(effect, match)) {
                    return true;
                }
            }
            return false;
        }

        // Rules are merged in evidence order, so the first rule to tag an axis
        // is the strongest evidence for it and owns the source.
        void RecordSource(const std::string& ruleSource, std::string& axisSource)
        {
            if (axisSource.empty()) {
                axisSource = ruleSource;
            }
        }
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
        return true;
    }

    TagSet Classify(const json& spell, const RuleSet& rules)
    {
        TagSet tags;

        if (!spell.is_object()) {
            return tags;
        }

        for (const auto& rule : rules.rules) {
            if (!SpellMatches(spell, rule.match)) {
                continue;
            }

            if (!rule.addElements.empty()) {
                tags.elements.insert(rule.addElements.begin(), rule.addElements.end());
                RecordSource(rule.source, tags.elementSource);
            }
            if (!rule.addTechniques.empty()) {
                tags.techniques.insert(rule.addTechniques.begin(), rule.addTechniques.end());
                RecordSource(rule.source, tags.techniqueSource);
            }
        }

        return tags;
    }
}
