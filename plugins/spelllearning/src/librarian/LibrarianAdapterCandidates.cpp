#include "librarian/AdapterCandidates.h"
#include "librarian/AdapterMatch.h"

#include <algorithm>

// =============================================================================
// LibrarianAdapterCandidates - see AdapterCandidates.h
//
// Why each filter skips only spells that change nothing:
// - tags: HasTag sets withoutSources only when it finds the tag, so a spell
//   missing tags.all[0] (or every tags.any tag) fails TagsMatch untouched.
// - school, archetype: used only for a line with no tag condition, where
//   nothing is counted before SpellMatches and PickTarget turn the spell down.
// Keys are the fields as the matchers read them, missing ones as "".
// =============================================================================

namespace Librarian::Adapters::Candidates
{
    using namespace Match;

    namespace
    {
        void Add(std::vector<std::size_t>& positions, std::size_t position)
        {
            if (positions.empty() || positions.back() != position) positions.push_back(position);
        }
    }

    Index::Index(const PlanSource& source)
    {
        source.forEachCatalogSpell([&](const std::string& id, const json& spell, const json& entry) {
            m_spells.push_back({ id, &spell, &entry });
        });

        for (std::size_t i = 0; i < m_spells.size(); ++i) {
            const json& spell = *m_spells[i].spell;
            const json& entry = *m_spells[i].entry;
            for (const char* axis : { "elements", "techniques" }) {
                const auto list = entry.find(axis);
                if (list == entry.end() || !list->is_array()) continue;
                for (const auto& value : *list) {
                    if (value.is_string()) Add(m_byTag[value.get<std::string>()], i);
                }
            }
            Add(m_bySchool[Field(spell, "school")], i);
            for (const auto& effect : EffectsOf(spell)) {
                if (IsVisible(effect)) Add(m_byArchetype[Field(effect, "archetype")], i);
            }
        }
    }

    const Index::Positions* Index::Find(const std::unordered_map<std::string, Positions>& index,
        const std::string& key) const
    {
        const auto found = index.find(key);
        return found == index.end() ? nullptr : &found->second;
    }

    void Index::ForEachCandidate(const Line& line, const std::function<void(const CatalogSpell&)>& visit) const
    {
        const auto visitAll = [&](const Positions* positions) {
            if (!positions) return;
            for (const std::size_t i : *positions) visit(m_spells[i]);
        };

        if (!line.tags.all.empty()) {
            visitAll(Find(m_byTag, line.tags.all.front()));
            return;
        }
        if (!line.tags.any.empty()) {
            Positions merged;
            for (const auto& tag : line.tags.any) {
                if (const Positions* positions = Find(m_byTag, tag)) {
                    merged.insert(merged.end(), positions->begin(), positions->end());
                }
            }
            std::ranges::sort(merged);
            merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
            visitAll(&merged);
            return;
        }
        if (line.spell.school) {
            visitAll(Find(m_bySchool, *line.spell.school));
            return;
        }
        // A castingPerk line has no target effect, so only the others need a visible one
        if (line.castingPerk.empty() && line.effect && line.effect->archetype) {
            visitAll(Find(m_byArchetype, *line.effect->archetype));
            return;
        }
        for (const auto& spell : m_spells) visit(spell);
    }
}
