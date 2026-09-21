#include "treebuilder/TreeBuilderInternal.h"

#include <algorithm>
#include <cmath>
#include <map>

// =============================================================================
// CROSS SCHOOL BRIDGES
// =============================================================================
//
// Every builder splits the spells by school first and grows five trees that
// never hear of each other, so Fireball and Conjure Flame Atronach - both fire -
// are never even compared. A bridge is a link between two such spells: a fire
// mage working up Destruction finds a way into the fire summons.
//
// A bridge is an EXTRA way in, never a requirement. The layout adds the source
// to the target's soft prerequisites, which already mean "any one of these", so
// the target still opens through its own school exactly as before. That is why
// bridges are returned as data next to the trees and not written into them: a
// school's tree must stay complete without them.
//
// Which spells: the ones that share what they ARE. Only telling keywords count -
// elements, kinds, the actor value, id words - weighed by how few spells carry
// them (inverse document frequency over the whole load order). How a spell is
// delivered does not count at all: two projectiles are not kin, nor two cloaks.
//
// Id words need two more checks that traits do not:
//  - A word only one plugin uses is that author's shorthand ("styy", "dcd"),
//    and being rare it would outweigh everything else and bridge a mod to itself.
//    A word counts once two plugins use it.
//  - A word that repeats a trait the spell already has ("frost" next to
//    element.frost) would count the same fact twice.

namespace
{
    // A shared form, area or casting style says how a spell is used, not what it is.
    constexpr std::string_view kShapePrefixes[] = { "form.", "area.", "cast." };
    constexpr std::string_view kTooBroad = "kind.damage";
    constexpr std::string_view kWordPrefix = "word.";
    constexpr std::size_t kMinPluginsPerWord = 2;

    // Weighted share of keywords two spells must have in common. Low enough that
    // "fire" alone bridges a plain fire spell to a fire summon, high enough that
    // one incidental word among many does not.
    constexpr float kMinAffinity = 0.34f;

    // The source is what you learn first: same tier or the one below.
    constexpr int kMaxTierGap = 1;

    constexpr std::size_t kBridgesPerSchoolPair = 12;  // keeps the picture readable
    constexpr std::size_t kMaxBridgesFromOneSpell = 2; // no single spell opens half a school

    // One family (a mod's dozen "detect" spells, all sharing the same three
    // keywords) would otherwise take every place between two schools.
    constexpr std::size_t kMaxBridgesPerSharedSet = 3;

    struct Entry
    {
        std::string formId;
        std::string school;
        int tier = 0;
        std::vector<int> keywords;  // sorted ids
        float weight = 0.0f;
    };

    struct Candidate
    {
        std::size_t from = 0;
        std::size_t to = 0;
        float affinity = 0.0f;
        float evidence = 0.0f;  // summed weight of what is shared
        std::vector<int> shared;
    };

    bool IsTelling(const std::string& keyword)
    {
        if (keyword == kTooBroad || TreeBuilder::IsShapeKind(keyword)) return false;
        for (const auto prefix : kShapePrefixes) {
            if (keyword.starts_with(prefix)) return false;
        }
        return true;
    }

    std::string_view AfterDot(std::string_view keyword)
    {
        const auto dot = keyword.find('.');
        return dot == std::string_view::npos ? keyword : keyword.substr(dot + 1);
    }

    // "word.frost" next to "element.frost" - every trait counts, shape or not,
    // so that "word.cloak" goes the same way as kind.cloak.
    bool RepeatsATrait(const std::string& word, const std::vector<std::string>& keywords)
    {
        const auto said = AfterDot(word);
        return std::any_of(keywords.begin(), keywords.end(), [&](const std::string& other) {
            return !other.starts_with(kWordPrefix) && AfterDot(other) == said;
        });
    }
}

json TreeBuilder::ComputeCrossSchoolBridges(const std::vector<json>& spells)
{
    const auto modTags = FindModTags(spells);

    // Which plugins use each id word
    std::unordered_map<std::string, std::unordered_set<std::string>> pluginsOfWord;
    for (const auto& spell : spells) {
        const auto plugin = spell.value("plugin", std::string(""));
        for (const auto& keyword : SpellKeywords(spell, modTags)) {
            if (keyword.starts_with(kWordPrefix)) pluginsOfWord[keyword].insert(plugin);
        }
    }

    // Keyword ids and how many spells carry each
    std::unordered_map<std::string, int> keywordIds;
    std::vector<std::string> keywordNames;
    std::vector<int> spellsWithKeyword;
    std::vector<Entry> entries;

    for (const auto& spell : spells) {
        Entry entry;
        entry.formId = spell.value("formId", std::string(""));
        entry.school = spell.value("school", std::string(""));
        if (entry.formId.empty() || entry.school.empty()) continue;
        entry.tier = std::max(0, TierIndex(spell.value("skillLevel", std::string(""))));

        const auto all = SpellKeywords(spell, modTags);
        for (const auto& keyword : all) {
            if (!IsTelling(keyword)) continue;
            if (keyword.starts_with(kWordPrefix) &&
                (pluginsOfWord[keyword].size() < kMinPluginsPerWord || RepeatsATrait(keyword, all))) continue;
            const auto [it, isNew] = keywordIds.try_emplace(keyword, static_cast<int>(keywordIds.size()));
            if (isNew) {
                keywordNames.push_back(keyword);
                spellsWithKeyword.push_back(0);
            }
            spellsWithKeyword[it->second]++;
            entry.keywords.push_back(it->second);
        }
        std::sort(entry.keywords.begin(), entry.keywords.end());
        entries.push_back(std::move(entry));
    }

    const auto total = static_cast<float>(entries.size());
    std::vector<float> weights(spellsWithKeyword.size(), 0.0f);
    for (std::size_t k = 0; k < weights.size(); ++k) {
        weights[k] = std::log((total + 1.0f) / (static_cast<float>(spellsWithKeyword[k]) + 1.0f));
    }
    for (auto& entry : entries) {
        for (const int id : entry.keywords) entry.weight += weights[id];
    }

    // Best source for every target, per pair of schools
    std::map<std::pair<std::string, std::string>, std::vector<Candidate>> byPair;
    for (std::size_t to = 0; to < entries.size(); ++to) {
        const auto& target = entries[to];
        if (target.keywords.empty()) continue;

        std::map<std::string, Candidate> bestPerSchool;
        for (std::size_t from = 0; from < entries.size(); ++from) {
            const auto& source = entries[from];
            if (source.school == target.school || source.keywords.empty()) continue;
            if (source.tier > target.tier || target.tier - source.tier > kMaxTierGap) continue;

            std::vector<int> shared;
            std::set_intersection(source.keywords.begin(), source.keywords.end(),
                target.keywords.begin(), target.keywords.end(), std::back_inserter(shared));
            if (shared.empty()) continue;

            float sharedWeight = 0.0f;
            for (const int id : shared) sharedWeight += weights[id];
            const float unionWeight = source.weight + target.weight - sharedWeight;
            const float affinity = unionWeight > 0.0f ? sharedWeight / unionWeight : 0.0f;
            if (affinity < kMinAffinity) continue;

            auto& best = bestPerSchool[source.school];
            const bool better = affinity > best.affinity ||
                (affinity == best.affinity && !best.shared.empty() && source.formId < entries[best.from].formId);
            if (best.shared.empty() || better) {
                best = Candidate{ from, to, affinity, sharedWeight, std::move(shared) };
            }
        }

        for (auto& [sourceSchool, candidate] : bestPerSchool) {
            byPair[{ sourceSchool, target.school }].push_back(std::move(candidate));
        }
    }

    // Most evidence first within each pair. Not closest first: a spell with a
    // single keyword is a perfect match for anything that has it, and would
    // crowd out pairs that share three.
    // Two spells of one tier often pick each other; that is one bridge, open both ways.
    json bridges = json::array();
    std::unordered_map<std::size_t, std::size_t> usesOfSource;
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> emitted;
    for (auto& [pair, candidates] : byPair) {
        std::sort(candidates.begin(), candidates.end(), [&entries](const Candidate& a, const Candidate& b) {
            if (a.evidence != b.evidence) return a.evidence > b.evidence;
            if (a.affinity != b.affinity) return a.affinity > b.affinity;
            return entries[a.to].formId < entries[b.to].formId;
        });

        std::size_t taken = 0;
        std::map<std::vector<int>, std::size_t> usesOfSharedSet;
        for (const auto& candidate : candidates) {
            if (taken >= kBridgesPerSchoolPair) break;
            if (const auto back = emitted.find({ candidate.to, candidate.from }); back != emitted.end()) {
                bridges[back->second]["twoWay"] = true;
                continue;
            }
            if (usesOfSource[candidate.from] >= kMaxBridgesFromOneSpell) continue;
            if (usesOfSharedSet[candidate.shared] >= kMaxBridgesPerSharedSet) continue;
            usesOfSharedSet[candidate.shared]++;
            usesOfSource[candidate.from]++;
            taken++;

            emitted[{ candidate.from, candidate.to }] = bridges.size();

            json shared = json::array();
            for (const int id : candidate.shared) shared.push_back(keywordNames[id]);
            bridges.push_back({
                { "from", entries[candidate.from].formId },
                { "to", entries[candidate.to].formId },
                { "fromSchool", pair.first },
                { "toSchool", pair.second },
                { "affinity", candidate.affinity },
                { "evidence", candidate.evidence },
                { "shared", shared },
                { "twoWay", false },
            });
        }
    }
    return bridges;
}
