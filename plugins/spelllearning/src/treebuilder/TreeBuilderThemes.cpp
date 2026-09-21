#include "treebuilder/TreeBuilderInternal.h"

#include <algorithm>
#include <set>

// =============================================================================
// THEME DISCOVERY
// =============================================================================

const std::unordered_map<std::string, std::vector<std::string>>& TreeBuilder::GetVanillaThemeHints()
{
    static const std::unordered_map<std::string, std::vector<std::string>> hints = {
        {"Destruction", {"fire", "frost", "shock", "cloak", "rune", "wall", "bolt", "storm"}},
        {"Conjuration", {"conjure", "summon", "bound", "atronach", "zombie", "raise", "reanimate", "dremora"}},
        {"Alteration",  {"flesh", "armor", "paralyze", "detect", "light", "transmute", "waterbreathing", "telekinesis"}},
        {"Illusion",    {"fury", "fear", "calm", "courage", "invisibility", "muffle", "frenzy", "pacify"}},
        {"Restoration", {"heal", "healing", "ward", "turn", "undead", "cure", "bane", "circle"}},
    };
    return hints;
}

// =============================================================================
// MOD TAGS
// =============================================================================
//
// Editor ids carry the author's prefix: "ABY_ShadowGrasp", "NAT_WaterSpray",
// "DAR_ArcaneBlast". It says which mod a spell is from, not what the spell is,
// and with a big mod installed it would top the theme count.
//
// A prefix is told by POSITION: the word an id starts with, shared by most of
// that plugin's spells. How often a word appears is no test - a mod about one
// thing (Abyss: shadow, Bloodmoon: blood) uses that word on nearly every spell
// too, and it is exactly the word the branch should be named after. It never
// leads the id, though: the prefix does.

namespace
{
    constexpr std::size_t kMinPluginSpellsForTag = 5;  // too few spells prove nothing
    constexpr float kTagLeadShareInPlugin = 0.8f;      // leads this share of the plugin's ids

    std::string SpellPlugin(const json& spell)
    {
        auto plugin = spell.value("plugin", std::string(""));
        if (plugin.empty()) {
            const auto persistentId = spell.value("persistentId", std::string(""));
            plugin = persistentId.substr(0, persistentId.find('|'));
        }
        return TreeNLP::ToLower(plugin);
    }

    // First word of the editor id, as the tokenizer would see it.
    std::string LeadingIdWord(const json& spell)
    {
        const auto editorId = spell.value("editorId", std::string(""));
        std::string word;
        unsigned char previous = 0;  // as written, before lower-casing
        for (const char c : editorId) {
            const auto current = static_cast<unsigned char>(c);
            if (!std::isalnum(current)) {
                if (word.empty()) continue;  // ids like "_NV_Player_..." start with a separator
                break;
            }
            // camelCase boundary: "madAbsorb" leads with "mad"
            if (!word.empty() && std::isupper(current) && std::islower(previous)) break;
            word += static_cast<char>(std::tolower(current));
            previous = current;
        }
        return word;
    }

    std::unordered_set<std::string> FindModTags(const std::vector<json>& spells)
    {
        std::unordered_map<std::string, std::size_t> pluginSizes;
        std::unordered_map<std::string, std::unordered_map<std::string, std::size_t>> leadsByPlugin;
        for (const auto& spell : spells) {
            const std::string plugin = SpellPlugin(spell);
            const std::string lead = LeadingIdWord(spell);
            if (plugin.empty() || lead.empty()) continue;
            pluginSizes[plugin]++;
            leadsByPlugin[plugin][lead]++;
        }

        std::unordered_set<std::string> tags;
        for (const auto& [plugin, leads] : leadsByPlugin) {
            const std::size_t pluginSize = pluginSizes[plugin];
            if (pluginSize < kMinPluginSpellsForTag) continue;
            for (const auto& [lead, count] : leads) {
                if (static_cast<float>(count) / static_cast<float>(pluginSize) >= kTagLeadShareInPlugin) {
                    tags.insert(lead);
                }
            }
        }
        return tags;
    }

    // "alt50", "ill25", "100": level codes and magnitudes, never a nature.
    bool HasDigit(const std::string& term)
    {
        return std::any_of(term.begin(), term.end(), [](unsigned char c) { return std::isdigit(c); });
    }
}
std::unordered_map<std::string, std::vector<std::string>>
TreeBuilder::DiscoverThemesPerSchool(const std::vector<json>& spells, int topN)
{
    static const std::unordered_set<std::string> VALID_SCHOOLS = {
        "Alteration", "Conjuration", "Destruction", "Illusion", "Restoration"
    };

    // Group spells by school
    std::unordered_map<std::string, std::vector<json>> schoolSpells;
    for (const auto& spell : spells) {
        auto school = spell.value("school", std::string(""));
        if (VALID_SCHOOLS.contains(school)) {
            schoolSpells[school].push_back(spell);
        }
    }

    std::unordered_map<std::string, std::vector<std::string>> result;

    for (const auto& [school, sSpells] : schoolSpells) {
        if (sSpells.size() < 2) continue;

        // Build text corpus for this school. Spells that get their theme from
        // traits stay out of it: the word themes only have to cover the rest.
        std::vector<std::vector<std::string>> documents;
        for (const auto& spell : sSpells) {
            if (!ThemeFromTraits(spell).empty()) continue;
            auto text = TreeNLP::BuildThemeText(spell);
            auto tokens = TreeNLP::Tokenize(text);
            // Filter stop words
            std::vector<std::string> filtered;
            for (const auto& t : tokens) {
                if (!TreeNLP::IsStopWord(t)) {
                    filtered.push_back(t);
                }
            }
            documents.push_back(std::move(filtered));
        }

        // Compute TF-IDF
        auto vectors = TreeNLP::ComputeTfIdf(documents);

        // Sum TF-IDF scores per term across all documents
        std::unordered_map<std::string, float> termScores;
        for (const auto& vec : vectors) {
            for (const auto& [token, weight] : vec.weights) {
                termScores[token] += weight;
            }
        }

        // Sort by score descending, take top N
        std::vector<std::pair<std::string, float>> sorted(termScores.begin(), termScores.end());
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        // Prefixes are judged over every spell of the plugin, not just this
        // school's leftovers: the more ids, the surer the prefix.
        const auto modTags = FindModTags(spells);

        std::vector<std::string> themes;
        for (const auto& [term, score] : sorted) {
            if (TreeNLP::IsStopWord(term)) continue;
            if (modTags.contains(term)) continue;
            if (term.size() <= 2) continue;
            if (HasDigit(term)) continue;
            themes.push_back(term);
            if (static_cast<int>(themes.size()) >= topN) break;
        }

        result[school] = std::move(themes);
    }

    return result;
}

std::unordered_map<std::string, std::vector<std::string>>
TreeBuilder::MergeWithHints(
    const std::unordered_map<std::string, std::vector<std::string>>& discovered,
    int maxThemes)
{
    const auto& hints = GetVanillaThemeHints();
    std::unordered_map<std::string, std::vector<std::string>> merged;

    for (const auto& [school, themes] : discovered) {
        auto hintIt = hints.find(school);
        if (hintIt != hints.end()) {
            // Discovered themes first, hints after, and the hints do not eat into
            // the discovered ones' room. The hints are fire / frost / shock /
            // summon ... - what rule 1 already settles - so when they went first
            // they used 8 of the 12 slots and words like "wind" or "arcane", the
            // very thing rule 2 is kept for, fell off the end.
            std::vector<std::string> result(themes.begin(),
                themes.begin() + std::min(static_cast<int>(themes.size()), maxThemes));
            std::unordered_set<std::string> present;
            for (const auto& t : result) present.insert(TreeNLP::ToLower(t));

            for (const auto& h : hintIt->second) {
                if (present.insert(TreeNLP::ToLower(h)).second) {
                    result.push_back(h);
                }
            }
            merged[school] = std::move(result);
        } else {
            merged[school] = std::vector<std::string>(themes.begin(),
                themes.begin() + std::min(static_cast<int>(themes.size()), maxThemes));
        }
    }

    // Add hint-only schools not in discovered
    for (const auto& [school, hintThemes] : hints) {
        if (!merged.contains(school)) {
            merged[school] = hintThemes;
        }
    }

    return merged;
}

// =============================================================================
// TRAIT THEMES
// =============================================================================
//
// Word themes only work where spell names are English: on a translated load
// order the most frequent "words" left are fragments of mod keyword names, and
// the branches end up grouped by which framework tagged a spell. The scan's
// traits column says what a spell is without reading any text, so a spell that
// has traits takes its theme from them and the word path is left for the rest.
//
// RULE 1 - traits. One theme per spell, most telling trait first: what is
// summoned, then the element, then what the spell does.
// RULE 2 - words (DiscoverThemesPerSchool + CalculateThemeScore, the original
// method). It stays, because it is the only thing that can name a nature the
// game has no value for: water, wind, stone, blood. Those spells have no resist
// value and no vanilla keyword, so rule 1 has nothing to say about them.
//
// Order in GetSpellPrimaryTheme: rule 1 -> rule 2 -> rule 1's shape traits.
// Cloak, rune and stagger describe the shape of a spell rather than its nature,
// so for a spell without an element they wait until the words have had a go: a
// wind cloak should land in "wind" when the words can tell, in "cloak" when not.

namespace
{
    constexpr int kTraitThemeScore = 100;  // top of CalculateThemeScore's range

    constexpr std::string_view kElementPrefix = "element.";
    constexpr std::string_view kKindPrefix = "kind.";
    constexpr std::string_view kSummonTrait = "kind.summon";
    constexpr std::string_view kReanimateTrait = "kind.reanimate";

    // Says "this hurts" and nothing else; every attack spell has it.
    constexpr std::string_view kTooBroadKind = "kind.damage";

    // Shape, not nature: only used when rule 2 finds nothing better.
    constexpr std::string_view kShapeKinds[] = { "kind.cloak", "kind.rune", "kind.stagger" };

    // Below this a word match is noise (same cut the builders apply).
    constexpr int kWordThemeMinScore = 30;

    bool IsShapeKind(std::string_view trait)
    {
        return std::find(std::begin(kShapeKinds), std::end(kShapeKinds), trait) != std::end(kShapeKinds);
    }

    // Which of the summon's other traits names the branch, best first.
    constexpr std::string_view kSummonQualifiers[] = {
        "element.fire", "element.frost", "element.shock", "kind.undead", "kind.familiar"
    };

    std::string AfterDot(std::string_view trait)
    {
        const auto dot = trait.find('.');
        return std::string(dot == std::string_view::npos ? trait : trait.substr(dot + 1));
    }
}

std::string TreeBuilder::ThemeFromTraits(const json& spell, bool shapeOnly)
{
    const auto it = spell.find("traits");
    if (it == spell.end() || !it->is_array()) return "";

    std::vector<std::string> traits;
    for (const auto& trait : *it) {
        if (trait.is_string()) traits.push_back(trait.get<std::string>());
    }
    const auto has = [&traits](std::string_view wanted) {
        return std::find(traits.begin(), traits.end(), wanted) != traits.end();
    };

    if (shapeOnly) {
        for (const auto& trait : traits) {
            if (IsShapeKind(trait)) return AfterDot(trait);
        }
        return "";
    }

    // Raising a corpse carries vanilla's MagicSummonUndead as well, but it is its
    // own craft: it needs a body, a conjured thrall does not.
    if (has(kReanimateTrait)) return AfterDot(kReanimateTrait);

    if (has(kSummonTrait)) {
        for (const auto qualifier : kSummonQualifiers) {
            if (has(qualifier)) return "summon_" + AfterDot(qualifier);
        }
        return "summon";
    }

    for (const auto& trait : traits) {
        if (trait.starts_with(kElementPrefix)) return AfterDot(trait);
    }
    for (const auto& trait : traits) {
        if (trait.starts_with(kKindPrefix) && trait != kTooBroadKind && !IsShapeKind(trait)) {
            return AfterDot(trait);
        }
    }
    return "";
}

// =============================================================================
// SPELL GROUPING
// =============================================================================

std::pair<std::string, int>
TreeBuilder::GetSpellPrimaryTheme(const json& spell, const std::vector<std::string>& themes)
{
    // Rule 1: what the spell is beats what its name happens to contain.
    const std::string traitTheme = ThemeFromTraits(spell);
    if (!traitTheme.empty()) return {traitTheme, kTraitThemeScore};

    // Rule 1's shape traits, kept in hand in case rule 2 comes up empty.
    const std::string shapeTheme = ThemeFromTraits(spell, true);

    if (themes.empty()) {
        if (!shapeTheme.empty()) return {shapeTheme, kTraitThemeScore};
        return {"_unassigned", 0};
    }

    std::string bestTheme;
    int bestScore = 0;

    for (const auto& theme : themes) {
        int score = TreeNLP::CalculateThemeScore(spell, theme);
        if (score > bestScore) {
            bestScore = score;
            bestTheme = theme;
        }
    }

    // Rule 2 did not find a convincing word: fall back to the spell's shape.
    if (bestScore <= kWordThemeMinScore && !shapeTheme.empty()) {
        return {shapeTheme, kTraitThemeScore};
    }

    return {bestTheme.empty() ? "_unassigned" : bestTheme, bestScore};
}

std::unordered_map<std::string, std::vector<json>>
TreeBuilder::GroupSpellsBestFit(const std::vector<json>& spells,
                                const std::vector<std::string>& themes,
                                int minScore)
{
    std::unordered_map<std::string, std::vector<json>> groups;
    for (const auto& theme : themes) {
        groups[theme] = {};
    }
    groups["_unassigned"] = {};

    for (const auto& spell : spells) {
        auto [bestTheme, bestScore] = GetSpellPrimaryTheme(spell, themes);

        if (bestScore >= minScore && !bestTheme.empty() && bestTheme != "_unassigned") {
            groups[bestTheme].push_back(spell);
        } else {
            groups["_unassigned"].push_back(spell);
        }
    }

    // Reclassify unassigned spells with LLM keywords (if present)
    auto& unassigned = groups["_unassigned"];
    std::vector<json> reclassified;
    for (const auto& spell : unassigned) {
        auto llmKw = spell.value("llm_keyword", std::string(""));
        if (llmKw.empty()) continue;

        if (groups.contains(llmKw)) {
            groups[llmKw].push_back(spell);
            reclassified.push_back(spell);
        } else {
            auto parent = spell.value("llm_keyword_parent", std::string(""));
            if (!parent.empty() && groups.contains(parent)) {
                groups[parent].push_back(spell);
                reclassified.push_back(spell);
            }
        }
    }
    for (const auto& r : reclassified) {
        auto fid = r.value("formId", std::string(""));
        unassigned.erase(
            std::remove_if(unassigned.begin(), unassigned.end(),
                [&fid](const json& s) { return s.value("formId", std::string("")) == fid; }),
            unassigned.end());
    }

    return groups;
}

// =============================================================================
// TREE VALIDATION
// =============================================================================

std::unordered_set<std::string> TreeBuilder::SimulateUnlocks(
    const std::unordered_map<std::string, TreeNode>& nodes,
    const std::string& rootId)
{
    std::unordered_set<std::string> unlocked;
    if (!nodes.contains(rootId)) return unlocked;

    unlocked.insert(rootId);

    // Fixed-point iteration: keep unlocking until no new unlocks
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& [fid, node] : nodes) {
            if (unlocked.contains(fid)) continue;

            // Node unlocks when ALL prerequisites are unlocked
            bool allPrereqsMet = true;
            for (const auto& prereq : node.prerequisites) {
                if (!unlocked.contains(prereq)) {
                    allPrereqsMet = false;
                    break;
                }
            }

            if (allPrereqsMet && !node.prerequisites.empty()) {
                unlocked.insert(fid);
                changed = true;
            }
        }
    }

    return unlocked;
}

std::vector<std::string> TreeBuilder::FindUnreachableNodes(
    const std::unordered_map<std::string, TreeNode>& nodes,
    const std::string& rootId)
{
    auto unlocked = SimulateUnlocks(nodes, rootId);

    std::vector<std::string> unreachable;
    for (const auto& [fid, node] : nodes) {
        if (!unlocked.contains(fid)) {
            unreachable.push_back(fid);
        }
    }
    return unreachable;
}

std::vector<std::vector<std::string>> TreeBuilder::DetectCycles(
    const std::unordered_map<std::string, TreeNode>& nodes)
{
    // DFS-based cycle detection
    std::vector<std::vector<std::string>> cycles;
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> inStack;
    std::vector<std::string> stack;

    std::function<void(const std::string&)> dfs = [&](const std::string& nodeId) {
        if (inStack.contains(nodeId)) {
            // Found a cycle — extract it
            std::vector<std::string> cycle;
            auto it = std::find(stack.begin(), stack.end(), nodeId);
            if (it != stack.end()) {
                for (; it != stack.end(); ++it) {
                    cycle.push_back(*it);
                }
                cycle.push_back(nodeId);
                cycles.push_back(std::move(cycle));
            }
            return;
        }
        if (visited.contains(nodeId)) return;

        visited.insert(nodeId);
        inStack.insert(nodeId);
        stack.push_back(nodeId);

        auto it = nodes.find(nodeId);
        if (it != nodes.end()) {
            for (const auto& childId : it->second.children) {
                dfs(childId);
            }
        }

        stack.pop_back();
        inStack.erase(nodeId);
    };

    for (const auto& [fid, node] : nodes) {
        if (!visited.contains(fid)) {
            dfs(fid);
        }
    }

    return cycles;
}

TreeBuilder::ValidationResult TreeBuilder::ValidateSchoolTree(
    const std::unordered_map<std::string, TreeNode>& nodes,
    const std::string& rootId,
    int maxChildren)
{
    ValidationResult result;
    result.totalNodes = static_cast<int>(nodes.size());

    if (!nodes.contains(rootId)) {
        result.allValid = false;
        result.warnings.push_back("Root node not found: " + rootId);
        return result;
    }

    // Check reachability
    auto unlocked = SimulateUnlocks(nodes, rootId);
    result.reachableNodes = static_cast<int>(unlocked.size());
    result.unreachableCount = result.totalNodes - result.reachableNodes;

    for (const auto& [fid, node] : nodes) {
        if (!unlocked.contains(fid)) {
            result.unreachableIds.push_back(fid);
        }
    }

    // Check cycles
    auto cycles = DetectCycles(nodes);
    result.cycleCount = static_cast<int>(cycles.size());

    // Check max children violations
    for (const auto& [fid, node] : nodes) {
        if (static_cast<int>(node.children.size()) > maxChildren + 2) {
            result.warnings.push_back(
                "Node " + fid + " has " + std::to_string(node.children.size()) +
                " children (max " + std::to_string(maxChildren) + " + 2 overflow tolerance)");
        }
    }

    result.allValid = (result.unreachableCount == 0 && result.cycleCount == 0);
    return result;
}

int TreeBuilder::FixUnreachableNodes(
    std::unordered_map<std::string, TreeNode>& nodes,
    const std::string& rootId,
    int maxChildren)
{
    int totalFixes = 0;

    for (int pass = 0; pass < 20; ++pass) {
        auto unreachable = FindUnreachableNodes(nodes, rootId);
        if (unreachable.empty()) break;

        bool fixedAny = false;

        for (const auto& fid : unreachable) {
            auto& node = nodes[fid];

            // Strategy 1: Remove blocking prerequisites
            // Find prereqs that are themselves unreachable
            std::vector<std::string> blockingPrereqs;
            auto currentUnlocked = SimulateUnlocks(nodes, rootId);

            for (const auto& prereq : node.prerequisites) {
                if (!currentUnlocked.contains(prereq)) {
                    blockingPrereqs.push_back(prereq);
                }
            }

            if (!blockingPrereqs.empty()) {
                for (const auto& bp : blockingPrereqs) {
                    // Remove this prerequisite
                    node.prerequisites.erase(
                        std::remove(node.prerequisites.begin(), node.prerequisites.end(), bp),
                        node.prerequisites.end());
                    // Also remove from parent's children
                    if (nodes.contains(bp)) {
                        auto& parent = nodes[bp];
                        parent.children.erase(
                            std::remove(parent.children.begin(), parent.children.end(), fid),
                            parent.children.end());
                    }
                }
                totalFixes++;
                fixedAny = true;
                continue;
            }

            // Strategy 2: If no prerequisites at all, connect to root or nearest available
            if (node.prerequisites.empty()) {
                // Find best parent among reachable nodes
                std::string bestParent;
                int bestChildCount = std::numeric_limits<int>::max();

                for (const auto& [rid, rnode] : nodes) {
                    if (!currentUnlocked.contains(rid)) continue;
                    if (rid == fid) continue;
                    if (static_cast<int>(rnode.children.size()) < maxChildren &&
                        static_cast<int>(rnode.children.size()) < bestChildCount) {
                        bestChildCount = static_cast<int>(rnode.children.size());
                        bestParent = rid;
                    }
                }

                if (!bestParent.empty()) {
                    LinkNodes(nodes[bestParent], node);
                    totalFixes++;
                    fixedAny = true;
                } else {
                    // Last resort: connect to root (even if over capacity)
                    LinkNodes(nodes[rootId], node);
                    totalFixes++;
                    fixedAny = true;
                }
            }
        }

        if (!fixedAny) break;
    }

    return totalFixes;
}
