#pragma once

// =============================================================================
// blackflame-greybuild - grey duplicates of shared-particle fire meshes
// =============================================================================
// Black Flame (plugins/BlackFlame/README.md, "Grey duplicates"). Fire impacts and
// hazards carry AddOnNodes (BSValueNode, value = ADDN index) whose particles the
// engine emits into ONE shared BSMasterParticleSystem per index, so a single
// instance cannot be recoloured at runtime. This tool writes grey copies of those
// master particle system NIFs (shipped under new ADDN indices) and copies of the
// impact / hazard NIFs whose AddOnNodes point at the grey indices.

#include <chrono>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nifly
{
    class NifFile;
    class BSEffectShaderProperty;
}

namespace GreyBuild
{
    struct TextureJob;  // GreyTexture.h

    /// Perf audit 2: "time  <stage>: N ms" when it goes out of scope (stdout, with the run's other lines)
    class StageTimer
    {
    public:
        explicit StageTimer(const char* a_stage) noexcept :
            m_stage(a_stage), m_start(std::chrono::steady_clock::now())
        {}
        ~StageTimer();
        StageTimer(const StageTimer&) = delete;
        StageTimer& operator=(const StageTimer&) = delete;

    private:
        const char* m_stage;
        std::chrono::steady_clock::time_point m_start;
    };

    // =========================================================================
    // Constants
    // =========================================================================

    // The palette the DLL loads (plugins/BlackFlame/src/Palette.cpp), as a NIF stores it
    inline constexpr const char* kPalettePath = "textures\\HeartOfMagic\\BlackFlame\\GradBlackFlame.dds";
    // v2i: the palette of additive palette shaders (bright edges: an additive shape cannot draw dark). The path is fixed;
    // which colours it holds is chosen by the DDS placed there (Godskin by default; data/optional/palettes has others)
    inline constexpr const char* kPaletteAddPath = "textures\\HeartOfMagic\\BlackFlame\\GradBlackFlameAdd.dds";

    // SLSF1 bit positions (CommonLibSSE-NG RE/B/BSShaderProperty.h EShaderPropertyFlag8,
    // kGrayscaleToPaletteColor = 4, kGrayscaleToPaletteAlpha = 5)
    inline constexpr std::uint32_t kSlsf1GreyscaleToPaletteColor = 1u << 4;
    inline constexpr std::uint32_t kSlsf1GreyscaleToPaletteAlpha = 1u << 5;

    // Rec. 709 luma weights, the DLL's own (plugins/BlackFlame/src/Tint.h)
    inline constexpr float kLumaRed = 0.2126f;
    inline constexpr float kLumaGreen = 0.7152f;
    inline constexpr float kLumaBlue = 0.0722f;

    inline constexpr float kMinTint = 0.0f;
    inline constexpr float kMaxTint = 2.0f;

    // Every grey output lives here (Data-relative, lower case)
    inline constexpr const char* kGreyRoot = "meshes\\heartofmagic\\blackflame\\";
    inline constexpr const char* kMeshesRoot = "meshes\\";

    /// A path as UTF-8 text for messages (path::string() throws on a name outside the ANSI code page)
    [[nodiscard]] inline std::string PathText(const std::filesystem::path& a_path)
    {
        const auto text = a_path.u8string();
        return std::string(text.begin(), text.end());
    }

    [[nodiscard]] inline bool StartsWith(std::string_view a_text, std::string_view a_prefix) noexcept
    {
        return a_text.substr(0, a_prefix.size()) == a_prefix;
    }

    [[nodiscard]] inline bool EndsWith(std::string_view a_text, std::string_view a_suffix) noexcept
    {
        return a_text.size() >= a_suffix.size() && a_text.substr(a_text.size() - a_suffix.size()) == a_suffix;
    }

    // =========================================================================
    // Map file (plugins/BlackFlame/data/SKSE/Plugins/HeartOfMagic_BlackFlame_greymap.json)
    // =========================================================================

    inline constexpr float kDefaultTint = 0.6f;  // the copies' base colour scale (was the DLL tintScale default, a key retired in C6)
    inline constexpr float kDefaultTintAdd = 0.9f;  // v2i: base colour scale of additive palette shaders (--tint-add)

    // ADDN DNAM flags as Skyrim.esm stores them (raw DNAM: MPSFireSmall01 01 00, MPSFireBoltImpact01 03 00)
    inline constexpr std::uint16_t kDnamMasterParticleSystem = 1;
    inline constexpr std::uint16_t kDnamAlwaysLoaded = 3;

    struct AddonEntry
    {
        int index = 0;      // the original ADDN index
        int grey = 0;       // the grey ADDN index
        std::string model;  // the original MPS NIF, Data-relative
        std::string greyModel;
        // For the DLL and for people; carried through when a map is written
        std::string editorId;      // the original ADDN's EditorID
        std::string source;        // the original ADDN, "Plugin|localId"
        std::uint16_t masterParticleCap = 0;
        std::uint16_t dnamFlags = kDnamMasterParticleSystem;
        std::string greyEditorId;  // the grey ADDN record's EditorID
        std::string greyForm;      // the grey ADDN record, "Plugin|localId"
    };

    struct GreyMap
    {
        float tint = kDefaultTint;
        float tintAdd = kDefaultTintAdd;  // v2i: additive palette shaders' base colour (optional "tintAdd" in the map)
        std::map<int, AddonEntry> addons;           // by original index
        std::map<std::string, std::string> models;  // impact / hazard NIF -> grey copy
    };

    /// Reads and validates the map. Throws std::runtime_error naming what is wrong.
    [[nodiscard]] GreyMap LoadMap(const std::filesystem::path& a_file);

    /// Writes a map in the same format LoadMap reads. Throws std::runtime_error on a write failure.
    void WriteMap(const GreyMap& a_map, const std::filesystem::path& a_file, const std::vector<std::string>& a_comment);

    /// ASCII lower case, forward slashes to backslashes, a double-byte character kept (DataPathRule::LowerSlashes).
    [[nodiscard]] std::string NormalizeDataPath(std::string a_path);

    /// DataPathRule::Normalize under "meshes\" (record Model fields leave it out); a path it refuses: NormalizeDataPath
    /// with "meshes\" put in front when missing.
    [[nodiscard]] std::string MeshDataPath(const std::string& a_path);

    /// The grey copy's path for a Data-relative mesh path: meshes\heartofmagic\blackflame\<rest>.
    [[nodiscard]] std::string GreyPathFor(const std::string& a_meshDataPath);

    // =========================================================================
    // Request (GreyRequest.cpp): what one setup needs, for maps made per setup
    // =========================================================================
    // Written by hand or by a tool that sees the load order (planned: the DLL at
    // kDataLoaded). "addons" lists the load order's ADDN records (index, model as the
    // record stores it, DNAM); "models" the fire impact / hazard NIFs to make grey copies of.

    struct RequestAddon
    {
        int index = 0;
        std::string model;  // Data-relative (meshes\ added when missing)
        std::string editorId;
        std::string source;
        std::uint16_t masterParticleCap = 0;
        std::uint16_t dnamFlags = kDnamMasterParticleSystem;
    };

    struct Request
    {
        std::map<int, RequestAddon> addons;  // by index (every ADDN of the load order)
        std::vector<std::string> models;     // Data-relative impact / hazard NIFs
    };

    /// Reads and validates a request. Throws std::runtime_error naming what is wrong.
    [[nodiscard]] Request LoadRequest(const std::filesystem::path& a_file);

    // The generated plugin for grey ADDN records a setup needs beyond the shipped ones
    inline constexpr const char* kLocalPluginName = "HeartOfMagic_BlackFlame_Local.esp";
    inline constexpr int kGreyIndexBase = 9100;       // preferred grey index = base + original
    inline constexpr int kMaxIndex = INT_MAX - kGreyIndexBase;  // any ADDN index read or allocated (base + it fits an int)
    inline constexpr std::uint32_t kFirstLightFormId = 0x800;
    inline constexpr std::uint32_t kLastLightFormId = 0xFFF;

    struct Plan
    {
        GreyMap map;
        std::vector<AddonEntry> newRecords;  // every record kLocalPluginName must hold, by local id (kept and new)
        std::vector<std::string> problems;   // values with no ADDN in the request, and the like
        std::vector<std::string> skipped;    // request models in no --in folder
        std::vector<std::string> warnings;   // a_warnAndContinue: what would have been a problem, kept going
    };

    /// Reads every listed model found in a_inputs (first folder that has it wins), collects
    /// its AddOnNode values and allocates grey indices: an existing ADDN that already names the
    /// grey copy is reused (the shipped records, or kLocalPluginName's from an earlier run, which
    /// stay in newRecords at their local id), else base + original when free, otherwise the next
    /// free index above every used one; new local records take ids after every local id the request has.
    /// a_valueOnlyModels (--forms: the copies' models) count for the AddOnNode values a grey index is
    /// needed for, but get no remapped copy in map.models (their copies are the full-grey forms copies).
    /// a_warnAndContinue (--from-game): an AddOnNode value in no ADDN, or a model nifly cannot read, is a warning
    /// (the model gets no remapped copy; its other values still count), not a problem.
    [[nodiscard]] Plan PlanFromRequest(const Request& a_request, const std::vector<std::filesystem::path>& a_inputs,
        float a_tint, const std::vector<std::string>& a_valueOnlyModels = {}, bool a_warnAndContinue = false);

    // =========================================================================
    // Generated ESL (GreyEsl.cpp)
    // =========================================================================

    inline constexpr std::uint32_t kEslFlag = 0x200;  // TES4 record flag: light plugin
    // HEDR version 1.70: every record is at 0x800 or above, so the 1.71 header (records from
    // 0x000, AE 1.6.1130+) is not needed, and runtimes or xEdit builds older than it may refuse 1.71
    inline constexpr float kHedrVersion = 1.70f;

    /// The local FormID of a "Plugin|localId" form when Plugin is kLocalPluginName (any case),
    /// nothing for another plugin. Throws std::runtime_error when it names kLocalPluginName with
    /// an id that is not hex in 0x800..0xFFF.
    [[nodiscard]] std::optional<std::uint32_t> LocalFormId(const std::string& a_form);

    /// Writes an ESL-flagged plugin with no masters holding one ADDN record per entry
    /// (EDID, OBND, MODL, DATA = grey index, DNAM copied), in the order given; each record's
    /// FormID is the local id its greyForm names (kLocalPluginName|0008xx).
    void WriteAddonEsl(const std::vector<AddonEntry>& a_records, const std::filesystem::path& a_file);

    /// The FormIDs WriteAddonEsl gives a_records, in the same order. Throws std::runtime_error
    /// when a greyForm does not name kLocalPluginName or two records share an id.
    [[nodiscard]] std::vector<std::uint32_t> EslFormIds(const std::vector<AddonEntry>& a_records);

    struct EslAddon
    {
        std::uint32_t formId = 0;
        std::string editorId;
        std::string model;
        int index = 0;
        std::uint16_t masterParticleCap = 0;
        std::uint16_t dnamFlags = 0;
    };

    /// Reads the ADDN records back (only what WriteAddonEsl writes). Throws std::runtime_error.
    [[nodiscard]] std::vector<EslAddon> ReadAddonEsl(const std::filesystem::path& a_file, std::uint32_t& a_headerFlags);

    /// Reads a_file back and compares it with what WriteAddonEsl was given (light flag, and per
    /// record FormID, EDID, MODL, DATA, DNAM). Returns the differences; empty = it matches.
    [[nodiscard]] std::vector<std::string> VerifyAddonEsl(const std::vector<AddonEntry>& a_records,
        const std::filesystem::path& a_file);

    // =========================================================================
    // NIF operations (GreyNif.cpp)
    // =========================================================================

    struct GreyStats
    {
        int effectShaders = 0;     // BSEffectShaderProperty blocks
        int paletted = 0;          // of those, normal-blend palette shaders (flag on, a source texture): GradBlackFlame.dds
        int palettedAdd = 0;       // additive palette shaders (destination blend ONE): GradBlackFlameAdd.dds, tintAdd (v2i)
        int paletteNoAlpha = 0;    // of paletted, without an NiAlphaProperty (counted as normal blend, logged)
        int paletteLeft = 0;       // the others: flags and textures kept, only the base colour greyed (v2i)
        int lightingShaders = 0;   // BSLightingShaderProperty blocks (left alone)
        int colorModifiers = 0;    // BSPSysSimpleColorModifier blocks desaturated
        int colorControllers = 0;  // BSEffectShaderPropertyColorController blocks
        int controllerKeys = 0;    // NiPosData keys of those controllers desaturated
        int controllersUnreached = 0;  // controllers without a direct NiPoint3Interpolator: keys left
        int psysColorData = 0;     // NiColorData blocks of NiPSysColorModifier desaturated
    };

    struct RemapStats
    {
        int valueNodes = 0;                     // BSValueNode blocks
        int remapped = 0;                       // values changed to a grey index
        int alreadyGrey = 0;                    // values that already are grey indices
        std::vector<std::string> remapLines;    // "name: 9 -> 9109"
        std::vector<std::string> unmapped;      // "name: value" (a failure)
    };

    [[nodiscard]] float Luminance(float a_red, float a_green, float a_blue) noexcept;

    /// v2i: a_written's header string table starts with a_source's, string for string in the same order (SaveToBytes keeps
    /// it); the first difference, or empty
    [[nodiscard]] std::vector<std::string> CheckHeaderStrings(const nifly::NifFile& a_source, const nifly::NifFile& a_written);

    /// A palette shader: the source already maps its texture through a palette (SLSF1 greyscale-to-palette colour on
    /// and a non-empty source texture). Reads shader data only; the builder never changes either, so it holds on the copy.
    [[nodiscard]] bool UsesPalette(const nifly::BSEffectShaderProperty& a_prop);

    /// Every BSEffectShaderProperty: base colour RGB luminance x tint (alpha kept); a palette shader (UsesPalette) also gets
    /// our palette as its greyscale texture - GradBlackFlameAdd.dds and a_tintAdd when its shape blends additively
    /// (NiAlphaProperty blending on, destination ONE), else GradBlackFlame.dds - every other one keeps its flags and
    /// textures (v2i); colour modifier keys and effect colour controller keys RGB to luminance (alpha kept). Lighting
    /// shaders are left alone. a_log gets one line per palette shader without an alpha property.
    GreyStats GreyEffects(nifly::NifFile& a_nif, float a_tint, float a_tintAdd = kDefaultTintAdd,
        std::vector<std::string>* a_log = nullptr);

    /// Every BSValueNode value through the map (names and everything else kept).
    RemapStats RemapAddonValues(nifly::NifFile& a_nif, const GreyMap& a_map);

    /// Loads a NIF; throws std::runtime_error naming the file on failure.
    void LoadNif(nifly::NifFile& a_nif, const std::filesystem::path& a_file);

    /// Saves without nifly's optimize / block sort (a duplicate keeps the source's block order).
    void SaveNif(nifly::NifFile& a_nif, const std::filesystem::path& a_file);

    /// The bytes a save would write.
    [[nodiscard]] std::string SaveToBytes(nifly::NifFile& a_nif);

    /// Count of unknown (raw-kept) blocks.
    [[nodiscard]] int CountUnknownBlocks(const nifly::NifFile& a_nif);

    /// Checks a written grey MPS / remapped copy against the map; returns the problems.
    /// a_allowUnmapped: an AddOnNode value in no map entry is not a problem (kept on purpose, warn-and-continue).
    [[nodiscard]] std::vector<std::string> VerifyGrey(nifly::NifFile& a_nif, bool a_expectGreyEffects,
        const GreyMap& a_map, bool a_allowUnmapped = false);

    // =========================================================================
    // Round trip (GreyRoundTrip.cpp)
    // =========================================================================

    enum class RoundTripKind
    {
        Identical,    // an unchanged re-save is byte-identical
        StringOrder,  // only the header string table order differs
        Different
    };

    struct RoundTrip
    {
        RoundTripKind kind = RoundTripKind::Different;
        std::string detail;
    };

    [[nodiscard]] const char* RoundTripName(RoundTripKind a_kind) noexcept;

    /// Loads a NIF, re-saves it unchanged and compares. Throws std::runtime_error on a load failure.
    [[nodiscard]] RoundTrip CheckRoundTrip(const std::filesystem::path& a_file);

    // =========================================================================
    // Build run (GreyRun.cpp)
    // =========================================================================

    struct Totals
    {
        int built = 0;
        int skipped = 0;
        int failed = 0;
        GreyStats shaders;  // v2i: effect shaders of the NIFs built, summed (palette kinds)
    };

    /// Adds a_stats' shader counts into a_sum
    void AddShaderCounts(GreyStats& a_sum, const GreyStats& a_stats) noexcept;

    /// The first folder in a_inputs that has a_dataPath, or nothing.
    [[nodiscard]] std::optional<std::filesystem::path> FindSource(const std::vector<std::filesystem::path>& a_inputs,
        const std::string& a_dataPath);

    /// a_child is a_parent or inside it (case-insensitive, as Windows paths are).
    [[nodiscard]] bool IsInside(const std::filesystem::path& a_child, const std::filesystem::path& a_parent);

    /// Writes every grey MPS and remapped copy the map lists into a_out, reading each back
    /// and verifying it. An MPS in no input folder is skipped, or a failure when a_needAll.
    Totals BuildFromMap(const GreyMap& a_map, const std::vector<std::filesystem::path>& a_inputs,
        const std::filesystem::path& a_out, bool a_needAll);

    /// One NIF: a_rel from the first input that has it, effect shaders greyed (a_greyEffects) and AddOnNodes
    /// remapped through a_map, written to a_out / a_greyRel, read back and verified. False on a failure (or a
    /// missing source when a_needed); a_totals counts it.
    bool BuildNif(const GreyMap& a_map, const std::vector<std::filesystem::path>& a_inputs,
        const std::filesystem::path& a_out, const std::string& a_rel, const std::string& a_greyRel, bool a_greyEffects,
        bool a_needed, Totals& a_totals);

    struct NifOptions
    {
        bool greyEffects = true;
        bool needed = true;         // a missing source counts as failed (else skipped)
        bool keepUnmapped = false;  // an AddOnNode value in no map entry stays as it is (warned), the NIF is written
        TextureJob* textures = nullptr;  // with greyEffects: texture slots retargeted at grey copies (GreyTexture.h); none: left
    };

    struct NifResult
    {
        bool ok = false;
        bool missing = false;       // in no --in folder
        std::string reason;         // why not ok
        std::vector<std::string> keptValues;  // keepUnmapped: "name: value" left as they were
        GreyStats stats;            // v2i: what GreyEffects did (zero when effects were not greyed)
    };

    /// BuildNif with options and the reason of a failure (BuildNif is this with keepUnmapped off)
    NifResult BuildNifWith(const GreyMap& a_map, const std::vector<std::filesystem::path>& a_inputs,
        const std::filesystem::path& a_out, const std::string& a_rel, const std::string& a_greyRel, const NifOptions& a_options,
        Totals& a_totals);

    /// Writes kLocalPluginName with a_records (read back and compared), or with none removes one an earlier
    /// run left in the folder (--request; a --forms run writes it always, empty or not). False (reason printed) on a failure.
    [[nodiscard]] bool SyncLocalEsl(const std::vector<AddonEntry>& a_records, const std::filesystem::path& a_file);

    // =========================================================================
    // Dump and self-test
    // =========================================================================

    /// Prints what the builder reads and writes (values, effect shaders, colour keys). Returns 0/1.
    int DumpNif(const std::filesystem::path& a_file);

    /// Re-saves a NIF unchanged and reports whether the bytes match (and which block sizes moved). Returns 0/1.
    int RoundTripReport(const std::filesystem::path& a_file);

    /// Builds a tiny NIF in a temp folder, runs both transforms, reloads and checks. Returns 0/1.
    int RunSelfTest();
}
