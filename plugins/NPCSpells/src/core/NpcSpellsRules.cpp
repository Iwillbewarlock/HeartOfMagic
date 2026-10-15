#include "core/NpcSpellsRules.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
namespace NpcSpells {
namespace {
    constexpr std::size_t kMaxLocalHexDigits = 6;
    constexpr double kMinShare = 0.0, kMaxShare = 1.0;   // chances, shares and scales are fractions
    constexpr double kTenthsPerUnit = 10.0;              // classTiers shares are kept in tenths
    std::string Lower(std::string_view s) { std::string r(s); for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return r; }
    FormRef Ref(const char* text) { std::string e; return *ParseFormRef(text, e); }
    template <class T> void Read(const nlohmann::json& j, const char* key, T& out) { if (auto it = j.find(key); it != j.end()) out = it->get<T>(); }
    // Every problem the file has is kept, "; " between them (one bad entry does not hide the next)
    void AppendError(std::string& error, const std::string& part) { if (!error.empty()) error += "; "; error += part; }
    // A bad entry drops that entry only (section 7); the rest of the list stays
    void ReadRefs(const nlohmann::json& j, const char* key, std::vector<FormRef>& out, std::string& error) {
        auto it = j.find(key); if (it == j.end() || !it->is_array()) return;
        out.clear();
        for (const auto& v : *it) {
            if (!v.is_string()) { AppendError(error, std::format("{}: not a FormID (XXXXXX:Plugin.ext): {}", key, v.dump())); continue; }
            std::string refError;
            if (auto ref = ParseFormRef(v.get<std::string>(), refError)) out.push_back(*ref); else AppendError(error, std::format("{}: {}", key, refError));
        }
    }
    void ReadRef(const nlohmann::json& j, const char* key, FormRef& out, std::string& error) {
        auto it = j.find(key); if (it == j.end()) return;
        if (!it->is_string()) { AppendError(error, std::format("{}: not a FormID (XXXXXX:Plugin.ext): {}", key, it->dump())); return; }
        std::string refError;
        if (auto ref = ParseFormRef(it->get<std::string>(), refError)) out = *ref; else AppendError(error, std::format("{}: {}", key, refError));
    }
    // A fraction outside 0..1 goes back to its default, with an error (not clamped: the file says something else was meant)
    void CheckShare(double& value, double fallback, const std::string& name, std::string& error) {
        if (value >= kMinShare && value <= kMaxShare) return;
        AppendError(error, std::format("{} {} is outside 0..1, the default {} is used", name, value, fallback));
        value = fallback;
    }
}
std::optional<FormRef> ParseFormRef(std::string_view text, std::string& error)
{
    const auto colon = text.find(':');
    const auto hex = colon == std::string_view::npos ? std::string_view{} : text.substr(0, colon);
    const bool allHex = !hex.empty() && hex.size() <= kMaxLocalHexDigits && std::all_of(hex.begin(), hex.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
    if (!allHex || colon + 1 >= text.size()) { error = "not a FormID (XXXXXX:Plugin.ext): " + std::string(text); return std::nullopt; }
    FormRef ref;
    ref.pluginLower = Lower(text.substr(colon + 1));
    ref.local = static_cast<std::uint32_t>(std::stoul(std::string(hex), nullptr, 16));
    return ref;
}
Rules DefaultRules()
{
    Rules r;
    r.ladder = { { Tier::Novice, false, 1.0 }, { Tier::Apprentice, true, 0.25 }, { Tier::Adept, true, 0.25 }, { Tier::Expert, true, 0.5 }, { Tier::Master, false, 0.5 } };
    r.necromancerClasses = { Ref("0C969F:Skyrim.esm") };
    r.beastRaces = { Ref("0CDD84:Skyrim.esm"), Ref("00283A:Dawnguard.esm") };
    r.groups = {
        { "Forsworn", { Ref("043599:Skyrim.esm"), Ref("04359E:Skyrim.esm") }, { "nature", "earth", "flesh", "fire", "curse" }, 0.75, { "sun" } },
        { "Vampire", { Ref("027242:Skyrim.esm"), Ref("003376:Dawnguard.esm") }, { "siphon", "health", "shadow", "frost", "undead" }, 0.75, { "sun", "light", "curing" } },
        { "Necromancer", { Ref("034B74:Skyrim.esm") }, { "undead", "soul", "frost", "siphon" }, 0.75, { "sun" } },
        { "Thalmor", { Ref("039F26:Skyrim.esm"), Ref("0B80F7:Skyrim.esm"), Ref("0135A0:Skyrim.esm") }, { "shock", "fire", "arcane", "light", "shield" }, 0.75, { "undead" } },
        { "Vigilant", { Ref("0B3292:Skyrim.esm") }, { "sun", "light", "curing", "shield" }, 0.75, { "undead", "daedra", "shadow" } },
    };
    r.undeadExceptNpcs = { Ref("0135E9:Skyrim.esm") };   // Falion
    r.redguardRace = Ref("013748:Skyrim.esm");
    r.keywordNpc = Ref("013794:Skyrim.esm"); r.keywordCreature = Ref("013795:Skyrim.esm"); r.keywordAnimal = Ref("013798:Skyrim.esm");
    return r;
}
namespace {
// classTiers: each share 0..1, and the hybrid floor not above the mage floor (else both go back to the defaults)
void ReadClassTiers(const nlohmann::json& c, Rules& out, std::string& error)
{
    const Rules defaults = DefaultRules();
    const double defaultMage = defaults.mageMinMagicShareTenths / kTenthsPerUnit, defaultHybrid = defaults.hybridMinMagicShareTenths / kTenthsPerUnit;
    double mage = defaultMage, hybrid = defaultHybrid; Read(c, "mageMinMagicShare", mage); Read(c, "hybridMinMagicShare", hybrid);
    CheckShare(mage, defaultMage, "classTiers.mageMinMagicShare", error); CheckShare(hybrid, defaultHybrid, "classTiers.hybridMinMagicShare", error);
    if (hybrid > mage) {
        AppendError(error, std::format("classTiers.hybridMinMagicShare {} is above mageMinMagicShare {}, the defaults {} and {} are used", hybrid, mage, defaultHybrid, defaultMage));
        mage = defaultMage; hybrid = defaultHybrid;
    }
    out.mageMinMagicShareTenths = static_cast<int>(std::lround(mage * kTenthsPerUnit)); out.hybridMinMagicShareTenths = static_cast<int>(std::lround(hybrid * kTenthsPerUnit));
}
bool ParseRulesImpl(const std::string& jsonText, Rules& out, std::string& error)
{
    nlohmann::json j = nlohmann::json::parse(jsonText, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { error = "rules JSON is not an object"; return false; }
    out = DefaultRules();
    const Rules defaults = DefaultRules();
    if (auto l = j.find("ladder"); l != j.end() && l->is_array()) {
        out.ladder.clear();
        for (const auto& rung : *l) {
            const auto tier = TierFromName(rung.value("startTier", "novice"));
            if (!tier) { error = "ladder: unknown tier"; return false; }
            out.ladder.push_back({ *tier, rung.value("below", false), rung.value("chance", 1.0) });
        }
        // The slot table has kRungCount rungs; InSlot/BuildLists index rules.ladder[rung] for each
        if (out.ladder.size() != kRungCount) { error = "ladder must have 5 rungs"; return false; }
        for (std::size_t i = 0; i < kRungCount; ++i) CheckShare(out.ladder[i].chance, defaults.ladder[i].chance, std::format("ladder[{}].chance", i), error);
    }
    Read(j, "hybridScale", out.hybridScale); Read(j, "nonMageChance", out.nonMageChance); Read(j, "nonMageTags", out.nonMageTags);
    CheckShare(out.hybridScale, defaults.hybridScale, "hybridScale", error); CheckShare(out.nonMageChance, defaults.nonMageChance, "nonMageChance", error);
    if (auto c = j.find("classTiers"); c != j.end()) ReadClassTiers(*c, out, error);
    if (auto c = j.find("conjuration"); c != j.end()) {
        ReadRefs(*c, "necromancerClasses", out.necromancerClasses, error); Read(*c, "share", out.conjurationShare);
        CheckShare(out.conjurationShare, defaults.conjurationShare, "conjuration.share", error);
    }
    Read(j, "excludeTags", out.excludeTags); Read(j, "excludeSpells", out.excludeSpells);
    ReadRefs(j, "beastRaces", out.beastRaces, error);
    if (auto g = j.find("groups"); g != j.end() && g->is_array()) {
        for (const auto& gj : *g) {
            const std::string name = gj.value("name", "");
            auto it = std::find_if(out.groups.begin(), out.groups.end(), [&](const GroupRule& r) { return r.name == name; });
            if (it == out.groups.end()) continue;   // unknown group names are logged by the caller (built-in slot table)
            const double defaultPreferShare = it->preferShare;
            ReadRefs(gj, "factions", it->factions, error); Read(gj, "prefer", it->prefer); Read(gj, "preferShare", it->preferShare); Read(gj, "never", it->never);
            CheckShare(it->preferShare, defaultPreferShare, "groups." + name + ".preferShare", error);
        }
    }
    if (auto u = j.find("undead"); u != j.end()) ReadRefs(*u, "exceptNpcs", out.undeadExceptNpcs, error);
    if (auto rg = j.find("redguard"); rg != j.end()) ReadRef(*rg, "race", out.redguardRace, error);
    if (auto k = j.find("keywords"); k != j.end()) { ReadRef(*k, "npc", out.keywordNpc, error); ReadRef(*k, "creature", out.keywordCreature, error); ReadRef(*k, "animal", out.keywordAnimal, error); }
    return true;
}
}
// A wrong-typed value (a string where a number belongs, a non-object rung) is malformed input: false + error, never a throw out of data load
bool ParseRules(const std::string& jsonText, Rules& out, std::string& error)
{
    try { return ParseRulesImpl(jsonText, out, error); }
    catch (const nlohmann::json::exception& e) { error = std::string("rules JSON has a wrong-typed value: ") + e.what(); return false; }
}
}
