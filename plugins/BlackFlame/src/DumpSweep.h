#pragma once

// =============================================================================
// DUMP SWEEP - the dump files no manifest stamp names, removed (disk trim, 2026-10-26; blackflame-cosave-check tests it)
// =============================================================================
// The dump only visits the paths its plan names, so the files of sources a later load order no longer plans stayed under
// data\ for good (the author's first run of the disk trim: 149 files, 45 MiB). CopyRequestDump::RemoveUnstamped reads the
// manifest and calls this. C++17 (the tools build it).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace BlackFlame::DumpSweep
{
    struct Swept
    {
        std::size_t files = 0;
        std::uint64_t bytes = 0;
        std::size_t failed = 0;
    };

    [[nodiscard]] inline std::string LowerAscii(std::string a_text)
    {
        for (auto& c : a_text) {
            c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
        return a_text;
    }

    /// Every plain file under a_root whose path relative to it (UTF-8, '\' separators, compared without ASCII case) is not in
    /// a_stamped (already lower-cased) deleted, then the folders under a_root left empty (a_root itself stays). A link is
    /// never followed or removed
    [[nodiscard]] inline Swept RemoveUnstamped(const std::filesystem::path& a_root, const std::set<std::string>& a_stamped)
    {
        namespace fs = std::filesystem;
        Swept out;
        std::error_code ec;
        std::vector<fs::path> files;
        std::vector<fs::path> folders;
        for (fs::recursive_directory_iterator it(a_root, fs::directory_options::none, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code typeEc;
            if (it->is_symlink(typeEc)) {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_directory(typeEc)) {
                folders.push_back(it->path());
            } else if (it->is_regular_file(typeEc)) {
                auto relative = it->path().lexically_relative(a_root).make_preferred().u8string();
                if (a_stamped.count(LowerAscii(std::string(relative.begin(), relative.end()))) == 0) {
                    files.push_back(it->path());
                }
            }
        }
        for (const auto& file : files) {
            std::error_code fileEc;
            const auto size = fs::file_size(file, fileEc);
            if (fs::remove(file, fileEc) && !fileEc) {
                ++out.files;
                out.bytes += size;
            } else {
                ++out.failed;
            }
        }
        for (auto it = folders.rbegin(); it != folders.rend(); ++it) {  // a folder comes before its children: deepest first
            std::error_code folderEc;
            if (fs::is_empty(*it, folderEc) && !folderEc) {
                fs::remove(*it, folderEc);  // an empty folder only, never remove_all
            }
        }
        return out;
    }
}
