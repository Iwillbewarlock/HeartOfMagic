#pragma once

#include "Common.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
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
    inline constexpr std::array<std::string_view, 13> kTests{ "A-mesh", "A-texture", "B-mesh", "B-texture", "C-mesh", "C-texture",
        "D-mesh", "D-texture", "A-mesh-neg", "A-texture-neg", "C-mesh-neg", "C-texture-neg", "control-mesh" };
    inline constexpr std::size_t kMainTests = 8;      // then the summary's "negative cache" part
    inline constexpr std::size_t kNegativeTests = 4;  // then the control: a shipped NIF that must load (else the environment failed)

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
        std::shared_ptr<const Bytes> nif;
        std::shared_ptr<const Bytes> dds;
        std::uint32_t ddsWidth = 0;            // from the DDS header
        std::uint32_t ddsHeight = 0;
        TextureLook sourceLook;                // the source DDS through GetTexture at its own path (the expected size)
        TextureLook missingLook;               // a path no file has, through GetTexture (what a miss gives)
        std::map<std::string, Verdict, std::less<>> results;
        std::vector<PendingTexture> textures;
        std::vector<std::string> lines;        // every line, for the result file
        Clock::time_point begun = Clock::now();
        Clock::time_point judgeAt{};
        bool ready = false;                    // Prepare found the source bytes
        bool dRegistered = false;              // test D's LooseFileLocation was registered (checked a frame later)
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
    [[nodiscard]] bool WriteBytes(const std::filesystem::path& a_file, const Bytes& a_bytes, std::string& a_why);
    [[nodiscard]] std::string PathText(const std::filesystem::path& a_path);
    /// Only regular files named probe_*.nif / probe_*.dds directly inside a_folder
    void RemoveProbeFiles(Run& a_run, const std::filesystem::path& a_folder);
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
    void JudgeTextures(Run& a_run); // every texture asked for, by its loaded size
}
