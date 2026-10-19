// =============================================================================
// FormCopiesFillModel - the source-model placeholder and the published model of a slot (out of FormCopiesFill.cpp)
// =============================================================================

#include "FormCopiesFillModel.h"

#include "BuildProgress.h"
#include "DataFiles.h"
#include "DataPathRule.h"
#include "FileIndex.h"
#include "SlotAssign.h"
#include "Config.h"
#include "SlotLedger.h"
#include "SlotTxst.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <functional>
#include <utility>
#include <vector>

#include "FormCopies.h"

namespace BlackFlame::FormCopies
{
    RE::TESModel* ModelOf(RE::TESForm* a_form) noexcept
    {
        if (!a_form) {
            return nullptr;
        }
        switch (a_form->GetFormType()) {
        case RE::FormType::Projectile:
        case RE::FormType::Explosion:
        case RE::FormType::Impact:
        case RE::FormType::Hazard:
        case RE::FormType::ArtObject:
        case RE::FormType::AddonNode:
            return a_form->As<RE::TESModel>();
        default:
            return nullptr;
        }
    }

    std::string RecordModelPath(std::string_view a_dataPath)
    {
        if (DataPathRule::StartsWith(a_dataPath, DataPathRule::kMeshesRoot)) {
            a_dataPath.remove_prefix(DataPathRule::kMeshesRoot.size());
        }
        return std::string(a_dataPath);
    }

    bool SetFormModel(RE::TESForm* a_form, const std::string& a_recordPath)
    {
        auto* model = ModelOf(a_form);
        if (!model) {
            return false;
        }
        const char* now = model->GetModel();
        if (std::strcmp(now ? now : "", a_recordPath.c_str()) != 0) {
            model->SetModel(a_recordPath.c_str());
        }
        return true;
    }

    void FillAddonSlot(RE::BGSAddonNode& a_slot, const RE::BGSAddonNode& a_source)
    {
        a_slot.data.masterParticleCap = a_source.data.masterParticleCap;
        a_slot.sound = a_source.sound;
    }

    void SetAddonFlagsFromSource(RE::BGSAddonNode& a_slot, const RE::BGSAddonNode& a_source)
    {
        a_slot.data.flags = a_source.data.flags;
    }

    bool DataFileMatches(const std::string& a_dataPath, std::uint64_t a_bytes, std::uint64_t a_fnv64, std::string& a_why)
    {
        if (!DataFiles::Matches(a_dataPath, a_bytes, a_fnv64)) {
            a_why = std::format("'{}' is not in Data with {} bytes and fnv64 {:016x}", a_dataPath, a_bytes, a_fnv64);
            return false;
        }
        return true;
    }

    bool ObjectIntact(const SlotLedgerModel::Object& a_object, const SlotKeys::Inputs& a_inputs, std::string& a_why)
    {
        if (!a_object.path.empty() && !DataPathRule::ParseCopyPath(a_object.path)) {
            a_why = std::format("'{}' is not a copy path", a_object.path);  // review agy dc 9: never hashed or registered
            return false;
        }
        if (!a_object.path.empty() && !DataFileMatches(a_object.path, a_object.bytes, a_object.fnv64, a_why)) {
            return false;
        }
        for (const auto& dep : a_object.deps) {
            if (dep.isAddn) {
                continue;
            }
            if (DataPathRule::ParseCopyPath(dep.path)) {
                const auto hash = DataFiles::HashOf(dep.path);  // a dep's size is not recorded: its hash decides (once a session)
                if (!hash || hash->fnv64 != dep.fnv64) {
                    a_why = std::format("the dep '{}' is not in Data or changed", dep.path);
                    return false;
                }
                continue;
            }
            const auto state = DataPathRule::StartsWith(dep.path, DataPathRule::kTexturesRoot) ?
                                   SlotKeys::TextureHash(a_inputs, dep.path) :
                                   SlotKeys::NifHash(a_inputs, RecordModelPath(dep.path));
            if (state.state != CopyContract::FileState::kHashed || state.fnv64 != dep.fnv64) {
                a_why = std::format("the source file '{}' changed since the generation was built", dep.path);
                return false;
            }
        }
        return true;
    }

    bool RegisterObject(const SlotLedgerModel::Object& a_object, std::string& a_why)
    {
        if (!FileIndex::Available()) {
            return true;  // the next-launch fallback: files from an earlier session are in the startup index
        }
        std::vector<std::pair<std::string, std::uint64_t>> files;
        if (!a_object.path.empty()) {
            if (!DataPathRule::ParseCopyPath(a_object.path)) {
                a_why = std::format("'{}' is not a copy path", a_object.path);
                return false;
            }
            files.emplace_back(a_object.path, a_object.bytes);
        }
        for (const auto& dep : a_object.deps) {
            if (dep.isAddn || !DataPathRule::ParseCopyPath(dep.path)) {
                continue;  // an ADDN slot, or a source file (in the startup index)
            }
            const auto size = DataFiles::SizeOf(dep.path);
            if (!size) {
                a_why = std::format("the dep '{}' is not in Data", dep.path);  // review agy 3: never registered without it
                return false;
            }
            files.emplace_back(dep.path, *size);  // the size the loose stream must have (no byte read)
        }
        for (const auto& [path, bytes] : files) {
            const auto registration = FileIndex::Register(path, bytes);
            if (!FileIndex::Usable(registration.outcome)) {
                a_why = std::format("'{}': {}", path, FileIndex::OutcomeName(registration.outcome));
                return false;
            }
        }
        return true;
    }

    std::size_t ApplyLastPublished(const SlotKeys::Inputs& a_inputs)
    {
        namespace LM = SlotLedgerModel;
        const auto ledger = SlotLedger::Copy();
        std::vector<std::pair<std::uint16_t, LM::Role>> restored;  // a previous generation shown: it becomes the published one
        const auto intactObject = [&](const LM::Object* object, LM::Role a_role, const SlotAssign::Slot& a_slot, const char* a_which) -> const LM::Object* {
            std::string why;
            // the FNV-1a 64 collision guard: the same key with another key string is not this session's object
            const auto& current = a_role == LM::Role::kMuzzle ? a_slot.muzzleKey : a_role == LM::Role::kTxst ? a_slot.txstKey : a_slot.modelKey;
            const bool collision = object && current && object->key == current->key && !object->keyText.empty() &&
                                   object->keyText != current->text;
            if (collision) {
                why = "its key equals this session's but the key strings differ (an FNV-1a 64 collision)";
            }
            if (object && object->path.empty() && a_role != LM::Role::kTxst) {  // a txst object's files are its deps
                why = "it has no file";  // review agy 4: never SetModel("") nor an ADDN gate opened on it
            }
            if (object && why.empty() && BuildProgress::AnyReplacedNow(LM::FilesOf(*object))) {
                // review Codex 3 P1: a file renamed in at this start (ApplyDeferred) may still have the old file's record
                why = "a file of it replaced another at this start: used from the next start";
            }
            if (!object || !why.empty() || !ObjectIntact(*object, a_inputs, why) || !RegisterObject(*object, why)) {
                if (object) {
                    logger::info("BlackFlame: slots: slot {:X} ({}): its {} {} is not used: {}", a_slot.localId, a_slot.sourceText,
                        a_which, LM::RoleText(a_role), why);
                }
                return nullptr;
            }
            return object;
        };
        // (publication fix, 2026-10-19) the last published object, else the previous generation (an upgrade recorded but
        // not shown may fail here: the one shown before it stands in, and the ledger swaps them). a_usable: the caller's
        // check before anything is hashed or registered (the ADDN slots it names). A TXST slot's loop below does the same with
        // its strings and the write after the files (review round 1)
        struct Picked
        {
            const LM::Object* object = nullptr;
            bool previous = false;
        };
        const auto pick = [&](const LM::Entry& a_entry, LM::Role a_role, const SlotAssign::Slot& a_slot,
                              const std::function<bool(const LM::Object*)>& a_usable) -> Picked {
            const auto* published = LM::PublishedOf(a_entry, a_role);
            if (const auto* object = a_usable(published) ? intactObject(published, a_role, a_slot, "last published") : nullptr) {
                return { object, false };
            }
            const auto* previous = LM::PreviousOf(a_entry, a_role);
            return { a_usable(previous) ? intactObject(previous, a_role, a_slot, "previous") : nullptr, true };
        };
        // the caller set the picked object: a previous one becomes the ledger's published one (RestorePrevious below)
        const auto shown = [&](const Picked& a_picked, const SlotAssign::Slot& a_slot, std::uint16_t a_id, LM::Role a_role) {
            if (a_picked.previous) {
                logger::info("BlackFlame: slots: slot {:X} ({}): its previous {} is shown instead of the last published one",
                    a_slot.localId, a_slot.sourceText, LM::RoleText(a_role));
                restored.emplace_back(a_id, a_role);
            }
        };
        std::size_t applied = 0;
        // ADDN slots first: a NIF naming an ADDN slot index is used only when that slot's MPS is (design v3.2 §3)
        // an MPS may name other ADDN slots too (contract v4): the ADDN pass runs again until nothing more opens
        constexpr int kAddnPasses = 4;
        for (int pass = 0; pass <= kAddnPasses; ++pass) {
            const bool addnPass = pass < kAddnPasses;
            const auto before = applied;
            for (const auto id : SlotAssign::Assigned()) {
                auto& slot = SlotAssign::At(id);
                const auto* entry = LM::FindSlot(ledger, id);
                if (!slot.usable || !entry || (slot.type == CopyContract::SlotType::kADDN) != addnPass ||
                    (addnPass && SlotAssign::GateOpen(id))) {
                    continue;
                }
                if (slot.type == CopyContract::SlotType::kTXST) {
                    // slotTxst: its last published txst object written now (gate closed until its group opens); off: unpublished.
                    // review round 1 (Codex P2 / D1 / Gemini): a set whose strings cannot be restored or written is like one
                    // whose files fail - the previous set stands in (Write sets nothing when it fails)
                    for (const bool previous : { false, true }) {
                        const char* which = previous ? "previous" : "last published";
                        const auto* object = !GetConfig().slotTxst ? nullptr :
                                             previous             ? LM::PreviousOf(*entry, LM::Role::kTxst) :
                                                                    LM::PublishedOf(*entry, LM::Role::kTxst);
                        const auto* txst = intactObject(object, LM::Role::kTxst, slot, which);
                        std::string why;
                        const auto strings = txst ? SlotTxst::RestoredFor(slot, *txst, why) : std::nullopt;
                        if (strings && SlotTxst::Write(slot, *strings, why)) {
                            slot.publishedTxst = CopyContract::Hex16(txst->key);
                            shown({ txst, previous }, slot, id, LM::Role::kTxst);
                            ++applied;
                            break;
                        }
                        if (txst) {
                            logger::info("BlackFlame: slots: TXST slot {:X} ({}): its {} set is not used: {}", slot.localId,
                                slot.sourceText, which, why);
                        }
                    }
                    continue;
                }
                // review: the rule W2 has (CopyGroups AddnDeps): an ADDN slot it names must be ours this session and usable;
                // one not open yet shows the model it has until it opens (only a collided or unusable one refuses)
                const auto addnReady = [](const LM::Object* a_object) {
                    return a_object && std::all_of(a_object->deps.begin(), a_object->deps.end(), [](const LM::Dep& a_dep) {
                        const auto addn = a_dep.isAddn ? CopyContract::SlotOfAddnIndex(a_dep.addn) : std::nullopt;
                        if (!a_dep.isAddn) {
                            return true;
                        }
                        if (!addn) {
                            return true;  // not a slot index (as AddnDeps reads it)
                        }
                        const auto& slot = SlotAssign::At(*addn);
                        return SlotAssign::GateOpen(*addn) || (slot.assigned && slot.usable);
                    });
                };
                const auto role = addnPass ? LM::Role::kMps : LM::Role::kModel;
                // review round 1 (Codex P2 / B2): a slot without a source model (a projectile with only a muzzle) has no
                // model object: its muzzle is restored by itself
                const bool muzzleOnly = !addnPass && slot.sourceModel.empty();
                if (!muzzleOnly) {
                    // review Gemini 9: the ADDN slots first, before any file is hashed or registered for an object not applied now
                    const auto picked = pick(*entry, role, slot, addnReady);
                    const auto* model = picked.object;
                    if (!model || !SetFormModel(slot.form, RecordModelPath(model->path))) {
                        continue;
                    }
                    shown(picked, slot, id, role);
                    slot.publishedModel = RecordModelPath(model->path);
                }
                if (addnPass) {
                    const auto* source = slot.source ? slot.source->As<RE::BGSAddonNode>() : nullptr;
                    if (auto* addon = slot.form->As<RE::BGSAddonNode>(); addon && source) {
                        SetAddonFlagsFromSource(*addon, *source);
                    }
                    SlotAssign::OpenGate(id);  // an ADDN slot's gate: its MPS is published (CopyOf never hands one out)
                } else if (!slot.sourceMuzzle.empty()) {  // review B4: the muzzle's ADDN slots too
                    if (const auto muzzle = pick(*entry, LM::Role::kMuzzle, slot, addnReady); muzzle.object) {
                        slot.publishedMuzzle = RecordModelPath(muzzle.object->path);
                        shown(muzzle, slot, id, LM::Role::kMuzzle);
                    }
                }
                if (muzzleOnly && slot.publishedMuzzle.empty()) {
                    continue;  // nothing of it restored: it shows its source
                }
                ++applied;
            }
            if (addnPass && applied == before) {
                pass = kAddnPasses - 1;  // no ADDN slot opened in this pass: on to the forms
            }
        }
        if (!restored.empty()) {
            SlotLedger::Edit([&](LM::Ledger& a_ledger) {
                for (const auto& [id, role] : restored) {
                    if (auto* entry = LM::FindSlot(a_ledger, id)) {
                        (void)LM::RestorePrevious(*entry, role);
                    }
                }
            });
            (void)SlotLedger::Save("kDataLoaded: previous generations shown");
        }
        logger::info("BlackFlame: slots: kDataLoaded: {} slot(s) took their last published generation (gates still closed until "
                     "their groups open)", applied);
        return applied;
    }
}
