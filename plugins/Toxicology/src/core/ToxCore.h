#pragma once

// =============================================================================
// Toxicology core - pure C++ (no RE/SKSE types), shared by the DLL and
// tools/toxicology-test: settings, the poison spell list from the librarian's
// catalog, the top favorited poison, the magicka-per-dose count and the
// coat interval.
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Toxicology
{
    struct Settings
    {
        bool enabled = true;
        bool showIcon = true;
        float minIntervalSeconds = 1.0f;
        float magickaPerDose = 100.0f;
    };

    // A missing key keeps its default silently. A key of the wrong type, a negative
    // minIntervalSeconds, a magickaPerDose <= 0 or a non-finite number keeps the
    // default and adds one warning naming the key and the value.
    Settings ParseSettings(const nlohmann::json& a_json, std::vector<std::string>& a_warnings);

    // Every spells key whose "elements" array holds "poison", sorted. Empty with
    // a_error set when "version" is not 1 or "spells" is not an object.
    std::vector<std::string> PoisonSpellKeys(const nlohmann::json& a_catalog, std::string& a_error);

    // A spell a cloak effect casts on whoever comes near (one level down from a catalog spell)
    struct CloakSubSpell
    {
        std::uint32_t formId = 0;
        bool hostile = false;  // at least one hostile effect
    };

    // A catalog spell tagged "poison" that resolved to a loaded spell
    struct PoisonSpellNode
    {
        std::uint32_t formId = 0;
        bool hostile = false;
        std::vector<CloakSubSpell> cloakSubs;
    };

    struct PoisonSets
    {
        // What a hit is checked against: hostile catalog spells and hostile cloak sub-spells (the cloak
        // spell itself is not hostile; its sub-spell is what reaches the enemy)
        std::unordered_set<std::uint32_t> hit;
        // What a cast is charged for: hostile catalog spells and catalog spells with a hostile cloak
        // sub-spell (the player casts and pays for the cloak, never for its sub-spell)
        std::unordered_set<std::uint32_t> cost;
        std::size_t catalogSpells = 0;   // distinct catalog spells
        std::size_t cloakSubSpells = 0;  // distinct sub-spells that are not catalog spells themselves
        std::size_t notHostile = 0;      // of both, those without a hostile effect
    };

    // Builds both sets; each form counted once, so catalogSpells + cloakSubSpells = hit.size() + notHostile.
    PoisonSets BuildPoisonSets(const std::vector<PoisonSpellNode>& a_catalog);

    // The largest local FormID a catalog key may name: a full plugin's 24 bits. A light plugin's
    // narrower range (12 bits) is the caller's check, once it knows the file is light.
    inline constexpr std::uint32_t kMaxCatalogLocalId = 0xFFFFFF;

    struct CatalogKey
    {
        std::string plugin;
        std::uint32_t local = 0;
    };

    // "Plugin.esp|0x00XXXX" -> {plugin, local}. The "0x"/"0X" prefix is optional; the rest must be
    // hex digits only, at most kMaxCatalogLocalId. nullopt for anything else (no '|', empty plugin,
    // empty or bad hex, too large).
    std::optional<CatalogKey> SplitCatalogKey(std::string_view a_key);

    struct PoisonCandidate
    {
        std::string name;
        std::uint32_t formId = 0;
        bool favorited = false;
        std::int32_t count = 0;
    };

    // Among favorited candidates with count > 0: the smallest name by ASCII-lowercased
    // UTF-8 bytes (unsigned), ties by the smaller formId. nullopt when none qualifies.
    std::optional<std::size_t> PickTopPoison(const std::vector<PoisonCandidate>& a_candidates);

    struct DoseResult
    {
        std::int32_t doses = 0;
        float remainder = 0.0f;
    };

    // Adds a_spent to a_accumulated and takes whole doses of a_perDose out of it.
    // A negative or non-finite a_spent adds nothing; a negative or non-finite
    // a_accumulated counts as 0; a non-positive or non-finite a_perDose takes no doses.
    DoseResult ConsumeMagicka(float a_accumulated, float a_spent, float a_perDose);

    // One stack of a poison in the player's inventory: one ExtraDataList's bottles (favorited when
    // the list carries ExtraHotkey), or the bottles outside any list.
    struct BottleStack
    {
        std::int32_t count = 0;
        bool favorited = false;
    };

    struct StackTake
    {
        std::size_t index = 0;
        std::int32_t count = 0;
    };

    // Spec 12: where the next of a_doses bottles come from - the first stack in a_stacks that is
    // not favorited and holds bottles, else the first favorited one; min(a_doses, its count) of
    // them. nullopt when a_doses <= 0 or no stack holds a bottle.
    std::optional<StackTake> NextDoseStack(const std::vector<BottleStack>& a_stacks, std::int32_t a_doses);

    // True when never seen (a_lastSeconds < 0) or a_nowSeconds - a_lastSeconds >= a_minInterval.
    bool IntervalElapsed(double a_lastSeconds, double a_nowSeconds, float a_minInterval);
}
