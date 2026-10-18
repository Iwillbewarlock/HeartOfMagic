#pragma once

// =============================================================================
// GreyForms - Black Flame v2 copies (README "Black Flame v2", --forms)
// =============================================================================
// A forms request lists source forms (PROJ, EXPL, IPCT, HAZD, ARTO, TXST) of one load order with
// their model path, object bounds and scalar DATA values (ARTO: its DNAM art type; TXST: its eight texture slots, DNAM
// flags and whether it has DODT - GreyFormsTxst.h). The builder writes:
//   - a masterless ESL (HeartOfMagic_BlackFlame_Copies.esp) of placeholder records,
//     one per source: EDID, OBND (types that have it), MODL = the fully grey copy of
//     the source NIF, DATA in Skyrim.esm's size for the type with the scalars copied
//     and every form link zero (VNAM sound level on PROJ; ARTO has no DATA, only DNAM).
//     Form version 44, HEDR 1.70,
//     FormIDs 0x800 up, stable across re-runs through the copies map.
//   - the grey NIFs under meshes\heartofmagic\blackflame\copies\ (every effect shader
//     on the grey palette and AddOnNodes on grey ADDN indices: the grey map built in
//     the same run from --request).
//   - SKSE\Plugins\HeartOfMagic_BlackFlame_copies.json: source -> copy, the stable-id
//     file (retired ids are kept so they are never given to another source).
// The DLL fills each copy from the live winning source at kDataLoaded (FormCopies.h).

#include "GreyBuild.h"
#include "GreyPalette.h"

#include "../../plugins/BlackFlame/src/CopyContract.h"
#include "GreyTexture.h"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace GreyBuild
{
    inline constexpr const char* kCopiesPluginName = "HeartOfMagic_BlackFlame_Copies.esp";
    inline constexpr const char* kCopiesMapName = "HeartOfMagic_BlackFlame_copies.json";
    inline constexpr const char* kCopyRoot = "meshes\\heartofmagic\\blackflame\\copies\\";
    inline constexpr const char* kCopyEditorIdPrefix = "HoMBF_C_";  // + type + "_" + the copy's local id (3 hex)
    inline constexpr int kFormsRequestVersion = 2;  // hand-made requests (forms only)
    inline constexpr int kGameRequestVersion = 4;   // the DLL's request (forms, addons, models, nifs, fingerprint, dumpComplete)
    inline constexpr int kGameRequestVersionOld = 3;  // AD1: the same without dumpComplete (read as complete)
    inline constexpr int kGameRequestVersionKeyed = 5;  // single launch: version 4 plus the keys (GreyKeyed.h); --out reads it too
    inline constexpr int kCopiesMapVersion = 2;     // version 1 maps are still read

    enum class CopyType
    {
        kProjectile,
        kHazard,
        kExplosion,
        kImpact,
        kArtObject,
        kTextureSet  // decal texture sets (TXST) of copied PROJ / IPCT
    };

    // TXST: the eight TX00..TX07 slots (CommonLibSSE-NG RE/B/BSTextureSet.h Textures, kUsedTotal); the colour slots the
    // builder recolours are diffuse (TX00) and glow / detail (TX03). Not the NIF slot numbers (GreyTexture.h kSlotGlow 2)
    inline constexpr std::size_t kTxstSlots = 8;
    inline constexpr std::size_t kTxstSlotDiffuse = 0;
    inline constexpr std::size_t kTxstSlotGlow = 3;
    inline constexpr std::size_t kDodtSize = 36;  // DODT (RE/D/DecalData.h DECAL_DATA_DATA 0x24)

    inline constexpr std::size_t kMaxLinkOffsets = 9;
    inline constexpr std::size_t kMaxLocalIdDigits = 6;  // a local id in "Plugin|localId" and a copy id: up to 6 hex digits
    inline constexpr int kHexBase = 16;

    /// Every per-type fact in one table (indexed by the enum, checked below): the record signature, its scalar subrecord
    /// and size as Skyrim.esm stores it, OBND, MODL, and the scalar subrecord's form-link offsets (each written 0)
    struct CopyTypeTraits
    {
        CopyType type;
        const char* tag;
        const char* scalarTag;
        std::size_t scalarSize;
        bool bounds;
        bool model;
        std::size_t linkCount;
        std::size_t links[kMaxLinkOffsets];
    };

    inline constexpr CopyTypeTraits kCopyTypeTraits[] = {
        { CopyType::kProjectile, "PROJ", "DATA", 92, true, true, 9, { 0x10, 0x14, 0x24, 0x28, 0x38, 0x3C, 0x40, 0x54, 0x58 } },
        { CopyType::kHazard, "HAZD", "DATA", 40, true, true, 4, { 0x18, 0x1C, 0x20, 0x24 } },
        { CopyType::kExplosion, "EXPL", "DATA", 52, true, true, 6, { 0x00, 0x04, 0x08, 0x0C, 0x10, 0x14 } },
        { CopyType::kImpact, "IPCT", "DATA", 24, false, true, 0, {} },
        { CopyType::kArtObject, "ARTO", "DNAM", 4, true, true, 0, {} },
        { CopyType::kTextureSet, "TXST", "DNAM", 2, true, false, 0, {} },
    };

    [[nodiscard]] constexpr bool CopyTraitsInOrder() noexcept
    {
        for (std::size_t i = 0; i < std::size(kCopyTypeTraits); ++i) {
            if (static_cast<std::size_t>(kCopyTypeTraits[i].type) != i || kCopyTypeTraits[i].linkCount > kMaxLinkOffsets) {
                return false;
            }
        }
        return static_cast<std::size_t>(CopyType::kTextureSet) + 1 == std::size(kCopyTypeTraits);
    }
    static_assert(CopyTraitsInOrder(), "kCopyTypeTraits: one row per CopyType, in the enum's order");

    [[nodiscard]] constexpr const CopyTypeTraits& TraitsOf(CopyType a_type) noexcept
    {
        return kCopyTypeTraits[static_cast<std::size_t>(a_type)];
    }

    /// Every copy type in the ESL's group order (Skyrim.esm's top group order: TXST, PROJ, HAZD, EXPL, IPCT, ARTO)
    inline constexpr CopyType kCopyTypes[] = { CopyType::kTextureSet, CopyType::kProjectile, CopyType::kHazard,
        CopyType::kExplosion, CopyType::kImpact, CopyType::kArtObject };
    static_assert(std::size(kCopyTypes) == std::size(kCopyTypeTraits), "kCopyTypes lists every copy type once");

    /// "PROJ" / "HAZD" / "EXPL" / "IPCT" / "ARTO" / "TXST"
    [[nodiscard]] const char* CopyTypeTag(CopyType a_type) noexcept;

    /// The type's tag back (nullopt for any other text)
    [[nodiscard]] std::optional<CopyType> CopyTypeFromTag(std::string_view a_tag) noexcept;

    /// The scalar subrecord size Skyrim.esm uses for the type (PROJ 92, HAZD 40, EXPL 52, IPCT 24 DATA; ARTO 4, TXST 2 DNAM)
    [[nodiscard]] std::size_t CopyDataSize(CopyType a_type) noexcept;

    /// The type's scalar subrecord: "DATA", or "DNAM" for ARTO and TXST (neither has DATA)
    [[nodiscard]] const char* CopyDataTag(CopyType a_type) noexcept;

    /// The type has OBND (every type but IPCT)
    [[nodiscard]] bool CopyHasBounds(CopyType a_type) noexcept;

    /// The scalar subrecord offsets of the type's form links (each a 4-byte FormID written as 0)
    [[nodiscard]] std::vector<std::size_t> CopyLinkOffsets(CopyType a_type);

    struct FormSource
    {
        std::string source;    // "Plugin.esp|00XXXX" (defining plugin, local id, as the request gives it)
        CopyType type = CopyType::kProjectile;
        std::string editorId;  // the source's EditorID
        std::string model;     // Data-relative with meshes\ (lower case), "" when the source has none
        std::string muzzleModel;  // PROJ: its muzzle flash model, the same form (v3), "" when none
        std::array<std::int16_t, 6> bounds{};
        std::map<std::string, double> data;  // scalar DATA values by key (CopyFieldKeys)
        // TXST: each slot's own bytes as the record stores them (relative to textures\; AB1: the request's "texturesRaw",
        // never through a code page), "" when the slot is empty; and the same as UTF-8 text for people ("textures")
        std::array<std::string, kTxstSlots> textures{};
        std::array<std::string, kTxstSlots> textureNames{};
        bool hasDecal = false;  // TXST: the source has DODT (the copy's DODT is a stub the DLL fills)
        std::string key;        // request v5: its model's key16 ("" when the DLL gave none; a TXST form has none, it is
                                // found by its slot, and a key it does carry must be its txst[] entry's); "" in older requests
        int slot = -1;          // request v5: the slot the DLL gave it (forms[].slot), -1 when none or an older request
    };

    /// The scalar keys a type's "data" may hold (unknown keys are refused, missing ones are 0)
    [[nodiscard]] std::vector<std::string> CopyFieldKeys(CopyType a_type);

    struct ExcludedForm
    {
        std::string source;
        std::string type;
        std::string reason;
    };

    struct FormsRequest
    {
        int version = kFormsRequestVersion;
        std::vector<FormSource> forms;
        std::optional<Request> addons;      // v3: the load order's ADDN records and the closure's impact models
        std::string fingerprint;            // v3: copied into the copies map (the DLL compares it at every start)
        std::string fingerprintSizes;       // v3: the same without the NIF byte hashes (the DLL's cheap check)
        std::string game;                   // v3: the runtime that wrote it
        std::vector<ExcludedForm> excluded; // v3: what the DLL left out and why (copied into the map)
        std::vector<std::string> failedNifs;  // v3: Data-relative NIFs the game could not read (no grey copy made)
        std::vector<std::string> warnings;  // v3: entries skipped while reading (duplicate ADDN index)
        bool hasTextures = false;           // the request lists "textures" (a DLL that dumps them; older requests do not)
        std::map<std::string, std::string> texturesNotDumped;  // "textures" entries with ok false: path -> error
        std::set<std::string> texturesDumped;  // "textures" entries with ok true (TextureDataPath): the dump's listed files
        // request v5: every "textures" entry's state (required there): ok, notdumped, missing, unread
        std::map<std::string, BlackFlame::CopyContract::Request::TextureState> textureStates;
        bool decalTextureSets = false;      // the DLL walked decal texture sets (TXST in "forms"); older requests do not
        std::set<std::string> palettesListed;  // our two fixed palettes the request names (DataPathRule kOurPalette*)
        std::set<std::string> palettesDumped;  // of those, the ones in the dump (ok true)
        std::set<std::string> leftOut;         // AA3: normalised sources of forms left out while reading (a TXST slot)
        bool dumpComplete = true;              // AC1: the request's "dumpComplete" (absent: true)
    };

    /// Reads and validates a forms request: version 2 (forms only), 3 and 4 (the DLL's: also addons, models, nifs,
    /// fingerprint, excluded) or 5 (the single launch's: no models; forms carry slot and key). Throws std::runtime_error
    /// naming what is wrong.
    /// a_keyRoute: read for the key mode's run (--staging), which uses no ADDN model and no data value: an ADDN with no
    /// mesh model and a null data value are warnings there, refusals on every other route (--out writes them into the ESL)
    [[nodiscard]] FormsRequest LoadFormsRequestFile(const std::filesystem::path& a_file, bool a_keyRoute = false);

    /// A request's or map's "excluded" list (absent: none)
    [[nodiscard]] std::vector<ExcludedForm> ReadExcludedForms(const std::filesystem::path& a_file, const nlohmann::json& a_root);

    /// The forms of LoadFormsRequestFile
    [[nodiscard]] std::vector<FormSource> LoadFormsRequest(const std::filesystem::path& a_file);

    /// "plugin.esp|00xxxx" with the plugin lower case and the id 6 hex digits upper case (the map key)
    [[nodiscard]] std::string NormalizeSource(const std::string& a_source);

    struct CopyEntry
    {
        std::string source;  // normalised
        CopyType type = CopyType::kProjectile;
        std::uint32_t localId = 0;  // in kCopiesPluginName, 0x800..0xFFF
        std::string editorId;       // the copy's EditorID
        std::string sourceEditorId;
        std::string sourceModel;  // "" when none
        std::string greyModel;    // "" when none, or when the grey copy could not be made (the record then names sourceModel)
        std::string sourceMuzzleModel;  // PROJ, "" when none
        std::string greyMuzzleModel;    // PROJ: the grey copy of its muzzle model, "" when none or not made
        std::string note;         // why a grey copy is missing (warn-and-continue), "" when none
        std::array<std::string, kTxstSlots> sourceTextures{};  // TXST: the source's slots (its record string's bytes)
        std::array<std::string, kTxstSlots> sourceTextureNames{};  // TXST: the same as UTF-8 text (the map, for people)
        std::array<std::string, kTxstSlots> greyTextures{};    // TXST: a slot's grey copy (record text), "" = the source's
        bool hasDecal = false;                                 // TXST: the record has the DODT stub
    };

    /// TXST: the text the copy record names in slot a_slot: its grey copy, else the source's
    [[nodiscard]] const std::string& CopyRecordTexture(const CopyEntry& a_entry, std::size_t a_slot);

    struct CopiesMap
    {
        std::vector<CopyEntry> entries;  // by local id
        std::map<std::string, std::uint32_t> retired;  // sources no longer requested: their ids stay taken
        std::map<std::string, CopyType> retiredTypes;  // their types (maps before TXST copies did not keep them: absent)
        std::string requestFingerprint;  // v2: the request's fingerprint (the DLL's freshness check)
        std::string requestFingerprintSizes;  // v2: its size-only form (the check when the game reads no NIF bytes)
        bool dumpComplete = true;             // AC1: the request's dump had every file (false: the DLL reads the map as stale)
        std::vector<ExcludedForm> excluded;  // v2: the request's excluded forms
        GreyStats shaders;                   // v2i: effect shaders of the grey NIFs by palette kind (written, not read back)
        float tint = kDefaultTint;
        float tintAdd = kDefaultTintAdd;
    };

    /// The model the copy's record names (without meshes\): the grey copy, else the source's own model, else ""
    [[nodiscard]] std::string CopyRecordModel(const CopyEntry& a_entry);

    /// Reads a copies map (version 1 or 2). Throws std::runtime_error naming what is wrong.
    [[nodiscard]] CopiesMap LoadCopiesMap(const std::filesystem::path& a_file);
    void WriteCopiesMap(const CopiesMap& a_map, const std::filesystem::path& a_file);

    /// Local ids for a_sources: a source already in a_previous keeps its id (entries or retired), a new one
    /// takes the lowest id above every id a_previous holds; previous entries not requested are retired. With
    /// a_reclaimRetired, once the ids above run out, retired ids are given to new sources lowest first (and leave
    /// the retired list). Throws, naming the active, retired and per-type counts, when the ids run out, or when a
    /// source is listed twice.
    [[nodiscard]] CopiesMap AssignCopyIds(const std::vector<FormSource>& a_sources, const CopiesMap& a_previous,
        bool a_reclaimRetired = false);

    /// The grey copy's path of a source model: kCopyRoot + the path after meshes\ (lower case)
    [[nodiscard]] std::string CopyModelFor(const std::string& a_meshDataPath);

    /// Writes the copies ESL: one record per entry (the matching source by index), groups TXST, PROJ, HAZD, EXPL,
    /// IPCT, ARTO in that order (kCopyTypes, Skyrim.esm's group order). a_sources[i] belongs to a_map.entries[i].
    void WriteCopiesEsl(const CopiesMap& a_map, const std::vector<FormSource>& a_sources,
        const std::filesystem::path& a_file);

    /// One top-level group of a_map's a_type entries, records as WriteCopiesEsl writes them ("" when there are none);
    /// a_count gains the records and the group, a_highest the largest local id (the slot pool ESL, GreySlotPool.h)
    [[nodiscard]] std::string CopiesEslGroup(CopyType a_type, const CopiesMap& a_map, const std::vector<FormSource>& a_sources,
        std::int32_t& a_count, std::uint32_t& a_highest);

    /// A masterless ESL: TES4 (light flag, HEDR 1.70 with a_count and a_nextObjectId, CNAM), then a_groups as given
    void WriteLightPlugin(const std::filesystem::path& a_file, const std::string& a_groups, std::int32_t a_count,
        std::uint32_t a_nextObjectId);

    struct EslForm
    {
        std::string type;  // record signature
        std::uint32_t formId = 0;
        std::string editorId;
        bool hasModel = false;
        std::string model;
        bool hasBounds = false;
        std::array<std::int16_t, 6> bounds{};
        bool hasData = false;
        std::string data;  // raw DATA bytes
        bool hasDnam = false;
        std::string dnam;  // raw DNAM bytes (ARTO art type, TXST flags)
        bool hasSoundLevel = false;
        std::uint32_t soundLevel = 0;  // PROJ VNAM
        std::array<std::string, kTxstSlots> textures{};  // TXST TX00..TX07 (text without the terminator)
        std::uint32_t textureMask = 0;                   // bit i: TX0i present
        bool hasDodt = false;
        std::string dodt;  // raw DODT bytes
        std::vector<std::string> otherSubrecords;  // anything else, or a subrecord seen twice (a writer bug)
    };

    struct EslHeader
    {
        std::uint32_t flags = 0;
        float version = 0.0f;
        std::int32_t records = 0;
        std::uint32_t nextObjectId = 0;
        int masters = 0;
    };

    /// Reads every record of every top-level group back. Throws std::runtime_error on a bad layout.
    [[nodiscard]] std::vector<EslForm> ReadCopiesEsl(const std::filesystem::path& a_file, EslHeader& a_header);

    /// Reads the ESL back and compares it with what was written; the differences (empty = it matches)
    [[nodiscard]] std::vector<std::string> VerifyCopiesEsl(const CopiesMap& a_map,
        const std::vector<FormSource>& a_sources, const std::filesystem::path& a_file);

    struct FormsTotals
    {
        std::string riskGate;     // the visual-risk check's T0 line (GreyRisk.h); "" when it did not run
        bool riskPassed = true;   // its gate: FAIL 0 and UNREAD 0
        int copies = 0;      // records written
        int nifsBuilt = 0;   // distinct grey copy NIFs
        int failed = 0;      // NIFs or records that failed (nothing then written for the ESL and map)
        int warnings = 0;    // grey copies not made (the copy keeps the source's model), values kept, entries dropped
        GreyStats shaders;   // v2i: effect shaders of the grey copy NIFs and grey MPS built, by palette kind
        TextureTotals textures;  // the texture copies (all zero with textures off)
    };

    struct FormsRunOptions
    {
        std::string fingerprint;             // into the copies map
        std::string fingerprintSizes;        // into the copies map
        std::vector<ExcludedForm> excluded;  // into the copies map
        std::vector<std::string> failedNifs; // models the game could not read: no grey copy is tried
        bool reclaimRetired = false;         // AssignCopyIds
        float tintAdd = kDefaultTintAdd;     // v2i: additive palette shaders' base colour scale (--tint-add)
        PaletteMode paletteMode = PaletteMode::kDerived;  // --palette-mode (GreyPalette.h)
        bool warnAndContinue = false;        // --from-game: a NIF that cannot be read or parsed, or an AddOnNode value
                                             // in no ADDN, is a warning (source model kept / value kept), not a failure
        bool textures = true;                // texture copies (GreyTexture.h) of the full-grey copy NIFs and grey MPS; --no-textures: off
        std::map<std::string, std::string> texturesNotDumped;  // --from-game: the request's textures not in the dump, and why
        bool decalTextureSets = true;        // the request walked decal texture sets (false: a game request from an older
                                             // DLL - refused when --out already holds TXST copies)
        bool ownTextureFolders = true;       // the risk check may read our palettes from the builder's own folders (false
                                             // when the request lists them: the game's own then, or its reason)
        std::set<std::string> leftOut;       // AA3: sources the request reader left out; one --out still copies is refused
        bool retireLeftOut = false;          // AB5: --retire-left-out: retire those copies on purpose (their records go)
        bool dumpComplete = true;            // AC1: into the copies map
    };

    /// The whole --forms run: grey ADDN plan from a_addons (whose models gain the copies' models for their
    /// AddOnNode values, without remapped copies of them), its MPS and map (and local ESL), the full-grey copy
    /// NIFs, the copies ESL (read back and verified) and the copies map. a_out must not overlap a_inputs.
    [[nodiscard]] FormsTotals BuildForms(const std::vector<FormSource>& a_sources, const Request& a_addons,
        const std::vector<std::filesystem::path>& a_inputs, const std::filesystem::path& a_out, float a_tint,
        const FormsRunOptions& a_options = {});

    /// --from-game without a folder: <Documents>\My Games\{Skyrim Special Edition, Skyrim Special Edition GOG}\SKSE\
    /// HeartOfMagic_BlackFlame, the one whose request.json is newer; nullopt with a_why naming what was looked at and
    /// the next step (GreyFromGame.cpp)
    [[nodiscard]] std::optional<std::filesystem::path> FindRequestFolder(std::string& a_why);

    /// The whole --from-game run: the request (version 3) and its NIF dump in a_folder (empty: FindRequestFolder) into
    /// a_out, warn-and-continue. Exit code 0 / 1 (failure) / 2 (bad arguments), each refusal with the next step.
    [[nodiscard]] int RunFromGame(const std::filesystem::path& a_folder, const std::filesystem::path& a_out, float a_tint,
        bool a_reclaim, float a_tintAdd = kDefaultTintAdd, bool a_textures = true, PaletteMode a_paletteMode = PaletteMode::kDerived,
        bool a_retireLeftOut = false);

    /// Writes an ESL-flagged, masterless plugin with no records (HEDR 1.70, next id 0x800): the empty
    /// HeartOfMagic_BlackFlame_Copies.esp / _Local.esp shipped with the mod (a built output replaces them)
    void WriteEmptyEsl(const std::filesystem::path& a_file);
}
