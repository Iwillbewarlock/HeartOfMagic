#include "core/NpcSpellsRules.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
namespace NpcSpells {
namespace {
    constexpr std::size_t kMaxLocalHexDigits = 6;
    std::string Lower(std::string_view s) { std::string r(s); for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return r; }
    FormRef Ref(const char* text) { std::string e; return *ParseFormRef(text, e); }
    template <class T> void Read(const nlohmann::json& j, const char* key, T& out) { if (auto it = j.find(key); it != j.end()) out = it->get<T>(); }
    // A bad entry drops that entry only (section 7); the rest of the list stays
    void ReadRefs(const nlohmann::json& j, const char* key, std::vector<FormRef>& out, std::string& error) {
        auto it = j.find(key); if (it == j.end() || !it->is_array()) return;
        out.clear();
        for (const auto& v : *it) { if (auto ref = ParseFormRef(v.get<std::string>(), error)) out.push_back(*ref); }
    }
    void ReadRef(const nlohmann::json& j, const char* key, FormRef& out, std::string& error) {
        if (auto it = j.find(key); it != j.end() && it->is_string()) if (auto ref = ParseFormRef(it->get<std::string>(), error)) out = *ref;
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
bool ParseRules(const std::string& jsonText, Rules& out, std::string& error)
{
    nlohmann::json j = nlohmann::json::parse(jsonText, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { error = "rules JSON is not an object"; return false; }
    out = DefaultRules();
    if (auto t = j.find("tierTarget"); t != j.end())
        for (std::size_t i = 0; i < kTierCount; ++i) Read(*t, std::string(kTierNames[i]).c_str(), out.tierTarget[i]);
    if (auto l = j.find("ladder"); l != j.end() && l->is_array()) {
        out.ladder.clear();
        for (const auto& rung : *l) {
            const auto tier = TierFromName(rung.value("startTier", "novice"));
            if (!tier) { error = "ladder: unknown tier"; return false; }
            out.ladder.push_back({ *tier, rung.value("below", false), rung.value("chance", 1.0) });
        }
        // The slot table has kRungCount rungs; InSlot/BuildLists index rules.ladder[rung] for each
        if (out.ladder.size() != kRungCount) { error = "ladder must have 5 rungs"; return false; }
    }
    Read(j, "hybridScale", out.hybridScale); Read(j, "nonMageChance", out.nonMageChance); Read(j, "nonMageTags", out.nonMageTags);
    if (auto c = j.find("classTiers"); c != j.end()) {
        double mage = 0.6, hybrid = 0.3; Read(*c, "mageMinMagicShare", mage); Read(*c, "hybridMinMagicShare", hybrid);
        out.mageMinMagicShareTenths = static_cast<int>(std::lround(mage * 10)); out.hybridMinMagicShareTenths = static_cast<int>(std::lround(hybrid * 10));
    }
    if (auto c = j.find("conjuration"); c != j.end()) { ReadRefs(*c, "necromancerClasses", out.necromancerClasses, error); Read(*c, "share", out.conjurationShare); }
    Read(j, "excludeTags", out.excludeTags); Read(j, "excludeSpells", out.excludeSpells);
    ReadRefs(j, "beastRaces", out.beastRaces, error);
    if (auto g = j.find("groups"); g != j.end() && g->is_array()) {
        for (const auto& gj : *g) {
            const std::string name = gj.value("name", "");
            auto it = std::find_if(out.groups.begin(), out.groups.end(), [&](const GroupRule& r) { return r.name == name; });
            if (it == out.groups.end()) continue;   // unknown group names are logged by the caller (built-in slot table)
            ReadRefs(gj, "factions", it->factions, error); Read(gj, "prefer", it->prefer); Read(gj, "preferShare", it->preferShare); Read(gj, "never", it->never);
        }
    }
    if (auto u = j.find("undead"); u != j.end()) ReadRefs(*u, "exceptNpcs", out.undeadExceptNpcs, error);
    if (auto rg = j.find("redguard"); rg != j.end()) ReadRef(*rg, "race", out.redguardRace, error);
    if (auto k = j.find("keywords"); k != j.end()) { ReadRef(*k, "npc", out.keywordNpc, error); ReadRef(*k, "creature", out.keywordCreature, error); ReadRef(*k, "animal", out.keywordAnimal, error); }
    return true;
}
}
