#pragma once

// =============================================================================
// FORM COPIES FILL MODEL - a slot's model: the source's as a placeholder, or a published generation (FormCopiesFillModel.cpp)
// =============================================================================
// The copies-map route never touches a copy's own model (its record holds the grey path). A pool slot's record holds a
// placeholder, so the slot route sets it: the source's own model while the slot shows no grey generation, the copy path of
// its published generation when one is verified (TESModel::SetModel, RE/T/TESModel.h:30; BSFixedString is release then
// construct, so only at kDataLoaded or on the main thread before the first load, never in play). A generation from the
// ledger is used only when its files and deps are still what it recorded (fnv64 and size): copies in Data read whole,
// source files by the key inputs' hashes (SlotKeys.h).

#include "Common.h"

#include "SlotKeys.h"
#include "SlotLedgerModel.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace BlackFlame::FormCopies
{
    /// The form's own model component (PROJ, EXPL, IPCT, HAZD, ARTO, ADDN), or null (TXST has none)
    [[nodiscard]] RE::TESModel* ModelOf(RE::TESForm* a_form) noexcept;

    /// A Data-relative mesh path (meshes\...) as a record stores a model: without "meshes\"
    [[nodiscard]] std::string RecordModelPath(std::string_view a_dataPath);

    /// SetModel(a_recordPath) on the form's own model, when it differs; false for a form without one
    bool SetFormModel(RE::TESForm* a_form, const std::string& a_recordPath);

    /// An ADDN slot from its source: the particle cap and the sound; the DNAM flags stay as the ESL ships them (1: on
    /// demand) until the slot's MPS is published at kDataLoaded (SetAddonFlagsFromSource)
    void FillAddonSlot(RE::BGSAddonNode& a_slot, const RE::BGSAddonNode& a_source);

    /// kAlwaysLoaded and the rest of the source's DNAM flags (only for an MPS published at kDataLoaded, design v3 #5)
    void SetAddonFlagsFromSource(RE::BGSAddonNode& a_slot, const RE::BGSAddonNode& a_source);

    /// The file a_dataPath under the Data folder holds a_bytes bytes whose FNV-1a 64 is a_fnv64 (read whole, in-process,
    /// so through MO2's VFS); a_why says what differs
    [[nodiscard]] bool DataFileMatches(const std::string& a_dataPath, std::uint64_t a_bytes, std::uint64_t a_fnv64, std::string& a_why);

    /// A ledger object's own file and every path dep are what it recorded: a copy path in Data (DataFileMatches), any
    /// other path by the key inputs' hash of the engine's bytes. ADDN deps are the caller's (the slot's MPS must be open)
    [[nodiscard]] bool ObjectIntact(const SlotLedgerModel::Object& a_object, const SlotKeys::Inputs& a_inputs, std::string& a_why);

    /// Registers a_object's own file and its copy deps in the engine's index (FileIndex::Register; with the index unusable
    /// the files from an earlier session are the startup index's). False when one is an archive clash or did not register
    [[nodiscard]] bool RegisterObject(const SlotLedgerModel::Object& a_object, std::string& a_why);

    /// W1 (kDataLoaded): each active usable slot's last published generation (the ledger), when intact and every ADDN slot
    /// it names has an intact MPS too, is registered and set as the slot's model; the PROJ muzzle object is recorded for the
    /// group's opening (Slot::publishedMuzzle). Gates stay closed: CopyGroups opens whole groups. Returns how many slots took
    /// a generation
    std::size_t ApplyLastPublished(const SlotKeys::Inputs& a_inputs);
}
