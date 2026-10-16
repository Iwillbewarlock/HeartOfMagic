#pragma once

// =============================================================================
// GreyForms - Black Flame v2 copies (README "Black Flame v2", --forms)
// =============================================================================
// A forms request lists source forms (PROJ, EXPL, IPCT, HAZD, ARTO) of one load order with
// their model path, object bounds and scalar DATA values (ARTO: its DNAM art type). The builder writes:
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
#include "GreyTexture.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace GreyBuild
{
    inline constexpr const char* kCopiesPluginName = "HeartOfMagic_BlackFlame_Copies.esp";
    inline constexpr const char* kCopiesMapName = "HeartOfMagic_BlackFlame_copies.json";
    inline constexpr const char* kCopyRoot = "meshes\\heartofmagic\\blackflame\\copies\\";
    inline constexpr const char* kCopyEditorIdPrefix = "HoMBF_C_";  // + type + "_" + the copy's local id (3 hex)
    inline constexpr int kFormsRequestVersion = 2;  // hand-made requests (forms only)
    inline constexpr int kGameRequestVersion = 3;   // the DLL's request (forms, addons, models, nifs, fingerprint)
    inline constexpr int kCopiesMapVersion = 2;     // version 1 maps are still read

    enum class CopyType
    {
        kProjectile,
        kHazard,
        kExplosion,
        kImpact,
        kArtObject
    };

    /// Every copy type in the ESL's group order (Skyrim.esm's top group order: PROJ, HAZD, EXPL, IPCT, ARTO)
    inline constexpr CopyType kCopyTypes[] = { CopyType::kProjectile, CopyType::kHazard, CopyType::kExplosion,
        CopyType::kImpact, CopyType::kArtObject };

    /// "PROJ" / "HAZD" / "EXPL" / "IPCT" / "ARTO"
    [[nodiscard]] const char* CopyTypeTag(CopyType a_type) noexcept;

    /// The DATA subrecord size Skyrim.esm uses for the type (PROJ 92, HAZD 40, EXPL 52, IPCT 24; ARTO: its DNAM, 4)
    [[nodiscard]] std::size_t CopyDataSize(CopyType a_type) noexcept;

    /// The type's scalar subrecord: "DATA", or "DNAM" for ARTO (an ARTO record has no DATA)
    [[nodiscard]] const char* CopyDataTag(CopyType a_type) noexcept;

    /// The type has OBND (PROJ, HAZD, EXPL, ARTO; not IPCT)
    [[nodiscard]] bool CopyHasBounds(CopyType a_type) noexcept;

    /// The DATA offsets of the type's form links (each a 4-byte FormID written as 0)
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
    };

    /// Reads and validates a forms request: version 2 (forms only) or 3 (the DLL's: also addons, models, nifs,
    /// fingerprint, excluded). Throws std::runtime_error naming what is wrong.
    [[nodiscard]] FormsRequest LoadFormsRequestFile(const std::filesystem::path& a_file);

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
    };

    struct CopiesMap
    {
        std::vector<CopyEntry> entries;  // by local id
        std::map<std::string, std::uint32_t> retired;  // sources no longer requested: their ids stay taken
        std::string requestFingerprint;  // v2: the request's fingerprint (the DLL's freshness check)
        std::string requestFingerprintSizes;  // v2: its size-only form (the check when the game reads no NIF bytes)
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

    /// Writes the copies ESL: one record per entry (the matching source by index), groups PROJ, HAZD, EXPL,
    /// IPCT, ARTO in that order (Skyrim.esm's group order). a_sources[i] belongs to a_map.entries[i].
    void WriteCopiesEsl(const CopiesMap& a_map, const std::vector<FormSource>& a_sources,
        const std::filesystem::path& a_file);

    struct EslForm
    {
        std::string type;  // record signature
        std::uint32_t formId = 0;
        std::string editorId;
        std::string model;
        bool hasBounds = false;
        std::array<std::int16_t, 6> bounds{};
        std::string data;  // raw DATA bytes
        bool hasDnam = false;
        std::string dnam;  // raw DNAM bytes (ARTO art type)
        bool hasSoundLevel = false;
        std::uint32_t soundLevel = 0;  // PROJ VNAM
        std::vector<std::string> otherSubrecords;  // anything else (a writer bug)
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
        bool warnAndContinue = false;        // --from-game: a NIF that cannot be read or parsed, or an AddOnNode value
                                             // in no ADDN, is a warning (source model kept / value kept), not a failure
        bool textures = true;                // texture copies (GreyTexture.h) of the full-grey copy NIFs and grey MPS; --no-textures: off
        std::map<std::string, std::string> texturesNotDumped;  // --from-game: the request's textures not in the dump, and why
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
        bool a_reclaim, float a_tintAdd = kDefaultTintAdd, bool a_textures = true);

    /// Writes an ESL-flagged, masterless plugin with no records (HEDR 1.70, next id 0x800): the empty
    /// HeartOfMagic_BlackFlame_Copies.esp / _Local.esp shipped with the mod (a built output replaces them)
    void WriteEmptyEsl(const std::filesystem::path& a_file);
}
