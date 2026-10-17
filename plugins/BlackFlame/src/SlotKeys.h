#pragma once

// =============================================================================
// SLOT KEYS - the bytes the single-launch keys are made of, read at kDataLoaded, and the keys (CopyContractKeys.h)
// =============================================================================
// Read once on the kDataLoaded thread through the engine's file system (NifDump::ReadModel: archives, loose files and MO2's
// VFS as the game loads them):
//   NIFs      every closure form's model, every PROJ muzzle, every ADDN model not ours (the ADDN table part)
//   textures  the dump's plan (CopyRequestData::PlanTextures: every texture those NIFs name and the TXST colour slots,
//             within the dump caps), each planned one read whole for its fnv64; one outside the caps is "notdumped",
//             one that does not open "missing" (CopyContract FileState); our two palettes are read whole (the key prefix)
//   builder   <DLL folder>\HeartOfMagic_BlackFlame\blackflame-greybuild.exe, its bytes' fnv64 (CopyContract
//             kBuilderExeRelative). Missing: no key prefix, so no key this session (nothing new is built); a slot's last
//             published generation can still be shown (it needs no new key)
//   ADDN      the BSValueNode values of the closure's form and muzzle NIFs (NifValueNodes.h): only the ADDN records the
//             engine's addonNodes table gives for those indices get an ADDN slot
// The key functions turn those into CopyContract key strings; both sides hash the same strings (the contract's test table).

#include "Common.h"

#include "CopyContract.h"
#include "CopyRequestData.h"
#include "CopyRequestDump.h"
#include "FireClosure.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace BlackFlame::SlotKeys
{
    namespace CC = CopyContract;

    struct Inputs
    {
        std::vector<CopyRequestDump::Nif> nifs;                     // every NIF read (the dump writes them too)
        std::unordered_map<std::string, std::size_t> nifAt;         // normalised path -> index in nifs
        std::vector<CopyRequestData::Texture> textures;             // the dump's texture plan
        std::unordered_map<std::string, CC::FileHash> textureHash;  // normalised texture path -> its state for a key
        std::optional<CC::KeyPrefix> prefix;                        // nullopt: no new key this session (prefixWhy)
        std::string prefixWhy;
        std::filesystem::path builderExe;
        bool builderExeFound = false;
        std::vector<CC::AddnRecord> addnRecords;                    // every ADDN record not ours
        std::set<std::uint32_t> valueNodeIndices;                   // AddOnNode values the closure's NIFs name
        std::size_t valueNodeUnparsed = 0;                          // closure NIFs with BSValueNode whose header did not parse
        std::size_t nifsFailed = 0;
        long long nifMs = 0;
        long long textureMs = 0;
        std::uint64_t textureBytesHashed = 0;
    };

    /// kDataLoaded thread: every read above (each step's time logged)
    [[nodiscard]] Inputs Read(const FireClosure& a_closure);

    /// <directory of this DLL>\HeartOfMagic_BlackFlame\blackflame-greybuild.exe, or empty when the DLL's path cannot be read
    [[nodiscard]] std::filesystem::path BuilderExePath();

    /// A NIF's state for a key (by its record model text): hashed, or missing when it did not read
    [[nodiscard]] CC::FileHash NifHash(const Inputs& a_inputs, const std::string& a_recordModel);

    /// The ADDN table hash of a_pairs (every ADDN slot binding of the ledger) and the load order's ADDN records
    [[nodiscard]] std::uint64_t AddnTableHash(const Inputs& a_inputs, const std::vector<CC::LedgerPair>& a_pairs);

    /// A NIF object's key (form model, PROJ muzzle, ADDN MPS); nullopt without a prefix or for a path Normalize refuses
    [[nodiscard]] std::optional<std::uint64_t> NifKey(const Inputs& a_inputs, CC::NifKind a_kind, const std::string& a_recordModel,
        std::uint64_t a_addnTableHash);

    /// A TXST slot's key from its source's eight slots, TX00 / TX03's states and its DODT; nullopt without a prefix
    [[nodiscard]] std::optional<std::uint64_t> TxstKey(const Inputs& a_inputs, const RE::BGSTextureSet& a_source);

    /// a_texture's state by its raw slot text (the plan's table, or one read now when the plan has no entry)
    [[nodiscard]] CC::FileHash TextureHash(const Inputs& a_inputs, std::string_view a_raw);
}
