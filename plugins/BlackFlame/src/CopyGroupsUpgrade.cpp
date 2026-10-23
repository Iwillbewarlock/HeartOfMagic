// =============================================================================
// CopyGroupsUpgrade - an open group's newer objects: recorded in the ledger, then shown at the main menu (CopyGroupsState.h)
// =============================================================================
// RecordUpgrade / NoteUpgrade split out of CopyGroups.cpp for the 600-line limit (publication fix, 2026-10-19).
// ShowUpgrades (same-launch upgrades, 2026-10-23): an open group whose newer objects are all verified and recorded is shown
// in the same launch while the publish window is open - before the first load (the startup drains or the main menu), when
// no reference uses the slots' forms - the way Open shows a group with nothing shown, member by member (one that cannot be
// set keeps what it shows): the models and muzzles set, the TXST sets written, the
// fresh members' gates opened, then the links of every open member (CopyFill::Refill, pointers only). The ledger already
// holds what is shown (RecordUpgrade), so a load that closes the window first only leaves it for the next start, as before.

#include "CopyGroupsState.h"

#include "FormCopies.h"
#include "FormCopiesFillModel.h"
#include "SlotAssign.h"
#include "SlotLedger.h"
#include "SlotTxst.h"

#include <algorithm>
#include <format>
#include <unordered_set>

namespace BlackFlame::CopyGroups::Detail
{
    namespace
    {
        namespace CC = CopyContract;

        // The member's main object: a TXST's set, the model, or (a slot without a source model) its muzzle
        LM::Role MainRoleOf(const SlotAssign::Slot& a_slot)
        {
            if (a_slot.type == CC::SlotType::kTXST) {
                return LM::Role::kTxst;
            }
            return a_slot.sourceModel.empty() ? LM::Role::kMuzzle : LM::Role::kModel;
        }

        // A member's recorded objects not shown yet
        std::vector<Upgrade*> RecordedOf(Group& a_group, std::uint16_t a_id)
        {
            std::vector<Upgrade*> out;
            for (auto& [key, upgrade] : a_group.upgrades) {
                if (key.first == a_id && upgrade.recorded && !upgrade.shown && upgrade.object) {
                    out.push_back(&upgrade);
                }
            }
            return out;
        }

        // One recorded object onto its slot's form; false with a_why (the slot keeps what it shows for this object)
        bool SetObject(SlotAssign::Slot& a_slot, const LM::Object& a_object, std::string& a_why)
        {
            switch (a_object.role) {
            case LM::Role::kTxst: {
                if (a_object.textures.size() != SlotTxstPaths::kSlots) {
                    a_why = std::format("its stored strings are {} not {}", a_object.textures.size(), SlotTxstPaths::kSlots);
                    return false;
                }
                SlotTxstPaths::Strings strings;
                std::copy(a_object.textures.begin(), a_object.textures.end(), strings.begin());
                if (!SlotTxst::Write(a_slot, strings, a_why)) {
                    return false;
                }
                a_slot.publishedTxst = CC::Hex16(a_object.key);
                return true;
            }
            case LM::Role::kModel:
                a_slot.publishedModel = FormCopies::RecordModelPath(a_object.path);
                (void)FormCopies::SetFormModel(a_slot.form, a_slot.publishedModel);  // ModelOf checked by the caller
                return true;
            case LM::Role::kMuzzle:
                if (auto* projectile = a_slot.form->As<RE::BGSProjectile>()) {
                    a_slot.publishedMuzzle = FormCopies::RecordModelPath(a_object.path);
                    projectile->muzzleFlashModel.SetModel(a_slot.publishedMuzzle.c_str());
                    return true;
                }
                a_why = "a muzzle on a form that is no projectile";
                return false;
            case LM::Role::kMps:
                break;
            }
            a_why = "an ADDN MPS is no group member's object";
            return false;
        }
    }

    std::optional<LM::Object> RecordUpgrade(State& a_state, const Verify& a_item, std::string& a_why)
    {
        auto object = a_item.object;
        if (object.role == LM::Role::kTxst) {
            const auto strings = SlotTxst::RuleUsable(a_why) ? SlotTxst::StringsFor(SlotAssign::At(a_item.slot), object, a_why) : std::nullopt;
            if (!strings) {
                return std::nullopt;
            }
            object.textures.assign(strings->begin(), strings->end());
            object.idsRule = SlotTxst::RuleName();
        }
        bool recorded = false;
        SlotLedger::Edit([&](LM::Ledger& a_ledger) {
            if (auto* entry = LM::FindSlot(a_ledger, a_item.slot)) {
                LM::SetPublished(*entry, object);  // the generation shown now becomes its previous (a fallback)
                recorded = true;
            }
        });
        a_state.ledgerDirty = a_state.ledgerDirty || recorded;
        a_why = recorded ? "" : "the slot has no ledger entry";
        return recorded ? std::optional<LM::Object>(std::move(object)) : std::nullopt;  // kept for the group's turn (ShowUpgrades)
    }

    void NoteUpgrade(Group& a_group, const Verify& a_item, std::optional<LM::Object> a_recorded, const std::string& a_why)
    {
        const bool recorded = a_recorded.has_value();
        a_group.upgrades[{ a_item.slot, a_item.key }] = { recorded, recorded ? "its newer object is recorded (shown at the main menu, "
                                                                               "else at the next start)" :
                                                                             "its newer object is not used: " + a_why,
            std::move(a_recorded) };
        if (!recorded) {
            logger::warn("BlackFlame: slots: slot {:X}'s newer {} is not recorded for the next start: {} (it keeps what it shows; "
                         "its group stays open)", a_item.slot, LM::RoleText(a_item.object.role), a_why);
        }
    }

    std::size_t ShowUpgrades(Group& a_group)
    {
        a_group.showDue = false;
        std::unordered_set<std::uint16_t> opened;
        std::size_t shown = 0;
        std::size_t kept = 0;
        for (auto& member : a_group.members) {
            auto& slot = SlotAssign::At(member.id);
            auto recorded = RecordedOf(a_group, member.id);
            const auto mainRole = MainRoleOf(slot);
            const bool mainRecorded = std::any_of(recorded.begin(), recorded.end(), [&](const Upgrade* a_u) { return a_u->object->role == mainRole; });
            const auto atW1 = member.state == Member::kLeft ? Decide::AtW1::kLeft : LeftForNow(a_group, member.id) ? Decide::AtW1::kFresh : member.atW1;
            const auto show = Decide::ShowOf(atW1, mainRecorded, !recorded.empty());
            if (show == Decide::Show::kNothing) {
                continue;
            }
            const bool modelRecorded = std::any_of(recorded.begin(), recorded.end(), [](const Upgrade* a_u) { return a_u->object->role == LM::Role::kModel; });
            if (!slot.form || (modelRecorded && !FormCopies::ModelOf(slot.form))) {
                logger::warn("BlackFlame: slots: slot {:X}'s newer object(s) are not shown this session: no form or no model to set "
                             "(recorded for the next start)", member.id);
                for (auto* upgrade : recorded) {
                    upgrade->why = "recorded, not shown this session: no form or no model to set";
                }
                if (show == Decide::Show::kOpen) {
                    member.model.why = "not shown this session (no form or no model to set): recorded for the next start";
                }
                kept += recorded.size();
                continue;
            }
            bool mainSet = false;
            for (auto* upgrade : recorded) {
                std::string why;
                if (SetObject(slot, *upgrade->object, why)) {
                    upgrade->shown = true;
                    upgrade->why = "its newer object is shown (same launch)";
                    mainSet = mainSet || upgrade->object->role == mainRole;
                    ++shown;
                } else {
                    ++kept;
                    upgrade->why = "recorded, not shown this session: " + why;
                    logger::warn("BlackFlame: slots: slot {:X}'s newer {} is not shown this session ({}): recorded for the next start",
                        member.id, LM::RoleText(upgrade->object->role), why);
                }
            }
            if (show == Decide::Show::kOpen && mainSet) {
                SlotAssign::OpenGate(member.id);
                opened.insert(member.id);
            } else if (show == Decide::Show::kOpen) {
                member.model.why = "its main object was not shown this session: gated, recorded for the next start";
            }
        }
        if (!opened.empty()) {
            std::erase_if(a_group.leftForNow, [&](std::uint16_t a_id) { return opened.count(a_id) != 0; });
        }
        for (const auto& member : a_group.members) {  // the links now that the new gates are open (pointers only)
            const auto& slot = SlotAssign::At(member.id);
            if (member.state != Member::kLeft && slot.form && SlotAssign::GateOpen(member.id)) {
                (void)CopyFill::Refill(slot.form, slot.source, true);
            }
        }
        logger::info("BlackFlame: slots: group of {}: {} newer object(s) shown now, {} fresh member(s) opened{}", a_group.ids.size(),
            shown, opened.size(), kept == 0 ? std::string() : std::format(", {} kept for the next start", kept));
        return shown;
    }
}
