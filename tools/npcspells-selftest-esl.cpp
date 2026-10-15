#include "NpcSpellsCheck.h"
#include "core/NpcSpellsEsl.h"
#include "core/NpcSpellsReport.h"
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
            if (tag(q, "OBND")) obnd = n == 12; if (tag(q, "LVLD")) lvld = n == 1 && bytes[q + 6] == 0; if (tag(q, "LVLF")) lvlf = n == 1 && bytes[q + 6] == 0; if (tag(q, "LLCT")) llct = n == 1 && bytes[q + 6] == 0;
            q += 6 + n; }
        bodies = bodies && edid && obnd && lvld && lvlf && llct;
        rec += 24 + size; ++count;
    }
    Check(count == table.size() * kMaxChunks && count == 921, "921 LVSP records (slots x kMaxChunks)");
    Check(idsOk, "in-file FormIDs are 0x01000800.. in table order, form version 44, uncompressed");
    Check(bodies, "each record: EDID, OBND(12), LVLD 0, LVLF 0, LLCT 0");
    std::printf("report\n");
    ReportData rd; rd.version = "1.2.0"; rd.stopReason.clear(); rd.pool.kept = 1400; rd.build.lists = 600; rd.build.emptySlots = 12; rd.build.droppedOverCapacity = 0;
    rd.eslRecords = 921; rd.listSizes = { { "HoMNS_Destruction_None_All_R1_C0", 240, 0 } }; rd.npcsScanned = 26090; rd.npcsAttached = 2; rd.skipped = { { "template spell list", 14389 } };
    rd.byTier = { 170, 69, 48 }; rd.timingsMs = { { "total", 123.4 } };
    const auto destSlot = *FindSlot({ School::Destruction, kGroupNone, Variant::All, 0, false });
    const auto thalmorSlot = *FindSlot({ School::Destruction, 4, Variant::Prefer, 1, false });
    NpcDecision mage; mage.eligible = true; mage.tier = ClassTier::Mage; mage.undead = UndeadPolicy::Conjurer; mage.lists = { { destSlot, 0 } };
    NpcDecision thalmor; thalmor.eligible = true; thalmor.tier = ClassTier::Hybrid; thalmor.group = 4; thalmor.lists = { { thalmorSlot, 2 } };
    rd.npcs = { { "Skyrim.esm|0x01CB4E", "Bandit Wizard", mage }, { "Skyrim.esm|0x0D8D62", "Thalmor Justiciar", thalmor } };
    const std::string text = RenderReport(rd);
    Check(text.find("HoMNS_Destruction_None_All_R1_C0") != std::string::npos && text.find("240") != std::string::npos, "report lists each list with its entry count");
    Check(text.find("template spell list") != std::string::npos && text.find("14389") != std::string::npos, "report lists skip reasons with counts");
    Check(text.find("total") != std::string::npos && text.find("123.4") != std::string::npos, "report carries timings");
    Check(text.find("Skyrim.esm|0x01CB4E") != std::string::npos && text.find("Bandit Wizard") != std::string::npos && text.find("conjurer") != std::string::npos,
          "every NPC row: persistent id, name, reason");
    Check(text.find("Skyrim.esm|0x0D8D62") != std::string::npos && text.find("group Thalmor") != std::string::npos && text.find("hybrid") != std::string::npos &&
          text.find(EditorIdFor(SlotTable()[thalmorSlot].key, 2)) != std::string::npos, "every NPC row: group and attached list EditorIDs");
    { ReportData pre; pre.listsSkippedPrefilled = 7; pre.build.variantsToppedUp = 31; const std::string t = RenderReport(pre);
      Check(t.find("skippedPrefilled 7") != std::string::npos && t.find("topped up from All 31") != std::string::npos, "report shows skippedPrefilled and topped-up variant lists"); }
    { ReportData heap; heap.listsSkippedNoRecord = 2; heap.listsSkippedNoMemory = 5; heap.pool.duplicates = 4; const std::string t = RenderReport(heap);
      Check(t.find("missing record 2") != std::string::npos && t.find("skippedNoMemory 5") != std::string::npos && t.find("duplicate persistentId 4") != std::string::npos,
            "report counts a game heap failure apart from a missing record, and duplicate tree nodes"); }
    {   // inherited rows: their own section, holder named, list count; counters on the NPCs section
        ReportData inh; inh.npcsAttached = 1; inh.inheritedAttached = 1; inh.inheritedLists = 1; inh.inheriting = 3; inh.beforeHolder = 2; inh.notCopiedLeveledChain = 1;
        inh.npcs = { { "Skyrim.esm|0x039D31", "Bandit Wizard", mage }, { "Skyrim.esm|0x039D37", "Bandit Mage", mage, "Skyrim.esm|0x039D31" } };
        const std::string t = RenderReport(inh);
        const auto own = t.find("== NPC lists ==\n"), inherited = t.find("== NPC lists inherited from a template ==\n");
        const auto row = t.find("Skyrim.esm|0x039D37  Bandit Mage  inherited from Skyrim.esm|0x039D31  (1 lists)");
        Check(own != std::string::npos && inherited != std::string::npos && row != std::string::npos && row > inherited, "an inherited NPC's row names its holder and list count, in its own section");
        Check(t.find("Skyrim.esm|0x039D37", own) > inherited && t.find("Skyrim.esm|0x039D31  Bandit Wizard  group") < inherited, "the holder's row stays in NPC lists, the inherited one is not there");
        Check(t.find("spell list from a template 3: got their holder's lists 1 (1 lists)") != std::string::npos && t.find("met before their holder 2") != std::string::npos &&
              t.find("without kCopiedTemplate 1 (leveled link") != std::string::npos, "the NPCs section counts inherited NPCs and lists, and the kDataLoaded template counters");
    }
    ReportData stopped; stopped.stopReason = "spell_tree.json not found";
    Check(RenderReport(stopped).find("spell_tree.json not found") != std::string::npos, "a stopped run says why");
    return NpcSpellsCheck::g_failed;
}
