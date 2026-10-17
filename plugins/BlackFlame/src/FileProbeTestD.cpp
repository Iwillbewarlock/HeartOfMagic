#include "FileProbeRun.h"

#include "FileProbeEngine.h"
#include "SehGuard.h"

#include <format>
#include <optional>

// Tests D and D-neg of the dev file visibility probe (FileVisibilityProbe.h): the engine's own LooseFileLocation, rooted at
// a folder of ours outside Data, registered with BSResource::RegisterLocation at the probe's time. LooseFileLocation::Create
// (RE/L/LooseFileLocation.h) allocates it on the game heap, zeroes it, puts the engine's vtable in and sets its prefix (the
// folder its paths are read under); the engine keeps the pointer for the rest of the process, so it is never freed.
// Folders: <SKSE log folder>\HeartOfMagic_BlackFlame\fileprobe-root\ (D) and ...\fileprobe-root-neg\ (D-neg), outside MO2's
// VFS; the prefix is the folder's full path with a trailing '\' - whether a LooseFileLocation takes a full path as its
// prefix is what the run shows (the engine's own Data location's name is in the "B: before" lines).
//   D       files written, then the location registered; asked for a frame later (a location the engine mounts later,
//           GlobalLocations' pendingMount list, has a frame to mount)
//   D-neg   the real use: the location registered EMPTY in the first step, left to mount (FileVisibilityProbe kMountWait),
//           then the files written at fresh paths, then asked for the first time

namespace BlackFlame::FileProbe
{
    namespace
    {
        constexpr std::string_view kFolder = "heartofmagic\\blackflame\\droot\\";
        constexpr std::string_view kMeshesRoot = "meshes\\";
        constexpr std::string_view kTexturesRoot = "textures\\";

        struct Variant
        {
            std::string_view label;    // "d" / "dneg": in the file names
            std::string_view folder;   // a folder in <SKSE log folder>\HeartOfMagic_BlackFlame
            std::string_view meshTest;
            std::string_view textureTest;
        };
        constexpr Variant kD{ "d", "fileprobe-root", "D-mesh", "D-texture" };
        constexpr Variant kDNeg{ "dneg", "fileprobe-root-neg", "D-mesh-neg", "D-texture-neg" };

        std::optional<std::filesystem::path> RootFolder(const Variant& a_variant)
        {
            const auto dir = logger::log_directory();
            if (!dir) {
                return std::nullopt;
            }
            return *dir / "HeartOfMagic_BlackFlame" / std::string(a_variant.folder);
        }

        std::string MeshRecord(const Run& a_run, const Variant& a_variant)
        {
            return std::format("{}probe_{}_{}.nif", kFolder, a_variant.label, a_run.tag);
        }

        std::string TextureData(const Run& a_run, const Variant& a_variant)
        {
            return std::format("{}{}probe_{}_{}.dds", kTexturesRoot, kFolder, a_variant.label, a_run.tag);
        }

        void Skip(Run& a_run, const Variant& a_variant, const std::string& a_why)
        {
            SetVerdict(a_run, a_variant.meshTest, "SKIPPED", a_why);
            SetVerdict(a_run, a_variant.textureTest, "SKIPPED", a_why);
        }

        // The root folder with the last run's probe files removed; nothing (and both tests SKIPPED) when it cannot be named
        std::optional<std::filesystem::path> CleanRoot(Run& a_run, const Variant& a_variant, std::string& a_prefix)
        {
            const auto root = RootFolder(a_variant);
            if (!root) {
                Skip(a_run, a_variant, "no SKSE log folder for the location's root");
                return std::nullopt;
            }
            try {
                a_prefix = root->string() + "\\";
            } catch (...) {
                Skip(a_run, a_variant, "the root folder's path is outside the ANSI code page");
                return std::nullopt;
            }
            RemoveProbeFiles(a_run, *root / std::string(kMeshesRoot) / std::string(kFolder));
            RemoveProbeFiles(a_run, *root / std::string(kTexturesRoot) / std::string(kFolder));
            return root;
        }

        bool WriteFilesUnder(Run& a_run, const Variant& a_variant, const std::filesystem::path& a_root)
        {
            const std::string meshData = std::string(kMeshesRoot) + MeshRecord(a_run, a_variant);
            for (const auto& [dataPath, bytes] : { std::pair{ meshData, a_run.nif }, std::pair{ TextureData(a_run, a_variant), a_run.dds } }) {
                const auto file = a_root / dataPath;
                std::string why;
                if (!WriteBytes(file, *bytes, why)) {
                    Skip(a_run, a_variant, std::format("'{}' could not be written: {}", PathText(file), why));
                    return false;
                }
                Note(a_run, std::format("{}: written '{}' ({} bytes)", a_variant.meshTest, PathText(file), bytes->size()));
            }
            return true;
        }

        bool RegisterRoot(Run& a_run, const Variant& a_variant, const std::string& a_prefix)
        {
            RE::BSResource::LooseFileLocation* location = nullptr;
            if (!GuardedKeep("file probe D create", [&]() { location = RE::BSResource::LooseFileLocation::Create(a_prefix.c_str()); }) ||
                !location) {
                Skip(a_run, a_variant, "LooseFileLocation::Create gave nothing (or faulted)");
                return false;
            }
            const auto priority = ChoosePriority(a_run, a_variant.meshTest);
            const bool ok = GuardedKeep("file probe D RegisterLocation", [&]() { RE::BSResource::RegisterLocation(location, priority); });
            Note(a_run, std::format("{}: LooseFileLocation prefix '{}' {} at priority {}", a_variant.meshTest, a_prefix,
                ok ? "registered" : "FAULTED in RegisterLocation (logged)", priority));
            if (!ok) {
                Skip(a_run, a_variant, "RegisterLocation faulted");
            }
            return ok;
        }

        void Check(Run& a_run, const Variant& a_variant)
        {
            for (const auto& line : Engine::DescribeLocations()) {
                Note(a_run, std::format("{}: locations now: {}", a_variant.meshTest, line));
            }
            const auto record = MeshRecord(a_run, a_variant);
            Note(a_run, std::format("{}: {}", a_variant.meshTest, StreamText(std::string(kMeshesRoot) + record)));
            const auto result = Demand(record);
            SetVerdict(a_run, a_variant.meshTest, Passed(result) ? "PASS" : "FAIL", Text(record, result));
            const auto texture = TextureData(a_run, a_variant);
            Note(a_run, std::format("{}: {}", a_variant.textureTest, StreamText(texture)));
            Request(a_run, a_variant.textureTest, texture);
        }
    }

    void TestD(Run& a_run)
    {
        std::string prefix;
        const auto root = CleanRoot(a_run, kD, prefix);
        if (root && WriteFilesUnder(a_run, kD, *root)) {
            a_run.dRegistered = RegisterRoot(a_run, kD, prefix);
        }
    }

    void TestDCheck(Run& a_run)
    {
        if (a_run.dRegistered) {
            Check(a_run, kD);
        }
    }

    void RegisterDNegEarly(Run& a_run)
    {
        std::string prefix;
        if (const auto root = CleanRoot(a_run, kDNeg, prefix)) {
            std::error_code ec;
            std::filesystem::create_directories(*root, ec);  // the root exists, empty of probe files, before it is registered
            a_run.dNegRegistered = RegisterRoot(a_run, kDNeg, prefix);
        }
    }

    void CreateDNegFiles(Run& a_run)
    {
        if (!a_run.dNegRegistered) {
            return;
        }
        if (const auto root = RootFolder(kDNeg); !root || !WriteFilesUnder(a_run, kDNeg, *root)) {
            a_run.dNegRegistered = false;  // SKIPPED with the reason by WriteFilesUnder (or no root: logged at registration)
        }
    }

    void CheckDNeg(Run& a_run)
    {
        if (a_run.dNegRegistered) {
            Check(a_run, kDNeg);
        }
    }
}
