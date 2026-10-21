#include "librarian/AdapterMatch.h"
#include "librarian/Librarian.h"
#include "librarian/PerkAdapters.h"

#include <algorithm>
#include <string_view>
#include <utility>

// =============================================================================
// LibrarianAdapterDamage - "damage" keyword lines, planned per effect
//
// Environment mods (Rain Extinguishes Fires, Sneak Tools, DISDD, the OIF rule
// packs ...) and perk mods ask whether the effect that hit carries
// MagicDamageFire or a pack's own fire keyword. A keyword line of the ordinary
// kind writes the first matching visible effect of a spell; a damage line
// writes every effect of the spell that really deals damage of that element,
// and only effects whose every user is such a spell. Pure: no game types, so
// tools/librarian-test runs the same plan. The rules are in PerkAdapters.h,
// "DAMAGE LINES"; docs/librarian/PERK_ADAPTERS.md section 6-5.
// =============================================================================

namespace Librarian::Adapters
{
    using namespace Match;

    namespace
    {
        // The engine's own resistance of each engine damage element
        // (ENGINE_DAMAGE_ELEMENTS): the only resistances that name an element.
        constexpr std::pair<std::string_view, std::string_view> kEngineResistances[] = {
            { "fire", "FireResist" },
            { "frost", "FrostResist" },
            { "shock", "ElectricResist" },
            { "poison", "PoisonResist" },
        };

        // Archetypes that change a value on the target. Absorb moves it to
        // the caster - not damage of an element.
        constexpr std::string_view kDamageArchetypes[] = {
            "ValueModifier", "DualValueModifier", "PeakValueModifier",
        };
        constexpr std::string_view kScriptArchetype = "Script";
        constexpr std::string_view kHealth = "Health";
        // Resistances that say nothing about the element: the engine has no
        // water, air or earth resistance, so a mod's damage of those is
        // resisted by magic resistance or not at all
        constexpr std::string_view kNeutralResistances[] = { "", "None", "MagicResist" };

        std::string_view ElementOfResistance(std::string_view resistance)
        {
            for (const auto& [name, value] : kEngineResistances) {
                if (value == resistance) return name;
            }
            return {};
        }

        bool HasElement(const json& catalogEntry, const std::string& element)
        {
            const auto list = catalogEntry.find("elements");
            if (list == catalogEntry.end() || !list->is_array()) return false;
            return std::ranges::any_of(*list, [&](const json& value) {
                return value.is_string() && value.get<std::string>() == element;
            });
        }

        // The line's elements the spell has at tierMin.
        std::vector<std::string> MatchedElements(const json& catalogEntry, const Line& line, bool& withoutSources)
        {
            std::vector<std::string> matched;
            for (const auto& element : line.damageElements) {
                if (!HasElement(catalogEntry, element)) continue;
                if (line.tierMinRank && ElementRank(catalogEntry, element, withoutSources) < *line.tierMinRank) continue;
                matched.push_back(element);
            }
            return matched;
        }

        // The catalog gives the spell an engine damage element the line does
        // not match: then only an effect resisted as a matched element is
        // known to be the matched element's damage.
        bool IsMixed(const json& catalogEntry, const std::vector<std::string>& matched)
        {
            const auto list = catalogEntry.find("elements");
            if (list == catalogEntry.end() || !list->is_array()) return false;
            return std::ranges::any_of(*list, [&](const json& value) {
                if (!value.is_string()) return false;
                const std::string element = value.get<std::string>();
                return IsEngineDamageElement(element) && !Contains(matched, element);
            });
        }

        // Whether the record of one effect of a picked spell says it deals
        // damage of one of the matched elements - everything but what the
        // spell's own entry sets (magnitude, conditions). PerkAdapters.h,
        // "DAMAGE LINES".
        bool RecordSaysDamageOf(const json& effect, const std::vector<std::string>& matched, bool mixed)
        {
            if (!Flag(effect, "detrimental")) return false;

            const std::string resistance = Field(effect, "resistance");
            const std::string_view named = ElementOfResistance(resistance);
            const bool resistedAsMatched = !named.empty() && Contains(matched, std::string(named));
            // An element the engine has a resistance for is told by the
            // effect's own record. Water, air and the like have none: a
            // visible effect with a neutral resistance stands for them, in a
            // spell no other engine element shares
            const bool neutral = std::ranges::find(kNeutralResistances, std::string_view(resistance)) != std::end(kNeutralResistances);
            const bool resistlessMatched = std::ranges::any_of(matched, [](const std::string& e) { return !IsEngineDamageElement(e); });
            const bool standsForMatched = neutral && resistlessMatched && !mixed && IsVisible(effect);
            if (!resistedAsMatched && !standsForMatched) return false;

            const std::string archetype = Field(effect, "archetype");
            // A hidden script is a helper (a perk's fear or disintegrate check), as in the librarian
            if (archetype == kScriptArchetype) return resistedAsMatched && IsVisible(effect);
            const bool changesValue = std::ranges::find(kDamageArchetypes, std::string_view(archetype)) != std::end(kDamageArchetypes);
            return changesValue && Field(effect, "primaryAV") == kHealth;
        }

        bool HasMagnitude(const json& effect)
        {
            const auto magnitude = effect.find("magnitude");
            return magnitude != effect.end() && magnitude->is_number() && magnitude->get<double>() > 0;
        }

        bool EffectHasKeyword(const json& effect, const std::string& key, const std::string& keyword,
            const EffectKeywords& effectKeywords)
        {
            const auto written = effectKeywords.find(key);
            if (written != effectKeywords.end() && written->second.contains(keyword)) return true;
            const auto keywords = effect.find("keywords");
            if (keywords == effect.end() || !keywords->is_array()) return false;
            return std::ranges::any_of(*keywords, [&](const json& value) {
                return value.is_string() && value.get<std::string>() == keyword;
            });
        }
    }

    void PlanDamageLine(std::size_t fileIndex, std::size_t lineIndex, const Line& line, bool live,
        const PlanSource& source, EffectKeywords& effectKeywords,
        std::unordered_map<std::string, std::unordered_set<std::string>>& carriedCache, Plan& plan)
    {
        LineStats& stats = plan.stats[fileIndex][lineIndex];
        const auto carriedBy = [&](const std::string& id, const json& spell) -> const std::unordered_set<std::string>& {
            auto [slot, fresh] = carriedCache.try_emplace(id);
            if (fresh) slot->second = CarriedKeywords(spell);
            return slot->second;
        };
        static const EffectCondition kAnyEffect;
        const EffectCondition& effectFilter = line.effect ? *line.effect : kAnyEffect;

        // Effects that want the keyword, each with the picked spells using it
        std::unordered_map<std::string, std::vector<std::string>> wanting;
        std::unordered_map<std::string, std::string> labels;   // spell id -> name, for the examples
        // Per picked spell, the effects whose record says the line's damage -
        // also those its entry uses with conditions or magnitude 0: such a
        // user does not stop a write (the effect is that damage for it too)
        std::unordered_map<std::string, std::unordered_set<std::string>> recordUses;
        std::unordered_set<std::string> conditionedKeys;   // lacking the keyword, used with conditions

        source.forEachCatalogSpell([&](const std::string& id, const json& spell, const json& entry) {
            bool withoutSources = false;
            const bool spellMatches = SpellLevelMatch(spell, &entry, line, carriedBy(id, spell), withoutSources);
            const std::vector<std::string> matched = spellMatches ? MatchedElements(entry, line, withoutSources)
                                                                  : std::vector<std::string>{};
            if (withoutSources) ++plan.entriesWithoutSources;
            if (matched.empty()) return;

            const bool mixed = IsMixed(entry, matched);
            bool anyDamage = false;
            bool allCarry = true;
            for (const json& effect : EffectsOf(spell)) {
                if (!effect.is_object() || !RecordSaysDamageOf(effect, matched, mixed) || !EffectMatches(effect, effectFilter)) {
                    continue;
                }
                const std::string key = EffectKey(effect);
                if (key.empty()) continue;
                // An excluding mark counts whether its plugin or an earlier line put it there
                if (std::ranges::any_of(line.damageKeywordsNone,
                        [&](const std::string& mark) { return EffectHasKeyword(effect, key, mark, effectKeywords); })) {
                    continue;
                }
                recordUses[id].insert(key);
                if (!HasMagnitude(effect)) continue;
                const bool carries = EffectHasKeyword(effect, key, line.keyword, effectKeywords);
                // A perk bonus riding in the spell: not this spell's damage to judge it by
                if (HasConditions(effect)) {
                    if (!carries) conditionedKeys.insert(key);
                    continue;
                }
                anyDamage = true;
                if (carries) continue;
                allCarry = false;
                auto& wanters = wanting[key];
                if (wanters.empty() || wanters.back() != id) wanters.push_back(id);
                labels.try_emplace(id, SpellLabel(spell, id));
            }
            if (anyDamage && allCarry) ++stats.agree;
        });
        // Effects only ever used with conditions: never written
        stats.conditioned += std::ranges::count_if(conditionedKeys, [&](const std::string& key) { return !wanting.contains(key); });

        std::unordered_set<std::string> gained;
        for (const auto& [key, wanters] : wanting) {
            // Leak check: every item using the effect must be a spell this line
            // picks whose record view of this very effect is the same damage
            const auto users = source.usersOf(key);
            const EffectUser* blocker = nullptr;
            for (const auto& user : users) {
                if (line.allowSharedEffects) break;   // "sharedEffects": "allow"
                if (user.spell) {
                    const auto uses = recordUses.find(user.id);
                    if (uses != recordUses.end() && uses->second.contains(key)) continue;
                }
                blocker = &user;
                break;
            }
            if (blocker) {
                ++stats.leaks;
                if (!blocker->spell) ++stats.blockedByItem;
                const std::string blockerLabel = blocker->spell ? SpellLabel(*blocker->spell, blocker->id) : blocker->id;
                AddExample(stats.leakExamples, key + " (" + labels[wanters.front()] + ") <- " + blockerLabel);
                continue;
            }

            ++stats.writes;
            if (live) {
                plan.writes.push_back({ fileIndex, lineIndex, line.keyword, key });
                effectKeywords[key].insert(line.keyword);
            }
            for (const auto& user : users) {
                if (!user.spell || gained.contains(user.id)) continue;
                if (carriedBy(user.id, *user.spell).contains(line.keyword)) continue;
                gained.insert(user.id);
                ++stats.gain;
                AddExample(stats.gainExamples, SpellLabel(*user.spell, user.id));
                if (live) carriedCache[user.id].insert(line.keyword);
            }
        }
    }
}
