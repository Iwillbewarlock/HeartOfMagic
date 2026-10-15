#pragma once
#include "core/NpcSpellsSlots.h"
#include <cstdint>
#include <string_view>
#include <vector>

namespace NpcSpells {
    inline constexpr std::string_view kEslFileName = "HeartOfMagic_NPCSpells.esp";
    inline constexpr std::string_view kEslMaster = "Skyrim.esm";
    inline constexpr std::uint32_t kTes4LightFlag = 0x00000200;
    inline constexpr float kHeaderVersion = 1.70f;          // keeps 0x800..0xFFF valid on 1.5.97
    inline constexpr std::uint16_t kFormVersion = 44;
    inline constexpr std::uint32_t kOwnModIndexInFile = 0x01000000;   // one master -> own records are index 1 in the file
    inline constexpr std::uint32_t kTopGroupCount = 1;                // HEDR numRecords counts records and groups (one GRUP LVSP)
    // TES4 (HEDR, CNAM "HeartOfMagic", MAST, DATA) + GRUP LVSP with one empty record per (slot, chunk):
    // EDID, OBND (12 zero bytes), LVLD 0, LVLF 0, LLCT 0. Uncompressed. The DLL fills entries and LVLF (llFlags) at runtime.
    std::vector<unsigned char> BuildEslBytes(const std::vector<SlotDef>& slots);
}
