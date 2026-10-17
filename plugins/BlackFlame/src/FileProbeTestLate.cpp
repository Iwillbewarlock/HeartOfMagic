#include "FileProbeRun.h"

#include "FileProbeEngine.h"
#include "FileProbeLocation.h"
#include "SehGuard.h"

#include <format>

// The dev file visibility probe's tests of the real use (FileVisibilityProbe.h): the file appears after the route is set up
// or after the game asked for it.
//   B-neg      an EMPTY memory location registered in the first step (RegisterEarly), left to mount for
//              FileVisibilityProbe::kMountWait, then the files added to it at fresh paths, then asked for the first time
//   D-neg      the same with the engine's LooseFileLocation over an empty folder (FileProbeTestD.cpp)
//   C-late     asked for late in the session with no file (fails), then written into Data, put into the index
//              (RegisterLoose), asked again. C-neg asks before its file exists too, but early, before any location of ours
//              is registered; C-late's failed request and its registration are a frame apart, after all of them
//   C-loading  a texture load started without waiting (BSShaderManager::GetTexture with demand false: the load is queued, so
//              it can be in flight), then at once the files written, registered and asked for. BSModelDB has only Demand
//              (synchronous) in CommonLib's headers, so the in-flight load is a texture for the mesh case too; whether it
//              was in flight is read from it (no GPU texture yet = still loading) and logged

namespace BlackFlame::FileProbe
{
    namespace
    {
        constexpr std::string_view kMeshesRoot = "meshes\\";
        constexpr std::string_view kTexturesRoot = "textures\\";

        std::string BNegRecord(const Run& a_run) { return std::format("{}probe_bneg_{}.nif", VirtualFiles::kFolder, a_run.tag); }
        std::string BNegTexture(const Run& a_run)
        {
            return std::format("{}{}probe_bneg_{}.dds", kTexturesRoot, VirtualFiles::kFolder, a_run.tag);
        }

        void SkipPair(Run& a_run, std::string_view a_mesh, std::string_view a_texture, const std::string& a_why)
        {
            SetVerdict(a_run, a_mesh, "SKIPPED", a_why);
            SetVerdict(a_run, a_texture, "SKIPPED", a_why);
        }

        // a_dataPath written into Data and put into the index; the note says what happened
        bool WriteAndRegister(Run& a_run, std::string_view a_test, const std::string& a_dataPath, const Bytes& a_bytes)
        {
            const auto file = a_run.data / a_dataPath;
            std::string why;
            if (!WriteBytes(file, a_bytes, why)) {
                SetVerdict(a_run, a_test, "FAIL", std::format("'{}' could not be written: {}", PathText(file), why));
                return false;
            }
            std::string note;
            const bool registered = Engine::RegisterLoose(a_dataPath, note);
            Note(a_run, std::format("{}: written '{}' ({} bytes); RegisterLoose: {}", a_test, PathText(file), a_bytes.size(), note));
            if (!registered) {
                SetVerdict(a_run, a_test, "FAIL", "not registered: " + note);
            }
            return registered;
        }

        void AskMesh(Run& a_run, std::string_view a_test, const std::string& a_record, const std::string& a_context)
        {
            const auto result = Demand(a_record);
            SetVerdict(a_run, a_test, Passed(result) ? "PASS" : "FAIL", Text(a_record, result) + a_context);
        }

        void LateC(Run& a_run)
        {
            const auto mesh = MeshDataPath(a_run, "clate");
            if (WriteAndRegister(a_run, "C-mesh-late", mesh, *a_run.nif)) {
                AskMesh(a_run, "C-mesh-late", MeshRecordPath(a_run, "clate"), "");
            }
            const auto texture = TextureDataPath(a_run, "clate");
            if (WriteAndRegister(a_run, "C-texture-late", texture, *a_run.dds)) {
                Request(a_run, "C-texture-late", texture);
            }
        }

        // The in-flight load: a DDS of the source NIF not asked for before (the source DDS itself when it has no other)
        std::string StartInFlightLoad(Run& a_run)
        {
            const auto target = a_run.otherTextures.empty() ? a_run.sourceTexture : a_run.otherTextures.front();
            RE::NiPointer<RE::NiTexture> texture;
            (void)GuardedKeep("file probe in-flight GetTexture", [&]() { RE::BSShaderManager::GetTexture(target.c_str(), false, texture, false); });
            const auto* source = texture ? netimmerse_cast<RE::NiSourceTexture*>(texture.get()) : nullptr;
            const char* state = !texture ? "nothing came back" :
                                (!source ? "not an NiSourceTexture" :
                                           (source->rendererTexture ? "already loaded: NOT in flight" : "no GPU texture yet: in flight"));
            a_run.textures.push_back({ std::string(kInFlightTexture), target, std::move(texture) });  // kept: the load goes on
            return std::format("GetTexture(\"{}\", demand false) just before: {}", target, state);
        }

        void LoadingC(Run& a_run)
        {
            const auto inFlight = StartInFlightLoad(a_run);
            Note(a_run, "C-loading: " + inFlight);
            const auto mesh = MeshDataPath(a_run, "cload");
            if (WriteAndRegister(a_run, "C-mesh-loading", mesh, *a_run.nif)) {
                AskMesh(a_run, "C-mesh-loading", MeshRecordPath(a_run, "cload"), "; " + inFlight);
            }
            const auto texture = TextureDataPath(a_run, "cload");
            if (WriteAndRegister(a_run, "C-texture-loading", texture, *a_run.dds)) {
                Request(a_run, "C-texture-loading", texture);
            }
        }
    }

    void RegisterEarly(Run& a_run)
    {
        const auto priority = ChoosePriority(a_run, "B-mesh-neg");
        std::string note;
        a_run.bNegLocation = VirtualFiles::Register({}, priority, note);
        Note(a_run, "B-mesh-neg: " + note);
        if (!a_run.bNegLocation) {
            SkipPair(a_run, "B-mesh-neg", "B-texture-neg", note);
        }
        RegisterDNegEarly(a_run);
        a_run.earlyAt = Clock::now();
    }

    void LateCreate(Run& a_run)
    {
        if (a_run.bNegLocation) {
            const auto mesh = std::string(kMeshesRoot) + BNegRecord(a_run);
            const auto texture = BNegTexture(a_run);
            const bool added = VirtualFiles::AddFile(*a_run.bNegLocation, { mesh, a_run.nif }) &&
                               VirtualFiles::AddFile(*a_run.bNegLocation, { texture, a_run.dds });
            Note(a_run, std::format("B-mesh-neg: files added to the registered location: '{}', '{}' ({})", mesh, texture,
                added ? "ok" : "FAILED"));
            if (!added) {
                SkipPair(a_run, "B-mesh-neg", "B-texture-neg", "the files could not be added to the location");
                a_run.bNegLocation.reset();
            }
        }
        CreateDNegFiles(a_run);
        // C-late's first requests: no file yet
        const auto record = MeshRecordPath(a_run, "clate");
        Note(a_run, "C-mesh-late: first request, no file yet: " + Text(record, Demand(record)));
        Note(a_run, "C-texture-late: first request, no file yet: " + AskTextureText(TextureDataPath(a_run, "clate")));
    }

    void LateCheck(Run& a_run)
    {
        if (a_run.bNegLocation) {
            const auto record = BNegRecord(a_run);
            Note(a_run, "B-mesh-neg: " + StreamText(std::string(kMeshesRoot) + record));
            AskMesh(a_run, "B-mesh-neg", record, "");
            Note(a_run, "B-texture-neg: " + StreamText(BNegTexture(a_run)));
            Request(a_run, "B-texture-neg", BNegTexture(a_run));
            Note(a_run, "B-neg: " + VirtualFiles::CountersText());
        }
        CheckDNeg(a_run);
        if (const auto why = Engine::IndexUnavailable(); !why.empty()) {
            SkipPair(a_run, "C-mesh-late", "C-texture-late", why);
            SkipPair(a_run, "C-mesh-loading", "C-texture-loading", why);
            return;
        }
        LateC(a_run);
        LoadingC(a_run);
    }
}
