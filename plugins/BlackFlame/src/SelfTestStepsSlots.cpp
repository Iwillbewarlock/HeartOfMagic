// =============================================================================
// SelfTestStepsSlots - self-test T0-slots and T0-W2 (single launch; config selfTestSlots, off in the shipped json)
// =============================================================================
// Both run after a save load like every T0 step, judged from data the slot route kept (no world needed):
//   T0-slots  the slots are in use (the slot ESL loaded, a ledger); the assignment is stable over two starts (every source
//             the ledger held kept its slot; SKIP on the ledger's first session); a ledger loss restores from the backups
//             and the mirror (their union, read now without main, holds every reservation of the ledger in use); the BFSL
//             record round-trips in memory, and the loaded save's BFSL matched this session's pairs; no Demand was made of a
//             gated slot's model (CopyModelProbeVerify's counter: 0); the file index state (INFO)
//   T0-W2     every publish step ran on RE::Main's thread and none while a load was under way; every open slot's model and
//             PROJ muzzle strings are the ones its publication set and the ledger records (the kPostLoadGame refresh did
//             not write them); how many publish steps ran with the main menu open (SetModel at the main menu: INFO, the
//             look is the player's eye check)
// What stays for the eye: a grey copy on screen after the main-menu publication (it cannot be judged from data).

#include "SelfTestCtx.h"

#include "Config.h"
#include "CopyGroups.h"
#include "CopyModelProbeVerify.h"
#include "FileIndex.h"
#include "FormCopiesFillModel.h"
#include "SelfTestLog.h"
#include "SingleLaunch.h"
#include "SlotAssign.h"
#include "SlotLedger.h"
#include "SlotLedgerFiles.h"

#include <format>

namespace BlackFlame::SelfTest
{
    namespace
    {
        namespace LM = SlotLedgerModel;
        constexpr std::size_t kMaxFailLines = 20;

        bool Wanted(const char* a_id)
        {
            const auto& config = GetConfig();
            if (!config.singleLaunch || !config.selfTestSlots) {
                SelfTestLog::Info(a_id, std::format("off (singleLaunch {}, selfTestSlots {})", config.singleLaunch, config.selfTestSlots));
                return false;
            }
            if (!SlotAssign::Active()) {
                SelfTestLog::Fail(a_id, "the slot route is not in use this session (the slot ESL, the ledger or the closure: see the log)");
                return false;
            }
            return true;
        }

        // The union of the backups and the mirror, read now without main: it must hold every reservation in use
        void CheckRestore()
        {
            const auto status = SlotLedger::GetStatus();
            LM::Ledger restored;
            std::vector<std::string> conflicts;
            int read = 0;
            std::vector<std::filesystem::path> files;
            for (int n = 1; n <= SlotLedgerFiles::kBackups; ++n) {
                files.push_back(SlotLedgerFiles::BackupPath(status.main, n));
            }
            for (const auto& file : files) {
                std::string why;
                if (const auto ledger = SlotLedgerFiles::ReadOne(file, why)) {
                    (void)LM::Merge(restored, *ledger, conflicts, "a backup");
                    ++read;
                }
            }
            if (read == 0) {
                SelfTestLog::Skip("T0-slots", "ledger loss: no backup yet (the backups are rolled once a session: start again)");
                return;
            }
            const auto current = SlotLedger::Copy();
            const auto session = SlotAssign::GetCounts().session;
            std::size_t missing = 0;
            std::size_t judged = 0;
            for (const auto& entry : current.entries) {
                if (entry.since >= session) {
                    continue;  // reserved this session: in no backup yet
                }
                ++judged;
                const auto* kept = LM::FindSlot(restored, entry.slot);
                missing += kept && LM::SameSource(*kept, entry.type, entry.plugin, entry.localId) ? 0 : 1;
            }
            SelfTestLog::Check("T0-slots", missing == 0,
                std::format("ledger loss: {} backup file(s) read without main and the mirror; their union holds {} of {} reservations "
                            "(this session's new ones aside)",
                    read, judged - missing, judged));
        }

        bool StepSlots(Ctx&, bool)
        {
            if (!Wanted("T0-slots")) {
                return true;
            }
            const auto counts = SlotAssign::GetCounts();
            SelfTestLog::Check("T0-slots", counts.wanted > 0 && counts.active > 0, std::format("slots in use: {} wanted, {} active ({} usable), {} tombstones, {} conflicts, {} pool "
                "full, {} refused, {} from a template, {} ADDN collided, {} gates open", counts.wanted, counts.active, counts.usable,
                counts.tombstones, counts.conflicts, counts.poolFull, counts.refused, counts.templates, counts.collided, counts.open));
            if (counts.session <= 1) {
                SelfTestLog::Skip("T0-slots", "assignment stable over two starts: this is the ledger's first session (start again)");
            } else {
                SelfTestLog::Check("T0-slots", counts.moved == 0, std::format("assignment stable over two starts (session {}): {} sources "
                    "kept their slot, {} moved", counts.session, counts.kept, counts.moved));
            }
            CheckRestore();
            // review Gemini / agy 13: T0-request hands request v5 and its dump to this step on the slot route
            const auto request = SingleLaunch::Request();
            const bool dumpOk = request.dumpComplete && !request.dumpState.starts_with("failed");  // T0-request's rule
            SelfTestLog::Check("T0-slots", request.written && dumpOk, std::format("request v5 {}: {} form keys, {} muzzles, {} ADDN "
                "MPS, {} TXST; dump {}{}", request.written ? "written" : "NOT written", request.forms, request.muzzles, request.addnMps,
                request.txst, request.dumpState, request.dumpComplete ? "" : " (incomplete)"));
            std::string detail;
            SelfTestLog::Check("T0-slots", SlotLedger::RoundTripNow(detail), "BFSL round trip in memory: " + detail);
            const auto loaded = SlotLedger::LastLoadedSave();
            if (!loaded.hadRecord) {
                SelfTestLog::Info("T0-slots", "the loaded save holds no BFSL record (made without the slot route)");
            } else {
                SelfTestLog::Check("T0-slots", loaded.read && loaded.mismatches == 0, std::format("the loaded save's BFSL: {}, {} slots, "
                    "{} differ from this session's pairs", loaded.read ? "read" : "NOT read (" + loaded.why + ")", loaded.slots,
                    loaded.mismatches));
            }
            SelfTestLog::Check("T0-slots", CopyModelProbe::GatedSlotDemands() == 0, std::format("no Demand of a gated slot's model "
                "({} counted, {} verify Demands in all)", CopyModelProbe::GatedSlotDemands(), CopyModelProbe::VerifyDemands()));
            const auto index = FileIndex::GetCounters();
            SelfTestLog::Info("T0-slots", std::format("file index {}: {} registered, {} already loose, {} archive clashes, {} failed, {} ms",
                FileIndex::Available() ? "in use" : "not used (" + FileIndex::Unavailable() + ")", index.registered, index.alreadyLoose,
                index.clashes, index.failed, index.micros / 1000));
            return true;
        }

        bool StepW2(Ctx&, bool)
        {
            if (!Wanted("T0-W2")) {
                return true;
            }
            const auto log = CopyGroups::GetPublishLog();
            if (log.steps == 0) {
                SelfTestLog::Skip("T0-W2", "no publish step ran this session (nothing was ready): the threads cannot be judged");
            } else {
                SelfTestLog::Check("T0-W2", log.allOnMainThread && !log.loadDuringPublish, std::format("{} publish step(s): every one "
                    "on Main's thread: {}; a load under way during one: {}", log.steps, log.allOnMainThread ? "yes" : "NO",
                    log.loadDuringPublish ? "YES" : "no"));
            }
            const auto ledger = SlotLedger::Copy();
            std::size_t open = 0;
            std::size_t wrong = 0;
            for (const auto id : SlotAssign::Assigned()) {
                const auto& slot = SlotAssign::At(id);
                if (!SlotAssign::GateOpen(id) || !slot.form) {
                    continue;
                }
                ++open;
                const auto* model = FormCopies::ModelOf(slot.form);
                const char* now = model ? model->GetModel() : nullptr;
                const auto* entry = LM::FindSlot(ledger, id);
                const auto* recorded = entry ? LM::PublishedOf(*entry, slot.type == CopyContract::SlotType::kADDN ? LM::Role::kMps : LM::Role::kModel) : nullptr;
                std::string why;
                // the model pool hands back the first spelling of a path (case-blind): compared as the engine compares them
                const auto differs = [](const std::string& a_ours, const char* a_engine) { return !a_engine || _stricmp(a_ours.c_str(), a_engine) != 0; };
                if (!slot.publishedModel.empty() && differs(slot.publishedModel, now)) {
                    why = std::format("model '{}', published '{}'", now ? now : "", slot.publishedModel);
                } else if (recorded ? FormCopies::RecordModelPath(recorded->path) != slot.publishedModel : !slot.publishedModel.empty()) {
                    why = std::format("the ledger records '{}', the slot shows '{}'", recorded ? recorded->path : std::string("nothing"),
                        slot.publishedModel);
                }
                if (const auto* projectile = slot.form->As<RE::BGSProjectile>(); why.empty() && projectile && !slot.publishedMuzzle.empty()) {
                    const char* muzzle = projectile->muzzleFlashModel.GetModel();
                    const auto* recordedMuzzle = entry ? LM::PublishedOf(*entry, LM::Role::kMuzzle) : nullptr;
                    if (differs(slot.publishedMuzzle, muzzle)) {
                        why = std::format("muzzle '{}', published '{}'", muzzle ? muzzle : "", slot.publishedMuzzle);
                    } else if (!recordedMuzzle || FormCopies::RecordModelPath(recordedMuzzle->path) != slot.publishedMuzzle) {
                        why = std::format("the ledger records the muzzle '{}', the slot shows '{}'",
                            recordedMuzzle ? recordedMuzzle->path : std::string("nothing"), slot.publishedMuzzle);
                    }
                }
                if (!why.empty() && ++wrong <= kMaxFailLines) {
                    SelfTestLog::Fail("T0-W2", std::format("slot {:X} ({}): {}", id, slot.sourceText, why));
                }
            }
            if (open == 0) {
                SelfTestLog::Skip("T0-W2", "no slot is open: the published models cannot be judged");
            } else {
                SelfTestLog::Check("T0-W2", wrong == 0, std::format("{} open slot(s): models and muzzles as published and recorded ({} "
                    "differ)", open, wrong));
            }
            SelfTestLog::Check("T0-W2", CopyModelProbe::GatedSlotDemands() == 0, "no Demand of a gated slot's model");
            SelfTestLog::Info("T0-W2", std::format("{} publish step(s) ran with the main menu open (SetModel at the main menu); the grey "
                "look of those copies is the eye check", log.mainMenuSteps));
            return true;
        }
    }

    bool StepSlotsT0(Ctx& a_c, bool a_first)
    {
        return StepSlots(a_c, a_first);
    }

    bool StepSlotsW2(Ctx& a_c, bool a_first)
    {
        return StepW2(a_c, a_first);
    }
}
