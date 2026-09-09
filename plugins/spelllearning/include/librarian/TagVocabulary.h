#pragma once

#include <algorithm>
#include <string_view>

// =============================================================================
// Tag vocabulary - the closed list of tags the librarian may assign
//
// Rules may only add tags from these lists; anything else is dropped with a
// warning when the rule file loads. That keeps a typo in a user's rule file
// from quietly inventing a tag no adapter knows how to translate.
//
// MIRRORED in PrismaUI/.../modules/tagVocabulary.js. The two lists must stay
// identical - change one, change the other. librarian-test --check-vocab
// compares them.
//
// The definitions live in docs/librarian/TAGS.md. Read that before adding a
// tag: the list is deliberately finer grained than any single consumer needs,
// because tags can always be merged on the way out to a guest and never split.
// =============================================================================

namespace Librarian
{
    // =========================================================================
    // ELEMENTS - what the magic is made of
    // =========================================================================
    //
    // The 34 from Spell Research, plus four the load order's own keyword
    // frameworks distinguish and Spell Research cannot express.

    inline constexpr std::string_view ELEMENTS[] = {
        "acid",
        "air",
        "apparition",
        "arcane",
        "armor",
        "blood",       // beyond Spell Research
        "construct",
        "creature",
        "daedra",
        "disease",
        "earth",
        "eldritch",    // beyond Spell Research
        "fire",
        "flesh",
        "force",
        "frost",
        "health",
        "holy",        // beyond Spell Research
        "human",
        "life",
        "light",
        "magicka",
        "metal",
        "nature",
        "necrotic",    // beyond Spell Research
        "poison",
        "resistance",
        "shadow",
        "shield",
        "shock",
        "soul",
        "stamina",
        "sun",
        "time",
        "trap",
        "undead",
        "water",
        "weapon",
    };

    // =========================================================================
    // TECHNIQUES - what the magic does
    // =========================================================================
    //
    // The 15 from Spell Research, plus three the frameworks distinguish.

    inline constexpr std::string_view TECHNIQUES[] = {
        "cloak",
        "control",
        "courage",
        "curing",
        "curse",
        "dispel",      // beyond Spell Research
        "fear",
        "frenzy",
        "infuse",
        "pacify",
        "sacrifice",   // beyond Spell Research
        "sense",
        "siphon",
        "strengthen",
        "summoning",
        "telekinesis",
        "teleport",    // beyond Spell Research
        "transform",
    };

    // =========================================================================
    // LOOKUP
    // =========================================================================

    [[nodiscard]] inline bool IsElement(std::string_view tag)
    {
        return std::find(std::begin(ELEMENTS), std::end(ELEMENTS), tag) != std::end(ELEMENTS);
    }

    [[nodiscard]] inline bool IsTechnique(std::string_view tag)
    {
        return std::find(std::begin(TECHNIQUES), std::end(TECHNIQUES), tag) != std::end(TECHNIQUES);
    }
}
