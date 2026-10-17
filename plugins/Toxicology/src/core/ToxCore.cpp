#include "core/ToxCore.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>

namespace Toxicology
{
    namespace
    {
        constexpr int kCatalogVersion = 1;
        constexpr std::string_view kPoisonElement = "poison";

        void Warn(std::vector<std::string>& a_warnings, std::string_view a_key, const nlohmann::json& a_value, std::string_view a_why)
        {
            a_warnings.push_back(std::string(a_key) + ": " + a_value.dump() + " " + std::string(a_why) + ", keeping the default");
        }

        void ReadBool(const nlohmann::json& a_json, std::string_view a_key, bool& a_out, std::vector<std::string>& a_warnings)
        {
            const auto it = a_json.find(a_key);
            if (it == a_json.end()) return;
            if (!it->is_boolean()) {
                Warn(a_warnings, a_key, *it, "is not true or false");
                return;
            }
            a_out = it->get<bool>();
        }

        // A finite number above 0 (a dose size)
        void ReadPositiveNumber(const nlohmann::json& a_json, std::string_view a_key, float& a_out,
            std::vector<std::string>& a_warnings)
        {
            const auto it = a_json.find(a_key);
            if (it == a_json.end()) return;
            if (!it->is_number()) {
                Warn(a_warnings, a_key, *it, "is not a number");
                return;
            }
            const auto value = static_cast<float>(it->get<double>());
            if (!std::isfinite(value)) {
                Warn(a_warnings, a_key, *it, "is not a finite number");
                return;
            }
            if (value <= 0.0f) {
                Warn(a_warnings, a_key, *it, "is not above 0");
                return;
            }
            a_out = value;
        }

        bool HasPoisonElement(const nlohmann::json& a_entry)
        {
            if (!a_entry.is_object()) return false;
            const auto it = a_entry.find("elements");
            if (it == a_entry.end() || !it->is_array()) return false;
            return std::any_of(it->begin(), it->end(),
                [](const nlohmann::json& a_element) { return a_element.is_string() && a_element.get<std::string>() == kPoisonElement; });
        }
    }

    Settings ParseSettings(const nlohmann::json& a_json, std::vector<std::string>& a_warnings)
    {
        Settings settings;
        if (!a_json.is_object()) {
            a_warnings.push_back("settings: " + a_json.dump() + " is not an object, keeping every default");
            return settings;
        }
        ReadBool(a_json, "enabled", settings.enabled, a_warnings);
        ReadBool(a_json, "showIcon", settings.showIcon, a_warnings);
        // "minIntervalSeconds" (removed 2026-10-18, spec 5.4) is not read: an older file that has it loads silently
        ReadPositiveNumber(a_json, "magickaPerDose", settings.magickaPerDose, a_warnings);
        return settings;
    }

    std::vector<std::string> PoisonSpellKeys(const nlohmann::json& a_catalog, std::string& a_error)
    {
        std::vector<std::string> keys;
        if (!a_catalog.is_object()) {
            a_error = "the catalog is not a JSON object";
            return keys;
        }
        const auto version = a_catalog.find("version");
        if (version == a_catalog.end() || !version->is_number_integer() || version->get<std::int64_t>() != kCatalogVersion) {
            a_error = "catalog version is " + (version == a_catalog.end() ? std::string("missing") : version->dump()) +
                      ", this addon reads version " + std::to_string(kCatalogVersion);
            return keys;
        }
        const auto spells = a_catalog.find("spells");
        if (spells == a_catalog.end() || !spells->is_object()) {
            a_error = "catalog \"spells\" is missing or not an object";
            return keys;
        }
        for (const auto& [key, entry] : spells->items()) {
            if (HasPoisonElement(entry)) keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    std::string LowerAscii(std::string_view a_text)
    {
        std::string out(a_text);
        for (auto& c : out) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        return out;
    }

    std::optional<std::size_t> PickTopPoison(const std::vector<PoisonCandidate>& a_candidates)
    {
        std::optional<std::size_t> best;
        std::string bestName;
        for (std::size_t i = 0; i < a_candidates.size(); ++i) {
            const auto& candidate = a_candidates[i];
            if (!candidate.favorited || candidate.count <= 0) continue;
            // std::string compares bytes as unsigned char, so UTF-8 names sort after ASCII ones
            auto name = LowerAscii(candidate.name);
            if (!best || name < bestName || (name == bestName && candidate.formId < a_candidates[*best].formId)) {
                best = i;
                bestName = std::move(name);
            }
        }
        return best;
    }

    // Mirrors the split in SpellScanner's ResolveWith (plugins/spelllearning/src/spellscanner/SpellScannerFormId.cpp),
    // stricter: the whole rest must be hex and fit 24 bits. Not shared: that file also holds the tree validation,
    // which pulls in the scanner's gate and voice-slot code, so it cannot be compiled into the addon alone.
    std::optional<CatalogKey> SplitCatalogKey(std::string_view a_key)
    {
        constexpr std::string_view kHexPrefixLower = "0x";
        constexpr std::string_view kHexPrefixUpper = "0X";
        constexpr int kHexBase = 16;

        const auto bar = a_key.find('|');
        if (bar == std::string_view::npos || bar == 0) return std::nullopt;
        auto hex = a_key.substr(bar + 1);
        if (hex.starts_with(kHexPrefixLower) || hex.starts_with(kHexPrefixUpper)) {
            hex.remove_prefix(kHexPrefixLower.size());
        }
        CatalogKey out{ std::string(a_key.substr(0, bar)), 0 };
        const auto [end, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), out.local, kHexBase);
        if (hex.empty() || ec != std::errc{} || end != hex.data() + hex.size() || out.local > kMaxCatalogLocalId) {
            return std::nullopt;
        }
        return out;
    }

    DoseResult ConsumeMagicka(float a_accumulated, float a_spent, float a_perDose)
    {
        const double accumulated = (std::isfinite(a_accumulated) && a_accumulated > 0.0f) ? a_accumulated : 0.0;
        const double spent = (std::isfinite(a_spent) && a_spent > 0.0f) ? a_spent : 0.0;
        const double total = accumulated + spent;
        if (!std::isfinite(a_perDose) || a_perDose <= 0.0f || total <= 0.0) {
            return { 0, static_cast<float>(total) };
        }
        const double whole = std::min(std::floor(total / a_perDose), static_cast<double>(std::numeric_limits<std::int32_t>::max()));
        const double remainder = std::max(total - whole * a_perDose, 0.0);
        return { static_cast<std::int32_t>(whole), static_cast<float>(remainder) };
    }

    std::optional<StackTake> NextDoseStack(const std::vector<BottleStack>& a_stacks, std::int32_t a_doses)
    {
        if (a_doses <= 0) return std::nullopt;
        for (const bool favorited : { false, true }) {
            for (std::size_t i = 0; i < a_stacks.size(); ++i) {
                const auto& stack = a_stacks[i];
                if (stack.favorited == favorited && stack.count > 0) {
                    return StackTake{ i, std::min(a_doses, stack.count) };
                }
            }
        }
        return std::nullopt;
    }

    PoisonSets BuildPoisonSets(const std::vector<PoisonSpellNode>& a_catalog)
    {
        PoisonSets sets;
        std::unordered_set<std::uint32_t> seen;
        const auto take = [&](std::uint32_t a_formId, bool a_hostile, std::size_t& a_count) {
            if (!seen.insert(a_formId).second) return;
            ++a_count;
            if (a_hostile) {
                sets.hit.insert(a_formId);
            } else {
                ++sets.notHostile;
            }
        };
        // Catalog spells first, so a sub-spell that is also a catalog spell counts as a catalog spell
        for (const auto& spell : a_catalog) {
            take(spell.formId, spell.hostile, sets.catalogSpells);
            if (spell.hostile) sets.cost.insert(spell.formId);
        }
        for (const auto& spell : a_catalog) {
            for (const auto& sub : spell.cloakSubs) {
                take(sub.formId, sub.hostile, sets.cloakSubSpells);
                if (sub.hostile) sets.cost.insert(spell.formId);
            }
        }
        return sets;
    }

    bool IntervalElapsed(double a_lastSeconds, double a_nowSeconds, float a_minInterval)
    {
        if (a_lastSeconds < 0.0) return true;
        return a_nowSeconds - a_lastSeconds >= a_minInterval;
    }
}
