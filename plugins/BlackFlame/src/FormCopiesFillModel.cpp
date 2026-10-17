// =============================================================================
// FormCopiesFillModel - the source-model placeholder and the published model of a slot (out of FormCopiesFill.cpp)
// =============================================================================

#include "FormCopiesFillModel.h"

#include "DataPathRule.h"
#include "FileIndex.h"
#include "NifDump.h"
#include "SlotAssign.h"
#include "SlotLedger.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <vector>

#include "FormCopies.h"

namespace BlackFlame::FormCopies
{
    namespace
    {
        constexpr std::size_t kReadChunk = 1 << 16;
    }

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

    namespace
    {
        // a_dataPath under the Data folder read whole: its size and FNV-1a 64; false when it does not open
        bool HashDataFile(const std::string& a_dataPath, std::uint64_t& a_size, std::uint64_t& a_hash)
        {
            const auto data = FileIndex::DataFolder();
            if (!data) {
                return false;
            }
            std::ifstream in(*data / std::filesystem::path(a_dataPath), std::ios::binary);
            if (!in) {
                return false;
            }
            a_hash = NifDump::kFnvOffset;
            a_size = 0;
            std::vector<char> chunk(kReadChunk);
            while (in.read(chunk.data(), static_cast<std::streamsize>(chunk.size())) || in.gcount() > 0) {
                const auto count = static_cast<std::size_t>(in.gcount());
                a_hash = NifDump::Fnv64(chunk.data(), count, a_hash);
                a_size += count;
            }
            return true;
        }
    }

    bool DataFileMatches(const std::string& a_dataPath, std::uint64_t a_bytes, std::uint64_t a_fnv64, std::string& a_why)
    {
        std::uint64_t size = 0;
        std::uint64_t hash = 0;
        if (!HashDataFile(a_dataPath, size, hash)) {
            a_why = std::format("'{}' is not in Data", a_dataPath);
            return false;
        }
        if (size != a_bytes || hash != a_fnv64) {
            a_why = std::format("'{}' is {} bytes, fnv64 {:016x}; {} bytes, {:016x} recorded", a_dataPath, size, hash, a_bytes, a_fnv64);
            return false;
        }
        return true;
    }

    bool ObjectIntact(const SlotLedgerModel::Object& a_object, const SlotKeys::Inputs& a_inputs, std::string& a_why)
    {
        if (!a_object.path.empty() && !DataFileMatches(a_object.path, a_object.bytes, a_object.fnv64, a_why)) {
            return false;
        }
        for (const auto& dep : a_object.deps) {
            if (dep.isAddn) {
                continue;
            }
            if (DataPathRule::ParseCopyPath(dep.path)) {
                std::uint64_t size = 0;
                std::uint64_t hash = 0;
                if (!HashDataFile(dep.path, size, hash) || hash != dep.fnv64) {  // a dep's size is not recorded: its hash decides
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
            files.emplace_back(a_object.path, a_object.bytes);
        }
        for (const auto& dep : a_object.deps) {
            std::uint64_t size = 0;
            std::uint64_t hash = 0;
            if (!dep.isAddn && DataPathRule::ParseCopyPath(dep.path) && HashDataFile(dep.path, size, hash)) {
                files.emplace_back(dep.path, size);
            }
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
        const auto intact = [&](const LM::Entry& a_entry, LM::Role a_role, const SlotAssign::Slot& a_slot) -> const LM::Object* {
            const auto* object = LM::PublishedOf(a_entry, a_role);
            std::string why;
            // the FNV-1a 64 collision guard: the same key with another key string is not this session's object
            const auto& current = a_role == LM::Role::kMuzzle ? a_slot.muzzleKey : a_slot.modelKey;
            const bool collision = object && current && object->key == current->key && !object->keyText.empty() &&
                                   object->keyText != current->text;
            if (collision) {
                why = "its key equals this session's but the key strings differ (an FNV-1a 64 collision)";
            }
            if (!object || collision || !ObjectIntact(*object, a_inputs, why) || !RegisterObject(*object, why)) {
                if (object) {
                    logger::info("BlackFlame: slots: slot {:X} ({}): its last published {} is not used: {}", a_slot.localId,
                        a_slot.sourceText, LM::RoleText(a_role), why);
                }
                return nullptr;
            }
            return object;
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
                    slot.type == CopyContract::SlotType::kTXST || (addnPass && SlotAssign::GateOpen(id))) {
                    continue;  // a TXST slot stays unpublished (CopyGroups.h)
                }
                const auto* model = intact(*entry, addnPass ? LM::Role::kMps : LM::Role::kModel, slot);
                const bool addnReady = !model || std::all_of(model->deps.begin(), model->deps.end(), [](const LM::Dep& a_dep) {
                    const auto addn = a_dep.isAddn ? CopyContract::SlotOfAddnIndex(a_dep.addn) : std::nullopt;
                    return !a_dep.isAddn || (addn && SlotAssign::GateOpen(*addn));
                });
                if (!model || !addnReady) {
                    continue;
                }
                (void)SetFormModel(slot.form, RecordModelPath(model->path));
                slot.publishedModel = RecordModelPath(model->path);
                if (addnPass) {
                    if (auto* addon = slot.form->As<RE::BGSAddonNode>(); addon && slot.source) {
                        SetAddonFlagsFromSource(*addon, *slot.source->As<RE::BGSAddonNode>());
                    }
                    SlotAssign::OpenGate(id);  // an ADDN slot's gate: its MPS is published (CopyOf never hands one out)
                } else if (const auto* muzzle = slot.sourceMuzzle.empty() ? nullptr : intact(*entry, LM::Role::kMuzzle, slot)) {
                    slot.publishedMuzzle = RecordModelPath(muzzle->path);
                }
                ++applied;
            }
            if (addnPass && applied == before) {
                pass = kAddnPasses - 1;  // no ADDN slot opened in this pass: on to the forms
            }
        }
        logger::info("BlackFlame: slots: kDataLoaded: {} slot(s) took their last published generation (gates still closed until "
                     "their groups open)", applied);
        return applied;
    }
}
