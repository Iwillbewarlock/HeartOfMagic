#include "FileProbeRun.h"

#include "FileProbeEngine.h"
#include "FileProbeLocation.h"
#include "NifDump.h"
#include "SehGuard.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <fstream>
#include <optional>
#include <system_error>
#include <tuple>
#include <utility>

#include <Windows.h>

// The tests of the dev file visibility probe (FileVisibilityProbe.h). Paths: the "record" form is what a record stores and
// what BSModelDB::Demand is given (no meshes\ in front, as CopyModelProbe asks); the "data" form is Data-relative
// (meshes\... / textures\...), what a BSResource stream, PathToID and the index take.

namespace BlackFlame::FileProbe
{
    namespace
    {
        constexpr std::string_view kProbeFolder = "heartofmagic\\blackflame\\fileprobe\\";
        constexpr std::string_view kFilePrefix = "probe_";
        constexpr std::string_view kMeshesRoot = "meshes\\";
        constexpr std::string_view kTexturesRoot = "textures\\";
        constexpr std::uint32_t kMaxMipSkip = 3;    // texture quality may load a DDS without its top mips
        constexpr std::array<std::string_view, 4> kWrittenLabels{ "a", "aneg", "c", "cneg" };
        constexpr const char* kMissingLabel = "missing";  // a texture path no file ever gets: what a miss returns
        constexpr std::size_t kExePathChars = 32768;       // the longest Windows path, in UTF-16 units
        // FQ5: a vanilla file (Skyrim - Meshes0.bsa) as the last index check when the source NIF and DDS are not found
        constexpr std::string_view kVanillaModel = "meshes\\actors\\character\\character assets\\skeleton.nif";
    }

    // Helpers below are shared with FileProbeTestD.cpp (declared in FileProbeRun.h) or used only here

    DemandResult Demand(const std::string& a_recordPath)
    {
        DemandResult result;
        RE::NiPointer<RE::NiNode> out;
        auto code = RE::BSResource::ErrorCode::kNone;
        const auto begin = Clock::now();
        result.fault = !GuardedKeep("file probe Demand", [&]() { code = RE::BSModelDB::Demand(a_recordPath.c_str(), out, {}); });
        result.ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin).count();
        result.code = static_cast<int>(code);
        result.node = out != nullptr;
        out.reset();
        return result;
    }

    bool Passed(const DemandResult& a_result) { return !a_result.fault && a_result.code == 0 && a_result.node; }

    std::string Text(const std::string& a_recordPath, const DemandResult& a_result)
    {
        return std::format("BSModelDB::Demand(\"{}\"): {}error code {}, node {}, {} ms", a_recordPath, a_result.fault ? "FAULT; " : "",
            a_result.code, a_result.node ? "yes" : "NULL", a_result.ms);
    }

    std::string StreamText(const std::string& a_dataPath)
    {
        bool good = false;
        int error = 0;
        std::uint64_t size = 0;
        const bool ok = GuardedKeep("file probe stream open", [&]() {
            RE::BSResourceNiBinaryStream stream(a_dataPath.c_str());
            good = stream.good();
            error = static_cast<int>(stream.lastError);
            size = good && stream.stream ? stream.stream->totalSize : 0;
        });
        return std::format("plain stream open (BSResourceNiBinaryStream) of '{}': {}", a_dataPath,
            !ok ? "FAULT" : (good ? std::format("opens, {} bytes", size) : std::format("does not open (error {})", error)));
    }

    std::string LooseText(const std::string& a_dataPath)
    {
        if (const auto why = Engine::GlobalsUnavailable(); !why.empty()) {
            return "GlobalLocations open not run: " + why;
        }
        Engine::StreamSlot slot;
        const auto code = Engine::OpenLoose(a_dataPath, slot);
        return std::format("GlobalLocations::DoCreateStream(\"data\\{}\"): error {}, stream {}", a_dataPath, static_cast<int>(code),
            slot.Get() ? "yes" : "none");
    }

    std::string IndexText(const std::string& a_dataPath)
    {
        if (const auto why = Engine::IndexUnavailable(); !why.empty()) {
            return std::format("index lookup of '{}' not run: {}", a_dataPath, why);
        }
        return std::format("index lookup of '{}': {}", a_dataPath, Engine::Describe(Engine::Find(a_dataPath)));
    }

    TextureLook Look(RE::NiTexture* a_texture)
    {
        TextureLook look;
        look.returned = a_texture != nullptr;
        auto* source = a_texture ? netimmerse_cast<RE::NiSourceTexture*>(a_texture) : nullptr;
        look.source = source != nullptr;
        if (!source) {
            return look;
        }
        look.name = source->name.c_str() ? source->name.c_str() : "";
        look.renderer = source->rendererTexture != nullptr;
        if (look.renderer) {
            std::uint32_t mips = 0;
            std::uint32_t layers = 0;
            look.fault = !GuardedKeep("file probe texture size",
                [&]() { source->GetTextureDimensionsAndMipInfo(&look.width, &look.height, &mips, &layers); });
        }
        return look;
    }

    std::string Text(const TextureLook& a_look)
    {
        if (!a_look.returned) {
            return "no texture";
        }
        if (!a_look.source) {
            return "a texture that is not an NiSourceTexture";
        }
        return std::format("NiSourceTexture '{}', GPU texture {}, {}x{}{}", a_look.name, a_look.renderer ? "yes" : "NOT made",
            a_look.width, a_look.height, a_look.fault ? " (size call FAULTED)" : "");
    }

    // BSShaderManager::GetTexture (98986 / 105640; Palette.cpp loads the addon's palette through it in game). The
    // "textures\" form first, then without it, as Palette.cpp does; a_form says which one gave a texture
    RE::NiPointer<RE::NiTexture> AskTexture(const std::string& a_dataPath, std::string& a_form)
    {
        RE::NiPointer<RE::NiTexture> texture;
        for (const auto& form : { a_dataPath, a_dataPath.substr(kTexturesRoot.size()) }) {
            (void)GuardedKeep("file probe GetTexture", [&]() { RE::BSShaderManager::GetTexture(form.c_str(), true, texture, false); });
            a_form = form;
            if (texture) {
                break;
            }
        }
        return texture;
    }

    std::string AskTextureText(const std::string& a_dataPath)
    {
        std::string form;
        auto texture = AskTexture(a_dataPath, form);
        return std::format("GetTexture(\"{}\"): {}", form, Text(Look(texture.get())));
    }

    void Request(Run& a_run, std::string_view a_test, const std::string& a_dataPath)
    {
        std::string form;
        auto texture = AskTexture(a_dataPath, form);
        Note(a_run, std::format("{}: GetTexture(\"{}\", demand true) asked: {} (judged in a moment)", a_test, form,
            texture ? "a texture came back" : "nothing came back"));
        a_run.textures.push_back({ std::string(a_test), form, std::move(texture) });
    }

    std::string PathText(const std::filesystem::path& a_path)
    {
        try {
            return a_path.string();
        } catch (...) {
            return "(a path outside the ANSI code page)";
        }
    }

    // Nothing when the exe path cannot be read whole (0, or a cut path: the length fills the buffer)
    std::optional<std::filesystem::path> DataFolder()
    {
        std::wstring buffer(kExePathChars, L'\0');
        const auto length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) {
            return std::nullopt;
        }
        buffer.resize(length);
        return std::filesystem::path(buffer).parent_path() / L"Data";
    }

    void RemoveOldFiles(Run& a_run)
    {
        for (const auto root : { kMeshesRoot, kTexturesRoot }) {
            RemoveProbeFiles(a_run, a_run.data / std::string(root) / std::string(kProbeFolder));
        }
    }

    void RemoveProbeFiles(Run& a_run, const std::filesystem::path& a_folder)
    {
        const auto& folder = a_folder;
        std::error_code ec;
        if (!std::filesystem::is_directory(folder, ec)) {
            return;
        }
        int removed = 0;
        int failed = 0;
        for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
            std::error_code fileError;
            const auto name = entry.path().filename().wstring();
            const auto extension = entry.path().extension().wstring();
            if (!entry.is_regular_file(fileError) || !name.starts_with(L"probe_") ||
                (_wcsicmp(extension.c_str(), L".nif") != 0 && _wcsicmp(extension.c_str(), L".dds") != 0)) {
                continue;
            }
            if (std::filesystem::remove(entry.path(), fileError)) {
                ++removed;
            } else {
                ++failed;
            }
        }
        Note(a_run, std::format("old probe files in '{}': {} removed, {} could not be removed{}", PathText(folder), removed,
            failed, ec ? std::format(" (listing: {})", ec.message()) : ""));
    }

    bool WriteBytes(const std::filesystem::path& a_file, const Bytes& a_bytes, std::string& a_why)
    {
        std::error_code ec;
        std::filesystem::create_directories(a_file.parent_path(), ec);
        std::ofstream out(a_file, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(a_bytes.data()), static_cast<std::streamsize>(a_bytes.size()));
        out.close();
        if (!out) {
            a_why = ec ? ec.message() : "the write failed";
            return false;
        }
        const auto size = std::filesystem::file_size(a_file, ec);
        if (ec || size != a_bytes.size()) {
            a_why = ec ? ec.message() : std::format("{} bytes on disk, {} written", size, a_bytes.size());
            return false;
        }
        return true;
    }

    bool SizeMatches(const Run& a_run, const TextureLook& a_look, std::string& a_expected)
    {
        if (a_run.sourceLook.renderer && a_run.sourceLook.width > 0) {
            a_expected = std::format("{}x{} (the source DDS loaded at its own path)", a_run.sourceLook.width, a_run.sourceLook.height);
            return a_look.width == a_run.sourceLook.width && a_look.height == a_run.sourceLook.height;
        }
        a_expected = std::format("{}x{} from the DDS header, or that halved up to {} times", a_run.ddsWidth, a_run.ddsHeight, kMaxMipSkip);
        for (std::uint32_t skip = 0; skip <= kMaxMipSkip; ++skip) {
            if (a_look.width == (std::max)(1u, a_run.ddsWidth >> skip) && a_look.height == (std::max)(1u, a_run.ddsHeight >> skip)) {
                return true;
            }
        }
        return false;
    }

    void JudgeOneKnown(Run& a_run, const PendingTexture& a_pending, const TextureLook& a_look);

    // FQ6: a missing path that came back as an NiSourceTexture still without its GPU texture after the wait leaves what a
    // miss gives unknown: every texture verdict is then INCONCLUSIVE, with the verdict it would have had in its reason
    void JudgeOne(Run& a_run, const PendingTexture& a_pending, const TextureLook& a_look)
    {
        if (a_run.missingLook.source && !a_run.missingLook.renderer) {
            JudgeOneKnown(a_run, a_pending, a_look);
            auto& verdict = a_run.results[a_pending.test];
            SetVerdict(a_run, a_pending.test, "INCONCLUSIVE", std::format("would be {} ({}), but a missing path's texture got no GPU "
                                                                          "texture within the wait: what a miss gives is unknown",
                                                                  verdict.word, verdict.why));
            return;
        }
        JudgeOneKnown(a_run, a_pending, a_look);
    }

    void JudgeOneKnown(Run& a_run, const PendingTexture& a_pending, const TextureLook& a_look)
    {
        const auto lead = std::format("GetTexture(\"{}\"): {}", a_pending.path, Text(a_look));
        if (!a_look.returned || !a_look.source || !a_look.renderer || a_look.fault) {
            SetVerdict(a_run, a_pending.test, "FAIL", lead);
            return;
        }
        std::string expected;
        if (!SizeMatches(a_run, a_look, expected)) {
            SetVerdict(a_run, a_pending.test, "FAIL", std::format("{}; expected {}; a missing path gives: {}", lead, expected,
                Text(a_run.missingLook)));
            return;
        }
        if (a_run.missingLook.renderer && a_run.missingLook.width == a_look.width && a_run.missingLook.height == a_look.height) {
            SetVerdict(a_run, a_pending.test, "INCONCLUSIVE", lead + "; a missing path gives a texture of the same size");
            return;
        }
        SetVerdict(a_run, a_pending.test, "PASS", lead + "; size as expected: " + expected);
    }

    std::uint32_t ChoosePriority(Run& a_run, std::string_view a_who)
    {
        // No source says how RegisterLocation orders by priority; our folders are served by no other location, so the order
        // decides only who is asked first. The loose-file location's own priority is taken when it is in the list
        constexpr std::uint32_t kPriorityWithoutLoose = 0;
        const auto loose = Engine::LooseFilePriority();
        const auto priority = loose.value_or(kPriorityWithoutLoose);
        Note(a_run, std::format("{}: priority {} ({})", a_who, priority, loose ? "the LooseFileLocation's" : "no LooseFileLocation in the list"));
        return priority;
    }

    std::string MeshRecordPath(const Run& a_run, std::string_view a_label)
    {
        return std::format("{}{}{}_{}.nif", kProbeFolder, kFilePrefix, a_label, a_run.tag);
    }

    std::string MeshDataPath(const Run& a_run, std::string_view a_label)
    {
        return std::string(kMeshesRoot) + MeshRecordPath(a_run, a_label);
    }

    std::string TextureDataPath(const Run& a_run, std::string_view a_label)
    {
        return std::format("{}{}{}{}_{}.dds", kTexturesRoot, kProbeFolder, kFilePrefix, a_label, a_run.tag);
    }

    bool Prepare(Run& a_run)
    {
        std::vector<std::string> lines;
        Engine::Init(lines);
        for (auto& line : lines) {
            Note(a_run, std::move(line));
        }
        const auto data = DataFolder();
        if (!data) {
            Note(a_run, "the game's exe path could not be read (GetModuleFileNameW gave 0 or a cut path) - no Data folder, nothing to test");
            return false;
        }
        a_run.data = *data;
        Note(a_run, std::format("Data folder (from the game's exe): '{}'", PathText(a_run.data)));
        RemoveOldFiles(a_run);
        bool picked = false;
        if (!GuardedKeep("file probe source pick", [&]() { picked = PickSources(a_run); }) || !picked) {
            Note(a_run, "no source NIF and DDS could be read through the game's file system - nothing to test");
            return false;
        }
        (void)GuardedKeep("file probe in-flight target pick", [&]() { PickInFlightTarget(a_run); });
        const auto size = DdsSize(*a_run.dds);
        a_run.ddsWidth = size->first;
        a_run.ddsHeight = size->second;
        Note(a_run, std::format("source NIF '{}' ({} bytes), source DDS '{}' ({} bytes, {}x{} in its header); C-loading's target '{}'",
            a_run.sourceModel, a_run.nif->size(), a_run.sourceTexture, a_run.dds->size(), a_run.ddsWidth, a_run.ddsHeight,
            a_run.inFlightTarget.empty() ? "(none found)" : a_run.inFlightTarget));
        // The environment check: a NIF the load order ships, Demanded the same way; a FAIL here means the way of asking (or
        // the game state) is at fault, not the route a test takes
        const auto control = Demand(a_run.sourceModel);
        SetVerdict(a_run, "control-mesh", Passed(control) ? "PASS" : "FAIL", Text(a_run.sourceModel, control));
        // FP2: the index layout is used only after a lookup of this shipped file found it
        lines.clear();
        Engine::ConfirmIndex({ *NifDump::NormalizeModelPath(a_run.sourceModel), a_run.sourceTexture, std::string(kVanillaModel) }, lines);
        for (auto& line : lines) {
            Note(a_run, std::move(line));
        }
        Request(a_run, kControlTexture, a_run.sourceTexture);
        a_run.ready = true;
        return true;
    }

    void PreRequest(Run& a_run)
    {
        for (const auto label : { "aneg", "cneg" }) {
            const auto path = MeshRecordPath(a_run, label);
            Note(a_run, std::format("before its file exists: {}", Text(path, Demand(path))));
        }
        for (const auto label : { "aneg", "cneg" }) {
            Note(a_run, "before its file exists: " + AskTextureText(TextureDataPath(a_run, label)));
        }
        // FP1: judged with the others (after the same wait), first, so the INCONCLUSIVE check sees its GPU texture
        Request(a_run, kMissingTexture, TextureDataPath(a_run, kMissingLabel));
    }

    void WriteFiles(Run& a_run)
    {
        for (const auto label : kWrittenLabels) {
            for (const auto& [dataPath, bytes] : { std::pair{ MeshDataPath(a_run, label), a_run.nif },
                     std::pair{ TextureDataPath(a_run, label), a_run.dds } }) {
                const auto file = a_run.data / dataPath;
                std::string why;
                const bool ok = WriteBytes(file, *bytes, why);
                Note(a_run, std::format("written '{}' ({} bytes): {}", PathText(file), bytes->size(), ok ? "ok" : "FAILED: " + why));
                if (!ok) {
                    a_run.writeFailed.emplace(label);
                }
            }
        }
    }

    // FP8: a test whose file could not be written is SKIPPED, not FAIL
    bool SkippedForWrite(Run& a_run, std::string_view a_label, std::string_view a_test)
    {
        if (!a_run.writeFailed.contains(a_label)) {
            return false;
        }
        SetVerdict(a_run, a_test, "SKIPPED", std::format("the '{}' files could not be written (see 'written' above)", a_label));
        return true;
    }

    void TestA(Run& a_run)
    {
        for (const auto& [label, test] : { std::pair{ "a", "A-mesh" }, std::pair{ "aneg", "A-mesh-neg" } }) {
            if (SkippedForWrite(a_run, label, test)) {
                continue;
            }
            const auto data = MeshDataPath(a_run, label);
            Note(a_run, std::format("{}: {}", test, StreamText(data)));
            Note(a_run, std::format("{}: {}", test, LooseText(data)));
            Note(a_run, std::format("{}: {}", test, IndexText(data)));
            const auto record = MeshRecordPath(a_run, label);
            const auto result = Demand(record);
            SetVerdict(a_run, test, Passed(result) ? "PASS" : "FAIL", Text(record, result));
        }
        for (const auto& [label, test] : { std::pair{ "a", "A-texture" }, std::pair{ "aneg", "A-texture-neg" } }) {
            if (SkippedForWrite(a_run, label, test)) {
                continue;
            }
            const auto data = TextureDataPath(a_run, label);
            Note(a_run, std::format("{}: {}", test, StreamText(data)));
            Note(a_run, std::format("{}: {}", test, IndexText(data)));
            Request(a_run, test, data);
        }
    }

    void TestB(Run& a_run)
    {
        const auto record = std::format("{}{}b_{}.nif", VirtualFiles::kFolder, kFilePrefix, a_run.tag);
        const auto meshKey = std::string(kMeshesRoot) + record;
        const auto textureKey = std::format("{}{}{}b_{}.dds", kTexturesRoot, VirtualFiles::kFolder, kFilePrefix, a_run.tag);
        for (const auto& line : Engine::DescribeLocations()) {
            Note(a_run, "B: before: " + line);
        }
        const auto priority = ChoosePriority(a_run, "B");
        std::string note;
        if (!VirtualFiles::Register({ { meshKey, a_run.nif }, { textureKey, a_run.dds } }, priority, note)) {
            Note(a_run, "B: " + note);
            SetVerdict(a_run, "B-mesh", "SKIPPED", note);
            SetVerdict(a_run, "B-texture", "SKIPPED", note);
            return;
        }
        Note(a_run, "B: " + note);
        for (const auto& line : Engine::DescribeLocations()) {
            Note(a_run, "B: after: " + line);
        }
        Note(a_run, "B-mesh: " + StreamText(meshKey));
        Note(a_run, "B-mesh: " + IndexText(meshKey));
        const auto result = Demand(record);
        SetVerdict(a_run, "B-mesh", Passed(result) ? "PASS" : "FAIL", Text(record, result));
        Note(a_run, "B-texture: " + StreamText(textureKey));
        Request(a_run, "B-texture", textureKey);
        Note(a_run, "B: " + VirtualFiles::CountersText());
    }

    void TestC(Run& a_run)
    {
        if (const auto why = Engine::IndexUnavailable(); !why.empty()) {
            for (const auto test : { "C-mesh", "C-texture", "C-mesh-neg", "C-texture-neg" }) {
                SetVerdict(a_run, test, "SKIPPED", why);
            }
            return;
        }
        for (const auto& [label, meshTest, textureTest] :
            { std::tuple{ "c", "C-mesh", "C-texture" }, std::tuple{ "cneg", "C-mesh-neg", "C-texture-neg" } }) {
            if (SkippedForWrite(a_run, label, meshTest) | SkippedForWrite(a_run, label, textureTest)) {
                continue;
            }
            std::string note;
            const auto mesh = MeshDataPath(a_run, label);
            const bool meshRegistered = Engine::RegisterLoose(mesh, a_run.nif->size(), note);
            Note(a_run, std::format("{}: RegisterLoose '{}': {}", meshTest, mesh, note));
            if (meshRegistered) {
                Note(a_run, std::format("{}: {}", meshTest, StreamText(mesh)));
                const auto record = MeshRecordPath(a_run, label);
                const auto result = Demand(record);
                SetVerdict(a_run, meshTest, Passed(result) ? "PASS" : "FAIL", Text(record, result));
            } else {
                SetVerdict(a_run, meshTest, "FAIL", "not registered: " + note);
            }
            const auto texture = TextureDataPath(a_run, label);
            const bool textureRegistered = Engine::RegisterLoose(texture, a_run.dds->size(), note);
            Note(a_run, std::format("{}: RegisterLoose '{}': {}", textureTest, texture, note));
            if (textureRegistered) {
                Request(a_run, textureTest, texture);
            } else {
                SetVerdict(a_run, textureTest, "FAIL", "not registered: " + note);
            }
        }
    }

    void ApplyLooseRouteRule(Run& a_run)
    {
        // FQ2: RegisterLoose is shown to matter only when the same kind of file, written the same way, did NOT load without it
        constexpr std::array<std::pair<std::string_view, std::string_view>, 8> kTwins{ { { "C-mesh", "A-mesh" },
            { "C-texture", "A-texture" }, { "C-mesh-neg", "A-mesh-neg" }, { "C-texture-neg", "A-texture-neg" },
            { "C-mesh-late", "A-mesh-neg" }, { "C-texture-late", "A-texture-neg" }, { "C-mesh-loading", "A-mesh" },
            { "C-texture-loading", "A-texture" } } };
        for (const auto& [cTest, aTest] : kTwins) {
            const auto c = a_run.results.find(cTest);
            const auto a = a_run.results.find(aTest);
            if (c == a_run.results.end() || a == a_run.results.end() || c->second.word != "PASS" || a->second.word != "PASS") {
                continue;
            }
            SetVerdict(a_run, cTest, "INCONCLUSIVE (loose route open)",
                std::format("{} passed without RegisterLoose too, so this PASS does not show RegisterLoose mattered ({})", aTest,
                    c->second.why));
        }
    }

    bool TexturesReady(const Run& a_run)
    {
        return std::ranges::all_of(a_run.textures, [](const PendingTexture& a_pending) {
            const auto* source = a_pending.texture ? netimmerse_cast<RE::NiSourceTexture*>(a_pending.texture.get()) : nullptr;
            return !a_pending.texture || (source && source->rendererTexture != nullptr);  // nothing came back: nothing to wait for
        });
    }

    void JudgeTextures(Run& a_run)
    {
        // First the control, the miss and the in-flight load: the size the others are measured against, what a miss gives
        // (FP1), and whether C-loading's load finished (FQ1)
        const auto special = [](const PendingTexture& a_pending) {
            return a_pending.test == kControlTexture || a_pending.test == kMissingTexture || a_pending.test == kInFlightTexture;
        };
        bool inFlightFinished = false;
        for (const auto& pending : a_run.textures) {
            if (!special(pending)) {
                continue;
            }
            const auto look = Look(pending.texture.get());
            if (pending.test == kInFlightTexture) {
                inFlightFinished = look.renderer;
                Note(a_run, std::format("C-loading's in-flight load at the judging: GetTexture(\"{}\", demand false): {}", pending.path,
                    Text(look)));
                continue;
            }
            (pending.test == kControlTexture ? a_run.sourceLook : a_run.missingLook) = look;
            Note(a_run, std::format("{}: GetTexture(\"{}\"): {}", pending.test, pending.path, Text(look)));
        }
        // FP6 / FQ1: C-loading counts only for a load that was outstanding when the files were registered and has finished since
        const std::string loadingDoubt = !a_run.loadInFlight ? std::format("no load was in flight ({})", a_run.inFlightState) :
                                         (!inFlightFinished ? "the in-flight load never got its GPU texture (it may have failed)" : "");
        if (a_run.loadingMesh) {
            const auto& [record, result] = *a_run.loadingMesh;
            if (!loadingDoubt.empty()) {
                SetVerdict(a_run, "C-mesh-loading", "INCONCLUSIVE", std::format("{}; {}", loadingDoubt, Text(record, result)));
            } else {
                SetVerdict(a_run, "C-mesh-loading", Passed(result) ? "PASS" : "FAIL", Text(record, result) + "; " + a_run.inFlightState);
            }
        }
        for (const auto& pending : a_run.textures) {
            if (special(pending)) {
                continue;
            }
            const auto look = Look(pending.texture.get());
            if (pending.test == "C-texture-loading" && !loadingDoubt.empty()) {
                SetVerdict(a_run, pending.test, "INCONCLUSIVE", std::format("{}; GetTexture(\"{}\"): {}", loadingDoubt, pending.path, Text(look)));
                continue;
            }
            JudgeOne(a_run, pending, look);
        }
        a_run.textures.clear();  // our references given back here, on the main thread
        Note(a_run, "B: " + VirtualFiles::CountersText());
    }
}
