#include "NpcSpellsCheck.h"
#include "core/NpcSpellsEsl.h"
#include "core/NpcSpellsSlots.h"
#include <algorithm>
#include <cstring>
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
    std::printf("esl\n");
    const auto bytes = BuildEslBytes(table);
    auto u32 = [&](std::size_t at) { return std::uint32_t(bytes[at]) | std::uint32_t(bytes[at + 1]) << 8 | std::uint32_t(bytes[at + 2]) << 16 | std::uint32_t(bytes[at + 3]) << 24; };
    auto u16 = [&](std::size_t at) { return std::uint16_t(bytes[at] | bytes[at + 1] << 8); };
    auto tag = [&](std::size_t at, const char* t) { return std::memcmp(&bytes[at], t, 4) == 0; };
    Check(tag(0, "TES4") && (u32(8) & 0x200) != 0, "TES4 header carries the ESL flag");
    const std::size_t tes4Size = u32(4); std::size_t p = 24; bool hedr = false, mast = false, data = false, cnam = false;
    while (p < 24 + tes4Size) { const std::uint16_t n = u16(p + 4);
        if (tag(p, "HEDR")) { hedr = n == 12 && u32(p + 10) == table.size() * kMaxChunks + kTopGroupCount && u32(p + 14) == kFirstLocalFormId + table.size() * kMaxChunks; }
        if (tag(p, "CNAM")) cnam = true; if (tag(p, "MAST")) mast = std::string(reinterpret_cast<const char*>(&bytes[p + 6])) == "Skyrim.esm"; if (tag(p, "DATA")) data = n == 8;
        p += 6 + n; }
    Check(hedr && cnam && mast && data, "HEDR (records + groups = 922, next object id), CNAM, MAST Skyrim.esm, DATA");
    Check(tag(p, "GRUP") && tag(p + 8, "LVSP"), "one top group LVSP");
    std::size_t rec = p + 24; std::size_t count = 0; bool idsOk = true, bodies = true;
    while (rec < bytes.size()) {
        const std::uint32_t size = u32(rec + 4), formId = u32(rec + 12);
        idsOk = idsOk && tag(rec, "LVSP") && formId == (kOwnModIndexInFile | LocalFormId(count / kMaxChunks, static_cast<std::uint8_t>(count % kMaxChunks))) && u16(rec + 20) == kFormVersion && u32(rec + 8) == 0;
        std::size_t q = rec + 24; bool edid = false, obnd = false, lvld = false, lvlf = false, llct = false;
        while (q < rec + 24 + size) { const std::uint16_t n = u16(q + 4);
            if (tag(q, "EDID")) edid = std::string(reinterpret_cast<const char*>(&bytes[q + 6])) == EditorIdFor(table[count / kMaxChunks].key, static_cast<std::uint8_t>(count % kMaxChunks));
            if (tag(q, "OBND")) obnd = n == 12; if (tag(q, "LVLD")) lvld = n == 1 && bytes[q + 6] == 0; if (tag(q, "LVLF")) lvlf = n == 1; if (tag(q, "LLCT")) llct = n == 1 && bytes[q + 6] == 0;
            q += 6 + n; }
        bodies = bodies && edid && obnd && lvld && lvlf && llct;
        rec += 24 + size; ++count;
    }
    Check(count == table.size() * kMaxChunks && count == 921, "921 LVSP records (slots x kMaxChunks)");
    Check(idsOk, "in-file FormIDs are 0x01000800.. in table order, form version 44, uncompressed");
    Check(bodies, "each record: EDID, OBND(12), LVLD 0, LVLF, LLCT 0");
    return NpcSpellsCheck::g_failed;
}
