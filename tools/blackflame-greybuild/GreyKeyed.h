#pragma once

// =============================================================================
// GreyKeyed - key mode: request.json v5's objects built at the paths the DLL's keys give, into the staging folder
// =============================================================================
// blackflame-greybuild --from-game [<request folder>] --staging <dir> [--job-memory <MiB>] [--commit-reserve <MiB>]
//                      [--parent-pid <pid>]   (README "Single launch: the builder (P1)")
// The DLL makes every object's key (CopyContractKeys.h) and writes it into request v5; the builder never invents one but
// the keys of its own child objects (texture and palette copies): the contract's TextureKeyString / PaletteKeyString over
// the prefix it rebuilt and verified (VerifyKeyPrefix: options, its compiled output identity, the dumped palettes' hashes
// must give options.keyPrefix exactly, else the run is refused) and the source file's bytes. Every object goes to
// DataPathRule::CopyPath(<root>, key16, <source>) in staging, written as <path>.tmp and renamed, then one progress line
// (GreyProgress.h). Objects (each key once):
//   nif   forms[].key of a form with a model (its model), and muzzles[] {path, key}
//   mps   addnMps[] {slot, path, key[, index]}: the grey copy of an ADDN slot's master particle system
//   txst  txst[] {slot, key, dodt}: the TXST form whose forms[].key is that key; its TX00 / TX03 copies are the deps
// AddOnNode values go to the slot indices: an addnMps entry binds its slot (index 40000 + place) to the original ADDN
// index it names ("index", required since contract 4). A value bound to no slot
// stays as it is (fallback). A NIF's line lists its texture and palette copies (path, fnv64) and {"addn": n} for every
// slot index it now holds.
// Outcomes (ClassifyObject): a transient failure (I/O, out of memory, a source NIF that will not parse, the request's
// NIF bytes not the dump's) makes the object failed; else a texture over the job memory cap makes it too_large, a
// texture the rule picks that is outside the dump caps not_dumped (both terminal); else a deterministic fallback (a slot
// left: unsupported format, missing texture, our palette or a source palette not readable, an unbound AddOnNode value) is
// fallback; else ok. An object whose key's line is reusable (GreyProgress.h) is skipped. Scheduler: objects that name no
// texture the rule recolours first (MPS before the rest), then by the estimated texture cost.

#include "GreyBudget.h"
#include "GreyForms.h"
#include "GreyGpuEncoder.h"
#include "GreyProgress.h"
#include "GreyTexStats.h"
#include "GreyTexture.h"

#include "../../plugins/BlackFlame/src/CopyContractEfsh.h"
#include "../../plugins/BlackFlame/src/CopyContractEfshBase.h"
#include "../../plugins/BlackFlame/src/CopyContractKeys.h"
#include "../../plugins/BlackFlame/src/CopyContractProgress.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace GreyBuild
{
    inline constexpr std::size_t kNoForm = static_cast<std::size_t>(-1);

    struct KeyedOptions
    {
        std::filesystem::path staging;
        std::uint64_t jobMemory = kDefaultJobMemoryMiB * kBytesPerMiB;
        std::uint64_t commitReserve = kDefaultCommitReserveMiB * kBytesPerMiB;
        std::uint32_t parentPid = 0;  // 0: no parent to watch (and no throttle event: BuilderSignal.h)
        // BC7 targets on the GPU (GreyGpuEncoder.h): main sets kAuto (BLACKFLAME_GREYBUILD_GPU=off: kOff); off by default,
        // so a self-test run is on the CPU encoder unless it asks
        GpuUse gpu = GpuUse::kOff;
        MemoryProbe memoryProbe = MeasureMemory;  // the self-test's seam (a cap below what any process holds)
        // The self-test's seam, never set from the command line: the output identity the run keys with in place of this
        // build's own (the golden test keys its fixture with the release identity, so a dev build compares the same bytes)
        std::string identityOverride;
        // The self-test's seam, never set from the command line: the base copies' total cap (GreyKeyedBases.cpp)
        std::uint64_t baseCopyCap = BlackFlame::CopyContract::kMaxBaseCopyBytes;
    };

    /// This build's output identity ("<n>" or "<n>+dev<digest>"), compiled in from GreyIdentity.cmake's GreyIdentityText.h:
    /// the same text the exe's RCDATA resource carries for the DLL (CopyContractIdentity.h)
    [[nodiscard]] const char* OutputIdentity() noexcept;

    struct KeyedSpec
    {
        BlackFlame::CopyContract::ProgressKind kind = BlackFlame::CopyContract::ProgressKind::kNif;
        std::uint64_t key = 0;
        std::string source;          // the source NIF, Data-relative under meshes\ ("" for txst)
        std::size_t form = kNoForm;  // txst: the TXST form in the request's forms (kNoForm: none has the key)
        std::uint64_t cost = 0;      // the scheduler's texture estimate
        bool textured = false;       // names a texture the rule recolours that the dump has
        std::optional<double> tintAddHands;  // casting-art object override, absent for every other role
        std::optional<double> handMarbleOpacity;  // absent for every non-casting role
        double handMarbleScale = BlackFlame::CopyContract::kDefaultHandMarbleScale;
        std::string requestFailure;  // per-object validation failure; GuardObject reports it without building
    };

    struct KeyedRequest
    {
        FormsRequest base;
        double tint = kDefaultTint;
        double tintAdd = kDefaultTintAdd;
        PaletteMode paletteMode = PaletteMode::kDerived;
        std::string paletteModeText;     // options.paletteMode as written
        std::string keyPrefix;           // options.keyPrefix, as the DLL built it (VerifyKeyPrefix at the run's start)
        std::optional<std::uint64_t> publishedToken;  // contract 9: staging\published.tsv's token for this start (none: no list)
        std::vector<KeyedSpec> objects;  // in request order
        std::map<int, int> addnSlots;    // original ADDN index -> slot ADDN index (40000 + place)
        std::map<std::string, std::uint64_t> nifHashes;  // nifs[] the game read: Data-relative path -> fnv64
        std::set<std::string> unreadNifs;                // nifs[] ok false, MeshDataPath like nifHashes and the objects' sources
        std::vector<BlackFlame::CopyContract::EfshPalette> efshPalettes;  // contract 6 (CopyContractEfsh.h ParseEfshPalettes)
        std::vector<BlackFlame::CopyContract::EfshPalette> efshBases;     // contract 8 (CopyContractEfshBase.h ParseEfshBases)
        std::vector<std::string> warnings;
    };

    // =========================================================================
    // Effect shader palettes (contract 6, GreyKeyedEfsh.cpp): no objects of the scheduler; each keyed one before them
    // =========================================================================
    // A palette is fire when, of its lit texels (brightest channel over kStatLitWeight; alpha not looked at: it is a
    // separate channel the shader reads only with greyscale-to-alpha), kEfshFireShare or more are fire coloured: hue in
    // 0..kEfshFireHueMax or from kEfshFireHueWrap (red to yellow), HSV saturation and value at least kEfshFireMinSat and
    // kEfshFireMinValue (the EFSH census's numbers, scratchpad efsh_fire_census.py). The whole palette is judged, not the
    // rows a shader samples (design v2 section 2's sampled coordinates: not done). Fire: its grey copy (DesaturatePalette:
    // luminance, alpha and rows kept) at PaletteCopyPath(key16) and an ok dds line. Not fire, or a format that does not
    // decode: no line, counted (the source palette stays). The key is checked against the dump's bytes first (a failed
    // line when they differ, like a NIF's).
    inline constexpr double kEfshFireHueMax = 55.0 / 360.0;
    inline constexpr double kEfshFireHueWrap = 345.0 / 360.0;
    inline constexpr double kEfshFireMinSat = 0.45;
    inline constexpr double kEfshFireMinValue = 0.30;
    inline constexpr double kEfshFireShare = 0.20;

    struct EfshTotals
    {
        std::size_t listed = 0;
        std::size_t grey = 0;       // written this run (ok line)
        std::size_t reused = 0;     // an ok line staging still matches
        std::size_t notFire = 0;    // skipped: no line
        std::size_t unreadable = 0; // skipped: the format does not decode (no line)
        std::size_t missing = 0;    // the game has no such file (no key)
        std::size_t notDumped = 0;  // outside the dump caps (no key)
        std::size_t unread = 0;     // keyed, the dump's read failed this start (textures[] unread): no line, the next start
        std::size_t failed = 0;     // a failed line (the next run tries again)
        std::size_t tooLarge = 0;
    };

    /// The fire test's share of a palette image (0 when it has no lit texel)
    [[nodiscard]] double EfshFireShare(const RgbaImage& a_image) noexcept;

    /// Every keyed palette of a_request: its line, or a count (see above). a_stop: the parent is gone (stop before the next)
    [[nodiscard]] EfshTotals BuildEfshPalettes(const KeyedRequest& a_request, const std::filesystem::path& a_dump, ProgressLog& a_log,
        Budget& a_budget, const BlackFlame::CopyContract::KeyPrefix& a_prefix, const std::function<bool()>& a_stop);

    /// "efsh   ..." for a run's end
    [[nodiscard]] std::string EfshTotalsLine(const EfshTotals& a_totals);

    // =========================================================================
    // Effect shader base textures (contract 8, GreyKeyedBases.cpp): the membrane fills and particle bases the DLL lists
    // (CopyContractEfshBase.h EfshBaseRoles), each keyed one after the palettes, before the scheduled objects
    // =========================================================================
    // Fire test for a base (EfshBaseFireShare): alpha-weighted over its visible lit texels (alpha > 0, brightest channel over
    // kStatLitWeight), the share at HSV saturation kEfshFireMinSat and value kEfshFireMinValue or more whose hue is orange
    // to yellow (kEfshBaseOrangeHueMin..kEfshFireHueMax), or red (under kEfshBaseOrangeHueMin or from kEfshFireHueWrap) at
    // value kEfshBaseRedMinValue or more. A base is fire at kEfshBaseFireShare or more. Measured on the full top mip of the
    // FEC census (build/fec-base, 2026-10-20; the v2.1 agent's measure.py / sens.py): animalburnshader 11.27%, burnshader
    // 23.01%, FXFireAtlas02 94.87%; fx_blooddrip 0.00% (its reds peak at value 0.451, under the red floor), the blood
    // splatters, smoke, soot, frost, shock and the rest 0.00% (the highest 0.008%). The threshold is under half the weakest
    // fire base and far over every non-fire one; the red floor is 0.1 over the brightest blood red.
    // Fire: a grey copy at CopyPath(textures\, key16, source) = copies\<key16>\copy.dds: every image of the chain decoded to
    // R8G8B8A8_UNORM, RGB to its Rec. 709 luminance in the encoded domain (Luminance, rounded), alpha bytes untouched, saved
    // RGBA8 with every mip (never recompressed: BC7 changed alpha by up to 140/255), read back and checked (size, mip count,
    // alpha exact, R = G = B on every mip) before its ok dds line. Not fire, or a format left out (sRGB for now, float/HDR,
    // typeless, planar, palettized, video, one or two channels, a cube map, an array or a volume): no line, counted and
    // logged (the source stays). These constants govern the new kind only: they are not in the golden constant digest; the
    // copy recipe is in its key (CopyContractEfshBase.h kEfshBaseRecipe), the fire test's numbers in the not-fire cache's tag.
    // Only the top mip is decoded for the test; a base that is not fire goes into a staging cache (builder only) so later
    // runs skip it unread. Copies stop at kMaxBaseCopyBytes together (the ones staging holds count first).
    inline constexpr double kEfshBaseOrangeHueMin = 15.0 / 360.0;
    inline constexpr double kEfshBaseRedMinValue = 0.55;
    inline constexpr double kEfshBaseFireShare = 0.05;
    // Raise when the fire test's code changes (EfshBaseFireShare, its hue rule, the top-mip pick, the accepted formats):
    // the not-fire cache's tag carries it, so an old "not fire" is judged again.
    inline constexpr int kEfshBaseFireTestVersion = 1;

    struct EfshBaseTotals
    {
        std::size_t listed = 0;
        std::size_t grey = 0;       // written this run (ok line)
        std::size_t reused = 0;
        std::size_t notFire = 0;    // skipped: no line
        std::size_t notFireCached = 0;  // of notFire, known from the staging cache (nothing read)
        std::size_t overCopyCap = 0;    // fire, skipped: the base copies would pass their cap (kMaxBaseCopyBytes)
        std::size_t leftOut = 0;    // skipped: a format left out (sRGB included) or one that does not decode
        std::size_t srgb = 0;       // of leftOut, sRGB sources
        std::size_t missing = 0;
        std::size_t notDumped = 0;
        std::size_t unread = 0;
        std::size_t failed = 0;
        std::size_t tooLarge = 0;
        std::uint64_t bytes = 0;      // the copies written this run
        std::uint64_t copyBytes = 0;  // every base copy of this request staging holds after the run (reused and written)
    };

    /// The base fire test's share of an image (0 when it has no visible lit texel)
    [[nodiscard]] double EfshBaseFireShare(const RgbaImage& a_image) noexcept;

    /// Every keyed base of a_request: its line, or a count (see above). a_stop: the parent is gone (stop before the next).
    /// a_copyCap: the base copies' total (the self-test's seam; kMaxBaseCopyBytes otherwise)
    [[nodiscard]] EfshBaseTotals BuildEfshBases(const KeyedRequest& a_request, const std::filesystem::path& a_dump, ProgressLog& a_log,
        Budget& a_budget, const BlackFlame::CopyContract::KeyPrefix& a_prefix, const std::function<bool()>& a_stop,
        std::uint64_t a_copyCap = BlackFlame::CopyContract::kMaxBaseCopyBytes);

    /// "bases  ..." for a run's end
    [[nodiscard]] std::string EfshBaseTotalsLine(const EfshBaseTotals& a_totals);

    /// request.json version 5 (LoadFormsRequestFile for the v4 parts, then options, keys, muzzles, addnMps, txst).
    /// Throws std::runtime_error naming the field (a version other than 5 included).
    [[nodiscard]] KeyedRequest LoadKeyedRequest(const std::filesystem::path& a_file);

    /// The builder's texture role as the contract names it
    [[nodiscard]] BlackFlame::CopyContract::TextureRole ContractRole(TextureRole a_role) noexcept;

    /// One of our palettes as the key prefix states it: the dump's file hashed, else notdumped:<size> when a_received
    /// (the DLL's prefix text) says so for a_field ("grad" / "gradadd"), else missing
    [[nodiscard]] BlackFlame::CopyContract::FileHash PaletteState(const std::filesystem::path& a_dump, std::string_view a_path,
        std::string_view a_received, std::string_view a_field, const std::set<std::string>& a_listed);

    /// What one object's build collected
    struct KeyedObject
    {
        std::vector<BlackFlame::CopyContract::ProgressDep> deps;  // its texture and palette copies
        std::vector<std::string> fallbacks;                       // deterministic fallbacks, each once
        std::string failed;                                       // the first transient failure
        std::optional<BlackFlame::CopyContract::Outcome> terminal;  // too_large or not_dumped
        std::string terminalWhy;

        void AddDep(const std::string& a_path, std::uint64_t a_fnv64);
    };

    /// a_build's line, or a failed line for a_spec's key when it throws (std::bad_alloc: "out of memory"): one object's
    /// exception never stops the run
    [[nodiscard]] BlackFlame::CopyContract::ProgressLine GuardObject(const KeyedSpec& a_spec,
        const std::function<BlackFlame::CopyContract::ProgressLine()>& a_build);

    /// The object's outcome and the line's reason: failed, else terminal, else fallback, else ok
    [[nodiscard]] std::pair<BlackFlame::CopyContract::Outcome, std::string> ClassifyObject(const KeyedObject& a_object);

    /// The child objects (texture and palette copies) of a key mode run, and the object being built (Begin)
    class KeyedOutputs
    {
    public:
        /// a_listed: the request's dumped textures (TextureDataPath), the only ones read
        KeyedOutputs(ProgressLog& a_log, Budget& a_budget, BlackFlame::CopyContract::KeyPrefix a_prefix, TextureJob& a_textures,
            std::set<std::string> a_listed, std::map<std::string, BlackFlame::CopyContract::Request::TextureState> a_states);

        void Begin(KeyedObject* a_object, std::optional<double> a_tintAddHands = std::nullopt) noexcept
        {
            m_object = a_object;
            m_tintAddHands = a_tintAddHands;
        }

        /// The object in hand has failed: its remaining slots, palettes and reads are not tried (second OCR review 4)
        [[nodiscard]] bool ObjectFailed() const noexcept { return m_object && !m_object->failed.empty(); }

        /// A texture slot (TextureDataPath a_path, picked as a_role at a_tint): its copy's slot text, "" when the slot stays
        [[nodiscard]] std::string Texture(const std::string& a_path, TextureRole a_role, float a_tint, const std::string& a_where);

        enum class Made
        {
            kMade,
            kFallback,  // deterministic: today's palette kept
            kFailed,    // transient: the object fails
            kGated      // the read gate refused a texture it needs and told the object: nothing more to say, not cached
        };
        using MakeImage = std::function<Made(RgbaImage&, std::string&)>;
        using WriteImage = std::function<bool(const RgbaImage&, const std::filesystem::path&, std::string&)>;

        /// A palette shader's derived palette from a_source (a TextureSource look that read): its slot text, "" when the
        /// shader keeps today's palette. a_make turns the source image into the palette (false: a deterministic fallback),
        /// a_write writes it (false: failed)
        [[nodiscard]] std::string Palette(const TextureLook& a_source, bool a_additive, const MakeImage& a_make,
            const WriteImage& a_write, std::string& a_why);

        void Fallback(const std::string& a_why);
        void Failed(const std::string& a_why);
        void Terminal(BlackFlame::CopyContract::Outcome a_outcome, const std::string& a_why);

        /// Every palette copy slot this run named -> additive (VerifyGrey's key mode list)
        [[nodiscard]] const std::map<std::string, bool>& Palettes() const noexcept { return m_paletteBlends; }

    private:
        enum class State
        {
            kCopy,      // the slot names the copy (dep)
            kLeft,      // already grey: the slot stays, nothing to say
            kFallback,
            kFailed,
            kTerminal
        };

        struct Child
        {
            State state = State::kFailed;
            std::string slot;
            std::uint64_t fnv64 = 0;
            BlackFlame::CopyContract::Outcome terminal = BlackFlame::CopyContract::Outcome::kTooLarge;
            std::string why;
            bool keep = true;  // false: not cached for the run's later objects (a gate refusal)
        };

        [[nodiscard]] std::string Apply(const Child& a_child);
        [[nodiscard]] Child MakeTexture(const std::string& a_path, TextureRole a_role, float a_tint, const std::string& a_where);
        [[nodiscard]] Child MakePalette(const TextureLook& a_source, bool a_additive, const MakeImage& a_make, const WriteImage& a_write);
        /// a_temp renamed to a_slot's place and its ok line appended; the child, or a failed one
        [[nodiscard]] Child Commit(const std::filesystem::path& a_temp, const std::string& a_slot, std::uint64_t a_key);
        [[nodiscard]] std::optional<Child> Reused(std::uint64_t a_key, const std::string& a_slot);

        ProgressLog& m_log;
        Budget& m_budget;
        BlackFlame::CopyContract::KeyPrefix m_prefix;
        TextureJob& m_textures;
        std::set<std::string> m_listed;
        std::map<std::string, BlackFlame::CopyContract::Request::TextureState> m_states;  // request v5 textures[].state
        KeyedObject* m_object = nullptr;
        std::optional<double> m_tintAddHands;
        std::map<std::string, Child> m_children;  // "<role|blend>|<source path>" -> what it gave
        std::map<std::string, bool> m_paletteBlends;
    };

    /// The whole key mode run. Exit 0 (every object has a line, whatever its outcome), 1 (the run could not go on),
    /// 2 (bad arguments or a request that is not version 5), kExitParentGone (stopped: the parent process is gone).
    [[nodiscard]] int RunKeyed(const std::filesystem::path& a_folder, const KeyedOptions& a_options);
}
