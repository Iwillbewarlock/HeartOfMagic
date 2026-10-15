#pragma once
#include "core/NpcSpellsTypes.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Slot table shared by the ESL writer and the DLL. Slots = 307:
//   non-Conjuration 4 schools x (None 1 + 5 factions x 2 variants) = 44 rows x 5 rungs = 220
//   Conjuration (None 3 + Forsworn 4 + Vampire 2 + Necromancer 2 + Thalmor 2 + Vigilant 2) = 15 rows x 5 rungs = 75
//   non-mage lists 6 groups x 2 schools (Restoration, Alteration), 1 rung = 12
// ESL records = 307 x kMaxChunks (3) = 921, local FormIDs 0x800..0xB98. Changing kMaxChunks or the
// enumeration order renumbers every FormID: new slots append only.
namespace NpcSpells {
    enum class UndeadGroupPolicy : std::uint8_t { Mixed, AlwaysNecro, NeverUndead };
    struct GroupDef { std::string_view name; UndeadGroupPolicy policy; };
    inline constexpr std::array<GroupDef, 6> kGroups{ { { "None", UndeadGroupPolicy::Mixed }, { "Forsworn", UndeadGroupPolicy::Mixed }, { "Vampire", UndeadGroupPolicy::AlwaysNecro },
        { "Necromancer", UndeadGroupPolicy::AlwaysNecro }, { "Thalmor", UndeadGroupPolicy::NeverUndead }, { "Vigilant", UndeadGroupPolicy::NeverUndead } } };
    inline constexpr std::uint8_t kGroupNone = 0;
    enum class Variant : std::uint8_t { All, Prefer, NecroPrefer, NoUndeadAll };
    struct SlotKey { School school; std::uint8_t group; Variant variant; std::uint8_t rung; bool nonMage; bool operator==(const SlotKey&) const = default; };
    struct SlotDef { SlotKey key; std::string editorId; };
    inline constexpr std::uint32_t kFirstLocalFormId = 0x800;
    inline constexpr std::uint8_t kMaxChunks = 3;        // renumbers every FormID if changed
    const std::vector<SlotDef>& SlotTable();             // built once, append-only order
    // Linear scan over 307 slots; fine for load-time use, index it if it ever lands on a hot path.
    std::optional<std::size_t> FindSlot(const SlotKey& key);
    constexpr std::uint32_t LocalFormId(std::size_t slotIndex, std::uint8_t chunk) { return kFirstLocalFormId + static_cast<std::uint32_t>(slotIndex) * kMaxChunks + chunk; }
    std::vector<Variant> VariantsFor(School school, std::uint8_t group);
    std::string EditorIdFor(const SlotKey& key, std::uint8_t chunk);
}
