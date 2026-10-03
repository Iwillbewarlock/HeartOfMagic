#include "treebuilder/TreeBuilderInternal.h"

#include <algorithm>
#include <cmath>

// =============================================================================
// TYPE GROUPING (2026-10-04)
// =============================================================================
//
// How strongly Classic puts a spell under a parent of the same kind and made of
// the same thing (BuildConfig::typeGrouping, the panel's "Type grouping").
//
//  - Same element: the flat bonus of 1.2.0, raised by how rare the element is in
//    the school. Fire on a quarter of Destruction tells less than soul on 2% of
//    Conjuration; never less than the flat bonus, so common elements keep the
//    pull they had.
//  - Same kind (kind.bound, kind.rune, kind.cloak ...): a bonus weighed by the
//    same rarity, so kind.summon on 80% of Conjuration counts for next to
//    nothing and kind.bound on 5% of it counts nearly in full. Before, a kind
//    only counted through the themes.
//  - Different elements (both spells carry elements, none in common: fire under
//    frost, poison under disease): a penalty, and the kind bonus does not count,
//    so the element branches stay apart. (Halving the kind bonus when only one
//    of the two has an element too was tried: bound weapons 74% -> 69% under a
//    bound parent, fire/frost/shock unchanged.)
//
// Strength 0 is exactly the 1.2.0 scoring (the flat element bonus and nothing
// else); the strength scales every part above it, 1 being the default.

namespace
{
    // A parent made of the same thing (both blood, both water) - the element
    // traits the tag librarian hands on. A little more than a theme match: enough to
    // pull a mod's blood spells onto one branch, not enough to outweigh an
    // effect that is plainly the same spell a tier up.
    // Measured on the 1,428-spell test load order (classic, seeds 1-3): same
    // element clusters 333 -> 247 at 30, barely fewer above it (241 at 60),
    // theme-sharing edges and tier gaps unchanged.
    constexpr float kSharedElementBonus = 30.0f;

    // How much of the element bonus rarity may add, per unit of strength: at
    // strength 1 the rarest element gets 1.5 x 30 = 45, fire in Destruction
    // (25% of the school) about 35.
    constexpr float kElementRarityBoost = 0.5f;

    // A parent of the same kind, at full rarity and strength 1. Below the element
    // bonus: a bound weapon looks for other bound weapons, a fire bound weapon
    // still for fire first.
    constexpr float kSharedKindBonus = 30.0f;

    // Both spells made of something, of different things, at strength 1
    constexpr float kDifferentElementPenalty = 15.0f;

    // A trait on this share of a school (or less) counts as fully rare; the
    // rarity falls with the log of the share and is 0 for a trait every spell has.
    constexpr float kRareShare = 0.02f;

    constexpr std::string_view kElementPrefix = "element.";
    constexpr std::string_view kKindPrefix = "kind.";

    bool AnyShared(const std::vector<std::string>& a, const std::vector<std::string>& b)
    {
        for (const auto& x : a) {
            if (std::find(b.begin(), b.end(), x) != b.end()) return true;
        }
        return false;
    }
}

TreeBuilder::Internal::TypeGrouping::TypeGrouping(
    const std::unordered_map<std::string, TreeNode>& nodes, float strength) :
    m_strength(strength)
{
    std::unordered_map<std::string, int> counts;
    for (const auto& [fid, node] : nodes) {
        Types types;
        const auto traits = node.spellData.find("traits");
        if (traits != node.spellData.end() && traits->is_array()) {
            for (const auto& t : *traits) {
                if (!t.is_string()) continue;
                const auto& s = t.get_ref<const std::string&>();
                if (s.starts_with(kElementPrefix)) types.elements.push_back(s);
                else if (s.starts_with(kKindPrefix)) types.kinds.push_back(s);
                else continue;
                ++counts[s];
            }
        }
        m_types.emplace(fid, std::move(types));
    }

    const float total = static_cast<float>(nodes.size());
    const float fullRarity = std::log(1.0f / kRareShare);
    for (const auto& [trait, count] : counts) {
        const float share = static_cast<float>(count) / total;
        m_rarity[trait] = std::clamp(std::log(1.0f / share) / fullRarity, 0.0f, 1.0f);
    }
}

float TreeBuilder::Internal::TypeGrouping::BestSharedRarity(
    const std::vector<std::string>& a, const std::vector<std::string>& b) const
{
    float best = -1.0f;
    for (const auto& x : a) {
        if (std::find(b.begin(), b.end(), x) == b.end()) continue;
        const auto it = m_rarity.find(x);
        best = (std::max)(best, it != m_rarity.end() ? it->second : 0.0f);
    }
    return best;
}

float TreeBuilder::Internal::TypeGrouping::Score(const TreeNode& child, const TreeNode& parent) const
{
    const auto ci = m_types.find(child.formId);
    const auto pi = m_types.find(parent.formId);
    if (ci == m_types.end() || pi == m_types.end()) return 0.0f;
    const Types& c = ci->second;
    const Types& p = pi->second;

    // Strength 0: the 1.2.0 bonus, nothing else
    if (m_strength <= 0.0f) {
        return AnyShared(c.elements, p.elements) ? kSharedElementBonus : 0.0f;
    }

    float score = 0.0f;
    bool kindCounts = true;
    const float element = BestSharedRarity(c.elements, p.elements);
    if (element >= 0.0f) {
        score += kSharedElementBonus * (1.0f + m_strength * kElementRarityBoost * element);
    } else if (!c.elements.empty() && !p.elements.empty()) {
        score -= m_strength * kDifferentElementPenalty;
        kindCounts = false;   // a frost rune is no parent for a fire rune
    }

    const float kind = BestSharedRarity(c.kinds, p.kinds);
    if (kindCounts && kind > 0.0f) score += m_strength * kSharedKindBonus * kind;
    return score;
}
