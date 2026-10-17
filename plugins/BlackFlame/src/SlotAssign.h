#pragma once

// =============================================================================
// SLOT ASSIGN - a permanent pool slot for each closure source (out of FormCopies.cpp)
// =============================================================================
// Single launch: the grey copies are the records of the slot ESL HeartOfMagic_BlackFlame_Slots.esp (written once by the
// builder's --write-slot-pool, P1; the name is not in the P0 contract: design v3 "P1 builder" line), local ids 0x800-0xFCF
// in the CopyContract pool table. At kDataLoaded, after the fire closure and the key inputs (SlotKeys.h):
//   wants     every closure form of a copy type (PROJ, EXPL, IPCT, HAZD, ARTO, TXST - a TXST by its DODT), and every ADDN
//             record the engine's addonNodes table gives for an AddOnNode index a closure NIF names (NifValueNodes.h)
//   assign    SlotLedgerModel::Assign on the ledger (SlotLedger.h): kept, re-activated, or a never-used id; pool full and
//             refused ones stay orange (logged, counted)
//   fill      every ledger entry's pool form, whatever its state, from its source (FormCopies' per-type data fill; an ADDN
//             slot its particle cap and sound, its DNAM flags left as the ESL ships them, 1) or, when the source is gone, from
//             a fixed vanilla template of the type (a saved reference then loads as a real, harmless form), and its model set
//             to the source's own (TESModel::SetModel) - a slot with no published generation shows the source's look
//   gate      one atomic byte per slot, closed at the start. CopyOf hands a slot out only while its gate is open (so every
//             link through LinkFor goes to the source while closed). Only active, usable slots are ever opened (CopyGroups)
//   keys      this session's model / muzzle / MPS / TXST keys per active slot (SlotKeys)
// Then the copies table (FormCopiesState.h) holds the active usable slots, so every hook finds them through CopyOf /
// SourceOf as it found the copies-map copies; the per-type counts are the slots assigned (hooks install when they exist).
// A slot ADDN's index (40000 + n) that another loaded ADDN record also claims, or whose addonNodes entry is not the slot
// record, is collided: never published. The ESL missing or a slot form of the wrong type: no slots this session (logged).

#include "Common.h"

#include "CopyContractKeys.h"
#include "FireClosure.h"
#include "SlotKeys.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace BlackFlame::SlotAssign
{
    namespace CC = CopyContract;

    inline constexpr const char* kSlotsPluginName = "HeartOfMagic_BlackFlame_Slots.esp";

    struct Slot
    {
        bool assigned = false;               // filled this session (a ledger entry of any state)
        std::uint16_t localId = 0;
        CC::SlotType type = CC::SlotType::kNone;
        RE::TESForm* form = nullptr;         // the pool form
        RE::TESForm* source = nullptr;       // the binding's source (null when it is gone)
        const RE::TESForm* filledFrom = nullptr;  // source, or the template
        std::string sourceText;              // the binding, "plugin|localId"
        bool active = false;                 // bound to a source of this session's closure
        bool usable = false;                 // active and publishable (forms right, not a conflict, not collided)
        std::string why;                     // why not usable
        std::string sourceModel;             // the source's model as its record stores it
        std::string sourceMuzzle;            // PROJ: the source's muzzle flash model
        std::optional<SlotKeys::Key> modelKey;   // kModel (an ADDN slot: its MPS, kMps)
        std::optional<SlotKeys::Key> muzzleKey;  // PROJ with a muzzle
        std::optional<SlotKeys::Key> txstKey;    // TXST
        bool txstDodt = false;
        std::string publishedModel;          // main thread: the record-form path SetModel gave ("" = the source's model)
        std::string publishedMuzzle;         // main thread: PROJ ("" = the source's muzzle)
    };

    /// kDataLoaded (singleLaunch), after SlotLedger::Load and SlotKeys::Read: assign, fill, gates closed, keys, the copies
    /// table. False: no slots this session (the reason logged)
    bool AssignAndFill(const FireClosure& a_closure, const SlotKeys::Inputs& a_inputs);

    /// The slot route filled its table this session
    [[nodiscard]] bool Active() noexcept;

    /// a_form is one of the slot ESL's pool forms (any thread)
    [[nodiscard]] bool IsSlotForm(const RE::TESForm* a_form) noexcept;

    /// The slot's local id of a pool form, or nullopt
    [[nodiscard]] std::optional<std::uint16_t> SlotOf(const RE::TESForm* a_form) noexcept;

    /// A pool form whose gate is closed (CopyOf then returns null for it); false for every other form (any thread)
    [[nodiscard]] bool IsGated(const RE::TESForm* a_form) noexcept;

    [[nodiscard]] bool GateOpen(std::uint16_t a_slot) noexcept;

    /// Main thread: the gate opens (release: the published model and muzzle written before are seen by every reader)
    void OpenGate(std::uint16_t a_slot) noexcept;

    /// Any thread: the gate closes (a loaded save's mismatch, an unreadable slot list)
    void CloseGate(std::uint16_t a_slot) noexcept;

    /// The slot (kDataLoaded and the main thread); a_slot must be in the pool (0x800-0xFCF)
    [[nodiscard]] Slot& At(std::uint16_t a_slot);

    /// Every assigned slot's local id, in id order
    [[nodiscard]] const std::vector<std::uint16_t>& Assigned() noexcept;

    /// v3.2 §6: the grey muzzle a PROJ slot holds only while it is published and open; null otherwise (the source's muzzle)
    [[nodiscard]] const char* PublishedMuzzle(const RE::TESForm* a_copy) noexcept;

    struct Counts
    {
        std::size_t wanted = 0;      // closure sources of a copy type and named ADDNs
        std::size_t active = 0;      // active slots
        std::size_t usable = 0;      // of them, publishable
        std::size_t tombstones = 0;
        std::size_t conflicts = 0;
        std::size_t poolFull = 0;
        std::size_t refused = 0;     // new slots refused (total loss with an unreadable co-save)
        std::size_t templates = 0;   // filled from a template (source gone)
        std::size_t collided = 0;    // ADDN slots whose index another record claims
        std::size_t open = 0;        // gates open now
        std::size_t kept = 0;        // sources the ledger already held, in the same slot (T0-slots: assignment stable)
        std::size_t moved = 0;       // sources the ledger held that are in another slot now (stable: 0)
        std::uint32_t session = 0;   // this start's session number (1: the ledger's first)
    };

    [[nodiscard]] Counts GetCounts() noexcept;
}
