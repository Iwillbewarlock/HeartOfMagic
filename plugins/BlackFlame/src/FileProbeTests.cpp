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
        constexpr std::size_t kMaxSourceBytes = 8u * 1024u * 1024u;  // a larger NIF or DDS is not a quick probe file
        constexpr std::size_t kMaxProjectilesTried = 48;             // per pass (fire models first, then any)
        constexpr const char* kPaletteTexture = "textures\\heartofmagic\\blackflame\\gradblackflame.dds";  // ships with the addon
        constexpr std::size_t kDdsHeaderEnd = 128;  // "DDS " + the 124-byte header
        constexpr std::size_t kDdsHeightAt = 12;    // after the 4-byte magic: dwSize, dwFlags, dwHeight, dwWidth
        constexpr std::size_t kDdsWidthAt = 16;
        constexpr std::uint32_t kMaxMipSkip = 3;    // texture quality may load a DDS without its top mips
        constexpr std::array<std::string_view, 4> kWrittenLabels{ "a", "aneg", "c", "cneg" };
        constexpr const char* kMissingLabel = "missing";  // a texture path no file ever gets: what a miss returns
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

    void Request(Run& a_run, std::string_view a_test, const std::string& a_dataPath)
    {
        std::string form;
        auto texture = AskTexture(a_dataPath, form);
        Note(a_run, std::format("{}: GetTexture(\"{}\", demand true) asked: {} (judged in a moment)", a_test, form,
            texture ? "a texture came back" : "nothing came back"));
        a_run.textures.push_back({ std::string(a_test), form, std::move(texture) });
    }

    std::optional<std::pair<std::uint32_t, std::uint32_t>> DdsSize(const Bytes& a_bytes)
    {
        if (a_bytes.size() < kDdsHeaderEnd || std::memcmp(a_bytes.data(), "DDS ", 4) != 0) {
            return std::nullopt;
        }
        std::uint32_t height = 0;
        std::uint32_t width = 0;
        std::memcpy(&height, a_bytes.data() + kDdsHeightAt, sizeof(height));
        std::memcpy(&width, a_bytes.data() + kDdsWidthAt, sizeof(width));
        if (width == 0 || height == 0) {
            return std::nullopt;
        }
        return std::pair{ width, height };
    }

    std::string PathText(const std::filesystem::path& a_path)
    {
        try {
            return a_path.string();
        } catch (...) {
            return "(a path outside the ANSI code page)";
        }
    }

    std::filesystem::path DataFolder()
    {
        std::wstring buffer(MAX_PATH * 4, L'\0');
        const auto length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
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

    bool ReadSource(const std::string& a_dataPath, Bytes& a_bytes)
    {
        a_bytes.clear();
        const auto blob = NifDump::ReadModel(a_dataPath, &a_bytes);
        return blob.ok && !a_bytes.empty() && a_bytes.size() <= kMaxSourceBytes;
    }

    // A projectile's NIF that reads through the game's file system, and a DDS it names that reads too
    bool PickSources(Run& a_run)
    {
        auto* handler = RE::TESDataHandler::GetSingleton();
        if (!handler) {
            return false;
        }
        std::string fallbackModel;
        Bytes fallbackNif;
        for (const bool firePass : { true, false }) {
            std::size_t tried = 0;
            for (auto* projectile : handler->GetFormArray<RE::BGSProjectile>()) {
                const char* model = projectile ? static_cast<const RE::TESModel*>(projectile)->GetModel() : nullptr;
                if (!model || !*model) {
                    continue;
                }
                std::string lower(model);
                std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if ((lower.find("fire") != std::string::npos) != firePass || ++tried > kMaxProjectilesTried) {
                    continue;
                }
                const auto dataPath = NifDump::NormalizeModelPath(model);
                Bytes nif;
                if (!dataPath || !ReadSource(*dataPath, nif)) {
                    continue;
                }
                if (fallbackModel.empty()) {
                    fallbackModel = model;
                    fallbackNif = nif;
                }
                for (const auto& slot : NifDump::TexturePathsIn(nif)) {
                    const auto texture = NifDump::NormalizeTexturePath(slot);
                    Bytes dds;
                    if (!texture || !ReadSource(*texture, dds) || !DdsSize(dds)) {
                        continue;
                    }
                    a_run.sourceModel = model;
                    a_run.sourceTexture = *texture;
                    a_run.nif = std::make_shared<const Bytes>(std::move(nif));
                    a_run.dds = std::make_shared<const Bytes>(std::move(dds));
                    return true;
                }
            }
        }
        Bytes palette;
        if (fallbackModel.empty() || !ReadSource(kPaletteTexture, palette) || !DdsSize(palette)) {
            return false;
        }
        a_run.sourceModel = fallbackModel;
        a_run.sourceTexture = kPaletteTexture;
        a_run.nif = std::make_shared<const Bytes>(std::move(fallbackNif));
        a_run.dds = std::make_shared<const Bytes>(std::move(palette));
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

    void JudgeOne(Run& a_run, const PendingTexture& a_pending, const TextureLook& a_look)
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
        a_run.data = DataFolder();
        Note(a_run, std::format("Data folder (from the game's exe): '{}'", PathText(a_run.data)));
        RemoveOldFiles(a_run);
        bool picked = false;
        if (!GuardedKeep("file probe source pick", [&]() { picked = PickSources(a_run); }) || !picked) {
            Note(a_run, "no source NIF and DDS could be read through the game's file system - nothing to test");
            return false;
        }
        const auto size = DdsSize(*a_run.dds);
        a_run.ddsWidth = size->first;
        a_run.ddsHeight = size->second;
        Note(a_run, std::format("source NIF '{}' ({} bytes), source DDS '{}' ({} bytes, {}x{} in its header)", a_run.sourceModel,
            a_run.nif->size(), a_run.sourceTexture, a_run.dds->size(), a_run.ddsWidth, a_run.ddsHeight));
        // The environment check: a NIF the load order ships, Demanded the same way; a FAIL here means the way of asking (or
        // the game state) is at fault, not the route a test takes
        const auto control = Demand(a_run.sourceModel);
        SetVerdict(a_run, "control-mesh", Passed(control) ? "PASS" : "FAIL", Text(a_run.sourceModel, control));
        Note(a_run, "control: " + Engine::DescribeIds(*NifDump::NormalizeModelPath(a_run.sourceModel)) + "; " +
                        IndexText(*NifDump::NormalizeModelPath(a_run.sourceModel)));
        Request(a_run, "control", a_run.sourceTexture);
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
            std::string form;
            auto texture = AskTexture(TextureDataPath(a_run, label), form);
            Note(a_run, std::format("before its file exists: GetTexture(\"{}\"): {}", form, Text(Look(texture.get()))));
        }
        std::string form;
        auto missing = AskTexture(TextureDataPath(a_run, kMissingLabel), form);
        a_run.missingLook = Look(missing.get());
        Note(a_run, std::format("a path no file has: GetTexture(\"{}\"): {}", form, Text(a_run.missingLook)));
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
            }
        }
    }

    void TestA(Run& a_run)
    {
        for (const auto& [label, test] : { std::pair{ "a", "A-mesh" }, std::pair{ "aneg", "A-mesh-neg" } }) {
            const auto data = MeshDataPath(a_run, label);
            Note(a_run, std::format("{}: {}", test, StreamText(data)));
            Note(a_run, std::format("{}: {}", test, LooseText(data)));
            Note(a_run, std::format("{}: {}", test, IndexText(data)));
            const auto record = MeshRecordPath(a_run, label);
            const auto result = Demand(record);
            SetVerdict(a_run, test, Passed(result) ? "PASS" : "FAIL", Text(record, result));
        }
        for (const auto& [label, test] : { std::pair{ "a", "A-texture" }, std::pair{ "aneg", "A-texture-neg" } }) {
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
        // No source says how RegisterLocation orders by priority; our folder is served by no other location, so the order
        // decides only who is asked first. The loose-file location's own priority is taken when it is in the list
        constexpr std::uint32_t kPriorityWithoutLoose = 0;
        const auto loose = Engine::LooseFilePriority();
        const auto priority = loose.value_or(kPriorityWithoutLoose);
        Note(a_run, std::format("B: priority {} ({})", priority, loose ? "the LooseFileLocation's" : "no LooseFileLocation in the list"));
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
            std::string note;
            const auto mesh = MeshDataPath(a_run, label);
            const bool meshRegistered = Engine::RegisterLoose(mesh, note);
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
            const bool textureRegistered = Engine::RegisterLoose(texture, note);
            Note(a_run, std::format("{}: RegisterLoose '{}': {}", textureTest, texture, note));
            if (textureRegistered) {
                Request(a_run, textureTest, texture);
            } else {
                SetVerdict(a_run, textureTest, "FAIL", "not registered: " + note);
            }
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
        // The control first: its size is what the others are measured against
        (void)std::ranges::stable_partition(a_run.textures, [](const PendingTexture& a_pending) { return a_pending.test == "control"; });
        for (const auto& pending : a_run.textures) {
            const auto look = Look(pending.texture.get());
            if (pending.test == "control") {
                a_run.sourceLook = look;
                Note(a_run, std::format("control: GetTexture(\"{}\"): {}", pending.path, Text(look)));
                continue;
            }
            JudgeOne(a_run, pending, look);
        }
        a_run.textures.clear();  // our references given back here, on the main thread
        Note(a_run, "B: " + VirtualFiles::CountersText());
    }
}
