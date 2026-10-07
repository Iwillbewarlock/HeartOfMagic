#include "NpcSpellsCheck.h"
#include "core/NpcSpellsSlots.h"
#include <algorithm>
#include <string>
#include <vector>
using namespace NpcSpells; using NpcSpellsCheck::Check;
int RunEslSelfTest()
{
    std::printf("slots\n");
    const auto& table = SlotTable();
    Check(table.size() == 307, "307 slots (220 non-conjuration + 75 conjuration + 12 non-mage)");
    Check(LocalFormId(table.size() - 1, kMaxChunks - 1) == 0xB98, "last record is local 0xB98");
    Check(LocalFormId(0, 0) == 0x800, "first record is local 0x800");
    Check(VariantsFor(School::Destruction, kGroupNone).size() == 1 && VariantsFor(School::Conjuration, 1).size() == 4 && VariantsFor(School::Conjuration, 4).size() == 2, "variant sets per school and group");
    const auto thalmorPrefer = FindSlot({ School::Destruction, 4, Variant::Prefer, 1, false });
    Check(thalmorPrefer && table[*thalmorPrefer].editorId == "HoMNS_Destruction_Thalmor_Prefer_R2", "EditorID names school, group, variant and rung");
    Check(thalmorPrefer && EditorIdFor(table[*thalmorPrefer].key, 2) == "HoMNS_Destruction_Thalmor_Prefer_R2_C2", "chunk suffix");
    Check(!FindSlot({ School::Destruction, kGroupNone, Variant::Prefer, 0, false }), "no Prefer slot for the None group");
    Check(FindSlot({ School::Restoration, 2, Variant::All, 0, true }).has_value(), "non-mage Restoration slot exists for Vampire");
    std::vector<std::string> ids; for (const auto& s : table) ids.push_back(s.editorId);
    std::sort(ids.begin(), ids.end());
    Check(std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "EditorIDs are unique");
    return NpcSpellsCheck::g_failed;   // Task 6 continues
}
