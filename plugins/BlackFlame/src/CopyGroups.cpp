// =============================================================================
// CopyGroups - closure groups (connected copy links) and the group gate: verify, then open
// =============================================================================
// The plan (which object each member would show) is CopyGroupsPlan.cpp's; here the ADDN MPS step, the verification in
// slices, the opening, the ledger write (one a step) and the progress the HUD shows.

#include "CopyGroups.h"
#include "CopyGroupsState.h"

#include "BuildProgress.h"
#include "CopyModelProbeVerify.h"
#include "FileIndex.h"
#include "FormCopies.h"
#include "FormCopiesFillModel.h"
#include "GameThread.h"
#include "MenuWatch.h"
#include "SingleLaunch.h"
#include "SlotAssign.h"
#include "SlotLedger.h"
#include "SlotTxst.h"
#include "TrackedMutex.h"
#include "Watchdog.h"

#include <algorithm>
#include <format>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace BlackFlame::CopyGroups
{
    namespace
    {
        using namespace Detail;
        namespace CC = CopyContract;
        constexpr const char* kTaskName = "BlackFlameSlotPublish";

        TrackedMutex g_mutex;  // the two snapshots below
        Progress g_progress;
        PublishLog g_log;

        // a_sync: a W1 step at kDataLoaded itself - on the kDataLoaded (loader) thread, which is not Main's on 1.6.1170 under
        // MO2 (run 2026-10-18): counted apart, T0-W2 judges the drain steps only
        void NoteStep(bool a_sync)
        {
            const auto thread = REX::W32::GetCurrentThreadId();
            const auto* main = RE::Main::GetSingleton();
            std::lock_guard lock(g_mutex);
            if (a_sync) {
                if (g_log.syncSteps++ == 0) {
                    g_log.syncThread = thread;
                    logger::info("BlackFlame: slots: the first W1 publish step runs at kDataLoaded on thread {} ({})", thread,
                        main && thread == main->threadID ? "Main's thread" : "not Main's thread: the kDataLoaded thread");
                }
                return;
            }
            if (g_log.steps++ == 0) {
                g_log.firstThread = thread;
                logger::info("BlackFlame: slots: the first publish step runs on thread {} ({})", thread,
                    main && thread == main->threadID ? "Main's thread" : "NOT Main's thread");
            }
            g_log.allOnMainThread = g_log.allOnMainThread && main && thread == main->threadID;
            g_log.mainMenuSteps += MenuWatch::MainMenuOpened() ? 1 : 0;
            g_log.loadDuringPublish = g_log.loadDuringPublish || Watchdog::LoadUnderWay();
        }

        bool WindowOpen(bool a_sync)
        {
            return a_sync ? MenuWatch::PublishOpen() : MenuWatch::DrainMayPublish();
        }

        // One parse failure of a_key in this session for each slot of a_ids holding it (two sessions: terminal "parse")
        bool CountParseFailure(State& a_state, const std::vector<std::uint16_t>& a_ids, std::uint64_t a_key)
        {
            bool terminal = false;
            SlotLedger::Edit([&](LM::Ledger& a_ledger) {
                for (const auto id : a_ids) {
                    const auto& slot = SlotAssign::At(id);
                    auto* entry = LM::FindSlot(a_ledger, id);
                    if (entry && ((slot.modelKey && slot.modelKey->key == a_key) || (slot.muzzleKey && slot.muzzleKey->key == a_key))) {
                        terminal = LM::NoteParseFailure(*entry, a_key, SlotLedger::Session()) || terminal;
                    }
                }
            });
            a_state.ledgerDirty = true;
            return terminal;
        }

        void Open(State& a_state, Group& a_group, bool a_sync)
        {
            if (!WindowOpen(a_sync)) {
                return;  // closed right before the first SetModel: the group stays as it was
            }
            std::unordered_map<std::uint16_t, SlotTxstPaths::Strings> txst;  // every TXST write made before anything is written
            std::unordered_set<std::uint16_t> unwritten;  // a TXST write that failed: its gate stays closed
            for (auto& member : a_group.members) {  // review agy dc 6: every SetFormModel below can take its model
                const auto& slot = SlotAssign::At(member.id);
                const bool isTxst = slot.type == CC::SlotType::kTXST;
                std::string why;
                if (member.state != Member::kLeft && isTxst && member.model.candidate) {
                    // the plan mapped it already; should it fail now, only this slot stays closed (the group's others open)
                    if (const auto strings = SlotTxst::StringsFor(slot, *member.model.candidate, why)) {
                        txst.emplace(member.id, *strings);
                    } else {
                        logger::warn("BlackFlame: slots: TXST slot {:X} not written ({}): its gate stays closed", member.id, why);
                        unwritten.insert(member.id);
                        member.model.why = "not written: " + why;  // its state in the open group (CopyGroups::SlotStateOf)
                    }
                }
                if (member.state != Member::kLeft && (!slot.form || (!isTxst && member.model.candidate && !FormCopies::ModelOf(slot.form)))) {
                    a_group.state = GroupState::kFailed;
                    logger::warn("BlackFlame: slots: a group of {} is not opened: slot {:X} has no form or no model to set{}",
                        a_group.ids.size(), member.id, why.empty() ? "" : " (" + why + ")");
                    return;
                }
            }
            NoteStep(a_sync);
            std::vector<std::pair<std::uint16_t, LM::Object>> recorded;
            for (auto& member : a_group.members) {
                if (member.state == Member::kLeft) {
                    continue;
                }
                auto& slot = SlotAssign::At(member.id);
                if (slot.type == CC::SlotType::kTXST) {
                    const auto it = txst.find(member.id);
                    std::string why;
                    if (it == txst.end()) {
                        continue;  // kReady (written before) or unwritten: no model, no muzzle
                    }
                    if (SlotTxst::Write(slot, it->second, why)) {
                        slot.publishedTxst = CC::Hex16(member.model.candidate->key);
                        auto object = *member.model.candidate;  // review B2: the strings and the IDs rule kept for a later W1
                        object.textures.assign(it->second.begin(), it->second.end());
                        object.idsRule = SlotTxst::RuleName();
                        recorded.emplace_back(member.id, std::move(object));
                    } else {
                        logger::warn("BlackFlame: slots: TXST slot {:X} not written ({}): its gate stays closed (the decal keeps the source)", member.id, why);
                        unwritten.insert(member.id);
                        member.model.why = "not written: " + why;
                    }
                    continue;  // no model, no muzzle
                }
                if (member.model.candidate) {
                    slot.publishedModel = FormCopies::RecordModelPath(member.model.candidate->path);
                    (void)FormCopies::SetFormModel(slot.form, slot.publishedModel);  // checked above
                    recorded.emplace_back(member.id, *member.model.candidate);
                }
                if (member.muzzle.candidate) {
                    slot.publishedMuzzle = FormCopies::RecordModelPath(member.muzzle.candidate->path);
                    recorded.emplace_back(member.id, *member.muzzle.candidate);
                } else if (member.muzzle.state != Member::kReady) {
                    slot.publishedMuzzle.clear();
                }
                if (auto* projectile = slot.form->As<RE::BGSProjectile>()) {
                    // review agy 11: a muzzle that is not published shows the source's again (the form and the slot agree)
                    const auto& muzzle = slot.publishedMuzzle.empty() ? slot.sourceMuzzle : slot.publishedMuzzle;
                    if (!muzzle.empty()) {
                        projectile->muzzleFlashModel.SetModel(muzzle.c_str());
                    }
                }
            }
            for (const auto& member : a_group.members) {
                if (member.state != Member::kLeft && unwritten.count(member.id) == 0) {
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
                a_state.ledgerDirty = true;
            }
            a_group.state = GroupState::kOpen;
            logger::info("BlackFlame: slots: group of {} opened ({}; {} new object(s))", a_group.ids.size(), a_sync ? "kDataLoaded" :
                MenuWatch::MainMenuOpened() ? "W2" : "after kDataLoaded", recorded.size());
        }

        // (publication fix, 2026-10-19) W1: a group one of whose members shows a generation (ApplyLastPublished set it) opens
        // on what its forms show: the shown members' gates and links. A fresh member (nothing to show yet: its object is
        // still coming or not verified) stays gated for this session - links to it go to its source - and its object, like
        // a shown member's newer one, is verified in the drains and only recorded for the next start (StepVerify). Nothing
        // is set here but the muzzle ApplyLastPublished took (it keeps the string only)
        void OpenShown(State& a_state, Group& a_group)
        {
            if (!WindowOpen(true)) {
                return;
            }
            for (const auto& member : a_group.members) {
                if (member.state != Member::kLeft && !member.Fresh() && !SlotAssign::At(member.id).form) {
                    a_group.state = GroupState::kFailed;
                    logger::warn("BlackFlame: slots: a group of {} is not opened: slot {:X} has no form", a_group.ids.size(), member.id);
                    return;
                }
            }
            NoteStep(true);
            a_group.leftForNow.clear();
            for (const auto& member : a_group.members) {
                const auto& slot = SlotAssign::At(member.id);
                if (member.state == Member::kLeft || member.Fresh()) {
                    if (member.Fresh()) {
                        a_group.leftForNow.push_back(member.id);
                    }
                    continue;
                }
                if (auto* projectile = slot.form->As<RE::BGSProjectile>()) {  // review agy 11: the form and the slot agree
                    const auto& muzzle = slot.publishedMuzzle.empty() ? slot.sourceMuzzle : slot.publishedMuzzle;
                    if (!muzzle.empty()) {
                        projectile->muzzleFlashModel.SetModel(muzzle.c_str());
                    }
                }
                SlotAssign::OpenGate(member.id);
            }
            for (const auto& member : a_group.members) {  // the links now that the shown members' gates are open
                const auto& slot = SlotAssign::At(member.id);
                if (member.state != Member::kLeft && !member.Fresh()) {
                    (void)CopyFill::Refill(slot.form, slot.source, true);
                }
            }
            PlanOpenGroup(a_state, a_group, false);
            logger::info("BlackFlame: slots: group of {} opened at kDataLoaded on what its forms show ({} member(s) left for this "
                         "session; {} newer object(s) to verify for the next start{})", a_group.ids.size(), a_group.leftForNow.size(),
                a_group.verify.size(), a_group.upgradeWaits ? ", more still coming" : "");
        }

        // (publication fix, 2026-10-19) kUpgrading: a verified object goes into the ledger only, for the next start's
        // ApplyLastPublished - no SetModel, no TXST write, no gate or link change in an open group. A TXST set is recorded
        // with its strings and the IDs rule (as Open keeps them), and only while the rule is usable (none: not recorded)
        bool RecordUpgrade(State& a_state, const Verify& a_item, std::string& a_why)
        {
            auto object = a_item.object;
            if (object.role == LM::Role::kTxst) {
                const auto strings = SlotTxst::RuleUsable(a_why) ? SlotTxst::StringsFor(SlotAssign::At(a_item.slot), object, a_why) : std::nullopt;
                if (!strings) {
                    return false;
                }
                object.textures.assign(strings->begin(), strings->end());
                object.idsRule = SlotTxst::RuleName();
            }
            bool recorded = false;
            SlotLedger::Edit([&](LM::Ledger& a_ledger) {
                if (auto* entry = LM::FindSlot(a_ledger, a_item.slot)) {
                    LM::SetPublished(*entry, std::move(object));  // the generation shown now becomes its previous (a fallback)
                    recorded = true;
                }
            });
            a_state.ledgerDirty = a_state.ledgerDirty || recorded;
            a_why = recorded ? "" : "the slot has no ledger entry";
            return recorded;
        }

        void NoteUpgrade(Group& a_group, const Verify& a_item, bool a_recorded, const std::string& a_why)
        {
            a_group.upgrades[{ a_item.slot, a_item.key }] = { a_recorded, a_recorded ? "its newer object is recorded for the next start" :
                                                                                       "its newer object is not used: " + a_why };
            if (!a_recorded) {
                logger::warn("BlackFlame: slots: slot {:X}'s newer {} is not recorded for the next start: {} (it keeps what it shows; "
                             "its group stays open)", a_item.slot, LM::RoleText(a_item.object.role), a_why);
            }
        }

        // review agy dc 5: a Demand that faulted may have left the model DB's lock or state behind: nothing more this session
        void StopOnFault(State& a_state, const std::string& a_path, const CopyModelProbe::VerifyResult& a_result)
        {
            a_state.stopped = true;
            logger::error("BlackFlame: slots: {} - the model database's state is unknown: nothing more is published this session",
                CopyModelProbe::Describe(a_path, a_result));
        }

        void FailGroup(State& a_state, Group& a_group, const std::string& a_path, std::optional<std::uint64_t> a_parseKey,
            const std::string& a_what)
        {
            a_group.state = GroupState::kFailed;
            const bool terminal = a_parseKey && CountParseFailure(a_state, a_group.ids, *a_parseKey);
            logger::warn("BlackFlame: slots: a group of {} is not opened this session: {} ({}){}", a_group.ids.size(), a_path, a_what,
                !a_parseKey ? "" : terminal ? "; its key is now terminal \"parse\" (two sessions)" : "; tried again next start");
        }

        // An ADDN slot's newest MPS: verified and published before any NIF that names it (flags stay 1: not at kDataLoaded).
        // An open slot is replaced by a newer complete generation of its current key (review decision 10). False only when
        // the window closed
        bool StepAddn(State& a_state, Clock::time_point a_start)
        {
            if (!a_state.addnDirty || a_state.addns.empty()) {
                return true;
            }
            bool opened = false;
            auto& pass = a_state.addnPass;  // review Codex 1: a pass goes on where the last slice stopped
            const auto erase = [&]() {
                a_state.addns.erase(a_state.addns.begin() + static_cast<std::ptrdiff_t>(pass.next));
                pass.changed = true;
            };
            while (pass.next < a_state.addns.size() && Clock::now() - a_start < kSlice) {
                const auto id = a_state.addns[pass.next];
                auto& slot = SlotAssign::At(id);
                const auto plan = PlanObject(a_state, LM::Role::kMps, slot.modelKey, slot.publishedModel, LM::FindSlot(a_state.ledger, id));
                // review Codex 1: a slot showing an older MPS stays listed while its current key may still come (its NIFs use
                // the older one meanwhile)
                if (plan.state == Member::kPending || plan.waiting) {
                    ++pass.pending;
                    ++pass.next;
                    continue;
                }
                if (plan.state != Member::kCandidate) {
                    erase();  // shows what it has for the session (its verified MPS, or the source's)
                    continue;
                }
                const auto deps = AddnDeps(a_state, *plan.candidate);  // an MPS may name other ADDN slots too (contract v4)
                if (deps == AddnDep::kWait) {
                    ++pass.waiting;
                    ++pass.next;
                    continue;
                }
                if (!MenuWatch::DrainMayPublish()) {
                    return false;
                }
                std::string why;
                const auto path = FormCopies::RecordModelPath(plan.candidate->path);
                if (EnteredDataNow(*plan.candidate) && !FileIndex::Available()) {
                    why = "its files entered Data this session and the file index is off now: the next start's index has them";
                } else if (deps == AddnDep::kLeft) {
                    why = "it names a collided or unusable ADDN slot";
                } else if (!slot.form || !FormCopies::ModelOf(slot.form)) {
                    why = "no form or no model to set";
                } else if (!FormCopies::RegisterObject(*plan.candidate, why) && why.empty()) {
                    why = "a file did not register";
                }
                if (!why.empty()) {
                    logger::warn("BlackFlame: slots: ADDN slot {:X}'s MPS is not used this session: {}", id, why);
                    erase();
                    continue;
                }
                const auto result = CopyModelProbe::DemandOnce(path);
                if (result.fault) {
                    StopOnFault(a_state, path, result);
                    return false;
                }
                if (!result.ok) {
                    const bool terminal = CountParseFailure(a_state, { id }, plan.candidate->key);
                    logger::warn("BlackFlame: slots: ADDN slot {:X}'s MPS failed: {}{}", id, CopyModelProbe::Describe(path, result),
                        terminal ? "; its key is now terminal \"parse\"" : "");
                    erase();
                    continue;
                }
                NoteStep(false);
                slot.publishedModel = path;
                (void)FormCopies::SetFormModel(slot.form, path);
                SlotAssign::OpenGate(id);
                SlotLedger::Edit([&](LM::Ledger& a_ledger) {
                    if (auto* entry = LM::FindSlot(a_ledger, id)) {
                        LM::SetPublished(*entry, *plan.candidate);
                    }
                });
                a_state.ledgerDirty = true;
                opened = true;
                erase();
            }
            const bool passDone = pass.next >= a_state.addns.size();
            a_state.addnDirty = !passDone;  // cut by the slice: go on next frame from there
            if (passDone) {
                if (!pass.changed && pass.pending == 0 && pass.waiting > 0) {
                    // a whole pass changed nothing and every slot left waits for another listed one (MPS naming each other,
                    // or itself): none can open first, so none opens this session; the NIFs naming them see the models they have
                    logger::warn("BlackFlame: slots: {} ADDN slot(s) wait for each other's MPS: not published this session", pass.waiting);
                    a_state.addns.clear();
                    opened = true;  // re-plan: their NIFs no longer wait
                }
                // review round 1 B3: a slot dropped from the list without opening changes its NIFs' plan too (kWait becomes
                // kReady): re-plan after every pass that changed the list (it only shrinks, so this ends)
                opened = opened || pass.changed;
                pass = {};
            }
            if (opened) {
                a_state.generation = kNoGeneration;  // NIFs naming it may be ready now: re-plan the groups
            }
            return true;
        }

        void UpdateProgress(const State& a_state)
        {
            const auto progress = CountProgress(a_state);  // CopyGroupsPlan.cpp
            std::lock_guard lock(g_mutex);
            g_progress = progress;
        }

        bool AllReady(const Group& a_group)
        {
            return !a_group.members.empty() && std::all_of(a_group.members.begin(), a_group.members.end(), [](const Slot& a_m) {
                return a_m.state == Member::kReady || a_m.state == Member::kLeft;
            });
        }

        // A verifying (or upgrading) group's next objects: each registered (FindRecord first), then Demanded, within the slice.
        // Verifying: a failure leaves the group closed for the session (FailGroup), all verified opens it. Upgrading
        // (publication fix, 2026-10-19): the group is open and stays so; a failure refuses that object only (a parse failure
        // counted as a verifying group's), a verified one is recorded for the next start
        void StepVerify(State& a_state, Group& a_group, Clock::time_point a_start)
        {
            const auto stepping = a_group.state;
            const bool upgrading = stepping == GroupState::kUpgrading;
            const auto fail = [&](const Verify& a_item, std::optional<std::uint64_t> a_parseKey, const std::string& a_what) {
                if (!upgrading) {
                    FailGroup(a_state, a_group, a_item.path, a_parseKey, a_what);
                    return;
                }
                const bool terminal = a_parseKey && CountParseFailure(a_state, a_group.ids, *a_parseKey);
                NoteUpgrade(a_group, a_item, false, a_what + (terminal ? "; its key is now terminal \"parse\"" : ""));
                ++a_group.verifyNext;
            };
            while (a_group.state == stepping && a_group.verifyNext < a_group.verify.size() && Clock::now() - a_start < kSlice) {
                auto& item = a_group.verify[a_group.verifyNext];
                std::string why;
                if (!item.registered && !FileIndex::Available() && EnteredDataNow(item.object)) {
                    // review N1: the index turned off after the plan; never Demanded (no parse failure counted)
                    fail(item, std::nullopt, "its files entered Data this session and the file index is off now (the next start's index has them)");
                    continue;
                }
                if (!item.registered) {
                    item.registered = FormCopies::RegisterObject(item.object, why);
                    if (!item.registered) {
                        fail(item, std::nullopt, "a file did not register: " + why);
                        continue;
                    }
                }
                if (item.demand) {  // a TXST object: its DDS deps registered, nothing to Demand
                    const auto result = CopyModelProbe::DemandOnce(item.path);
                    if (result.fault) {
                        StopOnFault(a_state, item.path, result);
                        return;
                    }
                    if (!result.ok) {
                        fail(item, item.key, CopyModelProbe::Describe(item.path, result));
                        continue;
                    }
                }
                if (upgrading) {
                    NoteUpgrade(a_group, item, RecordUpgrade(a_state, item, why), why);
                }
                ++a_group.verifyNext;
            }
            if (a_group.state != stepping || a_group.verifyNext < a_group.verify.size()) {
                return;
            }
            if (!upgrading) {
                Open(a_state, a_group, false);
            } else {
                // the list is done: kOpen unless more may come; planned again first when the progress moved meanwhile (Replan
                // left the group alone while it verified)
                PlanOpenGroup(a_state, a_group, a_group.plannedFrom != a_state.generation);
            }
        }

        // review: at most one ledger write every kLedgerWriteGap while publishing; a_final (kDataLoaded, settled, the window
        // closed, the stepper not queued again) writes what is left at once
        void SaveLedger(State& a_state, bool a_final)
        {
            const auto now = Clock::now();
            if (!a_state.ledgerDirty || (!a_final && a_state.lastLedgerWrite && now - *a_state.lastLedgerWrite < kLedgerWriteGap)) {
                return;
            }
            a_state.ledgerDirty = false;
            a_state.lastLedgerWrite = now;
            (void)SlotLedger::Save("publication");
        }

        // One drain's slice: re-plan (within the slice) when the builder's progress moved, the ADDN MPS, then the groups
        void StepChecked(State& a_state)
        {
            const auto start = Clock::now();
            if (a_state.stopped) {
                return;
            }
            if (!MenuWatch::DrainMayPublish()) {
                if (MenuWatch::PublishOpen() && MenuWatch::ProofPending()) {
                    // review / Codex: not made yet (Main::threadID still 0, or not asked on the drain's thread): the stepper is
                    // queued again (not settled) and asks at the next drain
                    if (!a_state.proofWaitLogged) {
                        a_state.proofWaitLogged = true;
                        logger::info("BlackFlame: slots: publication waits for the main-thread proof ({})", MenuWatch::ProofText());
                    }
                    return;
                }
                a_state.stopped = true;
                logger::info("BlackFlame: slots: publication stopped ({})",
                    MenuWatch::PublishOpen() ? "no main-thread proof: " + MenuWatch::ProofText() : MenuWatch::CloseReason());
                return;
            }
            if (const auto generation = BuildProgress::Generation(); generation != a_state.generation) {
                a_state.generation = generation;
                a_state.ledger = SlotLedger::Copy();
                a_state.replanNext = 0;
                a_state.addnDirty = true;
                a_state.addnPass = {};  // a new generation: the pass starts over
            }
            const bool planned = Replan(a_state, start);
            if (!planned || !StepAddn(a_state, start)) {
                return;
            }
            for (auto& group : a_state.groups) {
                if (a_state.stopped || Clock::now() - start >= kSlice) {
                    break;
                }
                if (group.plannedAt != a_state.generation) {
                    continue;
                }
                if (group.state == GroupState::kWaiting && AllReady(group)) {
                    Open(a_state, group, false);  // every member ready (an ADDN dep opened, or the rest left)
                } else if (group.state == GroupState::kVerifying || group.state == GroupState::kUpgrading) {
                    StepVerify(a_state, group, start);
                }
            }
        }

        void Step()
        {
            auto& state = TheState();
            try {
                StepChecked(state);
            } catch (const std::exception& e) {
                state.stopped = true;
                logger::error("BlackFlame: slots: the publish step threw ({}): nothing more is published this session", e.what());
            } catch (...) {
                state.stopped = true;
                logger::error("BlackFlame: slots: the publish step threw an exception of an unknown type: nothing more is published");
            }
            UpdateProgress(state);
            SingleLaunch::NotifyProgress();
            const auto progress = GetProgress();
            const bool queued = !progress.settled && QueueTask(kTaskName, &Step);
            SaveLedger(state, !queued);
            if (!progress.settled) {
                if (!queued) {
                    logger::warn("BlackFlame: slots: the publish stepper could not be queued again: nothing more is published this session");
                }
            } else {
                // (publication fix, 2026-10-19) whether the open groups' newer objects were all verified for the next start
                logger::info("BlackFlame: slots: publication settled: {} of {} slots open, {} groups open, {} failed, {} ADDN MPS open; "
                             "for the next start {} object(s) recorded, {} not used, {}", progress.open, progress.slots, progress.groupsOpen,
                    progress.groupsFailed, progress.addnOpen, progress.recorded, progress.refused,
                    progress.upgrading == 0 ? std::string("every open group's upgrades done") :
                                              std::format("{} open group(s) with upgrades NOT finished ({})", progress.upgrading,
                                                  !MenuWatch::PublishOpen() ? "the publish window closed" : "publication stopped"));
            }
        }
    }

    void Start()
    {
        auto& state = TheState();
        // the proof is made in the first drain (MenuWatch.h): planned for when it can still be made; a refusal there stops the stepper
        state.canVerify = MenuWatch::DrainsMayProve();
        state.stopped = !state.canVerify;  // review agy 3: no drain will step, so the progress is settled after kDataLoaded
        BuildGroups(state);
        state.generation = BuildProgress::Generation();
        state.ledger = SlotLedger::Copy();
        (void)Replan(state, Clock::now(), true);  // kDataLoaded: the whole plan at once (no frame to keep)
        std::size_t opened = 0;
        std::size_t upgrading = 0;
        for (auto& group : state.groups) {
            // (publication fix, 2026-10-19) a member showing a generation opens its group now; fresh members never keep it
            // closed (a group with none shown - a first start - waits, is verified and opens in the drains)
            if ((group.state == GroupState::kWaiting || group.state == GroupState::kVerifying) && OpensAtW1(group)) {
                OpenShown(state, group);
                opened += group.state == GroupState::kOpen || group.state == GroupState::kUpgrading ? 1 : 0;
                upgrading += group.state == GroupState::kUpgrading ? 1 : 0;
            }
        }
        SaveLedger(state, true);
        UpdateProgress(state);
        LogTxstSlotsAtW1(state);  // trace (CopyGroupsNotes.cpp)
        logger::info("BlackFlame: slots: {} groups, {} opened at kDataLoaded on the generations their forms show ({} of them verify newer "
                     "objects for the next start); {}", state.groups.size(), opened, upgrading,
            state.canVerify ? "groups with nothing shown yet are published on the main thread as they are verified" :
                              "no main-thread drain: nothing more is published or recorded this session (" + MenuWatch::ProofText() + ")");
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
