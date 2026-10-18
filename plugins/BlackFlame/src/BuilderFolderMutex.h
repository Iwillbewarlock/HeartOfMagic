#pragma once

// =============================================================================
// BUILDER FOLDER MUTEX - one builder per request folder across game processes
// =============================================================================
// The DLL creates the named mutex Local\HoMBlackFlame_Builder_<key16> once a session (BuilderLauncher.cpp ClaimLocked, called by
// BuilderLauncher::ClaimFolder at the slot route's start, before the request, the dump or a deferred copy is touched; StartChecked
// reuses that claim before it rotates builder.log and starts the builder; reviewer round 2), and holds it for the game's lifetime (a handle never closed: the system closes it
// when the game ends, when the job's kill-on-close also ends that game's builder). key16 is the FNV-1a 64 (CopyContract.h)
// of the request folder's path normalised: absolute, lexically normal, '/' read as '\', no trailing '\', lower case
// (LCMapStringEx, the invariant locale), as UTF-8 bytes. A create that finds the mutex there (ERROR_ALREADY_EXISTS; or
// ERROR_ACCESS_DENIED: one a process of another integrity level made) means another game process on the same folder:
// builder.log is not rotated and no second builder starts on its staging folder. It replaced a probe of builder.log held
// for write (review round 2 of 0a878fd2: two games starting at once raced past it, and a log viewer read as a live
// builder). Known limits: Local\ is one logon session's namespace (two users' sessions on one folder are not excluded);
// one folder reached by two paths (a junction, a subst drive, an 8.3 name) gives two names; a builder run by hand makes
// no mutex. Header only, Windows, C++17: the DLL and tools/blackflame-cosave-check (BuilderControlTests.h).

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>

#include "CopyContract.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace BlackFlame::BuilderFolderMutex
{
    inline constexpr const wchar_t* kNamePrefix = L"Local\\HoMBlackFlame_Builder_";

    /// The request folder's path as the name hashes it (header comment): UTF-8, lower case; "" when it does not convert
    [[nodiscard]] inline std::string NormalisedFolder(const std::filesystem::path& a_folder)
    {
        std::error_code ec;
        const auto absolute = std::filesystem::absolute(a_folder, ec);
        std::wstring text = (ec ? a_folder : absolute).lexically_normal().wstring();
        for (auto& c : text) {
            c = c == L'/' ? L'\\' : c;
        }
        while (text.size() > 1 && text.back() == L'\\') {
            text.pop_back();
        }
        if (text.empty()) {
            return {};
        }
        std::wstring lower(text.size(), L'\0');
        const int lowered = ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.c_str(), static_cast<int>(text.size()), lower.data(),
            static_cast<int>(lower.size()), nullptr, nullptr, 0);
        if (lowered <= 0) {
            return {};
        }
        lower.resize(static_cast<std::size_t>(lowered));
        const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, lower.c_str(), static_cast<int>(lower.size()), nullptr, 0, nullptr, nullptr);
        if (bytes <= 0) {
            return {};
        }
        std::string out(static_cast<std::size_t>(bytes), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, lower.c_str(), static_cast<int>(lower.size()), out.data(), bytes, nullptr, nullptr);
        return out;
    }

    /// Local\HoMBlackFlame_Builder_<FNV-1a 64 of NormalisedFolder, 16 lower-case hex digits>
    [[nodiscard]] inline std::wstring MutexName(const std::filesystem::path& a_folder)
    {
        const auto key = CopyContract::Hex16(CopyContract::Fnv64(NormalisedFolder(a_folder)));
        return std::wstring(kNamePrefix) + std::wstring(key.begin(), key.end());
    }

    enum class Claim : std::uint8_t
    {
        kOwned,      // made by this process: it holds the folder
        kOtherGame,  // there already (another game process on the same folder): no rotation, no builder
        kUnknown     // not made for another reason: not excluded, the builder starts as before the mutex
    };

    /// The claim for CreateMutexW's result: a_made (a handle came back) and GetLastError() right after it
    [[nodiscard]] inline Claim FromCreate(bool a_made, DWORD a_error) noexcept
    {
        if (a_made) {
            return a_error == ERROR_ALREADY_EXISTS ? Claim::kOtherGame : Claim::kOwned;
        }
        return a_error == ERROR_ACCESS_DENIED ? Claim::kOtherGame : Claim::kUnknown;
    }

    [[nodiscard]] inline bool MayStart(Claim a_claim) noexcept
    {
        return a_claim != Claim::kOtherGame;
    }

    /// Creates a_folder's mutex (not owned: its existence is the claim). kOwned: a_mutex is the handle to hold for the
    /// game's lifetime; otherwise a_mutex is null (a handle to another process's mutex is closed at once). a_error gets
    /// the Windows error of the create
    [[nodiscard]] inline Claim Acquire(const std::filesystem::path& a_folder, HANDLE& a_mutex, DWORD& a_error) noexcept
    {
        a_mutex = nullptr;
        std::wstring name;
        try {
            name = MutexName(a_folder);
        } catch (...) {
            a_error = ERROR_OUTOFMEMORY;
            return Claim::kUnknown;
        }
        ::SetLastError(ERROR_SUCCESS);  // a new mutex need not reset it
        const HANDLE handle = ::CreateMutexW(nullptr, FALSE, name.c_str());
        a_error = ::GetLastError();
        const auto claim = FromCreate(handle != nullptr, a_error);
        if (claim == Claim::kOwned) {
            a_mutex = handle;
        } else if (handle) {
            ::CloseHandle(handle);
        }
        return claim;
    }
}
