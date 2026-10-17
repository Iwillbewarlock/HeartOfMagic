// =============================================================================
// CopyGroups - closure groups (connected copy links) and the group gate: verify, then open
// =============================================================================

#include "CopyGroups.h"

#include "BuildProgress.h"
#include "Config.h"
#include "CopyModelProbeVerify.h"
#include "FormCopies.h"
#include "FormCopiesFillModel.h"
#include "GameThread.h"
#include "MenuWatch.h"
#include "SingleLaunch.h"
#include "SlotAssign.h"
#include "SlotLedger.h"
#include "TrackedMutex.h"
#include "Watchdog.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <unordered_map>

namespace BlackFlame::CopyGroups
{
    namespace
    {
        namespace LM = SlotLedgerModel;
        namespace CC = CopyContract;
        using Clock = std::chrono::steady_clock;
        constexpr auto kSlice = std::chrono::milliseconds(8);  // the probe's measured no-hitch slice (README copyModelProbe)
        constexpr const char* kTaskName = "BlackFlameSlotPublish";

        enum class Member : std::uint8_t
        {
            kReady,
            kCandidate,
            kPending,
            kLeft,
        };

        struct Plan
        {
            Member state = Member::kLeft;  // kLeft here: no object (no file to show)
            std::optional<LM::Object> candidate;  // kCandidate: the object this session's key gave
            std::string why;
        };

        struct Slot
        {
            std::uint16_t id = 0;
            Member state = Member::kPending;
            Plan model;
            Plan muzzle;
        };

        enum class GroupState : std::uint8_t
        {
            kWaiting,
            kVerifying,
            kOpen,
            kFailed,
            kGone,  // every member left
        };

        struct Group
        {
            std::vector<std::uint16_t> ids;
            GroupState state = GroupState::kWaiting;
            std::vector<Slot> members;
            std::vector<std::pair<std::string, std::uint64_t>> verify;  // record path, key
            std::size_t verifyNext = 0;
        };

        struct State
        {
            std::vector<Group> groups;
            std::vector<std::uint16_t> addns;  // usable ADDN slots not open
            bool addnDirty = true;             // the builder moved since the ADDN slots were last looked at
            std::uint64_t generation = UINT64_MAX;
            bool canVerify = false;
            bool stopped = false;
        };
        State& TheState()  // main thread (kDataLoaded, then the drains)
        {
            static auto* state = std::make_unique<State>().release();
            return *state;
        }

        TrackedMutex g_mutex;  // the two snapshots below
        Progress g_progress;
        PublishLog g_log;

        LM::Object ObjectOf(LM::Role a_role, const CC::ProgressLine& a_line)
        {
            LM::Object object{ a_role, a_line.key, std::string(), a_line.path, a_line.bytes, a_line.fnv64, {} };
            for (const auto& dep : a_line.deps) {
                object.deps.push_back({ dep.isAddn, dep.addn, dep.path, dep.fnv64 });
            }
            return object;
        }

        bool AddnReady(const LM::Object& a_object)
        {
            for (const auto& dep : a_object.deps) {
                const auto slot = dep.isAddn ? CC::SlotOfAddnIndex(dep.addn) : std::nullopt;
                if (dep.isAddn && (!slot || !SlotAssign::GateOpen(*slot))) {
                    return false;
                }
            }
            return true;
        }

        // One object of a slot: the slot's ready generation (a_published), this session's key, or nothing
        Plan PlanObject(LM::Role a_role, const std::optional<SlotKeys::Key>& a_key, const std::string& a_published,
            const LM::Entry* a_entry, bool a_canVerify)
        {
            Plan plan;
            const bool older = !a_published.empty();
            const auto ready = [&](const char* a_why) {
                plan.state = older ? Member::kReady : Member::kLeft;
                plan.why = older ? "" : a_why;
                return plan;
            };
            if (!a_key) {
                return ready("no key this session (no builder) and no published generation");
            }
            if (a_entry && a_entry->terminal.count(a_key->key) != 0) {
                return ready("terminal: it failed to parse in two sessions");
            }
            const auto found = BuildProgress::Find(a_key->key);
            if (found && found->state == BuildProgress::State::kTerminal) {
                return ready("terminal: too large or outside the dump caps");
            }
            if (found && found->state == BuildProgress::State::kInData) {
                if (older && FormCopies::RecordModelPath(found->line.path) == a_published) {
                    plan.state = Member::kReady;  // the ledger's generation is this key's (ApplyLastPublished compared its string)
                    return plan;
                }
                if (a_canVerify || !older) {
                    plan.state = a_canVerify ? Member::kCandidate : Member::kPending;
                    plan.candidate = ObjectOf(a_role, found->line);
                    plan.candidate->keyText = a_key->text;
                    return plan;
                }
            }
            plan.state = older ? Member::kReady : Member::kPending;  // built later: the older generation meanwhile
            return plan;
        }

        // The member's objects and their worst state (a terminal muzzle only keeps the source's muzzle)
        Slot PlanSlot(std::uint16_t a_id, const LM::Ledger& a_ledger, bool a_canVerify)
        {
            const auto& slot = SlotAssign::At(a_id);
            const auto* entry = LM::FindSlot(a_ledger, a_id);
            Slot out{ a_id };
            if (slot.type == CC::SlotType::kTXST) {
                out.state = Member::kLeft;
                out.model.why = "a decal texture set slot stays unpublished (design v2 §1 TXST: no proof yet that SetTexturePath "
                                "and its textureFileIDs hold; the decal keeps the source)";
                return out;
            }
            if (!slot.sourceModel.empty()) {
                out.model = PlanObject(LM::Role::kModel, slot.modelKey, slot.publishedModel, entry, a_canVerify);
            }
            if (!slot.sourceMuzzle.empty()) {
                out.muzzle = PlanObject(LM::Role::kMuzzle, slot.muzzleKey, slot.publishedMuzzle, entry, a_canVerify);
                if (out.muzzle.state == Member::kLeft) {
                    out.muzzle.why += " (the source's muzzle stays)";
                }
            }
            const auto rank = [](Member a_m) { return a_m == Member::kPending ? 2 : a_m == Member::kCandidate ? 1 : 0; };
            if (!slot.sourceModel.empty() && out.model.state == Member::kLeft) {
                out.state = Member::kLeft;
                return out;
            }
            const int worst = std::max(slot.sourceModel.empty() ? 0 : rank(out.model.state),
                slot.sourceMuzzle.empty() || out.muzzle.state == Member::kLeft ? 0 : rank(out.muzzle.state));
            out.state = worst == 2 ? Member::kPending : worst == 1 ? Member::kCandidate : Member::kReady;
            for (const auto* plan : { &out.model, &out.muzzle }) {  // a NIF naming an ADDN slot waits for that slot's MPS
                if (plan->candidate && !AddnReady(*plan->candidate) && out.state != Member::kLeft) {
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
                    if (!SlotAssign::GateOpen(id)) {
                        a_state.addns.push_back(id);
                    }
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
                const auto root = find(id);
                const auto [it, added] = groupOf.emplace(root, a_state.groups.size());
                if (added) {
                    a_state.groups.emplace_back();
                }
                a_state.groups[it->second].ids.push_back(id);
            }
        }

        void Replan(State& a_state)
        {
            const auto ledger = SlotLedger::Copy();
            for (auto& group : a_state.groups) {
                if (group.state != GroupState::kWaiting) {
                    continue;
                }
                group.members.clear();
                bool pending = false;
                bool any = false;
                group.verify.clear();
                for (const auto id : group.ids) {
                    group.members.push_back(PlanSlot(id, ledger, a_state.canVerify));
                    const auto& m = group.members.back();
                    pending = pending || m.state == Member::kPending;
                    any = any || m.state != Member::kLeft;
                    for (const auto* plan : { &m.model, &m.muzzle }) {
                        if (m.state != Member::kLeft && plan->candidate) {
                            group.verify.emplace_back(FormCopies::RecordModelPath(plan->candidate->path), plan->candidate->key);
                        }
                    }
                }
                if (!any) {
                    group.state = GroupState::kGone;
                } else if (!pending) {
                    group.state = group.verify.empty() ? GroupState::kWaiting : GroupState::kVerifying;
                    group.verifyNext = 0;
                }
            }
        }

        void NoteStep()
        {
            const auto thread = REX::W32::GetCurrentThreadId();
            const auto* main = RE::Main::GetSingleton();
            std::lock_guard lock(g_mutex);
            if (g_log.steps++ == 0) {
                g_log.firstThread = thread;
                logger::info("BlackFlame: slots: the first publish step runs on thread {} ({})", thread,
                    main && thread == main->threadID ? "Main's thread" : "NOT Main's thread");
            }
            g_log.allOnMainThread = g_log.allOnMainThread && main && thread == main->threadID;
            g_log.loadDuringPublish = g_log.loadDuringPublish || Watchdog::LoadUnderWay();
        }

        // Every candidate object's files registered (FindRecord first); false when one did not register
        bool RegisterCandidates(const Group& a_group, std::string& a_why)
        {
            for (const auto& member : a_group.members) {
                for (const auto* plan : { &member.model, &member.muzzle }) {
                    if (member.state != Member::kLeft && plan->candidate && !FormCopies::RegisterObject(*plan->candidate, a_why)) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool WindowOpen(bool a_sync)
        {
            return a_sync ? MenuWatch::PublishOpen() : MenuWatch::DrainMayPublish();
        }

        void Open(Group& a_group, bool a_sync)
        {
            if (!WindowOpen(a_sync)) {
                return;  // closed right before the first SetModel: the group stays as it was
            }
            NoteStep();
            std::vector<std::pair<std::uint16_t, LM::Object>> recorded;
            for (const auto& member : a_group.members) {
                if (member.state == Member::kLeft) {
                    continue;
                }
                auto& slot = SlotAssign::At(member.id);
                if (member.model.candidate) {
                    slot.publishedModel = FormCopies::RecordModelPath(member.model.candidate->path);
                    (void)FormCopies::SetFormModel(slot.form, slot.publishedModel);
                    recorded.emplace_back(member.id, *member.model.candidate);
                }
                if (member.muzzle.candidate) {
                    slot.publishedMuzzle = FormCopies::RecordModelPath(member.muzzle.candidate->path);
                    recorded.emplace_back(member.id, *member.muzzle.candidate);
                } else if (member.muzzle.state != Member::kReady) {
                    slot.publishedMuzzle.clear();
                }
                if (auto* projectile = slot.form->As<RE::BGSProjectile>(); projectile && !slot.publishedMuzzle.empty()) {
                    projectile->muzzleFlashModel.SetModel(slot.publishedMuzzle.c_str());
                }
            }
            for (const auto& member : a_group.members) {
                if (member.state != Member::kLeft) {
                    SlotAssign::OpenGate(member.id);
                }
            }
            for (const auto& member : a_group.members) {  // the links now that the group's gates are open
                const auto& slot = SlotAssign::At(member.id);
                if (member.state != Member::kLeft) {
                    (void)CopyFill::Refill(slot.form, slot.source, true);
                }
            }
            if (!recorded.empty()) {
                SlotLedger::Edit([&](LM::Ledger& a_ledger) {
                    for (auto& [id, object] : recorded) {
                        if (auto* entry = LM::FindSlot(a_ledger, id)) {
                            LM::SetPublished(*entry, std::move(object));
                        }
                    }
                });
                (void)SlotLedger::Save("a group opened");
            }
            a_group.state = GroupState::kOpen;
            logger::info("BlackFlame: slots: group of {} opened ({}; {} new object(s))", a_group.ids.size(), a_sync ? "kDataLoaded" :
                MenuWatch::MainMenuOpened() ? "W2" : "after kDataLoaded", recorded.size());
        }

        void FailGroup(Group& a_group, const std::string& a_path, std::uint64_t a_key, const std::string& a_what)
        {
            a_group.state = GroupState::kFailed;
            bool terminal = false;
            SlotLedger::Edit([&](LM::Ledger& a_ledger) {
                for (const auto id : a_group.ids) {
                    if (auto* entry = LM::FindSlot(a_ledger, id)) {
                        const auto& slot = SlotAssign::At(id);
                        if ((slot.modelKey && slot.modelKey->key == a_key) || (slot.muzzleKey && slot.muzzleKey->key == a_key)) {
                            terminal = LM::NoteParseFailure(*entry, a_key, SlotLedger::Session()) || terminal;
                        }
                    }
                }
            });
            (void)SlotLedger::Save("a copy failed to parse");
            logger::warn("BlackFlame: slots: a group of {} is not opened this session: {} ({}){}", a_group.ids.size(), a_path, a_what,
                terminal ? "; its key is now terminal \"parse\" (two sessions)" : "; tried again next start");
        }

        // An ADDN slot's new MPS: verified and published before any NIF that names it (flags stay 1: not at kDataLoaded)
        bool StepAddn(State& a_state, Clock::time_point a_start)
        {
            if (!a_state.addnDirty || a_state.addns.empty()) {
                return true;
            }
            const auto ledger = SlotLedger::Copy();
            bool opened = false;
            auto it = a_state.addns.begin();
            for (; it != a_state.addns.end() && Clock::now() - a_start < kSlice;) {
                auto& slot = SlotAssign::At(*it);
                const auto plan = PlanObject(LM::Role::kMps, slot.modelKey, slot.publishedModel, LM::FindSlot(ledger, *it), true);
                if (plan.state != Member::kCandidate) {
                    it = plan.state == Member::kPending ? std::next(it) : a_state.addns.erase(it);
                    continue;
                }
                std::string why;
                const auto path = FormCopies::RecordModelPath(plan.candidate->path);
                if (!MenuWatch::DrainMayPublish() || !FormCopies::RegisterObject(*plan.candidate, why)) {
                    return false;
                }
                const auto result = CopyModelProbe::DemandOnce(path);
                if (!result.ok) {
                    logger::warn("BlackFlame: slots: ADDN slot {:X}'s MPS failed: {}", *it, CopyModelProbe::Describe(path, result));
                    it = a_state.addns.erase(it);
                    continue;
                }
                NoteStep();
                slot.publishedModel = path;
                (void)FormCopies::SetFormModel(slot.form, path);
                SlotAssign::OpenGate(*it);
                SlotLedger::Edit([&](LM::Ledger& a_ledger) {
                    if (auto* entry = LM::FindSlot(a_ledger, *it)) {
                        LM::SetPublished(*entry, *plan.candidate);
                    }
                });
                opened = true;
                it = a_state.addns.erase(it);
            }
            a_state.addnDirty = it != a_state.addns.end();  // cut by the slice: go on next frame
            if (opened) {
                (void)SlotLedger::Save("an ADDN slot's MPS was published");
                a_state.generation = UINT64_MAX;  // NIFs naming it may be ready now: re-plan the groups
            }
            return true;
        }

        void UpdateProgress(const State& a_state)
        {
            Progress progress;
            progress.groups = a_state.groups.size();
            bool busy = false;
            for (const auto& group : a_state.groups) {
                progress.groupsOpen += group.state == GroupState::kOpen ? 1 : 0;
                progress.groupsFailed += group.state == GroupState::kFailed ? 1 : 0;
                progress.verifying += group.state == GroupState::kVerifying ? 1 : 0;
                busy = busy || group.state == GroupState::kWaiting || group.state == GroupState::kVerifying;
                for (const auto& member : group.members) {
                    if (member.state == Member::kLeft) {
                        ++progress.left;
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
            std::lock_guard lock(g_mutex);
            g_progress = progress;
        }

        // One drain's slice: re-plan when the builder's progress moved, the ADDN MPS first, then the groups
        void Step()
        {
            auto& state = TheState();
            const auto start = Clock::now();
            if (!MenuWatch::DrainMayPublish()) {
                state.stopped = true;
                UpdateProgress(state);
                logger::info("BlackFlame: slots: publication stopped ({})", MenuWatch::CloseReason());
                return;
            }
            if (const auto generation = BuildProgress::Generation(); generation != state.generation) {
                state.generation = generation;
                state.addnDirty = true;
                Replan(state);
            }
            bool more = StepAddn(state, start);
            for (auto& group : state.groups) {
                if (Clock::now() - start >= kSlice || !more) {
                    break;
                }
                if (group.state == GroupState::kWaiting && group.verify.empty() && !group.members.empty() &&
                    std::none_of(group.members.begin(), group.members.end(), [](const Slot& a_m) { return a_m.state == Member::kPending; })) {
                    Open(group, false);  // every member ready (an ADDN dep opened since kDataLoaded)
                    continue;
                }
                if (group.state != GroupState::kVerifying) {
                    continue;
                }
                std::string why;
                if (group.verifyNext == 0 && !RegisterCandidates(group, why)) {
                    FailGroup(group, why, 0, "a file did not register");
                    continue;
                }
                while (group.verifyNext < group.verify.size() && Clock::now() - start < kSlice) {
                    const auto& [path, key] = group.verify[group.verifyNext];
                    const auto result = CopyModelProbe::DemandOnce(path);
                    if (!result.ok) {
                        FailGroup(group, path, key, CopyModelProbe::Describe(path, result));
                        break;
                    }
                    ++group.verifyNext;
                }
                if (group.state == GroupState::kVerifying && group.verifyNext == group.verify.size()) {
                    Open(group, false);
                }
            }
            UpdateProgress(state);
            SingleLaunch::NotifyProgress();
            if (!GetProgress().settled) {
                (void)QueueTask(kTaskName, &Step);
            } else {
                logger::info("BlackFlame: slots: publication settled: {} of {} slots open, {} groups open, {} failed",
                    GetProgress().open, GetProgress().slots, GetProgress().groupsOpen, GetProgress().groupsFailed);
            }
        }
    }

    void Start()
    {
        auto& state = TheState();
        state.canVerify = MenuWatch::MainThreadProven();
        BuildGroups(state);
        state.generation = BuildProgress::Generation();
        Replan(state);
        std::size_t opened = 0;
        for (auto& group : state.groups) {
            const bool allReady = !group.members.empty() &&
                                  std::all_of(group.members.begin(), group.members.end(), [](const Slot& a_m) {
                                      return a_m.state == Member::kReady || a_m.state == Member::kLeft;
                                  });
            if (group.state == GroupState::kWaiting && allReady) {
                Open(group, true);
                opened += group.state == GroupState::kOpen ? 1 : 0;
            }
        }
        UpdateProgress(state);
        logger::info("BlackFlame: slots: {} groups, {} opened at kDataLoaded from their last published generations; {}", state.groups.size(),
            opened, state.canVerify ? "the rest are published on the main thread as they are verified" :
                                       "no main-thread proof: nothing more is published this session (" + MenuWatch::ProofText() + ")");
        if (state.canVerify && !GetProgress().settled && !QueueTask(kTaskName, &Step)) {
            logger::warn("BlackFlame: slots: the publish stepper could not be queued: nothing more is published this session");
        }
    }

    Progress GetProgress()
    {
        std::lock_guard lock(g_mutex);
        return g_progress;
    }

    PublishLog GetPublishLog()
    {
        std::lock_guard lock(g_mutex);
        return g_log;
    }
}
