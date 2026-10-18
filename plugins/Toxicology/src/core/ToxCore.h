#pragma once

// =============================================================================
// Toxicology core - pure C++ (no RE/SKSE types), shared by the DLL and
// tools/toxicology-test: settings, the poison spell list from the librarian's
// catalog, the top favorited poison, the magicka-per-dose count, the immunity
// window after a poison wears off, the interval check the Magicka drain
// queue uses, and whether the quest reward gives the tome (with the co-saved
// flag's record).
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
        float magickaPerDose = 100.0f;
        float immunityMultiplier = 1.0f;  // spec 5.4: immunity = max(D x this, minImmunitySeconds)
        float minImmunitySeconds = 5.0f;
    };

    // A missing key keeps its default silently. A key of the wrong type, a
    // magickaPerDose <= 0, a negative immunityMultiplier or minImmunitySeconds, or
    // a non-finite number keeps the default and adds one warning naming the key and
    // the value. Other keys are ignored silently, minIntervalSeconds included
    // (removed 2026-10-18; older files still have it).
    Settings ParseSettings(const nlohmann::json& a_json, std::vector<std::string>& a_warnings);

    // Spec 5.4: the immunity after a poison of a_durationSeconds wears off,
    // max(D x a_multiplier, a_minImmunity). A negative or non-finite D counts as 0;
    // a negative or non-finite multiplier or minimum counts as 0.
    double ImmunitySeconds(double a_durationSeconds, float a_multiplier, float a_minImmunity);

    // Spec 5.4: when the poison may be cast on the same target again,
    // a_appliedAt + D + ImmunitySeconds(D, ...), D sanitized as in ImmunitySeconds.
    double ReapplyAt(double a_appliedAt, double a_durationSeconds, float a_multiplier, float a_minImmunity);

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

    // A-Z lowered, every other byte (UTF-8 lead and trail bytes too) kept: the name order PickTopPoison uses
    std::string LowerAscii(std::string_view a_text);

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

    // The tome as the quest reward (game/ToxReward.h): what one grant attempt does
    enum class TomeRewardAction
    {
        kGrant,          // give one tome, then mark the reward satisfied
        kMarkSatisfied,  // nothing given: the player already knows the ability or holds a tome
        kSkip,           // nothing given, the flag unchanged: "enabled" is off, or the reward is already satisfied
        kRetryLater      // nothing given, the flag unchanged: the player or a form is missing
    };

    // The inputs, read on the game's main thread just before the decision
    struct TomeRewardInputs
    {
        bool formsPresent = false;     // the player, the tome, the ability and the quest
        bool enabled = false;          // the setting "enabled"
        bool rewardSatisfied = false;  // the co-saved flag
        bool knowsAbility = false;
        std::int32_t tomesHeld = 0;    // in the player's inventory
    };

    // First match: a form missing -> kRetryLater; "enabled" off -> kSkip; already satisfied -> kSkip;
    // the ability known or a tome held -> kMarkSatisfied; otherwise kGrant.
    TomeRewardAction DecideTomeReward(const TomeRewardInputs& a_in);

    // "Grant", "MarkSatisfied", "Skip", "RetryLater" (for the log)
    const char* TomeRewardActionName(TomeRewardAction a_action);

    // The co-save record TOMG's data (game/ToxSave.h): exactly one byte, 0 or 1. nullopt for any
    // other length or byte.
    std::optional<bool> ParseRewardFlag(const std::vector<std::uint8_t>& a_data);
}
