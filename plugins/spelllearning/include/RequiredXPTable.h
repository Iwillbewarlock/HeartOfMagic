#pragma once

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>

// =============================================================================
// REQUIRED XP TABLE - where a spell's required XP comes from
// =============================================================================
//
// Three kinds of number, kept apart because they live differently:
//   tree       the tree's own requiredXP per node (LoadSpellTree). The tree is the
//              same for every save: kept across a revert.
//   override   the player's per-spell XP override in the panel's settings
//              (config.json "xpOverrides", global, not per character): kept across a
//              revert, replaced whole each time the settings are applied.
//   character  this character's number from the panel (a learning target's,
//              RequiredXPSync: the reverse unlock share depends on what this
//              character knows). Cleared on revert; EndLoad works the entries out
//              again without it.
// A spell's number: this character's, else (override, else tree, else the tier's)
// times the reverse unlock share when a known higher spell opened it - the panel's
// getRequiredXPForNode. No game types, so tools/progression-check runs it.
namespace RequiredXP
{
    enum class Kind
    {
        kTree,
        kCharacter,
    };

    class Table
    {
    public:
        using Map = std::unordered_map<std::uint32_t, float>;

        void Set(Kind kind, std::uint32_t formId, float xp)
        {
            (kind == Kind::kTree ? m_tree : m_character)[formId] = xp;
        }

        // The settings' overrides, all of them (an override removed in the panel goes)
        void SetOverrides(Map overrides) { m_overrides = std::move(overrides); }

        // A revert (another save, a new game): only this character's numbers go
        void OnRevert() { m_character.clear(); }

        [[nodiscard]] bool HasCharacterValue(std::uint32_t formId) const { return Positive(m_character, formId) > 0.0f; }

        // `tierXP`: the spell's tier XP; `share`: the reverse unlock share, applied when
        // `openedByKnownChild`
        [[nodiscard]] float For(std::uint32_t formId, float tierXP, bool openedByKnownChild, float share) const
        {
            if (const float own = Positive(m_character, formId); own > 0.0f) return own;
            float base = Positive(m_overrides, formId);
            if (!(base > 0.0f)) base = Positive(m_tree, formId);
            if (!(base > 0.0f)) base = tierXP;
            return openedByKnownChild ? Shared(base, share) : base;
        }

        // The panel's Math.max(1, Math.round(base * share)): whole XP, at least 1 (2 at
        // 30% is 1, not 0.6 - RequiredXPSync lets differences under 0.5 stand). floor(x +
        // 0.5) is Math.round for the positive values here
        [[nodiscard]] static float Shared(float base, float share)
        {
            const float rounded = std::floor(base * share + 0.5f);
            return rounded < 1.0f ? 1.0f : rounded;
        }

        [[nodiscard]] std::size_t TreeCount() const { return m_tree.size(); }
        [[nodiscard]] std::size_t OverrideCount() const { return m_overrides.size(); }

    private:
        [[nodiscard]] static float Positive(const Map& map, std::uint32_t formId)
        {
            const auto it = map.find(formId);
            return it != map.end() && it->second > 0.0f ? it->second : 0.0f;
        }

        Map m_tree;
        Map m_overrides;
        Map m_character;
    };
}
