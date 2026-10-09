#pragma once

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

// =============================================================================
// VanillaCastingPerks - the 25 half-cost perks of Skyrim.esm
//
// A spell's "casting perk" (CommonLib's castingPerk, the SPEL record's half-cost
// perk) files it under a school and tier: the vanilla cost perks and every perk
// that asks SpellHasCastingPerk read it. The scanner reads the tier from it; the
// perk adapters give one to catalog spells that have none.
// =============================================================================

namespace Librarian
{
    struct VanillaCastingPerk
    {
        std::string_view school;    // lowercase, as the catalog writes it
        const char* tier;           // as DetermineSpellTier returns it
        std::uint32_t formId;       // in Skyrim.esm
    };

    // AlterationNovice00, AlterationApprentice25 ... RestorationMaster100
    inline constexpr VanillaCastingPerk kVanillaCastingPerks[] = {
        { "alteration", "Novice", 0x000F2CA6 }, { "alteration", "Apprentice", 0x000C44B7 },
        { "alteration", "Adept", 0x000C44B8 }, { "alteration", "Expert", 0x000C44B9 },
        { "alteration", "Master", 0x000C44BA },
        { "conjuration", "Novice", 0x000F2CA7 }, { "conjuration", "Apprentice", 0x000C44BB },
        { "conjuration", "Adept", 0x000C44BC }, { "conjuration", "Expert", 0x000C44BD },
        { "conjuration", "Master", 0x000C44BE },
        { "destruction", "Novice", 0x000F2CA8 }, { "destruction", "Apprentice", 0x000C44BF },
        { "destruction", "Adept", 0x000C44C0 }, { "destruction", "Expert", 0x000C44C1 },
        { "destruction", "Master", 0x000C44C2 },
        { "illusion", "Novice", 0x000F2CA9 }, { "illusion", "Apprentice", 0x000C44C3 },
        { "illusion", "Adept", 0x000C44C4 }, { "illusion", "Expert", 0x000C44C5 },
        { "illusion", "Master", 0x000C44C6 },
        { "restoration", "Novice", 0x000F2CAA }, { "restoration", "Apprentice", 0x000C44C7 },
        { "restoration", "Adept", 0x000C44C8 }, { "restoration", "Expert", 0x000C44C9 },
        { "restoration", "Master", 0x000C44CA },
    };

    // The perk for a catalog school and tier ("destruction", "novice"), as a
    // persistent id the way the scan writes one ("Skyrim.esm|0x0F2CA8").
    // Empty for anything else (a tier the catalog left out, a non-school).
    inline std::string VanillaCastingPerkId(std::string_view school, std::string_view tier)
    {
        // Tier words are letters only, so case folds with one bit
        const auto sameLetter = [](unsigned char a, unsigned char b) { return (a | 0x20) == (b | 0x20); };
        for (const auto& perk : kVanillaCastingPerks) {
            if (perk.school != school || !std::ranges::equal(std::string_view(perk.tier), tier, sameLetter)) continue;
            // PluginNames::MakePersistentId's shape; Skyrim.esm needs no re-encoding
            return std::format("Skyrim.esm|0x{:06X}", perk.formId);
        }
        return {};
    }
}
