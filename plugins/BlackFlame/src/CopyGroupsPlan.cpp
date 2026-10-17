// =============================================================================
// CopyGroupsPlan - which object each group member would show, and the groups (CopyGroupsState.h)
// =============================================================================
// Split out of CopyGroups.cpp (verify and open) for the 600-line limit.

#include "CopyGroupsState.h"

#include "BuildProgress.h"
#include "FileIndex.h"
#include "FormCopiesFillModel.h"
#include "SlotAssign.h"

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

        // The member's objects and their worst state (a muzzle that will not be grey only keeps the source's muzzle)
        Slot PlanSlot(const State& a_state, std::uint16_t a_id)
        {
            const auto& slot = SlotAssign::At(a_id);
            const auto* entry = LM::FindSlot(a_state.ledger, a_id);
            Slot out{ a_id };
            if (slot.type == CC::SlotType::kTXST) {
                out.state = Member::kLeft;
                out.model.why = "a decal texture set slot stays unpublished (design v2 §1 TXST: no proof yet that SetTexturePath and "
                                "its textureFileIDs hold; the decal keeps the source)";
                return out;
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
                    if (m.state != Member::kLeft && plan->candidate) {
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
        Plan plan;
        const bool older = !a_published.empty();
        const auto fallback = [&](const char* a_why) {
            plan.state = older ? Member::kReady : Member::kLeft;  // the older generation, or the source for this session
            plan.why = a_why;
            return plan;
        };
        if (!a_key) {
            return fallback("no key this session (no builder)");
        }
        if (a_entry && a_entry->terminal.count(a_key->key) != 0) {
            return fallback("terminal: it failed to parse in two sessions");
        }
        const auto found = BuildProgress::Find(a_key->key);
        const auto stillComing = [&]() {  // older: shown meanwhile, the ADDN queue keeps the slot (review Codex 1)
            if (!older) {
                return Plan{ Member::kPending, std::nullopt, "" };
            }
            auto meanwhile = fallback("");
            meanwhile.waiting = true;
            return meanwhile;
        };
        if (!found) {
            // the builder ended (or never started) and every line it wrote is read: none will come this session
            return BuildProgress::CaughtUp() ? fallback("the builder made no line for it this session") : stillComing();
        }
        switch (found->state) {
        case BuildProgress::State::kTerminal:
            return fallback("terminal: too large or outside the dump caps");
        case BuildProgress::State::kFailed:
            return fallback("failed this session (tried again next start)");
        case BuildProgress::State::kStaging:
            if (BuildProgress::CaughtUp()) {
                // the worker's pass after the builder ended copied all it could: a file still missing will not come this session
                return fallback("its files did not all reach Data this session");
            }
            return stillComing();  // its files come into Data later
        case BuildProgress::State::kInData:
            break;
        }
        // review Codex 3 P1: before any route to ready or candidate - an object standing on a file that replaced another at
        // its path this session (its own or a copy dep's) is not published before the next start
        if (const auto files = LM::FilesOf(ObjectOf(a_role, found->line, a_key->text)); BuildProgress::AnyReplacedNow(files)) {
            return fallback("a file of it replaced another at the same path this session: used from the next start");
        }
        if (older && FormCopies::RecordModelPath(found->line.path) == a_published) {
            plan.state = Member::kReady;  // the ledger's generation is this key's (ApplyLastPublished compared its string)
            return plan;
        }
        if (!a_state.canVerify) {
            return fallback("no main-thread proof: verified at the next start");
        }
        // P1's builder is deterministic for the same inputs (its dev output was byte-identical over 774 files across rebuilds)
        // and a key's path is under its key folder, so one key gives one file: a copy replaces another file at its path only
        // when that file was damaged or cut. Should a key ever give two files, a path published this session could change
        auto candidate = ObjectOf(a_role, found->line, a_key->text);
        if (!FileIndex::Available() && EnteredDataNow(candidate)) {
            return fallback("in Data only since this session and no file index: the next start's index has it");
        }
        plan.state = Member::kCandidate;
        plan.candidate = std::move(candidate);
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
            } else {
                group.plannedAt = a_state.generation;
            }
        }
        return true;
    }
}
