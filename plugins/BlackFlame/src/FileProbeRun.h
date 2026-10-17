#pragma once

#include "Common.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// =============================================================================
// FILE PROBE RUN - the dev file visibility probe's state and its test steps (FileVisibilityProbe.h runs them)
// =============================================================================
// Main thread only: every step runs from the main-thread queue, one step a frame.

namespace BlackFlame::FileProbe
{
    using Clock = std::chrono::steady_clock;
    using Bytes = std::vector<std::byte>;

    /// The tests in the order the summary line names them
    /// D and D-neg each twice: "-data" = the files under <root>\data\meshes|textures\ instead of <root>\meshes|textures (FP5)
    inline constexpr std::array<std::string_view, 25> kTests{ "A-mesh", "A-texture", "B-mesh", "B-texture", "C-mesh", "C-texture",
        "D-mesh", "D-texture", "D-mesh-data", "D-texture-data", "A-mesh-neg", "A-texture-neg", "C-mesh-neg", "C-texture-neg",
        "B-mesh-neg", "B-texture-neg", "D-mesh-neg", "D-texture-neg", "D-mesh-neg-data", "D-texture-neg-data", "C-mesh-late", "C-texture-late", "C-mesh-loading", "C-texture-loading", "control-mesh" };

    /// The summary line's parts: how many of kTests (in order) each names. The first part has no label
    struct TestGroup
    {
        std::string_view label;
        std::size_t count;
    };
    inline constexpr std::array<TestGroup, 6> kTestGroups{ TestGroup{ "", 10 },
        TestGroup{ "asked before the file existed", 4 },    // A-neg, C-neg
        TestGroup{ "location registered before the file", 6 },  // B-neg, D-neg: the real use
        TestGroup{ "registered after a failed request", 2 },     // C-late
        TestGroup{ "while a load is in flight", 2 },            // C-loading
        TestGroup{ "environment", 1 } };                        // control: a shipped NIF that must load

    struct Verdict
    {
        std::string word = "NOT RUN";  // PASS, FAIL, SKIPPED, INCONCLUSIVE, NOT RUN
        std::string why;
    };

    /// What a GetTexture call handed back, read at once
    struct TextureLook
    {
        bool returned = false;  // a texture object came back
        bool source = false;    // an NiSourceTexture
        bool renderer = false;  // with its GPU texture made
        bool fault = false;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::string name;
    };

    /// A texture asked for now and judged later (the GPU texture may be made after the call returns)
    struct PendingTexture
    {
        std::string test;
        std::string path;  // the form GetTexture was given
        RE::NiPointer<RE::NiTexture> texture;
    };

    struct Run
    {
        std::string tag;                       // unique per run: every written or served path carries it
        std::filesystem::path data;            // <game folder>\Data
        std::string sourceModel;               // the source NIF as its record stores it (Demand form, no meshes\)
        std::string sourceTexture;             // the source DDS, "textures\\..."
        std::vector<std::string> otherTextures;  // the source NIF's other DDS slots (C-loading's in-flight load)
        std::shared_ptr<const Bytes> nif;
        std::shared_ptr<const Bytes> dds;
        std::uint32_t ddsWidth = 0;            // from the DDS header
        std::uint32_t ddsHeight = 0;
        TextureLook sourceLook;                // the source DDS through GetTexture at its own path (the expected size)
        TextureLook missingLook;               // a path no file has, through GetTexture (what a miss gives), read when judging
        std::set<std::string, std::less<>> writeFailed;  // labels ("a", "aneg", "c", "cneg") whose files could not be written
        std::string inFlightTarget;            // C-loading: a DDS of another projectile, never asked for by the probe before
        bool loadInFlight = false;             // C-loading: that load had no GPU texture yet when the files were registered
        std::string inFlightState = "not started";
        std::map<std::string, Verdict, std::less<>> results;
        std::vector<PendingTexture> textures;
        std::vector<std::string> lines;        // every line, for the result file
        Clock::time_point begun = Clock::now();
        Clock::time_point judgeAt{};
        bool ready = false;                    // Prepare found the source bytes
        bool dRegistered = false;              // test D's LooseFileLocation was registered (checked a frame later)
        bool dNegRegistered = false;           // D-neg's empty LooseFileLocation was registered (kEarly, after A, C, B)
        std::optional<std::size_t> bNegLocation;  // B-neg's empty memory location (FileProbeLocation.h id)
        Clock::time_point earlyAt{};           // when B-neg and D-neg were registered
        int framesSinceEarly = 0;              // frames the late wait has seen
    };

    struct DemandResult
    {
        bool fault = false;
        int code = 0;
        bool node = false;
        long long ms = 0;
    };

    // Shared by the test files (FileProbeTests.cpp)
    [[nodiscard]] DemandResult Demand(const std::string& a_recordPath);  // BSModelDB::Demand, SEH-guarded
    [[nodiscard]] bool Passed(const DemandResult& a_result);
    [[nodiscard]] std::string Text(const std::string& a_recordPath, const DemandResult& a_result);
    [[nodiscard]] std::string StreamText(const std::string& a_dataPath);  // a plain BSResourceNiBinaryStream open
    void Request(Run& a_run, std::string_view a_test, const std::string& a_dataPath);  // GetTexture now, judged later
    [[nodiscard]] std::string AskTextureText(const std::string& a_dataPath);           // GetTexture now, described at once
    inline constexpr std::string_view kInFlightTexture = "in-flight load";  // a PendingTexture kept only to keep its load going
    inline constexpr std::string_view kControlTexture = "control";         // the source DDS at its own path: the expected size
    inline constexpr std::string_view kMissingTexture = "missing";         // a path no file has: what a miss returns
    [[nodiscard]] bool WriteBytes(const std::filesystem::path& a_file, const Bytes& a_bytes, std::string& a_why);
    [[nodiscard]] std::string PathText(const std::filesystem::path& a_path);
    /// Only regular files named probe_*.nif / probe_*.dds directly inside a_folder
    void RemoveProbeFiles(Run& a_run, const std::filesystem::path& a_folder);
    // FileProbeSources.cpp
    [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> DdsSize(const Bytes& a_bytes);  // width, height
    [[nodiscard]] bool PickSources(Run& a_run);  // the source NIF and DDS (a projectile's, fire first; else the palette)
    void PickInFlightTarget(Run& a_run);         // C-loading: a DDS of another projectile the probe never asked for
    /// Every texture asked for has its GPU texture (the wait before JudgeTextures may end early)
    [[nodiscard]] bool TexturesReady(const Run& a_run);

    /// Logs a_text ("BlackFlame: file visibility probe: <text>", written through) and keeps it for the result file
    void Note(Run& a_run, std::string a_text);

    void SetVerdict(Run& a_run, std::string_view a_test, std::string a_word, std::string a_why);

    // The probe's own paths. a_label: "a", "aneg", "c", "cneg"
    [[nodiscard]] std::string MeshRecordPath(const Run& a_run, std::string_view a_label);  // heartofmagic\...\probe_<label>_<tag>.nif
    [[nodiscard]] std::string MeshDataPath(const Run& a_run, std::string_view a_label);    // meshes\ + the record path
    [[nodiscard]] std::string TextureDataPath(const Run& a_run, std::string_view a_label); // textures\heartofmagic\...\probe_<label>_<tag>.dds

    // Steps (FileProbeTests.cpp); each logs everything it does
    bool Prepare(Run& a_run);       // engine scans, old files removed, source NIF and DDS read, controls
    void PreRequest(Run& a_run);    // the "-neg" paths asked for before their files exist
    void WriteFiles(Run& a_run);    // the A and C files written into Data
    void TestA(Run& a_run);         // loose files written this session
    void TestB(Run& a_run);         // a registered memory location
    void TestC(Run& a_run);         // the files put into the engine's index (RegisterLoose)
    void TestD(Run& a_run);         // the engine's own LooseFileLocation over a folder of ours, registered (FileProbeTestD.cpp)
    void TestDCheck(Run& a_run);    // a frame later: its files asked for
    void RegisterDNegEarly(Run& a_run);  // kEarly: D-neg's location registered over an empty folder
    void CreateDNegFiles(Run& a_run);    // after the mount wait: its files written at fresh paths
    void CheckDNeg(Run& a_run);          // a frame later: asked for the first time
    // FileProbeTestLate.cpp
    void RegisterEarly(Run& a_run);  // kEarly (after A, C, B): B-neg's empty memory location and D-neg's root
    void LateCreate(Run& a_run);     // after the mount wait: B-neg and D-neg files made; C-late's first (failing) requests
    void LateCheck(Run& a_run);      // next frame: B-neg and D-neg asked; C-late written, registered, asked; C-loading
    [[nodiscard]] std::uint32_t ChoosePriority(Run& a_run, std::string_view a_who);  // RegisterLocation's priority, logged
    void JudgeTextures(Run& a_run); // every texture asked for, by its loaded size
}
