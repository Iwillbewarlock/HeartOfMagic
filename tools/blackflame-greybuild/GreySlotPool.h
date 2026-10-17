#pragma once

// =============================================================================
// GreySlotPool - the slot pool ESL of the single-launch copies (README "Single launch: the builder (P1)")
// =============================================================================
// HeartOfMagic_BlackFlame_Slots.esp: ESL, no masters, HEDR 1.70, one placeholder record per slot of the P0 pool table
// (plugins/BlackFlame/src/CopyContract.h kSlotPools), FormIDs 0x800-0xFCF, groups in Skyrim.esm's order TXST, PROJ,
// HAZD, EXPL, ADDN, IPCT, ARTO. Records as the copies ESL writes them (GreyFormsEsl.cpp), with nothing from a source:
//   PROJ, HAZD, EXPL, IPCT  EDID, OBND 0 (not IPCT), DATA in Skyrim.esm's size, every value and form link 0; PROJ VNAM 0
//   ARTO                    EDID, OBND 0, DNAM art type 0
//   TXST                    EDID, OBND 0, no texture slot, the 36-byte DODT stub on the first 72 (0xEA0-0xEE7) only,
//                           DNAM flags 0
//   ADDN                    EDID, OBND as Skyrim.esm's MPS records, MODL kSlotAddnModel (a path no file has: the DLL
//                           sets the model before the slot is used), DATA index 40000 + place, DNAM cap 0, flags 1
// No record has a MODL but the ADDNs: the DLL sets every slot's model (the source's, or the grey copy) at kDataLoaded.
// EditorIDs: HoMBF_S_<TYPE>_<local id, 3 hex digits>.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace GreyBuild
{
    inline constexpr const char* kSlotsPluginName = "HeartOfMagic_BlackFlame_Slots.esp";
    inline constexpr const char* kSlotEditorIdPrefix = "HoMBF_S_";
    // The slot ADDNs' MODL (no file has it; Skyrim.esm's ADDN records all carry a MODL, so the slots do too)
    inline constexpr const char* kSlotAddnModel = "HeartOfMagic\\BlackFlame\\Slots\\Unset.nif";

    /// The pool ESL into a_file (folders made). Throws std::runtime_error on a write failure.
    void WriteSlotPool(const std::filesystem::path& a_file);

    /// a_file read back with the builder's byte readers and compared with the pool table; the differences (empty = it
    /// matches)
    [[nodiscard]] std::vector<std::string> VerifySlotPool(const std::filesystem::path& a_file);

    struct AddnCollision
    {
        int index = 0;           // the request's ADDN index (in 40000-40207)
        std::uint16_t slot = 0;  // the slot ADDN that index belongs to
        std::string source;      // the record, as the request names it
        std::string model;
    };

    /// The ADDN records of a request (its "addons": a --request file or the game's request.json) whose index falls in the
    /// slot pool's range, our own records ("own": true) left out. Throws std::runtime_error naming a bad field.
    [[nodiscard]] std::vector<AddnCollision> FindAddnCollisions(const std::filesystem::path& a_request);

    /// --write-slot-pool: written beside a_file, read back, then moved into place. Exit 0 / 1.
    [[nodiscard]] int RunWriteSlotPool(const std::filesystem::path& a_file);

    /// --collisions: one line per collision. Exit 0 = none, 1 = some (or the request does not read).
    [[nodiscard]] int RunCollisions(const std::filesystem::path& a_request);
}
