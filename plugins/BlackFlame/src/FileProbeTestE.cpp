#include "FileProbeRun.h"

#include <format>

// Test E of the dev file visibility probe (FileVisibilityProbe.h): a placeholder the LAST run left on disk, overwritten
// mid-session. The point: a path the engine's resource index already knew at startup (the placeholder existed before the
// game started) is rewritten with different bytes this session; does Demand / GetTexture then give the new content or the
// old placeholder? This needs no index call, so it may make meshes work where A-D fail.
//   keep_e.nif / keep_e.dds    Data\meshes|textures\heartofmagic\blackflame\fileprobe\, left for the next run (not probe_*,
//                              so RemoveProbeFiles never deletes them), overwritten with a DIFFERENT byte size / dimensions
//   keep_es.nif / keep_es.dds  the same, overwritten with the source's exact byte size / dimensions (E-same), when such a
//                              different-content file of the same size was found
// On the very first run the placeholders do not exist yet: E is SKIPPED and they are written at the end for the next run.
// A mesh PASS needs the loaded root node's name to be the OVERWRITE NIF's, not the placeholder's (the source NIF). A texture
// PASS needs the overwrite DDS's dimensions. E-same's mesh is told apart by the root name too; E-texture-same cannot be
// (same dimensions), so it is SKIPPED with that reason.

namespace BlackFlame::FileProbe
{
    namespace
    {
        constexpr std::string_view kFolder = "heartofmagic\\blackflame\\fileprobe\\";
        constexpr std::string_view kMeshesRoot = "meshes\\";
        constexpr std::string_view kTexturesRoot = "textures\\";
        constexpr std::uint32_t kMaxMipSkip = 3;  // texture quality may load a DDS without its top mips (as FileProbeTests.cpp)

        struct Placeholder
        {
            std::string_view stem;        // "keep_e" / "keep_es"
            std::string_view meshTest;
            std::string_view textureTest;
        };
        constexpr Placeholder kE{ "keep_e", "E-mesh", "E-texture" };
        constexpr Placeholder kESame{ "keep_es", "E-mesh-same", "E-texture-same" };

        std::string MeshRecord(const Placeholder& a_p) { return std::format("{}{}.nif", kFolder, a_p.stem); }
        std::string MeshData(const Placeholder& a_p) { return std::string(kMeshesRoot) + MeshRecord(a_p); }
        std::string TextureData(const Placeholder& a_p) { return std::format("{}{}{}.dds", kTexturesRoot, kFolder, a_p.stem); }

        bool Exists(const Run& a_run, const std::string& a_dataPath)
        {
            std::error_code ec;
            return std::filesystem::exists(a_run.data / a_dataPath, ec);
        }

        void SkipMesh(Run& a_run, const Placeholder& a_p, const std::string& a_why) { SetVerdict(a_run, a_p.meshTest, "SKIPPED", a_why); }

        // Overwrite the placeholder's NIF with a_newNif (Demanded here for the first time this session), judge by the root name:
        // the new content's root (a_newRoot) vs the placeholder's old content (the source NIF's root, a_run.sourceNifRoot)
        void JudgeMesh(Run& a_run, const Placeholder& a_p, const Bytes& a_newNif, const std::string& a_newRoot)
        {
            const auto file = a_run.data / MeshData(a_p);
            std::string why;
            if (!WriteBytes(file, a_newNif, why)) {
                SkipMesh(a_run, a_p, std::format("could not overwrite '{}': {}", PathText(file), why));
                return;
            }
            Note(a_run, std::format("{}: overwrote '{}' with {} bytes (was the source NIF); new content's root {}", a_p.meshTest,
                PathText(file), a_newNif.size(), a_newRoot.empty() ? "(unknown)" : a_newRoot));
            std::string loadedRoot;
            const auto record = MeshRecord(a_p);
            const auto result = DemandNamed(record, loadedRoot);
            const auto text = std::format("{}; loaded root {}, placeholder's old root {}", Text(record, result),
                loadedRoot.empty() ? "(none)" : loadedRoot, a_run.sourceNifRoot.empty() ? "(none)" : a_run.sourceNifRoot);
            if (!Passed(result)) {
                SetVerdict(a_run, a_p.meshTest, "FAIL", text);
            } else if (a_newRoot.empty() || a_newRoot == a_run.sourceNifRoot) {
                SetVerdict(a_run, a_p.meshTest, "INCONCLUSIVE", "the new and old NIFs are not told apart by root name/children; " + text);
            } else if (loadedRoot == a_newRoot) {
                SetVerdict(a_run, a_p.meshTest, "PASS", "served the new content; " + text);
            } else if (loadedRoot == a_run.sourceNifRoot) {
                SetVerdict(a_run, a_p.meshTest, "FAIL", "served the old placeholder content (the new file on disk was not read); " + text);
            } else {
                SetVerdict(a_run, a_p.meshTest, "INCONCLUSIVE", "the loaded root is neither the new nor the old content; " + text);
            }
        }

        void OverwriteTexture(Run& a_run, const Placeholder& a_p, const Bytes& a_newDds)
        {
            const auto file = a_run.data / TextureData(a_p);
            std::string why;
            if (!WriteBytes(file, a_newDds, why)) {
                SetVerdict(a_run, a_p.textureTest, "SKIPPED", std::format("could not overwrite '{}': {}", PathText(file), why));
                return;
            }
            Note(a_run, std::format("{}: overwrote '{}' with {} bytes", a_p.textureTest, PathText(file), a_newDds.size()));
            Request(a_run, a_p.textureTest, TextureData(a_p));  // judged with the others, against the overwrite dimensions
        }
    }

    // E-texture: the overwrite DDS's dimensions, not the source's (a stale read would give the source's)
    void JudgeETexture(Run& a_run, const TextureLook& a_look)
    {
        if (!a_look.returned || !a_look.source || !a_look.renderer || a_look.fault) {
            SetVerdict(a_run, "E-texture", "FAIL", Text(a_look));
            return;
        }
        if (a_run.missingLook.source && !a_run.missingLook.renderer) {
            SetVerdict(a_run, "E-texture", "INCONCLUSIVE", "a missing path's texture got no GPU texture within the wait; " + Text(a_look));
            return;
        }
        bool overwrite = false;
        bool placeholder = false;
        for (std::uint32_t skip = 0; skip <= kMaxMipSkip; ++skip) {
            overwrite = overwrite || (a_look.width == (std::max)(1u, a_run.eDdsWidth >> skip) &&
                                         a_look.height == (std::max)(1u, a_run.eDdsHeight >> skip));
            placeholder = placeholder || (a_look.width == (std::max)(1u, a_run.ddsWidth >> skip) &&
                                             a_look.height == (std::max)(1u, a_run.ddsHeight >> skip));
        }
        const auto text = std::format("{}; overwrite DDS {}x{}, placeholder DDS {}x{}", Text(a_look), a_run.eDdsWidth, a_run.eDdsHeight,
            a_run.ddsWidth, a_run.ddsHeight);
        if (overwrite && !placeholder) {
            SetVerdict(a_run, "E-texture", "PASS", "served the new content's dimensions; " + text);
        } else if (placeholder && !overwrite) {
            SetVerdict(a_run, "E-texture", "FAIL", "served the old placeholder's dimensions; " + text);
        } else {
            SetVerdict(a_run, "E-texture", "INCONCLUSIVE", "the dimensions match both or neither; " + text);
        }
    }

    void NoteEPlaceholders(Run& a_run)
    {
        // Per resource: each E test needs only its own placeholder (E-texture-same is never run, so keep_es.dds is not checked)
        a_run.eMeshExisted = Exists(a_run, MeshData(kE));
        a_run.eTextureExisted = Exists(a_run, TextureData(kE));
        a_run.eSameMeshExisted = Exists(a_run, MeshData(kESame));
        const auto yesNo = [](bool a_value) { return a_value ? "yes" : "no"; };
        Note(a_run, std::format("E: at startup keep_e.nif in place: {}, keep_e.dds: {}, keep_es.nif: {} (a first run writes them "
                                "for the next)", yesNo(a_run.eMeshExisted), yesNo(a_run.eTextureExisted), yesNo(a_run.eSameMeshExisted)));
    }

    void TestE(Run& a_run)
    {
        constexpr const char* kNoPlaceholder = "placeholder did not exist at startup (written for the next run)";
        // E-mesh and E-texture each on their own placeholder and their own overwrite file (Codex P2 on 41d7fb3)
        if (!a_run.eMeshExisted) {
            SkipMesh(a_run, kE, kNoPlaceholder);
        } else if (!a_run.eNif) {
            SkipMesh(a_run, kE, "no different-size overwrite NIF was found");
        } else {
            std::string newRoot;
            if (!a_run.eNifRecord.empty()) {
                (void)DemandNamed(a_run.eNifRecord, newRoot);  // the overwrite NIF at its own path: the new content's root
            }
            JudgeMesh(a_run, kE, *a_run.eNif, newRoot);
        }
        if (!a_run.eTextureExisted) {
            SetVerdict(a_run, kE.textureTest, "SKIPPED", kNoPlaceholder);
        } else if (!a_run.eDds) {
            SetVerdict(a_run, kE.textureTest, "SKIPPED", "no different-dimension overwrite DDS was found");
        } else {
            OverwriteTexture(a_run, kE, *a_run.eDds);
        }
        // E-same (same-size / same-dimension overwrite): the mesh by root name; the texture cannot be told apart by size
        SetVerdict(a_run, kESame.textureTest, "SKIPPED",
            "a same-dimension overwrite cannot be told from the placeholder by GetTexture (size is the only signal)");
        if (!a_run.eSameMeshExisted) {
            SkipMesh(a_run, kESame, kNoPlaceholder);
        } else if (!a_run.eSameNif) {
            SkipMesh(a_run, kESame, "no same-size different-content NIF was found");
        } else {
            std::string newRoot;
            if (!a_run.eSameNifRecord.empty()) {
                (void)DemandNamed(a_run.eSameNifRecord, newRoot);
            }
            JudgeMesh(a_run, kESame, *a_run.eSameNif, newRoot);
        }
    }

    void WriteEPlaceholders(Run& a_run)
    {
        if (!a_run.nif || !a_run.dds) {
            return;
        }
        // The placeholders for the next run: the source NIF / DDS (the next run overwrites them and checks the new content
        // wins). Not probe_* names, so the next run's cleanup leaves them. Rewritten every run to the current source
        for (const auto& p : { kE, kESame }) {
            for (const auto& [data, bytes] : { std::pair{ MeshData(p), a_run.nif }, std::pair{ TextureData(p), a_run.dds } }) {
                const auto file = a_run.data / data;
                std::string why;
                const bool ok = WriteBytes(file, *bytes, why);
                Note(a_run, std::format("E: placeholder '{}' for the next run: {}", PathText(file), ok ? "written" : "FAILED: " + why));
            }
        }
    }
}
