#pragma once

// =============================================================================
// OLD COPIES FILES - the old-copies cleanup's file side (no game types, no logger; OldCopies.h)
// =============================================================================
// C++17, std::filesystem and the Win32 file API (Windows.h through DataPathRule.h), so tools/blackflame-cosave-check tests
// it in a temporary folder with a junction (OldCopiesFilesTests.h):
//   root     RootSafe: the base (the Data or the staging folder) itself and every folder from it down to the copies root is a
//            plain folder (no reparse point, no file); the folders above the base are not looked at (a Steam library or a
//            Documents folder behind a junction is the game's own layout, and nothing is removed outside the base)
//   folder   ListFolder: a key folder that is a plain folder holding plain files only (no sub-folder, no link), with its
//            files' sizes and its newest file's write time (the grace); RemoveListed: those files, then the folder
//   vfs      FinalPath: the file's real path (CreateFileW with FILE_FLAG_BACKUP_SEMANTICS, GetFinalPathNameByHandleW, the
//            "\\?\" prefix taken off); RootFromSample and Under: the MO2 overwrite folder from a sample file and the test
//            that a real path is under it (component by component, case folded by CompareStringOrdinal)

#include "DataPathRule.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace BlackFlame::OldCopiesFiles
{
    namespace fs = std::filesystem;

    [[nodiscard]] inline std::string Text(const fs::path& a_path)
    {
        const auto text = a_path.u8string();
        return std::string(text.begin(), text.end());
    }

    enum class Kind : std::uint8_t
    {
        kAbsent,       // not there (file or path not found)
        kPlainFolder,  // a folder that is no reparse point
        kOther,        // a file, a link, a junction, or attributes that could not be read
    };

    [[nodiscard]] inline Kind KindOf(const fs::path& a_path)
    {
        const auto attributes = ::GetFileAttributesW(a_path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const auto error = ::GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? Kind::kAbsent : Kind::kOther;
        }
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 ? Kind::kPlainFolder :
                                                                                                                 Kind::kOther;
    }

    /// a_base (absolute, lexically normal) and each folder from it down to a_root (under a_base) is a plain folder; an absent
    /// one ends the walk with a_present false (nothing under it). False with a_why for anything else
    [[nodiscard]] inline bool RootSafe(const fs::path& a_base, const fs::path& a_root, bool& a_present, std::string& a_why)
    {
        a_present = false;
        if (!a_base.is_absolute() || a_base.lexically_normal() != a_base) {
            a_why = "'" + Text(a_base) + "' is not an absolute normal path";
            return false;
        }
        const auto relative = a_root.lexically_relative(a_base);
        if (relative.empty() || relative == fs::path(".") || *relative.begin() == "..") {
            a_why = "'" + Text(a_root) + "' is not under '" + Text(a_base) + "'";
            return false;
        }
        fs::path at = a_base;
        const auto check = [&](const fs::path& a_at) {
            const auto kind = KindOf(a_at);
            if (kind == Kind::kOther) {
                a_why = "'" + Text(a_at) + "' is not a plain folder (a file, a link or a junction): not cleaned";
            }
            return kind;
        };
        auto kind = check(at);
        for (auto part = relative.begin(); kind == Kind::kPlainFolder && part != relative.end(); ++part) {
            at /= *part;
            kind = check(at);
        }
        a_present = kind == Kind::kPlainFolder;
        return kind != Kind::kOther;
    }

    struct Listing
    {
        std::vector<std::pair<fs::path, std::uint64_t>> files;  // each file and its size
        std::uint64_t newest = 0;                               // the newest file's last write (FILETIME ticks; 0: no file)
        std::uint64_t bytes = 0;
    };

    enum class Listed : std::uint8_t
    {
        kPlain,     // a plain folder of plain files: a_out holds them
        kNotPlain,  // no plain folder, or something other than a plain file in it: left whole
        kFailed,    // it could not be listed (a_why)
    };

    [[nodiscard]] inline std::uint64_t Ticks(const FILETIME& a_time)
    {
        return (static_cast<std::uint64_t>(a_time.dwHighDateTime) << 32) | a_time.dwLowDateTime;
    }

    [[nodiscard]] inline std::uint64_t NowTicks()
    {
        FILETIME now{};
        ::GetSystemTimeAsFileTime(&now);
        return Ticks(now);
    }

    [[nodiscard]] inline Listed ListFolder(const fs::path& a_folder, Listing& a_out, std::string& a_why)
    {
        a_out = {};
        if (KindOf(a_folder) != Kind::kPlainFolder) {
            return Listed::kNotPlain;
        }
        std::error_code ec;
        for (fs::directory_iterator it(a_folder, ec), end; !ec && it != end; it.increment(ec)) {
            const auto file = it->path();
            WIN32_FILE_ATTRIBUTE_DATA data{};
            if (!::GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data)) {
                a_why = "'" + Text(file.filename()) + "' could not be looked at (Windows error " + std::to_string(::GetLastError()) + ")";
                return Listed::kFailed;
            }
            if ((data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 || file.parent_path() != a_folder) {
                return Listed::kNotPlain;  // a sub-folder or a link inside: the folder is left whole
            }
            const auto size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            a_out.files.emplace_back(file, size);
            a_out.bytes += size;
            a_out.newest = (std::max)(a_out.newest, Ticks(data.ftLastWriteTime));
        }
        if (ec) {
            a_why = "it could not be listed: " + ec.message();
            return Listed::kFailed;
        }
        return Listed::kPlain;
    }

    /// a_listing's files, then a_folder (never remove_all, never a link followed). a_bytes: what went, also on a failure
    [[nodiscard]] inline bool RemoveListed(const fs::path& a_folder, const Listing& a_listing, std::uint64_t& a_bytes, std::string& a_why)
    {
        std::error_code ec;
        for (const auto& [file, size] : a_listing.files) {
            if (!fs::remove(file, ec) || ec) {
                a_why = "'" + Text(file.filename()) + "' could not be removed: " + (ec ? ec.message() : std::string("it was gone"));
                return false;
            }
            a_bytes += size;
        }
        if (!fs::remove(a_folder, ec) || ec) {
            a_why = "its files are removed but the folder is not: " + (ec ? ec.message() : std::string("it was gone"));
            return false;
        }
        return true;
    }

    /// GetFinalPathNameByHandleW's text without "\\?\" ("\\?\UNC\server\x" -> "\\server\x")
    [[nodiscard]] inline std::wstring StripFinalPrefix(std::wstring a_text)
    {
        const std::wstring unc = L"\\\\?\\UNC\\";
        const std::wstring local = L"\\\\?\\";
        if (a_text.compare(0, unc.size(), unc) == 0) {
            return L"\\\\" + a_text.substr(unc.size());
        }
        if (a_text.compare(0, local.size(), local) == 0) {
            return a_text.substr(local.size());
        }
        return a_text;
    }

    /// a_path's real path (a file or a folder), or nullopt with a_why
    [[nodiscard]] inline std::optional<fs::path> FinalPath(const fs::path& a_path, std::string& a_why)
    {
        const HANDLE handle = ::CreateFileW(a_path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            a_why = "it does not open (Windows error " + std::to_string(::GetLastError()) + ")";
            return std::nullopt;
        }
        std::wstring text(MAX_PATH, L'\0');
        DWORD length = ::GetFinalPathNameByHandleW(handle, text.data(), static_cast<DWORD>(text.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (length >= text.size()) {  // too small: the length asked for, with its NUL
            text.assign(length, L'\0');
            length = ::GetFinalPathNameByHandleW(handle, text.data(), static_cast<DWORD>(text.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        }
        const DWORD error = ::GetLastError();
        ::CloseHandle(handle);
        if (length == 0 || length >= text.size()) {
            a_why = "GetFinalPathNameByHandleW failed (Windows error " + std::to_string(error) + ")";
            return std::nullopt;
        }
        text.resize(length);
        return fs::path(StripFinalPrefix(std::move(text))).lexically_normal();
    }

    [[nodiscard]] inline bool SameName(const fs::path& a_left, const fs::path& a_right)
    {
        const auto& l = a_left.native();
        const auto& r = a_right.native();
        return ::CompareStringOrdinal(l.c_str(), static_cast<int>(l.size()), r.c_str(), static_cast<int>(r.size()), TRUE) == CSTR_EQUAL;
    }

    [[nodiscard]] inline std::vector<fs::path> Parts(const fs::path& a_path)
    {
        std::vector<fs::path> parts;
        for (const auto& part : a_path.lexically_normal()) {
            if (!part.empty()) {
                parts.push_back(part);
            }
        }
        return parts;
    }

    /// The folder a_final (a sample file's real path) is a_relative (its Data-relative path) under: a_final with
    /// a_relative's parts taken off its end, each matched (case folded). nullopt when they do not match
    [[nodiscard]] inline std::optional<fs::path> RootFromSample(const fs::path& a_final, const fs::path& a_relative)
    {
        const auto finalParts = Parts(a_final);
        const auto relativeParts = Parts(a_relative);
        if (relativeParts.empty() || relativeParts.size() >= finalParts.size() || !a_final.is_absolute()) {
            return std::nullopt;
        }
        const auto offset = finalParts.size() - relativeParts.size();
        for (std::size_t i = 0; i < relativeParts.size(); ++i) {
            if (!SameName(finalParts[offset + i], relativeParts[i])) {
                return std::nullopt;
            }
        }
        fs::path root;
        for (std::size_t i = 0; i < offset; ++i) {
            root /= finalParts[i];
        }
        return root;
    }

    /// a_path is a_root or under it, component by component (case folded); a_strict: under it only
    [[nodiscard]] inline bool Under(const fs::path& a_root, const fs::path& a_path, bool a_strict = true)
    {
        const auto rootParts = Parts(a_root);
        const auto pathParts = Parts(a_path);
        if (rootParts.empty() || pathParts.size() < rootParts.size() + (a_strict ? 1 : 0)) {
            return false;
        }
        for (std::size_t i = 0; i < rootParts.size(); ++i) {
            if (!SameName(rootParts[i], pathParts[i])) {
                return false;
            }
        }
        return true;
    }
}
