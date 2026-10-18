#pragma once

// =============================================================================
// COPY CONTRACT IDENTITY - the builder's output identity in its exe (RCDATA), as the DLL reads it
// =============================================================================
// The builder exe carries its output identity text ("<n>" or "<n>+dev<16 hex>", CopyContractKeys.h
// OutputIdentityVersion) as the RCDATA resource BLACKFLAME_BUILDER_IDENTITY_RESOURCE: the bytes of the text, no NUL
// (tools/blackflame-greybuild/GreyIdentity.cmake writes the .rc and the builder's compiled copy from one string). The
// DLL reads it at kDataLoaded with ReadIdentityResource (the exe loaded as a data file: never run, kernel32 only),
// checks it with BuilderIdentityFault and writes it verbatim into the key prefix; the builder rebuilds the prefix from its
// compiled copy, so the two match. Compiled by the DLL (C++23), the builder (C++17, its --self-test reads its own exe
// through the same function) and tools/blackflame-cosave-check (the failure paths). rc.exe includes this file too: only
// the #define stands outside RC_INVOKED.

#define BLACKFLAME_BUILDER_IDENTITY_RESOURCE 4101

#ifndef RC_INVOKED

#    include "CopyContractKeys.h"

#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>

#    include <cstddef>
#    include <cstdint>
#    include <filesystem>
#    include <optional>
#    include <string>
#    include <string_view>
#    include <system_error>

namespace BlackFlame::CopyContract
{
    inline constexpr int kBuilderIdentityResource = BLACKFLAME_BUILDER_IDENTITY_RESOURCE;
    // Longer than any identity ("4294967295+dev" + 16 hex = 30 bytes): read as malformed, not copied
    inline constexpr std::size_t kMaxIdentityResourceBytes = 64;

    struct IdentityResource
    {
        std::optional<std::string> text;  // the resource's bytes as they are (not validated here)
        std::string why;                  // nullopt text: why, worded after "the builder exe" ("has no ...")
    };

    /// a_path as LoadLibraryExW takes it (review E2 #4): absolute, so a relative path never goes through the module
    /// search, with '\' separators (LoadLibraryExW does not take '/'); empty when a_path is empty or does not resolve
    [[nodiscard]] inline std::filesystem::path DataFileLoadPath(const std::filesystem::path& a_path)
    {
        if (a_path.empty()) {
            return {};
        }
        std::error_code ec;
        auto absolute = std::filesystem::absolute(a_path, ec);
        if (ec) {
            return {};
        }
        return absolute.make_preferred();
    }

    /// The identity resource of the exe at a_exe (DataFileLoadPath first), its bytes verbatim (SizeofResource, never a
    /// NUL search); the module is released however the read ends
    [[nodiscard]] inline IdentityResource ReadIdentityResource(const std::filesystem::path& a_exe)
    {
        struct Module
        {
            HMODULE handle = nullptr;
            ~Module()
            {
                if (handle) {
                    ::FreeLibrary(handle);
                }
            }
        };
        IdentityResource out;
        const auto path = DataFileLoadPath(a_exe);
        if (path.empty()) {
            out.why = "has a path that does not resolve to an absolute one";  // no path text: u8string differs in C++17 and 20
            return out;
        }
        const Module module{ ::LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE) };
        if (!module.handle) {
            const DWORD error = ::GetLastError();
            out.why = "does not load as a data file (Windows error " + std::to_string(error) + ")";
            return out;
        }
        const HRSRC info = ::FindResourceW(module.handle, MAKEINTRESOURCEW(kBuilderIdentityResource), RT_RCDATA);
        if (!info) {
            out.why = "has no output identity resource (RCDATA " + std::to_string(kBuilderIdentityResource) +
                      "): a builder from before key format BFK4, or another exe";
            return out;
        }
        const DWORD size = ::SizeofResource(module.handle, info);
        const HGLOBAL loaded = ::LoadResource(module.handle, info);
        const void* bytes = loaded ? ::LockResource(loaded) : nullptr;
        if (!bytes || size == 0 || size > kMaxIdentityResourceBytes) {
            out.why = "has an output identity resource that does not read (" + std::to_string(size) + " bytes)";
            return out;
        }
        out.text = std::string(static_cast<const char*>(bytes), size);
        return out;
    }

    /// The DLL's check of the text it read: nullopt for a valid identity whose number is a_ownVersion (the DLL's own
    /// kBuilderOutputVersion: the request and progress contract it was built with), else what is wrong, worded after "the
    /// builder exe" ("is of output version 2 ...")
    [[nodiscard]] inline std::optional<std::string> BuilderIdentityFault(std::string_view a_text,
        std::uint32_t a_ownVersion = kBuilderOutputVersion)
    {
        const auto version = OutputIdentityVersion(a_text);
        if (!version) {
            const std::string shown = Detail::IsPrintableAscii(a_text) ? std::string(a_text) : "hex " + LowerHex(a_text);
            return "has a malformed output identity '" + shown + "'";
        }
        if (*version != a_ownVersion) {
            return "is of output version " + std::to_string(*version) + " and this DLL of " + std::to_string(a_ownVersion) +
                   " (the DLL and the builder are from two releases)";
        }
        return std::nullopt;
    }
}

#endif
