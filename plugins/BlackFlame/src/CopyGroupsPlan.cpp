// =============================================================================
// CopyGroupsPlan - which object each group member would show, and the groups (CopyGroupsState.h)
// =============================================================================
// Split out of CopyGroups.cpp (verify and open) for the 600-line limit; the progress counts too.

#include "CopyGroupsState.h"

#include "BuildProgress.h"
#include "Config.h"
#include "FileIndex.h"
#include "FormCopiesFillModel.h"
#include "MenuWatch.h"
#include "SlotAssign.h"
#include "SlotTxst.h"

#include <algorithm>
#include <map>
#include <memory>
#include <unordered_map>

namespace BlackFlame::CopyGroups::Detail
{
    namespace
    {
        namespace CC = CopyContract;

        LM::Object ObjectOf(LM::Role a_role, const CC::ProgressLine& a_line, const std::string& a_keyText)
        {
            LM::Object object{ a_role, a_line.key, a_keyText, a_line.path, a_line.bytes, a_line.fnv64, {} };
            for (const auto& dep : a_line.deps) {
                object.deps.push_back({ dep.isAddn, dep.addn, dep.path, dep.fnv64 });
            }
            return object;
        }

        // A TXST slot (slotTxst on): its txst object as the other members' objects; a candidate only when its deps map onto
        // TX00 / TX03 and the textureFileIDs rule is usable (SlotTxst.h). Off: left, the decal keeps the source
        Slot PlanTxst(const State& a_state, const SlotAssign::Slot& a_slot, const LM::Entry* a_entry)
        {
            Slot out{ a_slot.localId };
            if (!GetConfig().slotTxst) {
                out.state = Member::kLeft;
                out.model.why = "slotTxst off: the decal keeps the source";
                return out;
            }
            out.model = PlanObject(a_state, LM::Role::kTxst, a_slot.txstKey, a_slot.publishedTxst, a_entry);
            if (out.model.candidate) {
                std::string why;
                if (CC::Hex16(out.model.candidate->key) == a_slot.publishedTxst) {
                    out.model.candidate.reset();  // written already this session (W1 from the ledger)
                    out.model.state = Member::kReady;
                } else if (!SlotTxst::RuleUsable(why) || !SlotTxst::StringsFor(a_slot, *out.model.candidate, why)) {
                    out.model.candidate.reset();
                    out.model.state = a_slot.publishedTxst.empty() ? Member::kLeft : Member::kReady;
                    out.model.why = why;
                    logger::info("BlackFlame: slots: TXST slot {:X} ({}): not published: {}", a_slot.localId, a_slot.sourceText, why);
                }
            }
            out.state = out.model.state;
            return out;
        }

        // The member's objects and their worst state (a muzzle that will not be grey only keeps the source's muzzle)
        Slot PlanSlotObjects(const State& a_state, std::uint16_t a_id)
        {
            const auto& slot = SlotAssign::At(a_id);
            const auto* entry = LM::FindSlot(a_state.ledger, a_id);
            Slot out{ a_id };
            if (slot.type == CC::SlotType::kTXST) {
                return PlanTxst(a_state, slot, entry);
            }
            if (!slot.sourceModel.empty()) {
                out.model = PlanObject(a_state, LM::Role::kModel, slot.modelKey, slot.publishedModel, entry);
            }
            if (!slot.sourceMuzzle.empty()) {
                out.muzzle = PlanObject(a_state, LM::Role::kMuzzle, slot.muzzleKey, slot.publishedMuzzle, entry);
            }
            if (!slot.sourceModel.empty() && out.model.state == Member::kLeft) {
                out.state = Member::kLeft;
                return out;
            }
            const auto rank = [](Member a_m) { return a_m == Member::kPending ? 2 : a_m == Member::kCandidate ? 1 : 0; };
            const int worst = std::max(slot.sourceModel.empty() ? 0 : rank(out.model.state),
                slot.sourceMuzzle.empty() || out.muzzle.state == Member::kLeft ? 0 : rank(out.muzzle.state));
            out.state = worst == 2 ? Member::kPending : worst == 1 ? Member::kCandidate : Member::kReady;
            for (auto* plan : { &out.model, &out.muzzle }) {  // a NIF naming an ADDN slot waits for that slot's MPS
                if (!plan->candidate) {
                    continue;
                }
                const auto deps = AddnDeps(a_state, *plan->candidate);
                if (deps == AddnDep::kLeft && plan == &out.model) {
                    out.state = Member::kLeft;
                    out.model.why = "it names a collided or unusable ADDN slot";
                    return out;
                }
                if (deps == AddnDep::kLeft) {
                    plan->candidate.reset();  // the muzzle stays the source's
                    plan->state = Member::kLeft;
                } else if (deps == AddnDep::kWait) {
                    out.state = Member::kPending;
                }
            }
            return out;
        }

        Slot PlanSlot(const State& a_state, std::uint16_t a_id)
        {
            auto out = PlanSlotObjects(a_state, a_id);
            const auto& slot = SlotAssign::At(a_id);
            const auto& main = slot.type == CC::SlotType::kTXST || !slot.sourceModel.empty() ? out.model : out.muzzle;
            out.fresh = Decide::AtW1Of(out.state, main.state, main.older) == Decide::AtW1::kFresh;
            return out;
        }

        // The links of a slot's source to other slots' sources (the copy links FormCopiesFill rewires)
        std::vector<const RE::TESForm*> LinksOf(const RE::TESForm* a_source)
        {
            if (const auto* p = a_source->As<RE::BGSProjectile>()) {
                return { p->data.explosionType, p->data.decalData };
            }
            if (const auto* e = a_source->As<RE::BGSExplosion>()) {
                const auto* placed = reinterpret_cast<const RE::TESForm*>(e->data.impactPlacedObject);  // a base object (FormCopiesFill)
                const bool decal = placed && placed->GetFormType() == RE::FormType::TextureSet;  // AB2: the source's set is placed
                return { decal ? nullptr : placed, e->data.spawnProjectile };
            }
            if (const auto* i = a_source->As<RE::BGSImpactData>()) {
                return { i->hazard, i->decalTextureSet, i->decalTextureSet2 };
            }
            return {};
        }

        void PlanGroup(const State& a_state, Group& a_group)
        {
            a_group.members.clear();
            a_group.verify.clear();
            a_group.verifyNext = 0;
            bool pending = false;
            bool any = false;
            for (const auto id : a_group.ids) {
                a_group.members.push_back(PlanSlot(a_state, id));
                const auto& m = a_group.members.back();
                pending = pending || m.state == Member::kPending;
                any = any || m.state != Member::kLeft;
                for (const auto* plan : { &m.model, &m.muzzle }) {
                    if (m.state != Member::kLeft && plan->candidate && plan->candidate->role == LM::Role::kTxst) {
                        a_group.verify.push_back({ "", plan->candidate->key, *plan->candidate, false, false });  // registered only
                    } else if (m.state != Member::kLeft && plan->candidate) {
                        a_group.verify.push_back({ FormCopies::RecordModelPath(plan->candidate->path), plan->candidate->key, *plan->candidate });
                    }
                }
            }
            if (!any) {
                a_group.state = GroupState::kGone;
            } else if (!pending && !a_group.verify.empty()) {
                a_group.state = GroupState::kVerifying;
            }
            a_group.plannedAt = a_state.generation;
        }
    }

    State& TheState()
    {
        static auto* state = std::make_unique<State>().release();
        return *state;
    }

    bool EnteredDataNow(const LM::Object& a_object)
    {
        const auto files = LM::FilesOf(a_object);
        return std::any_of(files.begin(), files.end(), [](const auto& a_file) { return BuildProgress::CopiedNow(a_file.first, a_file.second); });
    }

    AddnDep AddnDeps(const State& a_state, const LM::Object& a_object)
    {
        for (const auto& dep : a_object.deps) {
            const auto id = dep.isAddn ? CC::SlotOfAddnIndex(dep.addn) : std::nullopt;
            if (!id || SlotAssign::GateOpen(*id)) {
                continue;
            }
            // review: an ADDN slot no source holds this session, or one that cannot be published (collided, refused,
            // a save's mismatch), is no ADDN of ours: the NIF would name a record that is not its MPS
            if (const auto& slot = SlotAssign::At(*id); !slot.assigned || !slot.usable) {
                return AddnDep::kLeft;
            }
            if (std::find(a_state.addns.begin(), a_state.addns.end(), *id) != a_state.addns.end()) {
                return AddnDep::kWait;
            }
            // not open and not going to open this session (failed, terminal, keyless): it shows the model it has
        }
        return AddnDep::kReady;
    }

    Plan PlanObject(const State& a_state, LM::Role a_role, const std::optional<SlotKeys::Key>& a_key, const std::string& a_published,
        const LM::Entry* a_entry)
    {
        // the decision is Decide::DecideObject's (publication fix, 2026-10-19: tested offline); the facts in its order
        Decide::ObjectFacts facts;
        facts.older = !a_published.empty();
        const bool terminalKey = a_key && a_entry && a_entry->terminal.count(a_key->key) != 0;
        const auto found = a_key && !terminalKey ? BuildProgress::Find(a_key->key) : std::nullopt;
        std::optional<LM::Object> candidate;
        if (!a_key) {
            facts.found = Decide::Found::kNoKey;
        } else if (terminalKey) {
            facts.found = Decide::Found::kTerminalKey;
        } else if (!found) {
            facts.found = Decide::Found::kNoLine;
        } else {
            switch (found->state) {
            case BuildProgress::State::kTerminal: facts.found = Decide::Found::kTerminal; break;
            case BuildProgress::State::kFailed: facts.found = Decide::Found::kFailed; break;
            case BuildProgress::State::kStaging: facts.found = Decide::Found::kStaging; break;
            case BuildProgress::State::kInData: facts.found = Decide::Found::kInData; break;
            }
        }
        facts.caughtUp = (facts.found == Decide::Found::kNoLine || facts.found == Decide::Found::kStaging) && BuildProgress::CaughtUp();
        if (facts.found == Decide::Found::kInData) {
            // review Codex 3 P1: before any route to ready or candidate - an object standing on a file that replaced another
            // at its path this session (its own or a copy dep's) is not published before the next start.
            // A key's path is under its key folder. The CPU encoder gives one key one file; the GPU's bytes differ from build
            // to build (review round 1 D1), so a rebuild of a published key keeps the Data file (BuildProgress KeepPublished,
            // and Find names it by the Data file's fnv64); only a build no ledger object stands on, or a damaged file, is deferred
            candidate = ObjectOf(a_role, found->line, a_key->text);
            facts.replacedNow = BuildProgress::AnyReplacedNow(LM::FilesOf(*candidate));
            facts.published = FormCopies::RecordModelPath(found->line.path) == a_published;  // ApplyLastPublished compared its string
            facts.canVerify = a_state.canVerify;
            facts.enteredNoIndex = !FileIndex::Available() && EnteredDataNow(*candidate);
        }
        const auto decision = Decide::DecideObject(facts);
        Plan plan{ decision.state, std::nullopt, decision.why, decision.waiting, facts.older };
        if (decision.state == Member::kCandidate) {
            plan.candidate = std::move(candidate);
        }
        return plan;
    }

    void BuildGroups(State& a_state)
    {
        std::unordered_map<RE::FormID, std::uint16_t> slotOfSource;
        std::vector<std::uint16_t> members;
        for (const auto id : SlotAssign::Assigned()) {
            const auto& slot = SlotAssign::At(id);
            if (!slot.usable || !slot.source) {
                continue;
            }
            if (slot.type == CC::SlotType::kADDN) {
                a_state.addns.push_back(id);  // open ones too: a newer complete MPS replaces theirs in W1 / W2 (review decision 10)
                continue;
            }
            slotOfSource.emplace(slot.source->GetFormID(), id);
            members.push_back(id);
        }
        std::unordered_map<std::uint16_t, std::uint16_t> parent;
        for (const auto id : members) {
            parent[id] = id;
        }
        const auto find = [&](std::uint16_t a_id) {
            while (parent[a_id] != a_id) {
                a_id = parent[a_id] = parent[parent[a_id]];
            }
            return a_id;
        };
        for (const auto id : members) {
            for (const auto* link : LinksOf(SlotAssign::At(id).source)) {
                const auto it = link ? slotOfSource.find(link->GetFormID()) : slotOfSource.end();
                if (it != slotOfSource.end()) {
                    parent[find(id)] = find(it->second);
                }
            }
        }
        std::map<std::uint16_t, std::size_t> groupOf;
        for (const auto id : members) {
            const auto [it, added] = groupOf.emplace(find(id), a_state.groups.size());
            if (added) {
                a_state.groups.emplace_back();
            }
            a_state.groups[it->second].ids.push_back(id);
        }
    }

    bool Replan(State& a_state, Clock::time_point a_start, bool a_unbounded)
    {
        for (; a_state.replanNext < a_state.groups.size(); ++a_state.replanNext) {
            if (!a_unbounded && Clock::now() - a_start >= kSlice) {
                return false;
            }
            auto& group = a_state.groups[a_state.replanNext];
            if (group.state == GroupState::kWaiting) {
                PlanGroup(a_state, group);
            } else if (group.state == GroupState::kUpgrading && group.verifyNext >= group.verify.size()) {
                PlanOpenGroup(a_state, group, true);  // one verifying finishes its list first (StepVerify plans it again then)
            } else {
                group.plannedAt = a_state.generation;
            }
        }
        return true;
    }

    bool OpensAtW1(const Group& a_group)
    {
        std::vector<Decide::AtW1> members;
        for (const auto& member : a_group.members) {
            members.push_back(member.state == Member::kLeft ? Decide::AtW1::kLeft : member.fresh ? Decide::AtW1::kFresh : Decide::AtW1::kShown);
        }
        return Decide::OpensAtW1(members);
    }

    bool LeftForNow(const Group& a_group, std::uint16_t a_id)
    {
        return std::find(a_group.leftForNow.begin(), a_group.leftForNow.end(), a_id) != a_group.leftForNow.end();
    }

    void PlanOpenGroup(const State& a_state, Group& a_group, bool a_replan)
    {
        if (a_replan) {
            a_group.members.clear();
            for (const auto id : a_group.ids) {
                a_group.members.push_back(PlanSlot(a_state, id));
            }
        }
        a_group.verify.clear();
        a_group.verifyNext = 0;
        a_group.upgradeWaits = false;
        const auto facts = [](const Plan& a_plan) { return Decide::PlanFacts{ a_plan.state, a_plan.candidate.has_value(), a_plan.waiting }; };
        for (auto& member : a_group.members) {
            for (auto* plan : { &member.model, &member.muzzle }) {
                const auto done = plan->candidate ? a_group.upgrades.find({ member.id, plan->candidate->key }) : a_group.upgrades.end();
                if (done != a_group.upgrades.end()) {
                    plan->candidate.reset();  // verified this session already: recorded for the next start, or refused
                    plan->state = Member::kReady;
                    plan->why = done->second.why;
                }
            }
            const auto work = Decide::WorkOf(member.state, facts(member.model), facts(member.muzzle));
            a_group.upgradeWaits = a_group.upgradeWaits || work.wait;
            for (const auto* plan : { &member.model, &member.muzzle }) {
                if (work.verify && plan->candidate) {
                    const bool txst = plan->candidate->role == LM::Role::kTxst;  // registered only
                    a_group.verify.push_back({ txst ? std::string() : FormCopies::RecordModelPath(plan->candidate->path), plan->candidate->key,
                        *plan->candidate, false, !txst, member.id });
                }
            }
            if (LeftForNow(a_group, member.id) && member.model.why.empty() && member.muzzle.why.empty()) {
                member.model.why = "no generation to show this session: its object is recorded for the next start once verified";
            }
        }
        const bool was = a_group.state == GroupState::kUpgrading;
        a_group.state = a_group.verify.empty() && !a_group.upgradeWaits ? GroupState::kOpen : GroupState::kUpgrading;
        a_group.plannedAt = a_state.generation;
        a_group.plannedFrom = a_state.generation;
        if (was && a_group.state == GroupState::kOpen) {
            std::size_t recorded = 0;
            for (const auto& upgrade : a_group.upgrades) {
                recorded += upgrade.second.recorded ? 1 : 0;
            }
            logger::info("BlackFlame: slots: group of {}: its newer objects are done ({} recorded for the next start, {} not used)",
                a_group.ids.size(), recorded, a_group.upgrades.size() - recorded);
        }
    }

    Progress CountProgress(const State& a_state)
    {
        Progress progress;
        progress.groups = a_state.groups.size();
        bool busy = !a_state.addns.empty();
        for (const auto& group : a_state.groups) {
            const bool open = group.state == GroupState::kOpen || group.state == GroupState::kUpgrading;
            progress.groupsOpen += open ? 1 : 0;
            progress.upgrading += group.state == GroupState::kUpgrading ? 1 : 0;
            progress.groupsFailed += group.state == GroupState::kFailed ? 1 : 0;
            progress.verifying += group.state == GroupState::kVerifying ? 1 : 0;
            busy = busy || group.state == GroupState::kWaiting || group.state == GroupState::kVerifying || group.state == GroupState::kUpgrading;
            for (const auto& upgrade : group.upgrades) {
                (upgrade.second.recorded ? progress.recorded : progress.refused) += 1;
            }
            for (const auto& member : group.members) {
                // a failed group's members, and those an open group left for this session, cannot open this session
                if (member.state == Member::kLeft || group.state == GroupState::kFailed || (open && LeftForNow(group, member.id))) {
                    ++progress.left;  // ("all ready" can show)
                    continue;
                }
                ++progress.slots;
                progress.open += SlotAssign::GateOpen(member.id) ? 1 : 0;
            }
        }
        for (const auto id : SlotAssign::Assigned()) {
            progress.addnOpen += SlotAssign::At(id).type == CC::SlotType::kADDN && SlotAssign::GateOpen(id) ? 1 : 0;
        }
        progress.settled = !busy || a_state.stopped || !MenuWatch::PublishOpen();
        return progress;
    }
}
