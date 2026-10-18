// =============================================================================
// SELF-TEST - the decal texture set slots by data: T0-decal on the slot route and E3-slots (SelfTestSlotDecal.h)
// =============================================================================

#include "SelfTestSlotDecal.h"

#include "Config.h"
#include "DataPathRule.h"
#include "FormCopies.h"
#include "SelfTestLog.h"
#include "SlotAssign.h"
#include "SlotTxst.h"

#include <cstring>
#include <format>
#include <set>
#include <vector>

namespace BlackFlame::SelfTest
{
    namespace
    {
        namespace CC = CopyContract;
        constexpr std::size_t kMaxLines = 20;

        [[nodiscard]] RE::FormID IdOf(const RE::TESForm* a_form) noexcept
        {
            return a_form ? a_form->GetFormID() : 0;
        }

        struct Tally
        {
            std::size_t links = 0;     // decal / hazard links judged
            std::size_t toOpenTxst = 0;
            std::size_t wrong = 0;
            std::size_t sets = 0;      // open TXST slots judged
            std::size_t setFaults = 0;
            std::string lines;

            void Fault(const std::string& a_line)
            {
                if (wrong + setFaults <= kMaxLines) {
                    lines += (lines.empty() ? "" : "; ") + a_line;
                }
            }
        };

        [[nodiscard]] bool IsClosedSlot(const RE::TESForm* a_form)
        {
            const auto id = a_form ? SlotAssign::SlotOf(a_form) : std::nullopt;
            return id && !SlotAssign::GateOpen(*id);
        }

        [[nodiscard]] bool IsOpenTxstSlot(const RE::TESForm* a_form)
        {
            const auto id = a_form ? SlotAssign::SlotOf(a_form) : std::nullopt;
            return id && SlotAssign::GateOpen(*id) && SlotAssign::At(*id).type == CC::SlotType::kTXST;
        }

        // a_held: what the open copy holds; a_sourceLink: what its source holds there
        void JudgeLink(Tally& a_tally, const RE::TESForm* a_copy, const char* a_what, const RE::TESForm* a_held, const RE::TESForm* a_sourceLink)
        {
            if (!a_sourceLink) {
                return;
            }
            ++a_tally.links;
            const auto* copy = CopyOf(a_sourceLink);
            const auto* want = copy ? copy : a_sourceLink;
            a_tally.toOpenTxst += IsOpenTxstSlot(a_held) ? 1 : 0;
            if (a_held != want || IsClosedSlot(a_held)) {
                ++a_tally.wrong;
                a_tally.Fault(std::format("{:08X} {}: holds {:08X}{}, expected {:08X}", IdOf(a_copy), a_what, IdOf(a_held),
                    IsClosedSlot(a_held) ? " (a CLOSED slot)" : "", IdOf(want)));
            }
        }

        // One open TXST slot: strings, copy paths, IDs, DODT
        void JudgeTxst(Tally& a_tally, const SlotAssign::Slot& a_slot)
        {
            const auto* set = a_slot.form ? a_slot.form->As<RE::BGSTextureSet>() : nullptr;
            const auto* source = a_slot.source ? a_slot.source->As<RE::BGSTextureSet>() : nullptr;
            if (!set || !source) {
                return;
            }
            ++a_tally.sets;
            std::string faults;
            const auto note = [&](const std::string& a_text) { faults += (faults.empty() ? "" : ", ") + a_text; };
            if (a_slot.publishedTxst.empty()) {
                note("open but never written (the stub's strings)");
            }
            std::size_t copies = 0;
            for (std::size_t i = 0; i < SlotTxstPaths::kSlots; ++i) {
                const char* text = set->textures[i].textureName.c_str();
                const std::string now = text ? text : "";
                const char* own = source->textures[i].textureName.c_str();
                const std::string sourceText = own ? own : "";
                if (!a_slot.publishedTxst.empty() && now != a_slot.publishedTextures[i]) {
                    note(std::format("TX0{} '{}', written '{}'", i, now, a_slot.publishedTextures[i]));
                }
                if (now != sourceText) {
                    const bool colour = i == CC::kTxstDiffuseSlot || i == CC::kTxstGlowSlot;
                    const auto parts = DataPathRule::ParseCopyPath(std::string(DataPathRule::kTexturesRoot) + now);
                    if (!colour || !parts || parts->palette || parts->extension != "dds") {
                        note(std::format("TX0{} '{}' differs from the source's '{}' and is no grey texture copy", i, now, sourceText));
                    } else {
                        ++copies;
                    }
                }
                const auto expected = SlotTxst::ExpectedId(a_slot, i, now);
                if (!expected || std::memcmp(&*expected, &set->textureFileIDs[i], sizeof(RE::BSResource::ID)) != 0) {
                    note(std::format("TX0{}'s textureFileID is not the expected one", i));
                }
            }
            if (!a_slot.publishedTxst.empty() && copies == 0) {
                note("no grey copy in TX00 / TX03");
            }
            if (!set->decalData != !source->decalData) {
                note("DODT presence differs from the source's");
            }
            if (!faults.empty()) {
                ++a_tally.setFaults;
                a_tally.Fault(std::format("TXST slot {:X} ({}): {}", a_slot.localId, a_slot.sourceText, faults));
            }
        }

        void JudgeHolder(Tally& a_tally, const RE::TESForm* a_copy, const RE::TESForm* a_source)
        {
            if (const auto* p = a_copy->As<RE::BGSProjectile>()) {
                JudgeLink(a_tally, a_copy, "PROJ decal", p->data.decalData, a_source->As<RE::BGSProjectile>()->data.decalData);
            } else if (const auto* i = a_copy->As<RE::BGSImpactData>()) {
                const auto& s = *a_source->As<RE::BGSImpactData>();
                JudgeLink(a_tally, a_copy, "IPCT texture set", i->decalTextureSet, s.decalTextureSet);
                JudgeLink(a_tally, a_copy, "IPCT texture set 2", i->decalTextureSet2, s.decalTextureSet2);
                JudgeLink(a_tally, a_copy, "IPCT hazard", i->hazard, s.hazard);
            }
        }

        [[nodiscard]] bool SlotsWanted(const char* a_id)
        {
            if (!GetConfig().selfTestSlots) {
                SelfTestLog::Info(a_id, "off (selfTestSlots false)");
                return false;
            }
            return true;
        }
    }

    bool StepDecalSlots()
    {
        if (!SlotsWanted("T0-decal")) {
            return true;
        }
        Tally tally;
        std::size_t txstAssigned = 0;
        std::size_t txstUsable = 0;
        std::size_t txstOpen = 0;
        for (const auto id : SlotAssign::Assigned()) {
            const auto& slot = SlotAssign::At(id);
            if (slot.type == CC::SlotType::kTXST) {
                ++txstAssigned;
                txstUsable += slot.usable ? 1 : 0;
                if (SlotAssign::GateOpen(id)) {
                    ++txstOpen;
                    JudgeTxst(tally, slot);
                }
                continue;
            }
            if (SlotAssign::GateOpen(id) && slot.form && slot.source) {
                JudgeHolder(tally, slot.form, slot.source);
            }
        }
        SelfTestLog::Info("T0-decal", std::format("slot route: TXST slots assigned {}, usable {}, open {}; {}", txstAssigned, txstUsable,
            txstOpen, SlotTxst::RuleText()));
        if (tally.links == 0 && tally.sets == 0) {
            SelfTestLog::Info("T0-decal", "nothing to judge: no open PROJ / IPCT slot holds a decal link and no TXST slot is open");
        } else {
            SelfTestLog::Check("T0-decal", tally.wrong == 0 && tally.setFaults == 0, std::format("{} links of open PROJ / IPCT slots ({} "
                "to open TXST slots), {} wrong; {} open TXST slots, {} with a fault{}", tally.links, tally.toOpenTxst, tally.wrong,
                tally.sets, tally.setFaults, tally.lines.empty() ? std::string() : ": " + tally.lines));
        }
        if (GetConfig().slotTxst && txstUsable > 0) {
            SelfTestLog::Check("T0-decal", txstOpen > 0, std::format("slotTxst: {} of {} usable TXST slots open (the plan's reasons for "
                "the closed ones: 'TXST slot ... not published' and the group lines in the log)", txstOpen, txstUsable));
        }
        return true;
    }

    void JudgeRuneSlots(const RE::SpellItem* a_rune)
    {
        if (!SlotAssign::Active() || !SlotsWanted("E3-slots")) {
            return;
        }
        const auto* effect = a_rune ? a_rune->GetAVEffect() : nullptr;
        const auto* projectile = effect ? effect->data.projectileBase : nullptr;
        if (!projectile) {
            SelfTestLog::Fail("E3-slots", "the Fire Rune has no effect projectile");
            return;
        }
        const auto* projCopy = CopyOf(projectile);
        SelfTestLog::Check("E3-slots", projCopy && SlotAssign::SlotOf(projCopy).has_value(), std::format("the rune's PROJ {:08X}: the "
            "player's launch takes {:08X} ({})", IdOf(projectile), IdOf(projCopy), projCopy ? "an open slot" : "none: its slot is closed"));
        // the chain's sources: PROJ, its explosion (and the MGEF's), the explosion's impact set's IPCTs
        std::vector<const RE::TESForm*> sources{ projectile };
        std::set<const RE::TESForm*> seen{ projectile };
        const auto add = [&](const RE::TESForm* a_form) {
            if (a_form && seen.insert(a_form).second) {
                sources.push_back(a_form);
            }
        };
        for (const auto* explosion : { projectile->data.explosionType, effect->data.explosion }) {
            add(explosion);
            const auto* set = explosion ? explosion->data.impactDataSet : nullptr;
            if (set) {
                for (const auto& [material, impact] : set->impactMap) {
                    add(impact);
                }
            }
        }
        Tally tally;
        std::size_t sourcesWithSlots = 0;
        std::size_t decalSets = 0;
        for (const auto* source : sources) {
            // the source itself is never a slot and holds no slot (the NPC's and every unswapped path keep the original)
            const RE::TESForm* links[3]{};
            if (const auto* p = source->As<RE::BGSProjectile>()) {
                links[0] = p->data.decalData;
            } else if (const auto* i = source->As<RE::BGSImpactData>()) {
                links[0] = i->decalTextureSet;
                links[1] = i->decalTextureSet2;
                links[2] = i->hazard;
            }
            for (const auto* link : links) {
                sourcesWithSlots += link && SlotAssign::IsSlotForm(link) ? 1 : 0;
                decalSets += link && link->GetFormType() == RE::FormType::TextureSet ? 1 : 0;
            }
            if (const auto* copy = CopyOf(source)) {
                JudgeHolder(tally, copy, source);
            }
        }
        for (const auto* source : sources) {
            if (const auto* p = source->As<RE::BGSProjectile>(); p && p->data.decalData) {
                if (const auto* copy = CopyOf(p->data.decalData); copy && IsOpenTxstSlot(copy)) {
                    JudgeTxst(tally, SlotAssign::At(*SlotAssign::SlotOf(copy)));
                }
            } else if (const auto* i = source->As<RE::BGSImpactData>()) {
                for (const auto* set : { i->decalTextureSet, i->decalTextureSet2 }) {
                    if (const auto* copy = set ? CopyOf(set) : nullptr; copy && IsOpenTxstSlot(copy)) {
                        JudgeTxst(tally, SlotAssign::At(*SlotAssign::SlotOf(copy)));
                    }
                }
            }
        }
        SelfTestLog::Check("E3-slots", sourcesWithSlots == 0, std::format("the rune's {} source form(s) hold no slot (the original stays "
            "for every path the swap does not take): {} slot link(s)", sources.size(), sourcesWithSlots));
        if (tally.links == 0 && tally.sets == 0) {
            SelfTestLog::Info("E3-slots", "the rune's chain: nothing to judge (no open copy in it holds a decal or hazard link)");
        } else {
            SelfTestLog::Check("E3-slots", tally.wrong == 0 && tally.setFaults == 0, std::format("the rune's chain: {} links of its open "
                "copies ({} to open TXST slots), {} wrong; {} open TXST slots judged, {} with a fault{}", tally.links, tally.toOpenTxst,
                tally.wrong, tally.sets, tally.setFaults, tally.lines.empty() ? std::string() : ": " + tally.lines));
        }
        if (GetConfig().slotTxst && decalSets > 0) {
            SelfTestLog::Check("E3-slots", tally.toOpenTxst > 0 && tally.sets > 0, std::format("slotTxst: the rune's chain names {} decal "
                "set(s); {} of its open copies' decal links reach an open TXST slot with grey copies", decalSets, tally.toOpenTxst));
        } else {
            SelfTestLog::Info("E3-slots", std::format("decal sets in the rune's chain: {} (slotTxst {})", decalSets, GetConfig().slotTxst));
        }
    }
}
