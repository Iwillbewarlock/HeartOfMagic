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

    // True when never seen (a_lastSeconds < 0) or a_nowSeconds - a_lastSeconds >= a_minInterval.
    bool IntervalElapsed(double a_lastSeconds, double a_nowSeconds, float a_minInterval);
}
