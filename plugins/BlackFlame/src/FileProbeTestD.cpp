#include "FileProbeRun.h"

#include "FileProbeEngine.h"
#include "SehGuard.h"

#include <format>
#include <optional>

// Test D of the dev file visibility probe (FileVisibilityProbe.h): the engine's own LooseFileLocation, rooted at a folder of
// ours outside Data, registered with BSResource::RegisterLocation at the probe's time. LooseFileLocation::Create
// (RE/L/LooseFileLocation.h) allocates it on the game heap, zeroes it, puts the engine's vtable in and sets its prefix (the
// folder its paths are read under); the engine keeps the pointer for the rest of the process, so it is never freed.
// The folder: <SKSE log folder>\HeartOfMagic_BlackFlame\fileprobe-root\ (outside MO2's VFS), its prefix the folder's full
// path with a trailing '\' - whether a LooseFileLocation takes a full path as its prefix is what the run shows (the
// engine's own Data location's name is in the "B: before" lines). Registered in one frame, asked for in the next, so a
// location the engine mounts later (GlobalLocations' pendingMount list) has a frame to mount.

namespace BlackFlame::FileProbe
{
    namespace
    {
        constexpr std::string_view kFolder = "heartofmagic\\blackflame\\droot\\";
        constexpr std::string_view kMeshesRoot = "meshes\\";
        constexpr std::string_view kTexturesRoot = "textures\\";
        constexpr std::uint32_t kPriorityWithoutLoose = 0;

        std::optional<std::filesystem::path> RootFolder()
        {
            const auto dir = logger::log_directory();
            if (!dir) {
                return std::nullopt;
            }
            return *dir / "HeartOfMagic_BlackFlame" / "fileprobe-root";
        }

        std::string MeshRecord(const Run& a_run) { return std::format("{}probe_d_{}.nif", kFolder, a_run.tag); }
        std::string TextureData(const Run& a_run) { return std::format("{}{}probe_d_{}.dds", kTexturesRoot, kFolder, a_run.tag); }

        void SkipD(Run& a_run, const std::string& a_why)
        {
            SetVerdict(a_run, "D-mesh", "SKIPPED", a_why);
            SetVerdict(a_run, "D-texture", "SKIPPED", a_why);
        }
    }

    void TestD(Run& a_run)
    {
        const auto root = RootFolder();
        if (!root) {
            SkipD(a_run, "no SKSE log folder for the location's root");
            return;
        }
        std::string prefix;
        try {
            prefix = root->string() + "\\";
        } catch (...) {
            SkipD(a_run, "the root folder's path is outside the ANSI code page");
            return;
        }
        RemoveProbeFiles(a_run, *root / std::string(kMeshesRoot) / std::string(kFolder));
        RemoveProbeFiles(a_run, *root / std::string(kTexturesRoot) / std::string(kFolder));
        const std::string meshData = std::string(kMeshesRoot) + MeshRecord(a_run);
        for (const auto& [dataPath, bytes] : { std::pair{ meshData, a_run.nif }, std::pair{ TextureData(a_run), a_run.dds } }) {
            const auto file = *root / dataPath;
            std::string why;
            if (!WriteBytes(file, *bytes, why)) {
                SkipD(a_run, std::format("'{}' could not be written: {}", PathText(file), why));
                return;
            }
            Note(a_run, std::format("D: written '{}' ({} bytes)", PathText(file), bytes->size()));
        }
        RE::BSResource::LooseFileLocation* location = nullptr;
        if (!GuardedKeep("file probe D create", [&]() { location = RE::BSResource::LooseFileLocation::Create(prefix.c_str()); }) ||
            !location) {
            SkipD(a_run, "LooseFileLocation::Create gave nothing (or faulted)");
            return;
        }
        const auto loose = Engine::LooseFilePriority();
        const auto priority = loose.value_or(kPriorityWithoutLoose);
        const bool ok = GuardedKeep("file probe D RegisterLocation", [&]() { RE::BSResource::RegisterLocation(location, priority); });
        Note(a_run, std::format("D: LooseFileLocation prefix '{}' {} at priority {} ({})", prefix,
            ok ? "registered" : "FAULTED in RegisterLocation (logged)", priority, loose ? "the Data LooseFileLocation's" : "no LooseFileLocation listed"));
        if (!ok) {
            SkipD(a_run, "RegisterLocation faulted");
            return;
        }
        a_run.dRegistered = true;
    }

    void TestDCheck(Run& a_run)
    {
        if (!a_run.dRegistered) {
            return;
        }
        for (const auto& line : Engine::DescribeLocations()) {
            Note(a_run, "D: after: " + line);
        }
        const auto record = MeshRecord(a_run);
        Note(a_run, "D-mesh: " + StreamText(std::string(kMeshesRoot) + record));
        const auto result = Demand(record);
        SetVerdict(a_run, "D-mesh", Passed(result) ? "PASS" : "FAIL", Text(record, result));
        const auto texture = TextureData(a_run);
        Note(a_run, "D-texture: " + StreamText(texture));
        Request(a_run, "D-texture", texture);
    }
}
