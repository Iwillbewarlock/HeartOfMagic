#pragma once

// =============================================================================
// DUMP SWEEP - the dump files no manifest stamp names, removed (disk trim, 2026-10-26; blackflame-cosave-check tests it)
// =============================================================================
// The dump only visits the paths its plan names, so the files of sources a later load order no longer plans stayed under
// data\ for good (the author's first run of the disk trim: 149 files, 45 MiB). CopyRequestDump::RemoveUnstamped reads the
// manifest and calls this. Paths compare as Windows compares file names (the invariant upper case of their UTF-16, review
// Codex: "Éffects" and "éffects" are one folder). Nothing under a reparse point (link, junction) is looked at or removed,
// the root included (review Codex). C++17 (the tools build it).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>

namespace BlackFlame::DumpSweep
{
    struct Swept
    {
        std::size_t files = 0;
        std::uint64_t bytes = 0;
        std::size_t failed = 0;
        bool refused = false;  // the root is a reparse point (or does not read as a folder): nothing looked at
    };

    /// a_path as Windows compares file names: '/' as '\', then the invariant upper case
    [[nodiscard]] inline std::wstring FoldName(std::wstring a_path)
    {
        for (auto& c : a_path) {
            c = c == L'/' ? L'\\' : c;
        }
        if (a_path.empty()) {
            return a_path;
        }
        std::wstring out(a_path.size(), L'\0');
        const int made = ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, a_path.data(), static_cast<int>(a_path.size()),
            out.data(), static_cast<int>(out.size()), nullptr, nullptr, 0);
        return made == static_cast<int>(a_path.size()) ? out : a_path;
    }

    /// A UTF-8 stamp path folded as FoldName
    [[nodiscard]] inline std::wstring FoldUtf8(const std::string& a_utf8)
    {
        if (a_utf8.empty()) {
            return {};
        }
        const int length = ::MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(length > 0 ? length : 0), L'\0');
        if (length > 0) {
            ::MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), wide.data(), length);
        }
        return FoldName(std::move(wide));
    }

    [[nodiscard]] inline bool IsReparsePoint(const std::filesystem::path& a_path)
    {
        const DWORD attributes = ::GetFileAttributesW(a_path.c_str());
        return attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    }

    /// Every plain file under a_root whose path relative to it is not in a_stamped (UTF-8 stamp paths) deleted, then the
    /// folders under a_root left empty (a_root itself stays)
    [[nodiscard]] inline Swept RemoveUnstamped(const std::filesystem::path& a_root, const std::set<std::string>& a_stamped)
    {
        namespace fs = std::filesystem;
        Swept out;
        std::error_code ec;
        if (!fs::exists(a_root, ec) || ec) {
            return out;  // no dump folder: nothing to do
        }
        if (IsReparsePoint(a_root) || !fs::is_directory(a_root, ec) || ec) {
            out.refused = true;
            return out;
        }
        std::set<std::wstring> stamped;
        for (const auto& path : a_stamped) {
            stamped.insert(FoldUtf8(path));
        }
        std::vector<fs::path> files;
        std::vector<fs::path> folders;
        for (fs::recursive_directory_iterator it(a_root, fs::directory_options::none, ec), end; !ec && it != end; it.increment(ec)) {
            if (IsReparsePoint(it->path())) {
                it.disable_recursion_pending();  // a link or junction (MSVC's is_symlink misses junctions): left alone
                continue;
            }
            std::error_code typeEc;
            if (it->is_directory(typeEc)) {
                folders.push_back(it->path());
            } else if (it->is_regular_file(typeEc) &&
                       stamped.count(FoldName(it->path().lexically_relative(a_root).wstring())) == 0) {
                files.push_back(it->path());
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
