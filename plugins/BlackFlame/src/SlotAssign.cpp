// =============================================================================
// SlotAssign - a permanent pool slot for each closure source (out of FormCopies.cpp)
// =============================================================================

#include "SlotAssign.h"

#include "Config.h"
#include "FormCopiesFillModel.h"
#include "FormCopiesState.h"
#include "SlotLedger.h"
#include "SlotNotice.h"

#include <array>
#include <atomic>
#include <format>
#include <memory>
#include <set>
#include <unordered_map>

namespace BlackFlame::SlotAssign
{
    namespace
    {
        namespace LM = SlotLedgerModel;
        constexpr std::uint32_t kLightLocalMask = 0xFFF;
        constexpr std::uint32_t kFullLocalMask = 0xFFFFFF;
        constexpr std::uint32_t kLightIdShift = 12;
        constexpr std::size_t kLoggedEach = 20;  // pool full / refused lines before a count

        // Tombstones whose source is gone are filled from these Skyrim.esm forms (one harmless real form a type; checked at
        // runtime: a template that does not resolve with its type leaves the slot as the ESL wrote it, gated)
        struct Template
        {
            CC::SlotType type;
            std::uint32_t localId;
        };
        constexpr std::array<Template, 7> kTemplates{ { { CC::SlotType::kPROJ, 0x10B0A5 }, { CC::SlotType::kEXPL, 0x0439C0 },
            { CC::SlotType::kIPCT, 0x03FA50 }, { CC::SlotType::kHAZD, 0x095457 }, { CC::SlotType::kARTO, 0x01B211 },
            { CC::SlotType::kTXST, 0x06051D }, { CC::SlotType::kADDN, 0x10FBEF } } };
        constexpr const char* kTemplatePlugin = "Skyrim.esm";

        struct Table
        {
            std::vector<Slot> slots = std::vector<Slot>(CC::kSlotCount);  // by local id - 0x800; never resized
            std::vector<std::uint16_t> assigned;
            Counts counts;
        };
        Table& TheTable()
        {
            static auto* table = std::make_unique<Table>().release();
            return *table;
        }
        std::array<std::atomic<std::uint8_t>, CC::kSlotCount> g_gates{};
        std::atomic<bool> g_active{ false };
        std::atomic<RE::FormID> g_base{ 0 };  // the slot ESL's light-plugin FormID base (0xFExxx000); 0 before kDataLoaded

        [[nodiscard]] std::size_t IndexOf(std::uint16_t a_slot) noexcept { return static_cast<std::size_t>(a_slot - CC::kFirstEslLocalId); }

        RE::FormType FormTypeOf(CC::SlotType a_type)
        {
            switch (a_type) {
            case CC::SlotType::kPROJ: return RE::FormType::Projectile;
            case CC::SlotType::kIPCT: return RE::FormType::Impact;
            case CC::SlotType::kEXPL: return RE::FormType::Explosion;
            case CC::SlotType::kARTO: return RE::FormType::ArtObject;
            case CC::SlotType::kHAZD: return RE::FormType::Hazard;
            case CC::SlotType::kTXST: return RE::FormType::TextureSet;
            case CC::SlotType::kADDN: return RE::FormType::AddonNode;
            case CC::SlotType::kNone: break;
            }
            return RE::FormType::None;
        }

        std::optional<CC::SlotType> SlotTypeOf(CopyKind a_kind)
        {
            switch (a_kind) {
            case CopyKind::kProjectile: return CC::SlotType::kPROJ;
            case CopyKind::kExplosion: return CC::SlotType::kEXPL;
            case CopyKind::kImpact: return CC::SlotType::kIPCT;
            case CopyKind::kHazard: return CC::SlotType::kHAZD;
            case CopyKind::kArt: return CC::SlotType::kARTO;
            case CopyKind::kTextureSet: return CC::SlotType::kTXST;
            }
            return std::nullopt;
        }

        // A form by its plugin's file name (the engine's bytes) and local id; the id must fit the file
        RE::TESForm* Lookup(RE::TESDataHandler* a_dh, const std::string& a_plugin, std::uint32_t a_local, CC::SlotType a_type)
        {
            const auto* file = a_dh->LookupModByName(a_plugin);
            if (!file) {  // a save's BFSL stores names ASCII-lower-cased (SlotCoSave Canonical): the case is not the file's
                for (const auto* candidate : a_dh->files) {
                    if (candidate && LM::SamePlugin(candidate->GetFilename(), a_plugin)) {
                        file = candidate;
                        break;
                    }
                }
            }
            if (!file || a_local > (file->IsLight() ? kLightLocalMask : kFullLocalMask)) {
                return nullptr;
            }
            auto* form = a_dh->LookupForm(a_local, file->GetFilename());
            return form && form->GetFormType() == FormTypeOf(a_type) ? form : nullptr;
        }

        std::optional<LM::Want> WantOf(const RE::TESForm* a_form, CC::SlotType a_type)
        {
            const auto* file = a_form ? a_form->GetFile(0) : nullptr;
            if (!file) {
                return std::nullopt;  // made at runtime: no defining file, no stable binding
            }
            LM::Want want{ a_type, std::string(file->GetFilename()), a_form->GetFormID() & (file->IsLight() ? kLightLocalMask : kFullLocalMask) };
            if (const auto* set = a_form->As<RE::BGSTextureSet>()) {
                want.txstDodt = set->decalData != nullptr;
            }
            return want;
        }

        std::vector<LM::Want> Wants(const FireClosure& a_closure, const SlotKeys::Inputs& a_inputs, RE::TESDataHandler* a_dh)
        {
            std::vector<LM::Want> wants;
            std::set<std::tuple<CC::SlotType, std::string, std::uint32_t>> seen;
            const auto add = [&](const RE::TESForm* a_form, CC::SlotType a_type) {
                auto want = WantOf(a_form, a_type);
                if (want && seen.emplace(want->type, want->plugin, want->localId).second) {
                    wants.push_back(std::move(*want));
                }
            };
            for (const auto& form : a_closure.forms) {
                if (const auto type = SlotTypeOf(form.kind); type && !IsOwnPluginForm(form.form)) {  // never a slot for a slot
                    add(form.form, *type);
                }
            }
            const auto& nodes = a_dh->addonNodes;
            for (const auto index : a_inputs.valueNodeIndices) {
                auto* addon = index < nodes.free_idx() ? nodes[static_cast<std::uint16_t>(index)] : nullptr;
                if (addon && !IsOwnPluginForm(addon) && !CC::SlotOfAddnIndex(index)) {
                    add(addon, CC::SlotType::kADDN);
                }
            }
            return wants;
        }

        // A source's data into its slot (FormCopies' per-type fill; an ADDN its cap and sound) and its model as placeholder
        // a_models false (the preflight at kPreLoadGame, review decision 6): data only, no SetModel of the model or the muzzle
        bool Fill(Slot& a_slot, const RE::TESForm* a_from, bool a_models = true)
        {
            bool filled = false;
            if (a_slot.type == CC::SlotType::kADDN) {
                auto* slot = a_slot.form->As<RE::BGSAddonNode>();
                const auto* from = a_from->As<RE::BGSAddonNode>();
                if (slot && from) {
                    FormCopies::FillAddonSlot(*slot, *from);
                    filled = true;
                }
            } else {
                filled = CopyFill::FillData(a_slot.form, a_from, a_models);
            }
            const auto model = ModelPathOf(a_from);
            a_slot.sourceModel = model;
            if (a_models) {
                (void)FormCopies::SetFormModel(a_slot.form, model);
            }
            if (const auto* projectile = a_from->As<RE::BGSProjectile>()) {
                a_slot.sourceMuzzle = CopyRequestData::MuzzleModelOf(*projectile);
            }
            return filled;
        }

        // A foreign ADDN record with a slot's AddOnNode index, or an addonNodes entry that is not the slot record
        void MarkCollisions(Table& a_table, RE::TESDataHandler* a_dh)
        {
            std::set<std::uint16_t> claimed;
            for (const auto* addon : a_dh->GetFormArray<RE::BGSAddonNode>()) {
                if (addon && !IsSlotForm(addon)) {
                    if (const auto slot = CC::SlotOfAddnIndex(addon->index)) {
                        claimed.insert(*slot);
                    }
                }
            }
            const auto& nodes = a_dh->addonNodes;
            for (const auto id : a_table.assigned) {
                auto& slot = a_table.slots[IndexOf(id)];
                if (slot.type != CC::SlotType::kADDN) {
                    continue;
                }
                const auto index = *CC::AddnIndexOfSlot(id);
                auto* entry = index < nodes.free_idx() ? nodes[static_cast<std::uint16_t>(index)] : nullptr;
                if (claimed.contains(id) || entry != slot.form) {
                    slot.usable = false;
                    slot.collided = true;
                    slot.why = std::format("ADDN index {} collided (another record claims it, or addonNodes[{}] is not the slot)", index, index);
                    ++a_table.counts.collided;
                }
            }
        }

        void ComputeKeys(Table& a_table, const SlotKeys::Inputs& a_inputs, const LM::Ledger& a_ledger)
        {
            std::vector<CC::LedgerPair> pairs;
            for (const auto& entry : a_ledger.entries) {
                if (entry.type == CC::SlotType::kADDN && entry.state != LM::State::kConflict) {
                    pairs.push_back({ entry.slot, entry.plugin, entry.localId });
                }
            }
            const auto addnTable = SlotKeys::AddnTableHash(a_inputs, pairs);
            for (const auto id : a_table.assigned) {
                auto& slot = a_table.slots[IndexOf(id)];
                if (!slot.active || !slot.source) {
                    continue;
                }
                if (slot.type == CC::SlotType::kTXST) {
                    if (GetConfig().slotTxst) {  // off: no TXST build is requested (none would be published: CopyGroups.h)
                        slot.txstKey = SlotKeys::TxstKey(a_inputs, *slot.source->As<RE::BGSTextureSet>());
                    }
                    continue;
                }
                const auto kind = slot.type == CC::SlotType::kADDN ? CC::NifKind::kMps : CC::NifKind::kModel;
                slot.modelKey = SlotKeys::NifKey(a_inputs, kind, slot.sourceModel, addnTable);
                if (!slot.sourceMuzzle.empty()) {
                    slot.muzzleKey = SlotKeys::NifKey(a_inputs, CC::NifKind::kMuzzle, slot.sourceMuzzle, addnTable);
                }
            }
        }

        // The copies table the hooks read (FormCopiesState.h): the active usable slots, built once here
        void BuildCopiesTable(Table& a_table)
        {
            auto& copies = CopiesState::TheCopies();
            std::unordered_map<const RE::TESForm*, RE::FormID> known;
            for (const auto id : a_table.assigned) {
                const auto& slot = a_table.slots[IndexOf(id)];
                if (!slot.usable || !slot.source) {
                    continue;
                }
                CopiesState::Entry entry;
                entry.sourceText = slot.sourceText;
                entry.type = FormTypeOf(slot.type);
                entry.source = slot.source;
                entry.copy = slot.form;
                entry.ok = slot.type != CC::SlotType::kADDN;  // an ADDN slot is reached through addonNodes, not CopyOf
                if (!entry.ok) {
                    continue;
                }
                copies.bySource.emplace(slot.source->GetFormID(), slot.form);
                copies.byCopy.emplace(slot.form->GetFormID(), slot.source);
                known.emplace(slot.source, slot.source->GetFormID());
                known.emplace(slot.form, slot.form->GetFormID());
                auto& c = copies.counts;
                ++(slot.type == CC::SlotType::kPROJ ? c.projectiles : slot.type == CC::SlotType::kEXPL ? c.explosions :
                    slot.type == CC::SlotType::kIPCT ? c.impacts : slot.type == CC::SlotType::kHAZD ? c.hazards :
                    slot.type == CC::SlotType::kARTO ? c.arts : c.textureSets);
                copies.entries.push_back(std::move(entry));
            }
            copies.counts.mapRead = true;  // the slot table stands in for the map (CheckCopies' first line)
            copies.counts.mapVersion = 0;
            copies.counts.mapEntries = copies.entries.size();
            CopyFill::SetKnownForms(std::move(known));
        }

        void OnMismatch(const std::vector<LM::Mismatch>& a_mismatches, SlotLedger::MismatchWhen a_when);
    }

    bool AssignAndFill(const FireClosure& a_closure, const SlotKeys::Inputs& a_inputs)
    {
        auto& table = TheTable();
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh || !SlotLedger::GetStatus().available) {
            logger::warn("BlackFlame: slots: no ledger this session - no slots (every fire form stays orange)");
            return false;
        }
        const auto* file = dh->LookupModByName(kSlotsPluginName);
        // review agy 14_01 #9: a light file's partial index is 0xFExxx, never 0xFF; an unloaded file has compile index 0xFF
        if (!file || !file->IsLight() || file->compileIndex == 0xFF) {
            logger::warn("BlackFlame: slots: {} is not loaded as a light plugin - no slots this session", kSlotsPluginName);
            return false;
        }
        g_base.store(static_cast<RE::FormID>(file->GetPartialIndex()) << kLightIdShift);
        const auto wants = Wants(a_closure, a_inputs, dh);
        table.counts.wanted = wants.size();
        LM::AssignResult result;
        LM::Ledger ledger;
        const auto status = SlotLedger::GetStatus();
        LM::Ledger before;
        SlotLedger::Edit([&](LM::Ledger& a_ledger) {
            before = a_ledger;
            result = LM::Assign(a_ledger, wants, status.allowNew, SlotLedger::Session());
            ledger = a_ledger;
        });
        table.counts.session = SlotLedger::Session();
        for (std::size_t i = 0; i < wants.size(); ++i) {  // T0-slots: a source the ledger held keeps its slot
            const auto& want = wants[i];
            const auto held = std::ranges::find_if(before.entries, [&](const LM::Entry& a_e) {
                return a_e.state != LM::State::kConflict && LM::SameSource(a_e, want.type, want.plugin, want.localId) && LM::SlotFits(a_e.slot, want);
            });
            if (held != before.entries.end()) {
                (result.slots[i] == held->slot ? table.counts.kept : table.counts.moved) += 1;
            }
        }
        table.counts.poolFull = result.poolFull.size();
        table.counts.refused = result.refused.size();
        for (std::size_t i = 0; i < result.poolFull.size() && i < kLoggedEach; ++i) {
            const auto& want = wants[result.poolFull[i]];
            logger::warn("BlackFlame: slots: pool full: {} {} stays orange", CC::SlotTypeTag(want.type), LM::SourceText(want.plugin, want.localId));
        }
        if (!result.refused.empty()) {
            logger::warn("BlackFlame: slots: {} source(s) need a new slot, refused this session ({}): they stay orange",
                result.refused.size(), status.why);
        }
        for (const auto& entry : ledger.entries) {
            if (!CC::PoolOf(entry.slot)) {
                continue;  // FromJson refuses such a slot; kept as the one guard before the table index
            }
            auto& slot = table.slots[IndexOf(entry.slot)];
            slot.localId = entry.slot;
            slot.type = entry.type;
            slot.sourceText = LM::SourceText(entry.plugin, entry.localId);
            slot.form = dh->LookupForm(entry.slot, kSlotsPluginName);
            if (!slot.form || slot.form->GetFormType() != FormTypeOf(entry.type)) {
                slot.why = std::format("the pool form {:X} is missing or not a {}", entry.slot, CC::SlotTypeTag(entry.type));
                logger::warn("BlackFlame: slots: {} ({}): not filled", slot.why, slot.sourceText);
                slot.form = nullptr;
                continue;
            }
            slot.assigned = true;
            table.assigned.push_back(entry.slot);
            slot.source = Lookup(dh, entry.plugin, entry.localId, entry.type);
            slot.active = entry.state == LM::State::kActive && slot.source;
            const RE::TESForm* from = slot.source;
            if (!from) {
                const auto it = std::ranges::find_if(kTemplates, [&](const Template& a_t) { return a_t.type == entry.type; });
                from = it != kTemplates.end() ? Lookup(dh, kTemplatePlugin, it->localId, entry.type) : nullptr;
                table.counts.templates += from ? 1 : 0;
            }
            if (from && !Fill(slot, from)) {
                slot.why = "the fill refused the type";
            }
            slot.filledFrom = from;
            const auto refusal = slot.source ? CopyFill::Refusal(slot.source) : std::string();
            slot.usable = slot.active && from && slot.why.empty() && refusal.empty() &&
                          (entry.type != CC::SlotType::kTXST || CC::TxstHasDodt(entry.slot) == (slot.source->As<RE::BGSTextureSet>()->decalData != nullptr));
            if (slot.active && !slot.usable && slot.why.empty()) {
                slot.why = refusal.empty() ? "its DODT stub does not match the source" : refusal;
            }
            slot.txstDodt = entry.type == CC::SlotType::kTXST && CC::TxstHasDodt(entry.slot);
            table.counts.active += slot.active ? 1 : 0;
            table.counts.tombstones += entry.state == LM::State::kTombstone ? 1 : 0;
            table.counts.conflicts += entry.state == LM::State::kConflict ? 1 : 0;
        }
        MarkCollisions(table, dh);
        for (const auto id : table.assigned) {
            table.counts.usable += table.slots[IndexOf(id)].usable ? 1 : 0;
        }
        ComputeKeys(table, a_inputs, ledger);
        BuildCopiesTable(table);
        SlotLedger::SetSessionFill(ledger);
        SlotLedger::SetMismatchHandler(&OnMismatch);
        g_active.store(true);
        (void)SlotLedger::Save("kDataLoaded: this session's assignment");
        const auto& c = table.counts;
        logger::info("BlackFlame: slots: {} wanted; kept {}, re-activated {}, new {}, tombstoned {}; {} pool full, {} refused; {} "
                     "slots filled ({} active, {} usable, {} tombstones, {} conflicts, {} from a template, {} ADDN collided); every gate "
                     "closed until its group is published", c.wanted, result.kept, result.reactivated, result.assigned, result.tombstoned,
            c.poolFull, c.refused, table.assigned.size(), c.active, c.usable, c.tombstones, c.conflicts, c.templates, c.collided);
        return true;
    }

    bool Active() noexcept
    {
        return g_active.load();
    }

    bool IsSlotForm(const RE::TESForm* a_form) noexcept
    {
        return SlotOf(a_form).has_value();
    }

    std::optional<std::uint16_t> SlotOf(const RE::TESForm* a_form) noexcept
    {
        const auto base = g_base.load();
        if (!a_form || base == 0) {
            return std::nullopt;
        }
        const auto id = a_form->GetFormID();
        const auto local = static_cast<std::uint16_t>(id & kLightLocalMask);
        if ((id & ~kLightLocalMask) != base || !CC::PoolOf(local)) {
            return std::nullopt;
        }
        return local;
    }

    bool IsGated(const RE::TESForm* a_form) noexcept
    {
        const auto slot = SlotOf(a_form);
        return slot && !GateOpen(*slot);
    }

    bool GateOpen(std::uint16_t a_slot) noexcept
    {
        return CC::PoolOf(a_slot) && g_gates[IndexOf(a_slot)].load(std::memory_order_acquire) != 0;
    }

    void OpenGate(std::uint16_t a_slot) noexcept
    {
        if (CC::PoolOf(a_slot)) {
            g_gates[IndexOf(a_slot)].store(1, std::memory_order_release);
        }
    }

    void CloseGate(std::uint16_t a_slot) noexcept
    {
        if (CC::PoolOf(a_slot)) {
            g_gates[IndexOf(a_slot)].store(0, std::memory_order_release);
        }
    }

    Slot& At(std::uint16_t a_slot)
    {
        return TheTable().slots[IndexOf(a_slot)];
    }

    const std::vector<std::uint16_t>& Assigned() noexcept
    {
        return TheTable().assigned;
    }

    const char* PublishedMuzzle(const RE::TESForm* a_copy) noexcept
    {
        const auto slot = SlotOf(a_copy);
        if (!slot || !GateOpen(*slot)) {
            return nullptr;
        }
        const auto& muzzle = TheTable().slots[IndexOf(*slot)].publishedMuzzle;
        return muzzle.empty() ? nullptr : muzzle.c_str();
    }

    Counts GetCounts() noexcept
    {
        auto counts = TheTable().counts;
        counts.open = 0;
        for (const auto& gate : g_gates) {
            counts.open += gate.load(std::memory_order_relaxed) != 0 ? 1 : 0;
        }
        return counts;
    }

    namespace
    {
        void OnMismatch(const std::vector<LM::Mismatch>& a_mismatches, SlotLedger::MismatchWhen a_when)
        {
            using When = SlotLedger::MismatchWhen;
            auto& table = TheTable();
            if (a_when == When::kUnreadable) {
                std::size_t closed = 0;
                for (const auto id : table.assigned) {
                    closed += GateOpen(id) ? 1 : 0;
                    CloseGate(id);
                    table.slots[IndexOf(id)].usable = false;  // sticky for the session
                }
                SlotNotice::Show(std::format("Black Flame: this save's slot list could not be read; grey is off for {} spells", closed));
                return;
            }
            auto* dh = RE::TESDataHandler::GetSingleton();
            for (const auto& mismatch : a_mismatches) {
                if (!CC::PoolOf(mismatch.slot)) {
                    continue;  // review Gemini / agy 14_01: a co-save's slot number is checked before the table index
                }
                CloseGate(mismatch.slot);
                auto& slot = table.slots[IndexOf(mismatch.slot)];
                slot.usable = false;  // sticky for the session: nothing publishes a slot a save binds otherwise
                if (a_when != When::kPreflight || !dh) {
                    continue;
                }
                if (slot.form && slot.type != mismatch.save.type) {
                    logger::warn("BlackFlame: slots: preflight: slot {:X} is a {} here, the save says {}: not refilled", mismatch.slot,
                        CC::SlotTypeTag(slot.type), CC::SlotTypeTag(mismatch.save.type));
                    continue;
                }
                // slotPreflight: the slot holds the save's source's data before the engine reads the save; its model is left
                // as it is (review decision 6: no SetModel at kPreLoadGame). A slot this session never filled is set up first
                if (!slot.form) {
                    auto* form = dh->LookupForm(mismatch.slot, kSlotsPluginName);
                    if (!form || form->GetFormType() != FormTypeOf(mismatch.save.type)) {
                        continue;
                    }
                    slot.form = form;
                    slot.localId = mismatch.slot;
                    slot.type = mismatch.save.type;
                    slot.sourceText = LM::SourceText(mismatch.save.plugin, mismatch.save.sourceLocalId);
                }
                if (auto* source = Lookup(dh, mismatch.save.plugin, mismatch.save.sourceLocalId, mismatch.save.type)) {
                    (void)Fill(slot, source, false);
                    slot.source = source;
                    slot.filledFrom = source;
                    slot.sourceText = LM::SourceText(mismatch.save.plugin, mismatch.save.sourceLocalId);
                    slot.modelKey.reset();  // this session's keys were the other source's
                    slot.muzzleKey.reset();
                    slot.txstKey.reset();
                    SlotLedger::NoteRefilled(mismatch.slot, mismatch.save);
                    logger::info("BlackFlame: slots: preflight: slot {:X} refilled from {} (gated for the session)", mismatch.slot,
                        LM::SourceText(mismatch.save.plugin, mismatch.save.sourceLocalId));
                }
            }
            if (a_when == When::kAfterLoad && !a_mismatches.empty()) {
                SlotNotice::Show("Black Flame: grey copies are off for some spells in this save until you restart the game.");
            }
        }
    }
}
