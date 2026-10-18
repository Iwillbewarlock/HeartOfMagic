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
    };

    struct KeyedRequest
    {
        FormsRequest base;
        double tint = kDefaultTint;
        double tintAdd = kDefaultTintAdd;
        PaletteMode paletteMode = PaletteMode::kDerived;
        std::string paletteModeText;     // options.paletteMode as written
        std::string keyPrefix;           // options.keyPrefix, as the DLL built it (VerifyKeyPrefix at the run's start)
        std::vector<KeyedSpec> objects;  // in request order
        std::map<int, int> addnSlots;    // original ADDN index -> slot ADDN index (40000 + place)
        std::map<std::string, std::uint64_t> nifHashes;  // nifs[] the game read: Data-relative path -> fnv64
        std::set<std::string> unreadNifs;                // nifs[] ok false, MeshDataPath like nifHashes and the objects' sources
        std::vector<std::string> warnings;
    };

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

        void Begin(KeyedObject* a_object) noexcept { m_object = a_object; }

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
        std::map<std::string, Child> m_children;  // "<role|blend>|<source path>" -> what it gave
        std::map<std::string, bool> m_paletteBlends;
    };

    /// The whole key mode run. Exit 0 (every object has a line, whatever its outcome), 1 (the run could not go on),
    /// 2 (bad arguments or a request that is not version 5), kExitParentGone (stopped: the parent process is gone).
    [[nodiscard]] int RunKeyed(const std::filesystem::path& a_folder, const KeyedOptions& a_options);
}
