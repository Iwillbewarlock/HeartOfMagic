// =============================================================================
// BuildProgressCopy - one progress line's file staging -> Data, and the deferred list's path (BuildProgressCopy.h)
// =============================================================================

#include "BuildProgressCopy.h"

#include "BuildProgress.h"
#include "DataFiles.h"
#include "FileIndex.h"
#include "MenuWatch.h"
#include "NifDump.h"

#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace BlackFlame::BuildProgress
{
    namespace fs = std::filesystem;

    fs::path DeferredList()
    {
        return StagingFolder().parent_path() / kDeferredName;
    }

    // staging -> Data through the VFS: the staging bytes checked against the line, a temporary file, a replacing rename,
    // the Data file read back. Copies, never moves (the builder's compaction and cache need the staging files)
    bool CopyIn(const CC::ProgressLine& a_line, bool& a_deferred, std::string& a_why)
    {
        const auto data = FileIndex::DataFolder();
        if (!data || !DataPathRule::ParseCopyPath(a_line.path)) {
            a_why = "no Data folder, or not a copy path";
            return false;
        }
        std::ifstream in(StagingFolder() / fs::path(a_line.path), std::ios::binary);
        if (!in) {
            a_why = "not in staging";
            return false;
        }
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes.size() != a_line.bytes || NifDump::Fnv64(bytes.data(), bytes.size()) != a_line.fnv64) {
            a_why = "the staging file is not the line's (size or fnv64)";
            return false;
        }
        const auto target = *data / fs::path(a_line.path);
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        if (ec) {
            a_why = "its Data folder could not be made: " + ec.message();
            return false;
        }
        auto temp = target;
        temp += ".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            out.flush();
            if (!out) {
                a_why = "writing the temporary file failed";
                out.close();
                fs::remove(temp, ec);
                return false;
            }
        }
        if (!MenuWatch::PublishOpen()) {  // closed while the bytes were written: nothing enters Data during a load
            a_why = "the publish window closed during the copy";
            fs::remove(temp, ec);
            return false;
        }
        // review N3: a file there now is neither the line's nor a published build of its key (Settle: Matches, then
        // KeepPublished): it is never replaced while the game runs. The copy waits beside it as <path>.next and the next
        // start renames it in before anything uses the path (ApplyDeferred). A damaged or cut file, or (review round 1 D1:
        // GPU bytes differ from build to build, so one key no longer gives one file) a build of the key no ledger object
        // stands on
        if (fs::exists(target, ec) || ec) {
            auto next = target;
            next += kNextSuffix;
            if (!::MoveFileExW(temp.c_str(), next.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                a_why = std::format("the deferred copy could not be put beside it (Windows error {})", ::GetLastError());
                fs::remove(temp, ec);
                return false;
            }
            std::ofstream list(DeferredList(), std::ios::binary | std::ios::app);
            list << a_line.path << '\t' << a_line.bytes << '\t' << CC::Hex16(a_line.fnv64) << '\n';
            list.flush();
            a_deferred = true;
            a_why = list ? "another file is at its path: the copy is renamed in at the next start" :
                           "another file is at its path, and the deferred list could not be written";
            return false;
        }
        if (!::MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            a_why = std::format("the rename failed (Windows error {})", ::GetLastError());
            fs::remove(temp, ec);
            return false;
        }
        DataFiles::Forget(a_line.path);
        if (!DataFiles::Matches(a_line.path, a_line.bytes, a_line.fnv64)) {
            a_why = "the Data file does not read back as the line's";
            return false;
        }
        return true;
    }
}
