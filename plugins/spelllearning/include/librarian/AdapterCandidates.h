#pragma once

#include "librarian/PerkAdapters.h"

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// =============================================================================
// AdapterCandidates - which catalog spells a planner line needs to look at
//
// The planner used to walk every catalog spell for every line. This keeps one
// snapshot of the walk and, per line, the spells that could pass its first
// check. A spell left out would have failed that check with no side effect
// (specs/003-load-time/plan.md, Phase 2c), so the plan comes out the same.
// Pure, like PerkAdapters.h. Internal to src/librarian/ and the harness.
// =============================================================================

namespace Librarian::Adapters::Candidates
{
    struct CatalogSpell
    {
        std::string id;
        const json* spell = nullptr;
        const json* entry = nullptr;
    };

    class Index
    {
    public:
        // Walks the source once. The refs it hands over must outlive this
        // (PlanSource::forEachCatalogSpell).
        explicit Index(const PlanSource& source);

        // Every catalog spell a keyword, formList or castingPerk line could pick,
        // in catalog order (damage lines walk the source themselves)
        void ForEachCandidate(const Line& line, const std::function<void(const CatalogSpell&)>& visit) const;

    private:
        using Positions = std::vector<std::size_t>;

        // Positions are appended in catalog order, so each list is ascending
        [[nodiscard]] const Positions* Find(const std::unordered_map<std::string, Positions>& index,
            const std::string& key) const;

        std::vector<CatalogSpell> m_spells;
        std::unordered_map<std::string, Positions> m_byTag;         // entry elements and techniques
        std::unordered_map<std::string, Positions> m_bySchool;      // spell "school"
        std::unordered_map<std::string, Positions> m_byArchetype;   // a visible effect's archetype
    };
}
