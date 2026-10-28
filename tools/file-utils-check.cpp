// file-utils-check - offline check of FileUtils::WriteAtomically
// (plugins/spelllearning/include/FileUtils.h), the one writer of the plugin's files.
//
//   file-utils-check --self-test     writes in a temp folder, no game needed
//
// The file named is there at every moment: WriteAtomically copies it to <name>.bak (no
// step moves or removes it; the copy goes to <name>.bak.tmp and is moved onto the .bak)
// and moves <name>.tmp onto it, which needs a move that replaces an existing file in one
// step. Checked here: that std::filesystem::rename does replace an existing file on this
// build; a write over a file, with and without the .bak, and of a new file; two failed
// writes (the move refused while the file is open, the temp file not creatable) that
// leave the file as it was; and a .bak copy that fails, which leaves the old .bak whole.
//
// Exit 0 when every case passes, 1 on a failure, 2 on a bad argument.

#include "Common.h"

#include "FileUtils.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    constexpr int kExitOk = 0;
    constexpr int kExitFailed = 1;
    constexpr int kExitUsage = 2;

    struct Checker
    {
        int failed = 0;
        void Check(bool ok, const std::string& what)
        {
            if (!ok) ++failed;
            std::printf("    [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
        }
    };

    std::optional<std::string> Read(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return std::nullopt;
        std::ostringstream text;
        text << file.rdbuf();
        return std::move(text).str();
    }

    void Write(const fs::path& path, std::string_view text)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    bool Exists(const fs::path& path)
    {
        std::error_code ec;
        return fs::exists(path, ec);
    }

    /// A fresh folder of its own under the system temp folder, removed when done
    struct Folder
    {
        fs::path path;
        Folder()
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path = fs::temp_directory_path() / ("file-utils-check-" + std::to_string(stamp));
            fs::create_directories(path);
        }
        ~Folder()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    };

    int ReplaceCase()
    {
        std::printf("  a move onto an existing file replaces it (the step the rest stands on)\n");
        Checker check;
        Folder folder;
        const auto from = folder.path / "a.tmp", to = folder.path / "a.json";
        Write(from, "new");
        Write(to, "old");
        std::error_code ec;
        fs::rename(from, to, ec);
        check.Check(!ec, "std::filesystem::rename onto an existing file: no error" + (ec ? " (" + ec.message() + ")" : std::string()));
        check.Check(Read(to) == "new" && !Exists(from), "the target holds the moved file's content, the source is gone");
        return check.failed;
    }

    int WriteCase()
    {
        std::printf("  WriteAtomically over a file, without a .bak, and a new file\n");
        Checker check;
        Folder folder;
        const auto path = folder.path / "spell_tree.json";
        const auto backup = FileUtils::BackupPath(path), temp = FileUtils::TempPath(path);
        check.Check(backup.filename() == "spell_tree.json.bak" && temp.filename() == "spell_tree.json.tmp",
            "the .bak and .tmp names");

        Write(path, "old");
        Write(backup, "older");
        check.Check(FileUtils::WriteAtomically(path, "new"), "over a file: true");
        check.Check(Read(path) == "new", "the file holds the new content");
        check.Check(Read(backup) == "old", "the .bak holds the previous content (the older .bak replaced)");
        check.Check(!Exists(temp) && !Exists(FileUtils::TempPath(backup)), "no .tmp or .bak.tmp left");

        check.Check(FileUtils::WriteAtomically(path, "newer", false), "keepBackup false: true");
        check.Check(Read(path) == "newer" && Read(backup) == "old" && !Exists(temp), "keepBackup false: the .bak left as it was");

        const auto fresh = folder.path / "spell_tree_loadorder.json";
        check.Check(FileUtils::WriteAtomically(fresh, "{}") && Read(fresh) == "{}", "a new file: written");
        check.Check(!Exists(FileUtils::BackupPath(fresh)) && !Exists(FileUtils::TempPath(fresh)), "a new file: no .bak, no .tmp");

        check.Check(FileUtils::WriteAtomically(fresh, "") && Read(fresh) == std::string(), "empty content: an empty file");
        return check.failed;
    }

    int FailedCase()
    {
        std::printf("  a failed write leaves the file as it was\n");
        Checker check;
        Folder folder;
        const auto path = folder.path / "config.json";
        Write(path, "old");
        {
            // The CRT opens a stream without FILE_SHARE_DELETE: no move may replace the file meanwhile
            std::ifstream open(path, std::ios::binary);
            const bool wrote = FileUtils::WriteAtomically(path, "new");
            check.Check(!wrote, "the file open elsewhere: the move refused, false");
            check.Check(Exists(path), "the file is still there");
        }
        check.Check(Read(path) == "old", "with its old content");
        check.Check(!Exists(FileUtils::TempPath(path)), "the .tmp removed");

        // The temp file cannot be created (a folder holds its name): nothing else is touched
        const auto other = folder.path / "presets.json";
        Write(other, "old");
        fs::create_directories(FileUtils::TempPath(other));
        check.Check(!FileUtils::WriteAtomically(other, "new"), "the .tmp not creatable: false");
        check.Check(Read(other) == "old" && !Exists(FileUtils::BackupPath(other)), "the file as it was, no .bak made");

        // The .bak's copy cannot be made (a folder with a file in it holds <name>.bak.tmp): the write goes on
        const auto kept = folder.path / "spell_tree.json";
        const auto keptBackup = FileUtils::BackupPath(kept);
        Write(kept, "old");
        Write(keptBackup, "older");
        fs::create_directories(FileUtils::TempPath(keptBackup));
        Write(FileUtils::TempPath(keptBackup) / "x", "x");
        check.Check(FileUtils::WriteAtomically(kept, "new") && Read(kept) == "new", "the .bak.tmp not creatable: written all the same");
        check.Check(Read(keptBackup) == "older", "the old .bak whole (not half a copy)");
        return check.failed;
    }

    int SelfTest()
    {
        const std::vector<int (*)()> cases = { ReplaceCase, WriteCase, FailedCase };
        int failedCases = 0;
        for (auto* run : cases) {
            if (run() > 0) ++failedCases;
        }
        std::printf("self-test: %d of %zu cases failed\n", failedCases, cases.size());
        return failedCases == 0 ? kExitOk : kExitFailed;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2 || std::string_view(argv[1]) != "--self-test") {
        std::fprintf(stderr, "usage: file-utils-check --self-test\n");
        return kExitUsage;
    }
    spdlog::set_level(spdlog::level::off);
    return SelfTest();
}
