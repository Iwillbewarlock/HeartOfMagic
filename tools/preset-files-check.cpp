// preset-files-check - offline check of a preset's rename
// (plugins/spelllearning/include/uimanager/PresetFiles.h, UIManager::OnRenamePreset).
//
//   preset-files-check --self-test     writes in a temp folder, no game needed
//
// PresetFiles::Rename finds the old preset's files first, writes the new file, and only
// then deletes the old files, none that is the new file itself. Checked here: a plain
// rename; a write that fails (the new name taken by a folder) deletes nothing; two names
// that are one file (sanitized alike) and a case-only change keep that file; an older
// build's file under a garbled name for a non-ASCII preset is deleted even when the new
// name lands on the preset's own file name; an ASCII name's add-on file that only carries
// the name inside is left alone; a new name whose file is another preset's ("Fire." onto
// "Fire", "a/b" onto "a_b", an add-on file) is refused unless the player confirmed it.
// Delete through Find as before.
//
// Exit 0 when every case passes, 1 on a failure, 2 on a bad argument.

#include "Common.h"

#include "uimanager/PresetFiles.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
    namespace fs = std::filesystem;

    constexpr int kExitOk = 0;
    constexpr int kExitFailed = 1;
    constexpr int kExitUsage = 2;

    // "한글" in UTF-8 (escaped: the source's encoding does not matter)
    const std::string kKorean = "\xED\x95\x9C\xEA\xB8\x80";

    int g_failed = 0;

    void Check(bool ok, const std::string& what)
    {
        if (!ok) ++g_failed;
        std::printf("    [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    }

    void Write(const fs::path& path, std::string_view text)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    std::optional<std::string> Read(const fs::path& path)
    {
        return FileUtils::ReadAll(path);
    }

    bool Exists(const fs::path& path)
    {
        std::error_code ec;
        return fs::exists(path, ec);
    }

    std::string Preset(const std::string& name) { return "{\"name\": \"" + name + "\"}"; }

    /// A fresh folder of its own under the system temp folder, removed when done
    struct Folder
    {
        fs::path path;
        explicit Folder(const char* tag)
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path = fs::temp_directory_path() / (std::string("preset-files-check-") + tag + "-" + std::to_string(stamp));
            fs::create_directories(path);
        }
        ~Folder()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
        fs::path File(const std::string& safeName) const { return PresetFiles::FilePath(path, safeName); }
    };

    void PlainCase()
    {
        std::printf("  a plain rename\n");
        Folder dir("plain");
        Write(dir.File("Ice"), Preset("Ice"));
        const auto r = PresetFiles::Rename(dir.path, "Ice", "Ice", "Frost", Preset("Frost"), false);
        Check(r.written && Read(dir.File("Frost")) == Preset("Frost"), "the new file is written");
        Check(!Exists(dir.File("Ice")) && r.removed.size() == 1 && r.kept.empty(), "the old file is deleted");
    }

    void FailedWriteCase()
    {
        std::printf("  a write that fails deletes nothing\n");
        Folder dir("failed");
        Write(dir.File("Ice"), Preset("Ice"));
        fs::create_directories(dir.File("Frost"));   // the new name is taken by a folder: no file can go there
        const auto r = PresetFiles::Rename(dir.path, "Ice", "Ice", "Frost", Preset("Frost"), true);   // past the taken-name check, to the write
        Check(!r.written && !r.refused, "not written (the write failed, not refused)");
        Check(Read(dir.File("Ice")) == Preset("Ice") && r.removed.empty(), "the old file is kept as it was");
    }

    void SameFileCase()
    {
        std::printf("  two names that are one file\n");
        Folder dir("same");
        Write(dir.File("Fire"), Preset("Fire"));
        // "Fire." sanitizes to "Fire" (trailing dots go): the caller passes that safe name
        const auto r = PresetFiles::Rename(dir.path, "Fire", "Fire", "Fire", Preset("Fire."), false);
        Check(r.written && Read(dir.File("Fire")) == Preset("Fire."), "the file holds the new preset");
        Check(r.removed.empty() && r.kept.size() == 1, "and is not deleted after the write");
    }

    void CaseOnlyCase()
    {
        std::printf("  a case-only rename\n");
        Folder dir("case");
        Write(dir.File("Fire"), Preset("Fire"));
        const auto r = PresetFiles::Rename(dir.path, "Fire", "Fire", "fire", Preset("fire"), false);
        Check(r.written && Read(dir.File("fire")) == Preset("fire"), "the new preset is on disk");
        // Windows file names ignore case: one file, kept. A case-sensitive file system has
        // two names and deletes the old one
        std::error_code ec;
        const bool oneFile = fs::equivalent(dir.File("Fire"), dir.File("fire"), ec) && !ec;
        Check(oneFile ? r.kept.size() == 1 && r.removed.empty() : r.removed.size() == 1 && !Exists(dir.File("Fire")),
              oneFile ? "one file on this system: kept, not deleted" : "two files on this system: the old one deleted");
    }

    void LegacyCase()
    {
        std::printf("  an older build's file for a non-ASCII preset\n");
        Folder dir("legacy");
        const auto garbled = dir.path / "_garbled_.json";   // written through the ANSI code page by an older build
        Write(garbled, Preset(kKorean));
        // "한글." sanitizes to "한글": the new file lands on the old preset's own file name,
        // which does not exist - the old one is the garbled file
        const auto r = PresetFiles::Rename(dir.path, kKorean, kKorean, kKorean, Preset(kKorean + "."), false);
        Check(r.written && Read(dir.File(kKorean)) == Preset(kKorean + "."), "the new file is written");
        Check(!Exists(garbled) && r.removed.size() == 1, "the garbled file is deleted (it would come back as a second preset)");
    }

    void AsciiAddonCase()
    {
        std::printf("  an ASCII name's add-on file is left alone\n");
        Folder dir("addon");
        const auto addon = dir.path / "AddonFire.json";
        Write(addon, Preset("Fire"));
        const auto r = PresetFiles::Rename(dir.path, "Fire", "Fire", "Blaze", Preset("Blaze"), false);
        Check(r.written && Exists(dir.File("Blaze")), "the new file is written");
        Check(Exists(addon) && r.removed.empty(), "AddonFire.json (\"Fire\" inside) is not deleted");
        const auto d = PresetFiles::Delete(dir.path, "Fire", "Fire");
        Check(!d.direct && d.legacy.empty() && Exists(addon), "Delete leaves it too");
    }

    void DeleteCase()
    {
        std::printf("  Delete through Find\n");
        Folder dir("delete");
        Write(dir.File("Ice"), Preset("Ice"));
        const auto d = PresetFiles::Delete(dir.path, "Ice", "Ice");
        Check(d.direct && !Exists(dir.File("Ice")), "its own file deleted");
        const auto garbled = dir.path / "_garbled_.json";
        Write(garbled, Preset(kKorean));
        const auto g = PresetFiles::Delete(dir.path, kKorean, kKorean);
        Check(!g.direct && g.legacy.size() == 1 && !Exists(garbled), "a non-ASCII preset's garbled file deleted");
    }

    void TakenCase()
    {
        std::printf("  a name whose file is another preset's\n");
        {
            // "Ice" renamed to "Fire.": sanitized to "Fire", the file of the preset "Fire"
            Folder dir("taken");
            Write(dir.File("Fire"), Preset("Fire"));
            Write(dir.File("Ice"), Preset("Ice"));
            const auto r = PresetFiles::Rename(dir.path, "Ice", "Ice", "Fire", Preset("Fire."), false);
            Check(r.refused && !r.written, "\"Ice\" -> \"Fire.\" without a confirm: refused, nothing written");
            Check(Read(dir.File("Fire")) == Preset("Fire") && Read(dir.File("Ice")) == Preset("Ice"),
                  "both presets kept as they were");
            const auto o = PresetFiles::Rename(dir.path, "Ice", "Ice", "Fire", Preset("Fire."), true);
            Check(o.written && !o.refused && Read(dir.File("Fire")) == Preset("Fire.") && !Exists(dir.File("Ice")),
                  "with the player's confirm: Fire's file replaced, Ice's deleted");
        }
        {
            // "a/b" sanitizes to "a_b", the preset "a_b"'s file
            Folder dir("slash");
            Write(dir.File("a_b"), Preset("a_b"));
            Write(dir.File("x"), Preset("x"));
            const auto r = PresetFiles::Rename(dir.path, "x", "x", "a_b", Preset("a/b"), false);
            Check(r.refused && Read(dir.File("a_b")) == Preset("a_b") && Exists(dir.File("x")),
                  "\"x\" -> \"a/b\" onto \"a_b\": refused, both kept");
        }
        {
            // An add-on's file the panel lists under another name (AddonFire.json, "Fire" inside)
            Folder dir("addonTaken");
            Write(dir.path / "AddonFire.json", Preset("Fire"));
            Write(dir.File("Ice"), Preset("Ice"));
            const auto r = PresetFiles::Rename(dir.path, "Ice", "Ice", "AddonFire", Preset("AddonFire"), false);
            Check(r.refused && Read(dir.path / "AddonFire.json") == Preset("Fire") && Exists(dir.File("Ice")),
                  "onto an add-on file the panel knows by another name: refused");
        }
    }
}

int main(int argc, char** argv)
{
    if (argc != 2 || std::string_view(argv[1]) != "--self-test") {
        std::fprintf(stderr, "usage: preset-files-check --self-test\n");
        return kExitUsage;
    }
    try {
        PlainCase();
        FailedWriteCase();
        SameFileCase();
        CaseOnlyCase();
        LegacyCase();
        AsciiAddonCase();
        DeleteCase();
        TakenCase();
    } catch (const std::exception& e) {
        std::printf("FAIL exception: %s\n", e.what());
        return kExitFailed;
    }
    std::printf("preset-files-check: %s (%d failed)\n", g_failed == 0 ? "ALL PASS" : "FAILED", g_failed);
    return g_failed == 0 ? kExitOk : kExitFailed;
}
