#include "core/NpcSpellsSlots.h"
#include <cctype>
#include <format>
namespace NpcSpells {
namespace {
    std::string Cap(std::string_view lower) { std::string s(lower); if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0]))); return s; }
    constexpr std::array<std::string_view, 4> kVariantNames{ "All", "Prefer", "NecroPrefer", "NoUndeadAll" };
    constexpr std::array<School, 2> kNonMageSchools{ School::Restoration, School::Alteration };
    constexpr std::size_t kChunkSuffixLen = 3;   // "_C0"
}
std::vector<Variant> VariantsFor(School school, std::uint8_t group)
{
    const bool none = group == kGroupNone;
    if (school != School::Conjuration) return none ? std::vector<Variant>{ Variant::All } : std::vector<Variant>{ Variant::All, Variant::Prefer };
    switch (kGroups[group].policy) {
    case UndeadGroupPolicy::Mixed: return none ? std::vector<Variant>{ Variant::All, Variant::NecroPrefer, Variant::NoUndeadAll }
                                               : std::vector<Variant>{ Variant::All, Variant::Prefer, Variant::NecroPrefer, Variant::NoUndeadAll };
    case UndeadGroupPolicy::AlwaysNecro: return { Variant::All, Variant::NecroPrefer };
    case UndeadGroupPolicy::NeverUndead: return { Variant::All, Variant::Prefer };
    }
    return { Variant::All };
}
std::string EditorIdFor(const SlotKey& k, std::uint8_t chunk)
{
    const std::string base = k.nonMage ? std::format("HoMNS_{}_{}_NonMage", Cap(kSchoolNames[static_cast<std::size_t>(k.school)]), kGroups[k.group].name)
        : std::format("HoMNS_{}_{}_{}_R{}", Cap(kSchoolNames[static_cast<std::size_t>(k.school)]), kGroups[k.group].name, kVariantNames[static_cast<std::size_t>(k.variant)], k.rung + 1);
    return std::format("{}_C{}", base, chunk);
}
const std::vector<SlotDef>& SlotTable()
{
    static const std::vector<SlotDef> table = [] {
        std::vector<SlotDef> t;
        for (std::size_t s = 0; s < kSchoolCount; ++s)
            for (std::uint8_t g = 0; g < kGroups.size(); ++g)
                for (Variant v : VariantsFor(static_cast<School>(s), g))
                    for (std::uint8_t r = 0; r < kRungCount; ++r) {
                        SlotKey k{ static_cast<School>(s), g, v, r, false };
                        std::string id = EditorIdFor(k, 0); id.resize(id.size() - kChunkSuffixLen);
                        t.push_back({ k, id });
                    }
        for (std::uint8_t g = 0; g < kGroups.size(); ++g)
            for (School s : kNonMageSchools) { SlotKey k{ s, g, Variant::All, 0, true }; std::string id = EditorIdFor(k, 0); id.resize(id.size() - kChunkSuffixLen); t.push_back({ k, id }); }
        return t;
    }();
    return table;
}
std::optional<std::size_t> FindSlot(const SlotKey& key)
{
    const auto& t = SlotTable();
    for (std::size_t i = 0; i < t.size(); ++i) if (t[i].key == key) return i;
    return std::nullopt;
}
}
