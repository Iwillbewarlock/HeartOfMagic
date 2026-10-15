#pragma once

// =============================================================================
// GreyForms - Black Flame v2 copies (README "Black Flame v2", --forms)
// =============================================================================
// A forms request lists source forms (PROJ, EXPL, IPCT, HAZD) of one load order with
// their model path, object bounds and scalar DATA values. The builder writes:
//   - a masterless ESL (HeartOfMagic_BlackFlame_Copies.esp) of placeholder records,
//     one per source: EDID, OBND (types that have it), MODL = the fully grey copy of
//     the source NIF, DATA in Skyrim.esm's size for the type with the scalars copied
//     and every form link zero (VNAM sound level on PROJ). Form version 44, HEDR 1.70,
//     FormIDs 0x800 up, stable across re-runs through the copies map.
//   - the grey NIFs under meshes\heartofmagic\blackflame\copies\ (every effect shader
//     on the grey palette and AddOnNodes on grey ADDN indices: the grey map built in
//     the same run from --request).
//   - SKSE\Plugins\HeartOfMagic_BlackFlame_copies.json: source -> copy, the stable-id
//     file (retired ids are kept so they are never given to another source).
// The DLL fills each copy from the live winning source at kDataLoaded (FormCopies.h).

#include "GreyBuild.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace GreyBuild
{
    inline constexpr const char* kCopiesPluginName = "HeartOfMagic_BlackFlame_Copies.esp";
    inline constexpr const char* kCopiesMapName = "HeartOfMagic_BlackFlame_copies.json";
    inline constexpr const char* kCopyRoot = "meshes\\heartofmagic\\blackflame\\copies\\";
    inline constexpr const char* kCopyEditorIdPrefix = "HoMBF_Copy_";
    inline constexpr int kFormsRequestVersion = 2;
    inline constexpr int kCopiesMapVersion = 1;

    enum class CopyType
    {
        kProjectile,
        kHazard,
        kExplosion,
        kImpact
    };

    /// "PROJ" / "HAZD" / "EXPL" / "IPCT"
    [[nodiscard]] const char* CopyTypeTag(CopyType a_type) noexcept;

    /// The DATA subrecord size Skyrim.esm uses for the type (PROJ 92, HAZD 40, EXPL 52, IPCT 24)
    [[nodiscard]] std::size_t CopyDataSize(CopyType a_type) noexcept;

    /// The type has OBND (PROJ, HAZD, EXPL; not IPCT)
    [[nodiscard]] bool CopyHasBounds(CopyType a_type) noexcept;

    /// The DATA offsets of the type's form links (each a 4-byte FormID written as 0)
    [[nodiscard]] std::vector<std::size_t> CopyLinkOffsets(CopyType a_type);

    struct FormSource
    {
        std::string source;    // "Plugin.esp|00XXXX" (defining plugin, local id, as the request gives it)
        CopyType type = CopyType::kProjectile;
        std::string editorId;  // the source's EditorID
        std::string model;     // Data-relative with meshes\ (lower case), "" when the source has none
        std::array<std::int16_t, 6> bounds{};
        std::map<std::string, double> data;  // scalar DATA values by key (CopyFieldKeys)
    };

    /// The scalar keys a type's "data" may hold (unknown keys are refused, missing ones are 0)
    [[nodiscard]] std::vector<std::string> CopyFieldKeys(CopyType a_type);

    /// Reads and validates a forms request (version 2). Throws std::runtime_error naming what is wrong.
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
        std::string greyModel;    // "" when none
    };

    struct CopiesMap
    {
        std::vector<CopyEntry> entries;  // by local id
        std::map<std::string, std::uint32_t> retired;  // sources no longer requested: their ids stay taken
    };

    /// Reads a copies map. Throws std::runtime_error naming what is wrong.
    [[nodiscard]] CopiesMap LoadCopiesMap(const std::filesystem::path& a_file);
    void WriteCopiesMap(const CopiesMap& a_map, const std::filesystem::path& a_file);

    /// Local ids for a_sources: a source already in a_previous keeps its id (entries or retired), a new one
    /// takes the lowest id above every id a_previous holds; previous entries not requested are retired.
    /// Throws when more than 0x800 ids would be needed or a source is listed twice.
    [[nodiscard]] CopiesMap AssignCopyIds(const std::vector<FormSource>& a_sources, const CopiesMap& a_previous);

    /// The grey copy's path of a source model: kCopyRoot + the path after meshes\ (lower case)
    [[nodiscard]] std::string CopyModelFor(const std::string& a_meshDataPath);

    /// Writes the copies ESL: one record per entry (the matching source by index), groups PROJ, HAZD, EXPL,
    /// IPCT in that order (Skyrim.esm's group order). a_sources[i] belongs to a_map.entries[i].
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
        int copies = 0;      // records written
        int nifsBuilt = 0;   // distinct grey copy NIFs
        int failed = 0;      // NIFs or records that failed (nothing then written for the ESL and map)
    };

    /// The whole --forms run: grey ADDN plan from a_addons (whose models gain the copies' models for their
    /// AddOnNode values, without remapped copies of them), its MPS and map (and local ESL), the full-grey copy
    /// NIFs, the copies ESL (read back and verified) and the copies map. a_out must not overlap a_inputs.
    [[nodiscard]] FormsTotals BuildForms(const std::vector<FormSource>& a_sources, const Request& a_addons,
        const std::vector<std::filesystem::path>& a_inputs, const std::filesystem::path& a_out, float a_tint);
}
